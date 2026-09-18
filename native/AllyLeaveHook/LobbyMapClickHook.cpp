// Warcraft II Remastered — click lobby map name → open map .jpg preview.
// Cache resets on match/lobby leave and on lobby-title change.
// Resolve = exact lobby title == .pud/.jpg basename; watcher opens/creates .jpg.
//
// NEVER ShellExecute from inside the game process (that crashed Remastered).
// On click we write a request file + signal; AllyLeaveWatch opens the .jpg
// (creates it if missing). No Explorer folder windows.
// See scripts/research/lobby-map-click-findings.txt

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#pragma comment(lib, "User32.lib")

// File-scope (not anonymous) so the naked probe can call/jmp with stable names.
static volatile DWORD g_lastMpLobbyTick = 0;
static uint8_t* g_layoutCallSite = nullptr;
static uint8_t g_layoutCallOrig[5]{};
static void* g_layoutAbs = nullptr;
static bool g_layoutProbeInstalled = false;
static char g_probeLogPath[MAX_PATH]{};

static void ProbeLog(const char* fmt, ...)
{
    if (!g_probeLogPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_probeLogPath, "%swar2_lobby_map_click.log", temp);
    }
    FILE* f = nullptr;
    if (fopen_s(&f, g_probeLogPath, "a") != 0 || !f) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

extern "C" void __cdecl UpdateMpLobbyTick()
{
    InterlockedExchange(&g_lastMpLobbyTick, GetTickCount());
}

extern "C" void __declspec(naked) MpLobbyLayoutProbe()
{
    __asm {
        pushad
        pushfd
        call UpdateMpLobbyTick
        popfd
        popad
        jmp dword ptr [g_layoutAbs]
    }
}

static void UninstallMpLobbyLayoutProbe()
{
    if (!g_layoutProbeInstalled || !g_layoutCallSite) return;
    DWORD oldProt = 0;
    if (VirtualProtect(g_layoutCallSite, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        memcpy(g_layoutCallSite, g_layoutCallOrig, 5);
        VirtualProtect(g_layoutCallSite, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), g_layoutCallSite, 5);
    }
    g_layoutProbeInstalled = false;
    g_layoutCallSite = nullptr;
    g_layoutAbs = nullptr;
    InterlockedExchange(&g_lastMpLobbyTick, 0);
    ProbeLog("lobby layout probe: removed");
}

