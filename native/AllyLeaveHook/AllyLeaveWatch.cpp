// Background watcher: when Extra features are ON in extra-features.json, inject
// the matching hooks into Warcraft II without keeping Modding Studio open.
// Registers itself in HKCU Run so Extra preferences survive reboot.
// Lobby map click: open warcraft2.site thumbnail only (never ShellExecute from the game).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdarg>
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

    char buf[8192]{};
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


bool WaitForJpgReady(const wchar_t* jpgPath, DWORD timeoutMs = 15000)
{
    if (!jpgPath || !jpgPath[0]) return false;
    const DWORD start = GetTickCount();
    ULONGLONG lastSize = 0;
    int stable = 0;
    while ((GetTickCount() - start) < timeoutMs) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW(jpgPath, GetFileExInfoStandard, &fad)) {
            Sleep(100);
            continue;
        }
        ULARGE_INTEGER sz;
        sz.HighPart = fad.nFileSizeHigh;
        sz.LowPart = fad.nFileSizeLow;
        if (sz.QuadPart < 2048) { // still writing / empty preview
            Sleep(100);
            continue;
        }
        if (sz.QuadPart == lastSize) {
            if (++stable >= 3) return true; // ~300ms unchanged
        } else {
            stable = 0;
            lastSize = sz.QuadPart;
        }
        Sleep(100);
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(jpgPath, GetFileExInfoStandard, &fad)) return false;
    ULARGE_INTEGER sz;
    sz.HighPart = fad.nFileSizeHigh;
    sz.LowPart = fad.nFileSizeLow;
    return sz.QuadPart >= 2048;
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



void WatchLog(const char* fmt, ...)
{
    char path[MAX_PATH]{};
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "war2_lobby_map_watch.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc(10, f);
    fclose(f);
}


// Read mapsPath from mod/studio-settings.json next to native/ (or TEMP override).
bool ReadMapsRootFromSettings(wchar_t* out, size_t outChars)
{
    if (!out || outChars == 0) return false;
    out[0] = 0;
    // Optional override written by Studio: %TEMP%/war2_maps_root.txt
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t overridePath[MAX_PATH]{};
    swprintf_s(overridePath, L"%swar2_maps_root.txt", tempDir);
    HANDLE h = CreateFileW(overridePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        wchar_t buf[MAX_PATH]{};
        DWORD read = 0;
        if (ReadFile(h, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr) && read >= sizeof(wchar_t)) {
            buf[read / sizeof(wchar_t)] = 0;
            if (buf[0] == 0xFEFF) wmemmove(buf, buf + 1, wcslen(buf + 1) + 1);
            for (wchar_t* p = buf; *p; ++p) if (*p == L'\r' || *p == L'\n') { *p = 0; break; }
            if (buf[0] && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) {
                wcsncpy_s(out, outChars, buf, _TRUNCATE);
                CloseHandle(h);
                return true;
            }
        }
        CloseHandle(h);
    }

    // mod/studio-settings.json (native -> ..)
    std::wstring settings = ModuleDir() + L"..\\studio-settings.json";
    h = CreateFileW(settings.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char json[4096]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(h, json, sizeof(json) - 1, &read, nullptr);
    CloseHandle(h);
    if (!ok || read == 0) return false;
    json[read] = 0;
    const char* key = strstr(json, "\"mapsPath\"");
    if (!key) key = strstr(json, "\"MapsPath\"");
    if (!key) return false;
    const char* colon = strchr(key, ':');
    if (!colon) return false;
    const char* q1 = strchr(colon, '"');
    if (!q1) return false;
    ++q1;
    const char* q2 = strchr(q1, '"');
    if (!q2 || q2 <= q1) return false;
    wchar_t path[MAX_PATH]{};
    int n = MultiByteToWideChar(CP_UTF8, 0, q1, (int)(q2 - q1), path, MAX_PATH - 1);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, q1, (int)(q2 - q1), path, MAX_PATH - 1);
    if (n <= 0) return false;
    path[n] = 0;
    // Unescape \\ in JSON
    wchar_t unesc[MAX_PATH]{};
    size_t j = 0;
    for (int i = 0; path[i] && j + 1 < MAX_PATH; ++i) {
        if (path[i] == L'\\' && path[i + 1] == L'\\') { unesc[j++] = L'\\'; ++i; }
        else unesc[j++] = path[i];
    }
    unesc[j] = 0;
    if (!unesc[0]) return false;
    wcsncpy_s(out, outChars, unesc, _TRUNCATE);
    return true;
}

