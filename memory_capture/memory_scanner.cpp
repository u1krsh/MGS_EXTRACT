/**
 * MGS2 Memory Scanner - Bone Matrix Reader
 * 
 * Reads bone matrices directly from game memory instead of GPU.
 * This DLL injects into MGS2 and scans for bone matrix arrays.
 */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>

// Bone matrix structure (4x4 float = 64 bytes)
struct BoneMatrix {
    float m[4][4];
};

// Global state
static std::atomic<bool> g_running(true);
static std::atomic<bool> g_capturing(false);
static std::vector<std::vector<BoneMatrix>> g_capturedFrames;
static DWORD g_boneArrayAddress = 0;
static int g_boneCount = 0;
static std::ofstream g_logFile;

void Log(const char* msg) {
    if (g_logFile.is_open()) {
        g_logFile << msg << std::endl;
        g_logFile.flush();
    }
    printf("%s\n", msg);
}

// Check if a memory region looks like bone matrices
bool LooksLikeBoneMatrices(const BoneMatrix* matrices, int count) {
    if (count < 20 || count > 200) return false;
    
    int validMatrices = 0;
    for (int i = 0; i < count; i++) {
        const float* m = &matrices[i].m[0][0];
        
        // Check for reasonable values (not NaN, not huge)
        bool valid = true;
        for (int j = 0; j < 16; j++) {
            if (!std::isfinite(m[j]) || std::abs(m[j]) > 10000.0f) {
                valid = false;
                break;
            }
        }
        
        // Check if it looks like a transform matrix (has some structure)
        // A valid bone matrix usually has a non-zero last row element (w)
        if (valid && std::abs(matrices[i].m[3][3]) > 0.5f) {
            validMatrices++;
        }
    }
    
    // At least 50% should look valid
    return validMatrices > count / 2;
}

// Scan memory for bone matrix arrays
bool ScanForBoneArrays(HANDLE hProcess, DWORD baseAddress, DWORD size) {
    // Allocate buffer for reading memory
    std::vector<BYTE> buffer(size);
    SIZE_T bytesRead;
    
    if (!ReadProcessMemory(hProcess, (LPVOID)baseAddress, buffer.data(), size, &bytesRead)) {
        return false;
    }
    
    // Scan for potential bone arrays
    for (DWORD offset = 0; offset < bytesRead - 64 * 100; offset += 16) {
        BoneMatrix* potential = reinterpret_cast<BoneMatrix*>(buffer.data() + offset);
        
        // Try different bone counts
        for (int boneCount = 40; boneCount <= 160; boneCount += 20) {
            if (LooksLikeBoneMatrices(potential, boneCount)) {
                g_boneArrayAddress = baseAddress + offset;
                g_boneCount = boneCount;
                
                std::ostringstream msg;
                msg << "Found potential bone array at 0x" << std::hex << g_boneArrayAddress 
                    << " with " << std::dec << boneCount << " bones";
                Log(msg.str().c_str());
                return true;
            }
        }
    }
    
    return false;
}

// Read current bone matrices from known address
std::vector<BoneMatrix> ReadBoneMatrices() {
    std::vector<BoneMatrix> result;
    
    if (g_boneArrayAddress == 0 || g_boneCount == 0) {
        return result;
    }
    
    result.resize(g_boneCount);
    SIZE_T bytesRead;
    
    HANDLE hProcess = GetCurrentProcess();
    if (ReadProcessMemory(hProcess, (LPVOID)g_boneArrayAddress, 
                          result.data(), g_boneCount * sizeof(BoneMatrix), &bytesRead)) {
        return result;
    }
    
    result.clear();
    return result;
}

