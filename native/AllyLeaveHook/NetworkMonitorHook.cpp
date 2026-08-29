// Warcraft II Remastered — multiplayer lockstep frame-gap monitor (v1).
// Polls TimeNow while a match is active; large frame gaps indicate stalls.
// Studio tab reads %TEMP%\war2_network_monitor.log.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kPreferredImageBase = 0x00400000;
constexpr uint32_t kPreferredTimeNow = 0x00625940;
constexpr uint32_t kPreferredMsgInitFlag = 0x009B1798;
constexpr uint32_t kPreferredLobbyName0 = 0x0091626D;
constexpr uint32_t kPreferredSlotBase = 0x00916268;
constexpr uint32_t kPreferredInGameName0 = 0x0091ADA8;
constexpr size_t kLobbyNameStride = 0x26;
constexpr size_t kSlotStride = 0x26;
constexpr size_t kInGameNameStride = 0x38;
constexpr size_t kSeatCount = 8;

volatile LONG g_enabled = 0;
volatile LONG g_ready = 0;
volatile LONG g_stop = 0;

uint8_t* g_imageBase = nullptr;
uint8_t* g_msgInitFlag = nullptr;
uint8_t* g_slotBase = nullptr;
char* g_lobbyNames = nullptr;
char* g_inGameNames = nullptr;

using TimeNowFn = uint32_t(__cdecl*)();
TimeNowFn g_timeNow = nullptr;

volatile uint32_t g_lastGapMs = 0;
volatile uint32_t g_maxGapMs = 0;
volatile uint32_t g_frameGaps = 0;
uint32_t g_seatLastMs[kSeatCount]{};
uint32_t g_seatMaxMs[kSeatCount]{};

HANDLE g_monitorThread = nullptr;
HANDLE g_logThread = nullptr;
char g_logPath[MAX_PATH]{};