static bool InstallMpLobbyLayoutProbe(uint8_t* imageBase)
{
    if (g_layoutProbeInstalled) return true;
    if (!imageBase) return false;

    constexpr uintptr_t kMpLobbyLayoutRva = 0x5362E0 - 0x400000;
    constexpr uintptr_t kMpLobbyLayoutCallRva = 0x533103 - 0x400000;

    uint8_t* callSite = imageBase + kMpLobbyLayoutCallRva;
    void* layout = imageBase + kMpLobbyLayoutRva;
    __try {
        if (callSite[0] != 0xE8) {
            ProbeLog("lobby layout probe: unexpected opcode %02X at call site", callSite[0]);
            return false;
        }
        const int32_t rel = *reinterpret_cast<int32_t*>(callSite + 1);
        const uint8_t* dest = callSite + 5 + rel;
        if (dest != reinterpret_cast<uint8_t*>(layout)) {
            ProbeLog("lobby layout probe: call target mismatch");
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ProbeLog("lobby layout probe: call site unreadable");
        return false;
    }

    const intptr_t stubRel = reinterpret_cast<uint8_t*>(&MpLobbyLayoutProbe) - (callSite + 5);
    if (stubRel < INT32_MIN || stubRel > INT32_MAX) {
        ProbeLog("lobby layout probe: stub too far for rel32");
        return false;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect(callSite, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        ProbeLog("lobby layout probe: VirtualProtect failed %lu", GetLastError());
        return false;
    }
    memcpy(g_layoutCallOrig, callSite, 5);
    callSite[0] = 0xE8;
    const int32_t stubRel32 = static_cast<int32_t>(stubRel);
    memcpy(callSite + 1, &stubRel32, 4);
    VirtualProtect(callSite, 5, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), callSite, 5);

    g_layoutAbs = layout;
    g_layoutCallSite = callSite;
    g_layoutProbeInstalled = true;
    ProbeLog("lobby layout probe: installed");
    return true;
}

namespace {

volatile LONG g_enabled = 0;
volatile LONG g_stop = 0;
HANDLE g_thread = nullptr;
HANDLE g_openEvent = nullptr;
DWORD g_enabledTick = 0;

constexpr uintptr_t kMsgInitFlagRva = 0x9B1798 - 0x400000;
constexpr DWORD kMpLobbyFreshMs = 60000; // idle lobby often stops layout calls for many seconds
// Hitbox fractions — tuned per aspect (ultrawide map row sits lower/center).
constexpr float kHitL = 0.05f;
constexpr float kHitR = 0.62f;
constexpr float kHitT = 0.07f;
constexpr float kHitB = 0.30f;
// Ultrawide (3440x1440): map title sits mid-row.
constexpr float kUwHitL = 0.34f;
constexpr float kUwHitR = 0.80f;
constexpr float kUwHitT = 0.32f;
constexpr float kUwHitB = 0.72f; // was 0.50; map name/preview row sits lower for some hosts
constexpr DWORD kDebounceMs = 900;
constexpr DWORD kEnableGraceMs = 4000;
constexpr wchar_t kOpenEventName[] = L"Local\\War2LobbyMapOpenRequest";

char g_logPath[MAX_PATH]{};
wchar_t g_cachedPud[MAX_PATH]{};
wchar_t g_cachedTitleKey[MAX_PATH]{}; // lobby map basename last seen
size_t g_lastResolveTitleReg = 0;
int g_lastResolveScore = 0;
wchar_t g_mapsRoot[MAX_PATH]{};
CRITICAL_SECTION g_cacheLock;
DWORD g_indexBuiltTick = 0;

void Log(const char* fmt, ...);

struct PudCandidate {
    std::wstring path;
    int score = 0;
    ULONGLONG access = 0;
    bool fromLobbyTitle = false; // exact lobby title / basename sighting
    int titleHits = 0;           // how often we saw this exact title
    size_t titleRegion = 0;      // smaller = more likely the Map value field
    size_t sourceRegion = 0;     // MEMORY_BASIC region size where we saw it
};

std::unordered_map<std::wstring, std::wstring> g_mapsByBase; // lower basename → full .pud path

std::wstring ToLowerKey(const wchar_t* s)
{
    std::wstring out;
    if (!s) return out;
    out.reserve(wcslen(s));
    for (const wchar_t* p = s; *p; ++p) {
        wchar_t c = *p;
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        out.push_back(c);
    }
    return out;
}

std::wstring BasenameKeyFromPath(const wchar_t* path)
{
    if (!path || !path[0]) return {};
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    std::wstring key = ToLowerKey(base);
    if (key.size() > 4 && key.compare(key.size() - 4, 4, L".pud") == 0)
        key.resize(key.size() - 4);
    return key;
}

bool PathUnderRoot(const wchar_t* path, const wchar_t* root)
{
    if (!path || !path[0] || !root || !root[0]) return false;
    wchar_t fullPath[MAX_PATH]{};
    wchar_t fullRoot[MAX_PATH]{};
    if (!GetFullPathNameW(path, MAX_PATH, fullPath, nullptr)) return false;
    if (!GetFullPathNameW(root, MAX_PATH, fullRoot, nullptr)) return false;
    size_t n = wcslen(fullRoot);
    while (n > 0 && (fullRoot[n - 1] == L'\\' || fullRoot[n - 1] == L'/')) {
        fullRoot[--n] = 0;
    }
    if (_wcsnicmp(fullPath, fullRoot, n) != 0) return false;
    return fullPath[n] == 0 || fullPath[n] == L'\\' || fullPath[n] == L'/';
}

void IndexMapsFolderRecursive(const wchar_t* dir)
{
    wchar_t pattern[MAX_PATH]{};
    swprintf_s(pattern, L"%s\\*", dir);
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        wchar_t full[MAX_PATH]{};
        swprintf_s(full, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            IndexMapsFolderRecursive(full);
            continue;
        }
        const size_t n = wcslen(fd.cFileName);
        if (n < 5 || _wcsicmp(fd.cFileName + n - 4, L".pud") != 0) continue;
        std::wstring key = BasenameKeyFromPath(full);
        if (key.empty()) continue;

        auto it = g_mapsByBase.find(key);
        if (it == g_mapsByBase.end()) {
            g_mapsByBase.emplace(key, full);
            continue;
        }
        // Duplicate basename: prefer sibling .jpg, then shorter path, then newer access.
        wchar_t jpgNew[MAX_PATH]{};
        wchar_t jpgOld[MAX_PATH]{};
        wcsncpy_s(jpgNew, full, _TRUNCATE);
        wcsncpy_s(jpgOld, it->second.c_str(), _TRUNCATE);
        wchar_t* d1 = wcsrchr(jpgNew, L'.');
        wchar_t* d2 = wcsrchr(jpgOld, L'.');
        if (d1) wcscpy_s(d1, MAX_PATH - (d1 - jpgNew), L".jpg");
        if (d2) wcscpy_s(d2, MAX_PATH - (d2 - jpgOld), L".jpg");
        const bool newHasJpg = GetFileAttributesW(jpgNew) != INVALID_FILE_ATTRIBUTES;
        const bool oldHasJpg = GetFileAttributesW(jpgOld) != INVALID_FILE_ATTRIBUTES;
        if (newHasJpg && !oldHasJpg) {
            it->second = full;
            continue;
        }
        if (!newHasJpg && oldHasJpg) continue;
        if (wcslen(full) < it->second.size()) it->second = full;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool ReadMapsPathFromSettings(wchar_t* out, size_t outChars)
{
    out[0] = 0;
    wchar_t dllPath[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ReadMapsPathFromSettings), &self) || !self)
        return false;
    if (!GetModuleFileNameW(self, dllPath, MAX_PATH)) return false;
    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    wchar_t settingsPath[MAX_PATH]{};
    swprintf_s(settingsPath, L"%s..\\studio-settings.json", dllPath);

    HANDLE file = CreateFileW(settingsPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char buf[4096]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) return false;
    buf[read] = 0;

    const char* key = strstr(buf, "\"mapsPath\"");
    if (!key) key = strstr(buf, "\"MapsPath\"");
    if (!key) return false;
    const char* colon = strchr(key, ':');
    if (!colon) return false;
    const char* q1 = strchr(colon, '"');
    if (!q1) return false;
    ++q1;
    const char* q2 = q1;
    while (*q2 && *q2 != '"') {
        if (*q2 == '\\' && q2[1]) q2 += 2;
        else ++q2;
    }
    if (*q2 != '"') return false;

    char pathA[MAX_PATH]{};
    size_t o = 0;
    for (const char* p = q1; p < q2 && o + 1 < sizeof(pathA); ++p) {
        if (*p == '\\' && p + 1 < q2) {
            ++p;
            if (*p == 'u' && p + 4 < q2) { // skip \uXXXX
                p += 4;
                continue;
            }
            pathA[o++] = *p;
            continue;
        }
        pathA[o++] = *p;
    }
    pathA[o] = 0;
    if (!pathA[0]) return false;
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, out, static_cast<int>(outChars));
    return out[0] != 0 && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

void EnsureMapsIndex(bool force)
{
    EnterCriticalSection(&g_cacheLock);
    const DWORD now = GetTickCount();
    if (!force && g_indexBuiltTick != 0 && (now - g_indexBuiltTick) < 60000 && g_mapsRoot[0] &&
        !g_mapsByBase.empty()) {
        LeaveCriticalSection(&g_cacheLock);
        return;
    }

    wchar_t root[MAX_PATH]{};
    if (!ReadMapsPathFromSettings(root, MAX_PATH)) {
        // Fallback: game x86\Maps next to Warcraft II.exe
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (slash) {
            slash[1] = 0;
            swprintf_s(root, L"%sMaps", exe);
        }
    }
    if (!root[0] || GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) {
        g_mapsRoot[0] = 0;
        g_mapsByBase.clear();
        g_indexBuiltTick = now;
        LeaveCriticalSection(&g_cacheLock);
        Log("maps index: no maps root");
        return;
    }

    if (!force && _wcsicmp(g_mapsRoot, root) == 0 && !g_mapsByBase.empty() &&
        (now - g_indexBuiltTick) < 60000) {
        LeaveCriticalSection(&g_cacheLock);
        return;
    }

    wcsncpy_s(g_mapsRoot, root, _TRUNCATE);
    g_mapsByBase.clear();
    IndexMapsFolderRecursive(g_mapsRoot);
    g_indexBuiltTick = now;
    const unsigned count = static_cast<unsigned>(g_mapsByBase.size());
    LeaveCriticalSection(&g_cacheLock);
    Log("maps index: root=%ls count=%u", root, count);
}

// Copies resolved path under lock; returns false if unknown.
bool ResolveUnderMapsRootCopy(const wchar_t* pathOrName, wchar_t* out, size_t outChars)
{
    if (!pathOrName || !pathOrName[0] || !out || outChars == 0) return false;
    out[0] = 0;
    EnterCriticalSection(&g_cacheLock);
    if (g_mapsByBase.empty()) {
        LeaveCriticalSection(&g_cacheLock);
        return false;
    }
    std::wstring key = BasenameKeyFromPath(pathOrName);
    if (key.empty()) key = ToLowerKey(pathOrName);
    auto it = g_mapsByBase.find(key);
    const bool ok = it != g_mapsByBase.end();
    if (ok) wcsncpy_s(out, outChars, it->second.c_str(), _TRUNCATE);
    LeaveCriticalSection(&g_cacheLock);
    return ok;
}

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_lobby_map_click.log", temp);
    }
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") != 0 || !f) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

