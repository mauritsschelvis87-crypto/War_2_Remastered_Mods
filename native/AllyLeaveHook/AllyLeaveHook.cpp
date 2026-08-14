// Warcraft II Remastered — Alliances leave indicator (red name)
// 1) Hooks leave/drop/elim status writes → g_leftFlags[player]
// 2) Tracks unit gain/lost per player; last asset wipe → UI gone marker
// 3) Hooks alliances row name bind; when leftFlags OR status>=2, force red + GONE

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kRedTextColor = 0xFF0000FF; // pure red RGB(255,0,0) + A — same gone mark for every seat
constexpr size_t kUiColorOffset = 0x20C;
constexpr uintptr_t kPreferredImageBase = 0x00400000;
constexpr uintptr_t kPreferredContentManager = 0x00966160;
constexpr uintptr_t kPreferredLayoutWidget = 0x005D1AE0;
constexpr uintptr_t kPreferredRenderLabel = 0x005AEBB0;
constexpr uintptr_t kPreferredDrawImage = 0x005C4B60;
constexpr uintptr_t kPreferredFindAtlas = 0x005F6340;
constexpr uintptr_t kPreferredFindAtlasImage = 0x00617450;
constexpr uint32_t kGoneAtlasFnv = 0x52A8BD96u;   // "qol_ally_leave"
constexpr uint32_t kGoneFrameFnv = 0x6F4E1B19u;   // "skeleton_head"
constexpr float kGoneIconDrawSize = 14.0f;
constexpr uint32_t kGoneIconTint = 0xFFFFFFFFu;   // preserve atlas alpha
constexpr char kGoneNamePrefix[] = "   ";         // gap before red name text

struct NkRect {
    float x;
    float y;
    float w;
    float h;
};

#pragma pack(push, 1)
struct NkImage {
    uint32_t handle;
    uint16_t w;
    uint16_t h;
    uint16_t region[4]; // atlas u,v,w,h
};
#pragma pack(pop)

using SetTextFn = void(__cdecl*)(void* ui, const char* name, int prop);
using RenderLabelFn = void(__cdecl*)(void* ui, void* styleCtx, const char* text, void* colorPtr, int prop);
using DrawImageFn = void(__cdecl*)(void* cmdBuf, float x, float y, float w, float h,
                                   const NkImage* image, uint32_t color);
using FindAtlasFn = void*(__thiscall*)(void* contentMgr, uint32_t atlasFnv);
using FindAtlasImageFn = bool(__thiscall*)(void* atlas, NkImage* out, uint32_t frameFnv);

volatile LONG g_enabled = 0;
volatile LONG g_ready = 0;
volatile LONG g_hitCount = 0;
volatile LONG g_recolorCount = 0;
volatile LONG g_leaveEventCount = 0;
// Feature split: mark computer (NPC) and/or human slots independently.
volatile LONG g_markComputers = 1;
volatile LONG g_markHumans = 0;

uint8_t* g_statusBase = nullptr;
// Per-player defeat/result flag at statusBase+0x21D8 (VA 0x91AA84).
// Values: 0=ok, 1=left, 2=eliminated, 3=defeat; other bytes are garbage/unused.
uint8_t* g_defeatBase = nullptr;
constexpr ptrdiff_t kDefeatFromStatus = 0x21D8;
// Preferred VAs in Warcraft II.exe (ImageBase 0x400000).
constexpr uintptr_t kPreferredStatus = 0x00918CAC;
// Lobby/slot table: stride 0x26. Byte0 mirrors controller/status
// (1=human, 2/4/6/7=computer variants, 3=gone, 5=empty). Byte2=race.
// At match start computers are often remapped 4→1, so cache kind early.
constexpr uintptr_t kPreferredSlotBase = 0x00916268;
constexpr size_t kSlotStride = 0x26;
constexpr uintptr_t kPreferredPlayerName0 = 0x0091ADA8;
constexpr size_t kPlayerNameStride = 0x38;
uint8_t* g_slotBase = nullptr;
char* g_playerName0 = nullptr;
// -1 unknown, 0 human, 1 computer
volatile LONG g_slotKind[8]{ -1, -1, -1, -1, -1, -1, -1, -1 };
// Per-unit-type linked-list heads (dword[type] → unit*, next at unit+0x68).
constexpr uintptr_t kPreferredUnitTypeHeads = 0x00934848;
// type → word[8] per-player counts (human/orc pairs often share one row).
constexpr uintptr_t kPreferredTypeCountRows = 0x008C0B80;
// PUD-style: Farm and above are buildings.
constexpr int kFirstBuildingType = 58;
// HasForces excludes these from wipe (still “alive” if only these remain → wiped).
// 26/27 oil tanker, 28/29 transport, 40 flying machine, 41 zeppelin.
constexpr int kMaxUnitTypesScan = 105;
constexpr int kMaxPerTypeList = 256;

void** g_unitTypeHeads = nullptr;
uint16_t** g_typeCountRows = nullptr; // 8C0B80[type] → word[8]
// Per-player wipe census (excludes tanker/transport/flyer/zeppelin).
volatile LONG g_units[8]{};
volatile LONG g_buildings[8]{};
volatile LONG g_liveAssets[8]{}; // units+buildings cache for UI logs
volatile LONG g_censusDone = 0;
using HasForcesFn = int(__cdecl*)(int player);
HasForcesFn g_hasForces = nullptr;
uint8_t* g_localPlayer = nullptr; // VA 0x918CCD

// Game eliminate helper: void __cdecl EliminatePlayer(int player) @ 0x4F3F80
using EliminateFn = void(__cdecl*)(int player);
EliminateFn g_originalEliminate = nullptr;
uint8_t* g_eliminateSite = nullptr;
uint8_t g_origEliminate[7]{};
void* g_eliminateTramp = nullptr;

// Leave/drop/elim chat announce: void __cdecl @ 0x4F4F30 (player index).
// Called for "Player %s left/dropped/eliminated" — more reliable than status-store H1.
using AnnounceGoneFn = void(__cdecl*)(int player);
AnnounceGoneFn g_originalAnnounceGone = nullptr;
uint8_t* g_announceGoneSite = nullptr;
uint8_t g_origAnnounceGone[7]{};
void* g_announceGoneTramp = nullptr;

SetTextFn g_originalSetText = nullptr;
RenderLabelFn g_originalRenderLabel = nullptr;
DrawImageFn g_drawImage = nullptr;
FindAtlasFn g_findAtlas = nullptr;
FindAtlasImageFn g_findAtlasImage = nullptr;
uint8_t* g_renderLabelSite = nullptr;
uint8_t g_origRenderLabel[7]{};
void* g_renderLabelTramp = nullptr;
NkImage g_goneSkullImage{};
volatile LONG g_goneSkullResolved = 0;
thread_local bool g_skullDrawPending = false;
thread_local void* g_skullDrawUi = nullptr;
thread_local int g_skullDrawPlayer = -1;
uint8_t* g_patchSite = nullptr;
uint8_t g_originalCall[5]{};
void* g_trampoline = nullptr;

// H1: movzx eax,[esi+1]; mov [eax+status],3  — packet leave/drop/elim
// H2: mov [ecx+status],3 ; mov [eax+slot],3 — secondary gone helper
uint8_t* g_leaveWriteH1 = nullptr;
uint8_t* g_leaveWriteH2 = nullptr;
uint8_t g_origLeaveH1[7]{};
uint8_t g_origLeaveH2[7]{};
void* g_leaveTrampH1 = nullptr;
void* g_leaveTrampH2 = nullptr;

// Unit gain/lost count updaters (VA 0x4B52D0 / 0x4B5730).
using UnitCountFn = void(__cdecl*)(void* unit);
UnitCountFn g_originalUnitGained = nullptr;
UnitCountFn g_originalUnitLost = nullptr;
uint8_t* g_unitGainedSite = nullptr;
uint8_t* g_unitLostSite = nullptr;
uint8_t g_origUnitGained[7]{};
uint8_t g_origUnitLost[7]{};
void* g_unitGainedTramp = nullptr;
void* g_unitLostTramp = nullptr;

volatile LONG g_leftFlags[8]{};
// Seat left via an explicit game event (leave/drop/announce). Sticky even if
// the leaver's units stay alive on the map — HasForces must not clear it.
volatile LONG g_explicitGone[8]{};
// Names from "<name> Left/Dropped/Eliminated" chat lines waiting for an
// alliances row: F11 may not have been opened yet when the line appeared.
char g_pendingGoneNames[8][80]{};
DWORD g_pendingGoneTick[8]{};
volatile LONG g_everHadAssets[8]{};
// Game HasForces once returned >0 for this seat (army truly online).
volatile LONG g_everHadForces[8]{};
// Seat had HasForces>0 continuously long enough to trust wipe detection.
volatile LONG g_aliveConfirmed[8]{};
// Tick when continuous HasForces>0 streak started (0 = none).
volatile DWORD g_aliveSince[8]{};
// First tick we saw a continuous zero-army for this seat (0 = not zeroing).
volatile DWORD g_zeroArmySince[8]{};
volatile LONG g_forceRowLog = 0;
volatile LONG g_verboseRows = 0;
volatile DWORD g_lastCensusTick = 0;
constexpr DWORD kCensusMinIntervalMs = 400;
void* g_lastUi[8]{};
char g_lastName[8][80]{};
DWORD g_lastSetTextTick = 0;
char g_nameBuf[8][160]{};
char g_logPath[MAX_PATH]{};
volatile LONG g_logQuiet = 0; // 1 = skip hot-path file logs (still log install/gone)

void EnsureLogPath()
{
    if (g_logPath[0]) return;
    char temp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, temp);
    sprintf_s(g_logPath, "%swar2_ally_leave_hook.log", temp);
}

