// Warcraft II Remastered — "Observe" on the defeat popup.
// Defeat ctor pushes Exit Game, then Show. We intercept Show on that popup
// so Observe is already in the button vector before widgets are built.
// Observe OnClick only closes the popup (no leave event).

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
volatile LONG g_hooked = 0;
volatile LONG g_addCount = 0;
volatile LONG g_inAdd = 0;

constexpr uintptr_t kPreferredImageBase = 0x00400000;
constexpr uintptr_t kPreferredDefeatPopup = 0x0095EA60;
constexpr uintptr_t kPreferredPopupShow = 0x00627430;
constexpr uintptr_t kPreferredStringCtor = 0x0049ED60;
constexpr uintptr_t kPreferredButtonSpecCtor = 0x00530320;
constexpr uintptr_t kPreferredPopupAddButton = 0x006273B0;
constexpr uintptr_t kPreferredButtonSpecDtor = 0x00525090;
constexpr uintptr_t kPreferredPopupClose = 0x00627450;
constexpr uintptr_t kPreferredBtnGetId = 0x00547970;
constexpr uintptr_t kPreferredBtnDtor1 = 0x004C5D50;
constexpr uintptr_t kPreferredBtnDtor2 = 0x004C5D80;
constexpr size_t kShowPatchLen = 7; // push ebp; mov ebp,esp; push ecx; mov [ebp-4],ecx
constexpr size_t kButtonSpecSize = 0x28;

uint8_t* g_gameBase = nullptr;
size_t g_gameSize = 0;

uint8_t* g_showSite = nullptr;
uint8_t g_savedShow[16]{};
void* g_showTrampoline = nullptr;

void* g_defeatPopup = nullptr;

using GameStringCtorFn = void(__thiscall*)(void* self, const char* text);
using ButtonSpecCtorFn = void(__thiscall*)(void* spec, void* cbObj, void* labelStr, uint8_t* outByte);
using PopupAddButtonFn = void(__thiscall*)(void* popup, void* spec);
using ButtonSpecDtorFn = void(__thiscall*)(void* spec);
using PopupCloseFn = void(__thiscall*)(void* popup);
using BtnGetIdFn = void*(__thiscall*)(void* self);
using BtnDtorFn = void(__thiscall*)(void* self);

GameStringCtorFn g_stringCtor = nullptr;
ButtonSpecCtorFn g_buttonSpecCtor = nullptr;
PopupAddButtonFn g_popupAddButton = nullptr;
ButtonSpecDtorFn g_buttonSpecDtor = nullptr;
PopupCloseFn g_popupClose = nullptr;
BtnGetIdFn g_btnGetId = nullptr;
BtnDtorFn g_btnDtor1 = nullptr;
BtnDtorFn g_btnDtor2 = nullptr;

void* g_observeVtable[6]{};

void ObserveOnClickImpl(void* self);
void ObserveCloneImpl(void* dst, void* src);
void MaybeAddObserveForShow(void* popup);
void Hook_PopupShow();

#if defined(_M_IX86)
__declspec(naked) void ObserveOnClickThunk()
{
    __asm {
        push ecx
        call ObserveOnClickImpl
        pop ecx
        ret
    }
}

__declspec(naked) void ObserveCloneThunk()
{
    __asm {
        mov eax, ecx
        mov ecx, dword ptr [esp + 4]
        push ecx
        push eax
        call ObserveCloneImpl
        add esp, 8
        ret 4
    }
}

__declspec(naked) void Hook_PopupShow()
{
    __asm {
        push ecx
        push ecx
        call MaybeAddObserveForShow
        add esp, 4
        pop ecx
        jmp dword ptr [g_showTrampoline]
    }
}
#else
#error ObserveHook targets 32-bit Warcraft II only
#endif

char g_logPath[MAX_PATH]{};

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_observe_hook.log", temp);
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

