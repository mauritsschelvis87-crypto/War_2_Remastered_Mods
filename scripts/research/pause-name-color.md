# Pause / resume name color

## Goal

Color the **player name** in multiplayer pause/resume system messages using Studio player colors (same idea as chat name colors), as a **separate** Feature toggle.

## Game strings (enUS)

| Key | Format |
|-----|--------|
| `mp_resume_message` | `%s resumed the game.` |
| pause-with-name | **not present** in `enUS.json` / exe literals |

There is no `mp_pause_message` / `paused the game` string in the Remastered build checked. The named notification players see around pause is the **resume** line.

## Draw path

Same in-game path as chat lines: `DrawTextColored` `0x5AEEB0` (calls `0x5AED00`). Hook lives in `ChatNameColorHook.dll` with a second enable flag so chat + pause features do not fight over the prologue.

## Match rule

Formatted text looks like: `Avent resumed the game.`

1. Match leading player name against `0x91ADA8` / stride `0x38`
2. Require following token `resumed` or `paused` (case-insensitive)
3. Overlay name prefix in that player’s Studio color; body stays default

## Feature flag

`extra-features.json` → `PauseColoredNames`  
Injector: `InjectPauseNameColor.exe` → export `PauseNameColor_SetEnabled`