void Log(const char* fmt, ...)
{
    EnsureLogPath();
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

void LogHot(const char* fmt, ...)
{
    if (InterlockedCompareExchange(&g_logQuiet, 0, 0) != 0) return;
    EnsureLogPath();
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

uintptr_t GameSlide()
{
    if (g_statusBase) {
        return reinterpret_cast<uintptr_t>(g_statusBase) - kPreferredStatus;
    }
    HMODULE game = GetModuleHandleW(L"Warcraft II.exe");
    if (!game) return 0;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return reinterpret_cast<uintptr_t>(game) + nt->OptionalHeader.ImageBase - kPreferredImageBase;
}

void BindUiDrawApis(uint8_t* base, uintptr_t imageBase)
{
    if (!base || imageBase == 0) return;
    const uintptr_t slide = reinterpret_cast<uintptr_t>(base) + imageBase - kPreferredImageBase;
    (void)slide;
    g_drawImage = reinterpret_cast<DrawImageFn>(base + (kPreferredDrawImage - imageBase));
    g_findAtlas = reinterpret_cast<FindAtlasFn>(base + (kPreferredFindAtlas - imageBase));
    g_findAtlasImage = reinterpret_cast<FindAtlasImageFn>(base + (kPreferredFindAtlasImage - imageBase));
}

void* GetContentManager()
{
    const uintptr_t slide = GameSlide();
    // Game passes the global NUIContent object by address (mov ecx, imm32),
    // not via [imm32] — see call site @ 0x0053B964.
    return reinterpret_cast<void*>(kPreferredContentManager + slide);
}

bool ResolveGoneSkullImage()
{
    if (InterlockedCompareExchange(&g_goneSkullResolved, 0, 0) != 0) {
        return g_goneSkullImage.handle != 0;
    }
    if (!g_findAtlas || !g_findAtlasImage) return false;

    void* contentMgr = GetContentManager();
    if (!contentMgr) {
        static LONG s_log = 0;
        if (InterlockedIncrement(&s_log) <= 4) {
            Log("ResolveGoneSkull: no content manager");
        }
        return false;
    }

    void* atlas = nullptr;
    NkImage image{};
    __try {
        atlas = g_findAtlas(contentMgr, kGoneAtlasFnv);
        if (!atlas) {
            static LONG s_log = 0;
            if (InterlockedIncrement(&s_log) <= 4) {
                Log("ResolveGoneSkull: atlas qol_ally_leave not loaded");
            }
            return false;
        }
        if (!g_findAtlasImage(atlas, &image, kGoneFrameFnv)) {
            static LONG s_log = 0;
            if (InterlockedIncrement(&s_log) <= 4) {
                Log("ResolveGoneSkull: frame skeleton_head missing");
            }
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("ResolveGoneSkull: exception");
        return false;
    }

    g_goneSkullImage = image;
    InterlockedExchange(&g_goneSkullResolved, 1);
    Log("ResolveGoneSkull: ok handle=%u size=%ux%u",
        g_goneSkullImage.handle, g_goneSkullImage.w, g_goneSkullImage.h);
    return g_goneSkullImage.handle != 0;
}

bool ReadLabelBounds(void* ui, NkRect* out)
{
    if (!ui || !out) return false;
    __try {
        // Alliances SetText uses 0x005AEBB0; layout floats live at widget+0x1E0.
        const uint8_t* layout = static_cast<const uint8_t*>(ui) + 0x1E0;
        out->x = *reinterpret_cast<const float*>(layout + 0x34);
        out->y = *reinterpret_cast<const float*>(layout + 0x38);
        out->w = *reinterpret_cast<const float*>(layout + 0x3C);
        out->h = *reinterpret_cast<const float*>(layout + 0x40);
        return out->w > 1.0f && out->h > 1.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ArmSkullDraw(void* ui, int playerIndex)
{
    g_skullDrawPending = true;
    g_skullDrawUi = ui;
    g_skullDrawPlayer = playerIndex;
}

void DisarmSkullDraw()
{
    g_skullDrawPending = false;
    g_skullDrawUi = nullptr;
    g_skullDrawPlayer = -1;
}

void DrawGoneSkullIcon(void* ui, const NkRect& textRect, int playerIndex)
{
    if (!ui || textRect.w <= 0.0f || textRect.h <= 0.0f) {
        static LONG s_badRect = 0;
        if (InterlockedIncrement(&s_badRect) <= 8) {
            Log("gone icon: bad rect p=%d w=%.1f h=%.1f", playerIndex, textRect.w, textRect.h);
        }
        return;
    }
    if (!ResolveGoneSkullImage() || !g_drawImage) return;

    void* renderer = nullptr;
    void* cmdBuf = nullptr;
    __try {
        renderer = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(ui) + 0x3D28);
        if (!renderer) {
            static LONG s_noRen = 0;
            if (InterlockedIncrement(&s_noRen) <= 8) {
                Log("gone icon: no renderer p=%d ui=%p", playerIndex, ui);
            }
            return;
        }
        cmdBuf = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(renderer) + 0x64);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!cmdBuf) {
        static LONG s_noCmd = 0;
        if (InterlockedIncrement(&s_noCmd) <= 8) {
            Log("gone icon: no cmdBuf p=%d", playerIndex);
        }
        return;
    }

    const float icon = kGoneIconDrawSize;
    const float y = textRect.y + (textRect.h - icon) * 0.5f;
    const float x = textRect.x + 1.0f;
    __try {
        g_drawImage(cmdBuf, x, y, icon, icon, &g_goneSkullImage, kGoneIconTint);
        static LONG s_drawOk = 0;
        if (InterlockedIncrement(&s_drawOk) <= 8) {
            Log("gone icon: drew p=%d at %.0f,%.0f handle=%u", playerIndex, x, y, g_goneSkullImage.handle);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("gone icon: DrawImage exception p=%d", playerIndex);
    }
}

void __cdecl Hook_RenderLabel(void* ui, void* styleCtx, const char* text, void* colorPtr, int prop)
{
    if (g_originalRenderLabel) {
        g_originalRenderLabel(ui, styleCtx, text, colorPtr, prop);
    }
    if (!g_skullDrawPending || !ui || ui != g_skullDrawUi) {
        return;
    }

    NkRect bounds{};
    if (ReadLabelBounds(ui, &bounds)) {
        DrawGoneSkullIcon(ui, bounds, g_skullDrawPlayer);
    } else {
        static LONG s_fail = 0;
        if (InterlockedIncrement(&s_fail) <= 12) {
            Log("gone icon: label bounds failed p=%d ui=%p", g_skullDrawPlayer, ui);
        }
    }
    DisarmSkullDraw();
}

bool SeatEligibleForHfWipe(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return false;
    // Computers / unclassified: first hf>0 sample is enough (blue AI often
    // dies before the 8s human aliveConfirmed streak).
    // Known humans: same — 0 units after ever having forces is a wipe.
    return InterlockedCompareExchange(&g_everHadForces[playerIndex], 0, 0) != 0;
}

bool ReadJsonBoolKey(const char* buf, const char* key)
{
    const char* found = strstr(buf, key);
    if (!found) return false;
    const char* colon = strchr(found, ':');
    if (!colon) return false;
    for (const char* p = colon + 1; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') continue;
        if (*p == 't' || *p == 'T' || *p == '1') return true;
        return false;
    }
    return false;
}

void LoadMarkModesFromJson()
{
    wchar_t dllPath[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LoadMarkModesFromJson), &self) ||
        !self) {
        Log("LoadMarkModes: no self module");
        return;
    }
    if (!GetModuleFileNameW(self, dllPath, MAX_PATH)) return;

    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (!slash) slash = wcsrchr(dllPath, L'/');
    if (slash) slash[1] = 0;

    wchar_t jsonPath[MAX_PATH]{};
    swprintf_s(jsonPath, L"%s..\\extra-features.json", dllPath);
    wchar_t full[MAX_PATH]{};
    if (GetFullPathNameW(jsonPath, MAX_PATH, full, nullptr) == 0) {
        wcsncpy_s(full, jsonPath, _TRUNCATE);
    }

    HANDLE file = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Log("LoadMarkModes: missing %ls — default computers=1 humans=0", full);
        InterlockedExchange(&g_markComputers, 1);
        InterlockedExchange(&g_markHumans, 0);
        return;
    }

    char buf[2048]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) {
        Log("LoadMarkModes: empty config");
        return;
    }

    const bool markComputers = ReadJsonBoolKey(buf, "AllyLeaveMarkComputers");
    const bool markHumans = ReadJsonBoolKey(buf, "AllyLeaveMarkHumans");
    const bool legacy = ReadJsonBoolKey(buf, "AllyLeaveRedNames");
    LONG computers = markComputers ? 1 : 0;
    LONG humans = markHumans ? 1 : 0;
    if (!computers && !humans && legacy) {
        // Pre-split configs → NPC focus only (not mark-everyone).
        computers = 1;
    }
    InterlockedExchange(&g_markComputers, computers);
    InterlockedExchange(&g_markHumans, humans);
    Log("LoadMarkModes: computers=%ld humans=%ld legacy=%d from %ls",
        computers, humans, legacy ? 1 : 0, full);
}

bool IsComputerController(uint8_t controller)
{
    // Matches game helper at 0x4A0BE0 (minus empty/init 5).
    return controller == 2 || controller == 4 || controller == 6 || controller == 7;
}

uint8_t ReadLiveController(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return 0xFF;
    uint8_t controller = 0xFF;
    if (g_statusBase) {
        __try {
            controller = g_statusBase[playerIndex];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            controller = 0xFF;
        }
    }
    if (controller == 0xFF && g_slotBase) {
        __try {
            controller = g_slotBase[playerIndex * kSlotStride];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            controller = 0xFF;
        }
    }
    return controller;
}

bool AnyLiveComputerController()
{
    for (int i = 0; i < 8; ++i) {
        if (IsComputerController(ReadLiveController(i))) return true;
    }
    return false;
}

void CacheSlotKindFromController(int playerIndex, uint8_t controller)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    // Controller bytes are only trustworthy pre-match. In-match the same byte
    // is a status where 2 = ELIMINATED — an eliminated human reads as
    // "computer controller 2" and was getting NPC-marked.
    if (g_censusDone) return;
    if (IsComputerController(controller)) {
        // Classify unknown seats only; never flip an already-known human.
        InterlockedCompareExchange(&g_slotKind[playerIndex], 1, -1);
        return;
    }
    // Lobby humans are status/controller 1 while computers are still 4/2/6/7.
    // After remaster remaps computers 4→1 we must NOT latch those seats as human.
    if (controller == 1 && AnyLiveComputerController()) {
        InterlockedCompareExchange(&g_slotKind[playerIndex], 0, -1);
    }
}

void NoteHumanSlot(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    if (InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0) == 1) return;
    InterlockedExchange(&g_slotKind[playerIndex], 0);
}

void RefreshSlotKinds()
{
    // Controller-based classification is lobby-only (see CacheSlotKindFromController).
    for (int i = 0; i < 8; ++i) {
        const uint8_t controller = ReadLiveController(i);
        if (controller == 0xFF) continue;
        CacheSlotKindFromController(i, controller);
    }
    if (g_localPlayer) {
        int local = -1;
        __try {
            local = static_cast<int>(*g_localPlayer);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            local = -1;
        }
        if (local >= 0 && local <= 7) NoteHumanSlot(local);
    }
}

bool SourceLooksLikeHumanLeaveOnly(const char* source)
{
    if (!source || !source[0]) return false;
    // Explicit leave/drop only — not elimination or army-wipe paths.
    return _strnicmp(source, "announce", 8) == 0 ||
           _strnicmp(source, "H1", 2) == 0 ||
           _strnicmp(source, "H2", 2) == 0 ||
           _strnicmp(source, "chat", 4) == 0 ||
           _stricmp(source, "status3-human") == 0 ||
           _stricmp(source, "defeat-left") == 0;
}

// Remaster/PUD computer labels vs Battle.net account names (e.g. "Avent").
const char* NameForClassify(const char* name)
{
    if (!name) return "";
    // Strip our own gone prefix so reclassification stays stable.
    if (_strnicmp(name, "[X] ", 4) == 0) name += 4;
    else if (_strnicmp(name, "X ", 2) == 0) name += 2;
    else if (_strnicmp(name, kGoneNamePrefix, sizeof(kGoneNamePrefix) - 1) == 0) {
        name += sizeof(kGoneNamePrefix) - 1;
    }
    while (*name == ' ' || *name == '\t') ++name;
    return name;
}

bool LooksLikeComputerName(const char* name)
{
    name = NameForClassify(name);
    if (!name[0]) return false;
    if (_strnicmp(name, "Computer", 8) == 0) return true;
    if (_strnicmp(name, "Nation of ", 10) == 0) return true;
    if (_stricmp(name, "Alliance Traitors") == 0) return true;
    const size_t n = strlen(name);
    if (n >= 5 && _stricmp(name + (n - 5), " Clan") == 0) return true;
    // Remaster F11 often drops the " Clan" suffix (blue = "Stormreaver").
    static const char* kHordeClans[] = {
        "Stormreaver", "Black Tooth", "Black Tooth Grin", "Twilight's Hammer",
        "Bleeding Hollow", "Dragonmaw", "Blackrock", "Burning Blade",
    };
    for (const char* clan : kHordeClans) {
        if (_stricmp(name, clan) == 0) return true;
    }
    return false;
}

void NoteComputerSlot(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    InterlockedExchange(&g_slotKind[playerIndex], 1);
}

bool IsLocalPlayer(int playerIndex); // defined with wipe helpers below

bool IsComputerSlot(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return false;
    RefreshSlotKinds();
    const LONG kind = InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0);
    if (kind == 1) return true;
    if (kind == 0) return false;
    // Unknown seat: only the name is safe evidence in-match. The controller
    // byte doubles as status (2 = eliminated) and must not be used here.
    if (LooksLikeComputerName(g_lastName[playerIndex])) return true;
    return false;
}

