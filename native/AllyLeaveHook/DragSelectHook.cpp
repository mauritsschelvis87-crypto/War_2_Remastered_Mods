// Warcraft II Remastered — custom drag-select rectangle color.
//
// Palette index 250 is what the visible rubber-band / drag box (and related
// classic selection rects) actually sample. Unit/building outlines force
// index 250 via `mov al, 0xFA` for the local player.
//
// Strategy: keep drag color on palette 250; remapping the unit-outline
// immediate 0xFA -> 0xF5 (245) so Other-colors "Selection" can own outlines
// without changing the drag box.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

volatile LONG g_enabled = 0;
volatile LONG g_ready = 0;
volatile LONG g_patched = 0;

uint8_t* g_immSite = nullptr; // points at the 0xFA immediate in unit-outline path
uint8_t g_origImm = 0xFA;

constexpr uint8_t kVanillaIndex = 0xFA; // 250 — drag box / shared selection
constexpr uint8_t kUnitOutlineIndex = 0xF5; // 245 — remapped unit/building outlines

char g_logPath[MAX_PATH]{};

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_drag_select_hook.log", temp);
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
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

bool WriteByte(uint8_t* site, uint8_t value)
{
    if (!site) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    *site = value;
    VirtualProtect(site, 1, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
    return true;
}

void ApplyPatch()
{
    if (!g_ready || !g_immSite) return;
    if (WriteByte(g_immSite, kUnitOutlineIndex)) {
        InterlockedExchange(&g_patched, 1);
        Log("ApplyPatch: unit-outline imm %p -> %02X", g_immSite, kUnitOutlineIndex);
    } else {
        Log("ApplyPatch: WriteByte failed");
    }
}

void RemovePatch()
{
    if (!g_immSite) return;
    if (WriteByte(g_immSite, g_origImm)) {
        InterlockedExchange(&g_patched, 0);
        Log("RemovePatch: unit-outline imm %p -> %02X", g_immSite, g_origImm);
    }
}

bool InstallSites()
{
    HMODULE game = GetModuleHandleW(L"Warcraft II.exe");
    if (!game) game = GetModuleHandleW(nullptr);
    if (!game) {
        Log("InstallSites: no module");
        return false;
    }

    auto* base = reinterpret_cast<uint8_t*>(game);
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;

    // Local-player unit/building selection outline color force:
    //   75 04          jnz short
    //   B0 FA          mov al, 0xFA
    //   EB 09          jmp short
    //   0F B6 C0       movzx eax, al
    const uint8_t pat[] = { 0x75, 0x04, 0xB0, 0xFA, 0xEB, 0x09, 0x0F, 0xB6, 0xC0 };
    const char* mask = "xxxxxxxxx";

    uint8_t* hit = FindPattern(base, imageSize, pat, mask);
    if (!hit) {
        // Already patched from a previous inject in this process.
        const uint8_t patPatched[] = { 0x75, 0x04, 0xB0, 0xF5, 0xEB, 0x09, 0x0F, 0xB6, 0xC0 };
        hit = FindPattern(base, imageSize, patPatched, mask);
        if (!hit) {
            Log("InstallSites: unit-outline pattern not found");
            return false;
        }
        Log("InstallSites: found already-patched site");
    }

    g_immSite = hit + 3; // B0 xx  → immediate byte
    g_origImm = kVanillaIndex;

    InterlockedExchange(&g_ready, 1);
    Log("InstallSites: ready immSite=%p", g_immSite);
    // Heal leftover F5 patches from earlier injects; only remapping when enabled.
    if (g_enabled) ApplyPatch();
    else RemovePatch();
    return true;
}

} // namespace

extern "C" __declspec(dllexport) DWORD __stdcall DragSelect_SetEnabled(LPVOID enabled)
{
    const LONG on = enabled ? 1 : 0;
    InterlockedExchange(&g_enabled, on);
    if (on) ApplyPatch();
    else RemovePatch();
    Log("SetEnabled=%ld ready=%ld patched=%ld", on, g_ready, g_patched);
    return 1;
}

extern "C" __declspec(dllexport) DWORD __stdcall DragSelect_IsReady(LPVOID)
{
    return g_ready ? 1u : 0u;
}

extern "C" __declspec(dllexport) DWORD __stdcall DragSelect_IsEnabled(LPVOID)
{
    return g_enabled ? 1u : 0u;
}

static DWORD WINAPI InstallThread(LPVOID)
{
    Log("InstallThread start");
    for (int i = 0; i < 50 && !InstallSites(); ++i) {
        Sleep(100);
    }
    if (!g_ready) Log("InstallThread: gave up");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Stay OFF until InjectDragSelect --enable. Defaulting ON remapped the
        // local selection outline from palette 250 (bright green) to 245
        // (gold/brown) whenever this DLL was merely loaded — including on
        // "--disable" injects from the watcher / Studio.
        InterlockedExchange(&g_enabled, 0);
        HANDLE thread = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        RemovePatch();
    }
    return TRUE;
}
