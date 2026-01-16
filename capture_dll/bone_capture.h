/**
 * MGS2 Animation Capture - Bone Matrix Capture Module
 * 
 * Captures bone transformation matrices per-frame from DX11 constant buffers
 * and exports them to JSON for Blender import.
 * 
 * Target: Metal Gear Solid 2: Master Collection (DX11, Windows)
 * Install Path: D:\Metal Gear Solid 2
 */

#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <set>

namespace MGS2Capture {

// Configuration
constexpr int MAX_BONES = 128;              // Maximum bones to capture per actor
constexpr int TARGET_CBUFFER_SLOT = 2;      // Typical slot for bone matrices (verify with RenderDoc)
constexpr size_t BONE_MATRIX_SIZE = sizeof(DirectX::XMFLOAT4X4);  // 64 bytes per matrix

/**
 * Single bone matrix data
 * Stored as row-major 4x4 float matrix (DirectX convention)
 */
struct BoneMatrix {
    int bone_index;
    DirectX::XMFLOAT4X4 matrix;  // Row-major, left-handed
};

/**
 * Camera matrix data for scene reconstruction
 */
struct CameraData {
    DirectX::XMFLOAT4X4 view_matrix;        // Camera view matrix
    DirectX::XMFLOAT4X4 projection_matrix;  // Camera projection matrix
    bool has_data = false;
};

/**
 * Frame capture data - all bone matrices for a single frame
 */
struct FrameCapture {
    int frame_index;
    double timestamp_ms;
    std::vector<BoneMatrix> bone_matrices;
    CameraData camera;  // Camera data for this frame
};

/**
 * Capture session metadata
 */
struct CaptureMetadata {
    std::string game_name = "MGS2_Master_Collection";
    std::string capture_timestamp;
    double frame_rate = 60.0;
    int total_frames = 0;
    int bone_count = 0;
    
    // Coordinate system info for Blender conversion
    std::string handedness = "left";
    std::string up_axis = "Y";
    std::string matrix_format = "row_major";
    float unit_scale = 1.0f;
};

/**
 * Skeleton bone definition
 */
struct BoneDefinition {
    int index;
    std::string name;
    int parent_index;  // -1 for root
};

/**
 * Main capture manager class
 * Singleton pattern for global access from hooks
 */
class BoneCaptureManager {
public:
    static BoneCaptureManager& Instance() {
        static BoneCaptureManager instance;
        return instance;
    }

    // Control
    void StartCapture(const std::string& output_path);
    void StopCapture();
    bool IsCapturing() const { return m_capturing; }

    // Called from hooks
    void OnConstantBufferUpdate(
        ID3D11DeviceContext* context,
        ID3D11Resource* dst_resource,
        UINT dst_subresource,
        const D3D11_BOX* dst_box,
        const void* src_data,
        UINT src_row_pitch,
        UINT src_depth_pitch
    );

    void OnPresent();  // Frame boundary
    
    // Direct bone matrix capture (for Map/Unmap hooks)
    void CaptureBoneMatrices(const void* data, int bone_count);
    
    // Camera capture (view and projection matrices)
    void CaptureCamera(const void* view_matrix, const void* proj_matrix);

    // Configuration
    void SetTargetBufferSlot(UINT slot) { m_target_slot = slot; }
    void SetBoneCount(int count) { m_bone_count = count; }
    void SetSkeleton(const std::vector<BoneDefinition>& skeleton);

    // Export
    void ExportToJSON(const std::string& filepath);
    void ExportToFolder(const std::string& folder_path);  // Creates folder with all Blender-ready files

private:
    BoneCaptureManager() = default;
    ~BoneCaptureManager() { if (m_capturing) StopCapture(); }

    BoneCaptureManager(const BoneCaptureManager&) = delete;
    BoneCaptureManager& operator=(const BoneCaptureManager&) = delete;

    // State
    bool m_capturing = false;
    std::string m_output_path;
    
    // Capture data
    std::mutex m_mutex;
    CaptureMetadata m_metadata;
    std::vector<BoneDefinition> m_skeleton;
    std::vector<FrameCapture> m_frames;
    
    // Frame tracking
    int m_frame_index = 0;
    std::chrono::high_resolution_clock::time_point m_start_time;
    FrameCapture m_current_frame;

    // Buffer identification
    UINT m_target_slot = TARGET_CBUFFER_SLOT;
    int m_bone_count = 64;
    ID3D11Buffer* m_tracked_buffer = nullptr;
    
    // Track unique buffer sizes seen during capture (for model identification)
    std::set<UINT> m_seen_buffer_sizes;

    // Helpers
    std::string GetTimestamp();
    void FinalizeCurrentFrame();
};

/**
 * Helper: Check if a constant buffer size could contain bone matrices
 * Accepts any matrix-aligned buffer from 32 to 256 bones
 */
inline bool IsBoneMatrixBuffer(UINT buffer_size, int expected_bones) {
    // Bone matrices are 64 bytes each (4x4 floats)
    // MGS2 uses 10240 byte buffers (160 bones)
    
    if (buffer_size < 2048) return false;      // Too small (less than 32 bones)
    if (buffer_size > 16384) return false;     // Too large (more than 256 bones)
    if (buffer_size % 64 != 0) return false;   // Not aligned for matrices
    
    return true;
}

/**
 * Helper: Parse raw buffer data into bone matrices
 */
inline std::vector<BoneMatrix> ParseBoneMatrices(
    const void* data, 
    int bone_count
) {
    std::vector<BoneMatrix> result;
    result.reserve(bone_count);

    const auto* matrices = static_cast<const DirectX::XMFLOAT4X4*>(data);
    for (int i = 0; i < bone_count; ++i) {
        BoneMatrix bm;
        bm.bone_index = i;
        bm.matrix = matrices[i];
        result.push_back(bm);
    }

    return result;
}

}  // namespace MGS2Capture

// Global convenience macros for hook integration
#define CAPTURE_MANAGER MGS2Capture::BoneCaptureManager::Instance()