void ExtractPudBasename(const wchar_t* pudPath, wchar_t* baseName, size_t baseChars)
{
    if (!baseName || baseChars == 0) return;
    baseName[0] = 0;
    if (!pudPath || !pudPath[0]) return;
    const wchar_t* slash = wcsrchr(pudPath, L'\\');
    if (!slash) slash = wcsrchr(pudPath, L'/');
    const wchar_t* file = slash ? slash + 1 : pudPath;
    wcsncpy_s(baseName, baseChars, file, _TRUNCATE);
    wchar_t* bdot = wcsrchr(baseName, L'.');
    if (bdot && _wcsicmp(bdot, L".pud") == 0) *bdot = 0;
}


bool ReadMapImagesDirFromSettings(wchar_t* out, size_t outChars)
{
    if (!out || outChars == 0) return false;
    out[0] = 0;
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t overridePath[MAX_PATH]{};
    swprintf_s(overridePath, L"%swar2_map_images_path.txt", tempDir);
    HANDLE h = CreateFileW(overridePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        wchar_t buf[MAX_PATH]{};
        DWORD read = 0;
        if (ReadFile(h, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr) && read >= sizeof(wchar_t)) {
            buf[read / sizeof(wchar_t)] = 0;
            if (buf[0] == 0xFEFF) wmemmove(buf, buf + 1, wcslen(buf + 1) + 1);
            for (wchar_t* p = buf; *p; ++p) if (*p == L'\r' || *p == L'\n') { *p = 0; break; }
            if (buf[0] && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) {
                wcsncpy_s(out, outChars, buf, _TRUNCATE);
                CloseHandle(h);
                return true;
            }
        }
        CloseHandle(h);
    }

    std::wstring settings = ModuleDir() + L"..\\studio-settings.json";
    h = CreateFileW(settings.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char json[8192]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(h, json, sizeof(json) - 1, &read, nullptr);
    CloseHandle(h);
    if (!ok || read == 0) return false;
    json[read] = 0;
    const char* key = strstr(json, "\"mapImagesPath\"");
    if (!key) key = strstr(json, "\"MapImagesPath\"");
    if (!key) return false;
    const char* colon = strchr(key, ':');
    if (!colon) return false;
    const char* q1 = strchr(colon, '"');
    if (!q1) return false;
    ++q1;
    const char* q2 = strchr(q1, '"');
    if (!q2 || q2 <= q1) return false;
    wchar_t path[MAX_PATH]{};
    int n = MultiByteToWideChar(CP_UTF8, 0, q1, (int)(q2 - q1), path, MAX_PATH - 1);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, q1, (int)(q2 - q1), path, MAX_PATH - 1);
    if (n <= 0) return false;
    path[n] = 0;
    wchar_t unesc[MAX_PATH]{};
    size_t j = 0;
    for (int i = 0; path[i] && j + 1 < MAX_PATH; ++i) {
        if (path[i] == L'\\' && path[i + 1] == L'\\') { unesc[j++] = L'\\'; ++i; }
        else unesc[j++] = path[i];
    }
    unesc[j] = 0;
    if (!unesc[0]) return false;
    wcsncpy_s(out, outChars, unesc, _TRUNCATE);
    return true;
}

// Local map_images only (Studio Download/Update). No live site fetch on click.
bool TryPreviewFile(const wchar_t* path, wchar_t* outPath, size_t outChars)
{
    if (!path || !path[0] || !outPath || outChars == 0) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return false;
    ULARGE_INTEGER sz;
    sz.HighPart = fad.nFileSizeHigh;
    sz.LowPart = fad.nFileSizeLow;
    if (sz.QuadPart < 512) return false;
    wcsncpy_s(outPath, outChars, path, _TRUNCATE);
    return true;
}

