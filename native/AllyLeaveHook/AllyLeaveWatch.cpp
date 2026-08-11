// Background watcher: when Extra features are ON in extra-features.json, inject
// the matching hooks into Warcraft II without keeping Modding Studio open.
// Registers itself in HKCU Run so Extra preferences survive reboot.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\War2AllyLeaveWatch";
constexpr wchar_t kQuitEventName[] = L"Local\\War2AllyLeaveWatchQuit";
constexpr wchar_t kRunValueName[] = L"War2AllyLeaveWatch";
constexpr DWORD kPollMs = 2000;

struct ExtraFlags {
    bool allyLeave = false;
    bool pauseChat = false;
    bool dragSelect = false;
    bool chatNameColor = false;
    bool Any() const { return allyLeave || pauseChat || dragSelect || chatNameColor; }
};

std::wstring ModuleDir()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) full.resize(slash + 1);
    return full;
}

std::wstring ModuleExePath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring ExtraConfigPath()
{
    // native\ -> mod\extra-features.json
    std::wstring dir = ModuleDir() + L"..\\extra-features.json";
    wchar_t full[MAX_PATH]{};
    if (GetFullPathNameW(dir.c_str(), MAX_PATH, full, nullptr) == 0) return dir;
    return full;
}

bool ReadJsonBool(const char* buf, const char* key)
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

ExtraFlags ReadExtraFlags()
{
    ExtraFlags flags{};
    const std::wstring path = ExtraConfigPath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return flags;

    char buf[1024]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) return flags;

    flags.allyLeave =
        ReadJsonBool(buf, "AllyLeaveMarkComputers") ||
        ReadJsonBool(buf, "AllyLeaveMarkHumans") ||
        ReadJsonBool(buf, "AllyLeaveRedNames");
    flags.pauseChat = ReadJsonBool(buf, "ChatDuringPauseScreen");
    flags.dragSelect = ReadJsonBool(buf, "DragSelectColorEnabled");
    flags.chatNameColor = ReadJsonBool(buf, "ChatColoredNames");
    return flags;
}

DWORD FindWarcraftPid()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Warcraft II.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool RunInjector(const wchar_t* exeName, bool enable)
{
    const std::wstring injector = ModuleDir() + exeName;
    if (GetFileAttributesW(injector.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    std::wstring cmd = L"\"" + injector + L"\" " + (enable ? L"--enable" : L"--disable");
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, ModuleDir().c_str(), &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

bool SetStartup(bool enable)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    LONG rc = ERROR_SUCCESS;
    if (enable) {
        const std::wstring value = L"\"" + ModuleExePath() + L"\"";
        rc = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValueName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

void SignalQuit()
{
    HANDLE quit = CreateEventW(nullptr, TRUE, FALSE, kQuitEventName);
    if (quit) {
        SetEvent(quit);
        CloseHandle(quit);
    }
}

void WatchLoop(HANDLE quitEvent)
{
    DWORD allyInjectedPid = 0;
    DWORD pauseInjectedPid = 0;
    DWORD dragInjectedPid = 0;
    DWORD chatNameInjectedPid = 0;
    ExtraFlags last = ReadExtraFlags();
    SetStartup(last.Any());

    for (;;) {
        if (quitEvent && WaitForSingleObject(quitEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        const ExtraFlags flags = ReadExtraFlags();
        const DWORD pid = FindWarcraftPid();

        if (flags.allyLeave != last.allyLeave || flags.pauseChat != last.pauseChat ||
            flags.dragSelect != last.dragSelect || flags.chatNameColor != last.chatNameColor) {
            SetStartup(flags.Any());
            if (!flags.allyLeave && pid != 0) {
                RunInjector(L"InjectAllyLeave.exe", false);
                allyInjectedPid = 0;
            }
            if (!flags.pauseChat && pid != 0) {
                RunInjector(L"InjectPauseChat.exe", false);
                pauseInjectedPid = 0;
            }
            if (!flags.dragSelect && pid != 0) {
                RunInjector(L"InjectDragSelect.exe", false);
                dragInjectedPid = 0;
            }
            if (!flags.chatNameColor && pid != 0) {
                RunInjector(L"InjectChatNameColor.exe", false);
                chatNameInjectedPid = 0;
            }
            last = flags;
        }

        if (flags.allyLeave && pid != 0 && pid != allyInjectedPid) {
            if (RunInjector(L"InjectAllyLeave.exe", true)) allyInjectedPid = pid;
        }
        if (flags.pauseChat && pid != 0 && pid != pauseInjectedPid) {
            if (RunInjector(L"InjectPauseChat.exe", true)) pauseInjectedPid = pid;
        }
        if (flags.dragSelect && pid != 0 && pid != dragInjectedPid) {
            if (RunInjector(L"InjectDragSelect.exe", true)) dragInjectedPid = pid;
        }
        if (flags.chatNameColor && pid != 0 && pid != chatNameInjectedPid) {
            if (RunInjector(L"InjectChatNameColor.exe", true)) chatNameInjectedPid = pid;
        }
        if (pid == 0) {
            allyInjectedPid = 0;
            pauseInjectedPid = 0;
            dragInjectedPid = 0;
            chatNameInjectedPid = 0;
        }

        if (quitEvent) {
            if (WaitForSingleObject(quitEvent, kPollMs) == WAIT_OBJECT_0) break;
        } else {
            Sleep(kPollMs);
        }
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int)
{
    const bool onlyInstall = cmdLine && wcsstr(cmdLine, L"--install-startup");
    const bool onlyUninstall = cmdLine && wcsstr(cmdLine, L"--uninstall-startup");
    const bool quitOnly = cmdLine && wcsstr(cmdLine, L"--quit");

    if (quitOnly) {
        SignalQuit();
        return 0;
    }

    if (onlyUninstall) {
        SetStartup(false);
        SignalQuit();
        return 0;
    }

    if (onlyInstall) {
        SetStartup(ReadExtraFlags().Any());
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    HANDLE quitEvent = CreateEventW(nullptr, TRUE, FALSE, kQuitEventName);
    if (quitEvent) ResetEvent(quitEvent);

    WatchLoop(quitEvent);

    if (quitEvent) CloseHandle(quitEvent);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
