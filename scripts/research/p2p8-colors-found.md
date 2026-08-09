## Remastered player color sources (headless)

### Runtime table in Warcraft II.exe
VA 0x008425F8 (used at 0x00512392): bytes D0 D4 D8 DC E0 E4 E8 ...
=> player color-band starts: 208,212,216,220,224,228,232 (P1-P7)
P2 exclusive band start = 0xD4 = 212

### Player 2 (blue)
- Exclusive unit band 212-215: #0C49CE, #0428A2, #001475, #00044D
- mapColors 212: #0049CE
- Shared minimap idx 252 (forest.ppl): #0000FF
- mapColors 252 (aliases exclusive blue): #0049CE
- Ally bar fe_endgame_stats_bar_1 progress_cursor: #003CC0 [0,60,192]
- Ally bar progress (darker): #001E60 [0,30,96]

### Player 8 (yellow)
- Minimap/shared band 188-191: #FFF759, #FFB639, #FF6118, #FF0000
- mapColors 188: #FBFB49
- Ally bar fe_endgame_stats_bar_7 progress_cursor: #FCFC48 [252,252,72]
- Ally bar progress (darker): #7E7E24 [126,126,36]
- Classic lobby_map used 0xC8=200 for yellow; Remastered 200 is magenta #FF00FF (not used)
- No exclusive 4-slot band in the D0..E8 runtime table for P8 (8th byte is 0x0C filler/RLE)

### Safe patch implication
- P2: patching indices 212-215 is the missing exclusive Remastered hook for unit/team coloring.
- P2 minimap still also reads 252 (shared); mapColors 252 already matches 212 blue.
- P8: still only 188-191 (shared with build-bar yellows); no exclusive band found yet.

### Apply note (2026-08-09)
- **Unit band:** 212–215 (table `0x008425F8`).
- **Minimap dots:** separate table `0x008C8D84` = `D0 01 D8 DC E0 E4 FF 02` → P2 = **palette index 1** (`.ppl` only, not `mapColors.bin`).
- **Ally/victory:** skins `fe_endgame_stats_bar_1`.
- P8 minimap table slot = index **2** (still locked / shared).