// Two independent features that also combine:
//  - NPC feature (markComputers): computer seats, marked by army-wipe/eliminate.
//  - Human feature (markHumans): human seats, marked by explicit leave/drop/
//    announce events plus eliminate and army wipe.
// A seat is judged by its kind, so each feature only ever touches its own kind.
bool ShouldMarkSlot(int playerIndex, const char* source)
{
    if (playerIndex < 0 || playerIndex > 7) return false;

    const bool markComputers = InterlockedCompareExchange(&g_markComputers, 0, 0) != 0;
    const bool markHumans = InterlockedCompareExchange(&g_markHumans, 0, 0) != 0;
    if (!markComputers && !markHumans) return false;
    if (IsLocalPlayer(playerIndex)) return false;

    const bool humanLeave = SourceLooksLikeHumanLeaveOnly(source);
    if (humanLeave) {
        // Leave/drop packets and chat announces only fire for real players.
        NoteHumanSlot(playerIndex);
    }

    // Classify from alliances name: "Nation of… / … Clan" = NPC, account name = human.
    // Controller bytes are remapped to 1 in-match so they alone are not enough.
    if (g_lastName[playerIndex][0]) {
        if (LooksLikeComputerName(g_lastName[playerIndex])) {
            NoteComputerSlot(playerIndex);
        } else if (InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0) != 1) {
            NoteHumanSlot(playerIndex);
        }
    }

    RefreshSlotKinds();
    const LONG kind = InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0);

    if (kind == 1 || IsComputerSlot(playerIndex)) {
        // Computers never "leave" — only army-wipe / eliminate marks them.
        return markComputers && !humanLeave;
    }
    if (!markHumans) return false;
    if (kind == 0) {
        // Known human: leave, drop, disconnect, eliminate and army wipe all count.
        return true;
    }
    // Unknown seat: explicit leave, or wipe/elim after this seat actually had forces.
    if (humanLeave) return true;
    if (!source) return false;
    const bool wipeOrElim =
        _strnicmp(source, "elim", 4) == 0 ||
        _strnicmp(source, "human-elim", 10) == 0 ||
        _strnicmp(source, "hf-poll", 7) == 0 ||
        _strnicmp(source, "status-elim", 11) == 0 ||
        _strnicmp(source, "status-paint", 12) == 0 ||
        _strnicmp(source, "defeat-elim", 11) == 0 ||
        _strnicmp(source, "defeat-poll", 11) == 0 ||
        _strnicmp(source, "defeat-human", 12) == 0 ||
        _strnicmp(source, "wipe-paint", 10) == 0 ||
        _strnicmp(source, "comp-wipe", 9) == 0;
    return wipeOrElim &&
           InterlockedCompareExchange(&g_everHadForces[playerIndex], 0, 0) != 0;
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

uint8_t* FindPattern(uint8_t* base, size_t size, const uint8_t* pat, const char* mask)
{
    const size_t len = strlen(mask);
    if (size < len) return nullptr;
    for (size_t i = 0; i + len <= size; ++i) {
        bool ok = true;
        for (size_t j = 0; j < len; ++j) {
            if (mask[j] == 'x' && base[i + j] != pat[j]) {
                ok = false;
                break;
            }
        }
        if (ok) return base + i;
    }
    return nullptr;
}

const char* MsgTypeName(int msgType)
{
    // Discriminant values observed in handler switch; unknown → numeric in log.
    switch (msgType) {
    case 0x10: return "left";
    default: return nullptr;
    }
}

int PlayerAssetCount(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return -1;
    return static_cast<int>(g_liveAssets[playerIndex]);
}

bool IsExcludedWipeType(int type)
{
    // Same exclusions as HasForces (0x4F4240): not required to stay “alive”.
    return type == 26 || type == 27 || type == 28 || type == 29 ||
           type == 40 || type == 41;
}

bool IsBuildingType(int type)
{
    return type >= kFirstBuildingType;
}

bool LooksLikeUserPtr(const void* p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000u && v < 0x7FFF0000u;
}

bool ReadUnitMeta(void* unit, int* playerOut, int* typeOut, int* flagsOut, void** nextOut)
{
    if (!unit || !LooksLikeUserPtr(unit)) return false;
    __try {
        const auto* b = static_cast<const uint8_t*>(unit);
        *playerOut = static_cast<int>(b[0x27]);
        *typeOut = static_cast<int>(b[0x2c]);
        if (flagsOut) *flagsOut = static_cast<int>(b[0x1e]);
        if (nextOut) {
            *nextOut = *reinterpret_cast<void* const*>(b + 0x68);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void NoteAssets(int playerIndex, int assets)
{
    if (playerIndex < 0 || playerIndex > 7 || assets <= 0) return;
    InterlockedExchange(&g_everHadAssets[playerIndex], 1);
}

int CallHasForces(int playerIndex); // defined below

void NoteHasForcesSample(int playerIndex, int hf)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    const DWORD now = GetTickCount();
    if (hf > 0) {
        InterlockedExchange(&g_everHadForces[playerIndex], 1);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_zeroArmySince[playerIndex]), 0);
        DWORD since = static_cast<DWORD>(InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&g_aliveSince[playerIndex]), 0, 0));
        if (since == 0) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_aliveSince[playerIndex]),
                static_cast<LONG>(now ? now : 1));
            since = now ? now : 1;
        }
        // 8-player starts flicker HasForces; only arm wipe after a long live streak.
        constexpr DWORD kAliveConfirmMs = 8000;
        if ((now - since) >= kAliveConfirmMs) {
            InterlockedExchange(&g_aliveConfirmed[playerIndex], 1);
        }
    } else {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_aliveSince[playerIndex]), 0);
    }
}

// A single alliances row got a new occupant (name changed) — new match or
// reshuffled lobby. The old seat's sticky mark must not paint the newcomer.
void ResetSeatTracking(int i, const char* reason)
{
    if (i < 0 || i > 7) return;
    InterlockedExchange(&g_leftFlags[i], 0);
    InterlockedExchange(&g_explicitGone[i], 0);
    InterlockedExchange(&g_everHadAssets[i], 0);
    InterlockedExchange(&g_everHadForces[i], 0);
    InterlockedExchange(&g_aliveConfirmed[i], 0);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_aliveSince[i]), 0);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_zeroArmySince[i]), 0);
    InterlockedExchange(&g_slotKind[i], -1);
    static LONG s_seatResetLog = 0;
    if (InterlockedIncrement(&s_seatResetLog) <= 24) {
        Log("seat reset p=%d (%s)", i, reason ? reason : "?");
    }
}

void ResetWipeTracking(const char* reason)
{
    for (int i = 0; i < 8; ++i) {
        InterlockedExchange(&g_leftFlags[i], 0);
        InterlockedExchange(&g_explicitGone[i], 0);
        InterlockedExchange(&g_everHadAssets[i], 0);
        InterlockedExchange(&g_everHadForces[i], 0);
        InterlockedExchange(&g_aliveConfirmed[i], 0);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_aliveSince[i]), 0);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_zeroArmySince[i]), 0);
        InterlockedExchange(&g_liveAssets[i], 0);
        InterlockedExchange(&g_units[i], 0);
        InterlockedExchange(&g_buildings[i], 0);
        InterlockedExchange(&g_slotKind[i], -1);
        g_pendingGoneNames[i][0] = 0;
        g_pendingGoneTick[i] = 0;
    }
    // Re-arm lobby classification: controller-based kind latching is gated on
    // !censusDone, so the next lobby must start from a clean census state.
    InterlockedExchange(&g_censusDone, 0);
    static LONG s_resetLog = 0;
    if (InterlockedIncrement(&s_resetLog) <= 12) {
        Log("wipeTracking reset (%s)", reason ? reason : "?");
    }
}

void CacheLiveAssets(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    const LONG total = g_units[playerIndex] + g_buildings[playerIndex];
    InterlockedExchange(&g_liveAssets[playerIndex], total < 0 ? 0 : total);
}

