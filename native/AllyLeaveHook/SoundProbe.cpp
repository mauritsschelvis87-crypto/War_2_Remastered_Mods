// SoundProbe.dll — research: logs every file-open of Gamesfx wavs so we can
// tell whether the game reloads unit sounds per play (redirect per click
// possible) or caches them once per session.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);

CreateFileWFn g_origW = nullptr;
CreateFileAFn g_origA = nullptr;

void Log(const char* fmt, ...)
{
    char path[MAX_PATH]{};
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "war2_sound_probe.log");
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

bool InterestingW(LPCWSTR p)
{
    if (!p) return false;
    // cheap case-insensitive scan for "gamesfx" / ".wav"
    wchar_t lower[512]{};
    wcsncpy_s(lower, p, _TRUNCATE);
    _wcslwr_s(lower);
    return wcsstr(lower, L"gamesfx") || wcsstr(lower, L".wav");
}

HANDLE WINAPI Hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                               LPSECURITY_ATTRIBUTES sa, DWORD disp,
                               DWORD flags, HANDLE tmpl)
{
    if (InterestingW(name))
        Log("openW %ls", name);
    return g_origW(name, access, share, sa, disp, flags, tmpl);
}

HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                               LPSECURITY_ATTRIBUTES sa, DWORD disp,
                               DWORD flags, HANDLE tmpl)
{
    if (name && (strstr(name, "amesfx") || strstr(name, ".wav") || strstr(name, ".WAV")))
        Log("openA %s", name);
    return g_origA(name, access, share, sa, disp, flags, tmpl);
}

// Patch the import table of the main exe module for kernel32 CreateFileW/A.
int PatchIat(const char* importName, void* hook, void** origOut)
{
    uint8_t* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int patched = 0;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        for (; origThunk->u1.AddressOfData; ++thunk, ++origThunk) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                base + origThunk->u1.AddressOfData);
            if (_stricmp(reinterpret_cast<const char*>(byName->Name), importName) != 0)
                continue;
            DWORD old = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old))
                continue;
            if (origOut && !*origOut)
                *origOut = reinterpret_cast<void*>(thunk->u1.Function);
            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            ++patched;
        }
    }
    return patched;
}

DWORD WINAPI InstallThread(LPVOID)
{
    const int w = PatchIat("CreateFileW", reinterpret_cast<void*>(&Hook_CreateFileW),
                           reinterpret_cast<void**>(&g_origW));
    const int a = PatchIat("CreateFileA", reinterpret_cast<void*>(&Hook_CreateFileA),
                           reinterpret_cast<void**>(&g_origA));
    if (!g_origW) g_origW = &CreateFileW;
    if (!g_origA) g_origA = &CreateFileA;
    Log("SoundProbe installed: CreateFileW=%d CreateFileA=%d sites", w, a);
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, &InstallThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
