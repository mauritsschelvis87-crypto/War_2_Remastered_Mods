"""Local A/B page: vanilla vs enhanced unit voice WAVs side by side."""
from __future__ import annotations

import argparse
import json
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

SKIP_DIRS = frozenset({"Misc", "Spells", "Bldg"})


def collect_pairs(vanilla_gamesfx: Path, enhanced_voice: Path) -> list[dict[str, str]]:
    pairs: list[dict[str, str]] = []
    if not vanilla_gamesfx.is_dir():
        return pairs
    for sub in sorted(vanilla_gamesfx.iterdir()):
        if not sub.is_dir() or sub.name in SKIP_DIRS:
            continue
        for wav in sorted(sub.glob("*.wav")):
            enhanced = enhanced_voice / sub.name / wav.name
            if not enhanced.is_file():
                continue
            rel = f"{sub.name}/{wav.name}"
            pairs.append(
                {
                    "id": rel,
                    "label": rel,
                    "vanilla": f"/vanilla/{sub.name}/{wav.name}",
                    "enhanced": f"/enhanced/{sub.name}/{wav.name}",
                }
            )
    return pairs


def build_html(pairs: list[dict[str, str]], title: str) -> str:
    options = "\n".join(
        f'<option value="{p["id"]}">{p["label"]}</option>' for p in pairs
    )
    data_json = json.dumps(pairs)
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>{title}</title>
  <style>
    :root {{
      color-scheme: dark;
      --bg: #12141a;
      --panel: #1c2029;
      --border: #2e3544;
      --text: #e8eaef;
      --muted: #9aa3b2;
      --vanilla: #6b9bd1;
      --enhanced: #7bc96f;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: "Segoe UI", system-ui, sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
    }}
    .wrap {{
      max-width: 1100px;
      margin: 0 auto;
      padding: 24px 20px 40px;
    }}
    h1 {{ font-size: 1.35rem; margin: 0 0 8px; }}
    p.lead {{ color: var(--muted); margin: 0 0 20px; line-height: 1.5; }}
    label {{ display: block; font-size: 0.85rem; color: var(--muted); margin-bottom: 6px; }}
    select {{
      width: 100%;
      max-width: 520px;
      padding: 10px 12px;
      border-radius: 8px;
      border: 1px solid var(--border);
      background: var(--panel);
      color: var(--text);
      font-size: 1rem;
    }}
    .row {{
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 16px;
      margin-top: 24px;
    }}
    @media (max-width: 720px) {{
      .row {{ grid-template-columns: 1fr; }}
    }}
    .card {{
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 16px;
    }}
    .card h2 {{
      font-size: 0.95rem;
      margin: 0 0 12px;
      letter-spacing: 0.02em;
    }}
    .card.vanilla h2 {{ color: var(--vanilla); }}
    .card.enhanced h2 {{ color: var(--enhanced); }}
    audio {{
      width: 100%;
      margin-bottom: 10px;
    }}
    .btn-row {{
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }}
    button {{
      padding: 8px 14px;
      border-radius: 8px;
      border: 1px solid var(--border);
      background: #252b36;
      color: var(--text);
      cursor: pointer;
      font-size: 0.9rem;
    }}
    button:hover {{ background: #303847; }}
    button.primary {{
      background: #3d4f68;
      border-color: #4d6280;
    }}
    .hint {{
      margin-top: 20px;
      font-size: 0.85rem;
      color: var(--muted);
      line-height: 1.5;
    }}
  </style>
</head>
<body>
  <div class="wrap">
    <h1>Voice sample compare</h1>
    <p class="lead">Vanilla (left) vs mastered enhanced (right). Use <strong>Play both</strong> to start at the same time.</p>
    <label for="sample">Sample</label>
    <select id="sample">{options}</select>
    <div class="row">
      <div class="card vanilla">
        <h2>Vanilla</h2>
        <audio id="vanilla" controls preload="auto"></audio>
        <div class="btn-row">
          <button type="button" id="playVanilla">Play vanilla</button>
        </div>
      </div>
      <div class="card enhanced">
        <h2>Enhanced</h2>
        <audio id="enhanced" controls preload="auto"></audio>
        <div class="btn-row">
          <button type="button" id="playEnhanced">Play enhanced</button>
        </div>
      </div>
    </div>
    <div class="btn-row" style="margin-top:16px">
      <button type="button" class="primary" id="playBoth">Play both</button>
      <button type="button" id="stopBoth">Stop both</button>
    </div>
    <p class="hint">Tip: pick Human/Hwhat1.wav or Knight/Knwhat1.wav first — short lines, easy to compare.</p>
  </div>
  <script>
    const pairs = {data_json};
    const byId = Object.fromEntries(pairs.map(p => [p.id, p]));
    const select = document.getElementById('sample');
    const vanilla = document.getElementById('vanilla');
    const enhanced = document.getElementById('enhanced');

    function loadSample(id) {{
      const p = byId[id];
      if (!p) return;
      vanilla.src = p.vanilla;
      enhanced.src = p.enhanced;
      vanilla.load();
      enhanced.load();
    }}

    select.addEventListener('change', () => loadSample(select.value));
    document.getElementById('playVanilla').onclick = () => vanilla.play();
    document.getElementById('playEnhanced').onclick = () => enhanced.play();
    document.getElementById('playBoth').onclick = () => {{
      vanilla.currentTime = 0;
      enhanced.currentTime = 0;
      vanilla.play();
      enhanced.play();
    }};
    document.getElementById('stopBoth').onclick = () => {{
      vanilla.pause();
      enhanced.pause();
    }};

    if (select.value) loadSample(select.value);
  </script>
</body>
</html>
"""


class CompareHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, repo_root: Path, **kwargs):
        self.repo_root = repo_root
        super().__init__(*args, directory=str(repo_root), **kwargs)

    def translate_path(self, path: str) -> str:
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean == "/compare":
            return str(self.repo_root / "scripts" / "audio" / "_compare_page.html")
        if clean.startswith("/vanilla/"):
            rel = clean[len("/vanilla/"):]
            return str(self.repo_root / "mod" / "backup" / "vanilla" / "x86" / "Data" / "Gamesfx" / rel.replace("/", "\\"))
        if clean.startswith("/enhanced/"):
            rel = clean[len("/enhanced/"):]
            return str(self.repo_root / "mod" / "assets" / "audio" / "voice" / rel.replace("/", "\\"))
        return super().translate_path(path)

    def log_message(self, format: str, *args) -> None:
        return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    vanilla = repo_root / "mod" / "backup" / "vanilla" / "x86" / "Data" / "Gamesfx"
    enhanced = repo_root / "mod" / "assets" / "audio" / "voice"
    pairs = collect_pairs(vanilla, enhanced)
    if not pairs:
        raise SystemExit("No vanilla/enhanced pairs found. Run SyncVoiceGamesfxBackup + enhance script first.")

    page_path = repo_root / "scripts" / "audio" / "_compare_page.html"
    page_path.write_text(build_html(pairs, "War2 voice compare"), encoding="utf-8")

    def handler(*a, **kw):
        CompareHandler(*a, repo_root=repo_root, **kw)

    server = ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    url = f"http://127.0.0.1:{args.port}/compare"
    print(f"Voice compare: {url} ({len(pairs)} samples)")
    print("Close this window or Ctrl+C to stop the server.")
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopped.")


if __name__ == "__main__":
    main()
