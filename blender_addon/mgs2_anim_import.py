"""
MGS2 Animation Import Addon for Blender

This addon imports captured bone matrix animation data from MGS2: Master Collection
and recreates it as a keyframed Armature Action in Blender.

INSTALLATION:
1. Open Blender → Edit → Preferences → Add-ons → Install
2. Select this .py file
3. Enable "Import-Export: MGS2 Animation Import"

USAGE:
1. Select an Armature object (or addon creates one)
2. File → Import → MGS2 Animation (.json)
3. Animation is applied as a new Action on the Armature
"""

bl_info = {
    "name": "MGS2 Animation Import",
    "author": "MGS2 Preservation Project",
    "version": (1, 0, 0),
    "blender": (3, 6, 0),
    "location": "File > Import > MGS2 Animation",
    "description": "Import captured bone animation from Metal Gear Solid 2",
    "category": "Import-Export",
}

import bpy
import json
import math
from bpy.props import StringProperty, BoolProperty, FloatProperty, EnumProperty
from bpy_extras.io_utils import ImportHelper
from mathutils import Matrix, Vector, Quaternion, Euler


# ============================================================================
# COORDINATE SYSTEM CONVERSION
# ============================================================================

class CoordinateConverter:
    """
    Convert between MGS2 (DirectX/Left-Handed) and Blender (OpenGL/Right-Handed)
    
    MGS2/DirectX:
        - Left-handed coordinate system
        - Y-up
        - Row-major matrices
        
    Blender:
        - Right-handed coordinate system  
        - Z-up
        - Column-major matrices (mathutils.Matrix)
    """
    
    # Axis conversion matrix: swap Y and Z, negate new Y (was Z)
    # This converts from Y-up left-handed to Z-up right-handed
    AXIS_CONVERSION = Matrix((
        (1,  0,  0,  0),
        (0,  0,  1,  0),
        (0, -1,  0,  0),
        (0,  0,  0,  1)
    ))
    
    AXIS_CONVERSION_INV = AXIS_CONVERSION.inverted()
    
    @classmethod
    def from_row_major(cls, row_major_list):
        """
        Convert a 4x4 row-major list to a Blender Matrix (column-major).
        
        Input: [[r0c0, r0c1, r0c2, r0c3], [r1c0, ...], ...]
        """
        # mathutils.Matrix constructor takes rows, but stores column-major internally
        # We need to transpose because the input is row-major
        return Matrix(row_major_list).transposed()
    
    @classmethod
    def convert_bone_matrix(cls, mgs_matrix_rowmajor, apply_axis_conversion=True):
        """
        Full conversion from MGS2 bone matrix to Blender pose matrix.
        
        Steps:
        1. Transpose (row-major → column-major)
        2. Apply axis conversion (Y-up left-hand → Z-up right-hand)
        """
        # Step 1: Convert from row-major to column-major
        mat = cls.from_row_major(mgs_matrix_rowmajor)
        
        if apply_axis_conversion:
            # Step 2: Apply coordinate system conversion
            # Formula: conversion @ mat @ conversion.inverted()
            mat = cls.AXIS_CONVERSION @ mat @ cls.AXIS_CONVERSION_INV
        
        return mat
    
    @classmethod
    def matrix_to_loc_rot_scale(cls, matrix):
        """
        Decompose a 4x4 matrix into location, rotation (quaternion), and scale.
        """
        loc = matrix.to_translation()
        rot = matrix.to_quaternion()
        scale = matrix.to_scale()
        return loc, rot, scale


# ============================================================================
# ANIMATION DATA PARSER
# ============================================================================

