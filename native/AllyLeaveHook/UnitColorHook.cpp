// UnitColorHook.dll — recolors HD unit sprites to the Studio player colors.
//
// The HD renderer tints the unit team masks with a per-player RGBA float
// color from a static 8-entry table in the game's data (vanilla holds the
// original DOS palette values: P1 164,0,0 ... P8 252,252,72). The palette
// files on disk do NOT feed this table, so the offline color patch never
// reached unit sprites. This hook finds the table by scanning for those
// vanilla float values and keeps writing the colors from player-colors.json
// over it — color changes in the Studio therefore apply live, no restart.
//
// Exports (used by InjectUnitColor.exe):
//   UnitColor_SetEnabled(BOOL)  toggle; disable restores vanilla values
//   UnitColor_IsReady()         table found?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPlayers = 8;
constexpr size_t kEntryBytes = 16;                    // RGBA floats
constexpr size_t kTableBytes = kPlayers * kEntryBytes;
constexpr uintptr_t kMatchInitFlagRva = 0x5B1798;
// Static master tint table in Warcraft II.exe (.data). The renderer copies
// from here into a per-match heap buffer; patch both when possible.
constexpr uintptr_t kMasterTableRva = 0x4C9640;

// Vanilla table contents (original DOS team colors, RGBA float, alpha 1).
const uint8_t kVanillaRgb[kPlayers][3] = {
    { 164,   0,   0 }, // P1 red
    {   0,  60, 192 }, // P2 blue
    {  44, 180, 148 }, // P3 teal
    { 156,  72, 176 }, // P4 violet
    { 240, 132,  20 }, // P5 orange
    {  40,  40,  60 }, // P6 black
    { 206, 205, 212 }, // P7 white
    { 252, 252,  72 }, // P8 yellow
};

volatile LONG g_enabled = 1;
volatile LONG g_ready = 0;
float* g_table = nullptr;              // per-match heap working copy (optional)
float* g_master = nullptr;             // exe .data master table (always present)
float g_original[kPlayers * 4]{};      // saved heap copy for restore
float g_masterOriginal[kPlayers * 4]{}; // saved master for restore
float g_colors[kPlayers * 4]{};        // desired colors (RGBA float)
uint8_t g_matchSnapshot[kTableBytes]{}; // master bytes at match start
bool g_haveMatchSnapshot = false;
FILETIME g_jsonTime{};
HANDLE g_thread = nullptr;
volatile LONG g_stop = 0;