bool CensusFromTypeRows(LONG unitsOut[8], LONG buildingsOut[8])
{
    if (!g_typeCountRows || !LooksLikeUserPtr(g_typeCountRows)) return false;

    for (int i = 0; i < 8; ++i) {
        unitsOut[i] = 0;
        buildingsOut[i] = 0;
    }

    __try {
        for (int t = 0; t < kMaxUnitTypesScan; ++t) {
            if (IsExcludedWipeType(t)) continue;

            // Human/orc pairs often share one row — count each row once.
            uint16_t* row = g_typeCountRows[t];
            if (!row || !LooksLikeUserPtr(row)) continue;
            bool dup = false;
            for (int prev = 0; prev < t; ++prev) {
                if (g_typeCountRows[prev] == row) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            for (int p = 0; p < 8; ++p) {
                const int n = static_cast<int>(row[p]);
                if (n <= 0 || n > 600) continue;
                if (IsBuildingType(t)) buildingsOut[p] += n;
                else unitsOut[p] += n;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Walk per-type unit lists. Strict: type byte must match list index, force bit,
// sane player, no global early-out that skips remaining types.
bool CensusFromUnitLists(void* excludeUnit, LONG unitsOut[8], LONG buildingsOut[8])
{
    for (int i = 0; i < 8; ++i) {
        unitsOut[i] = 0;
        buildingsOut[i] = 0;
    }
    if (!g_unitTypeHeads || !LooksLikeUserPtr(g_unitTypeHeads)) return false;

    for (int t = 0; t < kMaxUnitTypesScan; ++t) {
        if (IsExcludedWipeType(t)) continue;

        void* unit = nullptr;
        __try {
            unit = g_unitTypeHeads[t];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (unit && !LooksLikeUserPtr(unit)) continue;

        void* seenSlow = unit;
        int guard = 0;
        while (unit && guard++ < kMaxPerTypeList) {
            int player = -1;
            int type = -1;
            int flags = 0;
            void* next = nullptr;
            if (!ReadUnitMeta(unit, &player, &type, &flags, &next)) {
                break;
            }
            // Chain must stay on this type — otherwise we followed garbage.
            if (type != t) {
                break;
            }
            const bool isForce = (flags & 0x80) != 0;
            if (unit != excludeUnit && isForce && player >= 0 && player <= 7) {
                if (IsBuildingType(type)) ++buildingsOut[player];
                else ++unitsOut[player];
            }
            if (next && !LooksLikeUserPtr(next)) break;
            unit = next;
            // Floyd cycle break (every other step).
            if ((guard & 1) == 0 && seenSlow) {
                int sp = -1, st = -1, sf = 0;
                void* sn = nullptr;
                if (!ReadUnitMeta(seenSlow, &sp, &st, &sf, &sn)) break;
                seenSlow = sn;
                if (seenSlow == unit) break;
            }
        }
    }
    return true;
}

bool CensusFromWorld(void* excludeUnit, LONG unitsOut[8], LONG buildingsOut[8], bool* usedRowsOut)
{
    if (usedRowsOut) *usedRowsOut = false;
    LONG fromRowsU[8]{};
    LONG fromRowsB[8]{};
    LONG fromListU[8]{};
    LONG fromListB[8]{};
    const bool rowsOk = CensusFromTypeRows(fromRowsU, fromRowsB);
    const bool listOk = CensusFromUnitLists(excludeUnit, fromListU, fromListB);
    if (!rowsOk && !listOk) return false;

    // Prefer type-row tables when they show any real army; else list walk.
    LONG rowTotal = 0, listTotal = 0;
    for (int i = 0; i < 8; ++i) {
        rowTotal += fromRowsU[i] + fromRowsB[i];
        listTotal += fromListU[i] + fromListB[i];
    }

    if (rowsOk && rowTotal > 0) {
        for (int i = 0; i < 8; ++i) {
            unitsOut[i] = fromRowsU[i];
            buildingsOut[i] = fromRowsB[i];
        }
        if (usedRowsOut) *usedRowsOut = true;
        return true;
    }
    if (listOk) {
        for (int i = 0; i < 8; ++i) {
            unitsOut[i] = fromListU[i];
            buildingsOut[i] = fromListB[i];
        }
        return true;
    }
    for (int i = 0; i < 8; ++i) {
        unitsOut[i] = 0;
        buildingsOut[i] = 0;
    }
    return rowsOk || listOk;
}

void ApplyCensus(const LONG unitsIn[8], const LONG buildingsIn[8], const char* reason, bool markDone)
{
    for (int i = 0; i < 8; ++i) {
        InterlockedExchange(&g_units[i], unitsIn[i]);
        InterlockedExchange(&g_buildings[i], buildingsIn[i]);
        CacheLiveAssets(i);
        const LONG total = unitsIn[i] + buildingsIn[i];
        if (total > 0) {
            NoteAssets(i, static_cast<int>(total));
            NoteHasForcesSample(i, CallHasForces(i));
        }
    }
    if (markDone) {
        InterlockedExchange(&g_censusDone, 1);
    }
    static LONG s_censusLog = 0;
    const LONG n = InterlockedIncrement(&s_censusLog);
    if (n <= 6 || (n % 25) == 0) {
        LogHot("census(%s) done=%d u=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld b=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
            reason ? reason : "?", markDone ? 1 : 0,
            unitsIn[0], unitsIn[1], unitsIn[2], unitsIn[3],
            unitsIn[4], unitsIn[5], unitsIn[6], unitsIn[7],
            buildingsIn[0], buildingsIn[1], buildingsIn[2], buildingsIn[3],
            buildingsIn[4], buildingsIn[5], buildingsIn[6], buildingsIn[7]);
    }
}

bool ResyncCensus(void* excludeUnit, const char* reason)
{
    LONG units[8]{};
    LONG buildings[8]{};
    if (!CensusFromWorld(excludeUnit, units, buildings, nullptr)) {
        LogHot("census(%s) FAILED heads=%p", reason ? reason : "?", g_unitTypeHeads);
        return false;
    }
    LONG any = 0;
    for (int i = 0; i < 8; ++i) {
        any += units[i] + buildings[i];
    }
    // Boot may run before map objects exist — keep retrying until we see assets.
    const bool isBoot = reason && strcmp(reason, "boot") == 0;
    if (isBoot && any == 0) {
        LogHot("census(boot) empty — waiting for map");
        return false;
    }
    ApplyCensus(units, buildings, reason, true);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_lastCensusTick),
        static_cast<LONG>(GetTickCount()));
    return true;
}

// Full world walk is expensive in MP — throttle unless forced (first census).
bool ResyncCensusThrottled(void* excludeUnit, const char* reason, bool force)
{
    const DWORD now = GetTickCount();
    const DWORD last = static_cast<DWORD>(InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(&g_lastCensusTick), 0, 0));
    if (!force && g_censusDone && (now - last) < kCensusMinIntervalMs) {
        return true; // keep using cached counters
    }
    return ResyncCensus(excludeUnit, reason);
}

bool AdjustTrackedAsset(int playerIndex, int type, int delta)
{
    if (playerIndex < 0 || playerIndex > 7 || IsExcludedWipeType(type) || delta == 0) {
        return false;
    }
    volatile LONG* bucket =
        IsBuildingType(type) ? &g_buildings[playerIndex] : &g_units[playerIndex];
    LONG next = InterlockedExchangeAdd(bucket, delta) + delta;
    if (next < 0) {
        InterlockedExchange(bucket, 0);
        next = 0;
    }
    CacheLiveAssets(playerIndex);
    if (next > 0 || g_units[playerIndex] + g_buildings[playerIndex] > 0) {
        NoteAssets(playerIndex, 1);
    }
    return true;
}

bool IsLocalPlayer(int playerIndex)
{
    if (!g_localPlayer) return false;
    __try {
        return static_cast<int>(*g_localPlayer) == playerIndex;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void MarkGoneUi(int playerIndex, const char* source); // defined below
int CallHasForces(int playerIndex); // defined below

int CallHasForces(int playerIndex)
{
    if (!g_hasForces || playerIndex < 0 || playerIndex > 7) return -1;
    __try {
        return g_hasForces(playerIndex);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool ShouldKeepGoneDespiteForces(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return false;
    if (InterlockedCompareExchange(&g_explicitGone[playerIndex], 0, 0) != 0) return true;
    // Live computers keep HasForces until wipe; never sticky-keep them on
    // status==2 (that byte is also the computer-controller id).
    if (IsComputerSlot(playerIndex)) return false;
    if (InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0) != 0) return false;
    const uint8_t status = ReadLiveController(playerIndex);
    uint8_t defeat = 0;
    if (g_defeatBase) {
        __try {
            defeat = g_defeatBase[playerIndex];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            defeat = 0;
        }
    }
    // Humans can read HasForces>0 after elim/surrender until they exit the match.
    if (status >= 2 && status != 0xFF) return true;
    if (defeat == 1 || defeat == 2 || defeat == 3) return true;
    return false;
}

void ClearGoneUi(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    if (InterlockedCompareExchange(&g_leftFlags[playerIndex], 0, 0) == 0) return;

    // A human who left/dropped may leave a live army behind — the game
    // announced the leave, so the mark stays no matter what HasForces says.
    if (InterlockedCompareExchange(&g_explicitGone[playerIndex], 0, 0) != 0) {
        return;
    }
    if (ShouldKeepGoneDespiteForces(playerIndex)) {
        return;
    }

    // Undo false start/spawn wipes: game still says this seat has forces.
    // Real peon-wipe / elim leaves HasForces at 0 → stay sticky.
    const int hf = CallHasForces(playerIndex);
    NoteHasForcesSample(playerIndex, hf);
    if (hf > 0) {
        InterlockedExchange(&g_leftFlags[playerIndex], 0);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_zeroArmySince[playerIndex]), 0);
        static LONG s_clearOk = 0;
        if (InterlockedIncrement(&s_clearOk) <= 16) {
            Log("clearGone p=%d hf=%d (still has forces)", playerIndex, hf);
        }
        return;
    }

    static LONG s_clearIgnore = 0;
    if (InterlockedIncrement(&s_clearIgnore) <= 24) {
        Log("clearGone ignored p=%d (sticky mark hf=%d)", playerIndex, hf);
    }
}

void ApplyGoneToUi(int playerIndex, void* ui, const char* name)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    const char* baseName = (name && name[0]) ? name : g_lastName[playerIndex];
    // Space prefix reserves room for the skull icon drawn after SetText.
    if (baseName && baseName[0]) {
        _snprintf_s(g_nameBuf[playerIndex], _TRUNCATE, "%s%s", kGoneNamePrefix, baseName);
    } else {
        _snprintf_s(g_nameBuf[playerIndex], _TRUNCATE, "%s", kGoneNamePrefix);
    }
    if (ui && g_originalSetText) {
        __try {
            ArmSkullDraw(ui, playerIndex);
            g_originalSetText(ui, g_nameBuf[playerIndex], 0x11);
            if (g_skullDrawPending) {
                DisarmSkullDraw();
            }
            auto* colorPtr =
                reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(ui) + kUiColorOffset);
            *colorPtr = kRedTextColor;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void MarkGoneUi(int playerIndex, const char* source)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    if (!ShouldMarkSlot(playerIndex, source)) {
        static LONG s_skip = 0;
        if (InterlockedIncrement(&s_skip) <= 32) {
            Log("gone skip p=%d src=%s computer=%d kind=%ld ctrl=%u markC=%ld markH=%ld",
                playerIndex, source ? source : "?",
                IsComputerSlot(playerIndex) ? 1 : 0,
                InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0),
                ReadLiveController(playerIndex),
                InterlockedCompareExchange(&g_markComputers, 0, 0),
                InterlockedCompareExchange(&g_markHumans, 0, 0));
        }
        return;
    }
    // Explicit leave/drop/announce is sticky — even if the mark itself was
    // already placed earlier by the wipe path.
    if (SourceLooksLikeHumanLeaveOnly(source)) {
        InterlockedExchange(&g_explicitGone[playerIndex], 1);
    }
    if (InterlockedCompareExchange(&g_leftFlags[playerIndex], 1, 0) != 0) {
        return;
    }
    InterlockedIncrement(&g_leaveEventCount);

    // UI flag only for local wipe — do not invent defeat/status (false positives
    // were sticky when our unit counter drifted below the real army size).
    InterlockedExchange(&g_forceRowLog, 1);
    InterlockedExchange(&g_verboseRows, 4);

    Log("gone p=%d src=%s computer=%d kind=%ld flags=1 liveUi=%d name=%s",
        playerIndex, source ? source : "?",
        IsComputerSlot(playerIndex) ? 1 : 0,
        InterlockedCompareExchange(&g_slotKind[playerIndex], 0, 0),
        (g_lastSetTextTick != 0 && (GetTickCount() - g_lastSetTextTick) < 1000) ? 1 : 0,
        g_lastName[playerIndex][0] ? g_lastName[playerIndex] : "?");

    const DWORD now = GetTickCount();
    if (g_lastSetTextTick != 0 && (now - g_lastSetTextTick) < 2000) {
        ApplyGoneToUi(playerIndex, g_lastUi[playerIndex], g_lastName[playerIndex]);
    }
}

void RememberPendingGoneName(const char* name)
{
    if (!name || !name[0]) return;
    const DWORD now = GetTickCount();
    int freeSlot = -1;
    for (int i = 0; i < 8; ++i) {
        if (g_pendingGoneNames[i][0]) {
            if (_stricmp(g_pendingGoneNames[i], name) == 0) return; // already queued
        } else if (freeSlot < 0) {
            freeSlot = i;
        }
    }
    if (freeSlot < 0) freeSlot = 0;
    strncpy_s(g_pendingGoneNames[freeSlot], name, _TRUNCATE);
    g_pendingGoneTick[freeSlot] = now ? now : 1;
    Log("MarkGoneByName: pending '%s' (no alliances row yet)", name);
}

// Called from the alliances SetText bind: if this row's name was announced
// gone before F11 was ever opened, mark it now.
void CheckPendingGoneName(int playerIndex, const char* rawName)
{
    if (playerIndex < 0 || playerIndex > 7 || !rawName || !rawName[0]) return;
    const DWORD now = GetTickCount();
    for (int i = 0; i < 8; ++i) {
        if (!g_pendingGoneNames[i][0]) continue;
        if ((now - g_pendingGoneTick[i]) > 30u * 60u * 1000u) {
            g_pendingGoneNames[i][0] = 0; // stale — from a long-gone match
            continue;
        }
        if (_stricmp(g_pendingGoneNames[i], rawName) != 0) continue;
        g_pendingGoneNames[i][0] = 0;
        MarkGoneUi(playerIndex, "chat-name");
        return;
    }
}

void MarkGoneAndWrite(int playerIndex, const uint8_t* packet, const char* source)
{
    if (playerIndex < 0 || playerIndex > 7) {
        Log("gone ignore p=%d src=%s", playerIndex, source ? source : "?");
        return;
    }

    int msgType = -1;
    const char* typeName = nullptr;
    if (packet) {
        __try {
            msgType = packet[6];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            msgType = -1;
        }
        typeName = MsgTypeName(msgType);
    }

    char srcBuf[48]{};
    if (typeName) {
        sprintf_s(srcBuf, "%s/%s", source ? source : "?", typeName);
    } else if (msgType >= 0) {
        sprintf_s(srcBuf, "%s/t=%d", source ? source : "?", msgType);
    } else {
        sprintf_s(srcBuf, "%s", source ? source : "?");
    }

    // UI flag only — never rewrite status/defeat here (desync risk).
    MarkGoneUi(playerIndex, srcBuf);
}

void RefreshWipeFromAssets()
{
    // Alliances SetText fires often — never full-census every row.
    if (!g_censusDone) {
        ResyncCensusThrottled(nullptr, "ui", /*force=*/false);
        return;
    }
    // Census is bookkeeping only. Marking/unmarking runs in the HasForces poll
    // thread (PollWipeMarks) — the census misses seats on 8p maps (black) and
    // was the source of every false start mark.
    for (int i = 0; i < 8; ++i) {
        CacheLiveAssets(i);
        const LONG total = g_units[i] + g_buildings[i];
        if (total > 0) NoteAssets(i, static_cast<int>(total));
    }
}

// cdecl wrappers for naked gates (right-to-left push order).
void __cdecl MarkGoneFromH1(int playerIndex, const uint8_t* packet)
{
    MarkGoneAndWrite(playerIndex, packet, "H1");
}

void __cdecl MarkGoneFromH2(int playerIndex)
{
    MarkGoneAndWrite(playerIndex, nullptr, "H2");
}

int ReadUnitPlayer(void* unit)
{
    int player = -1;
    int type = -1;
    int flags = 0;
    if (!ReadUnitMeta(unit, &player, &type, &flags, nullptr)) return -1;
    if (player < 0 || player > 7) return -1;
    return player;
}

void __cdecl Hook_UnitGained(void* unit)
{
    if (g_originalUnitGained) {
        g_originalUnitGained(unit);
    }
    if (!unit) return;

    int playerIndex = -1;
    int type = -1;
    int flags = 0;
    if (!ReadUnitMeta(unit, &playerIndex, &type, &flags, nullptr)) return;
    if (playerIndex < 0 || playerIndex > 7) return;

    // Do not ±1 during map-spawn before the initial census snapshot.
    if (!g_censusDone) {
        static LONG s_preLog = 0;
        if (InterlockedIncrement(&s_preLog) <= 8) {
            LogHot("unitGain(pre-census) p=%d type=%d flags=0x%02X excl=%d",
                playerIndex, type, flags & 0xFF, IsExcludedWipeType(type) ? 1 : 0);
        }
        return;
    }

    if ((flags & 0x80) != 0) {
        AdjustTrackedAsset(playerIndex, type, +1);
    }
    if (g_units[playerIndex] + g_buildings[playerIndex] > 0) {
        ClearGoneUi(playerIndex);
    }

    static LONG s_gainLog = 0;
    if (InterlockedIncrement(&s_gainLog) <= 12) {
        LogHot("unitGain p=%d type=%d u=%ld b=%ld total=%ld excl=%d",
            playerIndex, type, g_units[playerIndex], g_buildings[playerIndex],
            g_liveAssets[playerIndex], IsExcludedWipeType(type) ? 1 : 0);
    }
}

void __cdecl Hook_UnitLost(void* unit)
{
    if (g_originalUnitLost) {
        g_originalUnitLost(unit);
    }
    if (!g_enabled || !unit) return;

    int playerIndex = -1;
    int type = -1;
    int flags = 0;
    if (!ReadUnitMeta(unit, &playerIndex, &type, &flags, nullptr)) return;
    if (playerIndex < 0 || playerIndex > 7) return;

    // Cheap path: ±1 like gain. Full world census only on first snapshot,
    // when counters hit 0 (possible wipe), or every kCensusMinIntervalMs.
    if (!g_censusDone) {
        ResyncCensus(unit, "lost-init");
    } else {
        if ((flags & 0x80) != 0) {
            AdjustTrackedAsset(playerIndex, type, -1);
        }
        const bool maybeWipe =
            (g_units[playerIndex] + g_buildings[playerIndex]) <= 0;
        ResyncCensusThrottled(unit, "lost", /*force=*/maybeWipe);
    }

    static LONG s_wipeLog = 0;
    if (InterlockedIncrement(&s_wipeLog) <= 16) {
        const int hf = CallHasForces(playerIndex);
        LogHot("unitLost p=%d type=%d flags=0x%02X u=%ld b=%ld hf=%d ever=%ld name=%s",
            playerIndex, type, flags & 0xFF,
            g_units[playerIndex], g_buildings[playerIndex], hf,
            g_everHadAssets[playerIndex],
            g_lastName[playerIndex][0] ? g_lastName[playerIndex] : "?");
    }
    // Marking happens in PollWipeMarks (HasForces poll) — not from census here.
}

void __cdecl Hook_AnnounceGone(int playerIndex)
{
    // Chat announce for left / dropped / eliminated — fire before original so
    // the sticky UI flag is set even if Remastered resets status back to 1.
    Log("announce call p=%d", playerIndex);
    if (playerIndex >= 0 && playerIndex <= 7) {
        MarkGoneUi(playerIndex, "announce");
    }
    if (g_originalAnnounceGone) {
        g_originalAnnounceGone(playerIndex);
    }
}

void __cdecl Hook_Eliminate(int playerIndex)
{
    if (playerIndex >= 0 && playerIndex <= 7 && !IsLocalPlayer(playerIndex)) {
        Log("elim call p=%d name=%s", playerIndex,
            g_lastName[playerIndex][0] ? g_lastName[playerIndex] : "?");
        NoteAssets(playerIndex, 1);
        InterlockedExchange(&g_everHadForces[playerIndex], 1);
        InterlockedExchange(&g_aliveConfirmed[playerIndex], 1);
        MarkGoneUi(playerIndex, "elim");
    }
    if (g_originalEliminate) {
        g_originalEliminate(playerIndex);
    }
}

void BindTypeCountTable(uint8_t* moduleBase, uintptr_t imageBase)
{
    if (!moduleBase || imageBase == 0) return;
    g_unitTypeHeads = reinterpret_cast<void**>(
        moduleBase + (kPreferredUnitTypeHeads - imageBase));
    g_typeCountRows = reinterpret_cast<uint16_t**>(
        moduleBase + (kPreferredTypeCountRows - imageBase));
    Log("BindUnitLists: heads=%p rows=%p module=%p imageBase=0x%08X",
        g_unitTypeHeads, g_typeCountRows, moduleBase, (unsigned)imageBase);
}

void BindStatusBase(uint8_t* statusBase)
{
    g_statusBase = statusBase;
    g_defeatBase = statusBase ? (statusBase + kDefeatFromStatus) : nullptr;
    // Prefer slide from the live status pointer — GetModuleHandle can disagree
    // with relocated absolute immediates on some launches.
    if (statusBase) {
        const uintptr_t slide =
            reinterpret_cast<uintptr_t>(statusBase) - kPreferredStatus;
        g_unitTypeHeads =
            reinterpret_cast<void**>(kPreferredUnitTypeHeads + slide);
        g_typeCountRows =
            reinterpret_cast<uint16_t**>(kPreferredTypeCountRows + slide);
        g_slotBase = reinterpret_cast<uint8_t*>(kPreferredSlotBase + slide);
        g_playerName0 = reinterpret_cast<char*>(kPreferredPlayerName0 + slide);
        Log("BindUnitLists(via status): heads=%p rows=%p slot=%p names=%p status=%p slide=0x%08X",
            g_unitTypeHeads, g_typeCountRows, g_slotBase, g_playerName0, statusBase, (unsigned)slide);
    }
}

bool PlayerInactive(int playerIndex, const char* name)
{
    (void)name;
    if (playerIndex < 0 || playerIndex > 7) return false;
    if (IsLocalPlayer(playerIndex)) return false;
    if (g_leftFlags[playerIndex] != 0) return true;
    if (!g_enabled) return false;

    uint8_t status = 0xFF;
    uint8_t defeat = 0;
    if (g_statusBase) {
        __try {
            status = g_statusBase[playerIndex];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            status = 0xFF;
        }
    }
    if (g_defeatBase) {
        __try {
            defeat = g_defeatBase[playerIndex];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            defeat = 0;
        }
    }
    const int hf = CallHasForces(playerIndex);

    // Seat participated this match — skip empty lobby slots / join garbage.
    const bool played = InterlockedCompareExchange(&g_everHadForces[playerIndex], 0, 0) != 0 ||
                        InterlockedCompareExchange(&g_aliveConfirmed[playerIndex], 0, 0) != 0 ||
                        InterlockedCompareExchange(&g_everHadAssets[playerIndex], 0, 0) != 0 ||
                        IsComputerSlot(playerIndex) ||
                        g_lastName[playerIndex][0] != 0;

    if (!played) return false;

    const bool computer = IsComputerSlot(playerIndex);
    if (computer) {
        // Live AI can read status==2 (computer controller). Only mark after hf==0.
        if (hf > 0) return false;
        if (status >= 2 && status != 0xFF) {
            return ShouldMarkSlot(playerIndex, "status-paint");
        }
        if (defeat == 2 || defeat == 3) {
            return ShouldMarkSlot(playerIndex, "defeat-paint");
        }
        if (InterlockedCompareExchange(&g_everHadForces[playerIndex], 0, 0) != 0) {
            return ShouldMarkSlot(playerIndex, "comp-wipe-paint");
        }
        return false;
    }

    // Humans: defeat/status even while HasForces still reads live (until Exit Game).
    if (status >= 2 && status != 0xFF) {
        return ShouldMarkSlot(playerIndex, "status-paint");
    }
    if (defeat == 2 || defeat == 3) {
        return ShouldMarkSlot(playerIndex, "defeat-paint");
    }
    if (defeat == 1) {
        return ShouldMarkSlot(playerIndex, "defeat-left");
    }
    if (hf > 0) return false;
    if (InterlockedCompareExchange(&g_everHadForces[playerIndex], 0, 0) != 0) {
        return ShouldMarkSlot(playerIndex, "wipe-paint");
    }
    return false;
}

void __cdecl Hook_SetText_Impl(void* ui, const char* name, int prop, int playerIndex)
{
    InterlockedIncrement(&g_hitCount);
    g_lastSetTextTick = GetTickCount();

    // Throttle wipe refresh — SetText can fire many times per alliances paint.
    static DWORD s_lastRefresh = 0;
    const DWORD now = GetTickCount();
    if ((now - s_lastRefresh) >= 200) {
        s_lastRefresh = now;
        RefreshSlotKinds();
        RefreshWipeFromAssets();
    }

    const uint8_t status =
        (g_statusBase && playerIndex >= 0 && playerIndex <= 7) ? g_statusBase[playerIndex] : 0xFF;
    uint8_t defeat = 0xFF;
    if (g_defeatBase && playerIndex >= 0 && playerIndex <= 7) {
        __try {
            defeat = g_defeatBase[playerIndex];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            defeat = 0xFE;
        }
    }

    const int assets = PlayerAssetCount(playerIndex);
    // Never clear leftFlags here: wipe/leave marks must stick for the rest of
    // the match even while status stays 1 (Remastered peon-wipe behavior).

    if (playerIndex >= 0 && playerIndex <= 7) {
        g_lastUi[playerIndex] = ui;
        if (name && name[0]) {
            const char* raw = NameForClassify(name);
            // Different occupant on this row than last time → new match; the
            // previous occupant's sticky mark must not carry over (a comp
            // was shown [X] at match start after a fast rematch).
            const bool nameChanged = _stricmp(g_lastName[playerIndex], raw) != 0;
            if (g_lastName[playerIndex][0] && nameChanged) {
                ResetSeatTracking(playerIndex, "name-change");
            }
            if (nameChanged) {
                static LONG s_bindLog = 0;
                if (InterlockedIncrement(&s_bindLog) <= 64) {
                    Log("rowBind p=%d name=%s st=%u df=%u hf=%d",
                        playerIndex, raw, status, defeat, CallHasForces(playerIndex));
                }
            }
            _snprintf_s(g_lastName[playerIndex], _TRUNCATE, "%s", raw);
            if (LooksLikeComputerName(raw)) NoteComputerSlot(playerIndex);
            else NoteHumanSlot(playerIndex);
            // Leave announced before this row was ever painted? Mark it now.
            CheckPendingGoneName(playerIndex, raw);
        }
    }

    const LONG leftFlag =
        (playerIndex >= 0 && playerIndex <= 7) ? g_leftFlags[playerIndex] : 0;
    const bool inactive = g_enabled && PlayerInactive(playerIndex, name);
    const char* useName = name;
    uint32_t* colorPtr = nullptr;

    if (InterlockedCompareExchange(&g_forceRowLog, 0, 1) == 1) {
        for (int i = 0; i < 8; ++i) {
            uint8_t s = g_statusBase ? g_statusBase[i] : 0xFF;
            uint8_t d = 0xFF;
            if (g_defeatBase) {
                __try { d = g_defeatBase[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { d = 0xFE; }
            }
            LogHot("postGone snap[%d] status=%u defeat=%u left=%ld assets=%d",
                i, s, d, g_leftFlags[i], PlayerAssetCount(i));
        }
    }

    static LONG s_snapshot = 0;
    if (InterlockedIncrement(&s_snapshot) <= 3) {
        for (int i = 0; i < 8; ++i) {
            uint8_t s = g_statusBase ? g_statusBase[i] : 0xFF;
            uint8_t d = 0xFF;
            if (g_defeatBase) {
                __try { d = g_defeatBase[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { d = 0xFE; }
            }
            LogHot("snap[%d] status=%u defeat=%u left=%ld assets=%d ever=%ld",
                i, s, d, g_leftFlags[i], PlayerAssetCount(i), g_everHadAssets[i]);
        }
    }

    LONG verboseLeft = InterlockedExchangeAdd(&g_verboseRows, 0);
    if (verboseLeft > 0) InterlockedDecrement(&g_verboseRows);
    static LONG s_rowLog = 0;
    const LONG rowN = InterlockedIncrement(&s_rowLog);
    const bool logRow = inactive || verboseLeft > 0 || rowN <= 8 || (rowN % 500) == 0;
    if (logRow) {
        LogHot("row p=%d status=%u defeat=%u left=%ld assets=%d enabled=%ld inactive=%d name=%s",
            playerIndex, status, defeat, leftFlag, assets, g_enabled,
            inactive ? 1 : 0, name ? name : "(null)");
    }

    if (inactive) {
        InterlockedIncrement(&g_recolorCount);
        if (playerIndex >= 0 && playerIndex <= 7)
            InterlockedExchange(&g_leftFlags[playerIndex], 1);
        if (name) {
            const char* base = NameForClassify(name);
            _snprintf_s(g_nameBuf[playerIndex], _TRUNCATE, "%s%s", kGoneNamePrefix, base);
            useName = g_nameBuf[playerIndex];
        } else {
            _snprintf_s(g_nameBuf[playerIndex >= 0 && playerIndex <= 7 ? playerIndex : 0],
                _TRUNCATE, "%s", kGoneNamePrefix);
            useName = g_nameBuf[playerIndex >= 0 && playerIndex <= 7 ? playerIndex : 0];
        }
        if (ui) {
            colorPtr = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ui) + kUiColorOffset);
            *colorPtr = kRedTextColor;
        }
    }

    if (inactive && ui) {
        ArmSkullDraw(ui, playerIndex);
    }
    g_originalSetText(ui, useName, prop);
    if (g_skullDrawPending) {
        static LONG s_miss = 0;
        if (InterlockedIncrement(&s_miss) <= 12) {
            Log("gone icon: RenderLabel missed p=%d name=%s", playerIndex, name ? name : "?");
        }
        DisarmSkullDraw();
    }

    if (inactive && colorPtr) {
        *colorPtr = kRedTextColor;
    }
}

// At the patched call site, EDI is the alliances row player index (0..7).
// Also pass stack leftover as fallback; prefer EDI.
void __declspec(naked) Hook_SetText_Gate()
{
    __asm {
        push ebp
        mov ebp, esp
        push edi
        push dword ptr [ebp + 0x10]
        push dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x08]
        call Hook_SetText_Impl
        add esp, 16
        pop ebp
        ret
    }
}

// After movzx eax, [esi+1]: eax = player. Trampoline calls us then runs the
// original status store (see PatchLeaveNotify).
void __declspec(naked) Hook_LeaveWrite_H1_Gate()
{
    __asm {
        pushad
        push esi
        push eax
        call MarkGoneFromH1
        add esp, 8
        popad
        ret
    }
}

// ecx = player index (secondary helper). Next instruction still writes the slot array.
void __declspec(naked) Hook_LeaveWrite_H2_Gate()
{
    __asm {
        pushad
        push ecx
        call MarkGoneFromH2
        add esp, 4
        popad
        ret
    }
}

// Replace a 7-byte status store with: call notifyGate ; [orig 7 bytes] via tramp.
// Critical: the game's original store MUST still run after our UI notify.
bool PatchLeaveNotify(uint8_t* site, void* gate, uint8_t* savedOrig, void** trampOut)
{
    memcpy(savedOrig, site, 7);
    void* tramp = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return false;

    auto* t = static_cast<uint8_t*>(tramp);
    // call gate (relative)
    t[0] = 0xE8;
    *reinterpret_cast<int32_t*>(t + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(gate) - (t + 5));
    // original 7-byte store
    memcpy(t + 5, site, 7);
    // jmp site+7
    t[12] = 0xE9;
    *reinterpret_cast<int32_t*>(t + 13) =
        static_cast<int32_t>((site + 7) - (t + 17));

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return false;
    }

    // jmp tramp ; nop ; nop
    site[0] = 0xE9;
    *reinterpret_cast<int32_t*>(site + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(tramp) - (site + 5));
    site[5] = 0x90;
    site[6] = 0x90;

    VirtualProtect(site, 7, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 7);
    *trampOut = tramp;
    return true;
}

bool PatchCall7(uint8_t* site, void* gate, uint8_t* savedOrig, void** trampOut)
{
    // Legacy name — leave hooks use PatchLeaveNotify so the original write runs.
    return PatchLeaveNotify(site, gate, savedOrig, trampOut);
}

bool PatchFuncPrologue(uint8_t* site, void* hook, uint8_t* savedOrig, void** trampOut, void** originalOut)
{
    // Copy 7-byte prologue to trampoline, then jmp back to site+7.
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

bool InstallRenderLabelHook(uint8_t* base, size_t imageSize, uintptr_t imageBase)
{
    (void)imageSize;
    uint8_t* site = base + (kPreferredRenderLabel - imageBase);
    if (!IsLikelyCode(site, 7)) {
        Log("InstallRenderLabel: site unreadable");
        return false;
    }
    void* original = nullptr;
    if (!PatchFuncPrologue(site, &Hook_RenderLabel, g_origRenderLabel, &g_renderLabelTramp, &original)) {
        Log("InstallRenderLabel: patch failed (%lu)", GetLastError());
        return false;
    }
    g_renderLabelSite = site;
    g_originalRenderLabel = reinterpret_cast<RenderLabelFn>(original);
    Log("InstallRenderLabel: ok site=%p", site);
    return true;
}

void Unpatch7(uint8_t* site, const uint8_t* savedOrig, void** tramp)
{
    if (!site) return;
    DWORD oldProtect = 0;
    if (VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(site, savedOrig, 7);
        VirtualProtect(site, 7, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), site, 7);
    }
    if (tramp && *tramp) {
        VirtualFree(*tramp, 0, MEM_RELEASE);
        *tramp = nullptr;
    }
}

bool InstallLeaveWriteHooks(uint8_t* base, size_t imageSize)
{
    // movzx eax, byte [esi+1] ; mov byte [eax+imm32], 3
    const uint8_t patH1[] = {
        0x0F, 0xB6, 0x46, 0x01,
        0xC6, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03
    };
    const char* maskH1 = "xxxxxx????x";

    // mov byte [ecx+imm32], 3 ; mov byte [eax+imm32], 3
    // The second imm is the slot table (preferred 0x916268). Relocated builds
    // shift the absolute address, so mask it and match on the low word only.
    const uint8_t patH2[] = {
        0xC6, 0x81, 0x00, 0x00, 0x00, 0x00, 0x03,
        0xC6, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03
    };
    const char* maskH2 = "xx????xxx????x";

    uint8_t* hit1 = FindPattern(base, imageSize, patH1, maskH1);

    uint8_t* hit2 = nullptr;
    for (uint8_t* cursor = base;;) {
        const size_t remaining = imageSize - static_cast<size_t>(cursor - base);
        uint8_t* candidate = FindPattern(cursor, remaining, patH2, maskH2);
        if (!candidate) break;
        const uint32_t dispSlot = *reinterpret_cast<const uint32_t*>(candidate + 9);
        if ((dispSlot & 0xFFFF) == 0x6268) {
            hit2 = candidate;
            break;
        }
        cursor = candidate + 1;
    }

    bool ok = true;
    if (hit1 && IsLikelyCode(hit1, sizeof(patH1))) {
        const uint32_t statusImm = *reinterpret_cast<uint32_t*>(hit1 + 6);
        if (!g_statusBase) {
            BindStatusBase(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(statusImm)));
        }
        g_leaveWriteH1 = hit1 + 4; // C6 80 ...
        if (!PatchCall7(g_leaveWriteH1, &Hook_LeaveWrite_H1_Gate, g_origLeaveH1, &g_leaveTrampH1)) {
            Log("InstallLeaveWrite: H1 patch failed (%lu)", GetLastError());
            g_leaveWriteH1 = nullptr;
            ok = false;
        } else {
            Log("InstallLeaveWrite: H1 ok site=%p", g_leaveWriteH1);
        }
    } else {
        Log("InstallLeaveWrite: H1 pattern not found");
        ok = false;
    }

    if (hit2 && IsLikelyCode(hit2, sizeof(patH2))) {
        g_leaveWriteH2 = hit2; // C6 81 ...
        if (!PatchCall7(g_leaveWriteH2, &Hook_LeaveWrite_H2_Gate, g_origLeaveH2, &g_leaveTrampH2)) {
            Log("InstallLeaveWrite: H2 patch failed (%lu)", GetLastError());
            g_leaveWriteH2 = nullptr;
            ok = false;
        } else {
            Log("InstallLeaveWrite: H2 ok site=%p", g_leaveWriteH2);
        }
    } else {
        // H2 is optional (eliminate helper); H1 alone is enough for leave packets.
        Log("InstallLeaveWrite: H2 pattern not found (optional)");
    }

    return g_leaveWriteH1 != nullptr || g_leaveWriteH2 != nullptr;
}

bool InstallAnnounceGoneHook(uint8_t* base, size_t imageSize)
{
    // MUST match leave/drop/elim announce @ 0x4F4F30 (writes defeat=2).
    // A near-twin at 0x4F43A0 writes defeat=1 and runs during normal play —
    // hooking that twin caused false "player left" / MP drops.
    //
    // push ebp; mov ebp,esp; push esi; mov esi,[ebp+8]
    // cmp byte [esi+defeat],0 ; jnz +0x19 ; mov byte [esi+defeat],2
    const uint8_t pat[] = {
        0x55,
        0x8B, 0xEC,
        0x56,
        0x8B, 0x75, 0x08,
        0x80, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x75, 0x19,
        0xC6, 0x86, 0x00, 0x00, 0x00, 0x00, 0x02
    };
    const char* mask = "xxxxxxxxx????xxx????x";

    uint8_t* hit = FindPattern(base, imageSize, pat, mask);
    if (!hit || !IsLikelyCode(hit, sizeof(pat))) {
        Log("InstallAnnounceGone: pattern not found (safe skip — chat leave still works)");
        return false;
    }
    const uint32_t dispCmp = *reinterpret_cast<uint32_t*>(hit + 9);
    const uint32_t dispMov = *reinterpret_cast<uint32_t*>(hit + 18);
    if ((dispCmp & 0xFFFF) != 0xAA84 || dispCmp != dispMov) {
        Log("InstallAnnounceGone: unexpected defeat disp cmp=0x%08X mov=0x%08X at %p",
            dispCmp, dispMov, hit);
        return false;
    }
    void* original = nullptr;
    if (!PatchFuncPrologue(hit, &Hook_AnnounceGone, g_origAnnounceGone, &g_announceGoneTramp, &original)) {
        Log("InstallAnnounceGone: patch failed (%lu)", GetLastError());
        return false;
    }
    g_announceGoneSite = hit;
    g_originalAnnounceGone = reinterpret_cast<AnnounceGoneFn>(original);
    Log("InstallAnnounceGone: ok site=%p tramp=%p (defeat=2 leave path)",
        g_announceGoneSite, g_announceGoneTramp);
    return true;
}

bool InstallHasForces(uint8_t* base, size_t imageSize)
{
    // push ebp; mov ebp,esp; mov edx,[ebp+8]; mov ax,word [edx*2+imm32]
    const uint8_t pat[] = {
        0x55,
        0x8B, 0xEC,
        0x8B, 0x55, 0x08,
        0x66, 0x8B, 0x04, 0x55, 0x00, 0x00, 0x00, 0x00
    };
    const char* mask = "xxxxxxxxxx????";

    uint8_t* hit = FindPattern(base, imageSize, pat, mask);
    if (!hit || !IsLikelyCode(hit, sizeof(pat))) {
        Log("InstallHasForces: pattern not found");
        return false;
    }
    // Confirm disp is the buildings table (preferred 0x91B38C) after slide.
    const uint32_t disp = *reinterpret_cast<uint32_t*>(hit + 10);
    if ((disp & 0xFFFF) != 0xB38C && (disp & 0xFFFF) != 0x38C) {
        // Still accept if nearby: low word B38C in preferred image.
        if (disp != 0x0091B38C && (disp % 0x10000) != 0xB38C) {
            Log("InstallHasForces: unexpected disp=0x%08X at %p", disp, hit);
            // Continue anyway — pattern is unique enough in this build.
        }
    }
    g_hasForces = reinterpret_cast<HasForcesFn>(hit);
    Log("InstallHasForces: ok fn=%p disp=0x%08X", g_hasForces, disp);
    return true;
}

void BindLocalPlayer(uint8_t* moduleBase, uintptr_t imageBase)
{
    if (!moduleBase || imageBase == 0) return;
    constexpr uintptr_t kPreferredLocal = 0x00918CCD;
    g_localPlayer = moduleBase + (kPreferredLocal - imageBase);
    g_playerName0 = reinterpret_cast<char*>(moduleBase + (kPreferredPlayerName0 - imageBase));
    Log("BindLocalPlayer: local=%p names=%p", g_localPlayer, g_playerName0);
}

bool InstallUnitCountHooks(uint8_t* base, size_t imageSize)
{
    // Gain: push ebp; mov ebp,esp; push esi; mov esi,[ebp+8]; mov eax,0x100; test [esi+0x1c],ax
    const uint8_t patGain[] = {
        0x55,
        0x8B, 0xEC,
        0x56,
        0x8B, 0x75, 0x08,
        0xB8, 0x00, 0x01, 0x00, 0x00,
        0x66, 0x85, 0x46, 0x1C,
        0x74, 0x1F,
        0x66, 0xFF, 0x05
    };
    const char* maskGain = "xxxxxxxxxxxxxxxxxxxxx";

    // Lost: same prologue but loads edx=0xFFFF before the test, then add [global],dx
    const uint8_t patLost[] = {
        0x55,
        0x8B, 0xEC,
        0x56,
        0x8B, 0x75, 0x08,
        0xB8, 0x00, 0x01, 0x00, 0x00,
        0xBA, 0xFF, 0xFF, 0x00, 0x00,
        0x66, 0x85, 0x46, 0x1C
    };
    const char* maskLost = "xxxxxxxxxxxxxxxxxxxxx";

    bool ok = true;
    uint8_t* hitGain = FindPattern(base, imageSize, patGain, maskGain);
    if (!hitGain || !IsLikelyCode(hitGain, sizeof(patGain))) {
        Log("InstallUnitGain: pattern not found");
        ok = false;
    } else {
        void* original = nullptr;
        if (!PatchFuncPrologue(hitGain, &Hook_UnitGained, g_origUnitGained, &g_unitGainedTramp, &original)) {
            Log("InstallUnitGain: patch failed (%lu)", GetLastError());
            ok = false;
        } else {
            g_unitGainedSite = hitGain;
            g_originalUnitGained = reinterpret_cast<UnitCountFn>(original);
            Log("InstallUnitGain: ok site=%p tramp=%p", g_unitGainedSite, g_unitGainedTramp);
        }
    }

    uint8_t* hitLost = FindPattern(base, imageSize, patLost, maskLost);
    if (!hitLost || !IsLikelyCode(hitLost, sizeof(patLost))) {
        Log("InstallUnitLost: pattern not found");
        ok = false;
    } else {
        void* original = nullptr;
        if (!PatchFuncPrologue(hitLost, &Hook_UnitLost, g_origUnitLost, &g_unitLostTramp, &original)) {
            Log("InstallUnitLost: patch failed (%lu)", GetLastError());
            ok = false;
        } else {
            g_unitLostSite = hitLost;
            g_originalUnitLost = reinterpret_cast<UnitCountFn>(original);
            Log("InstallUnitLost: ok site=%p tramp=%p", g_unitLostSite, g_unitLostTramp);
        }
    }

    return ok;
}

bool InstallEliminateHook(uint8_t* base, size_t imageSize)
{
    // push ebp; mov ebp,esp; push ebx; mov bl,[ebp+8]; movzx ecx,bl; imul eax,ecx,0x26
    const uint8_t pat[] = {
        0x55,
        0x8B, 0xEC,
        0x53,
        0x8A, 0x5D, 0x08,
        0x0F, 0xB6, 0xCB,
        0x6B, 0xC1, 0x26
    };
    const char* mask = "xxxxxxxxxxxxx";

    uint8_t* hit = FindPattern(base, imageSize, pat, mask);
    if (!hit || !IsLikelyCode(hit, sizeof(pat))) {
        Log("InstallEliminate: pattern not found");
        return false;
    }
    void* original = nullptr;
    if (!PatchFuncPrologue(hit, &Hook_Eliminate, g_origEliminate, &g_eliminateTramp, &original)) {
        Log("InstallEliminate: patch failed (%lu)", GetLastError());
        return false;
    }
    g_eliminateSite = hit;
    g_originalEliminate = reinterpret_cast<EliminateFn>(original);
    Log("InstallEliminate: ok site=%p tramp=%p", g_eliminateSite, g_eliminateTramp);
    return true;
}

bool InstallSetTextHook(uint8_t* base, size_t imageSize)
{
    // push dword [imm32] ; call rel32 ; add esp,10 ; cmp byte [edi+imm32],1 ; jnz +0x0E
    const uint8_t pat[] = {
        0xFF, 0x35, 0x00, 0x00, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x83, 0xC4, 0x10,
        0x80, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x75, 0x0E
    };
    const char* mask = "xx????x????xxxxx????xxx";

    uint8_t* hit = FindPattern(base, imageSize, pat, mask);
    if (!hit || !IsLikelyCode(hit, sizeof(pat))) {
        Log("InstallSetText: pattern not found (imageSize=%u)", (unsigned)imageSize);
        return false;
    }

    const uint32_t statusImm = *reinterpret_cast<uint32_t*>(hit + 16);
    BindStatusBase(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(statusImm)));

    g_patchSite = hit + 6; // E8
    memcpy(g_originalCall, g_patchSite, 5);

    const int32_t rel = *reinterpret_cast<int32_t*>(g_patchSite + 1);
    g_originalSetText = reinterpret_cast<SetTextFn>(g_patchSite + 5 + rel);

    g_trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_trampoline) {
        Log("InstallSetText: VirtualAlloc failed (%lu)", GetLastError());
        return false;
    }

    auto* t = static_cast<uint8_t*>(g_trampoline);
    t[0] = 0xE9;
    *reinterpret_cast<int32_t*>(t + 1) =
        static_cast<int32_t>(reinterpret_cast<uint8_t*>(&Hook_SetText_Gate) - (t + 5));

    DWORD oldProtect = 0;
    if (!VirtualProtect(g_patchSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("InstallSetText: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }

    const int32_t newRel = static_cast<int32_t>(t - (g_patchSite + 5));
    g_patchSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(g_patchSite + 1) = newRel;

    VirtualProtect(g_patchSite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), g_patchSite, 5);
    Log("InstallSetText: ok site=%p statusBase=%p defeatBase=%p original=%p",
        g_patchSite, g_statusBase, g_defeatBase, g_originalSetText);
    return true;
}

bool InstallHook()
{
    HMODULE game = GetModuleHandleW(L"Warcraft II.exe");
    if (!game) game = GetModuleHandleW(nullptr);
    if (!game) {
        Log("InstallHook: no module handle");
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    auto* base = reinterpret_cast<uint8_t*>(game);
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    const uintptr_t imageBase = nt->OptionalHeader.ImageBase;

    BindTypeCountTable(base, imageBase);
    BindLocalPlayer(base, imageBase);
    BindUiDrawApis(base, imageBase);

    // Leave-write hooks first so g_statusBase may already be known; set-text is required.
    const bool leaveOk = InstallLeaveWriteHooks(base, imageSize);
    const bool announceOk = InstallAnnounceGoneHook(base, imageSize);
    const bool forcesOk = InstallHasForces(base, imageSize);
    const bool wipeOk = InstallUnitCountHooks(base, imageSize);
    const bool elimOk = InstallEliminateHook(base, imageSize);
    const bool labelOk = InstallRenderLabelHook(base, imageSize, imageBase);
    if (!InstallSetTextHook(base, imageSize)) {
        return false;
    }

    // Re-bind local player / unit lists via status slide if module base was wrong.
    if (g_statusBase) {
        const uintptr_t slide =
            reinterpret_cast<uintptr_t>(g_statusBase) - kPreferredStatus;
        g_localPlayer = reinterpret_cast<uint8_t*>(0x00918CCD + slide);
        g_unitTypeHeads =
            reinterpret_cast<void**>(kPreferredUnitTypeHeads + slide);
        g_typeCountRows =
            reinterpret_cast<uint16_t**>(kPreferredTypeCountRows + slide);
        g_slotBase = reinterpret_cast<uint8_t*>(kPreferredSlotBase + slide);
        g_playerName0 = reinterpret_cast<char*>(kPreferredPlayerName0 + slide);
    }

    LoadMarkModesFromJson();
    RefreshSlotKinds();

    InterlockedExchange(&g_ready, 1);
    // DLL is only injected when the Extra feature is on — start enabled so a missed
    // SetEnabled remote-thread call cannot leave the hook silently inert.
    InterlockedExchange(&g_enabled, 1);
    // Quiet hot-path file I/O in MP (unitGain/lost/census/row). Gone/install still Log().
    InterlockedExchange(&g_logQuiet, 1);
    Log("InstallHook: ready enabled=1 leaveHooks=%d announce=%d wipeHook=%d elim=%d hasForces=%d label=%d local=%p heads=%p rows=%p slot=%p markC=%ld markH=%ld quiet=1",
        leaveOk ? 1 : 0, announceOk ? 1 : 0, wipeOk ? 1 : 0, elimOk ? 1 : 0, forcesOk ? 1 : 0,
        labelOk ? 1 : 0,
        g_localPlayer, g_unitTypeHeads, g_typeCountRows, g_slotBase,
        InterlockedCompareExchange(&g_markComputers, 0, 0),
        InterlockedCompareExchange(&g_markHumans, 0, 0));
    return true;
}

void RemoveHook()
{
    Unpatch7(g_leaveWriteH1, g_origLeaveH1, &g_leaveTrampH1);
    g_leaveWriteH1 = nullptr;
    Unpatch7(g_leaveWriteH2, g_origLeaveH2, &g_leaveTrampH2);
    g_leaveWriteH2 = nullptr;
    Unpatch7(g_unitGainedSite, g_origUnitGained, &g_unitGainedTramp);
    g_unitGainedSite = nullptr;
    g_originalUnitGained = nullptr;
    Unpatch7(g_unitLostSite, g_origUnitLost, &g_unitLostTramp);
    g_unitLostSite = nullptr;
    g_originalUnitLost = nullptr;
    Unpatch7(g_eliminateSite, g_origEliminate, &g_eliminateTramp);
    g_eliminateSite = nullptr;
    g_originalEliminate = nullptr;
    Unpatch7(g_announceGoneSite, g_origAnnounceGone, &g_announceGoneTramp);
    g_announceGoneSite = nullptr;
    g_originalAnnounceGone = nullptr;
    Unpatch7(g_renderLabelSite, g_origRenderLabel, &g_renderLabelTramp);
    g_renderLabelSite = nullptr;
    g_originalRenderLabel = nullptr;
    InterlockedExchange(&g_goneSkullResolved, 0);
    g_goneSkullImage = NkImage{};

    if (g_patchSite) {
        DWORD oldProtect = 0;
        if (VirtualProtect(g_patchSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_patchSite, g_originalCall, 5);
            VirtualProtect(g_patchSite, 5, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), g_patchSite, 5);
        }
        g_patchSite = nullptr;
    }
    if (g_trampoline) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
    }
    g_originalSetText = nullptr;
    g_statusBase = nullptr;
    g_defeatBase = nullptr;
    g_slotBase = nullptr;
    g_unitTypeHeads = nullptr;
    g_typeCountRows = nullptr;
    InterlockedExchange(&g_censusDone, 0);
    for (int i = 0; i < 8; ++i) {
        InterlockedExchange(&g_leftFlags[i], 0);
        InterlockedExchange(&g_explicitGone[i], 0);
        InterlockedExchange(&g_everHadAssets[i], 0);
        InterlockedExchange(&g_everHadForces[i], 0);
        InterlockedExchange(&g_aliveConfirmed[i], 0);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_aliveSince[i]), 0);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_zeroArmySince[i]), 0);
        InterlockedExchange(&g_liveAssets[i], 0);
        InterlockedExchange(&g_units[i], 0);
        InterlockedExchange(&g_buildings[i], 0);
        InterlockedExchange(&g_slotKind[i], -1);
        g_pendingGoneNames[i][0] = 0;
        g_pendingGoneTick[i] = 0;
    }
    InterlockedExchange(&g_ready, 0);
    Log("RemoveHook");
}

} // namespace