class MGS2AnimationData:
    """Parses and holds the imported animation data."""
    
    def __init__(self, filepath):
        self.filepath = filepath
        self.metadata = {}
        self.skeleton = []
        self.frames = []
        self.bone_name_map = {}  # index → name
        
        self._parse()
    
    def _parse(self):
        """Load and parse the JSON file."""
        with open(self.filepath, 'r') as f:
            data = json.load(f)
        
        # Metadata
        self.metadata = data.get('metadata', {})
        
        # Skeleton definition
        skeleton_data = data.get('skeleton', {}).get('bones', [])
        for bone_def in skeleton_data:
            self.skeleton.append({
                'index': bone_def['index'],
                'name': bone_def['name'],
                'parent': bone_def.get('parent', -1)
            })
            self.bone_name_map[bone_def['index']] = bone_def['name']
        
        # Frames
        self.frames = data.get('frames', [])
    
    @property
    def frame_count(self):
        return len(self.frames)
    
    @property
    def bone_count(self):
        return len(self.skeleton)
    
    @property
    def frame_rate(self):
        return self.metadata.get('frame_rate', 60.0)
    
    def get_bone_name(self, index):
        """Get bone name by index, with fallback."""
        return self.bone_name_map.get(index, f"bone_{index:03d}")
    
    def get_frame_matrices(self, frame_index):
        """
        Get all bone matrices for a specific frame.
        Returns: dict of {bone_index: Matrix}
        """
        if frame_index >= len(self.frames):
            return {}
        
        frame_data = self.frames[frame_index]
        result = {}
        
        for bone_data in frame_data.get('bone_matrices', []):
            bone_idx = bone_data['bone_index']
            matrix_rows = bone_data['matrix']
            result[bone_idx] = CoordinateConverter.convert_bone_matrix(matrix_rows)
        
        return result


# ============================================================================
# ARMATURE BUILDER
# ============================================================================

class ArmatureBuilder:
    """Creates or maps to an existing Blender Armature."""
    
    @staticmethod
    def create_armature(name, skeleton):
        """
        Create a new Armature object from skeleton definition.
        
        Args:
            name: Armature object name
            skeleton: List of bone defs with 'name', 'parent' index
        """
        # Create armature data
        arm_data = bpy.data.armatures.new(name + "_Armature")
        arm_obj = bpy.data.objects.new(name, arm_data)
        
        # Link to scene
        bpy.context.collection.objects.link(arm_obj)
        bpy.context.view_layer.objects.active = arm_obj
        arm_obj.select_set(True)
        
        # Enter edit mode to create bones
        bpy.ops.object.mode_set(mode='EDIT')
        
        edit_bones = arm_data.edit_bones
        bone_refs = {}
        
        # Build parent map
        parent_map = {b['index']: b.get('parent', -1) for b in skeleton}
        
        # Create bones (simple chain layout)
        for bone_def in skeleton:
            idx = bone_def['index']
            name = bone_def['name']
            parent_idx = bone_def.get('parent', -1)
            
            bone = edit_bones.new(name)
            
            # Default bone position (will be overwritten by animation)
            # Place in a hierarchy layout
            depth = 0
            p_idx = parent_idx
            while p_idx >= 0:
                depth += 1
                p_idx = parent_map.get(p_idx, -1)
            
            bone.head = Vector((0, 0, depth * 0.5))
            bone.tail = Vector((0, 0, depth * 0.5 + 0.3))
            
            # Set parent
            if parent_idx >= 0:
                parent_name = next((b['name'] for b in skeleton if b['index'] == parent_idx), None)
                if parent_name and parent_name in edit_bones:
                    bone.parent = edit_bones[parent_name]
            
            bone_refs[idx] = bone
        
        bpy.ops.object.mode_set(mode='OBJECT')
        
        return arm_obj
    
    @staticmethod
    def get_bone_mapping(armature, skeleton):
        """
        Map skeleton bone indices to pose bones.
        Returns: dict of {bone_index: PoseBone}
        """
        mapping = {}
        
        for bone_def in skeleton:
            idx = bone_def['index']
            name = bone_def['name']
            
            # Try exact name match first
            if name in armature.pose.bones:
                mapping[idx] = armature.pose.bones[name]
            else:
                # Try case-insensitive match
                for pb in armature.pose.bones:
                    if pb.name.lower() == name.lower():
                        mapping[idx] = pb
                        break
        
        return mapping


