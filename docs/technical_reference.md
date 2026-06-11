# MGS2 Animation Capture: Technical Reference

This document provides detailed technical information for implementing the animation capture pipeline.

## Coordinate System Conversion Details

### Source: MGS2 / DirectX 11

| Property | Value |
|----------|-------|
| Handedness | Left-handed |
| Up axis | Y+ |
| Forward axis | Z+ |
| Right axis | X+ |
| Matrix storage | Row-major |
| Unit scale | Unknown (likely cm or m) |

### Target: Blender / OpenGL

| Property | Value |
|----------|-------|
| Handedness | Right-handed |
| Up axis | Z+ |
| Forward axis | Y- |
| Right axis | X+ |
| Matrix storage | Column-major |
| Unit scale | Meters |

### Conversion Matrix

To convert from DirectX Y-up left-handed to Blender Z-up right-handed:

```
Axis Conversion Matrix:
┌                 ┐
│  1   0   0   0  │
│  0   0   1   0  │   (Swap Y and Z, negate new Y)
│  0  -1   0   0  │
│  0   0   0   1  │
└                 ┘
```

### Full Conversion Pipeline

```python
# 1. Input: Row-major 4x4 matrix from game
mgs_matrix = [
    [m00, m01, m02, m03],
    [m10, m11, m12, m13],
    [m20, m21, m22, m23],
    [m30, m31, m32, m33]
]

# 2. Transpose to column-major (Blender's mathutils.Matrix format)
blender_matrix = Matrix(mgs_matrix).transposed()

# 3. Apply axis conversion
# Formula: CONV @ mat @ CONV.inverted()
axis_conv = Matrix((
    (1,  0,  0, 0),
    (0,  0,  1, 0),
    (0, -1,  0, 0),
    (0,  0,  0, 1)
))
final_matrix = axis_conv @ blender_matrix @ axis_conv.inverted()
```

---

## JSON Data Format Specification

### Root Structure

```json
{
  "metadata": { ... },
  "skeleton": { ... },
  "frames": [ ... ]
}
```

### Metadata Object

| Field | Type | Description |
|-------|------|-------------|
| `game` | string | Game identifier |
| `capture_timestamp` | string | ISO 8601 timestamp |
| `frame_rate` | float | Capture framerate (usually 60.0) |
| `total_frames` | int | Total frame count |
| `bone_count` | int | Number of bones per frame |
| `coordinate_system` | object | Source coordinate system info |

### Coordinate System Object

| Field | Type | Description |
|-------|------|-------------|
| `handedness` | string | "left" or "right" |
| `up_axis` | string | "Y" or "Z" |
| `matrix_format` | string | "row_major" or "column_major" |
| `unit_scale` | float | Meters per unit |

### Skeleton Object

```json
{
  "bones": [
    {"index": 0, "name": "root", "parent": -1},
    {"index": 1, "name": "pelvis", "parent": 0},
    ...
  ]
}
```

### Frame Object

```json
{
  "frame_index": 0,
  "timestamp_ms": 0.0,
  "bone_matrices": [
    {
      "bone_index": 0,
      "matrix": [[...], [...], [...], [...]]
    }
  ]
}
```

### Matrix Format

4x4 row-major float matrix:
```json
"matrix": [
  [m00, m01, m02, m03],  // Row 0: Right vector + translation.x
  [m10, m11, m12, m13],  // Row 1: Up vector + translation.y  
  [m20, m21, m22, m23],  // Row 2: Forward vector + translation.z
  [m30, m31, m32, m33]   // Row 3: Usually [0, 0, 0, 1]
]
```

---

## DX11 Constant Buffer Layout

### Typical Bone Matrix Buffer

Bone matrices are sent to the GPU in a constant buffer, usually structured as:

```cpp
// HLSL constant buffer declaration (estimated)
cbuffer BoneMatrices : register(b2)  // Often slot 2
{
    float4x4 bones[MAX_BONES];  // 64-128 bones typical
};
```

### Buffer Size Calculation

```
Buffer Size = Bone Count × 64 bytes
Example: 64 bones × 64 = 4096 bytes
```

### Common Constant Buffer Slots

