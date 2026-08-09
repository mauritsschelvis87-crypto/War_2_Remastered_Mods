# Research: Ally-screen leave indicator (Remastered QoL)

**Goal:** When a multiplayer player leaves/drops, show that player’s name in **red** on the in-game Alliances screen.

**Status:** Hooks found (headless Rizin). Next step = prototype DLL.  
**Date:** 2026-08-09  
**Target build:** Warcraft II Remastered (`x86\Warcraft II.exe`, PE32 / x86)

---

## Problem statement

Remastered already prints chat lines when someone leaves, but the Alliances dialog does **not** mark that slot. Community reports (Blizzard forums, 2025) list this as a known gap vs classic BNE:

> “No indication in the alliance dialog when a player has left.”

Desired UX (user): a scratch/strikethrough through the left player’s row on the ally screen.

---

## What Remastered already knows (confirmed)

### 1. Distinct leave-related chat strings

In `x86\Data\Strings\enUS.json` and as string keys inside `Warcraft II.exe`:

| Key | Text |
|-----|------|
| `message_player_left` | Player %s left the game. |
| `message_player_dropped` | Player %s was dropped. |
| `message_player_eliminated` | Player %s was eliminated. |

These three keys sit adjacent in the exe string table (`~0x0044018C`), so the game already distinguishes **left / dropped / eliminated** for messaging.

### 2. Alliances UI widget tree (string IDs in exe)

Neighborhood around `alliances_menu` in `Warcraft II.exe` (~`0x00443928`):

```
team_color_ → fe_endgame_stats_bar_ → team_color_frame_
common_cancel / common_ok
alliances_menu
  main
    mp_dip_title
    main_content
      mp_dip_victory
      mp_dip_allies
      shared_vision_title
      mp_dip_vision
```

Related skins:

- `fe_endgame_stats_bar_0..7` — per-player color chips (already patched by Modding Studio colors)
- `fe_alliance_team_color_border` — border skin in `skins.json`
- Lobby has separate `player_slots` UI (not the in-game alliances menu)

### 3. Binary / tech shape

| Item | Finding |
|------|---------|
| Main binary | `x86\Warcraft II.exe` (~5.3 MB), **x86 / PE32** |
| Net/UI helper | `x86\ClientSdk.dll` (~13 MB) — few leave-related hits |
| Rendering | Direct3D / d3d11 / OpenGL / SDL strings present |
| Skin system | Uses `skins.json` + `widget_*` / `fe_*` ids |
| Classic BNE still present | `x86\Data\Files\Warcraft II BNE.exe` (not the Remastered UI path) |

No assets or string tokens found for `strikethrough`, `cross_out`, `scratch`, `slot_left`, etc. There is **no unused leave-overlay sprite** sitting in Data ready to enable via JSON.

---

## Why this is not a Modding Studio file-patch

Player colors work by rewriting static palette / skin colors offline.

A leave indicator must:

1. Detect leave/drop **during a live match** (per player slot).
2. Update only that row in the **Alliances** dialog while it is open / next time it opens.
3. Stay in sync for all clients (or at least locally from each client’s known player state).

`skins.json` cannot express “player 3 left at minute 12”. Painting a permanent scratch on a bar skin would mark that color forever for every game, including active players.

**Conclusion:** this needs a **runtime hook** (DLL inject / detour into Remastered), not an Apply-PlayerColors-style patch.

---

## Classic BNE comparison

- Classic Allies menu is the historical reference for “who is still in”.
- Community expectation: left players appear crossed out / inactive in that menu.
- BNE `.w2p` plugins (desktop `Warcraft-II-Plugins-main`) patch **BNE.exe** fixed addresses. Those offsets do **not** transfer to Remastered’s rewritten `Warcraft II.exe`.
- Useful only as a behavioral reference, not as portable code.

---

## Promising hook directions (next research steps)

Ordered by likely ROI:

### A. Hook the leave message path (best first probe)

1. Find xrefs / call sites for `message_player_left` / `_dropped` / `_eliminated` in `Warcraft II.exe` (IDA/Ghidra).
2. At that call, capture the **player index / name** being reported as gone.
3. Maintain a local `bool left[8]` table inside a helper DLL.
4. When Alliances UI rebuilds/draws a row, if `left[i]`, draw overlay (line) or force greyed text.

**Why first:** strings are confirmed present; leave events already fire for chat.

### B. Find player-slot status in memory

Search for an 8-slot structure with fields resembling:

- connected / active / human / computer
- alliance bitmask
- vision bitmask
- name pointer

Validate by leaving a slot in a private MP test and watching which byte flips.

### C. Hook Alliances UI bind/draw

String IDs `alliances_menu`, `team_color_`, `fe_endgame_stats_bar_` should lead to UI bind code. Goal: per-row draw callback or post-draw overlay using player index.

