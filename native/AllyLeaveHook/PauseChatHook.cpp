// Warcraft II Remastered — allow chat while the game is paused.
// Bypasses pause-flag gates on Message: UI / key handling, and rewires the
// main input-dispatch skip so keys still reach chat when message-mode is on.
//
// IMPORTANT: do not trust PE OptionalHeader.ImageBase for ASLR slide — Windows
// may rewrite it to the load address (slide becomes 0). Derive addresses from
// found code sites / instruction immediates instead.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

volatile LONG g_enabled = 0;
volatile LONG g_ready = 0;
volatile LONG g_patched = 0;

struct Patch {
    uint8_t* site = nullptr;
    uint8_t orig[16]{};
    uint8_t patch[16]{};
    size_t size = 0;
    const char* name = nullptr;
};

constexpr size_t kMaxPatches = 16;
Patch g_patches[kMaxPatches]{};
size_t g_patchCount = 0;
void* g_cave = nullptr;

uint8_t* g_pauseFlag = nullptr; // live VA of byte pause flag
uint8_t* g_chatMode = nullptr;  // live VA of chat mode byte
// While Message: is open we clear the pause byte so key handlers run, but keep
// the main tick frozen via tick-freeze caves. Restore pause when chat ends.
uint8_t g_savedPause = 0;

char g_logPath[MAX_PATH]{};

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_pause_chat_hook.log", temp);
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

bool Match(const uint8_t* p, const uint8_t* pat, const char* mask)
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
        if (Match(p, pat, mask)) return p;
    }
    return nullptr;
}

