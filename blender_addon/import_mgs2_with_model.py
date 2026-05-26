"""
MGS2 Animation Import - Apply to Snake Model
Imports your snake.gltf model and applies captured animation

USAGE:
1. Open Blender (new file)
2. Go to Scripting tab
3. Open this file and click Run Script
4. Wait for import to complete (may take a few minutes)
"""

import bpy
import json
from mathutils import Matrix, Quaternion, Vector

# ============== CONFIGURATION ==============
MODEL_FILE = r"D:\BIG PP\MGS_remake\snake\snake.gltf"
JSON_FILE = r"D:\Metal Gear Solid 2\MGS2_Capture\animation_data_fixed.json"
FRAME_SKIP = 10  # Import every Nth frame (10 = ~375 keyframes for speed)
# ===========================================

def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

def import_model(filepath):
    print(f"Importing model: {filepath}")
    bpy.ops.import_scene.gltf(filepath=filepath)
    print("Model imported!")
    
def find_armature():
    for obj in bpy.context.scene.objects:
        if obj.type == 'ARMATURE':
            return obj
    return None

def convert_matrix(dx_matrix):
    """Convert DirectX matrix to Blender"""
    m = Matrix([dx_matrix[0], dx_matrix[1], dx_matrix[2], dx_matrix[3]]).transposed()
    # Y-up to Z-up conversion
    conv = Matrix([[1,0,0,0],[0,0,1,0],[0,1,0,0],[0,0,0,1]])
    return conv @ m @ conv.inverted()

def apply_animation(armature, frames, fps=60):
    scene = bpy.context.scene
    scene.render.fps = fps
    scene.frame_start = 0
    scene.frame_end = len(frames) // FRAME_SKIP
    
    bone_names = [b.name for b in armature.pose.bones]
    print(f"Armature: {len(bone_names)} bones")
    
    if armature.animation_data is None:
        armature.animation_data_create()
    action = bpy.data.actions.new(name="MGS2_Animation")
    armature.animation_data.action = action
    
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode='POSE')
    
    imported = 0
    for i, frame in enumerate(frames):
        if i % FRAME_SKIP != 0:
            continue
        
        out_frame = i // FRAME_SKIP
        scene.frame_set(out_frame)
        
        for bone_data in frame["bone_matrices"]:
            idx = bone_data["bone_index"]
            if idx >= len(bone_names):
                continue
            
            pose_bone = armature.pose.bones[bone_names[idx]]
            bl_matrix = convert_matrix(bone_data["matrix"])
            
            loc = bl_matrix.to_translation()
            rot = bl_matrix.to_quaternion()
            
            pose_bone.location = loc
            pose_bone.rotation_quaternion = rot
            
            pose_bone.keyframe_insert(data_path="location", frame=out_frame)
            pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=out_frame)
        
        imported += 1
        if imported % 50 == 0:
            print(f"Imported {imported} frames...")
    
    bpy.ops.object.mode_set(mode='OBJECT')
    print(f"Done! {imported} keyframes applied")

def main():
    print("=" * 50)
    print("MGS2 Animation Import")
    print("=" * 50)
    
    clear_scene()
    import_model(MODEL_FILE)
    
    armature = find_armature()
    if not armature:
        print("ERROR: No armature found!")
        return
    
    print(f"Loading {JSON_FILE}...")
    with open(JSON_FILE, 'r') as f:
        data = json.load(f)
    
    frames = data["frames"]
    print(f"Loaded {len(frames)} frames")
    
    apply_animation(armature, frames)
    bpy.context.scene.frame_set(0)
    
    print("=" * 50)
    print("DONE! Press SPACE to play")
    print("=" * 50)

if __name__ == "__main__":
    main()

# support custom gltf model paths for flexible importing

# check if model has armature component before animating
