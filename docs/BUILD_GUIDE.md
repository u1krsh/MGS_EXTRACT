# MGS2 Animation Capture DLL - Build & Installation Guide

## Quick Requirements Check

You need:
- **Visual Studio 2022** (Community edition is free) with "Desktop development with C++" workload
- **Windows 10/11**

---

## Step 1: Install Visual Studio (if not installed)

1. Download [Visual Studio 2022 Community](https://visualstudio.microsoft.com/downloads/)
2. Run the installer
3. Select **"Desktop development with C++"** workload
4. Click Install (takes 10-20 minutes)

---

## Step 2: Open Developer Command Prompt

**Important**: You must use the Visual Studio command prompt, not regular PowerShell/CMD.

1. Press **Win** key
2. Type: `x64 Native Tools Command Prompt for VS 2022`
3. Click to open it
4. You should see something like:
   ```
   **********************************************************************
   ** Visual Studio 2022 Developer Command Prompt v17.x
   **********************************************************************
   ```

---

## Step 3: Navigate to the Project

In the VS command prompt, type:

```cmd
cd /d d:\PROGRAM\VIBEPRO\MGS_EXTRACT\capture_dll
```

---

## Step 4: Build the DLL

Run the build script:

```cmd
build.bat
```

If successful, you'll see:
```
============================================
  BUILD SUCCESSFUL!
============================================

Output: build\d3d11.dll
```

---

## Step 5: Install the DLL

1. **BACKUP FIRST** (optional but recommended):
   ```cmd
   copy "D:\Metal Gear Solid 2\d3d11.dll" "D:\Metal Gear Solid 2\d3d11.dll.backup" 2>nul
   ```
   (This will fail if no d3d11.dll exists - that's fine)

2. **Copy the capture DLL**:
   ```cmd
   copy build\d3d11.dll "D:\Metal Gear Solid 2\d3d11.dll"
   ```

---

## Step 6: Capture Animation

1. Launch MGS2 normally (Steam, shortcut, etc.)
2. Play to the Tanker intro cutscene
3. Press **F9** to start capture (you'll see a popup)
4. Let the cutscene play
5. Press **F9** again to stop capture

The animation data is saved to:
```
D:\Metal Gear Solid 2\animation_capture.json
```

---

## Step 7: Import to Blender

1. Open Blender
2. Install the addon:
   - Edit → Preferences → Add-ons → Install
   - Select: `d:\PROGRAM\VIBEPRO\MGS_EXTRACT\blender_addon\mgs2_anim_import.py`
   - Enable the checkbox
3. Import animation:
   - File → Import → MGS2 Animation (.json)
   - Select `animation_capture.json`
4. Animation appears on timeline!

---

## Troubleshooting

### "cmake is not recognized"
→ Use the build.bat script instead (it doesn't need CMake)

### "cl.exe not found"
→ You're not using the VS Developer Command Prompt. Open it from Start Menu.

### Game crashes on start
→ Delete `D:\Metal Gear Solid 2\d3d11.dll` to restore normal behavior

### No popup when pressing F9
→ DLL may not be loading. Check:
- File is named exactly `d3d11.dll`
- File is in the same folder as `METAL GEAR SOLID2.exe`

### Empty or small JSON file
→ Make sure to press F9 BEFORE the animation starts, then F9 after it ends

---

## Uninstall

Simply delete `D:\Metal Gear Solid 2\d3d11.dll`

If you made a backup:
```cmd
copy "D:\Metal Gear Solid 2\d3d11.dll.backup" "D:\Metal Gear Solid 2\d3d11.dll"
```
