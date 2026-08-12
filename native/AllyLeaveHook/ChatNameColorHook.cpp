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
constexpr uint32_t kBodyColor = 0xFFE3E3E3; // light gray / default chat body

using DrawColoredFn = void(__cdecl*)(void* ui, const char* text, uint32_t color);

volatile LONG g_enabled = 0;       // chat "Name:" lines
volatile LONG g_ready = 0;
volatile LONG g_hits = 0;
volatile LONG g_recolors = 0;

uint8_t* g_drawSite = nullptr;
uint8_t g_originalPrologue[8]{};
void* g_trampoline = nullptr;
DrawColoredFn g_originalDraw = nullptr;
char* g_playerName0 = nullptr;
uint8_t* g_localPlayer = nullptr; // VA 0x918CCD (+ slide)

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
};
ChatOwnerEntry g_owners[kOwnerRing]{};
volatile LONG g_ownerWrite = 0;

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

void RememberChatOwner(const char* text, int player)
{
    if (!text || !text[0] || player < 0 || player > 7) return;
    const LONG idx = InterlockedIncrement(&g_ownerWrite) - 1;
    ChatOwnerEntry& e = g_owners[static_cast<size_t>(idx) % kOwnerRing];
    strncpy_s(e.text, text, _TRUNCATE);
    e.player = player;
    e.tick = GetTickCount();
}

// cdecl helper invoked from the compose cave: (text, player)
extern "C" void __cdecl ChatNameColor_RememberOwner(const char* text, uint32_t player)
{
    RememberChatOwner(text, static_cast<int>(player));
    if (text && text[0])
        Log("owner-remember p=%u text=%.60s", player, text);
}

int LookupChatOwner(const char* text)
{
    if (!text || !text[0]) return -1;
    int best = -1;
    DWORD bestTick = 0;
    for (size_t i = 0; i < kOwnerRing; ++i) {
        const ChatOwnerEntry& e = g_owners[i];
        if (e.player < 0 || e.player > 7 || !e.text[0]) continue;
        if (strcmp(e.text, text) != 0) continue;
        if (e.tick >= bestTick) {
            bestTick = e.tick;
            best = e.player;
        }
    }
    return best;
}

FARPROC ResolveAllyExport(const char* exportName);

// Ask AllyLeaveHook for the alliances-row of this name. Those rows are
// engine player-index ordered — the exact order the game's colors use —
// unlike the join-ordered 0x91ADA8 table below.
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

    // Color-ordered source first: the alliances rows tracked by AllyLeaveHook.
    const int fromRows = AllyRowByName(nameStart, nameLen);
    if (fromRows >= 0) return fromRows;

    // Prefer sender slot remembered at compose (handles duplicate Battle.net names).
    // NOTE: compose slots and the table below are join-ordered — colors can be
    // wrong in shuffled lobbies until the alliances rows have been painted once.
    const int fromMsg = LookupChatOwner(text);
    if (fromMsg >= 0) return fromMsg;

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
    if (matchCount == 1) return matches[0];
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
    if (!fn) {
        // x86 stdcall exports are decorated: _Name@4
        char decorated[128]{};
        sprintf_s(decorated, "_%s@4", exportName);
        fn = GetProcAddress(ally, decorated);
    }
    if (!fn) Log("ally-notify: export %s not found", exportName);
    return fn;
}

void NotifyAllyLeaveByName(const char* name)
{
    if (!name || !name[0]) return;
    auto fn = reinterpret_cast<MarkGoneByNameFn>(
        ResolveAllyExport("AllyLeave_MarkGoneByName"));
    if (fn) fn(name);
}

void NotifyAllyLeaveByIndex(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    auto fn = reinterpret_cast<MarkGoneFn>(
        ResolveAllyExport("AllyLeave_MarkGone"));
    if (fn) fn(playerIndex);
}

// Detect "Player X left/dropped/eliminated" chat lines and mark ally-screen gone.
void TryMarkLeaveFromChatText(const char* text)
{
    if (!text || !text[0]) return;
    const char* start = text;
    while (*start && (static_cast<unsigned char>(*start) < 0x20 || *start == ' ')) ++start;

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
    if (!hit) return;

    const char* nameStart = start;
    if (StartsWithIgnoreCase(nameStart, "Player "))
        nameStart += 7;
    while (*nameStart == ' ' || *nameStart == '\t') ++nameStart;
    if (nameStart >= hit || nameStart[0] == 0) return;

    char name[80]{};
    size_t n = static_cast<size_t>(hit - nameStart);
    if (n == 0 || n >= sizeof(name)) return;
    memcpy(name, nameStart, n);
    name[n] = 0;
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t')) name[--n] = 0;
    if (n == 0) return;

    // The system line redraws every frame while visible — notify once per name.
    static char s_lastName[80]{};
    static DWORD s_lastTick = 0;
    const DWORD now = GetTickCount();
    if (_stricmp(name, s_lastName) == 0 && (now - s_lastTick) < 5000) {
        s_lastTick = now;
        return;
    }
    strcpy_s(s_lastName, name);
    s_lastTick = now;

    Log("leave-chat name='%s' text=%.80s", name, text);
    // Name-only notify: the ally hook maps it onto the alliances row (or
    // queues it until F11 binds the name). The 0x91ADA8 table used by
    // MatchPlayerIndex is join-ordered and marked the wrong seat.
    NotifyAllyLeaveByName(name);
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

    // Always watch for leave/drop/elim lines (does not require chat-color feature).
    TryMarkLeaveFromChatText(text);

    const bool chatOn = InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
    if (!chatOn) {
        g_originalDraw(ui, text, color);
        return;
    }

    const LONG hits = InterlockedIncrement(&g_hits);
    size_t channelLen = 0;
    size_t nameEnd = 0;
    const int player = MatchPlayerIndex(text, &channelLen, &nameEnd);

    if (player < 0 || nameEnd == 0 || nameEnd >= 160) {
        if (hits <= 30) Log("hit#%ld pass text=%.80s gameColor=%08X", hits, text, color);
        g_originalDraw(ui, text, color);
        return;
    }

    // Always use a neutral body — the game's passed chat color is often wrong
    // for this path (e.g. purple) and made the whole line look off.
    const uint32_t bodyColor = kBodyColor;
    const uint32_t nameColor = g_colors[player];

    // 1) Full line in body color.
    g_originalDraw(ui, text, bodyColor);

    // 2) Draw through end of "Name:" in player color.
    char throughName[160]{};
    memcpy(throughName, text, nameEnd);
    throughName[nameEnd] = 0;
    g_originalDraw(ui, throughName, nameColor);

    // 3) If there was a channel prefix, redraw it in body color so only the
    //    name stays player-colored: "(To All) " body + "Name:" player + " msg" body.
    if (channelLen > 0 && channelLen < nameEnd && channelLen < sizeof(throughName)) {
        char channelOnly[96]{};
        memcpy(channelOnly, text, channelLen);
        channelOnly[channelLen] = 0;
        g_originalDraw(ui, channelOnly, bodyColor);
    }

    InterlockedIncrement(&g_recolors);
    if (hits <= 40 || (hits % 50) == 0) {
        Log("recolor-name p=%d local=%d ch=%u end=%u name=%08X game=%08X text=%.60s",
            player, ReadLocalPlayerIndex(), (unsigned)channelLen, (unsigned)nameEnd,
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
    Log("Install: ok drawColored=%p tramp=%p names=%p local=%p (chat path)",
        site, g_originalDraw, g_playerName0, g_localPlayer);
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
    return true;
}

void RemoveHook()
{
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
