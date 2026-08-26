// Injects ChatNameColorHook.dll into Warcraft II.exe and sets enabled state.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

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

FARPROC RemoteGetProc(HANDLE process, DWORD pid, const wchar_t* moduleName, const char* exportName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    HMODULE remoteBase = nullptr;
    wchar_t localPath[MAX_PATH]{};
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0) {
                remoteBase = me.hModule;
                wcsncpy_s(localPath, me.szExePath, _TRUNCATE);
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    if (!remoteBase || !localPath[0]) return nullptr;

    HMODULE local = LoadLibraryExW(localPath, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) return nullptr;
    FARPROC localProc = GetProcAddress(local, exportName);
    if (!localProc) {
        // x86 stdcall exports are decorated as _Name@N
        char decorated[128]{};
        sprintf_s(decorated, "_%s@4", exportName);
        localProc = GetProcAddress(local, decorated);
    }
    if (!localProc) {
        FreeLibrary(local);
        return nullptr;
    }
    const auto offset = reinterpret_cast<uintptr_t>(localProc) - reinterpret_cast<uintptr_t>(local);
    FreeLibrary(local);
    return reinterpret_cast<FARPROC>(reinterpret_cast<uintptr_t>(remoteBase) + offset);
}

int InjectAndSet(const std::wstring& dllPath, bool enable, int timestamps, int history, int blacksmith)
{
    const DWORD pid = FindPidByName(L"Warcraft II.exe");
    if (!pid) {
        std::fwprintf(stderr, L"Warcraft II.exe is not running.\n");
        return 2;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        std::fwprintf(stderr, L"OpenProcess failed (%lu).\n", GetLastError());
        return 3;
    }

    if (!ModuleLoaded(pid, L"ChatNameColorHook.dll")) {
        const size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote) {
            CloseHandle(process);
            std::fwprintf(stderr, L"VirtualAllocEx failed (%lu).\n", GetLastError());
            return 4;
        }
        if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            CloseHandle(process);
            std::fwprintf(stderr, L"WriteProcessMemory failed (%lu).\n", GetLastError());
            return 5;
        }

        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLib, remote, 0, nullptr);
        if (!thread) {
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            CloseHandle(process);
            std::fwprintf(stderr, L"CreateRemoteThread(LoadLibraryW) failed (%lu).\n", GetLastError());
            return 6;
        }
        WaitForSingleObject(thread, 15000);
        DWORD exitCode = 0;
        GetExitCodeThread(thread, &exitCode);
        CloseHandle(thread);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        if (!exitCode) {
            CloseHandle(process);
            std::fwprintf(stderr, L"LoadLibraryW returned NULL in the game process.\n");
            return 7;
        }
        Sleep(100);
    }

    FARPROC setEnabled = RemoteGetProc(process, pid, L"ChatNameColorHook.dll", "ChatNameColor_SetEnabled");
    FARPROC isReady = RemoteGetProc(process, pid, L"ChatNameColorHook.dll", "ChatNameColor_IsReady");
    if (!setEnabled) {
        CloseHandle(process);
        std::fwprintf(stderr, L"Could not resolve ChatNameColor_SetEnabled.\n");
        return 8;
    }

    if (isReady) {
        DWORD ready = 0;
        for (int attempt = 0; attempt < 20 && !ready; ++attempt) {
            HANDLE readyThread = CreateRemoteThread(
                process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(isReady), nullptr, 0, nullptr);
            if (!readyThread) break;
            WaitForSingleObject(readyThread, 5000);
            GetExitCodeThread(readyThread, &ready);
            CloseHandle(readyThread);
            if (!ready) Sleep(50);
        }
        if (!ready) {
            CloseHandle(process);
            std::fwprintf(stderr, L"chat-name-color pattern was not found in this game build.\n");
            return 9;
        }
    }

    HANDLE setThread = CreateRemoteThread(
        process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(setEnabled),
        reinterpret_cast<LPVOID>(static_cast<uintptr_t>(enable ? 1 : 0)), 0, nullptr);
    if (!setThread) {
        CloseHandle(process);
        std::fwprintf(stderr, L"CreateRemoteThread(SetEnabled) failed (%lu).\n", GetLastError());
        return 10;
    }
    WaitForSingleObject(setThread, 5000);
    CloseHandle(setThread);

    // Timestamp mod flag (separate feature). -1 = leave as-is.
    if (timestamps >= 0) {
        FARPROC setStamps = RemoteGetProc(process, pid, L"ChatNameColorHook.dll",
                                          "ChatNameColor_SetTimestamps");
        if (setStamps) {
            HANDLE stampThread = CreateRemoteThread(
                process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(setStamps),
                reinterpret_cast<LPVOID>(static_cast<uintptr_t>(timestamps ? 1 : 0)), 0, nullptr);
            if (stampThread) {
                WaitForSingleObject(stampThread, 5000);
                CloseHandle(stampThread);
            }
        }
    }

    // Chat history recall flag (PageUp/PageDown). -1 = leave as-is.
    if (history >= 0) {
        FARPROC setHistory = RemoteGetProc(process, pid, L"ChatNameColorHook.dll",
                                           "ChatNameColor_SetHistory");
        if (setHistory) {
            HANDLE historyThread = CreateRemoteThread(
                process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(setHistory),
                reinterpret_cast<LPVOID>(static_cast<uintptr_t>(history ? 1 : 0)), 0, nullptr);
            if (historyThread) {
                WaitForSingleObject(historyThread, 5000);
                CloseHandle(historyThread);
            }
        }
    }

    // Blacksmith work-complete chat. -1 = leave as-is.
    if (blacksmith >= 0) {
        FARPROC setBlacksmith = RemoteGetProc(process, pid, L"ChatNameColorHook.dll",
                                              "ChatNameColor_SetBlacksmithWorkComplete");
        if (setBlacksmith) {
            HANDLE blacksmithThread = CreateRemoteThread(
                process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(setBlacksmith),
                reinterpret_cast<LPVOID>(static_cast<uintptr_t>(blacksmith ? 1 : 0)), 0, nullptr);
            if (blacksmithThread) {
                WaitForSingleObject(blacksmithThread, 5000);
                CloseHandle(blacksmithThread);
            }
        }
    }
    CloseHandle(process);

    std::fwprintf(stdout,
        L"Chat name colors %s. Log: %%TEMP%%\\war2_chat_name_color_hook.log\n",
        enable ? L"enabled" : L"disabled");
    return 0;
}