extern "C" __declspec(dllexport) DWORD __stdcall AllyLeave_SetEnabled(LPVOID enabled)
{
    const LONG on = enabled ? 1 : 0;
    if (on) LoadMarkModesFromJson();
    InterlockedExchange(&g_enabled, on);
    Log("SetEnabled=%ld ready=%ld hits=%ld recolors=%ld leaves=%ld markC=%ld markH=%ld",
        on, g_ready, g_hitCount, g_recolorCount, g_leaveEventCount,
        InterlockedCompareExchange(&g_markComputers, 0, 0),
        InterlockedCompareExchange(&g_markHumans, 0, 0));
    return 1;
}

extern "C" __declspec(dllexport) DWORD __stdcall AllyLeave_IsReady(LPVOID)
{
    return g_ready ? 1u : 0u;
}

extern "C" __declspec(dllexport) DWORD __stdcall AllyLeave_IsEnabled(LPVOID)
{
    return g_enabled ? 1u : 0u;
}

extern "C" __declspec(dllexport) DWORD __stdcall AllyLeave_GetStats(LPVOID)
{
    // hiword = recolors, loword = hits (capped)
    const LONG hits = g_hitCount > 0xFFFF ? 0xFFFF : g_hitCount;
    const LONG rec = g_recolorCount > 0xFFFF ? 0xFFFF : g_recolorCount;
    return (static_cast<DWORD>(rec) << 16) | static_cast<DWORD>(hits);
}

