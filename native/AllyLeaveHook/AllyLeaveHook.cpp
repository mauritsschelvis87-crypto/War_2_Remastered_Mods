// Warcraft II Remastered — Alliances leave indicator (red name)
// Hooks the alliances row name bind; when status[player] != 1, force red text
// and prefix the name so the change is visible even if color is later overwritten.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kRedTextColor = 0xFF627CEE; // bytes EE 7C 62 FF = RGB(238,124,98) + A
constexpr size_t kUiColorOffset = 0x20C;

using SetTextFn = void(__cdecl*)(void* ui, const char* name, int prop);

volatile LONG g_enabled = 0;
volatile LONG g_ready = 0;
volatile LONG g_hitCount = 0;
volatile LONG g_recolorCount = 0;

uint8_t* g_statusBase = nullptr;
SetTextFn g_originalSetText = nullptr;
uint8_t* g_patchSite = nullptr;
uint8_t g_originalCall[5]{};
void* g_trampoline = nullptr;

char g_nameBuf[8][160]{};
char g_logPath[MAX_PATH]{};

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_ally_leave_hook.log", temp);
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

bool PlayerInactive(int playerIndex)
{
    if (!g_statusBase || playerIndex < 0 || playerIndex > 7) return false;
    const uint8_t status = g_statusBase[playerIndex];
    // 0 = empty slot (do not mark), 1 = active.
    // Leave / drop / eliminate write 3; other values >= 2 are also inactive in UI.
    return status >= 2;
}

void __cdecl Hook_SetText_Impl(void* ui, const char* name, int prop, int playerIndex)
{
    InterlockedIncrement(&g_hitCount);

    const uint8_t status =
        (g_statusBase && playerIndex >= 0 && playerIndex <= 7) ? g_statusBase[playerIndex] : 0xFF;
    const bool inactive = g_enabled && PlayerInactive(playerIndex);
    const char* useName = name;
    uint32_t saved = 0;
    uint32_t* colorPtr = nullptr;

    static LONG s_rowLog = 0;
    if (InterlockedIncrement(&s_rowLog) <= 64) {
        Log("row p=%d status=%u enabled=%ld inactive=%d prop=%d name=%s",
            playerIndex, status, g_enabled, inactive ? 1 : 0, prop, name ? name : "(null)");
    }

    if (inactive) {
        InterlockedIncrement(&g_recolorCount);
        if (name) {
            // Visible fallback if draw color is not retained by the widget.
            _snprintf_s(g_nameBuf[playerIndex], _TRUNCATE, "[X] %s", name);
            useName = g_nameBuf[playerIndex];
        }
        if (ui) {
            colorPtr = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ui) + kUiColorOffset);
            saved = *colorPtr;
            *colorPtr = kRedTextColor;
        }
    }

    g_originalSetText(ui, useName, prop);

    // Color is sampled during set-text/draw; restore so the next row keeps its skin color.
    // The "[X] " prefix remains as a durable mark if the widget redraws without our color.
    if (colorPtr) {
        *colorPtr = saved;
    }
}

// Call site stack (cdecl): ret, ui, name, prop, playerIndex(edi leftover from prior push).
// Replaces only the E8 call; caller still does add esp, 0x10.
void __declspec(naked) Hook_SetText_Gate()
{
    __asm {
        push ebp
        mov ebp, esp
        push dword ptr [ebp + 0x14]
        push dword ptr [ebp + 0x10]
        push dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x08]
        call Hook_SetText_Impl
        add esp, 16
        pop ebp
        ret
    }
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
        Log("InstallHook: pattern not found (imageSize=%u)", (unsigned)imageSize);
        return false;
    }

    const uint32_t statusImm = *reinterpret_cast<uint32_t*>(hit + 16);
    g_statusBase = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(statusImm));

    g_patchSite = hit + 6; // E8
    memcpy(g_originalCall, g_patchSite, 5);

    const int32_t rel = *reinterpret_cast<int32_t*>(g_patchSite + 1);
    g_originalSetText = reinterpret_cast<SetTextFn>(g_patchSite + 5 + rel);

    g_trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_trampoline) {
        Log("InstallHook: VirtualAlloc failed (%lu)", GetLastError());
        return false;
    }

    // trampoline: jmp Hook_SetText_Gate
    auto* t = static_cast<uint8_t*>(g_trampoline);
    t[0] = 0xE9;
    *reinterpret_cast<int32_t*>(t + 1) =
        static_cast<int32_t>(reinterpret_cast<uint8_t*>(&Hook_SetText_Gate) - (t + 5));

    DWORD oldProtect = 0;
    if (!VirtualProtect(g_patchSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("InstallHook: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }

    const int32_t newRel = static_cast<int32_t>(t - (g_patchSite + 5));
    g_patchSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(g_patchSite + 1) = newRel;

    VirtualProtect(g_patchSite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), g_patchSite, 5);

    InterlockedExchange(&g_ready, 1);
    // DLL is only injected when the Extra feature is on — start enabled so a missed
    // SetEnabled remote-thread call cannot leave the hook silently inert.
    InterlockedExchange(&g_enabled, 1);
    Log("InstallHook: ok site=%p statusBase=%p original=%p enabled=1", g_patchSite, g_statusBase, g_originalSetText);
    return true;
}

void RemoveHook()
{
    if (!g_patchSite) return;
    DWORD oldProtect = 0;
    if (VirtualProtect(g_patchSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(g_patchSite, g_originalCall, 5);
        VirtualProtect(g_patchSite, 5, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), g_patchSite, 5);
    }
    if (g_trampoline) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
    }
    g_patchSite = nullptr;
    g_originalSetText = nullptr;
    g_statusBase = nullptr;
    InterlockedExchange(&g_ready, 0);
    Log("RemoveHook");
}

} // namespace

extern "C" __declspec(dllexport) DWORD __stdcall AllyLeave_SetEnabled(LPVOID enabled)
{
    const LONG on = enabled ? 1 : 0;
    InterlockedExchange(&g_enabled, on);
    Log("SetEnabled=%ld ready=%ld hits=%ld recolors=%ld", on, g_ready, g_hitCount, g_recolorCount);
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

static DWORD WINAPI InstallThread(LPVOID)
{
    Log("InstallThread start");
    for (int i = 0; i < 50 && !InstallHook(); ++i) {
        Sleep(100);
    }
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
