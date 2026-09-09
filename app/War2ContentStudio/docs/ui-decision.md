# UI decision — War2 Content Studio map editor

Date: 2026-08-31

## Options evaluated

| Approach | Pros | Cons |
|----------|------|------|
| **WPF native Canvas** | Same stack as PlayerColorStudio; no WebView2 runtime; direct file I/O; undo/state in C# | Must implement terrain rendering ourselves |
| **WebView2 + JC Fields fork** | Existing PUD UI logic in JS; cross-platform potential | Extra dependency; bridge for save/load; harder to integrate campaign deploy |

## Decision

**WPF native Canvas** for Fase 1 MVP.

Rationale:

1. Repo already ships WPF (`PlayerColorStudio`) — shared deployment and styling patterns.
2. PUD read/write is implemented in C# (`War2ContentStudio.Pud`); keeping UI in-process avoids JS↔C# marshalling.
3. JC Fields editor terrain layer is still incomplete; fork would not save time for terrain + UDTA MVP.
4. Campaign deploy tab fits naturally as a second WPF tab.

## Prototype notes

- `MainWindow.xaml`: zoomable tile grid on `Canvas`, tool sidebar, minimap via `PudThumbnail`.
- Pan via `ScrollViewer`; zoom 1–16× via slider.
- Future: optional WebView2 “preview mode” if we adopt JC Fields unit property panel — not required for MVP.

## Revisit triggers

- If terrain editing needs full movement-layer preview → evaluate porting JC Fields overlay or embedding WebView2 for that layer only.
- If we need macOS/Linux editor → split PUD library to shared project + Avalonia or web UI.