### D. Visual approach options

| Approach | Pros | Cons |
|----------|------|------|
| Draw diagonal line over row/chip | Matches “kras” request; no new assets | Needs draw hook + coords |
| Dim / grey bar + “(left)” suffix | Clear text | Needs text layout access |
| Reuse defeat/grey frame skins | May exist (`fe_endgame_stats_grey`) | May look like “dead” not “left”; still needs runtime switch |

---

## Risks / constraints

- **Anti-cheat / ToS:** injecting into the live Battle.net client may risk account action; treat as single-player / private test first.
- **Updates:** Remastered patches will shift offsets; prefer pattern/signature scans over hard addresses.
- **Desync:** visual-only local overlay is safer than changing shared game state.
- **Scope:** lobby leave (`mp_lobby_left`) is a different UI from in-game Alliances.

---

## Recommended implementation track (for Modding Studio later)

1. **Prototype DLL** loaded alongside Remastered (manual inject or launcher) — not inside the WPF color Apply path.
2. Log leave events (player index + which message key fired).
3. Private 2-box MP test: confirm `left[]` flips correctly for leave vs drop vs eliminate.
4. Overlay strikethrough on Alliances row when dialog is visible.
5. Only then: Modding Studio toggle “Ally leave marker (experimental)” that launches/enables the helper.

Do **not** block color-mod shipping on this; keep it a separate QoL module.

---

## Headless RE setup (this machine)

- Tool: **Rizin 0.9.1** (Windows static), no GUI
- Path: `%USERPROFILE%\Tools\rizin\current\bin` (`rizin.exe`, `rz-bin.exe`, …)
- Target: `C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe`
- PE: PE32 / x86, image base `0x00400000`, compiled ~2026-02-24, PDB path present in binary

Example:

```bat
set PATH=%USERPROFILE%\Tools\rizin\current\bin;%PATH%
rizin -e scr.color=0 -c "aa; axt @ 0x008417a8; pdf @ 0x004f16f0" -q -- "C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
```

---

## Concrete findings (Rizin, 2026-08-09)

### Leave handler

All three string pushes live in one function:

| Item | VA |
|------|----|
| Handler | `fcn.004f16f0` (large net/UI message switch) |
| `message_player_eliminated` push | `0x004f175b` |
| `message_player_left` push | `0x004f1784` |
| `message_player_dropped` push | `0x004f17a8` |

Packet / struct (`arg_4h` → `esi`):

- `byte [esi+0x01]` = **player index** (0..7)
- `byte [esi+0x06]` = message type (switch discriminant)

### Player status array (key for “red text”)

On leave / drop / eliminate path:

```asm
movzx eax, byte [esi+0x01]      ; player index
mov   byte [eax+0x918cac], 0x03 ; mark gone
```

| Item | Detail |
|------|--------|
| Array VA | `0x00918CAC` |
| Indexing | `status[playerIndex]` |
| Value set on leave/elim/drop | `0x03` |
| Other values observed in consumers | `0x01` (treated as active in an 8-slot loop at `0x004CA450`), also compares vs `0x04` / `0x05` elsewhere |

Also clears alliance/vision bits for that player (`btr` on `0x919678` / `0x919679` / `0x91967a`).

**Implication for QoL mod:** preferred display approach is now:

> If `*(uint8_t*)(0x918CAC + playerIndex) == 0x03` → render that Alliances row name in red (or grey).

No strikethrough draw hook required for v1.

### Alliances UI already reads that status (confirmed)

`0x00918CAC` has many game-logic readers. In the Alliances UI range the **only** hit is the per-row gate below — this is the natural red-name hook.

| Item | VA | Role |
|------|----|------|
| Alliances refresh | `0x005256B0` | Builds / refreshes `alliances_menu` |
| Open caller | `0x005439B0` → `call 0x5256B0` | Pause/diplomacy path opens alliances |
| Row widget builder | `fcn.00525110` @ call `0x00525D7A` | Selects `team_color_<i>` (+ `fe_endgame_stats_bar_<i>`) |
| Name text bind | `fcn.005AEE20` @ call `0x00525D93` | Property **`0x11`** = set text on current widget |
| **Status gate (PRIMARY HOOK)** | `0x00525D9B` | `cmp byte [edi+0x918CAC], 1` |
| Enable (active only) | `fcn.005AE470` @ call `0x00525DAA` | Unlocks row interaction (`[ctx+0x170]=1`, copies active visuals) |
| Disable (always) | `fcn.005AE7E0` @ call `0x00525DF2` | Re-locks after checkbox sync |
| Alliance checkbox | `0x0095C064 + playerIndex` | Synced via `fcn.00545370` |
| Vision checkbox | `0x0095C06C + playerIndex` | Synced via `fcn.00545370` |
| UI root global | `0x00965170` | Passed into all widget helpers |
| Loop | `edi` = player 0..7, `esi` += `0x18`, stop at `0xC0` | 8 rows |