// Save captured data to JSON
void SaveCapture(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        Log("Failed to save capture file!");
        return;
    }
    
    file << "{\n";
    file << "  \"metadata\": {\n";
    file << "    \"game\": \"MGS2_Memory_Capture\",\n";
    file << "    \"total_frames\": " << g_capturedFrames.size() << ",\n";
    file << "    \"bone_count\": " << g_boneCount << "\n";
    file << "  },\n";
    file << "  \"frames\": [\n";
    
    for (size_t f = 0; f < g_capturedFrames.size(); f++) {
        const auto& frame = g_capturedFrames[f];
        file << "    {\n";
        file << "      \"frame_index\": " << f << ",\n";
        file << "      \"bone_matrices\": [\n";
        
        for (size_t b = 0; b < frame.size(); b++) {
            file << "        {\n";
            file << "          \"bone_index\": " << b << ",\n";
            file << "          \"matrix\": [\n";
            
            for (int row = 0; row < 4; row++) {
                file << "            [";
                for (int col = 0; col < 4; col++) {
                    float val = frame[b].m[row][col];
                    if (!std::isfinite(val)) val = 0.0f;
                    file << std::fixed << val;
                    if (col < 3) file << ", ";
                }
                file << "]";
                if (row < 3) file << ",";
                file << "\n";
            }
            
            file << "          ]\n";
            file << "        }";
            if (b < frame.size() - 1) file << ",";
            file << "\n";
        }
        
        file << "      ]\n";
        file << "    }";
        if (f < g_capturedFrames.size() - 1) file << ",";
        file << "\n";
    }
    
    file << "  ]\n";
    file << "}\n";
    file.close();
    
    std::ostringstream msg;
    msg << "Saved " << g_capturedFrames.size() << " frames to " << filepath;
    Log(msg.str().c_str());
}

// Main capture thread
void CaptureThread() {
    // Create console
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    
    g_logFile.open("D:\\Metal Gear Solid 2\\memory_capture.log", std::ios::trunc);
    
    printf("=== MGS2 Memory Capture ===\n");
    printf("Press F8 to scan for bone arrays\n");
    printf("Press F9 to start/stop capture\n");
    printf("Press F10 to save capture\n\n");
    
    Log("Memory capture initialized");
    
    bool f8WasDown = false, f9WasDown = false, f10WasDown = false;
    
    while (g_running) {
        bool f8Down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        bool f9Down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        
        // F8 - Scan for bone arrays
        if (f8Down && !f8WasDown) {
            printf("\nScanning memory for bone arrays...\n");
            Log("Starting memory scan...");
            
            // Scan the game's memory regions
            HANDLE hProcess = GetCurrentProcess();
            MEMORY_BASIC_INFORMATION mbi;
            DWORD address = 0x00400000;  // Start from typical base
            
            while (address < 0x7FFFFFFF) {
                if (VirtualQueryEx(hProcess, (LPVOID)address, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                    if (mbi.State == MEM_COMMIT && 
                        (mbi.Protect & PAGE_READWRITE || mbi.Protect & PAGE_READONLY)) {
                        
                        if (ScanForBoneArrays(hProcess, address, mbi.RegionSize)) {
                            printf("Found bone array! Address: 0x%08X, Bones: %d\n", 
                                   g_boneArrayAddress, g_boneCount);
                            break;
                        }
                    }
                    address += mbi.RegionSize;
                } else {
                    address += 0x1000;
                }
            }
            
            if (g_boneArrayAddress == 0) {
                printf("No bone arrays found. Try again during gameplay.\n");
            }
        }
        f8WasDown = f8Down;
        
        // F9 - Toggle capture
        if (f9Down && !f9WasDown) {
            if (g_boneArrayAddress == 0) {
                printf("Press F8 first to find bone arrays!\n");
            } else {
                g_capturing = !g_capturing;
                if (g_capturing) {
                    g_capturedFrames.clear();
                    printf("\n>>> RECORDING... Press F9 to stop <<<\n");
                    Log("Capture started");
                } else {
                    printf("\n>>> STOPPED - %zu frames captured <<<\n", g_capturedFrames.size());
                    Log("Capture stopped");
                }
            }
        }
        f9WasDown = f9Down;
        
        // F10 - Save capture
        if (f10Down && !f10WasDown) {
            if (g_capturedFrames.empty()) {
                printf("No frames to save!\n");
            } else {
                SaveCapture("D:\\Metal Gear Solid 2\\MGS2_MemCapture\\animation.json");
                printf("Saved!\n");
            }
        }
        f10WasDown = f10Down;
        
        // Capture frame if recording
        if (g_capturing && g_boneArrayAddress != 0) {
            auto bones = ReadBoneMatrices();
            if (!bones.empty()) {
                g_capturedFrames.push_back(bones);
            }
        }
        
        Sleep(16);  // ~60fps capture rate
    }
    
    g_logFile.close();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateDirectory(L"D:\\Metal Gear Solid 2\\MGS2_MemCapture", NULL);
        std::thread(CaptureThread).detach();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_running = false;
    }
    return TRUE;
}
