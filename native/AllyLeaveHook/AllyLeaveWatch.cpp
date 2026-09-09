// Background watcher: when Extra features are ON in extra-features.json, inject
// the matching hooks into Warcraft II without keeping Modding Studio open.
// Registers itself in HKCU Run so Extra preferences survive reboot.
// Lobby map click: open the map .jpg externally (never ShellExecute from the game).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\War2AllyLeaveWatch";
constexpr wchar_t kQuitEventName[] = L"Local\\War2AllyLeaveWatchQuit";
constexpr wchar_t kOpenMapEventName[] = L"Local\\War2LobbyMapOpenRequest";
constexpr wchar_t kRunValueName[] = L"War2AllyLeaveWatch";
constexpr DWORD kPollMs = 2000;
constexpr DWORD kOpenPollMs = 250;

struct ExtraFlags {
    bool allyLeave = false;
    bool pauseChat = false;
    bool dragSelect = false;
    bool chatNameColor = false;
    bool humanLeave = false;
    bool unitColor = false;
    bool chatTimestamps = false;
    bool chatHistory = false;
    bool mpLobbyChatScrollFix = false;
    bool endGameObserve = false;
    bool allianceTeamNumbers = false;
    bool computerAnnihilatedChat = false;
    bool blacksmithWorkComplete = false;
    bool networkMonitor = false;
    bool lobbyMapClick = false;
    bool Any() const {
        return allyLeave || pauseChat || dragSelect || chatNameColor || unitColor ||
               chatTimestamps || chatHistory || mpLobbyChatScrollFix || endGameObserve ||
               allianceTeamNumbers ||
               computerAnnihilatedChat || blacksmithWorkComplete || networkMonitor ||
               lobbyMapClick;
    }
    bool ChatDllWanted() const {
        return chatNameColor || humanLeave || computerAnnihilatedChat ||
               chatTimestamps || chatHistory || blacksmithWorkComplete ||
               mpLobbyChatScrollFix;
    }
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

