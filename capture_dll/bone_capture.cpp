/**
 * MGS2 Animation Capture - Bone Capture Implementation
 */

#include "bone_capture.h"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace MGS2Capture {

void BoneCaptureManager::StartCapture(const std::string& output_path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_capturing) return;

    m_output_path = output_path;
    m_capturing = true;
    m_frame_index = 0;
    m_frames.clear();
    m_start_time = std::chrono::high_resolution_clock::now();
    m_metadata.capture_timestamp = GetTimestamp();
    m_current_frame = {};

    // Log start
    OutputDebugStringA("[MGS2Capture] Capture started\n");
}

void BoneCaptureManager::StopCapture() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_capturing) return;

    // Finalize last frame
    FinalizeCurrentFrame();
    
    m_capturing = false;
    m_metadata.total_frames = static_cast<int>(m_frames.size());
    m_metadata.bone_count = m_bone_count;

    // Auto-export on stop - create folder with all Blender-ready files
    if (!m_output_path.empty()) {
        ExportToFolder(m_output_path);
    }

    OutputDebugStringA("[MGS2Capture] Capture stopped\n");
}

void BoneCaptureManager::OnConstantBufferUpdate(
    ID3D11DeviceContext* context,
    ID3D11Resource* dst_resource,
    UINT dst_subresource,
    const D3D11_BOX* dst_box,
    const void* src_data,
    UINT src_row_pitch,
    UINT src_depth_pitch
) {
    if (!m_capturing || !src_data) return;

    // Get buffer description to check size
    ID3D11Buffer* buffer = nullptr;
    if (FAILED(dst_resource->QueryInterface(&buffer))) return;
    
    D3D11_BUFFER_DESC desc;
    buffer->GetDesc(&desc);
    buffer->Release();

    // Check if this looks like a bone matrix buffer
    if (!IsBoneMatrixBuffer(desc.ByteWidth, m_bone_count)) return;

    // Calculate actual bone count from buffer size
    int actual_bone_count = desc.ByteWidth / 64;  // 64 bytes per 4x4 matrix
    
    // Only capture the largest buffer per frame (most likely the full skeleton)
    std::lock_guard<std::mutex> lock(m_mutex);
    if (actual_bone_count > static_cast<int>(m_current_frame.bone_matrices.size())) {
        m_current_frame.bone_matrices = ParseBoneMatrices(src_data, actual_bone_count);
    }
}

void BoneCaptureManager::OnPresent() {
    if (!m_capturing) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    FinalizeCurrentFrame();
    
    // Start new frame
    m_current_frame = {};
    m_current_frame.frame_index = m_frame_index;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(now - m_start_time);
    m_current_frame.timestamp_ms = duration.count();
}

void BoneCaptureManager::CaptureBoneMatrices(const void* data, int bone_count) {
    if (!m_capturing || !data || bone_count <= 0) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Track buffer size for model identification
    UINT buffer_size = bone_count * 64;
    m_seen_buffer_sizes.insert(buffer_size);
    
    // Only capture the largest buffer per frame (most likely the full skeleton)
    if (bone_count > static_cast<int>(m_current_frame.bone_matrices.size())) {
        m_current_frame.bone_matrices = ParseBoneMatrices(data, bone_count);
    }
}

void BoneCaptureManager::CaptureCamera(const void* view_matrix, const void* proj_matrix) {
    if (!m_capturing) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (view_matrix) {
        memcpy(&m_current_frame.camera.view_matrix, view_matrix, sizeof(DirectX::XMFLOAT4X4));
        m_current_frame.camera.has_data = true;
    }
    if (proj_matrix) {
        memcpy(&m_current_frame.camera.projection_matrix, proj_matrix, sizeof(DirectX::XMFLOAT4X4));
        m_current_frame.camera.has_data = true;
    }
}

void BoneCaptureManager::FinalizeCurrentFrame() {
    if (m_current_frame.bone_matrices.empty()) return;
    
    m_frames.push_back(m_current_frame);
    m_frame_index++;
}