void Log(const char* fmt, ...)
{
    char path[MAX_PATH]{};
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "war2_unit_color_hook.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

void BuildVanillaFloats(float* out)
{
    for (int p = 0; p < kPlayers; ++p) {
        out[p * 4 + 0] = kVanillaRgb[p][0] / 255.0f;
        out[p * 4 + 1] = kVanillaRgb[p][1] / 255.0f;
        out[p * 4 + 2] = kVanillaRgb[p][2] / 255.0f;
        out[p * 4 + 3] = 1.0f;
    }
}

int ReadMatchActive()
{
    HMODULE game = GetModuleHandleW(nullptr);
    if (!game) return -1;
    auto* base = reinterpret_cast<uint8_t*>(game);
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return -1;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.SizeOfImage <= kMatchInitFlagRva) return -1;
        return base[kMatchInitFlagRva] ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool TableLooksValid(const float* table)
{
    if (!table) return false;
    __try {
        for (int p = 0; p < kPlayers; ++p) {
            const float* entry = table + p * 4;
            if (entry[3] != 1.0f) return false;
            for (int c = 0; c < 3; ++c) {
                if (entry[c] < 0.0f || entry[c] > 1.0f) return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float* ResolveMasterTable()
{
    HMODULE game = GetModuleHandleW(nullptr);
    if (!game) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(game);
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.SizeOfImage <= kMasterTableRva) {
            return nullptr;
        }
        float* table = reinterpret_cast<float*>(base + kMasterTableRva);
        return TableLooksValid(table) ? table : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void CaptureMatchSnapshot()
{
    g_haveMatchSnapshot = false;
    g_master = ResolveMasterTable();
    if (!g_master) return;
    __try {
        memcpy(g_matchSnapshot, g_master, kTableBytes);
        g_haveMatchSnapshot = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_haveMatchSnapshot = false;
    }
}

// The renderer clones the master table into a writable heap buffer each match.
// After Studio colors are applied the heap copy no longer matches the old DOS
// signature, so locate it by snapshotting the master table at match start.
float* FindWorkingTable()
{
    if (!g_haveMatchSnapshot) return nullptr;

    float vanilla[kPlayers * 4]{};
    BuildVanillaFloats(vanilla);
    const uint8_t* vanillaNeedle = reinterpret_cast<const uint8_t*>(vanilla + 4);
    const size_t vanillaNeedleLen = (kPlayers - 1) * kEntryBytes;
    const uint8_t* snapshotNeedle = g_matchSnapshot + kEntryBytes;
    const size_t snapshotNeedleLen = kTableBytes - kEntryBytes;
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t stackLow = reinterpret_cast<uintptr_t>(tib->StackLimit);
    const uintptr_t stackHigh = reinterpret_cast<uintptr_t>(tib->StackBase);
    const uintptr_t masterAddr =
        g_master ? reinterpret_cast<uintptr_t>(g_master) : 0;

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0x10000;
    while (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t next = regionBase + mbi.RegionSize;
        const bool ownStack = regionBase < stackHigh && next > stackLow;
        const bool writable =
            (mbi.Protect & PAGE_READWRITE) || (mbi.Protect & PAGE_WRITECOPY);
        const bool candidateRegion =
            !ownStack && mbi.State == MEM_COMMIT && writable &&
            (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED);
        if (candidateRegion) {
            __try {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(regionBase);
                for (size_t i = kEntryBytes; i + kTableBytes <= mbi.RegionSize; i += 4) {
                    float* table = reinterpret_cast<float*>(const_cast<uint8_t*>(p + i));
                    const uintptr_t tableAddr = reinterpret_cast<uintptr_t>(table);
                    if (masterAddr && tableAddr == masterAddr) continue;
                    if (!TableLooksValid(table)) continue;
                    if (memcmp(p + i, g_matchSnapshot, kTableBytes) == 0) return table;
                    if (memcmp(p + i + kEntryBytes, snapshotNeedle, snapshotNeedleLen) == 0) {
                        return reinterpret_cast<float*>(const_cast<uint8_t*>(p + i));
                    }
                    if (memcmp(p + i + kEntryBytes, vanillaNeedle, vanillaNeedleLen) == 0) {
                        return reinterpret_cast<float*>(const_cast<uint8_t*>(p + i));
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // A private region can disappear while the game changes state.
            }
        }
        if (next <= addr) break;
        addr = next;
        if (addr >= 0x7FFF0000) break;
    }
    return nullptr;
}

uint32_t ParseHexRgb(const char* hex, uint32_t fallback)
{
    if (!hex) return fallback;
    while (*hex == '#' || *hex == ' ' || *hex == '"') ++hex;
    unsigned r = 0, g = 0, b = 0;
    if (sscanf_s(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        return (r << 16) | (g << 8) | b;
    }
    return fallback;
}

bool ReadFileAll(const wchar_t* path, char* buf, size_t bufSize)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buf, static_cast<DWORD>(bufSize - 1), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    buf[read] = 0;
    return true;
}

void ConfigPath(wchar_t* out, size_t cap)
{
    out[0] = 0;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ConfigPath), &self) || !self) {
        return;
    }
    wchar_t dllPath[MAX_PATH]{};
    if (!GetModuleFileNameW(self, dllPath, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (!slash) slash = wcsrchr(dllPath, L'/');
    if (slash) slash[1] = 0;
    wchar_t joined[MAX_PATH]{};
    swprintf_s(joined, L"%s..\\player-colors.json", dllPath);
    if (GetFullPathNameW(joined, static_cast<DWORD>(cap), out, nullptr) == 0) {
        wcsncpy_s(out, cap, joined, _TRUNCATE);
    }
}

// Load desired colors: vanilla defaults overridden by player-colors.json.
// Returns true when the file changed since the previous load.
bool LoadColors(bool force)
{
    wchar_t path[MAX_PATH]{};
    ConfigPath(path, MAX_PATH);

    WIN32_FILE_ATTRIBUTE_DATA attr{};
    const bool haveFile = path[0] &&
        GetFileAttributesExW(path, GetFileExInfoStandard, &attr);
    if (!force && haveFile &&
        CompareFileTime(&attr.ftLastWriteTime, &g_jsonTime) == 0) {
        return false;
    }

    BuildVanillaFloats(g_colors);
    if (!haveFile) {
        if (force) Log("LoadColors: no %ls — vanilla", path);
        return force;
    }
    g_jsonTime = attr.ftLastWriteTime;

    char buf[8192]{};
    if (!ReadFileAll(path, buf, sizeof(buf))) return force;

    for (int player = 1; player <= kPlayers; ++player) {
        char key[32]{};
        sprintf_s(key, "\"player\": %d", player);
        const char* p = strstr(buf, key);
        if (!p) {
            sprintf_s(key, "\"player\":%d", player);
            p = strstr(buf, key);
        }
        if (!p) continue;
        const char* colorKey = strstr(p, "\"color\"");
        if (!colorKey || colorKey > p + 120) continue;
        const char* hash = strchr(colorKey, '#');
        if (!hash || hash > colorKey + 40) continue;
        const uint32_t fallback =
            (static_cast<uint32_t>(kVanillaRgb[player - 1][0]) << 16) |
            (static_cast<uint32_t>(kVanillaRgb[player - 1][1]) << 8) |
            kVanillaRgb[player - 1][2];
        const uint32_t rgb = ParseHexRgb(hash, fallback);
        g_colors[(player - 1) * 4 + 0] = ((rgb >> 16) & 0xFF) / 255.0f;
        g_colors[(player - 1) * 4 + 1] = ((rgb >> 8) & 0xFF) / 255.0f;
        g_colors[(player - 1) * 4 + 2] = (rgb & 0xFF) / 255.0f;
        g_colors[(player - 1) * 4 + 3] = 1.0f;
    }
    Log("LoadColors: ok from %ls", path);
    return true;
}

void WriteTable(const float* values)
{
    bool wrote = false;
    if (g_master) {
        __try {
            memcpy(g_master, values, kTableBytes);
            wrote = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("WriteTable: master exception");
            g_master = nullptr;
        }
    }
    if (g_table) {
        __try {
            memcpy(g_table, values, kTableBytes);
            wrote = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("WriteTable: heap exception — table lost");
            g_table = nullptr;
        }
    }
    if (!wrote) InterlockedExchange(&g_ready, 0);
}

void RestoreTables()
{
    if (g_master) {
        __try { memcpy(g_master, g_masterOriginal, kTableBytes); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_master = nullptr; }
    }
    if (g_table) {
        __try { memcpy(g_table, g_original, kTableBytes); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_table = nullptr; }
    }
}

DWORD WINAPI WorkThread(LPVOID)
{
    LoadColors(true);
    g_master = ResolveMasterTable();
    if (g_master) {
        __try { memcpy(g_masterOriginal, g_master, kTableBytes); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_master = nullptr; }
        InterlockedExchange(&g_ready, 1);
        Log("master table @ %p (exe base %p)", g_master, GetModuleHandleW(nullptr));
    } else {
        Log("master table not found (game build changed?)");
    }

    bool wasEnabled = false;
    bool loggedMissing = false;
    int lastMatch = -1;
    while (!g_stop) {
        if (!g_master) {
            g_master = ResolveMasterTable();
            if (g_master) {
                __try { memcpy(g_masterOriginal, g_master, kTableBytes); }
                __except (EXCEPTION_EXECUTE_HANDLER) { g_master = nullptr; }
                if (g_master) {
                    InterlockedExchange(&g_ready, 1);
                    Log("master table @ %p (exe base %p)", g_master, GetModuleHandleW(nullptr));
                }
            }
        }

        const int match = ReadMatchActive();
        if (match != lastMatch) {
            if (match == 1) {
                // Every match gets a newly cloned tint table. Never carry the
                // previous match's heap pointer into the next one.
                g_table = nullptr;
                wasEnabled = false;
                loggedMissing = false;
                CaptureMatchSnapshot();
                Log("new match — locating fresh working table");
            } else if (match == 0 && lastMatch == 1) {
                g_table = nullptr;
                g_haveMatchSnapshot = false;
                wasEnabled = false;
                Log("match ended — discarded working table");
            }
            lastMatch = match;
        }

        if (!g_table && match != 0) {
            g_table = FindWorkingTable();
            if (g_table) {
                __try { memcpy(g_original, g_table, kTableBytes); }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    g_table = nullptr;
                }
                if (g_table) {
                    InterlockedExchange(&g_ready, 1);
                    Log("working table found @ %p (master %p)", g_table, g_master);
                    loggedMissing = false;
                    wasEnabled = false;
                }
            } else if (!loggedMissing) {
                Log("working table not ready — keep checking during match");
                loggedMissing = true;
            }
        }

        const bool enabled = InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
        if (enabled && (g_master || g_table)) {
            LoadColors(false);
            WriteTable(g_colors); // game may refresh tables — keep them ours
            wasEnabled = true;
        } else if (wasEnabled) {
            RestoreTables();
            wasEnabled = false;
            Log("disabled — vanilla restored");
        }
        for (int i = 0; i < 20 && !g_stop; ++i) Sleep(100);
    }
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) void __stdcall UnitColor_SetEnabled(BOOL enable)
{
    InterlockedExchange(&g_enabled, enable ? 1 : 0);
    Log("SetEnabled(%d)", enable ? 1 : 0);
}

extern "C" __declspec(dllexport) DWORD __stdcall UnitColor_IsReady(LPVOID)
{
    return InterlockedCompareExchange(&g_ready, 0, 0) ? 1 : 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        Log("attach");
        g_thread = CreateThread(nullptr, 0, WorkThread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
        if (InterlockedCompareExchange(&g_ready, 0, 0)) {
            RestoreTables();
        }
    }
    return TRUE;
}