    flags.humanLeave = ReadJsonBool(buf, "AllyLeaveMarkHumans");
    flags.allianceTeamNumbers = ReadJsonBool(buf, "AllianceTeamNumbers");
    flags.computerAnnihilatedChat = ReadJsonBool(buf, "ComputerAnnihilatedChat");
    flags.allyLeave =
        ReadJsonBool(buf, "AllyLeaveMarkComputers") ||
        flags.humanLeave ||
        ReadJsonBool(buf, "AllyLeaveRedNames") ||
        flags.allianceTeamNumbers ||
        flags.computerAnnihilatedChat;
    flags.pauseChat = ReadJsonBool(buf, "ChatDuringPauseScreen");
    flags.dragSelect = ReadJsonBool(buf, "DragSelectColorEnabled");
    flags.chatNameColor = ReadJsonBool(buf, "ChatColoredNames");
    flags.unitColor = ReadJsonBool(buf, "UnitSpriteColors");
    flags.chatTimestamps = ReadJsonBool(buf, "ChatTimestamps");
    flags.chatHistory = ReadJsonBool(buf, "ChatHistory");
    flags.mpLobbyChatScrollFix = ReadJsonBool(buf, "MpLobbyChatScrollFix");
    flags.blacksmithWorkComplete = ReadJsonBool(buf, "BlacksmithWorkCompleteChat");
    flags.endGameObserve = ReadJsonBool(buf, "EndGameObserve");
    flags.networkMonitor = ReadJsonBool(buf, "NetworkMonitor");
    flags.lobbyMapClick = ReadJsonBool(buf, "LobbyMapClickOpen");
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

bool RunInjector(const wchar_t* exeName, bool enable, const wchar_t* extraArgs = nullptr)
{
    const std::wstring injector = ModuleDir() + exeName;
    if (GetFileAttributesW(injector.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    std::wstring cmd = L"\"" + injector + L"\" " + (enable ? L"--enable" : L"--disable");
    if (extraArgs && extraArgs[0]) {
        cmd += L" ";
        cmd += extraArgs;
    }
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

bool RunExportPudJpg(const wchar_t* pudPath, const wchar_t* jpgPath)
{
    if (!pudPath || !pudPath[0] || !jpgPath || !jpgPath[0]) return false;
    const std::wstring script = ModuleDir() + L"Export-PudJpg.ps1";
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"";
    cmd += script;
    cmd += L"\" -PudPath \"";
    cmd += pudPath;
    cmd += L"\" -OutputPath \"";
    cmd += jpgPath;
    cmd += L"\"";

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
    WaitForSingleObject(pi.hProcess, 90000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return GetFileAttributesW(jpgPath) != INVALID_FILE_ATTRIBUTES;
}

void ProcessLobbyMapOpenRequest()
{
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t reqPath[MAX_PATH]{};
    swprintf_s(reqPath, L"%swar2_lobby_map_open.txt", tempDir);
    if (GetFileAttributesW(reqPath) == INVALID_FILE_ATTRIBUTES) return;

    static DWORD s_lastOpenTick = 0;
    const DWORD now = GetTickCount();
    if (s_lastOpenTick != 0 && (now - s_lastOpenTick) < 2000) {
        // Drop burst opens (startup spam / double inject).
        DeleteFileW(reqPath);
        return;
    }

    HANDLE h = CreateFileW(reqPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    wchar_t pud[MAX_PATH]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(h, pud, sizeof(pud) - sizeof(wchar_t), &read, nullptr);
    CloseHandle(h);
    DeleteFileW(reqPath);
    if (!ok || read < sizeof(wchar_t)) return;
    pud[read / sizeof(wchar_t)] = 0;
    for (wchar_t* p = pud; *p; ++p) {
        if (*p == L'\r' || *p == L'\n') { *p = 0; break; }
    }
    if (!pud[0] || GetFileAttributesW(pud) == INVALID_FILE_ATTRIBUTES) return;

    // Sibling .jpg next to the .pud (create if missing).
    wchar_t jpg[MAX_PATH]{};
    wcsncpy_s(jpg, pud, _TRUNCATE);
    wchar_t* dot = wcsrchr(jpg, L'.');
    if (dot) wcscpy_s(dot, MAX_PATH - (dot - jpg), L".jpg");
    else wcsncat_s(jpg, L".jpg", _TRUNCATE);

    if (GetFileAttributesW(jpg) == INVALID_FILE_ATTRIBUTES) {
        if (!RunExportPudJpg(pud, jpg)) {
            wchar_t baseName[MAX_PATH]{};
            const wchar_t* slash = wcsrchr(pud, L'\\');
            if (!slash) slash = wcsrchr(pud, L'/');
            wcsncpy_s(baseName, slash ? slash + 1 : pud, _TRUNCATE);
            wchar_t* bdot = wcsrchr(baseName, L'.');
            if (bdot) *bdot = 0;
            wchar_t previewDir[MAX_PATH]{};
            swprintf_s(previewDir, L"%sWar2MapPreviews", tempDir);
            CreateDirectoryW(previewDir, nullptr);
            swprintf_s(jpg, L"%s\\%s.jpg", previewDir, baseName);
            if (GetFileAttributesW(jpg) == INVALID_FILE_ATTRIBUTES)
                RunExportPudJpg(pud, jpg);
        }
    }

    if (GetFileAttributesW(jpg) == INVALID_FILE_ATTRIBUTES) return;

    s_lastOpenTick = now;
    ShellExecuteW(nullptr, L"open", jpg, nullptr, nullptr, SW_SHOWNORMAL);
}

void WatchLoop(HANDLE quitEvent)
{
    // Drop any leftover open request from a previous crash/session.
    {
        wchar_t tempDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempDir);
        wchar_t stale[MAX_PATH]{};
        swprintf_s(stale, L"%swar2_lobby_map_open.txt", tempDir);
        DeleteFileW(stale);
        swprintf_s(stale, L"%swar2_lobby_map_open.tmp", tempDir);
        DeleteFileW(stale);
    }

    DWORD allyInjectedPid = 0;
    DWORD pauseInjectedPid = 0;
    DWORD dragInjectedPid = 0;
    DWORD chatNameInjectedPid = 0;
    DWORD unitColorInjectedPid = 0;
    DWORD observeInjectedPid = 0;
    DWORD networkInjectedPid = 0;
    DWORD lobbyMapInjectedPid = 0;
    ExtraFlags last = ReadExtraFlags();
    SetStartup(last.Any());
    DWORD lastInjectTick = 0;

    HANDLE openEvent = CreateEventW(nullptr, FALSE, FALSE, kOpenMapEventName);

    for (;;) {
        if (quitEvent && WaitForSingleObject(quitEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        if (openEvent && WaitForSingleObject(openEvent, 0) == WAIT_OBJECT_0)
            ProcessLobbyMapOpenRequest();
        else
            ProcessLobbyMapOpenRequest();

        const DWORD now = GetTickCount();
        const bool dueInject = (now - lastInjectTick) >= kPollMs;
        if (!dueInject) {
            if (quitEvent) {
                HANDLE waits[2] = { quitEvent, openEvent ? openEvent : quitEvent };
                const DWORD n = openEvent ? 2u : 1u;
                const DWORD wr = WaitForMultipleObjects(n, waits, FALSE, kOpenPollMs);
                if (wr == WAIT_OBJECT_0) break;
                if (openEvent && wr == WAIT_OBJECT_0 + 1)
                    ProcessLobbyMapOpenRequest();
            } else {
                Sleep(kOpenPollMs);
            }
            continue;
        }
        lastInjectTick = now;

        const ExtraFlags flags = ReadExtraFlags();
        const DWORD pid = FindWarcraftPid();

        if (flags.allyLeave != last.allyLeave || flags.pauseChat != last.pauseChat ||
            flags.dragSelect != last.dragSelect || flags.chatNameColor != last.chatNameColor ||
            flags.humanLeave != last.humanLeave || flags.unitColor != last.unitColor ||
            flags.chatTimestamps != last.chatTimestamps || flags.chatHistory != last.chatHistory ||
            flags.blacksmithWorkComplete != last.blacksmithWorkComplete ||
            flags.endGameObserve != last.endGameObserve ||
            flags.networkMonitor != last.networkMonitor ||
            flags.allianceTeamNumbers != last.allianceTeamNumbers ||
            flags.lobbyMapClick != last.lobbyMapClick) {
            SetStartup(flags.Any());
            if (!flags.allyLeave && pid != 0) {
                RunInjector(L"InjectAllyLeave.exe", false);
                allyInjectedPid = 0;
            }
            if (!flags.pauseChat && pid != 0) {
                RunInjector(L"InjectPauseChat.exe", false);
                pauseInjectedPid = 0;
            }
            if (!flags.dragSelect && pid != 0 && (last.dragSelect || dragInjectedPid != 0)) {
                RunInjector(L"InjectDragSelect.exe", false);
                dragInjectedPid = 0;
            }
            if (!flags.ChatDllWanted() && pid != 0) {
                RunInjector(L"InjectChatNameColor.exe", false, L"--timestamps 0 --history 0 --blacksmith 0 --lobby-scroll 0");
            }
            if (!flags.unitColor && pid != 0) {
                RunInjector(L"InjectUnitColor.exe", false);
                unitColorInjectedPid = 0;
            }
            if (!flags.endGameObserve && pid != 0) {
                RunInjector(L"InjectObserve.exe", false);
                observeInjectedPid = 0;
            }
            if (!flags.networkMonitor && pid != 0) {
                RunInjector(L"InjectNetworkMonitor.exe", false);
                networkInjectedPid = 0;
            }
            if (!flags.lobbyMapClick && pid != 0) {
                RunInjector(L"InjectLobbyMapClick.exe", false);
                lobbyMapInjectedPid = 0;
            }
            chatNameInjectedPid = 0;
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
        if (flags.ChatDllWanted() && pid != 0 && pid != chatNameInjectedPid) {
            std::wstring chatArgs = flags.chatTimestamps ? L"--timestamps 1" : L"--timestamps 0";
            chatArgs += flags.chatHistory ? L" --history 1" : L" --history 0";
            chatArgs += flags.blacksmithWorkComplete ? L" --blacksmith 1" : L" --blacksmith 0";
            chatArgs += flags.mpLobbyChatScrollFix ? L" --lobby-scroll 1" : L" --lobby-scroll 0";
            if (RunInjector(L"InjectChatNameColor.exe", flags.chatNameColor, chatArgs.c_str())) {
                chatNameInjectedPid = pid;
            }
        }
        if (flags.unitColor && pid != 0 && pid != unitColorInjectedPid) {
            if (RunInjector(L"InjectUnitColor.exe", true)) unitColorInjectedPid = pid;
        }
        if (flags.endGameObserve && pid != 0 && pid != observeInjectedPid) {
            if (RunInjector(L"InjectObserve.exe", true)) observeInjectedPid = pid;
        }
        if (flags.networkMonitor && pid != 0 && pid != networkInjectedPid) {
            if (RunInjector(L"InjectNetworkMonitor.exe", true)) networkInjectedPid = pid;
        }
        if (flags.lobbyMapClick && pid != 0 && pid != lobbyMapInjectedPid) {
            if (RunInjector(L"InjectLobbyMapClick.exe", true)) lobbyMapInjectedPid = pid;
        }
        if (pid == 0) {
            allyInjectedPid = 0;
            pauseInjectedPid = 0;
            dragInjectedPid = 0;
            chatNameInjectedPid = 0;
            unitColorInjectedPid = 0;
            observeInjectedPid = 0;
            networkInjectedPid = 0;
            lobbyMapInjectedPid = 0;
        }
    }

    if (openEvent) CloseHandle(openEvent);
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
