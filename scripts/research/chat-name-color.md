# Research: Colored multiplayer chat names

**Date:** 2026-08-10  
**Target:** `Warcraft II Remastered` `x86\Warcraft II.exe` (PE32, ImageBase `0x00400000`)

## Goal

Show each multiplayer chat sender’s name in that player’s current Studio color  
(e.g. blue name for a blue slot): `avent: hello`.

## Findings

### Chat history UI

Widget ids (string VAs):

| Id | VA |
|----|-----|
| `chat` | `0x00846D6C` |
| `chat_content` | `0x00846D74` |
| `chat_history` | `0x00846D84` |
| `chat_send` / `mp_chat_send` | `0x00846D94` / `0x00846DA0` |

History rebuild lives near `0x00536A20` (find `chat_content` / `chat_history`).

Chat lines are stored in a vector of SSO strings:

| Symbol | Preferred VA | Notes |
|--------|--------------|--------|
| begin | `0x0095D404` | |
| end | `0x0095D408` | |
| stride | `0x18` (24) | one `std::string` per line |

Append helper: `fcn.00536270` (copies C-string into a new slot, advances end by `0x18`).

### Draw / set-text site (hook)

Unique site that binds each history line onto the current UI object:

```
0x00536BB1  push  eax                 ; line text
0x00536BB2  push  dword [0x965170]    ; current UI
0x00536BB8  call  0x5AEE80            ; SetText2(ui, text)
0x00536BBD  mov   ecx, dword [0x95D408]
```

Pattern (1 hit):

```
50 FF 35 70 51 96 00 E8 ?? ?? ?? ?? 8B 0D 08 D4 95 00
```

`0x5AEE80` → `0x5AECD0` reads **`ui + 0x20C`** and passes that ARGB into the text drawer  
(same color field AllyLeave uses for Alliances names).

**Implication:** one color per line widget. Coloring `ui+0x20C` before SetText paints the **whole line** (name + body) in the player color. Mid-string name-only coloring would need a separate rich-text path (game also has a `0x1F` color-escape builder around `0x0053551F` for other UI); v1 uses the proven `+0x20C` write.

### Player names / color slot in memory

Compose/loop over slots uses stride **`0x26`** from base `0x00916268`:

| Field | Preferred VA formula | Meaning |
|-------|----------------------|---------|
| color / type bytes | `0x00916268 + i*0x26` … `+0x91626A` | slot metadata |
| name C-string | `0x0091626D + i*0x26` | multiplayer name |

**Player names (in-game):** preferred base `0x0091ADA8`, stride **`0x38`**  
(`lea eax, [ecx*7*8+0x91ada8]` in leave-message formatting @ `0x004F17DA`).

Earlier candidate `0x0091626D` / stride `0x26` is a different lobby/status block and is often empty during a match.

### Color source

Studio `mod\player-colors.json` → `#RRGGBB` per player 1–8.  
Pack as little-endian UI color: `0xAABBGGRR` with `A=0xFF` (same as AllyLeave `0xFF0000FF` red).

## Hook plan

1. Pattern-scan the unique SetText2 site above.  
2. Detour the `call 0x5AEE80`.  
3. On each call: if enabled, parse `name:` prefix, match slot, write `ui+0x20C`, then call original.  
4. System lines without a matching name keep the existing widget color.

## Status

**In-game path (v3 — name only):** floating chat uses NKMapMessages → `DrawTextColored` (`0x5AEEB0`).

| Step | VA | Notes |
|------|-----|--------|
| Compose `name: msg` | `0x4D3160` | names from `0x91ADA8` / stride `0x38` |
| Push to map msgs | `0x614A90` → `0x614D50` | slot+0xCC color byte; chat uses **0** |
| Draw | `0x614DD0` → `0x5AEEB0(ui,text,color)` | one ARGB for whole string |

v3 hooks `0x5AEEB0`: draw full line in body color, then redraw `"Name:"` on top in Studio color.

**ASLR:** name table = `moduleBase + (0x91ADA8 - 0x400000)`.

## Chat history recall (PageUp/PageDown mod)

Map-message ring: **15 slots × `0xD0`** at `0x9B17A0` (BSS). Slot layout:

| Offset | Type | Meaning |
|--------|------|---------|
| `+0x00` | `char[0xC8]` | line text |
| `+0xC8` | `DWORD` | expiry = `TimeNow() + duration` (set by store fn `0x614D50`) |
| `+0xCC` | `BYTE` | color slot (chat uses 0) |

- `PushMapMsg 0x614A90(text, colorByte, duration)`: shifts slots 1..14 down
  (oldest = slot 0), stores the new line in slot 14.
- Per-frame update (`0x614C59` loop) calls `0x614DA0(entry, now)`; expired
  entries are WIPED via `0x614D20` (text[0]=0, expiry=0, color=0) — the ring
  does not preserve old lines, so the hook keeps its own 64-entry history
  captured by a prologue hook on `0x614A90` (prologue `55 8B EC 83 EC 0C 56`,
  7 clean bytes).
- `TimeNow 0x625940` → `0x67FDF0` → `0x671BD0`: pure QPC-based ms-since-start
  getter — safe to call from any thread.
- Replay = rewriting ring slots directly (expiry zeroed first, written last),
  no game calls besides `TimeNow`.
