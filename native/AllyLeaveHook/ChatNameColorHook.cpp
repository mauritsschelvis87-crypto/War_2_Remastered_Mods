// Warcraft II Remastered — color multiplayer chat *names* only.
//
// In-game chat is drawn via NKMapMessages → DrawTextColored (0x5AEEB0).
// That API takes one ARGB for the whole string, so we:
//   1) draw the full line in the default (body) color
//   2) redraw only "Name:" on top in the Studio player color
//
// Names matched against in-game table 0x91ADA8 / stride 0x38.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr size_t kPlayerStride = 0x38;
constexpr uint32_t kPreferredImageBase = 0x00400000;
constexpr uint32_t kPreferredName0 = 0x0091ADA8;
constexpr uint32_t kPreferredDrawColored = 0x005AEEB0;
constexpr uint32_t kPreferredDrawImpl = 0x005AED00; // called only by 0x5AEEB0 (not 0x5AEE20)
constexpr uint32_t kPreferredPushMapMsg = 0x00614A90;
constexpr uint32_t kPreferredMsgRing = 0x009B17A0;  // 15 × 0xD0 map-message slots
constexpr uint32_t kPreferredTimeNow = 0x00625940;  // ms since app start (QPC-based)
constexpr uint32_t kBodyColor = 0xFFE3E3E3; // light gray / default chat body
constexpr uint32_t kSystemMessageColor = 0xFF4BF0FF; // widget_text_yellow RGB(255,240,75)

using DrawColoredFn = void(__cdecl*)(void* ui, const char* text, uint32_t color);
using PushMapMsgFn = void(__cdecl*)(const char* text, uint32_t color, uint32_t duration);
using TimeNowFn = uint32_t(__cdecl*)();

volatile LONG g_enabled = 0;       // chat "Name:" lines
volatile LONG g_timestamps = 0;    // "[HH:MM] " prefix on chat lines (own mod)
volatile LONG g_historyOn = 0;     // PageUp/PageDown chat history recall (own mod)
volatile LONG g_ready = 0;
volatile LONG g_hits = 0;
volatile LONG g_recolors = 0;

uint8_t* g_drawSite = nullptr;
uint8_t g_originalPrologue[8]{};
void* g_trampoline = nullptr;
DrawColoredFn g_originalDraw = nullptr;
char* g_playerName0 = nullptr;
uint8_t* g_localPlayer = nullptr; // VA 0x918CCD (+ slide)
uint8_t* g_seatColors = nullptr;  // VA 0x919390 (+ slide): seat -> chosen color slot

uint8_t* g_ownerSite = nullptr;
uint8_t g_ownerOrig[16]{};
size_t g_ownerOrigLen = 0;
void* g_ownerCave = nullptr;

constexpr size_t kOwnerRing = 24;
constexpr size_t kOwnerTextMax = 180;
struct ChatOwnerEntry {
    char text[kOwnerTextMax]{};
    int player = -1;
    DWORD tick = 0;
    LONG epoch = 0;
};
ChatOwnerEntry g_owners[kOwnerRing]{};
volatile LONG g_ownerWrite = 0;
volatile LONG g_matchEpoch = 0;
// Last-known display name per seat for this match. The live 0x91ADA8 table
// is cleared as soon as someone drops — this snapshot survives until reset.
char g_seatNames[8][80]{};

// Per-message arrival time for the timestamp mod. Lines redraw every frame
// while visible; the stamp must show when the message APPEARED, so remember
// the wall time on first sight and reuse it while the line stays on screen.
constexpr size_t kStampRing = 24;
constexpr size_t kStampTextMax = 180;
constexpr DWORD kStampNewAfterMs = 8000; // same text later = new message
struct StampEntry {
    char text[kStampTextMax]{};
    char stamp[16]{}; // "[21:08] "
    DWORD lastSeen = 0;
};
StampEntry g_stamps[kStampRing]{};

// Chat history recall: the game's map-message ring at 0x9B17A0 keeps only
// 15 slots and WIPES a slot when its line expires (text[0]=0, expiry=0,
// color=0), so we keep our own copy of every pushed line. PageUp/PageDown
// rewrite the ring slots with a window of that history — plain memory
// writes into a static buffer, no game calls except the pure time getter.
constexpr size_t kRingSlots = 15;
constexpr size_t kRingStride = 0xD0;
constexpr size_t kRingTextMax = 0xC8;   // entry text capacity
constexpr uint32_t kEntryExpiry = 0xC8; // DWORD: TimeNow()+duration at push
constexpr uint32_t kEntryColor = 0xCC;  // BYTE: color slot (chat uses 0)
constexpr size_t kHistRing = 64;
constexpr LONG kPageStep = 10;
constexpr uint32_t kViewHoldMs = 3000;   // refreshed while browsing
constexpr uint32_t kExitHoldMs = 4000;   // fade-out after leaving the view
constexpr DWORD kViewIdleExitMs = 20000; // auto-return to live chat

struct HistEntry {
    char text[kRingTextMax]{};
    char stamp[16]{}; // wall time at push, so replays show the ORIGINAL time
    uint8_t color = 0;
};
HistEntry g_hist[kHistRing]{};
volatile LONG g_histCount = 0;  // total lines ever pushed (monotonic)
volatile LONG g_viewing = 0;    // 1 while PageUp view is active
volatile LONG g_viewBack = 0;   // lines back from newest (0 = last 15)

uint8_t* g_msgRing = nullptr;
uint8_t* g_msgInitFlag = nullptr; // VA 0x9B1798: 1 during a match, 0 in menus
TimeNowFn g_timeNow = nullptr;
uint8_t* g_pushSite = nullptr;
uint8_t g_pushPrologue[8]{};
void* g_pushTrampoline = nullptr;
PushMapMsgFn g_originalPush = nullptr;
HANDLE g_keyThread = nullptr;
HANDLE g_matchThread = nullptr;
volatile LONG g_keyStop = 0;

uint32_t g_colors[8]{};
char g_logPath[MAX_PATH]{};

using MarkGoneByNameFn = void(__stdcall*)(const char* name);
using MarkGoneFn = void(__stdcall*)(int playerIndex);

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_chat_name_color_hook.log", temp);
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

