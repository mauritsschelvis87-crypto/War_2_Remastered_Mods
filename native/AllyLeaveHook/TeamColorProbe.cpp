// TeamColorProbe.exe — research tool. Scans the running Warcraft II process
// for the HD unit team-tint table (the palette files on disk are already
// patched but HD unit sprites keep vanilla colors, so the tint must live in
// engine memory filled from an unknown source). Finds vanilla team colors in
// several encodings, reports clusters that look like a per-player table, and
// can live-poke a color so the user can see which table drives unit sprites.
//
// Usage:
//   TeamColorProbe.exe                      scan + report clusters
//   TeamColorProbe.exe --write ADDR RRGGBB ENC   poke one color
//        ENC: rgb | bgr | frgb | fbgr (f* = 3 floats 0..1)
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

struct TeamColor { uint8_t r, g, b; const char* name; };

// Vanilla band colors (from forest.ppl 208/212/216/... converted to 8-bit)
// plus the flat mapColors.bin variants where they differ.
static const TeamColor kColors[] = {
    { 0xA6, 0x00, 0x00, "P1 red" },
    { 0x0C, 0x49, 0xCE, "P2 blue" },
    { 0x00, 0x49, 0xCE, "P2 blue flat" },
    { 0x2D, 0xB6, 0x96, "P3 teal" },
    { 0x2C, 0xB2, 0x96, "P3 teal flat" },
    { 0x9A, 0x49, 0xB2, "P4 violet" },
    { 0x9E, 0x49, 0xAE, "P4 violet flat" },
    { 0xFB, 0x8E, 0x14, "P5 orange" },
    { 0xEF, 0x85, 0x14, "P5 orange flat" },
    { 0x28, 0x28, 0x3D, "P6 black" },
    { 0xE3, 0xE3, 0xE3, "P7 white" },
    { 0xDF, 0xDF, 0xDF, "P7 white flat" },
};

struct Pattern {
    std::vector<uint8_t> bytes;
    int colorId;
    const char* enc;
};

struct Hit {
    uintptr_t addr;
    int colorId;
    const char* enc;
};

static void AddFloatPattern(std::vector<Pattern>& out, int id,
    uint8_t a, uint8_t b, uint8_t c, const char* enc)
{
    float f[3] = { a / 255.0f, b / 255.0f, c / 255.0f };
    Pattern p;
    p.bytes.resize(12);
    memcpy(p.bytes.data(), f, 12);
    p.colorId = id;
    p.enc = enc;
    out.push_back(std::move(p));
}

static std::vector<Pattern> BuildPatterns()
{
    std::vector<Pattern> pats;
    for (int i = 0; i < (int)(sizeof(kColors) / sizeof(kColors[0])); ++i) {
        const TeamColor& c = kColors[i];
        pats.push_back({ { c.r, c.g, c.b }, i, "rgb" });
        // Black/white are symmetric — skip the redundant reversed byte form.
        if (!(c.r == c.b)) {
            pats.push_back({ { c.b, c.g, c.r }, i, "bgr" });
        }
        AddFloatPattern(pats, i, c.r, c.g, c.b, "frgb");
        if (!(c.r == c.b)) {
            AddFloatPattern(pats, i, c.b, c.g, c.r, "fbgr");
        }
    }
    return pats;
}

static DWORD FindGamePid()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsnicmp(pe.szExeFile, L"Warcraft II", 11) == 0 &&
                wcsstr(pe.szExeFile, L"Editor") == nullptr) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static const char* RegionKind(HANDLE proc, const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.Type == MEM_IMAGE) {
        char mod[MAX_PATH] = {};
        if (GetMappedFileNameA(proc, mbi.BaseAddress, mod, sizeof(mod) - 1)) {
            const char* slash = strrchr(mod, '\\');
            static char name[MAX_PATH];
            strncpy_s(name, slash ? slash + 1 : mod, _TRUNCATE);
            return name;
        }
        return "image";
    }
    if (mbi.Type == MEM_MAPPED) return "mapped";
    return "private";
}

