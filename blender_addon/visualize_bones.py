"""
MGS2 Bone Visualization - Just bones, no model
Shows captured bone positions as moving empty objects

USAGE:
1. Open Blender (new file)
2. Go to Scripting tab
3. Click Run Script
"""

import bpy
import json
from mathutils import Matrix, Vector

JSON_FILE = r"D:\Metal Gear Solid 2\MGS2_Capture\animation_data_fixed.json"
FRAME_SKIP = 10  # Every 10th frame

def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def main():
    print("Loading animation data...")
    with open(JSON_FILE, 'r') as f:
        data = json.load(f)
    
    frames = data["frames"]
    if not frames:
        print("No frames!")
        return
    
    bone_count = len(frames[0]["bone_matrices"])
    print(f"Frames: {len(frames)}, Bones: {bone_count}")
    
    clear_scene()
    
    # Create an empty for each bone
    empties = []
    for i in range(bone_count):
        bpy.ops.object.empty_add(type='SPHERE', radius=0.05)
        empty = bpy.context.object
        empty.name = f"Bone_{i:03d}"
        empties.append(empty)
    
    # Set up timeline
    scene = bpy.context.scene
    scene.frame_start = 0
    scene.frame_end = len(frames) // FRAME_SKIP
    
    # Apply keyframes
    print("Applying keyframes...")
    for frame_idx, frame in enumerate(frames):
        if frame_idx % FRAME_SKIP != 0:
            continue
        
        out_frame = frame_idx // FRAME_SKIP
        scene.frame_set(out_frame)
        
        for bone_data in frame["bone_matrices"]:
            idx = bone_data["bone_index"]
            if idx >= len(empties):
                continue
            
            mat = bone_data["matrix"]
            # Get position from matrix (last column = translation)
            x = mat[3][0]  # Row 4, col 1
            y = mat[3][2]  # Swap Y/Z for Blender
            z = mat[3][1]
            
            empties[idx].location = (x, y, z)
            empties[idx].keyframe_insert(data_path="location", frame=out_frame)
        
        if out_frame % 50 == 0:
            print(f"Frame {out_frame}...")
    
    scene.frame_set(0)
    print("DONE! Press SPACE to play")

if __name__ == "__main__":
    main()

# check if empty name is valid to avoid issues

# format progress indicators in the status bar

# optimize update performance in viewport

# add support for default frame range check

# verify frame skip default settings before import