uint8_t* GameBase()
{
    HMODULE game = GetModuleHandleW(L"Warcraft II.exe");
    if (!game) game = GetModuleHandleW(nullptr);
    return reinterpret_cast<uint8_t*>(game);
}

size_t GameImageSize(uint8_t* base)
{
    if (!base) return 0;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

bool InActiveMatch()
{
    uint8_t* base = GameBase();
    if (!base) return false;
    uint8_t* flag = base + kMsgInitFlagRva;
    __try {
        return *flag != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// True only while MP lobby layout has run recently (not campaign / menus / match).
bool InMpLobbyScreen()
{
    if (InActiveMatch()) return false;
    if (!g_layoutProbeInstalled) return false;
    const DWORD last = InterlockedCompareExchange(&g_lastMpLobbyTick, 0, 0);
    if (last == 0) return false;
    const DWORD now = GetTickCount();
    return (now - last) <= kMpLobbyFreshMs;
}

struct EnumCtx {
    HWND best;
    DWORD pid;
};

BOOL CALLBACK EnumGameWindowsProc(HWND hwnd, LPARAM lp)
{
    auto* c = reinterpret_cast<EnumCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != c->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, 256);
    if (!title[0]) return TRUE;
    if (wcsstr(title, L"Warcraft") || wcsstr(title, L"warcraft")) {
        c->best = hwnd;
        return FALSE;
    }
    if (!c->best) c->best = hwnd;
    return TRUE;
}

HWND FindGameWindow()
{
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(fg))
            return fg;
    }
    EnumCtx ctx{};
    ctx.pid = GetCurrentProcessId();
    EnumWindows(EnumGameWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

bool CursorInMapHitbox(HWND hwnd, POINT* outClient, int* outW, int* outH)
{
    POINT pt{};
    if (!GetCursorPos(&pt)) return false;
    if (!ScreenToClient(hwnd, &pt)) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (outClient) *outClient = pt;
    if (outW) *outW = w;
    if (outH) *outH = h;
    if (w <= 0 || h <= 0) return false;

    float hitL = kHitL, hitR = kHitR, hitT = kHitT, hitB = kHitB;
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    if (aspect >= 2.0f) {
        hitL = kUwHitL;
        hitR = kUwHitR;
        hitT = kUwHitT;
        hitB = kUwHitB;
    }

    const int x0 = static_cast<int>(w * hitL);
    const int x1 = static_cast<int>(w * hitR);
    const int y0 = static_cast<int>(h * hitT);
    const int y1 = static_cast<int>(h * hitB);
    return pt.x >= x0 && pt.x <= x1 && pt.y >= y0 && pt.y <= y1;
}

bool IsCampaignPath(const wchar_t* path)
{
    if (!path || !path[0]) return false;
    std::wstring lower(path);
    for (auto& c : lower) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return lower.find(L"\\campaign\\") != std::wstring::npos ||
           lower.find(L"/campaign/") != std::wstring::npos ||
           lower.find(L"\\campaign/") != std::wstring::npos;
}

bool IsPreferredMapPath(const wchar_t* path)
{
    if (!path || !path[0] || IsCampaignPath(path)) return false;
    std::wstring lower(path);
    for (auto& c : lower) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return lower.find(L"\\maps\\") != std::wstring::npos ||
           lower.find(L"/maps/") != std::wstring::npos ||
           lower.find(L"allmaps") != std::wstring::npos ||
           lower.find(L"scenario") != std::wstring::npos ||
           lower.find(L"ladder") != std::wstring::npos ||
           lower.find(L"warcraft2remastered") != std::wstring::npos ||
           lower.find(L"saved games") != std::wstring::npos;
}

bool EndsWithPudA(const char* s, size_t n)
{
    if (n < 4) return false;
    return _strnicmp(s + n - 4, ".pud", 4) == 0;
}

bool EndsWithPudW(const wchar_t* s, size_t n)
{
    if (n < 4) return false;
    return _wcsnicmp(s + n - 4, L".pud", 4) == 0;
}

bool LooksLikePathCharA(unsigned char c)
{
    return c >= 32 && c < 127 && c != '"' && c != '<' && c != '>' && c != '|';
}

void ConsiderCandidate(std::vector<PudCandidate>& list, const wchar_t* path, size_t regionSize)
{
    if (!path || !path[0]) return;
    if (wcslen(path) >= MAX_PATH) return;

    // Prefer the Studio maps-root copy (nested folders OK) over random memory paths.
    wchar_t underRoot[MAX_PATH]{};
    const bool hasUnder = ResolveUnderMapsRootCopy(path, underRoot, MAX_PATH);
    wchar_t mapsRootCopy[MAX_PATH]{};
    EnterCriticalSection(&g_cacheLock);
    wcsncpy_s(mapsRootCopy, g_mapsRoot, _TRUNCATE);
    LeaveCriticalSection(&g_cacheLock);

    wchar_t chosen[MAX_PATH]{};
    if (hasUnder) {
        wcsncpy_s(chosen, underRoot, _TRUNCATE);
    } else if (mapsRootCopy[0]) {
        // Maps root is configured: ignore puds outside it.
        if (!PathUnderRoot(path, mapsRootCopy)) return;
        wcsncpy_s(chosen, path, _TRUNCATE);
    } else {
        wcsncpy_s(chosen, path, _TRUNCATE);
    }
    if (GetFileAttributesW(chosen) == INVALID_FILE_ATTRIBUTES) return;
    if (IsCampaignPath(chosen)) return;

    int score = 10;
    if (IsPreferredMapPath(chosen)) score += 100;
    if (mapsRootCopy[0] && PathUnderRoot(chosen, mapsRootCopy)) score += 120;
    if (chosen[1] == L':' || (chosen[0] == L'\\' && chosen[1] == L'\\')) score += 20;
    if (regionSize > 0 && regionSize <= 8192) score += 80;
    else if (regionSize > 0 && regionSize <= 65536) score += 40;
    else if (regionSize > 0 && regionSize <= 256 * 1024) score += 10;
    const size_t len = wcslen(chosen);
    if (len > 140) score -= 40;
    else if (len > 100) score -= 15;

    // Sibling .jpg already present → useful, but not enough to override the lobby title.
    wchar_t jpg[MAX_PATH]{};
    wcsncpy_s(jpg, chosen, _TRUNCATE);
    wchar_t* dot = wcsrchr(jpg, L'.');
    if (dot) {
        wcscpy_s(dot, MAX_PATH - (dot - jpg), L".jpg");
        if (GetFileAttributesW(jpg) != INVALID_FILE_ATTRIBUTES) score += 30;
    }

    // Do NOT boost by last-access time — opening the wrong map made Shared sticky.

    for (auto& c : list) {
        if (_wcsicmp(c.path.c_str(), chosen) == 0) {
            if (score > c.score) c.score = score;
            if (regionSize > 0 && (c.sourceRegion == 0 || regionSize < c.sourceRegion))
                c.sourceRegion = regionSize;
            if (regionSize > 0 && regionSize <= 8192)
                c.score += 5;
            return;
        }
    }
    list.push_back({ chosen, score, 0, false, 0, 0, regionSize });
}

void ConsiderBasename(std::vector<PudCandidate>& list, const wchar_t* name, size_t regionSize)
{
    // Bare exact title. Path listings are full paths; this matches Map value only.
    if (!name || !name[0]) return;
    if (regionSize == 0) return;
    const size_t nlen = wcslen(name);
    // Short tokens / single Classic names ("Rivers") spam memory — not lobby titles.
    if (nlen < 5 || nlen > 80) return;
    // Lobby display titles are multi-word ("Crosshair BNE"). Reject single tokens here.
    if (!wcschr(name, L' ')) return;
    // Remastered internal ids look like UUIDs - never lobby display titles.
    {
        int hy = 0, hexish = 0;
        for (size_t i = 0; i < nlen; ++i) {
            const wchar_t c = name[i];
            if (c == L'-') ++hy;
            else if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'))
                ++hexish;
        }
        if (hy >= 4 && hexish >= 20) return;
    }

    auto markTitle = [&](const wchar_t* resolved) {
        ConsiderCandidate(list, resolved, regionSize);
        for (auto& c : list) {
            if (_wcsicmp(c.path.c_str(), resolved) == 0) {
                c.fromLobbyTitle = true;
                c.titleHits += 1;
                if (c.titleRegion == 0 || regionSize < c.titleRegion)
                    c.titleRegion = regionSize;
                int boost = 120;
                if (regionSize <= 64) boost += 120;
                else if (regionSize <= 256) boost += 60;
                else if (regionSize <= 4096) boost += 20;
                if (wcschr(name, L' ')) boost += 200;
                // Hosted BNE maps show "... BNE" in the lobby title.
                if (nlen >= 4 && _wcsicmp(name + (nlen - 4), L" BNE") == 0)
                    boost += 800;
                c.score += boost;
                return true;
            }
        }
        return false;
    };

    // Exact basename only: lobby title == pud stem == jpg stem (case-insensitive).
    wchar_t resolved[MAX_PATH]{};
    if (ResolveUnderMapsRootCopy(name, resolved, MAX_PATH))
        markTitle(resolved);
}

void TryAsciiPath(std::vector<PudCandidate>& list, const char* start, size_t maxLen, size_t regionSize)
{
    size_t n = 0;
    while (n < maxLen && LooksLikePathCharA(static_cast<unsigned char>(start[n]))) ++n;
    if (!EndsWithPudA(start, n)) return;
    wchar_t wide[MAX_PATH]{};
    if (MultiByteToWideChar(CP_ACP, 0, start, static_cast<int>(n), wide, MAX_PATH) <= 0) return;
    wide[n < MAX_PATH ? n : MAX_PATH - 1] = 0;
    ConsiderCandidate(list, wide, regionSize);
}

void TryWidePath(std::vector<PudCandidate>& list, const wchar_t* start, size_t maxChars, size_t regionSize)
{
    size_t n = 0;
    while (n < maxChars) {
        const wchar_t c = start[n];
        if (c == 0) break;
        if (c < 32 || c == L'"' || c == L'<' || c == L'>' || c == L'|') break;
        ++n;
    }
    if (!EndsWithPudW(start, n)) return;
    wchar_t buf[MAX_PATH]{};
    if (n >= MAX_PATH) return;
    wcsncpy_s(buf, start, n);
    ConsiderCandidate(list, buf, regionSize);
}

void ScanRegion(std::vector<PudCandidate>& list, const uint8_t* p, size_t size)
{
    if (!p || size < 8) return;
    if (size > 512 * 1024) size = 512 * 1024;

    for (size_t i = 0; i + 5 < size; ++i) {
        if ((p[i] == 'M' || p[i] == 'm' ||
             (p[i] >= 'A' && p[i] <= 'Z' && i + 1 < size && p[i + 1] == ':') ||
             p[i] == '\\') &&
            LooksLikePathCharA(p[i])) {
            size_t remain = size - i;
            if (remain > 260) remain = 260;
            bool hasPud = false;
            for (size_t j = 4; j + 4 <= remain; ++j) {
                if (_strnicmp(reinterpret_cast<const char*>(p + i + j - 4), ".pud", 4) == 0) {
                    hasPud = true;
                    break;
                }
            }
            if (hasPud) TryAsciiPath(list, reinterpret_cast<const char*>(p + i), remain, size);
        }
        if (i + 10 < size && p[i + 1] == 0 && LooksLikePathCharA(p[i]) &&
            (p[i] == 'M' || p[i] == 'm' || p[i] == '\\' ||
             (p[i] >= 'A' && p[i] <= 'Z' && p[i + 2] == ':' && p[i + 3] == 0))) {
            size_t remainBytes = size - i;
            if (remainBytes > 520) remainBytes = 520;
            TryWidePath(list, reinterpret_cast<const wchar_t*>(p + i), remainBytes / 2, size);
        }

        // Lobby Map title: bare UTF-16 name == pud stem (null-terminated).
        // Used as fallback when Map-adjacent resolve finds nothing; pick logic
        // still prefers titleRegion==32 and multi-word titles.
        if ((i % 2) == 0 && i + 12 < size && p[i + 1] == 0) {
            const wchar_t* ws = reinterpret_cast<const wchar_t*>(p + i);
            size_t n = 0;
            const size_t maxChars = (size - i) / 2;
            while (n < maxChars && n < 80) {
                const wchar_t c = ws[n];
                if (c == 0) break;
                if (c < 32 || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L'\\' || c == L'/')
                    break;
                ++n;
            }
            if (n >= 5 && n < 80 && n < maxChars && ws[n] == 0) {
                wchar_t name[100]{};
                wcsncpy_s(name, ws, n);
                // Reject UUID-like / single-token noise is handled in ConsiderBasename + pick.
                ConsiderBasename(list, name, (n + 1) * sizeof(wchar_t));
            }
        }
    }
}

void ScanRegionSEH(std::vector<PudCandidate>& list, const uint8_t* p, size_t size)
{
    // Isolated so ResolvePudFromMemoryInto can keep C++ objects (MSVC C2712).
    __try {
        ScanRegion(list, p, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool PathIsUnderModule(const uint8_t* p, uint8_t* base, size_t imageSize)
{
    if (!base || !imageSize) return false;
    return p >= base && p < base + imageSize;
}


// Remastered has no reliable UIA for the Map row. Find UTF-16 "Map"/"Map:" labels in
// writable memory and resolve nearby bare names against the Studio maps index.
void ConsiderNearMapLabel(std::vector<PudCandidate>& list, const uint8_t* p, size_t size)
{
    if (!p || size < 8) return;
    const wchar_t* labels[] = { L"Map", L"Map:" };
    for (size_t i = 0; i + 4 < size; i += 2) {
        if (p[i + 1] != 0) continue;
        const wchar_t* ws = reinterpret_cast<const wchar_t*>(p + i);
        for (const wchar_t* lab : labels) {
            const size_t labLen = wcslen(lab);
            bool match = true;
            for (size_t k = 0; k < labLen; ++k) {
                if (i + 2 * k + 1 >= size || ws[k] != lab[k]) { match = false; break; }
            }
            if (!match) continue;
            const wchar_t after = (i + 2 * labLen + 1 < size) ? ws[labLen] : 0;
            if (after != 0 && after != L' ' && after != L'\t' && after != L'\r' && after != L'\n')
                continue;

            const size_t winStart = i + 2 * labLen;
            const size_t winEnd = (std::min)(size, winStart + 768);

            // Full .pud path next to Map label (best signal for selected map).
            for (size_t j = winStart; j + 8 < winEnd; ++j) {
                size_t remain = winEnd - j;
                if (remain > 260) remain = 260;
                if (p[j] == 'M' || p[j] == 'm' || p[j] == '\\' ||
                    (p[j] >= 'A' && p[j] <= 'Z' && j + 1 < winEnd && p[j + 1] == ':')) {
                    bool hasPud = false;
                    for (size_t k = 4; k + 4 <= remain; ++k) {
                        if (_strnicmp(reinterpret_cast<const char*>(p + j + k - 4), ".pud", 4) == 0) {
                            hasPud = true;
                            break;
                        }
                    }
                    if (hasPud) {
                        const size_t before = list.size();
                        TryAsciiPath(list, reinterpret_cast<const char*>(p + j), remain, 64);
                        for (size_t n = before; n < list.size(); ++n) {
                            list[n].fromLobbyTitle = true;
                            list[n].titleHits += 1;
                            list[n].titleRegion = 32;
                            list[n].sourceRegion = 64;
                            list[n].score += 1200;
                        }
                    }
                }
                if ((j % 2) == 0 && j + 10 < winEnd && p[j + 1] == 0) {
                    TryWidePath(list, reinterpret_cast<const wchar_t*>(p + j), remain / 2, 64);
                    // boost any new wide puds — handled below via basename too
                }
            }

            // Bare map title next to Map label.
            for (size_t j = winStart; j + 6 < winEnd; j += 2) {
                if ((j % 2) != 0 || p[j + 1] != 0) continue;
                const wchar_t* name = reinterpret_cast<const wchar_t*>(p + j);
                size_t n = 0;
                const size_t maxChars = (winEnd - j) / 2;
                while (n < maxChars && n < 96) {
                    const wchar_t c = name[n];
                    if (c == 0) break;
                    if (c < 32 || c == L'"' || c == L'<' || c == L'>' || c == L'|' ||
                        c == L'\\' || c == L'/')
                        break;
                    ++n;
                }
                if (n < 3 || n >= 96) continue;
                const bool nullTerm = (n < maxChars && name[n] == 0);
                if (!nullTerm && n > 40) continue;

                wchar_t buf[100]{};
                wcsncpy_s(buf, name, n);
                wchar_t resolved[MAX_PATH]{};
                // Exact title == pud basename only (no " BNE" invent).
                if (!ResolveUnderMapsRootCopy(buf, resolved, MAX_PATH))
                    continue;
                ConsiderCandidate(list, resolved, 64);
                for (auto& c : list) {
                    if (_wcsicmp(c.path.c_str(), resolved) == 0) {
                        c.fromLobbyTitle = true;
                        c.titleHits += 1;
                        c.titleRegion = 32;
                        c.sourceRegion = 64;
                        c.score += 1100;
                        if (nullTerm) c.score += 100;
                        c.score += static_cast<int>(n);
                        if (wcschr(buf, L' ')) c.score += 200;
                        if (n >= 4 && _wcsicmp(buf + (n - 4), L" BNE") == 0) c.score += 800;
                        break;
                    }
                }
            }
        }
    }
}

void ConsiderNearMapLabelSEH(std::vector<PudCandidate>& list, const uint8_t* p, size_t size)
{
    __try {
        ConsiderNearMapLabel(list, p, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void ResolvePudFromMemoryInto(wchar_t* out, size_t outChars)
{
    out[0] = 0;
    EnsureMapsIndex(false);

    std::vector<PudCandidate> list;
    list.reserve(32);
    uint8_t* image = GameBase();
    const size_t imageSize = GameImageSize(image);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* addr = reinterpret_cast<uint8_t*>(si.lpMinimumApplicationAddress);
    uint8_t* const maxAddr = reinterpret_cast<uint8_t*>(si.lpMaximumApplicationAddress);

    while (addr < maxAddr) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        uint8_t* next = reinterpret_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_READONLY)) &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_EXECUTE | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
            mbi.RegionSize <= 1024 * 1024) {
            auto* p = reinterpret_cast<uint8_t*>(mbi.BaseAddress);
            if (!PathIsUnderModule(p, image, imageSize)) {
                ScanRegionSEH(list, p, mbi.RegionSize);
                // Map UI strings can live in larger RW pages than 64 KiB.
                if (mbi.RegionSize <= 512 * 1024)
                    ConsiderNearMapLabelSEH(list, p, mbi.RegionSize);
            }
        }
        addr = next;
    }

    if (list.empty()) return;


    
    auto hasSpace = [](const wchar_t* path) -> bool {
        std::wstring k = BasenameKeyFromPath(path);
        return k.find(L' ') != std::wstring::npos;
    };
    auto better = [&](const PudCandidate& x, const PudCandidate& y) -> bool {
        const bool xa = x.titleRegion == 32;
        const bool ya = y.titleRegion == 32;
        if (xa != ya) return xa;
        const bool xs = hasSpace(x.path.c_str());
        const bool ys = hasSpace(y.path.c_str());
        if (xs != ys) return xs;
        if (x.score != y.score) return x.score > y.score;
        if (x.titleHits != y.titleHits) return x.titleHits > y.titleHits;
        return x.titleRegion != 0 && (y.titleRegion == 0 || x.titleRegion < y.titleRegion);
    };

    const PudCandidate* best = nullptr;
    unsigned titleCount = 0;
    unsigned mapAdjCount = 0;
    for (const auto& c : list) {
        if (c.fromLobbyTitle) ++titleCount;
        if (c.titleRegion == 32) ++mapAdjCount;
    }
    // 1) Prefer Map-adjacent titles.
    for (const auto& c : list) {
        if (c.titleRegion != 32 || !c.fromLobbyTitle) continue;
        if (!best || better(c, *best)) best = &c;
    }
    // 2) Else multi-word lobby titles (bare-name hits).
    if (!best) {
        for (const auto& c : list) {
            if (!c.fromLobbyTitle || !hasSpace(c.path.c_str())) continue;
            if (!best || better(c, *best)) best = &c;
        }
        if (best) Log("resolve: fallback multi-word title score=%d", best->score);
    }
    // 3) Else multi-word pud path (last resort so click still opens something real).
    if (!best) {
        for (const auto& c : list) {
            if (!hasSpace(c.path.c_str())) continue;
            if (!best || better(c, *best)) best = &c;
        }
        if (best) Log("resolve: fallback multi-word path score=%d", best->score);
    }
    if (!best) {
        Log("resolve: no usable map among=%u titles=%u mapAdj=%u",
            static_cast<unsigned>(list.size()), titleCount, mapAdjCount);
        return;
    }
    Log("resolve: mapAdj=%u titleCount=%u score=%d titleReg=%u",
        mapAdjCount, titleCount, best->score, static_cast<unsigned>(best->titleRegion));
    g_lastResolveTitleReg = best->titleRegion;
    g_lastResolveScore = best->score;
    wcsncpy_s(out, outChars, best->path.c_str(), _TRUNCATE);
    wchar_t rootLog[MAX_PATH]{};
    EnterCriticalSection(&g_cacheLock);
    wcsncpy_s(rootLog, g_mapsRoot, _TRUNCATE);
    LeaveCriticalSection(&g_cacheLock);
    Log("resolve: picked score=%d title=%d titleReg=%u among=%u root=%ls %ls", best->score,
        best->fromLobbyTitle ? 1 : 0, static_cast<unsigned>(best->titleRegion),
        static_cast<unsigned>(list.size()),
        rootLog[0] ? rootLog : L"(none)", best->path.c_str());
}

void SetCachedPud(const wchar_t* path)
{
    EnterCriticalSection(&g_cacheLock);
    if (!path || !path[0]) {
        g_cachedPud[0] = 0;
        g_cachedTitleKey[0] = 0;
    } else {
        wcsncpy_s(g_cachedPud, path, _TRUNCATE);
        // Keep title key in sync with the pud basename unless caller set it.
        if (!g_cachedTitleKey[0]) {
            std::wstring key = BasenameKeyFromPath(path);
            wcsncpy_s(g_cachedTitleKey, key.c_str(), _TRUNCATE);
        }
    }
    LeaveCriticalSection(&g_cacheLock);
}

void SetCachedPudWithTitle(const wchar_t* path, const wchar_t* titleKey)
{
    EnterCriticalSection(&g_cacheLock);
    if (!path || !path[0]) {
        g_cachedPud[0] = 0;
        g_cachedTitleKey[0] = 0;
    } else {
        wcsncpy_s(g_cachedPud, path, _TRUNCATE);
        if (titleKey && titleKey[0])
            wcsncpy_s(g_cachedTitleKey, titleKey, _TRUNCATE);
        else {
            std::wstring key = BasenameKeyFromPath(path);
            wcsncpy_s(g_cachedTitleKey, key.c_str(), _TRUNCATE);
        }
    }
    LeaveCriticalSection(&g_cacheLock);
}

void GetCachedPud(wchar_t* out, size_t outChars)
{
    EnterCriticalSection(&g_cacheLock);
    wcsncpy_s(out, outChars, g_cachedPud, _TRUNCATE);
    LeaveCriticalSection(&g_cacheLock);
}

void GetCachedTitleKey(wchar_t* out, size_t outChars)
{
    EnterCriticalSection(&g_cacheLock);
    wcsncpy_s(out, outChars, g_cachedTitleKey, _TRUNCATE);
    LeaveCriticalSection(&g_cacheLock);
}

void RefreshCachedPud(bool forceForClick = false)
{
    // Match always clears. Outside lobby: keep cache unless this is a forced click resolve.
    if (InActiveMatch()) {
        SetCachedPud(nullptr);
        return;
    }
    if (!forceForClick && !InMpLobbyScreen()) {
        return;
    }
    wchar_t found[MAX_PATH]{};
    ResolvePudFromMemoryInto(found, MAX_PATH);
    if (!found[0] || GetFileAttributesW(found) == INVALID_FILE_ATTRIBUTES) {
        // Keep last short-memory value while still in this lobby (cleared on leave).
        Log("cached pud: resolve miss (keeping short-memory if any)");
        return;
    }

    std::wstring newKey = BasenameKeyFromPath(found);
    wchar_t prevKey[MAX_PATH]{};
    GetCachedTitleKey(prevKey, MAX_PATH);
    wchar_t prev[MAX_PATH]{};
    GetCachedPud(prev, MAX_PATH);

    const bool same = prevKey[0] && !newKey.empty() && _wcsicmp(prevKey, newKey.c_str()) == 0;
    auto keyHasSpace = [](const wchar_t* key) -> bool {
        return key && key[0] && wcschr(key, L' ') != nullptr;
    };
    const bool prevMulti = keyHasSpace(prevKey);
    const bool newMulti = keyHasSpace(newKey.c_str());
    // Sticky: keep a multi-word lobby title over single-word Classic noise.
    if (prev[0] && !same && prevMulti && !newMulti && !forceForClick) {
        Log("cached pud: keep multi-word sticky %ls (reject %ls)", prev, found);
        return;
    }
    if (prev[0] && !same && prevMulti && !newMulti && forceForClick) {
        Log("cached pud: keep multi-word on click %ls (reject single-word %ls)", prev, found);
        return;
    }
    if (prevKey[0] && !newKey.empty() && !same) {
        Log("cached pud: title changed %ls -> %ls", prevKey, newKey.c_str());
    }

    SetCachedPudWithTitle(found, newKey.c_str());
    if (_wcsicmp(prev, found) != 0)
        Log("cached pud: %ls", found);
}

void RequestPathUiOnly()
{
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t reqPath[MAX_PATH]{};
    wchar_t tmpPath[MAX_PATH]{};
    swprintf_s(reqPath, L"%swar2_lobby_map_open.txt", tempDir);
    swprintf_s(tmpPath, L"%swar2_lobby_map_open.tmp", tempDir);

    const wchar_t* pud = L"__UI_RESOLVE__";
    HANDLE h = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log("click: temp write failed %lu", GetLastError());
        return;
    }
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>((wcslen(pud) + 1) * sizeof(wchar_t));
    const BOOL ok = WriteFile(h, pud, bytes, &written, nullptr);
    CloseHandle(h);
    if (!ok || written != bytes) {
        DeleteFileW(tmpPath);
        Log("click: WriteFile failed");
        return;
    }
    DeleteFileW(reqPath);
    if (!MoveFileW(tmpPath, reqPath)) {
        if (!CopyFileW(tmpPath, reqPath, FALSE)) {
            Log("click: publish request failed %lu", GetLastError());
            DeleteFileW(tmpPath);
            return;
        }
        DeleteFileW(tmpPath);
    }
    if (g_openEvent) SetEvent(g_openEvent);
    Log("click: requested UI resolve (Studio maps folder)");
}

void RequestPath()
{
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t reqPath[MAX_PATH]{};
    wchar_t tmpPath[MAX_PATH]{};
    swprintf_s(reqPath, L"%swar2_lobby_map_open.txt", tempDir);
    swprintf_s(tmpPath, L"%swar2_lobby_map_open.tmp", tempDir);

    wchar_t pud[MAX_PATH]{};
    GetCachedPud(pud, MAX_PATH);
    if (!pud[0] || GetFileAttributesW(pud) == INVALID_FILE_ATTRIBUTES) {
        // Ask AllyLeaveWatch to resolve via UI Automation (Map row).
        wcsncpy_s(pud, L"__UI_RESOLVE__", _TRUNCATE);
        Log("click: requesting UI resolve (no title pud yet)");
    }

    HANDLE h = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log("click: temp write failed %lu", GetLastError());
        return;
    }
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>((wcslen(pud) + 1) * sizeof(wchar_t));
    const BOOL ok = WriteFile(h, pud, bytes, &written, nullptr);
    CloseHandle(h);
    if (!ok || written != bytes) {
        DeleteFileW(tmpPath);
        Log("click: WriteFile failed");
        return;
    }
    DeleteFileW(reqPath);
    if (!MoveFileW(tmpPath, reqPath)) {
        // Fallback overwrite.
        if (!CopyFileW(tmpPath, reqPath, FALSE)) {
            Log("click: publish request failed %lu", GetLastError());
            DeleteFileW(tmpPath);
            return;
        }
        DeleteFileW(tmpPath);
    }

    if (g_openEvent) SetEvent(g_openEvent);
    Log("click: requested open %ls", pud);
}

void OnMapNameClick()
{
    const DWORD now = GetTickCount();
    if (g_enabledTick != 0 && (now - g_enabledTick) < kEnableGraceMs) {
        Log("click ignored: enable grace");
        return;
    }

    if (InActiveMatch()) {
        Log("click ignored: in match");
        return;
    }

    HWND hwnd = FindGameWindow();
    if (!hwnd) {
        Log("click ignored: no game window");
        return;
    }

    // Prefer the foreground game window, but do NOT abort if Photos/Explorer
    // stole focus after a previous open — hitbox + lobby still decide.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId())
            hwnd = fg;
        else
            Log("click: game not foreground (continuing via hitbox/lobby)");
    }

    POINT client{};
    int cw = 0, ch = 0;
    const bool inHit = CursorInMapHitbox(hwnd, &client, &cw, &ch);
    Log("click at client=%d,%d size=%dx%d hit=%d lobby=%d",
        client.x, client.y, cw, ch, inHit ? 1 : 0, InMpLobbyScreen() ? 1 : 0);

    if (!inHit) {
        Log("click ignored: outside map hitbox");
        return;
    }
    // Require MP lobby probe. Soft-open without lobby caused menu/startup
    // false opens on ultrawide (hitbox match with lobby=0).
    const bool lobby = InMpLobbyScreen();
    if (!lobby) {
        Log("click ignored: not in MP lobby");
        return;
    }

    // Never reuse a sticky wrong map (e.g. Cramped) from a previous lobby.
    SetCachedPud(nullptr);
    RefreshCachedPud(true);
    wchar_t pud[MAX_PATH]{};
    GetCachedPud(pud, MAX_PATH);
    if (!pud[0] || GetFileAttributesW(pud) == INVALID_FILE_ATTRIBUTES) {
        Log("click ignored: no resolved pud");
        return;
    }
    RequestPath();
}

DWORD WINAPI WatchThread(LPVOID)
{
    Log("WatchThread start (jpg open via maps root)");
    // Assume button may already be down when we start — do not treat that as a click.
    bool wasDown = true;
    DWORD lastOpen = 0;
    bool indexed = false;
    bool wasEnabled = false;
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        Sleep(40);
        const bool enabled = InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
        if (!enabled) {
            if (wasEnabled) {
                UninstallMpLobbyLayoutProbe();
                SetCachedPud(nullptr);
            }
            wasDown = true; // next enable must see a fresh press
            indexed = false;
            wasEnabled = false;
            continue;
        }

        if (!wasEnabled) {
            wasEnabled = true;
            wasDown = true;
            g_enabledTick = GetTickCount();
            InstallMpLobbyLayoutProbe(GameBase());
            Log("enabled: grace %ums, ignore held mouse", kEnableGraceMs);
        }

        if (!indexed) {
            EnsureMapsIndex(true);
            indexed = true;
        }

        // Only a live match drops short-memory. Idle lobby must keep the last map.
        if (InActiveMatch()) {
            SetCachedPud(nullptr);
        } else if (InMpLobbyScreen()) {
            static DWORD s_lastMemRefresh = 0;
            const DWORD nowRefresh = GetTickCount();
            if (s_lastMemRefresh == 0 || (nowRefresh - s_lastMemRefresh) >= 450) {
                s_lastMemRefresh = nowRefresh;
                RefreshCachedPud(false);
            }
        }

        const DWORD now = GetTickCount();
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (down && !wasDown) {
            if (now - lastOpen >= kDebounceMs) {
                OnMapNameClick();
                lastOpen = now;
            }
        }
        wasDown = down;
    }
    UninstallMpLobbyLayoutProbe();
    Log("WatchThread exit");
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) void __stdcall SetEnabled(int enable)
{
    InterlockedExchange(&g_enabled, enable ? 1 : 0);
    if (enable) {
        g_enabledTick = GetTickCount();
    } else {
        UninstallMpLobbyLayoutProbe();
        SetCachedPud(nullptr);
    }
    Log("SetEnabled %d", enable);
    // Index / lobby probe install on the watch thread — never from the injector thread.
}

extern "C" __declspec(dllexport) unsigned __stdcall IsReady()
{
    return 1u;
}

extern "C" __declspec(dllexport) unsigned __stdcall IsEnabled()
{
    return g_enabled ? 1u : 0u;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_cacheLock);
        InterlockedExchange(&g_enabled, 0);
        InterlockedExchange(&g_stop, 0);
        g_cachedPud[0] = 0;
        g_openEvent = CreateEventW(nullptr, FALSE, FALSE, kOpenEventName);
        g_thread = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
        if (g_thread) {
            WaitForSingleObject(g_thread, 2000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_openEvent) {
            CloseHandle(g_openEvent);
            g_openEvent = nullptr;
        }
        DeleteCriticalSection(&g_cacheLock);
    }
    return TRUE;
}