# ============================================================================
# ANIMATION APPLICATOR
# ============================================================================

class AnimationApplicator:
    """Applies captured matrices to pose bones and inserts keyframes."""
    
    def __init__(self, armature, anim_data, bone_mapping):
        self.armature = armature
        self.anim_data = anim_data
        self.bone_mapping = bone_mapping
        self.action = None
    
    def create_action(self, action_name):
        """Create a new Action for the animation."""
        self.action = bpy.data.actions.new(name=action_name)
        
        if self.armature.animation_data is None:
            self.armature.animation_data_create()
        
        self.armature.animation_data.action = self.action
        return self.action
    
    def apply_frame(self, frame_index, blender_frame):
        """
        Apply bone matrices for a single frame and insert keyframes.
        
        Args:
            frame_index: Index in the captured animation data
            blender_frame: Blender timeline frame number
        """
        matrices = self.anim_data.get_frame_matrices(frame_index)
        
        for bone_idx, matrix in matrices.items():
            if bone_idx not in self.bone_mapping:
                continue
            
            pose_bone = self.bone_mapping[bone_idx]
            
            # Apply the matrix to the pose bone
            # The captured matrix is in world space relative to armature
            # We need to convert to pose space
            self._apply_matrix_to_pose_bone(pose_bone, matrix)
            
            # Insert keyframes
            pose_bone.keyframe_insert(data_path='location', frame=blender_frame)
            pose_bone.keyframe_insert(data_path='rotation_quaternion', frame=blender_frame)
            pose_bone.keyframe_insert(data_path='scale', frame=blender_frame)
    
    def _apply_matrix_to_pose_bone(self, pose_bone, world_matrix):
        """
        Apply a world-space matrix to a pose bone.
        
        Conversion: world → armature local → bone local
        """
        # Get armature world matrix inverse
        arm_world_inv = self.armature.matrix_world.inverted()
        
        # Convert world matrix to armature local space
        arm_local_matrix = arm_world_inv @ world_matrix
        
        # For child bones, we need to account for parent transforms
        if pose_bone.parent:
            # Get parent's current world matrix in armature space
            parent_matrix = pose_bone.parent.matrix
            parent_inv = parent_matrix.inverted()
            
            # Convert to bone local space
            local_matrix = parent_inv @ arm_local_matrix
        else:
            # Root bone: armature local = bone local
            local_matrix = arm_local_matrix
        
        # Account for rest pose
        rest_matrix = pose_bone.bone.matrix_local
        if pose_bone.parent:
            parent_rest_inv = pose_bone.parent.bone.matrix_local.inverted()
            rest_local = parent_rest_inv @ rest_matrix
        else:
            rest_local = rest_matrix
        
        rest_inv = rest_local.inverted()
        
        # Final pose matrix relative to rest pose
        pose_matrix = rest_inv @ local_matrix
        
        # Decompose and apply
        loc, rot, scale = CoordinateConverter.matrix_to_loc_rot_scale(pose_matrix)
        
        pose_bone.location = loc
        pose_bone.rotation_mode = 'QUATERNION'
        pose_bone.rotation_quaternion = rot
        pose_bone.scale = scale
    
    def apply_all_frames(self, start_frame=1, report_progress=None):
        """
        Apply all animation frames.
        
        Args:
            start_frame: Blender frame to start at
            report_progress: Optional callback(current, total)
        """
        total_frames = self.anim_data.frame_count
        
        for i in range(total_frames):
            blender_frame = start_frame + i
            self.apply_frame(i, blender_frame)
            
            if report_progress:
                report_progress(i + 1, total_frames)
        
        return total_frames


# ============================================================================
# BLENDER OPERATOR
# ============================================================================

