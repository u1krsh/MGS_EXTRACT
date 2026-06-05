"""
RenderDoc Bone Matrix Extractor for MGS2

This script extracts bone matrices from RenderDoc capture files (.rdc).
Use this for initial exploration to identify which constant buffers contain
bone matrix data before building the full DLL hook.

PREREQUISITES:
1. Install RenderDoc (https://renderdoc.org/)
2. Add RenderDoc's Python module to your path:
   - Windows: C:\Program Files\RenderDoc\pymodules
3. Capture frames using RenderDoc while running MGS2

USAGE:
    python renderdoc_extractor.py capture.rdc --output bones.json

WORKFLOW:
1. Launch RenderDoc
2. File → Launch Application → Select MGS2 executable
3. Play to the Tanker intro cutscene
4. Press F12 (or PrintScreen) to capture frames
5. Save each capture as .rdc file
6. Run this script on each capture
7. Combine outputs into a single animation file
"""

import sys
import os
import json
import argparse
import struct
from pathlib import Path

# Try to import RenderDoc
try:
    import renderdoc as rd
except ImportError:
    print("ERROR: Could not import renderdoc module.")
    print("Please add RenderDoc's pymodules directory to PYTHONPATH:")
    print('  set PYTHONPATH=C:\\Program Files\\RenderDoc\\pymodules')
    sys.exit(1)


class RenderDocBoneExtractor:
    """Extract bone matrices from RenderDoc captures."""
    
    def __init__(self, capture_path):
        self.capture_path = capture_path
        self.controller = None
        self.bone_matrices = []
        
    def load_capture(self):
        """Load the RenderDoc capture file."""
        cap, result, _ = rd.OpenCaptureFile(self.capture_path)
        
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError(f"Failed to open capture: {result}")
        
        if not cap.LocalReplaySupport().contains(rd.ReplaySupport.Supported):
            raise RuntimeError("Capture cannot be replayed locally")
        
        result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError(f"Failed to open replay: {result}")
        
        self.controller = controller
        return controller
    
    def find_skinned_draw_calls(self):
        """
        Find draw calls that likely involve skinned meshes.
        These typically have large constant buffers (bone matrices).
        """
        draws = self.controller.GetDrawcalls()
        skinned_draws = []
        
        def traverse(drawcalls, depth=0):
            for draw in drawcalls:
                # Look for actual draw calls (not just state changes)
                if draw.flags & rd.DrawFlags.Drawcall:
                    skinned_draws.append(draw)
                
                if draw.children:
                    traverse(draw.children, depth + 1)
        
        traverse(draws)
        return skinned_draws
    
    def get_constant_buffer_data(self, draw_event_id):
        """
        Get all constant buffer data for a specific draw call.
        Returns list of (slot, size, data) tuples.
        """
        buffers = []
        
        # Set state to this draw call
        self.controller.SetFrameEvent(draw_event_id, True)
        
        # Get the pipeline state
        state = self.controller.GetPipelineState()
        
        # Get vertex shader constant buffers
        shader = state.GetShader(rd.ShaderStage.Vertex)
        if shader == rd.ResourceId.Null():
            return buffers
        
        # Get bound constant buffers
        for i in range(16):  # Check first 16 slots
            try:
                cb = state.GetConstantBuffer(rd.ShaderStage.Vertex, i, 0)
                if cb.resourceId != rd.ResourceId.Null():
                    # Read buffer data
                    data = self.controller.GetBufferData(cb.resourceId, 0, 0)
                    if len(data) >= 64:  # At least one 4x4 matrix
                        buffers.append({
                            'slot': i,
                            'size': len(data),
                            'data': bytes(data),
                            'resource_id': str(cb.resourceId)
                        })
            except Exception as e:
                continue
        
        return buffers
    
    def identify_bone_buffer(self, buffers, expected_bone_count=64):
        """
        Identify which buffer contains bone matrices based on size.
        Bone matrices are 64 bytes each (4x4 float).
        """
        expected_size = expected_bone_count * 64
        
        candidates = []
        for buf in buffers:
            size = buf['size']
            # Check if size is consistent with bone matrices
            if size >= 64 and size % 64 == 0:
                num_matrices = size // 64
                if 20 <= num_matrices <= 256:  # Reasonable bone count range
                    candidates.append({
                        **buf,
                        'num_matrices': num_matrices,
                        'score': abs(num_matrices - expected_bone_count)
                    })
        
        # Sort by score (closest to expected count)
        candidates.sort(key=lambda x: x['score'])
        
        return candidates[0] if candidates else None
    
    def parse_matrices(self, data, num_matrices):
        """
        Parse raw buffer data into list of 4x4 matrices.
        Assumes row-major float32 format.
        """
        matrices = []
        
        for i in range(num_matrices):
            offset = i * 64
            matrix_data = data[offset:offset + 64]
            
            if len(matrix_data) < 64:
                break
            
            # Unpack 16 floats (row-major 4x4)
            floats = struct.unpack('16f', matrix_data)
            
            # Reshape to 4x4
            matrix = [
                list(floats[0:4]),
                list(floats[4:8]),
                list(floats[8:12]),
                list(floats[12:16])
            ]
            
            matrices.append({
                'bone_index': i,
                'matrix': matrix
            })
        
        return matrices
    
    def extract_frame(self, expected_bones=64):
        """
        Extract bone matrices from the capture.
        Returns dict with bone matrix data.
        """
        # Find draw calls
        draws = self.find_skinned_draw_calls()
        print(f"Found {len(draws)} draw calls")
        
        # Look for bone matrices in each draw call
        for draw in draws:
            buffers = self.get_constant_buffer_data(draw.eventId)
            
            if not buffers:
                continue
            
            bone_buffer = self.identify_bone_buffer(buffers, expected_bones)
            
            if bone_buffer:
                print(f"Found bone buffer at slot {bone_buffer['slot']}: "
                      f"{bone_buffer['num_matrices']} matrices")
                
                matrices = self.parse_matrices(
                    bone_buffer['data'], 
                    bone_buffer['num_matrices']
                )
                
                return {
                    'draw_event': draw.eventId,
                    'draw_name': str(draw.name),
                    'buffer_slot': bone_buffer['slot'],
                    'bone_count': bone_buffer['num_matrices'],
                    'bone_matrices': matrices
                }
        
        return None
    
    def close(self):
        """Clean up resources."""
        if self.controller:
            self.controller.Shutdown()