void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) {
        char temp[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(g_logPath, "%swar2_network_monitor.log", temp);
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

int ReadMsgInitFlag()
{
    if (!g_msgInitFlag) return 0;
    __try {
        return static_cast<int>(*g_msgInitFlag);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void ReadSeatName(int seat, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (seat < 0 || seat >= static_cast<int>(kSeatCount)) return;

    __try {
        if (g_inGameNames) {
            const char* name = g_inGameNames + seat * kInGameNameStride;
            if (name[0]) {
                strncpy_s(out, outLen, name, _TRUNCATE);
                return;
            }
        }
        if (g_lobbyNames) {
            const char* name = g_lobbyNames + seat * kLobbyNameStride;
            if (name[0]) {
                strncpy_s(out, outLen, name, _TRUNCATE);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
    }
}

bool LooksLikeComputerName(const char* name)
{
    if (!name || !name[0]) return false;
    if (_strnicmp(name, "Computer", 8) == 0) return true;
    if (_strnicmp(name, "Nation of ", 10) == 0) return true;
    if (_stricmp(name, "Alliance Traitors") == 0) return true;
    if (_stricmp(name, "Horde Traitors") == 0) return true;
    const size_t n = strlen(name);
    if (n >= 5 && _stricmp(name + (n - 5), " Clan") == 0) return true;
    return false;
}

bool IsHumanSeat(int seat)
{
    if (seat < 0 || seat >= static_cast<int>(kSeatCount)) return false;

    char name[64]{};
    ReadSeatName(seat, name, sizeof(name));
    if (!name[0] || LooksLikeComputerName(name)) return false;

    // Lobby-only: controller 2/4/6/7 = computer slot (in-match byte 2 can mean eliminated human).
    if (g_slotBase && ReadMsgInitFlag() != 1) {
        __try {
            const uint8_t controller = g_slotBase[seat * kSlotStride];
            if (controller == 2 || controller == 4 || controller == 6 || controller == 7) return false;
            if (controller == 5) return false; // empty
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return true;
}

bool SeatLooksActive(int seat)
{
    return IsHumanSeat(seat);
}

void ResetStats()
{
    g_lastGapMs = 0;
    g_maxGapMs = 0;
    g_frameGaps = 0;
    for (size_t i = 0; i < kSeatCount; ++i) {
        g_seatLastMs[i] = 0;
        g_seatMaxMs[i] = 0;
    }
}

void RecordGap(uint32_t gap)
{
    if (gap == 0) return;
    g_lastGapMs = gap;
    if (gap > g_maxGapMs) g_maxGapMs = gap;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_frameGaps));
    for (size_t i = 0; i < kSeatCount; ++i) {
        if (!IsHumanSeat(static_cast<int>(i))) continue;
        g_seatLastMs[i] = gap;
        if (gap > g_seatMaxMs[i]) g_seatMaxMs[i] = gap;
    }
}

void WriteSnapshot()
{
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "w") != 0 || !f) return;

    int active = 0;
    for (int seat = 0; seat < static_cast<int>(kSeatCount); ++seat) {
        if (!IsHumanSeat(seat)) continue;
        char name[64]{};
        ReadSeatName(seat, name, sizeof(name));
        ++active;
        fprintf(f, "SEAT %d name=%s lastMs=%u maxMs=%u\n",
                seat + 1, name, g_seatLastMs[seat], g_seatMaxMs[seat]);
    }
    fprintf(f, "SUMMARY humans=%d lastGap=%u maxGap=%u samples=%u\n",
            active, g_lastGapMs, g_maxGapMs, g_frameGaps);
    fclose(f);
}

DWORD WINAPI MonitorThreadProc(LPVOID)
{
    uint32_t lastTick = 0;
    int lastMatch = 0;

    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        if (!InterlockedCompareExchange(&g_enabled, 0, 0)) {
            lastTick = 0;
            lastMatch = 0;
            Sleep(200);
            continue;
        }

        const int inMatch = ReadMsgInitFlag();
        if (inMatch != lastMatch) {
            if (inMatch) {
                ResetStats();
                Log("MATCH start");
            } else {
                Log("MATCH end maxGap=%u", g_maxGapMs);
                lastTick = 0;
            }
            lastMatch = inMatch;
        }

        if (!inMatch || !g_timeNow) {
            Sleep(100);
            continue;
        }

        const uint32_t now = g_timeNow();
        if (lastTick != 0 && now >= lastTick) {
            RecordGap(now - lastTick);
        }
        lastTick = now;
        Sleep(1);
    }
    return 0;
}

DWORD WINAPI LogThreadProc(LPVOID)
{
    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        if (InterlockedCompareExchange(&g_enabled, 0, 0) && ReadMsgInitFlag() == 1) {
            WriteSnapshot();
        }
        Sleep(1000);
    }
    return 0;
}

bool ResolveGamePointers()
{
    HMODULE game = GetModuleHandleW(L"Warcraft II.exe");
    if (!game) game = GetModuleHandleW(nullptr);
    if (!game) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    g_imageBase = reinterpret_cast<uint8_t*>(game);
    g_msgInitFlag = g_imageBase + (kPreferredMsgInitFlag - kPreferredImageBase);
    g_slotBase = g_imageBase + (kPreferredSlotBase - kPreferredImageBase);
    g_lobbyNames = reinterpret_cast<char*>(g_imageBase + (kPreferredLobbyName0 - kPreferredImageBase));
    g_inGameNames = reinterpret_cast<char*>(g_imageBase + (kPreferredInGameName0 - kPreferredImageBase));

    uint8_t* timeFn = g_imageBase + (kPreferredTimeNow - kPreferredImageBase);
    if (!IsLikelyCode(timeFn, 4) ||
        timeFn[0] != 0x55 || timeFn[1] != 0x8B || timeFn[2] != 0xEC || timeFn[3] != 0xE8) {
        Log("Install: TimeNow prologue mismatch at %p", timeFn);
        return false;
    }
    g_timeNow = reinterpret_cast<TimeNowFn>(timeFn);
    Log("Install: ok timeNow=%p flag=%p lobbyNames=%p inGameNames=%p",
        g_timeNow, g_msgInitFlag, g_lobbyNames, g_inGameNames);
    return true;
}

void StartThreads()
{
    if (!g_monitorThread) {
        g_monitorThread = CreateThread(nullptr, 0, MonitorThreadProc, nullptr, 0, nullptr);
    }
    if (!g_logThread) {
        g_logThread = CreateThread(nullptr, 0, LogThreadProc, nullptr, 0, nullptr);
    }
}

void StopThreads()
{
    InterlockedExchange(&g_stop, 1);
    if (g_monitorThread) {
        WaitForSingleObject(g_monitorThread, 2000);
        CloseHandle(g_monitorThread);
        g_monitorThread = nullptr;
    }
    if (g_logThread) {
        WaitForSingleObject(g_logThread, 2000);
        CloseHandle(g_logThread);
        g_logThread = nullptr;
    }
    InterlockedExchange(&g_stop, 0);
}

} // namespace

extern "C" __declspec(dllexport) DWORD __stdcall NetworkMonitor_SetEnabled(LPVOID enabled)
{
    InterlockedExchange(&g_enabled, enabled ? 1 : 0);
    if (!enabled) {
        ResetStats();
    }
    Log("NetworkMonitor_SetEnabled=%d", enabled ? 1 : 0);
    return 1;
}

extern "C" __declspec(dllexport) DWORD __stdcall NetworkMonitor_IsReady(LPVOID)
{
    return static_cast<DWORD>(InterlockedCompareExchange(&g_ready, 0, 0));
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        if (ResolveGamePointers()) {
            InterlockedExchange(&g_ready, 1);
            StartThreads();
        } else {
            Log("Install: failed");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        StopThreads();
    }
    return TRUE;
}