| Slot | Typical Use | Size Pattern |
|------|-------------|--------------|
| b0 | View/Projection matrices | 128-256 bytes |
| b1 | Per-object transforms | 64-128 bytes |
| **b2** | **Bone matrices** | **4096+ bytes** |
| b3 | Material parameters | Variable |

---

## DX11 Hook Points

### VTable Indices (ID3D11DeviceContext)

| Index | Method | Purpose |
|-------|--------|---------|
| 7 | `Map` | Buffer lock for update |
| 8 | `Unmap` | Buffer unlock |
| 47 | `VSSetConstantBuffers` | Bind constant buffers |
| **48** | **`UpdateSubresource`** | **Direct buffer update** |

### VTable Indices (IDXGISwapChain)

| Index | Method | Purpose |
|-------|--------|---------|
| **8** | **`Present`** | **Frame boundary** |
| 13 | `ResizeBuffers` | Resolution change |

---

## Snake (Solid Snake) Skeleton Reference

Based on typical humanoid game skeletons, Snake likely has ~64 bones:

### Core Hierarchy

```
root (0)
├── pelvis (1)
│   ├── spine_00 (2)
│   │   ├── spine_01 (3)
│   │   │   ├── spine_02 (4)
│   │   │   │   ├── spine_03 (5)
│   │   │   │   │   ├── neck (6)
│   │   │   │   │   │   └── head (7)
│   │   │   │   │   │       └── head_end (8)
│   │   │   │   │   ├── clavicle_l (9)
│   │   │   │   │   │   └── upperarm_l (10)
│   │   │   │   │   │       └── lowerarm_l (11)
│   │   │   │   │   │           └── hand_l (12)
│   │   │   │   │   │               └── [fingers...]
│   │   │   │   │   └── clavicle_r (13)
│   │   │   │   │       └── [mirror of left arm]
│   ├── thigh_l (17)
│   │   └── calf_l (18)
│   │       └── foot_l (19)
│   │           └── toe_l (20)
│   └── thigh_r (21)
│       └── [mirror of left leg]
```

### Discovering Actual Bone Order

1. **Method 1: RenderDoc Inspection**
   - Capture a frame with Snake in T-pose or known pose
   - Examine bone matrices
   - Identity matrices = rest pose bones
   - Find root by looking for no parent transform

2. **Method 2: MAR File Analysis**
   - Parse cdc_org_tng.mar for skeleton definition
   - Bone order in file usually matches buffer order

3. **Method 3: Visual Correlation**
   - Zero out specific bone matrices
   - Re-render in RenderDoc
   - Observe which body part stops animating

---

## Troubleshooting Guide

### Issue: Captured matrices look wrong (garbage values)

**Possible causes:**
- Wrong constant buffer slot
- Wrong buffer size assumption
- Buffer contains skinning weights, not matrices

**Solution:**
- Use RenderDoc to manually inspect buffer contents
- Look for identity matrix patterns (diagonal 1s)
- Verify buffer size is multiple of 64

### Issue: Blender animation is mirrored/flipped

**Possible causes:**
- Incorrect handedness conversion
- Axis conversion applied twice

**Solution:**
- Try disabling axis conversion in import
- Check if armature rest pose matches expected orientation

### Issue: Bones don't match expected skeleton

**Possible causes:**
- MGS2 uses different bone order for cutscenes
- Multiple actors' bones in same buffer

**Solution:**
- Filter capture to specific draw call
- Look for buffer size changes between actors

### Issue: Animation timing is wrong

**Possible causes:**
- Frame rate mismatch
- Missed frames during capture

**Solution:**
- Verify game runs at 60 FPS during capture
- Use timestamp_ms for timing instead of frame index

- *Present hook is called every frame boundary to update graphics status.*

- *Check skeleton definitions if bone count differs.*

- *Constant buffer slot 2 is typical for character animation matrices.*

- *Note on matrix formats: DirectX uses row-major indexing.*

- *Use MAR file analysis for precise joint names verification.*

- *Matrix math details: transpose is required.*

- *MGS2 uses 64 bones for Solid Snake's skeleton model.*

- *Identity matrices indicate rest pose for skeleton bones.*