class IMPORT_OT_mgs2_animation(bpy.types.Operator, ImportHelper):
    """Import MGS2 Captured Animation"""
    bl_idname = "import_anim.mgs2_json"
    bl_label = "Import MGS2 Animation"
    bl_options = {'REGISTER', 'UNDO', 'PRESET'}
    
    filename_ext = ".json"
    
    filter_glob: StringProperty(
        default="*.json",
        options={'HIDDEN'},
    )
    
    create_armature: BoolProperty(
        name="Create Armature",
        description="Create a new armature from skeleton data if no armature is selected",
        default=True,
    )
    
    action_name: StringProperty(
        name="Action Name",
        description="Name for the new animation action",
        default="MGS2_Animation",
    )
    
    start_frame: bpy.props.IntProperty(
        name="Start Frame",
        description="Blender frame to start the animation",
        default=1,
        min=0,
    )
    
    apply_axis_conversion: BoolProperty(
        name="Apply Axis Conversion",
        description="Convert from DirectX (Y-up left-handed) to Blender (Z-up right-handed)",
        default=True,
    )
    
    def execute(self, context):
        return self.import_animation(context, self.filepath)
    
    def import_animation(self, context, filepath):
        """Main import function."""
        
        self.report({'INFO'}, f"Loading animation data from {filepath}")
        
        # Parse animation data
        try:
            anim_data = MGS2AnimationData(filepath)
        except Exception as e:
            self.report({'ERROR'}, f"Failed to parse animation file: {e}")
            return {'CANCELLED'}
        
        self.report({'INFO'}, f"Loaded {anim_data.frame_count} frames, {anim_data.bone_count} bones")
        
        # Get or create armature
        armature = None
        active_obj = context.active_object
        
        if active_obj and active_obj.type == 'ARMATURE':
            armature = active_obj
            self.report({'INFO'}, f"Using selected armature: {armature.name}")
        elif self.create_armature:
            armature = ArmatureBuilder.create_armature("MGS2_Snake", anim_data.skeleton)
            self.report({'INFO'}, f"Created new armature: {armature.name}")
        else:
            self.report({'ERROR'}, "No armature selected and 'Create Armature' is disabled")
            return {'CANCELLED'}
        
        # Create bone mapping
        bone_mapping = ArmatureBuilder.get_bone_mapping(armature, anim_data.skeleton)
        
        if not bone_mapping:
            self.report({'WARNING'}, "No bones could be mapped. Check bone names.")
        else:
            self.report({'INFO'}, f"Mapped {len(bone_mapping)} bones")
        
        # Apply animation
        applicator = AnimationApplicator(armature, anim_data, bone_mapping)
        applicator.create_action(self.action_name)
        
        def progress_callback(current, total):
            if current % 100 == 0:
                print(f"Processing frame {current}/{total}")
        
        # Ensure we're in object mode
        if context.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        
        # Select and activate armature
        bpy.context.view_layer.objects.active = armature
        armature.select_set(True)
        
        # Apply all frames
        frame_count = applicator.apply_all_frames(
            start_frame=self.start_frame,
            report_progress=progress_callback
        )
        
        # Set scene frame range
        context.scene.frame_start = self.start_frame
        context.scene.frame_end = self.start_frame + frame_count - 1
        context.scene.frame_current = self.start_frame
        
        # Set render framerate to match animation
        context.scene.render.fps = int(anim_data.frame_rate)
        
        self.report({'INFO'}, f"Successfully imported {frame_count} frames to action '{self.action_name}'")
        
        return {'FINISHED'}
    
    def draw(self, context):
        layout = self.layout
        
        layout.prop(self, "action_name")
        layout.prop(self, "start_frame")
        
        box = layout.box()
        box.label(text="Options:")
        box.prop(self, "create_armature")
        box.prop(self, "apply_axis_conversion")


# ============================================================================
# MENU REGISTRATION
# ============================================================================

def menu_func_import(self, context):
    self.layout.operator(IMPORT_OT_mgs2_animation.bl_idname, text="MGS2 Animation (.json)")


classes = (
    IMPORT_OT_mgs2_animation,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()

# menu registration cleanup on unregister

# decompose matrix to translation, rotation, and scale components

# skeleton parent map builder helper