bool ResolveLocalMapImage(const wchar_t* pudPath, wchar_t* outPath, size_t outChars)
{
    if (!outPath || outChars == 0) return false;
    outPath[0] = 0;
    wchar_t baseName[MAX_PATH]{};
    ExtractPudBasename(pudPath, baseName, MAX_PATH);
    if (!baseName[0]) return false;

    // Prefer explicit map-images folder (Studio Paths setting).
    wchar_t imagesDir[MAX_PATH]{};
    if (ReadMapImagesDirFromSettings(imagesDir, MAX_PATH)) {
        wchar_t cand[MAX_PATH]{};
        swprintf_s(cand, L"%s\\%s.jpg", imagesDir, baseName);
        if (TryPreviewFile(cand, outPath, outChars)) {
            WatchLog("open: map_images path jpg hit %ls", cand);
            return true;
        }
        swprintf_s(cand, L"%s\\map_image_not_found.png", imagesDir);
        if (TryPreviewFile(cand, outPath, outChars)) {
            WatchLog("open: map_images path missing for %ls - template", baseName);
            return true;
        }
    }

    wchar_t mapsRoot[MAX_PATH]{};
    if (!ReadMapsRootFromSettings(mapsRoot, MAX_PATH)) {
        // Infer Maps root from pud path (folder named Maps).
        wcsncpy_s(mapsRoot, pudPath ? pudPath : L"", _TRUNCATE);
        wchar_t* mapsTok = nullptr;
        for (wchar_t* q = mapsRoot; *q; ++q) {
            if ((q[0] == L'\\' || q[0] == L'/') &&
                (_wcsnicmp(q + 1, L"Maps\\", 5) == 0 || _wcsnicmp(q + 1, L"maps\\", 5) == 0 ||
                 _wcsnicmp(q + 1, L"Maps/", 5) == 0 || _wcsnicmp(q + 1, L"maps/", 5) == 0)) {
                mapsTok = q + 1;
                break;
            }
        }
        if (mapsTok) {
            mapsTok[4] = 0; // keep "Maps"
        } else {
            mapsRoot[0] = 0;
        }
    }

    if (mapsRoot[0]) {
        wchar_t cand[MAX_PATH]{};
        swprintf_s(cand, L"%s\\map_images\\%s.jpg", mapsRoot, baseName);
        if (TryPreviewFile(cand, outPath, outChars)) {
            WatchLog("open: map_images jpg hit %ls", cand);
            return true;
        }
        swprintf_s(cand, L"%s\\map_images\\map_image_not_found.png", mapsRoot);
        if (TryPreviewFile(cand, outPath, outChars)) {
            WatchLog("open: map_images missing for %ls - template", baseName);
            return true;
        }
    }

    // Mod asset fallback next to native/
    std::wstring asset = ModuleDir() + L"..\\assets\\map_image_not_found.png";
    if (TryPreviewFile(asset.c_str(), outPath, outChars)) {
        WatchLog("open: template asset for %ls", baseName);
        return true;
    }
    WatchLog("open: no map_images and no template for %ls", baseName);
    return false;
}

// Legacy site fetch kept for optional tooling; lobby click no longer calls this.
bool FetchWarcraft2SiteThumbnail(const wchar_t* pudPath, wchar_t* outPath, size_t outChars)
{
    if (!pudPath || !pudPath[0] || !outPath || outChars == 0) return false;
    outPath[0] = 0;

    const wchar_t* slash = wcsrchr(pudPath, L'\\');
    if (!slash) slash = wcsrchr(pudPath, L'/');
    const wchar_t* file = slash ? slash + 1 : pudPath;
    wchar_t baseName[MAX_PATH]{};
    wcsncpy_s(baseName, file, _TRUNCATE);
    wchar_t* bdot = wcsrchr(baseName, L'.');
    if (bdot && _wcsicmp(bdot, L".pud") == 0) *bdot = 0;
    if (!baseName[0]) return false;

    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t previewDir[MAX_PATH]{};
    swprintf_s(previewDir, L"%sWar2MapPreviews", tempDir);
    CreateDirectoryW(previewDir, nullptr);
    swprintf_s(outPath, outChars, L"%s\\%s.webp", previewDir, baseName);

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(outPath, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER sz;
        sz.HighPart = fad.nFileSizeHigh;
        sz.LowPart = fad.nFileSizeLow;
        if (sz.QuadPart >= 2048) {
            WatchLog("open: site thumb cache hit %ls", outPath);
            return true;
        }
    }

    const std::wstring script = ModuleDir() + L"Fetch-Warcraft2Thumbnail.ps1";
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WatchLog("open: Fetch-Warcraft2Thumbnail.ps1 missing");
        return false;
    }

    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"";
    cmd += script;
    cmd += L"\" -MapBaseName \"";
    cmd += baseName;
    cmd += L"\" -OutputPath \"";
    cmd += outPath;
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
        WatchLog("open: site thumb spawn failed");
        return false;
    }
    WaitForSingleObject(pi.hProcess, 45000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0 || GetFileAttributesW(outPath) == INVALID_FILE_ATTRIBUTES) {
        WatchLog("open: site thumb miss for %ls", baseName);
        outPath[0] = 0;
        return false;
    }
    WatchLog("open: site thumb ok %ls", outPath);
    return true;
}