template<typename Fn>
Fn ResolveFn(uintptr_t preferredVa)
{
    if (!g_gameBase) return nullptr;
    return reinterpret_cast<Fn>(g_gameBase + (preferredVa - kPreferredImageBase));
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

bool PatchFuncPrologueLen(uint8_t* site, size_t patchLen, void* hook,
                          uint8_t* savedOrig, void** trampOut)
{
    if (patchLen < 5 || patchLen > 16) return false;
    memcpy(savedOrig, site, patchLen);
    void* tramp = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return false;

    auto* t = static_cast<uint8_t*>(tramp);
    memcpy(t, site, patchLen);
    t[patchLen] = 0xE9;
    *reinterpret_cast<int32_t*>(t + patchLen + 1) =
        static_cast<int32_t>((site + patchLen) - (t + patchLen + 5));

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, patchLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return false;
    }

    site[0] = 0xE9;
    *reinterpret_cast<int32_t*>(site + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(hook) - (site + 5));
    for (size_t i = 5; i < patchLen; ++i) site[i] = 0x90;

    VirtualProtect(site, patchLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, patchLen);
    *trampOut = tramp;
    return true;
}

void UnpatchSite(uint8_t* site, size_t origLen, const uint8_t* savedOrig, void** tramp)
{
    if (!site) return;
    DWORD oldProtect = 0;
    if (VirtualProtect(site, origLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(site, savedOrig, origLen);
        VirtualProtect(site, origLen, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), site, origLen);
    }
    if (tramp && *tramp) {
        VirtualFree(*tramp, 0, MEM_RELEASE);
        *tramp = nullptr;
    }
}

void ObserveCloneImpl(void* dst, void* src)
{
    *reinterpret_cast<void**>(dst) = g_observeVtable;
    *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(dst) + 4) =
        *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(src) + 4);
}

void ObserveOnClickImpl(void* self)
{
    (void)self;
    if (!g_popupClose || !g_defeatPopup) return;
    g_popupClose(g_defeatPopup);
    Log("ObserveOnClick: popup closed");
}

void InitObserveVtable()
{
    g_observeVtable[0] = reinterpret_cast<void*>(&ObserveCloneThunk);
    g_observeVtable[1] = reinterpret_cast<void*>(&ObserveCloneThunk);
    g_observeVtable[2] = reinterpret_cast<void*>(&ObserveOnClickThunk);
    g_observeVtable[3] = reinterpret_cast<void*>(g_btnGetId);
    g_observeVtable[4] = reinterpret_cast<void*>(g_btnDtor1);
    g_observeVtable[5] = reinterpret_cast<void*>(g_btnDtor2);
}

