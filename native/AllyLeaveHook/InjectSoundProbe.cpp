// Injects SoundProbe.dll (research) into Warcraft II.exe via LoadLibrary.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

namespace {

DWORD FindPidByName(const wchar_t* name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool ModuleLoaded(DWORD pid, const wchar_t* moduleName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

} // namespace

int wmain()
{
    const DWORD pid = FindPidByName(L"Warcraft II.exe");
    if (!pid) {
        wprintf(L"Warcraft II.exe not running\n");
        return 1;
    }
    if (ModuleLoaded(pid, L"SoundProbe.dll")) {
        wprintf(L"SoundProbe.dll already loaded\n");
        return 0;
    }

    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (slash) slash[1] = 0;
    wcscat_s(dllPath, L"SoundProbe.dll");

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        wprintf(L"OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }

    const size_t bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(process, remote, dllPath, bytes, nullptr)) {
        wprintf(L"write remote failed: %lu\n", GetLastError());
        CloseHandle(process);
        return 1;
    }

    auto* loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        wprintf(L"CreateRemoteThread failed: %lu\n", GetLastError());
        CloseHandle(process);
        return 1;
    }
    WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    wprintf(L"SoundProbe injected\n");
    return 0;
}