int main(int argc, char** argv)
{
    DWORD pid = FindGamePid();
    if (!pid) {
        printf("Warcraft II process not found - start the game (in a match) first.\n");
        return 1;
    }
    HANDLE proc = OpenProcess(
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!proc) {
        printf("OpenProcess failed (%lu) - run elevated?\n", GetLastError());
        return 1;
    }
    printf("game pid=%lu\n", pid);

    if (argc >= 3 && _stricmp(argv[1], "--dump") == 0) {
        uintptr_t addr = (uintptr_t)strtoull(argv[2], nullptr, 16);
        size_t len = (argc >= 4) ? (size_t)strtoul(argv[3], nullptr, 10) : 256;
        std::vector<uint8_t> dump(len);
        SIZE_T got = 0;
        if (!ReadProcessMemory(proc, (LPCVOID)addr, dump.data(), len, &got)) {
            printf("read failed (%lu)\n", GetLastError());
            return 1;
        }
        for (size_t o = 0; o + 16 <= got; o += 16) {
            printf("%p:", (void*)(addr + o));
            for (int x = 0; x < 16; ++x) printf(" %02X", dump[o + x]);
            // Annotate as floats when they look like colors.
            float f[4];
            memcpy(f, dump.data() + o, 16);
            if (f[0] >= 0 && f[0] <= 1.001f && f[1] >= 0 && f[1] <= 1.001f &&
                f[2] >= 0 && f[2] <= 1.001f) {
                printf("   (%3.0f,%3.0f,%3.0f a=%.2f)", f[0] * 255, f[1] * 255, f[2] * 255, f[3]);
            }
            printf("\n");
        }
        CloseHandle(proc);
        return 0;
    }

    if (argc >= 4 && _stricmp(argv[1], "--write") == 0) {
        uintptr_t addr = (uintptr_t)strtoull(argv[2], nullptr, 16);
        unsigned rgb = (unsigned)strtoul(argv[3], nullptr, 16);
        uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        const char* enc = (argc >= 5) ? argv[4] : "rgb";
        uint8_t buf[12]; SIZE_T len = 3;
        if (_stricmp(enc, "rgb") == 0) { buf[0] = r; buf[1] = g; buf[2] = b; }
        else if (_stricmp(enc, "bgr") == 0) { buf[0] = b; buf[1] = g; buf[2] = r; }
        else {
            float f[3];
            if (_stricmp(enc, "frgb") == 0) { f[0] = r / 255.0f; f[1] = g / 255.0f; f[2] = b / 255.0f; }
            else { f[0] = b / 255.0f; f[1] = g / 255.0f; f[2] = r / 255.0f; }
            memcpy(buf, f, 12); len = 12;
        }
        DWORD old = 0;
        VirtualProtectEx(proc, (LPVOID)addr, len, PAGE_READWRITE, &old);
        SIZE_T written = 0;
        BOOL ok = WriteProcessMemory(proc, (LPVOID)addr, buf, len, &written);
        if (old) VirtualProtectEx(proc, (LPVOID)addr, len, old, &old);
        printf("write %s @ %p: %s (%zu bytes)\n", enc, (void*)addr, ok ? "OK" : "FAILED", written);
        CloseHandle(proc);
        return ok ? 0 : 1;
    }

    // --palneedle <pplPath> : search memory for the vanilla band region copied
    // verbatim from forest.ppl (indices 208..235), in 6-bit and 8-bit-RGBA form.
    if (argc >= 3 && _stricmp(argv[1], "--palneedle") == 0) {
        FILE* f = nullptr;
        if (fopen_s(&f, argv[2], "rb") != 0 || !f) { printf("cannot open ppl %s\n", argv[2]); return 1; }
        uint8_t ppl[768]{};
        fread(ppl, 1, sizeof(ppl), f);
        fclose(f);

        // 6-bit needle: bytes 208*3 .. 235*3+2 (84 bytes)
        const size_t n6 = 28 * 3;
        const uint8_t* needle6 = ppl + 208 * 3;
        // 8-bit RGBA needle: expand each 6-bit channel to 8-bit, alpha FF (112 bytes)
        uint8_t needle8[28 * 4];
        for (int i = 0; i < 28; ++i) {
            for (int c = 0; c < 3; ++c)
                needle8[i * 4 + c] = (uint8_t)((ppl[(208 + i) * 3 + c] * 255 + 31) / 63);
            needle8[i * 4 + 3] = 0xFF;
        }

        std::vector<uint8_t> buf2;
        MEMORY_BASIC_INFORMATION mbi2{};
        uintptr_t a2 = 0;
        size_t found = 0;
        while (VirtualQueryEx(proc, (LPCVOID)a2, &mbi2, sizeof(mbi2)) == sizeof(mbi2)) {
            uintptr_t base = (uintptr_t)mbi2.BaseAddress;
            uintptr_t next = base + mbi2.RegionSize;
            const bool readable = mbi2.State == MEM_COMMIT &&
                !(mbi2.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (mbi2.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                    PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            if (readable) {
                buf2.resize(mbi2.RegionSize);
                SIZE_T got = 0;
                if (ReadProcessMemory(proc, mbi2.BaseAddress, buf2.data(), mbi2.RegionSize, &got) && got > 0) {
                    const uint8_t* d = buf2.data();
                    for (size_t i = 0; i + n6 <= got; ++i) {
                        if (d[i] == needle6[0] && memcmp(d + i, needle6, n6) == 0) {
                            printf("6bit band @ %p (palette base ~ %p, prot=0x%lX)\n",
                                (void*)(base + i), (void*)(base + i - 208 * 3), mbi2.Protect);
                            if (++found > 60) break;
                        }
                    }
                    for (size_t i = 0; i + sizeof(needle8) <= got; ++i) {
                        if (d[i] == needle8[0] && memcmp(d + i, needle8, sizeof(needle8)) == 0) {
                            printf("8bit band @ %p (palette base ~ %p, prot=0x%lX)\n",
                                (void*)(base + i), (void*)(base + i - 208 * 4), mbi2.Protect);
                            if (++found > 60) break;
                        }
                    }
                }
            }
            if (next <= a2) break;
            a2 = next;
            if (a2 >= 0x7FFF0000) break;
        }
        printf("palneedle done: %zu hits\n", found);
        CloseHandle(proc);
        return 0;
    }

    // --pal : find 256-entry in-memory palettes (drive minimap + classic units).
    // A real palette has the vanilla player bands at indices 208/212/.../232.
    if (argc >= 2 && _stricmp(argv[1], "--pal") == 0) {
        // Vanilla forest.ppl 8-bit values at the band starts (from earlier dump).
        struct Band { int idx; uint8_t r, g, b; const char* name; };
        const Band bands[] = {
            { 208, 166,   0,   0, "P1 red" },
            { 212,  12,  73, 206, "P2 blue" },
            { 216,  45, 182, 150, "P3 teal" },
            { 220, 154,  73, 178, "P4 violet" },
            { 224, 251, 142,  20, "P5 orange" },
            { 228,  40,  40,  61, "P6 black" },
            { 232, 227, 227, 227, "P7 white" },
        };
        auto close = [](uint8_t a, uint8_t b) { return abs((int)a - (int)b) <= 24; };

        std::vector<uint8_t> pbuf;
        MEMORY_BASIC_INFORMATION pmbi{};
        uintptr_t pa = 0;
        size_t found = 0;
        for (int stride : { 3, 4 }) {
            pa = 0;
            while (VirtualQueryEx(proc, (LPCVOID)pa, &pmbi, sizeof(pmbi)) == sizeof(pmbi)) {
                uintptr_t base = (uintptr_t)pmbi.BaseAddress;
                uintptr_t next = base + pmbi.RegionSize;
                const bool readable = pmbi.State == MEM_COMMIT &&
                    !(pmbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                    (pmbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
                const size_t palBytes = (size_t)256 * stride;
                if (readable && pmbi.RegionSize >= palBytes) {
                    pbuf.resize(pmbi.RegionSize);
                    SIZE_T got = 0;
                    if (ReadProcessMemory(proc, pmbi.BaseAddress, pbuf.data(), pmbi.RegionSize, &got) && got >= palBytes) {
                        const uint8_t* d = pbuf.data();
                        for (size_t p = 0; p + palBytes <= got; p += stride) {
                            bool ok = true;
                            for (const Band& bd : bands) {
                                const uint8_t* e = d + p + (size_t)bd.idx * stride;
                                if (!close(e[0], bd.r) || !close(e[1], bd.g) || !close(e[2], bd.b)) { ok = false; break; }
                            }
                            if (ok && found < 40) {
                                ++found;
                                printf("palette stride=%d @ %p (prot=0x%lX)\n",
                                    stride, (void*)(base + p), pmbi.Protect);
                                for (const Band& bd : bands) {
                                    const uint8_t* e = d + p + (size_t)bd.idx * stride;
                                    printf("  [%d] %02X %02X %02X  %s\n", bd.idx, e[0], e[1], e[2], bd.name);
                                }
                                // minimap indices 1, 2, 255
                                for (int mi : { 1, 2, 255 }) {
                                    const uint8_t* e = d + p + (size_t)mi * stride;
                                    printf("  [%d] %02X %02X %02X  (minimap)\n", mi, e[0], e[1], e[2]);
                                }
                            }
                        }
                    }
                }
                if (next <= pa) break;
                pa = next;
                if (pa >= 0x7FFF0000) break;
            }
        }
        printf("palette scan done: %zu candidates\n", found);
        CloseHandle(proc);
        return 0;
    }

    const bool sigMode = (argc >= 2 && _stricmp(argv[1], "--sig") == 0);
    if (sigMode) {
        // Value-independent scan: any 7 consecutive entries whose hues follow
        // the fixed player order red, blue, teal, violet, orange, black, white.
        auto testSig = [](const float e[7][3]) -> bool {
            for (int i = 0; i < 7; ++i) {
                for (int c = 0; c < 3; ++c) {
                    if (e[i][c] < 0.0f || e[i][c] > 1.001f) return false;
                }
            }
            const float* r0 = e[0];
            if (!(r0[0] > 0.5f && r0[1] < 0.35f && r0[2] < 0.35f)) return false;          // red
            const float* b1 = e[1];
            if (!(b1[2] > 0.5f && b1[2] > b1[0] + 0.2f)) return false;                     // blue
            const float* t2 = e[2];
            if (!(t2[1] > 0.45f && t2[1] > t2[0] + 0.1f && t2[2] < t2[1] + 0.05f)) return false; // teal/green
            const float* v3 = e[3];
            if (!(v3[0] > 0.3f && v3[2] > 0.3f && v3[1] < v3[0] && v3[1] < v3[2])) return false; // violet
            const float* o4 = e[4];
            if (!(o4[0] > 0.6f && o4[1] > 0.25f && o4[2] < 0.4f && o4[0] > o4[1] && o4[1] > o4[2])) return false; // orange
            const float* k5 = e[5];
            if (!(k5[0] < 0.4f && k5[1] < 0.4f && k5[2] < 0.45f)) return false;            // black
            const float* w6 = e[6];
            float mx = max(w6[0], max(w6[1], w6[2])), mn = min(w6[0], min(w6[1], w6[2]));
            if (!(mn > 0.55f && (mx - mn) < 0.2f)) return false;                            // white
            return true;
        };

        std::vector<uint8_t> sbuf;
        MEMORY_BASIC_INFORMATION smbi{};
        uintptr_t sa = 0;
        size_t found = 0;
        uint64_t sscanned = 0;
        const int strides[] = { 3, 4, 8, 12, 16, 20, 24, 32 };
        while (VirtualQueryEx(proc, (LPCVOID)sa, &smbi, sizeof(smbi)) == sizeof(smbi)) {
            uintptr_t base = (uintptr_t)smbi.BaseAddress;
            uintptr_t next = base + smbi.RegionSize;
            const bool readable =
                smbi.State == MEM_COMMIT &&
                !(smbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (smbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                    PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            // Heap/private data plus module .data sections; skip executable code
            // (immediates there pattern-match as false hits).
            const bool executable = (smbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
            if (readable && !executable) {
                sbuf.resize(smbi.RegionSize);
                SIZE_T got = 0;
                if (ReadProcessMemory(proc, smbi.BaseAddress, sbuf.data(), smbi.RegionSize, &got) && got > 0) {
                    sscanned += got;
                    const uint8_t* d = sbuf.data();
                    for (int stride : strides) {
                        const bool asFloat = stride >= 12;
                        const size_t span = (size_t)stride * 6 + (asFloat ? 12 : 3);
                        if (got < span) continue;
                        for (size_t i = 0; i + span <= got; i += (asFloat ? 4 : 1)) {
                            float e[7][3];
                            bool ok = true;
                            for (int p = 0; p < 7 && ok; ++p) {
                                const uint8_t* q = d + i + (size_t)p * stride;
                                if (asFloat) {
                                    float f[3];
                                    memcpy(f, q, 12);
                                    e[p][0] = f[0]; e[p][1] = f[1]; e[p][2] = f[2];
                                    if (f[0] < 0 || f[0] > 1.001f) ok = false;
                                } else {
                                    e[p][0] = q[0] / 255.0f; e[p][1] = q[1] / 255.0f; e[p][2] = q[2] / 255.0f;
                                }
                            }
                            if (!ok) continue;
                            bool rgb = testSig(e);
                            // BGR: swap channels 0/2
                            bool bgr = false;
                            if (!rgb) {
                                float s[7][3];
                                for (int p = 0; p < 7; ++p) { s[p][0] = e[p][2]; s[p][1] = e[p][1]; s[p][2] = e[p][0]; }
                                bgr = testSig(s);
                            }
                            if ((rgb || bgr) && found < 60) {
                                ++found;
                                printf("sig %s stride=%d @ %p (prot=0x%lX)\n",
                                    bgr ? "bgr" : "rgb", stride, (void*)(base + i), smbi.Protect);
                                for (size_t o = 0; o < span && o < 128; o += 16) {
                                    uint8_t row[16];
                                    memcpy(row, d + i + o, min((size_t)16, got - i - o));
                                    printf("  %p:", (void*)(base + i + o));
                                    for (int x = 0; x < 16; ++x) printf(" %02X", row[x]);
                                    printf("\n");
                                }
                            }
                        }
                    }
                }
            }
            if (next <= sa) break;
            sa = next;
            if (sa >= 0x7FFF0000) break;
        }
        printf("sig scan done: %.1f MB, %zu candidates\n", sscanned / (1024.0 * 1024.0), found);
        CloseHandle(proc);
        return 0;
    }

    std::vector<Pattern> pats = BuildPatterns();
    std::vector<Hit> hits;
    std::vector<uint8_t> buf;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;
    uint64_t scanned = 0;

    while (VirtualQueryEx(proc, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        uintptr_t next = base + mbi.RegionSize;
        const bool readable =
            mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (readable) {
            buf.resize(mbi.RegionSize);
            SIZE_T got = 0;
            if (ReadProcessMemory(proc, mbi.BaseAddress, buf.data(), mbi.RegionSize, &got) && got > 0) {
                scanned += got;
                for (const Pattern& p : pats) {
                    const size_t n = p.bytes.size();
                    if (got < n) continue;
                    const uint8_t* hay = buf.data();
                    const uint8_t first = p.bytes[0];
                    for (size_t i = 0; i + n <= got; ++i) {
                        if (hay[i] != first) continue;
                        if (memcmp(hay + i, p.bytes.data(), n) == 0) {
                            hits.push_back({ base + i, p.colorId, p.enc });
                        }
                    }
                }
            }
        }
        if (next <= addr) break;
        addr = next;
        if (addr >= 0x7FFF0000) break; // 32-bit target
    }

    printf("scanned %.1f MB, raw hits: %zu\n", scanned / (1024.0 * 1024.0), hits.size());

    std::sort(hits.begin(), hits.end(),
        [](const Hit& a, const Hit& b) { return a.addr < b.addr; });

    // A team color table has several *different* players close together.
    size_t clusters = 0;
    for (size_t i = 0; i < hits.size();) {
        size_t j = i;
        uint32_t colorMask = 0;
        while (j < hits.size() && hits[j].addr - hits[i].addr <= 160) {
            colorMask |= 1u << hits[j].colorId;
            ++j;
        }
        int distinct = 0;
        // Collapse flat/ramp variants of the same player before counting.
        uint32_t playerMask = 0;
        for (int c = 0; c < (int)(sizeof(kColors) / sizeof(kColors[0])); ++c) {
            if (colorMask & (1u << c)) {
                playerMask |= 1u << (kColors[c].name[1] - '0');
            }
        }
        for (int p = 0; p < 9; ++p) if (playerMask & (1u << p)) ++distinct;

        if (distinct >= 2 && clusters < 40) {
            ++clusters;
            MEMORY_BASIC_INFORMATION info{};
            VirtualQueryEx(proc, (LPCVOID)hits[i].addr, &info, sizeof(info));
            printf("\ncluster @ %p (%s, prot=0x%lX, %d players)\n",
                (void*)hits[i].addr, RegionKind(proc, info), info.Protect, distinct);
            for (size_t k = i; k < j; ++k) {
                printf("  %p  %-14s %s\n", (void*)hits[k].addr,
                    kColors[hits[k].colorId].name, hits[k].enc);
            }
            uint8_t dump[96] = {};
            SIZE_T got = 0;
            uintptr_t start = hits[i].addr & ~0xFu;
            if (ReadProcessMemory(proc, (LPCVOID)start, dump, sizeof(dump), &got)) {
                for (size_t o = 0; o + 16 <= got; o += 16) {
                    printf("  %p:", (void*)(start + o));
                    for (int x = 0; x < 16; ++x) printf(" %02X", dump[o + x]);
                    printf("\n");
                }
            }
        }
        i = j;
    }
    printf("\nclusters with >=2 players: %zu (printed max 40)\n", clusters);
    CloseHandle(proc);
    return 0;
}