Row pipeline (each player):

```asm
call  0x525110                 ; select team_color_<edi>
push  0x11                     ; text property
call  0x5AEE20                 ; set player name on that widget
cmp   byte [edi+0x918CAC], 1
jnz   skip_enable              ; left/dropped/etc. already skip unlock
call  0x5AE470                 ; enable only if active
; ... sync alliance + vision checkboxes ...
call  0x5AE7E0                 ; lock again
```

So Remastered **already knows** leavers in this UI (status ≠ 1 → no enable). It just never recolors the name.

### Secondary leave helper (mirrors UI slot array)

`0x004F3F80` also marks a player gone:

```asm
mov  byte [ecx+0x918CAC], 0x03
mov  byte [eax+0x916268], 0x03   ; eax = index * 0x26
; + clears alliance/vision bitmasks at 0x919678..
```

Useful if a future hook wants both status arrays kept in sync.

### Red text assets (already in skins)

In `Data\skins\skins.json`:

| Skin id | RGB |
|---------|-----|
| `widget_text_white` | `[220, 220, 220]` |
| `widget_text_yellow` | `[255, 240, 75]` |
| `widget_text_red` | `[238, 124, 98]` |

Skin names are FNV-hashed at startup (e.g. `push "widget_text_red"` → hash store). UI uses Nuklear (`NUISkin.cpp` / `NUIContent.cpp` paths in the binary). Runtime “apply skin by id” helper is not yet pin-pointed; v1 can either:

1. Call into the existing skin-apply path once found, **or**
2. Patch the current widget’s text RGB to `238,124,98` after the name bind when status ≠ 1.

---

## Hook points for implementation (signatures)

VAs are for this build (`Warcraft II.exe`, image base `0x00400000`, compile ~2026-02-24). Prefer signature scan — absolute data addresses (`0x918CAC`, `0x965170`) are relocated immediates inside the patterns.

### H1 — Leave write (source of truth)

```
VA:  0x004F1805
Asm: mov byte [eax+0x918CAC], 0x03
Sig: C6 80 AC 8C 91 00 03
```

Optional broader helper:

```
VA:  0x004F3F97
Sig: C6 81 AC 8C 91 00 03    ; mov byte [ecx+0x918CAC], 3
     C6 80 68 62 91 00 03    ; mov byte [eax+0x916268], 3
```

### H2 — Alliances row status gate (**best place for red name**)

```
VA:  0x00525D9B
Asm: cmp byte [edi+0x918CAC], 0x01
     jnz  +0x0E
     push dword [0x965170]
     call Enable (0x5AE470)
Sig: 80 BF AC 8C 91 00 01 75 0E FF 35 70 51 96 00 E8
```

**Detour idea:** when `status[edi] != 1` (or `== 3`), keep skipping Enable, and force the just-bound `team_color_<edi>` label to `widget_text_red` / RGB `(238,124,98)`.

### H3 — Name bind (alternate / complementary)

```
VA:  0x00525D86 .. 0x00525D93
Asm: push 0x11 / push name / push [0x965170] / call 0x5AEE20
Sig: 6A 11 0F 47 45 ?? 50 FF 35 70 51 96 00 E8
```

Hook after this call; `edi` is still the player index.

### H4 — Alliances open (refresh trigger)

```
VA:  0x005439B0
Asm: call 0x5256B0
Sig: E8 FB 1C FE FF          ; relative; recompute on scan via call target
```

Use to know when the dialog (re)builds rows.

---

## Evidence checklist

- [x] Leave strings exist (`left` / `dropped` / `eliminated`)
- [x] Alliances menu widget IDs exist in exe
- [x] Color chips tied to `fe_endgame_stats_bar_*` (already used by color mod)
- [x] No static strike asset / JSON state for “left”
- [x] Remastered is x86 PE; separate from BNE plugin ecosystem
- [x] Xrefs from leave strings to handler (via **Rizin headless**)
- [x] Confirmed player status byte array written on leave
- [x] Alliances row text bind + status gate located (`0x525D93` / `0x525D9B`)
- [x] Stable scan signatures captured for H1–H4
- [x] Working prototype DLL (`mod/native/AllyLeaveHook.dll` + injector)
- [x] Modding Studio **Extra** tab toggle (`AllyLeaveRedNames`)

---

## Immediate next action

1. Prototype a small x86 DLL that pattern-scans **H2** and, on `status[edi] != 1`, applies red text to the current `team_color_*` widget (skin id or raw RGB).
2. Private 2-player MP test: leave → `0x918CAC[i]==3` → open Alliances → name is red.
3. Only then: Modding Studio toggle “Ally leave marker (experimental)”.