bool WriteBytes(uint8_t* site, const uint8_t* bytes, size_t n)
{
    if (!site || !bytes || n == 0) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(site, n, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(site, bytes, n);
    VirtualProtect(site, n, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, n);
    return true;
}

bool AddPatch(uint8_t* site, const uint8_t* patchBytes, size_t size, const char* name)
{
    if (!site || !patchBytes || size == 0 || size > sizeof(Patch::patch) || g_patchCount >= kMaxPatches)
        return false;
    if (!IsLikelyCode(site, size)) return false;

    Patch& p = g_patches[g_patchCount++];
    p.site = site;
    p.size = size;
    p.name = name;
    memcpy(p.orig, site, size);
    memcpy(p.patch, patchBytes, size);
    Log("AddPatch: %s site=%p size=%u orig=%02X%02X", name, site, static_cast<unsigned>(size),
        p.orig[0], p.orig[1]);
    return true;
}

HMODULE FindGameModule(uint8_t** outBase, size_t* outSize)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return nullptr;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    HMODULE found = nullptr;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"Warcraft II.exe") == 0) {
                found = me.hModule;
                if (outBase) *outBase = me.modBaseAddr;
                if (outSize) *outSize = me.modBaseSize;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

void ApplyPatches()
{
    if (g_patched || g_patchCount == 0) return;
    for (size_t i = 0; i < g_patchCount; ++i) {
        if (!WriteBytes(g_patches[i].site, g_patches[i].patch, g_patches[i].size)) {
            Log("ApplyPatches: failed on %s (%lu)", g_patches[i].name, GetLastError());
            for (size_t j = 0; j < i; ++j)
                WriteBytes(g_patches[j].site, g_patches[j].orig, g_patches[j].size);
            return;
        }
    }
    InterlockedExchange(&g_patched, 1);
    Log("ApplyPatches: ok count=%u", static_cast<unsigned>(g_patchCount));
}

void RemovePatches()
{
    if (!g_patched) return;
    for (size_t i = 0; i < g_patchCount; ++i)
        WriteBytes(g_patches[i].site, g_patches[i].orig, g_patches[i].size);
    InterlockedExchange(&g_patched, 0);
    Log("RemovePatches");
}

const char* NextPatchName(const char* prefix)
{
    static char names[16][32]{};
    static LONG next = 0;
    const LONG id = InterlockedIncrement(&next);
    if (id < 1 || id > 16) return prefix;
    sprintf_s(names[id - 1], "%s-%ld", prefix, id);
    return names[id - 1];
}

// siteCmp: 80 3D xx xx xx xx 00 75 rr
// Rewrites to: if (!paused || chatMode) continue; else skip;
bool BuildDispatchCave(uint8_t* siteCmp, uint8_t* caveSlot, size_t caveBytesLeft,
                       uint8_t** caveUsed)
{
    if (!siteCmp || siteCmp[0] != 0x80 || siteCmp[1] != 0x3D || siteCmp[7] != 0x75)
        return false;
    if (!g_pauseFlag || !g_chatMode) return false;

    const uint8_t rel = siteCmp[8];
    uint8_t* cont = siteCmp + 9;
    uint8_t* skip = cont + rel;

    uint8_t* cave = caveSlot;
    size_t o = 0;
    auto emit = [&](const void* p, size_t n) -> bool {
        if (o + n > caveBytesLeft) return false;
        memcpy(cave + o, p, n);
        o += n;
        return true;
    };

    // cmp byte [pause], 0
    uint8_t cmpPause[] = { 0x80, 0x3D, 0, 0, 0, 0, 0x00 };
    *reinterpret_cast<uint32_t*>(cmpPause + 2) = reinterpret_cast<uint32_t>(g_pauseFlag);
    if (!emit(cmpPause, sizeof(cmpPause))) return false;

    // jz cont_jmp (rel 14)
    uint8_t jzCont[] = { 0x74, 0x0E };
    if (!emit(jzCont, sizeof(jzCont))) return false;

    // cmp byte [chat], 0
    uint8_t cmpChat[] = { 0x80, 0x3D, 0, 0, 0, 0, 0x00 };
    *reinterpret_cast<uint32_t*>(cmpChat + 2) = reinterpret_cast<uint32_t>(g_chatMode);
    if (!emit(cmpChat, sizeof(cmpChat))) return false;

    // jnz cont_jmp
    uint8_t jnzCont[] = { 0x75, 0x05 };
    if (!emit(jnzCont, sizeof(jnzCont))) return false;

    // jmp skip
    uint8_t jmpSkip[5] = { 0xE9, 0, 0, 0, 0 };
    *reinterpret_cast<int32_t*>(jmpSkip + 1) =
        static_cast<int32_t>(skip - (cave + o + 5));
    if (!emit(jmpSkip, sizeof(jmpSkip))) return false;

    // cont_jmp: jmp cont
    uint8_t jmpCont[5] = { 0xE9, 0, 0, 0, 0 };
    *reinterpret_cast<int32_t*>(jmpCont + 1) =
        static_cast<int32_t>(cont - (cave + o + 5));
    if (!emit(jmpCont, sizeof(jmpCont))) return false;

    uint8_t sitePatch[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    *reinterpret_cast<int32_t*>(sitePatch + 1) =
        static_cast<int32_t>(cave - (siteCmp + 5));

    if (!AddPatch(siteCmp, sitePatch, 9, NextPatchName("dispatch"))) return false;

    *caveUsed = cave + o;
    Log("BuildDispatchCave: site=%p cave=%p cont=%p skip=%p pause=%p chat=%p",
        siteCmp, cave, cont, skip, g_pauseFlag, g_chatMode);
    return true;
}

// siteCmp: 80 3D pause 00 75 rr  — if chatMode || paused → skip, else cont.
// Used on main-tick +0x67 so clearing pause for chat does not resume simulation.
bool BuildTickFreezeCave(uint8_t* siteCmp, uint8_t* caveSlot, size_t caveBytesLeft,
                         uint8_t** caveUsed)
{
    if (!siteCmp || siteCmp[0] != 0x80 || siteCmp[1] != 0x3D || siteCmp[7] != 0x75)
        return false;
    if (!g_pauseFlag || !g_chatMode) return false;

    const uint8_t rel = siteCmp[8];
    uint8_t* cont = siteCmp + 9;
    uint8_t* skip = cont + rel;

    uint8_t* cave = caveSlot;
    size_t o = 0;
    auto emit = [&](const void* p, size_t n) -> bool {
        if (o + n > caveBytesLeft) return false;
        memcpy(cave + o, p, n);
        o += n;
        return true;
    };

    // cmp byte [chatMode], 0 / jnz skip
    uint8_t cmpChat[] = { 0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x75, 0x00 };
    *reinterpret_cast<uint32_t*>(cmpChat + 2) = reinterpret_cast<uint32_t>(g_chatMode);
    const size_t jnzChatAt = o + 7;
    if (!emit(cmpChat, sizeof(cmpChat))) return false;

    // cmp byte [pause], 0 / jnz skip
    uint8_t cmpPause[] = { 0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x75, 0x00 };
    *reinterpret_cast<uint32_t*>(cmpPause + 2) = reinterpret_cast<uint32_t>(g_pauseFlag);
    const size_t jnzPauseAt = o + 7;
    if (!emit(cmpPause, sizeof(cmpPause))) return false;

    // jmp cont
    uint8_t jmpCont[5] = { 0xE9, 0, 0, 0, 0 };
    *reinterpret_cast<int32_t*>(jmpCont + 1) =
        static_cast<int32_t>(cont - (cave + o + 5));
    if (!emit(jmpCont, sizeof(jmpCont))) return false;

    // skip_label: jmp skip
    const size_t skipLabel = o;
    uint8_t jmpSkip[5] = { 0xE9, 0, 0, 0, 0 };
    *reinterpret_cast<int32_t*>(jmpSkip + 1) =
        static_cast<int32_t>(skip - (cave + o + 5));
    if (!emit(jmpSkip, sizeof(jmpSkip))) return false;

    cave[jnzChatAt + 1] = static_cast<uint8_t>(skipLabel - (jnzChatAt + 2));
    cave[jnzPauseAt + 1] = static_cast<uint8_t>(skipLabel - (jnzPauseAt + 2));

    uint8_t sitePatch[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    *reinterpret_cast<int32_t*>(sitePatch + 1) =
        static_cast<int32_t>(cave - (siteCmp + 5));
    if (!AddPatch(siteCmp, sitePatch, 9, NextPatchName("tick-freeze"))) return false;

    *caveUsed = cave + o;
    Log("BuildTickFreezeCave: site=%p cave=%p", siteCmp, cave);
    return true;
}

// Always-run merge point: while chatMode, clear pause byte (keys work); restore after.
bool BuildPauseClearCave(uint8_t* siteInc, uint8_t* caveSlot, size_t caveBytesLeft,
                         uint8_t** caveUsed)
{
    if (!siteInc || siteInc[0] != 0xFF || siteInc[1] != 0x05) return false;
    if (!g_chatMode || !g_pauseFlag) return false;

    uint8_t* cave = caveSlot;
    size_t o = 0;
    auto emit = [&](const void* p, size_t n) -> bool {
        if (o + n > caveBytesLeft) return false;
        memcpy(cave + o, p, n);
        o += n;
        return true;
    };

    auto* saved = &g_savedPause;

    // cmp byte [chatMode], 0 / jz try_restore
    uint8_t cmpChat[] = { 0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x74, 0x00 };
    *reinterpret_cast<uint32_t*>(cmpChat + 2) = reinterpret_cast<uint32_t>(g_chatMode);
    const size_t jzRestoreAt = o + 7;
    if (!emit(cmpChat, sizeof(cmpChat))) return false;

    // chatting: mov al,[pause] / test al,al / jz do_inc / mov [saved],al / mov [pause],0 / jmp do_inc
    uint8_t clearBlock[] = {
        0xA0, 0, 0, 0, 0,             // mov al, [pause]
        0x84, 0xC0,                   // test al, al
        0x74, 0x00,                   // jz do_inc (patch later)
        0xA2, 0, 0, 0, 0,             // mov [saved], al
        0xC6, 0x05, 0, 0, 0, 0, 0x00 // mov byte [pause], 0
    };
    *reinterpret_cast<uint32_t*>(clearBlock + 1) = reinterpret_cast<uint32_t>(g_pauseFlag);
    *reinterpret_cast<uint32_t*>(clearBlock + 10) = reinterpret_cast<uint32_t>(saved);
    *reinterpret_cast<uint32_t*>(clearBlock + 16) = reinterpret_cast<uint32_t>(g_pauseFlag);
    const size_t clearStart = o;
    if (!emit(clearBlock, sizeof(clearBlock))) return false;

    // jmp do_inc (after clear block) — placeholder, fill after restore block size known
    uint8_t jmpDoInc[2] = { 0xEB, 0x00 };
    const size_t jmpDoIncAt = o;
    if (!emit(jmpDoInc, sizeof(jmpDoInc))) return false;

    // try_restore:
    const size_t restoreAt = o;
    cave[jzRestoreAt + 1] = static_cast<uint8_t>(restoreAt - (jzRestoreAt + 2));

    uint8_t restoreBlock[] = {
        0xA0, 0, 0, 0, 0,             // mov al, [saved]
        0x84, 0xC0,                   // test al, al
        0x74, 0x00,                   // jz do_inc
        0xA2, 0, 0, 0, 0,             // mov [pause], al
        0xC6, 0x05, 0, 0, 0, 0, 0x00 // mov byte [saved], 0
    };
    *reinterpret_cast<uint32_t*>(restoreBlock + 1) = reinterpret_cast<uint32_t>(saved);
    *reinterpret_cast<uint32_t*>(restoreBlock + 10) = reinterpret_cast<uint32_t>(g_pauseFlag);
    *reinterpret_cast<uint32_t*>(restoreBlock + 16) = reinterpret_cast<uint32_t>(saved);
    const size_t restoreStart = o;
    if (!emit(restoreBlock, sizeof(restoreBlock))) return false;

    const size_t doInc = o;
    // fill short jumps to do_inc
    cave[clearStart + 8] = static_cast<uint8_t>(doInc - (clearStart + 9)); // jz in clearBlock
    cave[jmpDoIncAt + 1] = static_cast<uint8_t>(doInc - (jmpDoIncAt + 2));
    cave[restoreStart + 8] = static_cast<uint8_t>(doInc - (restoreStart + 9));

    if (!emit(siteInc, 6)) return false;

    uint8_t jmpBack[5] = { 0xE9, 0, 0, 0, 0 };
    *reinterpret_cast<int32_t*>(jmpBack + 1) =
        static_cast<int32_t>((siteInc + 6) - (cave + o + 5));
    if (!emit(jmpBack, sizeof(jmpBack))) return false;

    uint8_t sitePatch[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    *reinterpret_cast<int32_t*>(sitePatch + 1) =
        static_cast<int32_t>(cave - (siteInc + 5));
    if (!AddPatch(siteInc, sitePatch, 6, NextPatchName("pause-clear"))) return false;

    *caveUsed = cave + o;
    Log("BuildPauseClearCave: site=%p cave=%p saved=%p", siteInc, cave, saved);
    return true;
}

bool InstallSites()
{
    g_patchCount = 0;
    g_pauseFlag = nullptr;
    g_chatMode = nullptr;

    uint8_t* base = nullptr;
    size_t imageSize = 0;
    if (!FindGameModule(&base, &imageSize) || !base || imageSize < 0x1000) {
        Log("InstallSites: Warcraft II.exe module not found");
        return false;
    }
    Log("InstallSites: game base=%p size=0x%X", base, (unsigned)imageSize);

    if (!g_cave) {
        g_cave = VirtualAlloc(nullptr, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_cave) {
            Log("InstallSites: VirtualAlloc cave failed");
            return false;
        }
    }
    auto* caveCursor = reinterpret_cast<uint8_t*>(g_cave);
    size_t caveLeft = 1024;

    // Draw: cmp [pause],0 / lea / mov / jnz +0x103  — jnz at +13
    static const uint8_t drawPat[] = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x8D, 0x46, 0xE0, 0x89, 0x45, 0xAC,
        0x0F, 0x85, 0x03, 0x01, 0x00, 0x00
    };
    static const char drawMask[] = "xx????xxxxxxxxxxxxx";
    uint8_t* drawHit = FindPattern(base, imageSize, drawPat, drawMask);
    if (!drawHit) {
        Log("InstallSites: draw pattern miss");
        return false;
    }

    // Absolute pause flag from the cmp immediate (always relocated correctly).
    g_pauseFlag = *reinterpret_cast<uint8_t**>(drawHit + 2);
    // Preferred VA 0x91C596 at ImageBase 0x400000 → chat mode 0x9347BE is +0x1B228 away.
    g_chatMode = g_pauseFlag + (0x009347BE - 0x0091C596);
    Log("InstallSites: pauseFlag=%p chatMode=%p (from draw cmp)", g_pauseFlag, g_chatMode);

    {
        uint8_t* site = drawHit + 13;
        const uint8_t nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        if (!AddPatch(site, nops, 6, "draw-jnz")) return false;
    }

    // Input early-out jnz
    static const uint8_t inputPat[] = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x0F, 0x85, 0x98, 0x01, 0x00, 0x00, 0x80, 0x3D
    };
    static const char inputMask[] = "xx????xxxxxxxxx";
    uint8_t* inputHit = FindPattern(base, imageSize, inputPat, inputMask);
    if (!inputHit) {
        Log("InstallSites: input pattern miss");
        return false;
    }
    {
        const uint8_t nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        if (!AddPatch(inputHit + 7, nops, 6, "input-jnz")) return false;
        // optional second jnz
        uint8_t* input2 = inputHit + 7 + 6; // after first jnz: 80 3D ... 0F 85
        if (input2[0] == 0x80 && input2[1] == 0x3D && input2[7] == 0x0F && input2[8] == 0x85) {
            AddPatch(input2 + 7, nops, 6, "input2-jnz");
        }
    }

    // Key handler printable: jz → jmp
    static const uint8_t keyPat[] = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x74, 0x7B, 0x56, 0x57, 0xE8
    };
    static const char keyMask[] = "xx????xxxxxx";
    uint8_t* keyHit = FindPattern(base, imageSize, keyPat, keyMask);
    if (!keyHit) {
        Log("InstallSites: key-char pattern miss");
        return false;
    }
    {
        const uint8_t jmpRel[] = { 0xEB, 0x7B };
        if (!AddPatch(keyHit + 7, jmpRel, 2, "key-char-jz")) return false;
    }

    // Optional helpers near key handler (relative to keyHit = cmp at 4E6FB0)
    {
        // jnz at 4E7031 = keyHit + 0x81
        uint8_t* case3 = keyHit + 0x81;
        if (IsLikelyCode(case3, 2) && case3[0] == 0x75 && case3[1] == 0x49) {
            const uint8_t nops[2] = { 0x90, 0x90 };
            AddPatch(case3, nops, 2, "key-case3-jnz");
        }
    }

    static const uint8_t strPat[] = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x56, 0x57, 0x8B, 0x7D, 0x08, 0x74, 0x04, 0x33, 0xF6
    };
    static const char strMask[] = "xx????xxxxxxxxxx";
    if (uint8_t* strHit = FindPattern(base, imageSize, strPat, strMask)) {
        const uint8_t jmpRel[] = { 0xEB, 0x04 };
        AddPatch(strHit + 12, jmpRel, 2, "str-helper-jz");
    }

    // Key-callback (+0x29): allow when chatMode OR not paused.
    // Main-tick (+0x67): freeze when chatMode OR paused (so clearing pause for keys
    // does not resume simulation).
    int dispatchCount = 0;
    int freezeCount = 0;
    uint8_t* tickMergeInc = nullptr;
    for (size_t i = 0; i + 9 <= imageSize; ++i) {
        uint8_t* p = base + i;
        if (p[0] != 0x80 || p[1] != 0x3D || p[7] != 0x75) continue;
        uint8_t* immPause = *reinterpret_cast<uint8_t**>(p + 2);
        if (immPause != g_pauseFlag) continue;

        const uint8_t rel = p[8];
        if (rel == 0x67) {
            if (!tickMergeInc) {
                uint8_t* skip = p + 9 + rel;
                if (IsLikelyCode(skip, 6) && skip[0] == 0xFF && skip[1] == 0x05)
                    tickMergeInc = skip;
            }
            if (freezeCount >= 2) continue;
            uint8_t* used = nullptr;
            if (!BuildTickFreezeCave(p, caveCursor, caveLeft, &used)) {
                Log("InstallSites: tick-freeze failed at %p", p);
                continue;
            }
            caveLeft -= static_cast<size_t>(used - caveCursor);
            caveCursor = used;
            ++freezeCount;
            Log("InstallSites: tick-freeze site=%p", p);
            continue;
        }
        if (rel != 0x29 || dispatchCount >= 2) continue;

        uint8_t* used = nullptr;
        if (!BuildDispatchCave(p, caveCursor, caveLeft, &used)) {
            Log("InstallSites: dispatch cave failed at %p rel=%02X", p, rel);
            continue;
        }
        caveLeft -= static_cast<size_t>(used - caveCursor);
        caveCursor = used;
        ++dispatchCount;
        Log("InstallSites: dispatch site=%p rel=%02X", p, rel);
    }
    if (dispatchCount < 1) {
        Log("InstallSites: no key-callback dispatch sites");
        return false;
    }
    if (freezeCount < 1) {
        Log("InstallSites: no tick-freeze sites");
        return false;
    }

    if (tickMergeInc) {
        uint8_t* used = nullptr;
        if (BuildPauseClearCave(tickMergeInc, caveCursor, caveLeft, &used)) {
            caveLeft -= static_cast<size_t>(used - caveCursor);
            caveCursor = used;
            Log("InstallSites: pause-clear site=%p", tickMergeInc);
        } else {
            Log("InstallSites: pause-clear cave failed at %p", tickMergeInc);
        }
    } else {
        Log("InstallSites: tick merge inc site not found");
    }

    InterlockedExchange(&g_ready, 1);
    Log("InstallSites: ready patches=%u dispatch=%d freeze=%d",
        static_cast<unsigned>(g_patchCount), dispatchCount, freezeCount);
    if (g_enabled) ApplyPatches();
    return true;
}

} // namespace