extern "C" __declspec(dllexport) void __stdcall AllyLeave_MarkGone(int playerIndex)
{
    MarkGoneUi(playerIndex, "export");
}

extern "C" __declspec(dllexport) void __stdcall AllyLeave_MarkGoneFromChat(int playerIndex)
{
    if (playerIndex < 0 || playerIndex > 7) return;
    NoteHumanSlot(playerIndex);
    MarkGoneUi(playerIndex, "chat-name");
}

static int SeatFromUniquePlayerName(const char* name)
{
    if (!g_playerName0 || !name || !name[0]) return -1;
    int match = -1;
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        const char* slot = g_playerName0 + static_cast<size_t>(i) * kPlayerNameStride;
        __try {
            if (slot[0] && _stricmp(slot, name) == 0) {
                match = i;
                ++count;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return (count == 1) ? match : -1;
}

// Name → alliances-row (= engine player index = color slot). The chat hook
// uses this to color sender names: the 0x91ADA8 name table is join-ordered
// and painted the wrong colors in shuffled lobbies.
extern "C" __declspec(dllexport) int __stdcall AllyLeave_FindRowByName(const char* name)
{
    if (!name || !name[0]) return -1;
    int match = -1;
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        if (!g_lastName[i][0]) continue;
        if (_stricmp(g_lastName[i], name) == 0) {
            match = i;
            ++count;
        }
    }
    return (count == 1) ? match : -1;
}

extern "C" __declspec(dllexport) void __stdcall AllyLeave_MarkGoneByName(const char* name)
{
    if (!name || !name[0]) return;

    const int row = AllyLeave_FindRowByName(name);
    if (row >= 0) {
        MarkGoneUi(row, "chat-name");
        return;
    }

    const int seat = SeatFromUniquePlayerName(name);
    if (seat >= 0) {
        NoteHumanSlot(seat);
        MarkGoneUi(seat, "chat-name");
        return;
    }

    // No row/name table match yet — queue until F11 binds the alliances row.
    RememberPendingGoneName(name);
}

// Snapshot of HasForces for all 8 seats — logged with every poll mark so a
// wrong-seat mark can be traced to the index that actually went to zero.
static void FormatHfVector(char* buf, size_t cap)
{
    int hf[8];
    for (int i = 0; i < 8; ++i) hf[i] = CallHasForces(i);
    _snprintf_s(buf, cap, _TRUNCATE, "%d,%d,%d,%d,%d,%d,%d,%d",
        hf[0], hf[1], hf[2], hf[3], hf[4], hf[5], hf[6], hf[7]);
}

// Continuous NPC-wipe detection on the game's own HasForces (0x4F4240).
// Runs in the poll thread — independent of F11 paints and of our census,
// which both proved unreliable (census misses seats on 8p maps).
static void PollWipeMarks()
{
    if (!g_enabled || !g_ready || !g_hasForces) return;

    int local = -1;
    if (g_localPlayer) {
        __try {
            local = static_cast<int>(*g_localPlayer);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            local = -1;
        }
    }
    // Only while we ourselves are alive in a running match. Outside a match
    // HasForces reads 0 for everyone → would mark every seat. An invalid local
    // index also means "not in a match" — it must still advance the dead-timer,
    // otherwise the between-match reset never fires (menu leaves the local
    // player byte out of range) and explicit human marks leak into the rematch.
    static DWORD s_localDeadSince = 0;
    static bool s_matchSeen = false;
    const int localHf = (local >= 0 && local <= 7) ? CallHasForces(local) : 0;
    const DWORD now = GetTickCount();
    if (localHf <= 0) {
        if (s_localDeadSince == 0) {
            s_localDeadSince = now ? now : 1;
        } else if (s_matchSeen && (now - s_localDeadSince) > 3000) {
            // Match over / back to menu: drop all sticky state for the next game.
            ResetWipeTracking("local-gone");
            s_matchSeen = false;
        }
        return;
    }
    s_localDeadSince = 0;
    s_matchSeen = true;

    for (int i = 0; i < 8; ++i) {
        if (i == local) continue;
        const int hf = CallHasForces(i);
        NoteHasForcesSample(i, hf); // maintains aliveSince → aliveConfirmed (8s)

        uint8_t status = ReadLiveController(i);
        uint8_t defeat = 0;
        if (g_defeatBase) {
            __try {
                defeat = g_defeatBase[i];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                defeat = 0;
            }
        }
        const bool played =
            InterlockedCompareExchange(&g_everHadForces[i], 0, 0) != 0 ||
            InterlockedCompareExchange(&g_aliveConfirmed[i], 0, 0) != 0 ||
            InterlockedCompareExchange(&g_everHadAssets[i], 0, 0) != 0 ||
            IsComputerSlot(i) ||
            g_lastName[i][0] != 0;
        const bool computer = IsComputerSlot(i);

        // Computer / unknown wipe: status/defeat only after HasForces hits 0.
        // This is the path that marks blue AI (Stormreaver) on peon-wipe.
        if (InterlockedCompareExchange(&g_leftFlags[i], 0, 0) == 0 && hf <= 0 && played) {
            if (status == 3) {
                MarkGoneUi(i, computer ? "status3" : "status3-human");
            } else if (status >= 2 && status != 0xFF) {
                MarkGoneUi(i, "status-elim");
            } else if (defeat == 2 || defeat == 3) {
                MarkGoneUi(i, defeat == 2 ? "defeat-elim" : "defeat-poll");
            } else if (defeat == 1 && !computer) {
                MarkGoneUi(i, "defeat-left");
            }
        }

        // Humans: elim/surrender even when HasForces still reads >0 (no Exit Game).
        if (InterlockedCompareExchange(&g_leftFlags[i], 0, 0) == 0 &&
            !computer && played) {
            if (status == 3) {
                MarkGoneUi(i, "status3-human");
            } else if (status >= 2 && status != 0xFF) {
                MarkGoneUi(i, "human-elim");
            } else if (defeat == 1 || defeat == 2 || defeat == 3) {
                MarkGoneUi(i, "defeat-human");
            }
        }

        if (hf > 0) {
            if (g_leftFlags[i] && !ShouldKeepGoneDespiteForces(i)) {
                ClearGoneUi(i);
            }
            continue;
        }
        if (hf != 0) continue;      // helper unavailable — no census fallback
        if (g_leftFlags[i]) continue;
        if (!SeatEligibleForHfWipe(i)) continue;

        DWORD since = static_cast<DWORD>(InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&g_zeroArmySince[i]), 0, 0));
        if (since == 0) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_zeroArmySince[i]),
                static_cast<LONG>(now ? now : 1));
            continue;
        }
        if ((now - since) >= 2000) {
            static LONG s_hfMarkLog = 0;
            if (InterlockedIncrement(&s_hfMarkLog) <= 32) {
                char hfBuf[64];
                FormatHfVector(hfBuf, sizeof(hfBuf));
                Log("hfMark p=%d name=%s hf=%s", i,
                    g_lastName[i][0] ? g_lastName[i] : "?", hfBuf);
            }
            MarkGoneUi(i, "hf-poll");
        }
    }
}

