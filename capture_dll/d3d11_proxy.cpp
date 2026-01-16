/**
 * MGS2 Animation Capture - DX11 Proxy DLL
 * 
 * This DLL masquerades as d3d11.dll and forwards all calls to the real DLL
 * while intercepting specific functions to capture bone matrix data.
 * 
 * INSTALLATION:
 * 1. Build this DLL as "d3d11.dll"
 * 2. Place in: D:\Metal Gear Solid 2\ (game root)
 * 3. Run game normally
 * 4. Press F9 to start/stop capture
 * 5. Output saved to: D:\Metal Gear Solid 2\animation_capture.json
 * 
 * IMPORTANT: Backup original d3d11.dll first!
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

#include "bone_capture.h"

// Global flag to stop the input thread
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_threadStarted{false};
static std::thread g_inputThread;

// Status text for in-game overlay
static std::string g_statusText = "";
static std::atomic<bool> g_showStatus{false};
static std::chrono::steady_clock::time_point g_statusShowTime;
static IDXGISwapChain* g_capturedSwapChain = nullptr;

// Forward declaration
void InputThreadFunc();

// Start input thread (call only after DllMain returns!)
void StartInputThreadIfNeeded() {
    bool expected = false;
    if (g_threadStarted.compare_exchange_strong(expected, true)) {
        g_inputThread = std::thread(InputThreadFunc);
    }
}

// Simple file logger for debugging
void LogToFile(const char* message) {
    static bool firstCall = true;
    std::ofstream log("D:\\Metal Gear Solid 2\\capture_debug.log", 
                      firstCall ? std::ios::trunc : std::ios::app);
    firstCall = false;
    if (log.is_open()) {
        log << message << std::endl;
        log.close();
    }
}

// Link against D3D11
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ============================================================================
// ORIGINAL DLL FORWARDING
// ============================================================================

typedef HRESULT(WINAPI* PFN_D3D11CreateDevice)(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
);

typedef HRESULT(WINAPI* PFN_D3D11CreateDeviceAndSwapChain)(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
);

// Real function pointers
static PFN_D3D11CreateDevice Real_D3D11CreateDevice = nullptr;
static PFN_D3D11CreateDeviceAndSwapChain Real_D3D11CreateDeviceAndSwapChain = nullptr;
static HMODULE g_realD3D11 = nullptr;

// Hooked vtable function pointers
typedef void (STDMETHODCALLTYPE* PFN_UpdateSubresource)(
    ID3D11DeviceContext* This,
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    const D3D11_BOX* pDstBox,
    const void* pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch
);

typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(
    IDXGISwapChain* This,
    UINT SyncInterval,
    UINT Flags
);

// DXGI Factory CreateSwapChain hook
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChain)(
    IDXGIFactory* This,
    IUnknown* pDevice,
    DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain
);

// Map/Unmap hooks for games that use these instead of UpdateSubresource
typedef HRESULT(STDMETHODCALLTYPE* PFN_Map)(
    ID3D11DeviceContext* This,
    ID3D11Resource* pResource,
    UINT Subresource,
    D3D11_MAP MapType,
    UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE* pMappedResource
);

typedef void (STDMETHODCALLTYPE* PFN_Unmap)(
    ID3D11DeviceContext* This,
    ID3D11Resource* pResource,
    UINT Subresource
);

// VSSetConstantBuffers hook for camera matrices
typedef void (STDMETHODCALLTYPE* PFN_VSSetConstantBuffers)(
    ID3D11DeviceContext* This,
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer* const* ppConstantBuffers
);

static PFN_UpdateSubresource Real_UpdateSubresource = nullptr;
static PFN_Present Real_Present = nullptr;
static PFN_CreateSwapChain Real_CreateSwapChain = nullptr;
static PFN_Map Real_Map = nullptr;
static PFN_Unmap Real_Unmap = nullptr;
static PFN_VSSetConstantBuffers Real_VSSetConstantBuffers = nullptr;

// Track mapped buffers for capture
struct MappedBufferInfo {
    ID3D11Resource* resource;
    void* data;
    UINT size;
};
static MappedBufferInfo g_lastMappedBuffer = {};

// ============================================================================
// BACKGROUND INPUT THREAD (fallback if Present hook doesn't work)
// ============================================================================

void InputThreadFunc() {
    bool keyWasDown = false;
    
    // Create console for status display
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    printf("=== MGS2 Capture Ready ===\n");
    printf("Press F9 to start/stop capture\n\n");
    
    while (g_running) {
        bool keyIsDown = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        
        if (keyIsDown && !keyWasDown) {
            if (CAPTURE_MANAGER.IsCapturing()) {
                CAPTURE_MANAGER.StopCapture();
                printf("\n>>> CAPTURE STOPPED <<<\n");
                printf("Files saved to: D:\\Metal Gear Solid 2\\MGS2_Capture\\\n\n");
                LogToFile(">>> CAPTURE STOPPED - Files saved to MGS2_Capture folder <<<");
            } else {
                CAPTURE_MANAGER.StartCapture("D:\\Metal Gear Solid 2\\MGS2_Capture");
                printf("\n>>> RECORDING... <<<\n");
                printf("Press F9 again to stop\n");
                LogToFile(">>> CAPTURE STARTED - Press F9 again to stop <<<");
            }
        }
        keyWasDown = keyIsDown;
        
        Sleep(50);  // Check every 50ms
    }
}

// ============================================================================
// HOOK IMPLEMENTATIONS
// ============================================================================

// Counter to limit logging
static int g_updateCount = 0;

void STDMETHODCALLTYPE Hook_UpdateSubresource(
    ID3D11DeviceContext* This,
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    const D3D11_BOX* pDstBox,
    const void* pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch
) {
    // Log buffer sizes (first 20 calls only to avoid spam)
    if (g_updateCount < 20 && pDstResource) {
        ID3D11Buffer* buffer = nullptr;
        if (SUCCEEDED(pDstResource->QueryInterface(&buffer))) {
            D3D11_BUFFER_DESC desc;
            buffer->GetDesc(&desc);
            buffer->Release();
            
            std::ostringstream msg;
            msg << "UpdateSubresource: buffer size=" << desc.ByteWidth 
                << " bytes, bindFlags=" << desc.BindFlags;
            LogToFile(msg.str().c_str());
            g_updateCount++;
        }
    }
    
    // Capture bone matrix data if capturing is active
    CAPTURE_MANAGER.OnConstantBufferUpdate(
        This, pDstResource, DstSubresource,
        pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch
    );

    // Call original
    Real_UpdateSubresource(This, pDstResource, DstSubresource,
        pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

// Hook Map - track which buffer is being mapped
static int g_mapCount = 0;
HRESULT STDMETHODCALLTYPE Hook_Map(
    ID3D11DeviceContext* This,
    ID3D11Resource* pResource,
    UINT Subresource,
    D3D11_MAP MapType,
    UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE* pMappedResource
) {
    HRESULT hr = Real_Map(This, pResource, Subresource, MapType, MapFlags, pMappedResource);
    
    if (SUCCEEDED(hr) && pResource && pMappedResource) {
        // Track this mapped buffer
        ID3D11Buffer* buffer = nullptr;
        if (SUCCEEDED(pResource->QueryInterface(&buffer))) {
            D3D11_BUFFER_DESC desc;
            buffer->GetDesc(&desc);
            buffer->Release();
            
            // Log first 20 Map calls
            if (g_mapCount < 20) {
                std::ostringstream msg;
                msg << "Map: buffer size=" << desc.ByteWidth 
                    << " bytes, bindFlags=" << desc.BindFlags;
                LogToFile(msg.str().c_str());
                g_mapCount++;
            }
            
            // Track for Unmap capture
            g_lastMappedBuffer.resource = pResource;
            g_lastMappedBuffer.data = pMappedResource->pData;
            g_lastMappedBuffer.size = desc.ByteWidth;
        }
    }
    
    return hr;
}

// Hook Unmap - capture the buffer data that was just written
void STDMETHODCALLTYPE Hook_Unmap(
    ID3D11DeviceContext* This,
    ID3D11Resource* pResource,
    UINT Subresource
) {
    // If this is the buffer we tracked in Map, capture its contents
    if (pResource == g_lastMappedBuffer.resource && 
        g_lastMappedBuffer.data != nullptr) {
        
        // Check if this looks like bone data
        if (MGS2Capture::IsBoneMatrixBuffer(g_lastMappedBuffer.size, 24)) {
            int bone_count = g_lastMappedBuffer.size / 64;
            
            // Actually capture the bone matrices
            CAPTURE_MANAGER.CaptureBoneMatrices(g_lastMappedBuffer.data, bone_count);
            
            static bool firstCapture = true;
            if (firstCapture) {
                std::ostringstream msg;
                msg << "Bone data captured via Map/Unmap! size=" << g_lastMappedBuffer.size 
                    << " bones=" << bone_count;
                LogToFile(msg.str().c_str());
                firstCapture = false;
            }
        }
        
        g_lastMappedBuffer = {};  // Clear tracking
    }
    
    Real_Unmap(This, pResource, Subresource);
}

// Hook VSSetConstantBuffers - capture camera matrices from slot 0
static int g_vsConstLogCount = 0;
void STDMETHODCALLTYPE Hook_VSSetConstantBuffers(
    ID3D11DeviceContext* This,
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer* const* ppConstantBuffers
) {
    // Camera matrices are typically in slot 0 (128 bytes = 2 matrices)
    if (StartSlot == 0 && NumBuffers >= 1 && ppConstantBuffers && ppConstantBuffers[0]) {
        D3D11_BUFFER_DESC desc;
        ppConstantBuffers[0]->GetDesc(&desc);
        
        // Log first few calls to understand the format
        if (g_vsConstLogCount < 10) {
            std::ostringstream msg;
            msg << "VSSetConstantBuffers slot=0 size=" << desc.ByteWidth;
            LogToFile(msg.str().c_str());
            g_vsConstLogCount++;
        }
        
        // Camera view+projection matrices = 128 bytes (2 x 64 bytes)
        if (desc.ByteWidth == 128 || desc.ByteWidth == 256) {
            // We'd need to map this buffer to read its contents
            // For now, just log that we found it
            // Full implementation would require copying the buffer
        }
    }
    
    Real_VSSetConstantBuffers(This, StartSlot, NumBuffers, ppConstantBuffers);
}

HRESULT STDMETHODCALLTYPE Hook_Present(
    IDXGISwapChain* This,
    UINT SyncInterval,
    UINT Flags
) {
    // Mark frame boundary for animation capture
    CAPTURE_MANAGER.OnPresent();

    // Render status text overlay if active
    if (g_showStatus && !g_statusText.empty()) {
        // Get the back buffer to render text on
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(This->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
            // Get DXGI surface for GDI rendering
            IDXGISurface1* surface = nullptr;
            if (SUCCEEDED(backBuffer->QueryInterface(IID_PPV_ARGS(&surface)))) {
                HDC hdc = nullptr;
                if (SUCCEEDED(surface->GetDC(FALSE, &hdc))) {
                    // Set up text rendering
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(0, 255, 0));  // Green text
                    
                    HFONT hFont = CreateFontA(
                        24, 0, 0, 0, FW_BOLD,
                        FALSE, FALSE, FALSE,
                        ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, "Consolas"
                    );
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                    
                    // Draw text at top-left with padding
                    RECT rect = {10, 10, 600, 50};
                    DrawTextA(hdc, g_statusText.c_str(), -1, &rect, DT_LEFT | DT_TOP);
                    
                    // Cleanup
                    SelectObject(hdc, hOldFont);
                    DeleteObject(hFont);
                    surface->ReleaseDC(nullptr);
                }
                surface->Release();
            }
            backBuffer->Release();
        }
    }

    return Real_Present(This, SyncInterval, Flags);
}

// Hook for DXGI Factory CreateSwapChain - intercepts swapchain creation
HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(
    IDXGIFactory* This,
    IUnknown* pDevice,
    DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain
) {
    LogToFile("CreateSwapChain intercepted!");
    
    HRESULT hr = Real_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
    
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        // Hook Present on this swapchain
        void** swapChainVTable = *reinterpret_cast<void***>(*ppSwapChain);
        
        // Hook Present (vtable index 8)
        DWORD oldProtect;
        VirtualProtect(&swapChainVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        if (Real_Present == nullptr) {
            Real_Present = reinterpret_cast<PFN_Present>(swapChainVTable[8]);
        }
        swapChainVTable[8] = reinterpret_cast<void*>(Hook_Present);
        VirtualProtect(&swapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);
        
        LogToFile("Present hook installed on swapchain!");
        g_capturedSwapChain = *ppSwapChain;
    }
    
    return hr;
}

// ============================================================================
// VTABLE HOOKING
// ============================================================================

template<typename T>
void HookVTableFunction(void** vtable, int index, T hookFunc, T* originalFunc) {
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    *originalFunc = reinterpret_cast<T>(vtable[index]);
    vtable[index] = reinterpret_cast<void*>(hookFunc);
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

void InstallHooks(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain) {
    // Hook ID3D11DeviceContext::UpdateSubresource (vtable index 48)
    void** contextVTable = *reinterpret_cast<void***>(context);
    HookVTableFunction(contextVTable, 48, Hook_UpdateSubresource, &Real_UpdateSubresource);

    // Hook IDXGISwapChain::Present (vtable index 8)
    void** swapChainVTable = *reinterpret_cast<void***>(swapChain);
    HookVTableFunction(swapChainVTable, 8, Hook_Present, &Real_Present);

    OutputDebugStringA("[MGS2Capture] Hooks installed successfully\n");
}

// ============================================================================
// EXPORTED FUNCTIONS
// ============================================================================

extern "C" {

HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
) {
    OutputDebugStringA("[MGS2Capture] D3D11CreateDevice intercepted\n");
    LogToFile("D3D11CreateDevice called!");

    HRESULT hr = Real_D3D11CreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext
    );

    if (SUCCEEDED(hr) && ppDevice && ppImmediateContext) {
        // Hook UpdateSubresource, Map, and Unmap for bone matrix capture
        void** contextVTable = *reinterpret_cast<void***>(*ppImmediateContext);
        HookVTableFunction(contextVTable, 48, Hook_UpdateSubresource, &Real_UpdateSubresource);
        HookVTableFunction(contextVTable, 14, Hook_Map, &Real_Map);      // Map is vtable index 14
        HookVTableFunction(contextVTable, 15, Hook_Unmap, &Real_Unmap);  // Unmap is vtable index 15
        HookVTableFunction(contextVTable, 7, Hook_VSSetConstantBuffers, &Real_VSSetConstantBuffers);  // VSSetConstantBuffers is vtable index 7
        LogToFile("All hooks installed (UpdateSubresource, Map, Unmap, VSSetConstantBuffers)!");
        
        // Hook DXGI Factory to intercept CreateSwapChain for in-game text overlay
        IDXGIDevice* dxgiDevice = nullptr;
        if (SUCCEEDED((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
                IDXGIFactory* factory = nullptr;
                if (SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory))) {
                    // Hook CreateSwapChain on the factory (vtable index 10)
                    void** factoryVTable = *reinterpret_cast<void***>(factory);
                    HookVTableFunction(factoryVTable, 10, Hook_CreateSwapChain, &Real_CreateSwapChain);
                    LogToFile("DXGI Factory CreateSwapChain hooked!");
                    factory->Release();
                }
                adapter->Release();
            }
            dxgiDevice->Release();
        }
        
        // Start input thread
        StartInputThreadIfNeeded();
        LogToFile("Input thread started from D3D11CreateDevice");
    }

    return hr;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
) {
    OutputDebugStringA("[MGS2Capture] D3D11CreateDeviceAndSwapChain intercepted\n");
    LogToFile("D3D11CreateDeviceAndSwapChain called!");

    HRESULT hr = Real_D3D11CreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain,
        ppDevice, pFeatureLevel, ppImmediateContext
    );

    if (SUCCEEDED(hr)) {
        InstallHooks(
            ppDevice ? *ppDevice : nullptr,
            ppImmediateContext ? *ppImmediateContext : nullptr,
            ppSwapChain ? *ppSwapChain : nullptr
        );
        
        // Start input thread (safe now since we're outside DllMain)
        StartInputThreadIfNeeded();
    }

    return hr;
}

}  // extern "C"

// ============================================================================
// DLL ENTRY POINT
// ============================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        OutputDebugStringA("[MGS2Capture] DLL loaded\n");
        LogToFile("=== DllMain: DLL_PROCESS_ATTACH ===");

        // Load the real d3d11.dll from System32
        {
            char sysDir[MAX_PATH];
            GetSystemDirectoryA(sysDir, MAX_PATH);
            std::string realPath = std::string(sysDir) + "\\d3d11.dll";
            
            g_realD3D11 = LoadLibraryA(realPath.c_str());
            if (!g_realD3D11) {
                MessageBoxA(nullptr, "Failed to load real d3d11.dll!", "MGS2 Capture Error", MB_ICONERROR);
                return FALSE;
            }

            Real_D3D11CreateDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
                GetProcAddress(g_realD3D11, "D3D11CreateDevice"));
            Real_D3D11CreateDeviceAndSwapChain = reinterpret_cast<PFN_D3D11CreateDeviceAndSwapChain>(
                GetProcAddress(g_realD3D11, "D3D11CreateDeviceAndSwapChain"));

            if (!Real_D3D11CreateDevice || !Real_D3D11CreateDeviceAndSwapChain) {
                MessageBoxA(nullptr, "Failed to get D3D11 function pointers!", "MGS2 Capture Error", MB_ICONERROR);
                return FALSE;
            }
        }

        // Initialize default skeleton (Snake - will need to be verified)
        {
            std::vector<MGS2Capture::BoneDefinition> default_skeleton;
            // This is a placeholder - actual bone names need to be discovered
            const char* bone_names[] = {
                "root", "pelvis", "spine_00", "spine_01", "spine_02", "spine_03",
                "neck", "head", "head_end",
                "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
                "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
                "thigh_l", "calf_l", "foot_l", "toe_l",
                "thigh_r", "calf_r", "foot_r", "toe_r"
            };
            
            int parent_indices[] = {
                -1, 0, 1, 2, 3, 4,  // root -> spine chain
                5, 6, 7,           // neck -> head
                5, 9, 10, 11,      // left arm
                5, 13, 14, 15,     // right arm
                1, 17, 18, 19,     // left leg
                1, 21, 22, 23      // right leg
            };

            for (int i = 0; i < 24; ++i) {
                MGS2Capture::BoneDefinition bone;
                bone.index = i;
                bone.name = bone_names[i];
                bone.parent_index = parent_indices[i];
                default_skeleton.push_back(bone);
            }

            CAPTURE_MANAGER.SetSkeleton(default_skeleton);
        }

        // Note: Don't create threads in DllMain - causes loader lock!
        // F9 input is checked in the Present hook instead

        OutputDebugStringA("[MGS2Capture] Ready. Press F9 to start/stop capture.\n");
        break;

    case DLL_PROCESS_DETACH:
        g_running = false;
        
        if (CAPTURE_MANAGER.IsCapturing()) {
            CAPTURE_MANAGER.StopCapture();
        }
        if (g_realD3D11) {
            FreeLibrary(g_realD3D11);
        }
        break;
    }

    return TRUE;
}
