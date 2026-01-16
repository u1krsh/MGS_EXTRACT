# MGS2 Animation Capture Pipeline

Runtime animation capture from Metal Gear Solid 2: Master Collection (DX11) → Blender recreation.

## Quick Start

### Option A: RenderDoc Capture (Recommended First)

1. Install [RenderDoc](https://renderdoc.org/)
2. Launch MGS2 through RenderDoc
3. Capture frames during Tanker intro (F12)
4. Run extraction:
   ```powershell
   set PYTHONPATH=C:\Program Files\RenderDoc\pymodules
   python tools\renderdoc_extractor.py captures\*.rdc -o tanker_intro.json
   ```
5. Import in Blender using the addon

### Option B: DLL Hook (Real-time Capture)

1. Build the capture DLL:
   ```powershell
   cd capture_dll
   cmake -B build -G "Visual Studio 17 2022"
   cmake --build build --config Release
   ```
2. Copy `d3d11.dll` to `D:\Metal Gear Solid 2\`
3. Run the game
4. Press **F9** to start/stop capture
5. Animation saved to `D:\Metal Gear Solid 2\animation_capture.json`
6. Import in Blender using the addon

## Directory Structure

```
MGS_EXTRACT/
├── capture_dll/           # DX11 hook DLL
│   ├── d3d11_proxy.cpp    # Main proxy DLL
│   ├── bone_capture.h     # Capture data structures
│   ├── bone_capture.cpp   # Capture implementation
│   ├── CMakeLists.txt     # Build config
│   └── d3d11.def          # Export definitions
│
├── blender_addon/         # Blender import addon
│   └── mgs2_anim_import.py
│
├── tools/                 # Utility scripts
│   └── renderdoc_extractor.py
│
└── docs/                  # Documentation
    └── technical_reference.md
```

## Blender Addon Installation

1. Open Blender
2. Edit → Preferences → Add-ons
3. Click "Install..."
4. Select `blender_addon/mgs2_anim_import.py`
5. Enable "Import-Export: MGS2 Animation Import"

## Import Usage

1. File → Import → MGS2 Animation (.json)
2. Select your captured animation file
3. Options:
   - **Action Name**: Name for the animation
   - **Start Frame**: Where to begin on timeline
   - **Create Armature**: Auto-create skeleton from data
   - **Apply Axis Conversion**: Convert to Blender coordinates

## Requirements

- Windows 10/11
- Visual Studio 2022 (for building DLL)
- CMake 3.16+ (for building DLL)
- Python 3.10+
- Blender 3.6+
- RenderDoc (optional, for exploration)

## Game Path

```
D:\Metal Gear Solid 2
```

## Hotkeys (DLL)

| Key | Action |
|-----|--------|
| F9 | Start/Stop capture |

## License

For research and preservation purposes only.