int ButtonCount(void* popup)
{
    if (!popup) return 0;
    uint8_t* vec = static_cast<uint8_t*>(popup) + 0x1C;
    uint8_t* begin = nullptr;
    uint8_t* finish = nullptr;
    __try {
        begin = *reinterpret_cast<uint8_t**>(vec);
        finish = *reinterpret_cast<uint8_t**>(vec + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    if (!begin || finish < begin) return 0;
    return static_cast<int>((finish - begin) / kButtonSpecSize);
}

void AddObserveButton()
{
    if (!g_stringCtor || !g_buttonSpecCtor || !g_popupAddButton || !g_buttonSpecDtor ||
        !g_defeatPopup) {
        Log("AddObserveButton: skip (missing apis)");
        return;
    }

    alignas(8) uint8_t labelStr[0x28]{};
    alignas(8) uint8_t spec[0x30]{};
    alignas(8) uint8_t cbObj[8]{};
    uint8_t outByte = 0;

    g_stringCtor(labelStr, "Observe");
    *reinterpret_cast<void**>(cbObj) = g_observeVtable;
    *reinterpret_cast<int32_t*>(cbObj + 4) = 0;
    g_buttonSpecCtor(spec, cbObj, labelStr, &outByte);
    g_popupAddButton(g_defeatPopup, spec);
    g_buttonSpecDtor(spec);

    const LONG n = InterlockedIncrement(&g_addCount);
    if (n <= 5 || (n % 120) == 0) {
        Log("AddObserveButton: ok n=%ld outByte=%u count=%d",
            n, static_cast<unsigned>(outByte), ButtonCount(g_defeatPopup));
    }
}

void __cdecl MaybeAddObserveForShow(void* popup)
{
    if (!g_enabled || !g_ready) return;
    if (popup != g_defeatPopup) return;
    if (InterlockedCompareExchange(&g_inAdd, 1, 0) != 0) return;

    __try {
        const int count = ButtonCount(popup);
        if (count == 1) {
            AddObserveButton();
        } else if (g_addCount < 5) {
            Log("MaybeAddObserveForShow: skip count=%d popup=%p", count, popup);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("MaybeAddObserveForShow: exception code=0x%08X", GetExceptionCode());
    }

    InterlockedExchange(&g_inAdd, 0);
}

bool InstallHook()
{
    if (g_hooked) return true;

    g_gameBase = nullptr;
    g_gameSize = 0;
    if (!FindGameModule(&g_gameBase, &g_gameSize) || !g_gameBase || g_gameSize < 0x1000) {
        Log("InstallHook: game module not found");
        return false;
    }

    g_defeatPopup = g_gameBase + (kPreferredDefeatPopup - kPreferredImageBase);
    g_stringCtor = ResolveFn<GameStringCtorFn>(kPreferredStringCtor);
    g_buttonSpecCtor = ResolveFn<ButtonSpecCtorFn>(kPreferredButtonSpecCtor);
    g_popupAddButton = ResolveFn<PopupAddButtonFn>(kPreferredPopupAddButton);
    g_buttonSpecDtor = ResolveFn<ButtonSpecDtorFn>(kPreferredButtonSpecDtor);
    g_popupClose = ResolveFn<PopupCloseFn>(kPreferredPopupClose);
    g_btnGetId = ResolveFn<BtnGetIdFn>(kPreferredBtnGetId);
    g_btnDtor1 = ResolveFn<BtnDtorFn>(kPreferredBtnDtor1);
    g_btnDtor2 = ResolveFn<BtnDtorFn>(kPreferredBtnDtor2);

    if (!g_stringCtor || !g_buttonSpecCtor || !g_popupAddButton || !g_buttonSpecDtor ||
        !g_popupClose || !g_btnGetId || !g_btnDtor1 || !g_btnDtor2) {
        Log("InstallHook: resolve failed");
        return false;
    }

    InitObserveVtable();

    uint8_t* site = g_gameBase + (kPreferredPopupShow - kPreferredImageBase);
    if (site + kShowPatchLen > g_gameBase + g_gameSize || !IsLikelyCode(site, kShowPatchLen) ||
        site[0] != 0x55 || site[1] != 0x8B || site[2] != 0xEC || site[3] != 0x51) {
        Log("InstallHook: Show site mismatch");
        return false;
    }

    void* tramp = nullptr;
    if (!PatchFuncPrologueLen(site, kShowPatchLen, reinterpret_cast<void*>(&Hook_PopupShow),
                              g_savedShow, &tramp)) {
        Log("InstallHook: Show patch failed (%lu)", GetLastError());
        return false;
    }

    g_showSite = site;
    g_showTrampoline = tramp;
    InterlockedExchange(&g_hooked, 1);
    InterlockedExchange(&g_ready, 1);
    Log("InstallHook: ok show=%p tramp=%p popup=%p", site, tramp, g_defeatPopup);
    return true;
}

void RemoveHook()
{
    if (!g_hooked) return;
    UnpatchSite(g_showSite, kShowPatchLen, g_savedShow, &g_showTrampoline);
    g_showSite = nullptr;
    InterlockedExchange(&g_hooked, 0);
    Log("RemoveHook");
}

} // namespace

extern "C" __declspec(dllexport) DWORD __stdcall Observe_SetEnabled(LPVOID enabled)
{
    const LONG on = enabled ? 1 : 0;
    InterlockedExchange(&g_enabled, on);
    if (on && !g_hooked) InstallHook();
    else if (!on && g_hooked) RemoveHook();
    Log("SetEnabled=%ld ready=%ld hooked=%ld", on, g_ready, g_hooked);
    return 1;
}

extern "C" __declspec(dllexport) DWORD __stdcall Observe_IsReady(LPVOID)
{
    return g_ready ? 1u : 0u;
}

extern "C" __declspec(dllexport) DWORD __stdcall Observe_IsEnabled(LPVOID)
{
    return g_enabled ? 1u : 0u;
}

static DWORD WINAPI InstallThread(LPVOID)
{
    Log("InstallThread start");
    InterlockedExchange(&g_enabled, 1);
    for (int i = 0; i < 50 && !InstallHook(); ++i) Sleep(100);
    if (!g_ready) Log("InstallThread: gave up");
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