bool IsLikelyCode(const uint8_t* p, size_t n)
{
    __try {
        volatile uint8_t sum = 0;
        for (size_t i = 0; i < n; ++i) sum = static_cast<uint8_t>(sum + p[i]);
        (void)sum;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MatchBytes(const uint8_t* p, const uint8_t* pat, const char* mask)
{
    for (size_t i = 0; mask[i]; ++i) {
        if (mask[i] == 'x' && p[i] != pat[i]) return false;
    }
    return true;
}

uint8_t* FindPattern(uint8_t* base, size_t imageSize, const uint8_t* pat, const char* mask)
{
    const size_t len = strlen(mask);
    if (imageSize < len) return nullptr;
    for (size_t i = 0; i + len <= imageSize; ++i) {
        uint8_t* p = base + i;
        if (!IsLikelyCode(p, len)) continue;
        if (MatchBytes(p, pat, mask)) return p;
    }
    return nullptr;
}

uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
}

uint32_t ParseHexColor(const char* hex)
{
    if (!hex) return PackColor(0xDC, 0xDC, 0xDC);
    while (*hex == '#' || *hex == ' ' || *hex == '"') ++hex;
    unsigned r = 0xDC, g = 0xDC, b = 0xDC;
    if (sscanf_s(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        return PackColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
    }
    return PackColor(0xDC, 0xDC, 0xDC);
}

void SetDefaultColors()
{
    const char* defs[8] = {
        "#A60000", "#0096FF", "#2DB696", "#9A49B2",
        "#FB8E14", "#28283D", "#E3E3E3", "#FFF759"
    };
    for (int i = 0; i < 8; ++i) g_colors[i] = ParseHexColor(defs[i]);
}

bool ReadFileAll(const wchar_t* path, char* buf, size_t bufSize, size_t* outLen)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buf, static_cast<DWORD>(bufSize - 1), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    buf[read] = 0;
    if (outLen) *outLen = read;
    return true;
}

void LoadColorsFromJson()
{
    SetDefaultColors();

    wchar_t dllPath[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LoadColorsFromJson), &self) || !self) {
        Log("LoadColors: no self module");
        return;
    }
    if (!GetModuleFileNameW(self, dllPath, MAX_PATH)) return;

    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (!slash) slash = wcsrchr(dllPath, L'/');
    if (slash) slash[1] = 0;

    wchar_t jsonPath[MAX_PATH]{};
    swprintf_s(jsonPath, L"%s..\\player-colors.json", dllPath);

    wchar_t full[MAX_PATH]{};
    if (GetFullPathNameW(jsonPath, MAX_PATH, full, nullptr) == 0) {
        wcsncpy_s(full, jsonPath, _TRUNCATE);
    }

    char buf[8192]{};
    size_t len = 0;
    if (!ReadFileAll(full, buf, sizeof(buf), &len)) {
        Log("LoadColors: missing %ls — using defaults", full);
        return;
    }

    for (int player = 1; player <= 8; ++player) {
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
        g_colors[player - 1] = ParseHexColor(hash);
    }
    Log("LoadColors: ok from %ls", full);
}

