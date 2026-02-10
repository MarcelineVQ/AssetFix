// =============================================================================
// looseFiles.cpp - Pre-indexed loose file lookup
// =============================================================================
//
// Scans Data/ for non-MPQ files and stores normalized relative paths in a
// hash set for O(1) lookup by CheckFileExistence hook.
//
// =============================================================================

#include <windows.h>
#include "looseFiles.h"
#include <cstring>
#include <cstdint>

// =============================================================================
// Hash set (open addressing, power-of-2 size)
// =============================================================================

#define HASH_SET_INITIAL_SIZE 1024
#define HASH_SET_MAX_LOAD     70   // percent

struct HashEntry {
    char* key;          // Normalized game path, e.g. "character\human\humanmale.m2"
    char* diskPath;     // Actual disk path, e.g. "Data\Character\Human\HumanMale.m2"
    uint32_t hash;
};

static HashEntry* g_hashTable = nullptr;
static uint32_t g_hashCapacity = 0;
static uint32_t g_hashCount = 0;

// djb2 hash on normalized (lowercase, backslash) path
static uint32_t computeHash(const char* str) {
    uint32_t hash = 5381;
    while (*str) {
        char c = *str++;
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c += 32;
        hash = ((hash << 5) + hash) + (uint8_t)c;
    }
    return hash;
}

static void normalizePath(char* path) {
    for (char* p = path; *p; p++) {
        if (*p == '/') *p = '\\';
        else if (*p >= 'A' && *p <= 'Z') *p += 32;
    }
}

static bool hashSet_grow() {
    uint32_t newCap = g_hashCapacity ? g_hashCapacity * 2 : HASH_SET_INITIAL_SIZE;
    HashEntry* newTable = (HashEntry*)calloc(newCap, sizeof(HashEntry));
    if (!newTable) return false;

    for (uint32_t i = 0; i < g_hashCapacity; i++) {
        if (g_hashTable[i].key) {
            uint32_t slot = g_hashTable[i].hash & (newCap - 1);
            while (newTable[slot].key)
                slot = (slot + 1) & (newCap - 1);
            newTable[slot] = g_hashTable[i];
        }
    }

    free(g_hashTable);
    g_hashTable = newTable;
    g_hashCapacity = newCap;
    return true;
}

static bool hashSet_insert(const char* key, const char* diskPath) {
    if (g_hashCount * 100 >= g_hashCapacity * HASH_SET_MAX_LOAD) {
        if (!hashSet_grow()) return false;
    }

    uint32_t h = computeHash(key);
    uint32_t slot = h & (g_hashCapacity - 1);

    while (g_hashTable[slot].key) {
        if (g_hashTable[slot].hash == h &&
            _stricmp(g_hashTable[slot].key, key) == 0)
            return true; // already present
        slot = (slot + 1) & (g_hashCapacity - 1);
    }

    size_t keyLen = strlen(key);
    g_hashTable[slot].key = (char*)malloc(keyLen + 1);
    if (!g_hashTable[slot].key) return false;
    memcpy(g_hashTable[slot].key, key, keyLen + 1);
    normalizePath(g_hashTable[slot].key);

    size_t diskLen = strlen(diskPath);
    g_hashTable[slot].diskPath = (char*)malloc(diskLen + 1);
    if (!g_hashTable[slot].diskPath) {
        free(g_hashTable[slot].key);
        g_hashTable[slot].key = nullptr;
        return false;
    }
    memcpy(g_hashTable[slot].diskPath, diskPath, diskLen + 1);

    g_hashTable[slot].hash = h;
    g_hashCount++;
    return true;
}

// Case-insensitive, slash-normalized comparison
static int pathcmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca == '/') ca = '\\';
        if (cb == '/') cb = '\\';
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static const char* hashSet_lookup(const char* key) {
    if (!g_hashTable || !g_hashCapacity) return nullptr;

    uint32_t h = computeHash(key);
    uint32_t slot = h & (g_hashCapacity - 1);

    while (g_hashTable[slot].key) {
        if (g_hashTable[slot].hash == h &&
            pathcmp(g_hashTable[slot].key, key) == 0)
            return g_hashTable[slot].diskPath;
        slot = (slot + 1) & (g_hashCapacity - 1);
    }
    return nullptr;
}

// =============================================================================
// Directory scanning
// =============================================================================

static bool isMpqFile(const char* filename) {
    size_t len = strlen(filename);
    if (len < 4) return false;
    return (_stricmp(filename + len - 4, ".mpq") == 0);
}

static int g_wowDirLen = 0;

static void scanDirectory(const char* fullPath, int baseDirLen) {
    char searchPath[MAX_PATH];
    WIN32_FIND_DATAA fd;

    wsprintfA(searchPath, "%s\\*", fullPath);
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == '\0' ||
             (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
            continue;

        char childPath[MAX_PATH];
        wsprintfA(childPath, "%s\\%s", fullPath, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scanDirectory(childPath, baseDirLen);
        } else if (!isMpqFile(fd.cFileName)) {
            const char* gameKey = childPath + baseDirLen;
            const char* diskPath = childPath + g_wowDirLen;
            hashSet_insert(gameKey, diskPath);
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

// =============================================================================
// Public API
// =============================================================================

void looseFiles_init(const char* wowDir) {
    char dirBuf[MAX_PATH];

    if (!wowDir) {
        GetModuleFileNameA(nullptr, dirBuf, MAX_PATH);
        char* lastSlash = strrchr(dirBuf, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        else strcat(dirBuf, "\\");
        wowDir = dirBuf;
    }

    g_wowDirLen = (int)strlen(wowDir);
    hashSet_grow();

    char dataPath[MAX_PATH];
    wsprintfA(dataPath, "%sData", wowDir);
    int dataDirLen = g_wowDirLen + 5;  // +5 for "Data\"

    DWORD attr = GetFileAttributesA(dataPath);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        scanDirectory(dataPath, dataDirLen);
}

void looseFiles_cleanup() {
    if (g_hashTable) {
        for (uint32_t i = 0; i < g_hashCapacity; i++) {
            free(g_hashTable[i].key);
            free(g_hashTable[i].diskPath);
        }
        free(g_hashTable);
        g_hashTable = nullptr;
    }
    g_hashCapacity = 0;
    g_hashCount = 0;
    g_wowDirLen = 0;
}

const char* looseFiles_lookup(const char* gamePath) {
    return hashSet_lookup(gamePath);
}