void BoneCaptureManager::SetSkeleton(const std::vector<BoneDefinition>& skeleton) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_skeleton = skeleton;
    m_bone_count = static_cast<int>(skeleton.size());
}

std::string BoneCaptureManager::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

void BoneCaptureManager::ExportToJSON(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        OutputDebugStringA("[MGS2Capture] Failed to open output file\n");
        return;
    }

    // Write metadata
    file << "{\n";
    file << "  \"metadata\": {\n";
    file << "    \"game\": \"" << m_metadata.game_name << "\",\n";
    file << "    \"capture_timestamp\": \"" << m_metadata.capture_timestamp << "\",\n";
    file << "    \"frame_rate\": " << std::fixed << std::setprecision(1) << m_metadata.frame_rate << ",\n";
    file << "    \"total_frames\": " << m_metadata.total_frames << ",\n";
    file << "    \"bone_count\": " << m_metadata.bone_count << ",\n";
    file << "    \"coordinate_system\": {\n";
    file << "      \"handedness\": \"" << m_metadata.handedness << "\",\n";
    file << "      \"up_axis\": \"" << m_metadata.up_axis << "\",\n";
    file << "      \"matrix_format\": \"" << m_metadata.matrix_format << "\",\n";
    file << "      \"unit_scale\": " << m_metadata.unit_scale << "\n";
    file << "    }\n";
    file << "  },\n";

    // Write skeleton
    file << "  \"skeleton\": {\n";
    file << "    \"bones\": [\n";
    for (size_t i = 0; i < m_skeleton.size(); ++i) {
        const auto& bone = m_skeleton[i];
        file << "      {\"index\": " << bone.index 
             << ", \"name\": \"" << bone.name 
             << "\", \"parent\": " << bone.parent_index << "}";
        if (i < m_skeleton.size() - 1) file << ",";
        file << "\n";
    }
    file << "    ]\n";
    file << "  },\n";

    // Write frames
    file << "  \"frames\": [\n";
    for (size_t f = 0; f < m_frames.size(); ++f) {
        const auto& frame = m_frames[f];
        file << "    {\n";
        file << "      \"frame_index\": " << frame.frame_index << ",\n";
        file << "      \"timestamp_ms\": " << std::fixed << std::setprecision(3) << frame.timestamp_ms << ",\n";
        file << "      \"bone_matrices\": [\n";
        
        for (size_t b = 0; b < frame.bone_matrices.size(); ++b) {
            const auto& bm = frame.bone_matrices[b];
            file << "        {\n";
            file << "          \"bone_index\": " << bm.bone_index << ",\n";
            file << "          \"matrix\": [\n";
            
            // Output 4x4 matrix row by row
            const float* m = reinterpret_cast<const float*>(&bm.matrix);
            for (int row = 0; row < 4; ++row) {
                file << "            [";
                for (int col = 0; col < 4; ++col) {
                    float val = m[row * 4 + col];
                    // Sanitize NaN and Infinity values (invalid in JSON)
                    if (!std::isfinite(val)) val = 0.0f;
                    file << std::fixed << std::setprecision(6) << val;
                    if (col < 3) file << ", ";
                }
                file << "]";
                if (row < 3) file << ",";
                file << "\n";
            }
            
            file << "          ]\n";
            file << "        }";
            if (b < frame.bone_matrices.size() - 1) file << ",";
            file << "\n";
        }
        
        file << "      ]\n";
        file << "    }";
        if (f < m_frames.size() - 1) file << ",";
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    file.close();

    std::ostringstream msg;
    msg << "[MGS2Capture] Exported " << m_frames.size() << " frames to " << filepath << "\n";
    OutputDebugStringA(msg.str().c_str());
}