const char* PlayerName(int index)
{
    if (!g_playerName0 || index < 0 || index > 7) return nullptr;
    __try {
        return g_playerName0 + index * static_cast<int>(kPlayerStride);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool NameEquals(const char* a, size_t aLen, const char* b)
{
    if (!a || !b || aLen == 0) return false;
    size_t i = 0;
    for (; i < aLen && b[i]; ++i) {
        const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) return false;
    }
    return i == aLen && b[i] == 0;
}

bool StartsWithIgnoreCase(const char* text, const char* prefix)
{
    if (!text || !prefix) return false;
    for (; *prefix; ++text, ++prefix) {
        const char ca = (*text >= 'A' && *text <= 'Z') ? static_cast<char>(*text - 'A' + 'a') : *text;
        const char cb = (*prefix >= 'A' && *prefix <= 'Z') ? static_cast<char>(*prefix - 'A' + 'a') : *prefix;
        if (!*text || ca != cb) return false;
    }
    return true;
}

bool EndsWithIgnoreCase(const char* text, size_t textLen, const char* suffix)
{
    const size_t sufLen = strlen(suffix);
    if (!text || textLen < sufLen) return false;
    const char* p = text + (textLen - sufLen);
    for (size_t i = 0; i < sufLen; ++i) {
        const char ca = (p[i] >= 'A' && p[i] <= 'Z') ? static_cast<char>(p[i] - 'A' + 'a') : p[i];
        const char cb = (suffix[i] >= 'A' && suffix[i] <= 'Z') ? static_cast<char>(suffix[i] - 'A' + 'a') : suffix[i];
        if (ca != cb) return false;
    }
    return true;
}

int ReadLocalPlayerIndex()
{
    if (!g_localPlayer) return -1;
    __try {
        const int v = static_cast<int>(*g_localPlayer);
        return (v >= 0 && v <= 7) ? v : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Seat -> lobby-chosen color slot. The name table, alliances rows, and owner
// stamps are all SEAT-indexed, but in multiplayer a player's color is picked
// in the lobby and can differ from the seat. The byte table at 0x919390 maps
// SEAT -> COLOR SLOT — a DIRECT lookup, proven by the game's own helper at
// 0x50E180: `movzx eax,[seat + 0x919390]; shl eax,4; add eax,0x8C9640`
// (color table is indexed by the RESULT). An earlier inverted reading fit a
// 12-08 dump only because those two seats had swapped colors symmetrically;
// a 13-08 match (table [0,4,7,3,2,6,1,5], seat-1 player was not white)
// refuted the inversion.
int SeatToColorSlot(int seat)
{
    if (seat < 0 || seat > 7 || !g_seatColors) return seat;
    __try {
        // The table lives in BSS: all-zero until a match is set up. An
        // all-zero read means "no mapping yet" — fall back to identity.
        bool anySet = false;
        for (int i = 0; i < 8; ++i) {
            if (g_seatColors[i] != 0) { anySet = true; break; }
        }
        if (!anySet) return seat;
        const int c = static_cast<int>(g_seatColors[seat]);
        return (c >= 0 && c <= 7) ? c : seat;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return seat;
    }
}

void RememberChatOwner(const char* text, int player)
{
    if (!text || !text[0] || player < 0 || player > 7) return;
    const LONG idx = InterlockedIncrement(&g_ownerWrite) - 1;
    ChatOwnerEntry& e = g_owners[static_cast<size_t>(idx) % kOwnerRing];
    strncpy_s(e.text, text, _TRUNCATE);
    e.player = player;
    e.tick = GetTickCount();
    e.epoch = InterlockedCompareExchange(&g_matchEpoch, 0, 0);
}

void RefreshSeatNameSnapshot()
{
    if (!g_playerName0) return;
    for (int i = 0; i < 8; ++i) {
        const char* slot = PlayerName(i);
        if (!slot) continue;
        __try {
            if (slot[0]) {
                strncpy_s(g_seatNames[i], slot, _TRUNCATE);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void RememberSeatName(int seat, const char* name, size_t len)
{
    if (seat < 0 || seat > 7 || !name || len == 0 || len >= 80) return;
    char buf[80]{};
    memcpy(buf, name, len);
    buf[len] = 0;
    strncpy_s(g_seatNames[seat], buf, _TRUNCATE);
}

char g_lastLeaveMarkName[80]{};
DWORD g_lastLeaveMarkTick = 0;
LONG g_lastLeaveMarkEpoch = -1;

void ResetLeaveOwnerCache()
{
    InterlockedIncrement(&g_matchEpoch);
    for (size_t i = 0; i < kOwnerRing; ++i) {
        g_owners[i].text[0] = 0;
        g_owners[i].player = -1;
        g_owners[i].tick = 0;
        g_owners[i].epoch = 0;
    }
    for (int i = 0; i < 8; ++i) g_seatNames[i][0] = 0;
    g_lastLeaveMarkName[0] = 0;
    g_lastLeaveMarkTick = 0;
    g_lastLeaveMarkEpoch = -1;
}

void AllyLeaveOnNewMatch();

// cdecl helper invoked from the compose cave: (text, player)
extern "C" void __cdecl ChatNameColor_RememberOwner(const char* text, uint32_t player)
{
    RememberChatOwner(text, static_cast<int>(player));
    RefreshSeatNameSnapshot();
    if (text && text[0])
        Log("owner-remember p=%u text=%.60s", player, text);
}

// Only called from the game's render thread (the draw hook) — no locking.
const char* StampFor(const char* text)
{
    const DWORD now = GetTickCount();
    int freeSlot = -1;
    int oldest = 0;
    DWORD oldestSeen = 0xFFFFFFFF;
    for (size_t i = 0; i < kStampRing; ++i) {
        StampEntry& e = g_stamps[i];
        if (!e.text[0]) {
            if (freeSlot < 0) freeSlot = static_cast<int>(i);
            continue;
        }
        if (strcmp(e.text, text) == 0) {
            if ((now - e.lastSeen) > kStampNewAfterMs) {
                SYSTEMTIME st{};
                GetLocalTime(&st);
                sprintf_s(e.stamp, "[%02u:%02u] ", st.wHour, st.wMinute);
            }
            e.lastSeen = now;
            return e.stamp;
        }
        if (e.lastSeen < oldestSeen) {
            oldestSeen = e.lastSeen;
            oldest = static_cast<int>(i);
        }
    }
    StampEntry& e = g_stamps[freeSlot >= 0 ? freeSlot : oldest];
    strncpy_s(e.text, text, _TRUNCATE);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    sprintf_s(e.stamp, "[%02u:%02u] ", st.wHour, st.wMinute);
    e.lastSeen = now;
    return e.stamp;
}

// Pre-fill the stamp ring so a replayed line keeps its original arrival
// time — StampFor would otherwise assign "now" to a re-shown old line.
void SeedStamp(const char* text, const char* stamp)
{
    if (!text || !text[0] || !stamp || !stamp[0]) return;
    const DWORD now = GetTickCount();
    int freeSlot = -1;
    int oldest = 0;
    DWORD oldestSeen = 0xFFFFFFFF;
    for (size_t i = 0; i < kStampRing; ++i) {
        StampEntry& e = g_stamps[i];
        if (!e.text[0]) {
            if (freeSlot < 0) freeSlot = static_cast<int>(i);
            continue;
        }
        if (strcmp(e.text, text) == 0) {
            strcpy_s(e.stamp, stamp);
            e.lastSeen = now;
            return;
        }
        if (e.lastSeen < oldestSeen) {
            oldestSeen = e.lastSeen;
            oldest = static_cast<int>(i);
        }
    }
    StampEntry& e = g_stamps[freeSlot >= 0 ? freeSlot : oldest];
    strncpy_s(e.text, text, _TRUNCATE);
    strcpy_s(e.stamp, stamp);
    e.lastSeen = now;
}

uint32_t SafeTimeNow()
{
    if (!g_timeNow) return 0;
    __try {
        return g_timeNow();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void RecordHistory(const char* text, uint8_t color)
{
    __try {
        if (!text || !text[0]) return;
        const LONG idx = InterlockedIncrement(&g_histCount) - 1;
        HistEntry& e = g_hist[static_cast<size_t>(idx) % kHistRing];
        strncpy_s(e.text, text, _TRUNCATE);
        e.color = color;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        sprintf_s(e.stamp, "[%02u:%02u] ", st.wHour, st.wMinute);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Rewrite the game's 15 ring slots with a window of our history ending
// g_viewBack lines before the newest. Expiry is zeroed first and written
// last so the game never draws a half-written line.
void RenderHistoryView(uint32_t holdMs)
{
    if (!g_msgRing) return;
    const LONG total = InterlockedCompareExchange(&g_histCount, 0, 0);
    if (total <= 0) return;
    const uint32_t now = SafeTimeNow();
    if (!now) return;
    LONG newestIdx = total - 1 - InterlockedCompareExchange(&g_viewBack, 0, 0);
    if (newestIdx < 0) newestIdx = 0;
    __try {
        for (int slot = static_cast<int>(kRingSlots) - 1; slot >= 0; --slot) {
            uint8_t* e = g_msgRing + static_cast<size_t>(slot) * kRingStride;
            const LONG idx = newestIdx - (static_cast<LONG>(kRingSlots) - 1 - slot);
            *reinterpret_cast<uint32_t*>(e + kEntryExpiry) = 0;
            if (idx < 0 || idx < total - static_cast<LONG>(kHistRing)) {
                e[0] = 0;
                e[kEntryColor] = 0;
                continue;
            }
            const HistEntry& h = g_hist[static_cast<size_t>(idx) % kHistRing];
            strncpy_s(reinterpret_cast<char*>(e), kRingTextMax, h.text, _TRUNCATE);
            e[kEntryColor] = h.color;
            SeedStamp(h.text, h.stamp);
            *reinterpret_cast<uint32_t*>(e + kEntryExpiry) = now + holdMs;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool GameWindowFocused()
{
    const HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// The game's map-message "initialized" byte: set to 1 when a match sets up
// its message ring (0x614A4F), cleared to 0 on teardown (0x614A84).
int ReadMsgInitFlag()
{
    if (!g_msgInitFlag) return -1;
    __try {
        return *g_msgInitFlag ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void ExitHistoryView()
{
    InterlockedExchange(&g_viewBack, 0);
    if (InterlockedExchange(&g_viewing, 0) != 0) RenderHistoryView(kExitHoldMs);
}

DWORD WINAPI MatchWatchThread(LPVOID)
{
    int lastInitFlag = -1;
    while (!InterlockedCompareExchange(&g_keyStop, 0, 0)) {
        Sleep(100);
        const int initFlag = ReadMsgInitFlag();
        if (initFlag < 0) continue;
        if (lastInitFlag >= 0 && initFlag != lastInitFlag) {
            if (initFlag == 1) {
                // New match — drop stale leave/name caches before lines arrive.
                ResetLeaveOwnerCache();
                AllyLeaveOnNewMatch();
                InterlockedExchange(&g_histCount, 0);
                RefreshSeatNameSnapshot();
            } else {
                // Match ending — exit history view only. AllyLeaveHook's own
                // msg-init poll calls ChatNameColor_OnNewMatch; clearing seat
                // snapshots here races with leave lines still on screen.
                InterlockedExchange(&g_viewing, 0);
                InterlockedExchange(&g_viewBack, 0);
            }
            Log("matchWatch: reset init %d->%d", lastInitFlag, initFlag);
        } else if (initFlag == 1) {
            RefreshSeatNameSnapshot();
        }
        lastInitFlag = initFlag;
    }
    return 0;
}

DWORD WINAPI HistoryKeyThread(LPVOID)
{
    bool upHeld = false;
    bool dnHeld = false;
    DWORD lastAction = 0;
    DWORD lastRefresh = 0;
    while (!InterlockedCompareExchange(&g_keyStop, 0, 0)) {
        Sleep(60);

        if (!InterlockedCompareExchange(&g_historyOn, 0, 0)) {
            ExitHistoryView();
            upHeld = dnHeld = false;
            continue;
        }
        const bool focused = GameWindowFocused();
        const bool up = focused && (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
        const bool dn = focused && (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
        const DWORD now = GetTickCount();
        const bool viewing = InterlockedCompareExchange(&g_viewing, 0, 0) != 0;

        if (up && (!upHeld || (now - lastAction) > 300)) {
            const LONG total = InterlockedCompareExchange(&g_histCount, 0, 0);
            if (total > 0) {
                if (!viewing) {
                    // First press: re-show the newest 15 lines as-is.
                    InterlockedExchange(&g_viewBack, 0);
                    InterlockedExchange(&g_viewing, 1);
                } else {
                    LONG maxBack = total - 1;
                    if (maxBack > static_cast<LONG>(kHistRing) - 1)
                        maxBack = static_cast<LONG>(kHistRing) - 1;
                    LONG next = InterlockedCompareExchange(&g_viewBack, 0, 0) + kPageStep;
                    if (next > maxBack) next = maxBack;
                    if (next < 0) next = 0;
                    InterlockedExchange(&g_viewBack, next);
                }
                RenderHistoryView(kViewHoldMs);
                lastAction = now;
                lastRefresh = now;
            }
        } else if (dn && (!dnHeld || (now - lastAction) > 300) && viewing) {
            const LONG back = InterlockedCompareExchange(&g_viewBack, 0, 0);
            if (back <= 0) {
                ExitHistoryView();
            } else {
                LONG next = back - kPageStep;
                if (next < 0) next = 0;
                InterlockedExchange(&g_viewBack, next);
                RenderHistoryView(kViewHoldMs);
            }
            lastAction = now;
            lastRefresh = now;
        } else if (viewing) {
            if ((now - lastAction) > kViewIdleExitMs) {
                ExitHistoryView();
            } else if ((now - lastRefresh) > 1000) {
                RenderHistoryView(kViewHoldMs); // keep the page on screen
                lastRefresh = now;
            }
        }
        upHeld = up;
        dnHeld = dn;
    }
    return 0;
}

int LookupChatOwner(const char* text)
{
    if (!text || !text[0]) return -1;
    const LONG epoch = InterlockedCompareExchange(&g_matchEpoch, 0, 0);
    int best = -1;
    DWORD bestTick = 0;
    for (size_t i = 0; i < kOwnerRing; ++i) {
        const ChatOwnerEntry& e = g_owners[i];
        if (e.player < 0 || e.player > 7 || !e.text[0]) continue;
        if (e.epoch != epoch) continue;
        if (strcmp(e.text, text) != 0) continue;
        if (e.tick >= bestTick) {
            bestTick = e.tick;
            best = e.player;
        }
    }
    return best;
}

FARPROC ResolveAllyExport(const char* exportName);

// Ask AllyLeaveHook for the alliances-row of this name. Rows are SEAT
// indexed, same as the 0x91ADA8 name table — a secondary name source for
// when one of the two is not (yet) populated. The seat is translated to
// the player's chosen color afterwards via SeatToColorSlot.
int AllyRowByName(const char* name, size_t len)
{
    if (!name || len == 0 || len >= 80) return -1;
    using FindRowFn = int(__stdcall*)(const char*);
    static FindRowFn s_fn = nullptr;
    static DWORD s_nextTry = 0;
    if (!s_fn) {
        const DWORD now = GetTickCount();
        if (now < s_nextTry) return -1;
        s_nextTry = now + 3000; // ally DLL may not be loaded; don't retry every frame
        s_fn = reinterpret_cast<FindRowFn>(ResolveAllyExport("AllyLeave_FindRowByName"));
        if (!s_fn) return -1;
    }
    char buf[80]{};
    memcpy(buf, name, len);
    buf[len] = 0;
    __try {
        return s_fn(buf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int MatchPlayerIndex(const char* text, size_t* channelLenOut, size_t* nameEndOut)
{
    if (channelLenOut) *channelLenOut = 0;
    if (nameEndOut) *nameEndOut = 0;
    if (!text || !text[0]) return -1;

    const char* start = text;
    while (*start && (static_cast<unsigned char>(*start) < 0x20 || *start == ' ')) ++start;

    // Human MP: "(To All) Name: msg" / "(To Allies) Name: msg"
    // Local/comp often: "Name: msg"
    const char* nameStart = start;
    static const char* kPrefixes[] = {
        "(To All) ",
        "(To Allies) ",
        "To Enemies: ",
        "To No One: ",
        nullptr
    };
    for (int p = 0; kPrefixes[p]; ++p) {
        if (StartsWithIgnoreCase(nameStart, kPrefixes[p])) {
            nameStart += strlen(kPrefixes[p]);
            break;
        }
    }
    if (nameStart == start && StartsWithIgnoreCase(nameStart, "To ")) {
        const char* firstColon = strchr(nameStart, ':');
        if (firstColon && firstColon[1] == ' ') {
            const char* after = firstColon + 2;
            if (strchr(after, ':')) nameStart = after;
        }
    }
    while (*nameStart == ' ' || *nameStart == '\t') ++nameStart;

    const char* colon = strchr(nameStart, ':');
    if (!colon || colon == nameStart) return -1;
    size_t nameLen = static_cast<size_t>(colon - nameStart);
    while (nameLen > 0 && (nameStart[nameLen - 1] == ' ' || nameStart[nameLen - 1] == '\t')) --nameLen;
    if (nameLen == 0 || nameLen > 64) return -1;

    if (channelLenOut) *channelLenOut = static_cast<size_t>(nameStart - text);
    if (nameEndOut) *nameEndOut = static_cast<size_t>(colon - text) + 1;

    // Compose-time owner first — required when two seats share a display name.
    const int fromMsg = LookupChatOwner(text);
    if (fromMsg >= 0) return fromMsg;

    // Alliances row when the name is unique on F11 (wrong if duplicated).
    const int fromRows = AllyRowByName(nameStart, nameLen);
    if (fromRows >= 0) return fromRows;

    int matches[8]{};
    int matchCount = 0;
    size_t foundLen = 0;
    int longest = -1;
    for (int i = 0; i < 8; ++i) {
        const char* slot = PlayerName(i);
        if (!slot) continue;
        __try {
            if (!slot[0] || !NameEquals(nameStart, nameLen, slot)) continue;
            matches[matchCount++] = i;
            size_t slotLen = 0;
            while (slot[slotLen] && slotLen < 64) ++slotLen;
            if (slotLen > foundLen) {
                foundLen = slotLen;
                longest = i;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (matchCount == 1) {
        RememberSeatName(matches[0], nameStart, nameLen);
        return matches[0];
    }
    if (matchCount == 0) return -1;

    // Duplicate in-game names (e.g. two "Avent"): do NOT prefer local — that
    // painted every line in your color. Without a map-msg stamp we cannot tell.
    (void)longest;
    return -1;
}

FARPROC ResolveAllyExport(const char* exportName)
{
    HMODULE ally = GetModuleHandleW(L"AllyLeaveHook.dll");
    if (!ally) {
        Log("ally-notify: AllyLeaveHook.dll not loaded");
        return nullptr;
    }
    FARPROC fn = GetProcAddress(ally, exportName);
    if (fn) return fn;
    // x86 stdcall decorations vary by arg count: void=@0, one arg=@4, etc.
    static const char kSuffixes[] = { '0', '4', '8', '1', '2' };
    char decorated[128]{};
    for (char suffix : kSuffixes) {
        sprintf_s(decorated, "_%s@%c", exportName, suffix);
        fn = GetProcAddress(ally, decorated);
        if (fn) return fn;
    }
    Log("ally-notify: export %s not found", exportName);
    return nullptr;
}

void NotifyAllyLeaveByName(const char* name)
{
    if (!name || !name[0]) return;
    auto fn = reinterpret_cast<MarkGoneByNameFn>(
        ResolveAllyExport("AllyLeave_MarkGoneByName"));
    if (fn) fn(name);
}

void NotifyAllyLeaveFromChat(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    auto fn = reinterpret_cast<MarkGoneFn>(
        ResolveAllyExport("AllyLeave_MarkGoneFromChat"));
    if (fn) fn(playerIndex);
}

void NotifyAllyLeaveByIndex(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    auto fn = reinterpret_cast<MarkGoneFn>(
        ResolveAllyExport("AllyLeave_MarkGone"));
    if (fn) fn(playerIndex);
}

// Optional "[21:08] " prefix (our timestamp mod or an embedded game stamp).
const char* SkipOptionalTimestamp(const char* text)
{
    if (!text || text[0] != '[') return text;
    const char* p = text + 1;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { ++p; ++digits; }
    if (digits == 0 || *p != ':') return text;
    ++p;
    digits = 0;
    while (*p >= '0' && *p <= '9') { ++p; ++digits; }
    if (digits < 1 || *p != ']') return text;
    ++p;
    if (*p == ' ') ++p;
    return p;
}

// Locate the player name inside a system leave/drop/elim line. Returns true
// with the name span (offset into text + length) when matched.
bool ParseLeaveLine(const char* text, size_t* nameOffOut, size_t* nameLenOut)
{
    if (!text || !text[0]) return false;
    const char* start = text;
    while (*start && (static_cast<unsigned char>(*start) < 0x20 || *start == ' ')) ++start;
    start = SkipOptionalTimestamp(start);

    static const char* kSuffixes[] = {
        " left the game",
        " was dropped",
        " was eliminated",
        nullptr
    };

    const char* hit = nullptr;
    for (int s = 0; kSuffixes[s]; ++s) {
        for (const char* p = start; *p; ++p) {
            if (!StartsWithIgnoreCase(p, kSuffixes[s])) continue;
            hit = p;
            break;
        }
        if (hit) break;
    }

    // Remastered system lines are short and colon-free: "Avent Left",
    // "Avent Dropped", "Avent Was Eliminated". Typed chat is always
    // "Name: msg", so requiring no colon avoids matching player text. The
    // extracted name must still equal a known player slot before any mark.
    if (!hit && !strchr(start, ':')) {
        size_t len = strlen(start);
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) --len;
        static const char* kEndSuffixes[] = {
            " left",
            " was dropped",
            " dropped",
            " was eliminated",
            " eliminated",
            nullptr
        };
        for (int s = 0; kEndSuffixes[s]; ++s) {
            if (EndsWithIgnoreCase(start, len, kEndSuffixes[s])) {
                hit = start + (len - strlen(kEndSuffixes[s]));
                break;
            }
        }
    }
    if (!hit) return false;

    const char* nameStart = start;
    if (StartsWithIgnoreCase(nameStart, "Player "))
        nameStart += 7;
    while (*nameStart == ' ' || *nameStart == '\t') ++nameStart;
    if (nameStart >= hit || nameStart[0] == 0) return false;

    size_t n = static_cast<size_t>(hit - nameStart);
    while (n > 0 && (nameStart[n - 1] == ' ' || nameStart[n - 1] == '\t')) --n;
    if (n == 0 || n >= 80) return false;

    if (nameOffOut) *nameOffOut = static_cast<size_t>(nameStart - text);
    if (nameLenOut) *nameLenOut = n;
    return true;
}

// Seat for a bare player name: alliances rows first (stable in shuffled
// lobbies), then a unique match in the join-ordered 0x91ADA8 name table.
int SeatForName(const char* name, size_t len)
{
    const int fromRows = AllyRowByName(name, len);
    if (fromRows >= 0) return fromRows;

    int match = -1;
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        const char* slot = PlayerName(i);
        if (!slot) continue;
        __try {
            if (slot[0] && NameEquals(name, len, slot)) {
                match = i;
                ++count;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return (count == 1) ? match : -1;
}

static int AllyLeaveSeatForLeavingName(const char* name)
{
    if (!name || !name[0]) return -1;
    using SeatFn = int(__stdcall*)(const char*);
    static SeatFn s_fn = nullptr;
    static DWORD s_nextTry = 0;
    if (!s_fn) {
        const DWORD now = GetTickCount();
        if (now < s_nextTry) return -1;
        s_nextTry = now + 1000;
        s_fn = reinterpret_cast<SeatFn>(
            ResolveAllyExport("AllyLeave_SeatForLeavingName"));
    }
    if (!s_fn) return -1;
    __try {
        return s_fn(name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void AllyLeaveOnNewMatch()
{
    using NewMatchFn = void(__stdcall*)();
    static NewMatchFn s_fn = nullptr;
    static DWORD s_nextTry = 0;
    if (!s_fn) {
        const DWORD now = GetTickCount();
        if (now < s_nextTry) return;
        s_nextTry = now + 1000;
        s_fn = reinterpret_cast<NewMatchFn>(ResolveAllyExport("AllyLeave_OnNewMatch"));
    }
    if (s_fn) {
        __try { s_fn(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Resolve seat for a leave line. Prefer current-match leave state and our
// seat-name snapshot — the live name table is often empty once they drop.
int SeatForLeaveLine(const char* text, size_t nameOff, size_t nameLen)
{
    if (!text || nameLen == 0 || nameLen >= 80) return -1;

    char name[80]{};
    memcpy(name, text + nameOff, nameLen);
    name[nameLen] = 0;

    int fromLeave = AllyLeaveSeatForLeavingName(name);
    if (fromLeave >= 0) return fromLeave;

    int fromName = SeatForName(name, nameLen);
    if (fromName >= 0) return fromName;

    int match = -1;
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        if (!g_seatNames[i][0]) continue;
        if (NameEquals(name, nameLen, g_seatNames[i])) {
            match = i;
            ++count;
        }
    }
    if (count == 1) return match;

    const int fromOwner = LookupChatOwner(text);
    if (fromOwner >= 0) return fromOwner;

    return -1;
}

// Detect "Player X left/dropped/eliminated" chat lines and mark ally-screen gone.
void TryMarkLeaveFromChatText(const char* text)
{
    size_t nameOff = 0;
    size_t nameLen = 0;
    if (!ParseLeaveLine(text, &nameOff, &nameLen)) return;

    char name[80]{};
    memcpy(name, text + nameOff, nameLen);
    name[nameLen] = 0;

    // The system line redraws every frame while visible — notify once per name
    // per match epoch (dedup must not block the same name in a later game).
    const DWORD now = GetTickCount();
    const LONG epoch = InterlockedCompareExchange(&g_matchEpoch, 0, 0);
    if (_stricmp(name, g_lastLeaveMarkName) == 0 &&
        epoch == g_lastLeaveMarkEpoch &&
        (now - g_lastLeaveMarkTick) < 5000) {
        g_lastLeaveMarkTick = now;
        return;
    }
    strcpy_s(g_lastLeaveMarkName, name);
    g_lastLeaveMarkTick = now;
    g_lastLeaveMarkEpoch = epoch;

    const int seat = SeatForLeaveLine(text, nameOff, nameLen);
    Log("leave-chat name='%s' seat=%d text=%.80s", name, seat, text);
    // Elim/surrender/drop all post "Name left" — mark by seat when unique.
    if (seat >= 0) {
        RememberSeatName(seat, name, nameLen);
        RememberChatOwner(text, seat);
        NotifyAllyLeaveFromChat(seat);
    } else {
        NotifyAllyLeaveByName(name);
    }
}

void LogPlayerNames()
{
    if (!g_playerName0) {
        Log("names: null");
        return;
    }
    for (int i = 0; i < 8; ++i) {
        const char* n = PlayerName(i);
        __try {
            Log("name[%d]=%s", i, (n && n[0]) ? n : "(empty)");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("name[%d]=<fault>", i);
        }
    }
}

// Leave line: timestamp + " left" in gold, player name in seat color.
// DrawTextColored always starts at the line origin — never pass suffix-only text.
static bool DrawLeaveLineColored(void* ui, const char* base, size_t leaveOff, size_t leaveLen,
                                 size_t stampLen, int seat)
{
    if (!ui || !base || !g_originalDraw) return false;
    if (seat < 0 || seat > 7) return false;
    if (stampLen + leaveOff + leaveLen >= 200) return false;

    const uint32_t gold = kSystemMessageColor;
    const uint32_t nameColor = g_colors[SeatToColorSlot(seat)];
    const size_t nameEndAbs = stampLen + leaveOff + leaveLen;

    char throughName[224]{};
    memcpy(throughName, base, nameEndAbs);
    throughName[nameEndAbs] = 0;

    // 1) Full line gold (timestamp + suffix stay gold after step 3).
    g_originalDraw(ui, base, gold);
    // 2) Through player name in seat color.
    g_originalDraw(ui, throughName, nameColor);
    // 3) Restore "[HH:MM] " in gold (step 2 may have tinted it with name color).
    if (stampLen > 0 && stampLen < sizeof(throughName)) {
        char stampOnly[224]{};
        memcpy(stampOnly, base, stampLen);
        stampOnly[stampLen] = 0;
        g_originalDraw(ui, stampOnly, gold);
    }
    return true;
}

bool PatchPrologue7(uint8_t* site, void* hook, uint8_t* savedOrig, void** trampOut, void** originalOut)
{
    memcpy(savedOrig, site, 7);
    void* tramp = VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return false;

    auto* t = static_cast<uint8_t*>(tramp);
    memcpy(t, site, 7);
    t[7] = 0xE9;
    *reinterpret_cast<int32_t*>(t + 8) =
        static_cast<int32_t>((site + 7) - (t + 12));

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return false;
    }

    site[0] = 0xE9;
    *reinterpret_cast<int32_t*>(site + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(hook) - (site + 5));
    site[5] = 0x90;
    site[6] = 0x90;

    VirtualProtect(site, 7, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 7);
    *trampOut = tramp;
    *originalOut = tramp;
    return true;
}

} // namespace

extern "C" void __cdecl ChatNameColor_OnDrawColored(void* ui, const char* text, uint32_t color)
{
    if (!g_originalDraw) return;
    if (!ui || !text || !text[0]) {
        if (g_originalDraw) g_originalDraw(ui, text, color);
        return;
    }

    // Always watch for leave/drop/elim lines (does not require chat-color
    // feature). Skipped while the history view replays old lines — a
    // re-shown "X Left" from an earlier game must not mark anyone again.
    if (!InterlockedCompareExchange(&g_viewing, 0, 0))
        TryMarkLeaveFromChatText(text);

    // Timestamp mod: prefix "[HH:MM] " (arrival time). Independent of the
    // name-color feature; all draw layers below shift by stampLen. Matching
    // (names, owners, stamps) always keys on the ORIGINAL text.
    const bool stampsOn = InterlockedCompareExchange(&g_timestamps, 0, 0) != 0;
    char stamped[224]{};
    size_t stampLen = 0;
    const char* base = text;
    if (stampsOn) {
        const char* stamp = StampFor(text);
        stampLen = strlen(stamp);
        _snprintf_s(stamped, _TRUNCATE, "%s%s", stamp, text);
        base = stamped;
    }

    // Leave lines: gamertag in seat color, " left"/suffix in gold. Runs even
    // when "Name:" chat recolor is off — system lines are never typed chat.
    {
        size_t leaveOff = 0;
        size_t leaveLen = 0;
        if (ParseLeaveLine(text, &leaveOff, &leaveLen) &&
            stampLen + leaveOff + leaveLen < 200) {
            const int seat = SeatForLeaveLine(text, leaveOff, leaveLen);
            if (seat >= 0 &&
                DrawLeaveLineColored(ui, base, leaveOff, leaveLen, stampLen, seat)) {
                InterlockedIncrement(&g_recolors);
                const LONG hits = InterlockedCompareExchange(&g_hits, 0, 0);
                if (hits <= 40 || (hits % 50) == 0) {
                    Log("recolor-leave seat=%d slot=%d off=%u len=%u text=%.60s",
                        seat, SeatToColorSlot(seat), (unsigned)leaveOff, (unsigned)leaveLen, text);
                }
                return;
            }
        }
    }

    const bool chatOn = InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
    if (!chatOn) {
        g_originalDraw(ui, base, color);
        return;
    }

    const LONG hits = InterlockedIncrement(&g_hits);
    size_t channelLen = 0;
    size_t nameEnd = 0;
    const int player = MatchPlayerIndex(text, &channelLen, &nameEnd);

    if (player < 0 || nameEnd == 0 || nameEnd >= 160) {
        if (hits <= 30) Log("hit#%ld pass text=%.80s gameColor=%08X", hits, text, color);
        g_originalDraw(ui, base, color);
        return;
    }

    // Always use a neutral body — the game's passed chat color is often wrong
    // for this path (e.g. purple) and made the whole line look off.
    const uint32_t bodyColor = kBodyColor;
    const int colorSlot = SeatToColorSlot(player);
    const uint32_t nameColor = g_colors[colorSlot];

    // 1) Full line in body color.
    g_originalDraw(ui, base, bodyColor);

    // 2) Draw through end of "Name:" in player color.
    const size_t nameEndAbs = stampLen + nameEnd;
    if (nameEndAbs >= 200) {
        return;
    }
    char throughName[224]{};
    memcpy(throughName, base, nameEndAbs);
    throughName[nameEndAbs] = 0;
    g_originalDraw(ui, throughName, nameColor);

    // 3) Redraw the stamp + channel prefix in body color so only the name
    //    stays player-colored: "[21:08] (To All) " body + "Name:" player.
    const size_t prefixLen = stampLen + channelLen;
    if (prefixLen > 0 && prefixLen < nameEndAbs && prefixLen < sizeof(throughName)) {
        char channelOnly[224]{};
        memcpy(channelOnly, base, prefixLen);
        channelOnly[prefixLen] = 0;
        g_originalDraw(ui, channelOnly, bodyColor);
    }

    InterlockedIncrement(&g_recolors);
    if (hits <= 40 || (hits % 50) == 0) {
        Log("recolor-name p=%d c=%d local=%d ch=%u end=%u name=%08X game=%08X text=%.60s",
            player, colorSlot, ReadLocalPlayerIndex(), (unsigned)channelLen, (unsigned)nameEnd,
            nameColor, color, text);
        static LONG s_nameDump = 0;
        if (InterlockedIncrement(&s_nameDump) <= 3)
            LogPlayerNames();
    }
}

static void __declspec(naked) Hook_DrawColored()
{
    __asm {
        // Site prologue was: push ebp; mov ebp,esp; mov eax,[ebp+0x10]; push eax
        // We replace it entirely and forward to our cdecl handler.
        jmp ChatNameColor_OnDrawColored
    }
}

// Runs on the game thread inside PushMapMsg: record every pushed line into
// our history, forward to the original, then keep an active PageUp view
// anchored (the push just shifted the ring under it).
extern "C" void __cdecl ChatHistory_OnPushMapMsg(const char* text, uint32_t color, uint32_t duration)
{
    RefreshSeatNameSnapshot();
    RecordHistory(text, static_cast<uint8_t>(color));
    if (text && text[0]) {
        size_t nameOff = 0;
        size_t nameLen = 0;
        if (ParseLeaveLine(text, &nameOff, &nameLen)) {
            // Mark and cache seat as early as possible — draw may happen before
            // packet hooks on some clients (non-host peers).
            TryMarkLeaveFromChatText(text);
            const int seat = SeatForLeaveLine(text, nameOff, nameLen);
            if (seat >= 0) {
                RememberSeatName(seat, text + nameOff, nameLen);
                RememberChatOwner(text, seat);
            }
        }
    }
    if (g_originalPush) g_originalPush(text, color, duration);
    if (InterlockedCompareExchange(&g_viewing, 0, 0)) {
        LONG back = InterlockedCompareExchange(&g_viewBack, 0, 0) + 1;
        if (back > static_cast<LONG>(kHistRing) - 1) back = static_cast<LONG>(kHistRing) - 1;
        InterlockedExchange(&g_viewBack, back);
        RenderHistoryView(kViewHoldMs);
    }
}

static void __declspec(naked) Hook_PushMapMsg()
{
    __asm {
        // Function prologue (7 bytes, whole instructions) lives in the
        // trampoline; forward the original stack args to our cdecl handler.
        jmp ChatHistory_OnPushMapMsg
    }
}

namespace {

uint8_t* FindDrawTextColored(uint8_t* base, size_t imageSize)
{
    // 0x5AEE20 / 0x5AEEB0 share the same prologue; only 0x5AEEB0 calls 0x5AED00.
    //   push ebp; mov ebp,esp
    //   mov eax,[ebp+10]; push eax; mov ecx,[ebp+0C]; push ecx
    //   call strlen; add esp,4; push eax
    //   mov edx,[ebp+0C]; push edx; mov eax,[ebp+8]; push eax
    //   call DrawImpl
    const uint8_t pat[] = {
        0x55, 0x8B, 0xEC,
        0x8B, 0x45, 0x10, 0x50,
        0x8B, 0x4D, 0x0C, 0x51,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x83, 0xC4, 0x04, 0x50,
        0x8B, 0x55, 0x0C, 0x52,
        0x8B, 0x45, 0x08, 0x50,
        0xE8
    };
    const char* mask = "xxxxxxxxxxx????xxxxxxxxxxxxx";
    const size_t len = strlen(mask);
    const uint8_t* expectedImpl = base + (kPreferredDrawImpl - kPreferredImageBase);

    if (imageSize < len + 5) return nullptr;
    for (size_t i = 0; i + len + 4 < imageSize; ++i) {
        uint8_t* p = base + i;
        if (!IsLikelyCode(p, len + 4)) continue;
        if (!MatchBytes(p, pat, mask)) continue;
        const int32_t rel = *reinterpret_cast<int32_t*>(p + 0x1D);
        const uint8_t* target = (p + 0x1C) + 5 + rel;
        if (target == expectedImpl) return p;
    }

    // Fallback: preferred VA under ASLR.
    uint8_t* site = base + (kPreferredDrawColored - kPreferredImageBase);
    if (site[0] == 0x55 && site[1] == 0x8B && site[2] == 0xEC) return site;
    return nullptr;
}

bool InstallChatOwnerHook(uint8_t* base, size_t imageSize)
{
    // Chat compose (~4D33B6): push esi; push 0; push eax; call PushMapMsg
    // Before that push, remember (text=eax, player=[ebp+0x0C]) so draw can
    // color by slot when two humans share the same display name.
    const uint8_t* pushTarget = base + (kPreferredPushMapMsg - kPreferredImageBase);
    uint8_t* site = nullptr;
    for (size_t i = 0; i + 10 <= imageSize; ++i) {
        uint8_t* p = base + i;
        if (!IsLikelyCode(p, 10)) continue;
        if (p[0] != 0x56 || p[1] != 0x6A || p[2] != 0x00 || p[3] != 0x50 || p[4] != 0xE8)
            continue;
        const int32_t rel = *reinterpret_cast<int32_t*>(p + 5);
        const uint8_t* target = (p + 4) + 5 + rel;
        if (target == pushTarget) {
            site = p;
            break;
        }
    }
    if (!site) {
        Log("Install: chat-owner site (push→PushMapMsg) not found");
        return false;
    }

    void* caveMem = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!caveMem) return false;
    auto* cave = static_cast<uint8_t*>(caveMem);
    size_t o = 0;

    // Preserve eax (text) and esi (duration); call Remember(text, player).
    // push eax
    cave[o++] = 0x50;
    // movzx ecx, byte [ebp+0x0C]
    cave[o++] = 0x0F; cave[o++] = 0xB6; cave[o++] = 0x4D; cave[o++] = 0x0C;
    // push ecx
    cave[o++] = 0x51;
    // push eax  (text)
    cave[o++] = 0x50;
    // call ChatNameColor_RememberOwner
    cave[o++] = 0xE8;
    *reinterpret_cast<int32_t*>(cave + o) =
        static_cast<int32_t>(reinterpret_cast<uint8_t*>(&ChatNameColor_RememberOwner) - (cave + o + 4));
    o += 4;
    // add esp, 8
    cave[o++] = 0x83; cave[o++] = 0xC4; cave[o++] = 0x08;
    // pop eax  (restore text)
    cave[o++] = 0x58;

    // Original: push esi; push 0; push eax; call PushMapMsg
    cave[o++] = 0x56;
    cave[o++] = 0x6A; cave[o++] = 0x00;
    cave[o++] = 0x50;
    cave[o++] = 0xE8;
    *reinterpret_cast<int32_t*>(cave + o) =
        static_cast<int32_t>(pushTarget - (cave + o + 4));
    o += 4;

    // jmp site+10
    cave[o++] = 0xE9;
    *reinterpret_cast<int32_t*>(cave + o) =
        static_cast<int32_t>((site + 10) - (cave + o + 4));
    o += 4;

    g_ownerOrigLen = 10;
    memcpy(g_ownerOrig, site, g_ownerOrigLen);

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, g_ownerOrigLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(caveMem, 0, MEM_RELEASE);
        return false;
    }
    site[0] = 0xE9;
    *reinterpret_cast<int32_t*>(site + 1) =
        static_cast<int32_t>(cave - (site + 5));
    for (size_t i = 5; i < g_ownerOrigLen; ++i) site[i] = 0x90;
    VirtualProtect(site, g_ownerOrigLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, g_ownerOrigLen);

    g_ownerSite = site;
    g_ownerCave = caveMem;
    Log("Install: chat-owner site=%p cave=%p", site, caveMem);
    return true;
}

// Prologue-hook PushMapMsg itself so every line (typed, received, system)
// lands in our history exactly once. Verified prologue for this build:
//   push ebp; mov ebp,esp; sub esp,0Ch; push esi   (7 bytes, whole instrs)
bool InstallPushHistoryHook(uint8_t* base)
{
    uint8_t* fn = base + (kPreferredPushMapMsg - kPreferredImageBase);
    static const uint8_t kPrologue[7] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56 };
    if (!IsLikelyCode(fn, sizeof(kPrologue)) || memcmp(fn, kPrologue, sizeof(kPrologue)) != 0) {
        Log("Install: PushMapMsg prologue mismatch — history capture off");
        return false;
    }
    if (!PatchPrologue7(fn, reinterpret_cast<void*>(&Hook_PushMapMsg),
                        g_pushPrologue, &g_pushTrampoline,
                        reinterpret_cast<void**>(&g_originalPush))) {
        Log("Install: PatchPrologue7(PushMapMsg) failed");
        return false;
    }
    g_pushSite = fn;
    g_msgRing = base + (kPreferredMsgRing - kPreferredImageBase);
    g_msgInitFlag = base + (0x009B1798 - kPreferredImageBase);

    uint8_t* timeFn = base + (kPreferredTimeNow - kPreferredImageBase);
    // Expected: push ebp; mov ebp,esp; call rel32 — a pure ms-since-start getter.
    if (IsLikelyCode(timeFn, 4) &&
        timeFn[0] == 0x55 && timeFn[1] == 0x8B && timeFn[2] == 0xEC && timeFn[3] == 0xE8) {
        g_timeNow = reinterpret_cast<TimeNowFn>(timeFn);
    } else {
        Log("Install: TimeNow prologue mismatch — history render off");
    }
    Log("Install: history push=%p ring=%p timeNow=%p", fn, g_msgRing, g_timeNow);
    return true;
}

bool InstallDrawColoredHook(uint8_t* base, size_t imageSize)
{
    uint8_t* site = FindDrawTextColored(base, imageSize);
    if (!site) {
        Log("Install: DrawTextColored (→5AED00) not found");
        return false;
    }

    if (!PatchPrologue7(site, reinterpret_cast<void*>(&Hook_DrawColored),
                        g_originalPrologue, &g_trampoline,
                        reinterpret_cast<void**>(&g_originalDraw))) {
        Log("Install: PatchPrologue7 failed");
        return false;
    }

    g_drawSite = site;
    g_playerName0 = reinterpret_cast<char*>(base + (kPreferredName0 - kPreferredImageBase));
    g_localPlayer = base + (0x00918CCD - kPreferredImageBase);
    g_seatColors = base + (0x00919390 - kPreferredImageBase);
    Log("Install: ok drawColored=%p tramp=%p names=%p local=%p seatColors=%p (chat path)",
        site, g_originalDraw, g_playerName0, g_localPlayer, g_seatColors);
    return true;
}

bool InstallHook()
{
    HMODULE game = GetModuleHandleW(L"Warcraft II.exe");
    if (!game) game = GetModuleHandleW(nullptr);
    if (!game) {
        Log("InstallHook: no module");
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    auto* base = reinterpret_cast<uint8_t*>(game);
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    LoadColorsFromJson();
    if (!InstallDrawColoredHook(base, imageSize)) return false;
    if (!InstallChatOwnerHook(base, imageSize))
        Log("InstallHook: continuing without chat-owner hook (duplicate names may share color)");
    g_matchThread = CreateThread(nullptr, 0, MatchWatchThread, nullptr, 0, nullptr);
    if (!g_matchThread) Log("InstallHook: match watch thread failed (%lu)", GetLastError());
    if (InstallPushHistoryHook(base)) {
        g_keyThread = CreateThread(nullptr, 0, HistoryKeyThread, nullptr, 0, nullptr);
        if (!g_keyThread) Log("InstallHook: history key thread failed (%lu)", GetLastError());
    } else {
        Log("InstallHook: continuing without chat history (PageUp/PageDown off)");
    }
    return true;
}

void RemoveHook()
{
    // Detach runs at process exit (the DLL is never freed at runtime); other
    // threads are already gone, so signal the key thread without waiting.
    InterlockedExchange(&g_keyStop, 1);
    if (g_matchThread) {
        WaitForSingleObject(g_matchThread, 500);
        CloseHandle(g_matchThread);
        g_matchThread = nullptr;
    }
    if (g_keyThread) {
        WaitForSingleObject(g_keyThread, 500);
        CloseHandle(g_keyThread);
        g_keyThread = nullptr;
    }
    if (g_pushSite && g_pushPrologue[0]) {
        DWORD oldProtect = 0;
        if (VirtualProtect(g_pushSite, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_pushSite, g_pushPrologue, 7);
            VirtualProtect(g_pushSite, 7, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), g_pushSite, 7);
        }
        g_pushSite = nullptr;
    }
    if (g_pushTrampoline) {
        VirtualFree(g_pushTrampoline, 0, MEM_RELEASE);
        g_pushTrampoline = nullptr;
        g_originalPush = nullptr;
    }
    if (g_ownerSite && g_ownerOrigLen) {
        DWORD oldProtect = 0;
        if (VirtualProtect(g_ownerSite, g_ownerOrigLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_ownerSite, g_ownerOrig, g_ownerOrigLen);
            VirtualProtect(g_ownerSite, g_ownerOrigLen, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), g_ownerSite, g_ownerOrigLen);
        }
        g_ownerSite = nullptr;
        g_ownerOrigLen = 0;
    }
    if (g_ownerCave) {
        VirtualFree(g_ownerCave, 0, MEM_RELEASE);
        g_ownerCave = nullptr;
    }
    if (g_drawSite && g_originalPrologue[0]) {
        DWORD oldProtect = 0;
        if (VirtualProtect(g_drawSite, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_drawSite, g_originalPrologue, 7);
            VirtualProtect(g_drawSite, 7, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), g_drawSite, 7);
        }
    }
    if (g_trampoline) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
    }
    g_drawSite = nullptr;
    g_originalDraw = nullptr;
    g_playerName0 = nullptr;
    InterlockedExchange(&g_ready, 0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (InstallHook()) {
            InterlockedExchange(&g_ready, 1);
            InterlockedExchange(&g_enabled, 0);
            LogPlayerNames();
            Log("DllMain: ready (awaiting SetEnabled chat)");
        } else {
            Log("DllMain: install failed");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        RemoveHook();
    }
    return TRUE;
}

extern "C" __declspec(dllexport) DWORD __stdcall ChatNameColor_IsReady(LPVOID)
{
    return static_cast<DWORD>(InterlockedCompareExchange(&g_ready, 0, 0));
}

extern "C" __declspec(dllexport) void __stdcall ChatNameColor_OnNewMatch()
{
    ResetLeaveOwnerCache();
    Log("ChatNameColor_OnNewMatch epoch=%ld", InterlockedCompareExchange(&g_matchEpoch, 0, 0));
}

extern "C" __declspec(dllexport) DWORD __stdcall ChatNameColor_SetEnabled(LPVOID enabled)
{
    const LONG on = enabled ? 1 : 0;
    if (on) LoadColorsFromJson();
    InterlockedExchange(&g_enabled, on);
    Log("Chat SetEnabled=%ld ready=%ld hits=%ld recolors=%ld",
        on,
        InterlockedCompareExchange(&g_ready, 0, 0),
        InterlockedCompareExchange(&g_hits, 0, 0),
        InterlockedCompareExchange(&g_recolors, 0, 0));
    return static_cast<DWORD>(InterlockedCompareExchange(&g_ready, 0, 0));
}

// Timestamp mod ("[HH:MM] " before chat lines). Separate feature flag —
// works with or without the name-color mod.
extern "C" __declspec(dllexport) DWORD __stdcall ChatNameColor_SetTimestamps(LPVOID enabled)
{
    InterlockedExchange(&g_timestamps, enabled ? 1 : 0);
    Log("Chat SetTimestamps=%d", enabled ? 1 : 0);
    return static_cast<DWORD>(InterlockedCompareExchange(&g_ready, 0, 0));
}

// Chat history recall mod (PageUp/PageDown re-show earlier lines).
// Separate feature flag — works with or without the other chat mods.
extern "C" __declspec(dllexport) DWORD __stdcall ChatNameColor_SetHistory(LPVOID enabled)
{
    InterlockedExchange(&g_historyOn, enabled ? 1 : 0);
    Log("Chat SetHistory=%d count=%ld", enabled ? 1 : 0,
        InterlockedCompareExchange(&g_histCount, 0, 0));
    return static_cast<DWORD>(InterlockedCompareExchange(&g_ready, 0, 0));
}
