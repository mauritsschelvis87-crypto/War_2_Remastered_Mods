# End-game popup: "Observe" button (research)

Addresses are preferred-base VAs (`0x400000`); apply the module base delta at
runtime (ASLR!).

## Popup object

- Defeat popup singleton @ `0x95EA60`, victory popup @ `0x95EA18`.
  Byte `[this+0]` = visible flag.
- `0x627430` Show: `byte[this]=1`.
- **`0x627450` Close: `byte[this]=0` + `0x5B3400(root 0x965170)` refresh.**
- `0x627470` IsShown, `0x627390` SetText(str), **`0x6273B0` AddButton(spec)**,
  `0x6273D0` set rect/params, `0x626560` (re)build.

## Defeat popup constructor — hook site

- `0x547AA0` (defeat), `0x547C10` (victory). Prologue defeat:
  `55 8B EC 6A FF 68 08 90 82 00` (fills at `0x547AA0`).
- Flow: `0x58AB00('defeat')` localize → string ctor `0x49ED60` →
  `SetText 0x627390` → label key `'exit_game'` if `byte[0x922F5B]!=0`
  (multiplayer) else `'defeat_btn'` ("Restart") → button spec `0x530320(cbObj,
  str, outByte)` → `AddButton 0x6273B0` → spec dtor `0x525090` → Show.

## Button callback object

Spec gets a `{vtable, dword}` callback object; defeat vtable @ `0x848DB4`:

| slot | fn | meaning |
|------|----|---------|
| 0/1 | `0x5478D0` | clone into new object (writes vtable, copies +4) |
| 2 | `0x547910` | **OnClick**: `0x5453B0(root, param)`; if ok → `Popup::Close(0x95EA60)` + `0x4C48E0(7)` = leave game |
| 3 | `0x547970` | returns `0x90F3FC` (id) |
| 4/5 | `0x4C5D50`/`0x4C5D80` | deleting dtor (game free `0x7E91F3`, size 8) |

## Observe button = tiny addition

Hook `0x547AA0`, run original, then (multiplayer only):

1. Build label with string ctor `0x49ED60("Observe")` — no locale key needed.
2. Own callback object with own 6-slot vtable: clone fn writes our vtable;
   OnClick does only `Popup::Close(0x95EA60)` (skip the leave event);
   reuse game's `0x547970`/`0x4C5D50`/`0x4C5D80` for id/dtor (layout equal).
3. `0x530320` spec ctor → `0x6273B0` AddButton → `0x525090` spec dtor.

Buttons stack vertically in the popup, so it lands under Exit Game
automatically. Closing via `0x627450` is exactly what the game's own Exit
click does before posting the leave event — UI state stays consistent.

Open question (test in game): input/scroll behavior after closing the popup
while eliminated; popup does not reappear (until game-end flow triggers its
own victory/defeat screen).