std::wstring SiblingPath(const wchar_t* fileName)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) full.resize(slash + 1);
    full += fileName;
    return full;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool enable = true;
    int timestamps = -1; // -1 = don't touch
    int history = -1;    // -1 = don't touch
    int blacksmith = -1; // -1 = don't touch
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--disable") == 0 || _wcsicmp(argv[i], L"0") == 0) enable = false;
        if (_wcsicmp(argv[i], L"--enable") == 0 || _wcsicmp(argv[i], L"1") == 0) enable = true;
        if (_wcsicmp(argv[i], L"--timestamps") == 0 && i + 1 < argc) {
            timestamps = (_wcsicmp(argv[i + 1], L"1") == 0) ? 1 : 0;
            ++i;
        }
        if (_wcsicmp(argv[i], L"--history") == 0 && i + 1 < argc) {
            history = (_wcsicmp(argv[i + 1], L"1") == 0) ? 1 : 0;
            ++i;
        }
        if (_wcsicmp(argv[i], L"--blacksmith") == 0 && i + 1 < argc) {
            blacksmith = (_wcsicmp(argv[i + 1], L"1") == 0) ? 1 : 0;
            ++i;
        }
    }

    const std::wstring dllPath = SiblingPath(L"ChatNameColorHook.dll");
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"Missing DLL: %s\n", dllPath.c_str());
        return 1;
    }
    return InjectAndSet(dllPath, enable, timestamps, history, blacksmith);
}