extern "C" __declspec(dllexport) DWORD __stdcall PauseChat_SetEnabled(LPVOID enabled)
{
    const LONG on = enabled ? 1 : 0;
    InterlockedExchange(&g_enabled, on);
    if (on) ApplyPatches();
    else RemovePatches();
    Log("SetEnabled=%ld ready=%ld patched=%ld", on, g_ready, g_patched);
    return 1;
}

extern "C" __declspec(dllexport) DWORD __stdcall PauseChat_IsReady(LPVOID)
{
    return g_ready ? 1u : 0u;
}

extern "C" __declspec(dllexport) DWORD __stdcall PauseChat_IsEnabled(LPVOID)
{
    return g_enabled ? 1u : 0u;
}

static DWORD WINAPI MonitorThread(LPVOID)
{
    uint8_t lastPause = 0xFF, lastChat = 0xFF;
    for (;;) {
        if (!g_ready || !g_pauseFlag || !g_chatMode) {
            Sleep(200);
            continue;
        }
        const uint8_t p = *g_pauseFlag;
        const uint8_t c = *g_chatMode;
        if (p != lastPause || c != lastChat) {
            Log("state pause=%u chat=%u saved=%u patched=%ld",
                (unsigned)p, (unsigned)c, (unsigned)g_savedPause, g_patched);
            lastPause = p;
            lastChat = c;
        }
        Sleep(100);
    }
    return 0;
}

static DWORD WINAPI InstallThread(LPVOID)
{
    Log("InstallThread start");
    for (int i = 0; i < 50 && !InstallSites(); ++i) {
        Sleep(100);
    }
    if (!g_ready) {
        Log("InstallThread: gave up");
        return 0;
    }
    HANDLE mon = CreateThread(nullptr, 0, MonitorThread, nullptr, 0, nullptr);
    if (mon) CloseHandle(mon);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InterlockedExchange(&g_enabled, 1);
        HANDLE thread = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        RemovePatches();
        if (g_cave) {
            VirtualFree(g_cave, 0, MEM_RELEASE);
            g_cave = nullptr;
        }
    }
    return TRUE;
}
