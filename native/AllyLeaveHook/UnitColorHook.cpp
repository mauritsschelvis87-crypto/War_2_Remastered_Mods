// UnitColorHook.dll — recolors HD unit sprites to the Studio player colors.
//
// Remastered tints HD units from a static 8-entry RGBA float table in the
// game's .data (RVA 0x4C9640). Code paths use that table directly
// (add eax, table_va / movups), so writing the heap "working copy" alone
// never recolors units. Palette .ppl files only feed minimap/UI.
//
// Safety (earlier crashes came from scanning MEM_MAPPED for false-positive
// tables and memcpy'ing into them):
//   - Write ONLY the known master table at kMasterTableRva
//   - Always VirtualProtect around the write
//   - No process-wide memory scans
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
float* g_master = nullptr;
float g_original[kPlayers * 4]{};      // DOS vanilla for restore
float g_colors[kPlayers * 4]{};        // desired colors (RGBA float)
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
            nt->OptionalHeader.SizeOfImage <= kMasterTableRva + kTableBytes) {
            return nullptr;
        }
        float* table = reinterpret_cast<float*>(base + kMasterTableRva);
        return TableLooksValid(table) ? table : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool WriteMaster(const float* values)
{
    if (!g_master || !values) return false;
    float* target = g_master;
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, kTableBytes, PAGE_READWRITE, &oldProtect)) {
        Log("WriteMaster: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    bool ok = true;
    __try {
        memcpy(target, values, kTableBytes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("WriteMaster: exception");
        ok = false;
        g_master = nullptr;
        InterlockedExchange(&g_ready, 0);
    }
    DWORD ignored = 0;
    VirtualProtect(target, kTableBytes, oldProtect, &ignored);
    return ok;
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

DWORD WINAPI WorkThread(LPVOID)
{
    BuildVanillaFloats(g_original);
    LoadColors(true);

    for (int i = 0; i < 100 && !g_stop && !g_master; ++i) {
        g_master = ResolveMasterTable();
        if (!g_master) Sleep(50);
    }
    if (!g_master) {
        Log("master table not found (game build changed?)");
        return 0;
    }
    InterlockedExchange(&g_ready, 1);
    Log("master table @ %p (exe base %p)", g_master, GetModuleHandleW(nullptr));

    bool wasEnabled = false;
    while (!g_stop) {
        if (!g_master) {
            g_master = ResolveMasterTable();
            if (g_master) {
                InterlockedExchange(&g_ready, 1);
                Log("master table @ %p", g_master);
            }
        }

        const bool enabled = InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
        if (enabled && g_master) {
            LoadColors(false);
            WriteMaster(g_colors);
            wasEnabled = true;
        } else if (wasEnabled) {
            WriteMaster(g_original);
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
        if (g_master && InterlockedCompareExchange(&g_ready, 0, 0)) {
            WriteMaster(g_original);
        }
    }
    return TRUE;
}