static DWORD WINAPI InstallThread(LPVOID)
{
    Log("InstallThread start");
    for (int i = 0; i < 50 && !InstallHook(); ++i) {
        Sleep(100);
    }
    if (!g_ready) {
        Log("InstallThread: gave up");
        return 0;
    }
    // Keep polling lobby controllers: computers are 4/2/6/7 until remaster
    // remaps them to 1 at match start. Missing that window leaves kind=-1 forever.
    for (int i = 0; i < 40 && !g_censusDone; ++i) {
        Sleep(250);
        RefreshSlotKinds();
        if (g_unitTypeHeads && ResyncCensus(nullptr, "boot")) {
            break;
        }
    }
    RefreshSlotKinds();
    if (!g_censusDone) Log("InstallThread: census still pending");
    Log("InstallThread: slotKinds c=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
        InterlockedCompareExchange(&g_slotKind[0], 0, 0),
        InterlockedCompareExchange(&g_slotKind[1], 0, 0),
        InterlockedCompareExchange(&g_slotKind[2], 0, 0),
        InterlockedCompareExchange(&g_slotKind[3], 0, 0),
        InterlockedCompareExchange(&g_slotKind[4], 0, 0),
        InterlockedCompareExchange(&g_slotKind[5], 0, 0),
        InterlockedCompareExchange(&g_slotKind[6], 0, 0),
        InterlockedCompareExchange(&g_slotKind[7], 0, 0));

    // Poll forever: slot kinds for classification + HasForces wipe detection.
    // The old 2-minute cap meant nothing was detected in longer matches.
    for (int i = 0; g_ready; ++i) {
        Sleep(200);
        RefreshSlotKinds();
        PollWipeMarks();
        if ((i % 150) == 0) { // every ~30s
            char hfBuf[64];
            FormatHfVector(hfBuf, sizeof(hfBuf));
            int localIdx = -1;
            if (g_localPlayer) {
                __try {
                    localIdx = static_cast<int>(*g_localPlayer);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    localIdx = -1;
                }
            }
            Log("hfPoll local=%d hf=%s left=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld", localIdx, hfBuf,
                InterlockedCompareExchange(&g_leftFlags[0], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[1], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[2], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[3], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[4], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[5], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[6], 0, 0),
                InterlockedCompareExchange(&g_leftFlags[7], 0, 0));
            Log("slotPoll[%d] c=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld anyComp=%d",
                i,
                InterlockedCompareExchange(&g_slotKind[0], 0, 0),
                InterlockedCompareExchange(&g_slotKind[1], 0, 0),
                InterlockedCompareExchange(&g_slotKind[2], 0, 0),
                InterlockedCompareExchange(&g_slotKind[3], 0, 0),
                InterlockedCompareExchange(&g_slotKind[4], 0, 0),
                InterlockedCompareExchange(&g_slotKind[5], 0, 0),
                InterlockedCompareExchange(&g_slotKind[6], 0, 0),
                InterlockedCompareExchange(&g_slotKind[7], 0, 0),
                AnyLiveComputerController() ? 1 : 0);
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        RemoveHook();
    }
    return TRUE;
}
