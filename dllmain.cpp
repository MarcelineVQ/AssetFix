// =============================================================================
// assetfix/dllmain.cpp - Loose File Loading & Permissive Patch Glob
// =============================================================================
//
// 1. Patches patch-?.MPQ → patch-*.MPQ so multi-char patch names work
// 2. NOPs two gates in File_FindInArchive so CheckFileExistence runs for
//    all files, not just Interface/AddOns
// 3. Hooks CheckFileExistence with an O(1) hash set of pre-indexed loose
//    files so non-existent files skip GetFileAttributesA entirely
//
// =============================================================================

#include <windows.h>
#include <cstdint>
#include <MinHook.h>
#include "looseFiles.h"

// =============================================================================
// Memory patching helpers
// =============================================================================

static bool patchByte(void* address, uint8_t newValue, uint8_t* oldValue) {
    DWORD oldProtect;
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    if (oldValue) *oldValue = *(uint8_t*)address;
    *(uint8_t*)address = newValue;

    VirtualProtect(address, 1, oldProtect, &oldProtect);
    return true;
}

static bool patchBytes(void* address, const uint8_t* newBytes, uint8_t* oldBytes, size_t len) {
    DWORD oldProtect;
    if (!VirtualProtect(address, len, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    if (oldBytes) memcpy(oldBytes, address, len);
    memcpy(address, newBytes, len);

    VirtualProtect(address, len, oldProtect, &oldProtect);
    return true;
}

// =============================================================================
// Patch 1: Permissive MPQ glob pattern
// =============================================================================
// At 0x82edc2: change '?' (0x3F) to '*' (0x2A)
// Changes "patch-?.MPQ" to "patch-*.MPQ"

static uint8_t g_oldGlobByte = 0;
static bool g_globPatched = false;

static bool applyGlobPatch() {
    void* addr = (void*)0x82edc2;
    if (*(uint8_t*)addr != 0x3F) return false;

    if (patchByte(addr, 0x2A, &g_oldGlobByte)) {
        g_globPatched = true;
        return true;
    }
    return false;
}

static void revertGlobPatch() {
    if (g_globPatched) {
        patchByte((void*)0x82edc2, g_oldGlobByte, nullptr);
        g_globPatched = false;
    }
}

// =============================================================================
// Patch 2 & 3: Remove loose file gates in File_FindInArchive
// =============================================================================
//
// Gate 1 (JZ at 0x654b5c): skips disk check when flags & 3 == 0.
//   Original: 74 25  (JZ +0x25)   Patched: 90 90
//
// Gate 2 (JNZ at 0x654b6a): skips disk check when archive+0x144 != 0.
//   Original: 75 17  (JNZ +0x17)  Patched: 90 90

static uint8_t g_oldJzBytes[2] = {};
static bool g_jzPatched = false;
static uint8_t g_oldJnzBytes[2] = {};
static bool g_jnzPatched = false;

static bool applyLooseFilePatch() {
    // Gate 1: JZ at 0x654b5c
    uint8_t* jz = (uint8_t*)0x654b5c;
    if (jz[0] != 0x74 || jz[1] != 0x25) return false;
    uint8_t nops[2] = { 0x90, 0x90 };
    if (!patchBytes(jz, nops, g_oldJzBytes, 2)) return false;
    g_jzPatched = true;

    // Gate 2: JNZ at 0x654b6a
    uint8_t* jnz = (uint8_t*)0x654b6a;
    if (jnz[0] != 0x75 || jnz[1] != 0x17) return false;
    if (!patchBytes(jnz, nops, g_oldJnzBytes, 2)) return false;
    g_jnzPatched = true;

    return true;
}

static void revertLooseFilePatch() {
    if (g_jzPatched) {
        patchBytes((void*)0x654b5c, g_oldJzBytes, nullptr, 2);
        g_jzPatched = false;
    }
    if (g_jnzPatched) {
        patchBytes((void*)0x654b6a, g_oldJnzBytes, nullptr, 2);
        g_jnzPatched = false;
    }
}

// =============================================================================
// Hook: CheckFileExistence (0x654DD0)
// =============================================================================
// Intercepts disk lookups with O(1) hash set.
// Miss → return 0 (no syscall).  Hit → call original with flags | 1.

typedef uint32_t (__fastcall *CheckFileExistence_t)(
    const char* filename,   // ECX
    uint32_t flags,         // EDX
    uint32_t* outputBuffer  // stack
);

static CheckFileExistence_t p_Original_CheckFileExistence = nullptr;

static uint32_t __fastcall Hook_CheckFileExistence(
    const char* filename,
    uint32_t flags,
    uint32_t* outputBuffer
) {
    if (filename) {
        const char* diskPath = looseFiles_lookup(filename);
        if (!diskPath)
            return 0;
        return p_Original_CheckFileExistence(diskPath, flags | 1, outputBuffer);
    }
    return p_Original_CheckFileExistence(filename, flags, outputBuffer);
}

// =============================================================================
// Init / Cleanup
// =============================================================================

static bool g_installed = false;

static bool install() {
    if (MH_Initialize() != MH_OK) return false;

    applyGlobPatch();
    applyLooseFilePatch();
    looseFiles_init(nullptr);

    // Hook CheckFileExistence
    void* original = nullptr;
    if (MH_CreateHook((void*)0x654DD0, (void*)&Hook_CheckFileExistence, &original) != MH_OK)
        return false;
    p_Original_CheckFileExistence = (CheckFileExistence_t)original;
    if (MH_EnableHook((void*)0x654DD0) != MH_OK)
        return false;

    g_installed = true;
    return true;
}

static void uninstall() {
    if (!g_installed) return;

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    revertLooseFilePatch();
    revertGlobPatch();
    looseFiles_cleanup();
    g_installed = false;
}

// =============================================================================
// DLL entry point
// =============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)hModule;
    (void)lpReserved;

    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        install();
        break;
    case DLL_PROCESS_DETACH:
        uninstall();
        break;
    }

    return TRUE;
}