def combine_captures(capture_files, output_path, frame_rate=60.0):
    """
    Combine multiple single-frame captures into one animation file.
    """
    frames = []
    
    for i, filepath in enumerate(sorted(capture_files)):
        print(f"\nProcessing: {filepath}")
        
        extractor = RenderDocBoneExtractor(filepath)
        
        try:
            extractor.load_capture()
            frame_data = extractor.extract_frame()
            
            if frame_data:
                frames.append({
                    'frame_index': i,
                    'timestamp_ms': i * (1000.0 / frame_rate),
                    'bone_matrices': frame_data['bone_matrices']
                })
                print(f"  Extracted {len(frame_data['bone_matrices'])} bones")
            else:
                print(f"  WARNING: No bone data found")
        
        except Exception as e:
            print(f"  ERROR: {e}")
        
        finally:
            extractor.close()
    
    # Build output structure
    output = {
        'metadata': {
            'game': 'MGS2_Master_Collection',
            'capture_timestamp': '',
            'frame_rate': frame_rate,
            'total_frames': len(frames),
            'bone_count': frames[0]['bone_matrices'][-1]['bone_index'] + 1 if frames else 0,
            'coordinate_system': {
                'handedness': 'left',
                'up_axis': 'Y',
                'matrix_format': 'row_major',
                'unit_scale': 1.0
            }
        },
        'skeleton': {
            'bones': []  # Would need to be filled in separately
        },
        'frames': frames
    }
    
    with open(output_path, 'w') as f:
        json.dump(output, f, indent=2)
    
    print(f"\nSaved {len(frames)} frames to {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description='Extract bone matrices from RenderDoc captures'
    )
    
    parser.add_argument(
        'captures',
        nargs='+',
        help='RenderDoc capture file(s) (.rdc)'
    )
    
    parser.add_argument(
        '--output', '-o',
        default='animation_capture.json',
        help='Output JSON file path'
    )
    
    parser.add_argument(
        '--framerate', '-f',
        type=float,
        default=60.0,
        help='Animation frame rate (default: 60)'
    )
    
    parser.add_argument(
        '--bones', '-b',
        type=int,
        default=64,
        help='Expected bone count (default: 64)'
    )
    
    args = parser.parse_args()
    
    # Validate input files
    captures = []
    for path in args.captures:
        if os.path.isdir(path):
            # Glob for .rdc files
            captures.extend(Path(path).glob('*.rdc'))
        elif os.path.isfile(path):
            captures.append(path)
        else:
            print(f"WARNING: {path} not found")
    
    if not captures:
        print("ERROR: No capture files found")
        return 1
    
    print(f"Processing {len(captures)} capture(s)...")
    
    if len(captures) == 1:
        # Single capture - just extract and print info
        extractor = RenderDocBoneExtractor(str(captures[0]))
        
        try:
            extractor.load_capture()
            frame_data = extractor.extract_frame(args.bones)
            
            if frame_data:
                output = {
                    'metadata': {
                        'game': 'MGS2_Master_Collection',
                        'frame_rate': args.framerate,
                        'total_frames': 1,
                        'bone_count': len(frame_data['bone_matrices']),
                        'coordinate_system': {
                            'handedness': 'left',
                            'up_axis': 'Y',
                            'matrix_format': 'row_major',
                            'unit_scale': 1.0
                        }
                    },
                    'skeleton': {'bones': []},
                    'frames': [{
                        'frame_index': 0,
                        'timestamp_ms': 0.0,
                        'bone_matrices': frame_data['bone_matrices']
                    }]
                }
                
                with open(args.output, 'w') as f:
                    json.dump(output, f, indent=2)
                
                print(f"\nExtracted {len(frame_data['bone_matrices'])} bones")
                print(f"Saved to: {args.output}")
            else:
                print("No bone matrices found in capture")
        
        finally:
            extractor.close()
    
    else:
        # Multiple captures - combine into animation
        combine_captures(
            [str(c) for c in captures],
            args.output,
            args.framerate
        )
    
    return 0


if __name__ == '__main__':
    sys.exit(main())

# helper method to check buffer slot bounds

# handle case where no frames are found in capture

# todo: support multi-frame combining for long sequences

# add verbose logging parameter

# check shader type support

# double check vertex shader constant buffer slot configuration

# add support for custom frame rates in options

# validate result code from open capture before proceeding