void BoneCaptureManager::ExportToFolder(const std::string& folder_path) {
    // Create the capture folder
    CreateDirectoryA(folder_path.c_str(), nullptr);
    
    // Export JSON data
    std::string json_path = folder_path + "\\animation_data.json";
    ExportToJSON(json_path);
    
    // Create Blender import script
    std::string script_path = folder_path + "\\import_to_blender.py";
    std::ofstream script(script_path);
    if (script.is_open()) {
        script << R"BLENDER_SCRIPT("""
MGS2 Animation Import Script for Blender
Generated by MGS2 Animation Capture Tool

To use:
1. Open Blender
2. Go to Scripting tab
3. Open this file and click Run Script
4. Or run from command line: blender --python import_to_blender.py
"""

import bpy
import json
import os
from mathutils import Matrix, Vector, Quaternion
import math

# HARDCODED PATH - Change this if your capture folder is different
JSON_FILE = r"D:\Metal Gear Solid 2\MGS2_Capture\animation_data.json"

# Alternative: Try to detect from script location (may not work in Blender text editor)
try:
    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))
    alt_path = os.path.join(script_dir, "animation_data.json")
    if os.path.exists(alt_path):
        JSON_FILE = alt_path
except:
    pass  # Use hardcoded path

# Coordinate system conversion (DirectX left-handed Y-up to Blender right-handed Z-up)
def convert_matrix(dx_matrix):
    """Convert DirectX row-major matrix to Blender column-major matrix"""
    # Convert from list to Matrix
    m = Matrix([dx_matrix[0], dx_matrix[1], dx_matrix[2], dx_matrix[3]]).transposed()
    
    # Swap Y and Z axes (Y-up to Z-up)
    conversion = Matrix([
        [1, 0, 0, 0],
        [0, 0, 1, 0],
        [0, 1, 0, 0],
        [0, 0, 0, 1]
    ])
    return conversion @ m @ conversion.inverted()

def create_armature(name, bone_count):
    """Create a simple armature with the required number of bones"""
    bpy.ops.object.armature_add(enter_editmode=True)
    armature = bpy.context.object
    armature.name = name
    
    # Clear default bone
    bpy.ops.armature.select_all(action='SELECT')
    bpy.ops.armature.delete()
    
    # Create bones
    amt = armature.data
    for i in range(bone_count):
        bone = amt.edit_bones.new(f"bone_{i:03d}")
        bone.head = (0, 0, i * 0.1)
        bone.tail = (0, 0, i * 0.1 + 0.05)
    
    bpy.ops.object.mode_set(mode='OBJECT')
    return armature

def apply_animation(armature, frames_data, fps=60):
    """Apply animation data to the armature"""
    scene = bpy.context.scene
    scene.render.fps = fps
    scene.frame_start = 0
    scene.frame_end = len(frames_data) - 1
    
    # Create action
    if armature.animation_data is None:
        armature.animation_data_create()
    
    action = bpy.data.actions.new(name="MGS2_Animation")
    armature.animation_data.action = action
    
    # Apply keyframes
    for frame_data in frames_data:
        frame_num = frame_data["frame_index"]
        scene.frame_set(frame_num)
        
        for bone_data in frame_data["bone_matrices"]:
            bone_idx = bone_data["bone_index"]
            bone_name = f"bone_{bone_idx:03d}"
            
            if bone_name not in armature.pose.bones:
                continue
            
            pose_bone = armature.pose.bones[bone_name]
            
            # Convert and apply matrix
            dx_matrix = bone_data["matrix"]
            bl_matrix = convert_matrix(dx_matrix)
            pose_bone.matrix = bl_matrix
            
            # Insert keyframes
            pose_bone.keyframe_insert(data_path="location", frame=frame_num)
            pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=frame_num)
            pose_bone.keyframe_insert(data_path="scale", frame=frame_num)
    
    print(f"Applied {len(frames_data)} frames to armature")

def main():
    print(f"Loading animation from: {JSON_FILE}")
    
    with open(JSON_FILE, 'r') as f:
        data = json.load(f)
    
    metadata = data["metadata"]
    frames = data["frames"]
    
    print(f"Loaded {metadata['total_frames']} frames with {metadata['bone_count']} bones")
    
    # Determine bone count from actual frame data
    if frames:
        bone_count = len(frames[0]["bone_matrices"])
    else:
        bone_count = metadata["bone_count"]
    
    # Create armature
    armature = create_armature("MGS2_Skeleton", bone_count)
    
    # Apply animation
    apply_animation(armature, frames, fps=int(metadata["frame_rate"]))
    
    print("Animation import complete!")

if __name__ == "__main__":
    main()
)BLENDER_SCRIPT";
        script.close();
    }
    
    // Create README with buffer signatures and model info
    std::string readme_path = folder_path + "\\README.txt";
    std::ofstream readme(readme_path);
    if (readme.is_open()) {
        readme << "MGS2 Animation Capture\n";
        readme << "======================\n\n";
        readme << "Frames captured: " << m_frames.size() << "\n";
        readme << "Bones per frame: " << (m_frames.empty() ? 0 : m_frames[0].bone_matrices.size()) << "\n";
        readme << "Capture time: " << m_metadata.capture_timestamp << "\n\n";
        
        // Buffer signatures for model identification
        readme << "Buffer Signatures (for model identification):\n";
        readme << "---------------------------------------------\n";
        for (UINT size : m_seen_buffer_sizes) {
            int bone_count = size / 64;
            readme << "  - " << size << " bytes (" << bone_count << " bones)\n";
        }
        readme << "\n";
        
        readme << "Possible Model Files:\n";
        readme << "---------------------\n";
        readme << "Based on bone counts, check these .mar files in:\n";
        readme << "D:\\Metal Gear Solid 2\\assets\\mar\\us\\\n\n";
        readme << "Key character models:\n";
        readme << "  - snake.mar (Solid Snake)\n";
        readme << "  - raiden.mar (Raiden)\n";
        readme << "  - solidus.mar (Solidus)\n";
        readme << "  - olga.mar (Olga)\n";
        readme << "  - otacon.mar (Otacon)\n";
        readme << "  - emma.mar (Emma)\n";
        readme << "  - vamp.mar (Vamp)\n";
        readme << "  - fatman.mar (Fatman)\n";
        readme << "  - fortune.mar (Fortune)\n\n";
        
        readme << "To import into Blender:\n";
        readme << "1. Open Blender\n";
        readme << "2. Go to Scripting tab\n";
        readme << "3. Open 'import_to_blender.py'\n";
        readme << "4. Click 'Run Script'\n";
        readme.close();
    }
    
    // Create model_files.txt with list of all .mar files in assets
    std::string marfiles_path = folder_path + "\\model_files.txt";
    std::ofstream marfiles(marfiles_path);
    if (marfiles.is_open()) {
        marfiles << "MGS2 Model Files (.mar)\n";
        marfiles << "========================\n\n";
        marfiles << "These files are located in: D:\\Metal Gear Solid 2\\assets\\mar\\us\\\n\n";
        marfiles << "Character Models:\n";
        marfiles << "-----------------\n";
        marfiles << "snake.mar\n";
        marfiles << "raiden.mar\n";
        marfiles << "solidus.mar\n";
        marfiles << "olga.mar\n";
        marfiles << "otacon.mar\n";
        marfiles << "emma.mar\n";
        marfiles << "rose.mar\n";
        marfiles << "vamp.mar\n";
        marfiles << "fatman.mar\n";
        marfiles << "fortune.mar\n";
        marfiles << "colonel.mar\n";
        marfiles << "liquid.mar\n";
        marfiles << "ocelot.mar\n\n";
        marfiles << "Enemy Models:\n";
        marfiles << "-------------\n";
        marfiles << "gbs.mar (Guard)\n";
        marfiles << "clthmsk.mar (Cloaked)\n";
        marfiles << "cyph.mar (Cypher drone)\n";
        marfiles << "guard_*.mar (Guard variants)\n\n";
        marfiles << "Mech Models:\n";
        marfiles << "------------\n";
        marfiles << "ray.mar (Metal Gear RAY)\n\n";
        marfiles << "Buffer sizes captured this session:\n";
        for (UINT size : m_seen_buffer_sizes) {
            int bone_count = size / 64;
            marfiles << "  " << size << " bytes = " << bone_count << " bones\n";
        }
        marfiles.close();
    }
    
    std::ostringstream msg;
    msg << "[MGS2Capture] Exported to folder: " << folder_path << "\n";
    OutputDebugStringA(msg.str().c_str());
}

}  // namespace MGS2Capture