bool ResolvePudViaUi(wchar_t* outPud, size_t outChars)
{
    if (!outPud || outChars == 0) return false;
    outPud[0] = 0;
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t resultPath[MAX_PATH]{};
    swprintf_s(resultPath, L"%swar2_lobby_map_ui_result.txt", tempDir);
    DeleteFileW(resultPath);

    const std::wstring script = ModuleDir() + L"Resolve-LobbyMapFromUi.ps1";
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"";
    cmd += script;
    cmd += L"\" -ResultPath \"";
    cmd += resultPath;
    cmd += L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, ModuleDir().c_str(), &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    HANDLE h = CreateFileW(resultPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[MAX_PATH * 3]{};
    DWORD nread = 0;
    const BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &nread, nullptr);
    CloseHandle(h);
    DeleteFileW(resultPath);
    if (!ok || nread == 0) return false;
    buf[nread] = 0;
    while (nread > 0 && (buf[nread - 1] == '\n' || buf[nread - 1] == '\r')) buf[--nread] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, buf, -1, outPud, static_cast<int>(outChars)))
        return false;
    return outPud[0] != 0 && GetFileAttributesW(outPud) != INVALID_FILE_ATTRIBUTES;
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
    // Strip UTF-16 BOM if a helper script wrote the request file.
    if (pud[0] == 0xFEFF) {
        wmemmove(pud, pud + 1, wcslen(pud + 1) + 1);
    }
    for (wchar_t* p = pud; *p; ++p) {
        if (*p == L'\r' || *p == L'\n') { *p = 0; break; }
    }
    if (!pud[0]) return;

    if (_wcsicmp(pud, L"__UI_RESOLVE__") == 0) {
        WatchLog("open: UI resolve requested");
        wchar_t resolved[MAX_PATH]{};
        if (!ResolvePudViaUi(resolved, MAX_PATH)) {
            WatchLog("open: UI resolve failed (Remastered often has no UIA Map row)");
            return;
        }
        wcsncpy_s(pud, resolved, _TRUNCATE);
        WatchLog("open: UI resolve ok %ls", pud);
    } else if (GetFileAttributesW(pud) == INVALID_FILE_ATTRIBUTES) {
        WatchLog("open: pud missing/invalid (still try map_images by name) %ls", pud);
    } else {
        WatchLog("open: pud ok %ls", pud);
    }

    // Local map_images only (Studio Maps Download/Update). Template if missing.
    wchar_t preview[MAX_PATH]{};
    if (!ResolveLocalMapImage(pud, preview, MAX_PATH)) {
        WatchLog("open: no local map image / template");
        return;
    }
    if (!WaitForJpgReady(preview)) {
        WatchLog("open: preview not ready %ls", preview);
        return;
    }

    s_lastOpenTick = now;
    WatchLog("open: ShellExecute %ls", preview);
    ShellExecuteW(nullptr, L"open", preview, nullptr, nullptr, SW_SHOWNORMAL);
}

void TryReapplySavedColors()
{
    // Best-effort: restore player colors after reboot without opening Studio.
    const std::wstring modDir = ModuleDir() + L"..\\";
    const std::wstring script = modDir + L"Apply-PlayerColors.ps1";
    const std::wstring colors = modDir + L"player-colors.json";
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesW(colors.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    std::wstring gameRoot = ModuleDir();
    if (!gameRoot.empty() && (gameRoot.back() == L'\\' || gameRoot.back() == L'/'))
        gameRoot.pop_back();
    for (int i = 0; i < 4; ++i) {
        const size_t slash = gameRoot.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return;
        gameRoot.resize(slash);
    }

    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"";
    cmd += script;
    cmd += L"\" -ApplySavedConfigOnly -GameRootPath \"";
    cmd += gameRoot;
    cmd += L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, modDir.c_str(), &si, &pi)) {
        return;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
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
    TryReapplySavedColors();
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
        return 0;
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
