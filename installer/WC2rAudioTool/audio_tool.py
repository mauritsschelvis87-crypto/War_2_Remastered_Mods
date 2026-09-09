"""WC2r Audio Tool — play original game voice samples and try custom replacements."""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
import wave
import webbrowser
from collections import defaultdict
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

import unit_presets

APP_NAME = "WC2r Audio Tool"
SKIP_DIRS = frozenset({"Misc", "Spells", "Bldg"})
PORT = 8799

APP_DIR = Path(os.environ.get("WC2R_AUDIO_TOOL_DIR", Path(__file__).resolve().parent))
VANILLA_DIR = APP_DIR / "audio" / "vanilla"
REPLACER_DIR = APP_DIR / "replacer"
DEMOS_DIR = APP_DIR / "audio" / "demos"
CONFIG_PATH = APP_DIR / "config.json"
SPEEDS_PATH = APP_DIR / "playback_speeds.json"
SELECTIONS_PATH = APP_DIR / "selections.json"
MOD_EXPORT_PATH = APP_DIR / "mod_export.json"
MIN_PLAYBACK_SPEED = 0.5
MAX_PLAYBACK_SPEED = 2.0
SPEED_STEP = 0.05
MIN_PLAYBACK_VOLUME = 0.0
MAX_PLAYBACK_VOLUME = 2.0
VOLUME_STEP = 0.05
TRIM_STEP = 0.05
MIN_TRIMMED_SECONDS = 0.05
VALID_CUSTOM_SLOTS = frozenset({"original", "custom1", "custom2"})
MIN_LONG_SAMPLE_SECONDS = 20.0
# Unit voice lines used for long samples; Zeppelin "ready" is mostly engine ambience.
LONG_SAMPLE_EXCLUDE: dict[str, frozenset[str]] = {
    "Zeppelin": frozenset({"Gbready.wav"}),
}
# Reduce low-frequency engine rumble baked into flying-unit voice takes.
LONG_SAMPLE_AF_FILTER: dict[str, str] = {
    "Zeppelin": "highpass=f=450",
}
ALLOWED_UPLOAD_EXTENSIONS = frozenset({".wav", ".mp3"})
GAME_WAV_CHANNELS = 1
GAME_WAV_RATE = 22050

DEFAULT_GAMESFX_CANDIDATES = [
    Path(r"C:\Program Files (x86)\Warcraft II Remastered\x86\Data\Gamesfx"),
    Path(r"C:\Program Files\Warcraft II Remastered\x86\Data\Gamesfx"),
]

SETUP_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <meta name="color-scheme" content="dark"/>
  <title>WC2r Audio Tool — Setup</title>
  <style>
    html, body { margin: 0; min-height: 100%; background: #0c0e12; color: #e6e9ef;
      font-family: "Segoe UI Variable", "Segoe UI", system-ui, sans-serif; }
    .wrap { max-width: 560px; margin: 0 auto; padding: 48px 24px; }
    h1 { margin: 0 0 8px; font-size: 1.35rem; }
    p { color: #8b93a7; line-height: 1.55; margin: 0 0 16px; }
    label { display: block; font-size: 0.82rem; color: #8b93a7; margin-bottom: 6px; }
    input[type=text] {
      width: 100%; box-sizing: border-box; padding: 10px 12px; border-radius: 8px;
      border: 1px solid #333b4d; background: #1a1f2a; color: #e6e9ef; font-size: 0.9rem;
    }
    .actions { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 16px; }
    .btn {
      padding: 10px 16px; border-radius: 8px; border: 1px solid #384055;
      background: #222836; color: #e6e9ef; cursor: pointer; font-size: 0.88rem;
    }
    .btn.primary { background: #2a4a32; border-color: #4a7a58; }
    .btn:hover { filter: brightness(1.08); }
    .status { margin-top: 20px; font-size: 0.85rem; min-height: 1.4em; }
    .status.ok { color: #7bc96f; }
    .status.err { color: #e07070; }
    .status.busy { color: #e8a84a; }
    code { background: #1a1f2a; padding: 2px 6px; border-radius: 4px; font-size: 0.85em; }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>WC2r Audio Tool</h1>
    <p>Point to your <strong>Warcraft II Remastered</strong> <code>Gamesfx</code> folder to load original voice samples.</p>
    <label for="gamesfx">Gamesfx folder</label>
    <input id="gamesfx" type="text" placeholder="C:\\Program Files (x86)\\Warcraft II Remastered\\x86\\Data\\Gamesfx"/>
    <div class="actions">
      <button class="btn" id="detect">Auto-detect</button>
      <button class="btn primary" id="sync">Load samples</button>
    </div>
    <div class="status" id="status"></div>
  </div>
  <script>
    const status = document.getElementById('status');
    function setStatus(msg, cls) {
      status.textContent = msg;
      status.className = 'status' + (cls ? ' ' + cls : '');
    }
    document.getElementById('detect').onclick = async () => {
      setStatus('Detecting…', 'busy');
      try {
        const res = await fetch('/api/detect-gamesfx');
        const data = await res.json();
        if (data.path) {
          document.getElementById('gamesfx').value = data.path;
          setStatus('Found: ' + data.path, 'ok');
        } else {
          setStatus('Could not find Gamesfx automatically.', 'err');
        }
      } catch (e) { setStatus(e.message, 'err'); }
    };
    document.getElementById('sync').onclick = async () => {
      const path = document.getElementById('gamesfx').value.trim();
      if (!path) { setStatus('Enter a Gamesfx path first.', 'err'); return; }
      setStatus('Loading samples…', 'busy');
      try {
        const res = await fetch('/api/sync', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ gamesfxPath: path }),
        });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Failed');
        setStatus('Done — ' + data.count + ' samples. Reloading…', 'ok');
        setTimeout(() => location.href = '/', 800);
      } catch (e) { setStatus(e.message, 'err'); }
    };
    fetch('/api/status').then(r => r.json()).then(d => {
      if (d.gamesfxPath) document.getElementById('gamesfx').value = d.gamesfxPath;
    });
  </script>
</body>
</html>"""

APP_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <meta name="color-scheme" content="dark"/>
  <title>WC2r Audio Tool</title>
  <style>
    html, body {
      margin: 0; height: 100%;
      background: #0c0e12 !important;
      color: #e6e9ef !important;
      font-family: "Segoe UI Variable", "Segoe UI", system-ui, sans-serif;
      overflow: hidden;
    }
    .app { display: grid; grid-template-rows: auto 1fr; height: 100vh; background: #0c0e12; }
    .topbar {
      display: flex; align-items: center; gap: 16px;
      padding: 14px 20px;
      background: linear-gradient(180deg, #151922 0%, #12151c 100%);
      border-bottom: 1px solid #2a3140;
    }
    .brand h1 { margin: 0; font-size: 1.05rem; font-weight: 650; }
    .brand p { margin: 2px 0 0; font-size: 0.78rem; color: #8b93a7; }
    .top-actions { margin-left: auto; display: flex; gap: 8px; align-items: center; }
    #search {
      width: 240px; padding: 8px 12px; border-radius: 8px;
      border: 1px solid #333b4d; background: #1a1f2a; color: #e6e9ef; font-size: 0.85rem;
    }
    .btn {
      padding: 8px 14px; border-radius: 8px; border: 1px solid #384055;
      background: #222836; color: #e6e9ef; cursor: pointer; font-size: 0.82rem;
    }
    .btn:hover { background: #2d3445; }
    .btn.ghost { background: transparent; }
    .main { display: grid; grid-template-columns: 220px 1fr; min-height: 0; }
    .sidebar {
      background: #10141c; border-right: 1px solid #252b38;
      overflow-y: auto; padding: 10px 8px;
    }
    .sidebar h2 {
      font-size: 0.68rem; text-transform: uppercase; letter-spacing: .08em;
      color: #6d768a; margin: 8px 8px 10px; font-weight: 600;
    }
    #unitNav {
      display: flex; flex-direction: column; gap: 8px;
    }
    .unit-row {
      display: grid; grid-template-columns: 16px minmax(0, 1fr) 54px;
      column-gap: 8px; align-items: center;
    }
    .unit-row input[type=checkbox] {
      width: 16px; height: 16px; accent-color: #7bc96f; flex-shrink: 0;
      cursor: default; grid-column: 1;
    }
    .unit-btn {
      display: flex; justify-content: space-between; align-items: center;
      grid-column: 2; min-width: 0; text-align: left; padding: 9px 10px;
      border: none; border-radius: 8px; background: transparent;
      color: #c5cad6; cursor: pointer; font-size: 0.88rem;
    }
    .unit-btn:hover { background: #1a2030; color: #fff; }
    .unit-btn.active { background: #243048; color: #fff; font-weight: 600; }
    .unit-btn .count {
      font-size: 0.72rem; color: #7a8498; background: #1a2030;
      padding: 2px 7px; border-radius: 999px;
    }
    .unit-btn .count.has-picks { color: #9fd494; background: #1a2a1a; }
    .unit-row .unit-sample {
      grid-column: 3; justify-self: end;
      width: 54px; padding: 4px 6px; font-size: 0.68rem;
      flex-shrink: 0; border-radius: 6px; text-align: center;
    }
    .unit-sample-spacer { grid-column: 3; width: 54px; }
    .long-panel {
      margin: 0 14px 14px; padding: 14px 16px;
      background: #141820; border: 1px solid #3a5240; border-radius: 12px;
      position: relative;
    }
    .long-panel.hidden { display: none; }
    .long-panel-head {
      display: flex; align-items: flex-start; justify-content: space-between;
      gap: 12px; margin-bottom: 10px;
    }
    .long-panel-head-text { min-width: 0; flex: 1; }
    .long-panel h3 { margin: 0 0 4px; font-size: 0.92rem; color: #b8e8b0; }
    .long-panel .long-meta { font-size: 0.74rem; color: #8b93a7; }
    .long-panel-close {
      flex-shrink: 0; width: 28px; height: 28px; padding: 0;
      border-radius: 8px; border: 1px solid #384055;
      background: #222836; color: #c5cad6; cursor: pointer;
      font-size: 1.1rem; line-height: 1; display: flex;
      align-items: center; justify-content: center;
    }
    .long-panel-close:hover { background: #3a2a2a; border-color: #6a4040; color: #f0c0c0; }
    .long-panel .long-actions { display: flex; gap: 8px; margin-top: 10px; flex-wrap: wrap; }
    .content { overflow-y: auto; padding: 16px 20px 32px; }
    .col-head {
      display: grid; grid-template-columns: 140px repeat(3, minmax(0, 1fr));
      gap: 10px; padding: 0 14px 10px;
      font-size: 0.72rem; text-transform: uppercase; letter-spacing: .06em;
      color: #6d768a; position: sticky; top: 0; background: #0c0e12; z-index: 2;
      border-bottom: 1px solid #222833; margin-bottom: 8px;
    }
    .col-head span:nth-child(2) { color: #6b9bd1; }
    .col-head span:nth-child(3), .col-head span:nth-child(4) { color: #e8a84a; }
    .sample {
      display: grid; grid-template-columns: 140px repeat(3, minmax(0, 1fr));
      gap: 10px; align-items: stretch;
      padding: 12px 14px; margin-bottom: 8px;
      background: #141820; border: 1px solid #232a38; border-radius: 12px;
    }
    .sample.has-custom { border-color: #4a3d28; background: #16140f; }
    .sample.has-selection { box-shadow: inset 3px 0 0 #e8a84a; }
    .sample-name-col { display: flex; flex-direction: column; min-width: 0; align-self: stretch; }
    .sample-name { font-size: 0.9rem; font-weight: 600; word-break: break-word; }
    .sample-picked-slot {
      display: flex; flex-direction: column; align-items: center; justify-content: center;
      min-height: calc(1.55rem + 0.72rem + 16px); margin-top: 8px; flex-shrink: 0;
    }
    .sample-picked {
      display: block; width: 100%;
      font-size: 1.55rem; color: #7bc96f; font-weight: 700; line-height: 1;
      text-align: center;
    }
    .sample-picked-speed {
      display: block; margin-top: 4px;
      font-size: 0.68rem; color: #9fd494; font-weight: 600;
      font-variant-numeric: tabular-nums; text-align: center; line-height: 1.25;
    }
    .sample-dur {
      display: block; margin-top: 4px; font-size: 0.74rem; color: #8b93a7;
      font-variant-numeric: tabular-nums;
    }
    .sample-dur .dur-adjusted { color: #b8c4dc; }
    .audio-box {
      display: flex; flex-direction: column; min-width: 0;
      height: 296px; box-sizing: border-box;
      border: 1px solid #333b4d; border-radius: 10px; padding: 10px;
      background: #1a1f2a; transition: border-color .15s, background .15s, box-shadow .15s;
      position: relative; overflow: hidden;
    }
    .audio-box.selected {
      border-color: #e8a84a; background: #1a1812;
      box-shadow: inset 0 0 0 1px #8a7040;
    }
    .audio-box.drag-over { border-color: #e8a84a; background: #2a2418; }
    .box-label {
      font-size: 0.68rem; text-transform: uppercase; letter-spacing: .06em;
      color: #8b93a7; margin-bottom: 8px; font-weight: 600; flex-shrink: 0;
    }
    .box-body { flex: 1; min-width: 0; min-height: 0; display: flex; flex-direction: column; overflow: hidden; }
    .box-empty {
      flex: 1; display: flex; align-items: center; justify-content: center;
      color: #7a8498; font-size: 0.74rem; text-align: center; line-height: 1.35;
      padding: 12px 6px;
    }
    .box-footer {
      display: flex; align-items: center; gap: 6px; flex-wrap: nowrap;
      margin-top: auto; padding-top: 8px; border-top: 1px solid #2a3140;
      height: 44px; flex-shrink: 0; overflow: hidden;
    }
    .box-footer-spacer { border-top-color: transparent; min-height: 0; padding-top: 0; margin-top: auto; }
    .custom-actions-spacer {
      min-width: 148px; height: 1px; margin-left: auto; flex-shrink: 0;
      visibility: hidden; pointer-events: none;
    }
    .track { display: flex; flex-direction: column; gap: 5px; min-width: 0; width: 100%; }
    .player-row { display: flex; align-items: center; gap: 8px; flex-wrap: nowrap; }
    .dur-row {
      display: flex; justify-content: flex-end; min-height: 14px;
      flex-shrink: 0; overflow: visible;
    }
    .play {
      display: inline-flex; align-items: center; gap: 6px; flex-shrink: 0;
      padding: 7px 12px; border-radius: 8px; border: 1px solid #333b4d;
      background: #1c2230; color: #dce2ef; cursor: pointer; font-size: 0.8rem;
    }
    .play:hover { background: #273041; }
    .play.playing { background: #2a3a52; border-color: #4a6280; }
    .play.missing { opacity: 0.45; cursor: default; }
    .progress {
      width: 100%; height: 6px; margin: 0; accent-color: #6b9bd1; cursor: pointer;
    }
    .time-row {
      display: flex; justify-content: space-between; font-size: 0.68rem;
      color: #8b93a7; font-variant-numeric: tabular-nums;
    }
    .speed-row, .volume-row, .trim-row { display: flex; align-items: center; gap: 4px; flex-wrap: nowrap; }
    .speed-row .btn-sm, .volume-row .btn-sm, .trim-row .btn-sm { min-width: 24px; padding: 3px 6px; text-align: center; font-size: 0.72rem; }
    .adjust-label {
      font-size: 0.62rem; text-transform: uppercase; letter-spacing: .05em;
      color: #6d768a; min-width: 20px; flex-shrink: 0;
    }
    .speed-val, .vol-val, .trim-val {
      font-size: 0.68rem; color: #c5cad6; font-variant-numeric: tabular-nums; min-width: 38px;
      text-align: center;
    }
    .trim-row .trim-gap { flex: 1; min-width: 4px; }
    .dur {
      font-size: 0.68rem; color: #8b93a7; font-variant-numeric: tabular-nums; font-weight: 600;
      white-space: nowrap;
    }
    .dur .dur-adjusted { color: #b8c4dc; }
    .use-radio {
      display: inline-flex; align-items: center; gap: 5px; font-size: 0.72rem; color: #8b93a7;
      cursor: pointer; user-select: none; margin-right: auto;
    }
    .use-radio input { accent-color: #e8a84a; cursor: pointer; }
    .use-radio input:disabled { opacity: 0.35; cursor: default; }
    .audio-box.selected .use-radio { color: #f0d090; font-weight: 600; }
    .custom-actions { display: flex; gap: 6px; flex-wrap: nowrap; margin-left: auto; flex-shrink: 0; }
    .specs-btn {
      position: absolute; right: 8px; top: 8px; z-index: 3;
      width: 26px; height: 26px; padding: 0; border-radius: 7px;
      border: 1px solid #455068; background: #222836; color: #b8c4dc;
      font-size: 0.82rem; font-weight: 700; line-height: 1; cursor: pointer;
      display: inline-flex; align-items: center; justify-content: center;
      user-select: none;
    }
    .specs-btn:hover, .specs-btn.active { background: #2a3a52; border-color: #6b9bd1; color: #fff; }
    .specs-overlay {
      display: none; position: absolute; inset: 0; z-index: 2;
      background: rgba(10, 12, 16, 0.94); border-radius: 9px;
      padding: 40px 12px 12px; pointer-events: none;
    }
    .specs-overlay.visible { display: flex; flex-direction: column; justify-content: center; }
    .specs-overlay h4 {
      margin: 0 0 8px; font-size: 0.72rem; text-transform: uppercase;
      letter-spacing: .06em; color: #6b9bd1;
    }
    .specs-grid {
      display: grid; grid-template-columns: auto 1fr; gap: 4px 10px;
      font-size: 0.72rem; color: #8b93a7;
    }
    .specs-grid strong { color: #e6e9ef; font-weight: 600; }
    .specs-grid .ok { color: #7bc96f; }
    .specs-grid .warn { color: #e8a84a; }
    .btn-sm {
      padding: 6px 10px; border-radius: 7px; border: 1px solid #384055;
      background: #222836; color: #e6e9ef; cursor: pointer; font-size: 0.76rem;
      white-space: nowrap; flex-shrink: 0;
    }
    .btn-sm.warn { background: #3d2a1e; border-color: #6a4a32; }
    .btn-sm.upload { background: #2a3420; border-color: #4a5c38; color: #d8e8c8; }
    .empty { color: #6d768a; padding: 40px 20px; text-align: center; }
    .status {
      position: fixed; bottom: 0; left: 220px; right: 0;
      padding: 8px 20px; font-size: 0.8rem; color: #8b93a7;
      background: rgba(12,14,18,.92); border-top: 1px solid #222833;
    }
    .status.ok { color: #7bc96f; }
    .status.err { color: #e07070; }
    input[type=file] { display: none; }

    .tabs { display: flex; gap: 6px; margin-left: 12px; }
    .tab {
      padding: 8px 14px; border-radius: 8px; border: 1px solid transparent;
      background: transparent; color: #9aa3b5; cursor: pointer; font-size: 0.85rem;
    }
    .tab:hover { color: #e6e9ef; background: #1a2030; }
    .tab.active { color: #fff; background: #243048; border-color: #33415c; font-weight: 600; }
    .panel { display: none; min-height: 0; height: 100%; }
    .panel.active { display: grid; grid-template-rows: 1fr; }
    .units-main {
      display: grid; grid-template-columns: 220px 1fr; min-height: 0; height: 100%;
      background: #0c0e12;
    }
    .unit-form {
      overflow-y: auto; padding: 16px 20px 48px; display: grid; gap: 14px;
      grid-template-columns: 1fr 1fr; align-content: start;
    }
    .unit-form h3 {
      grid-column: 1 / -1; margin: 8px 0 0; font-size: 0.75rem; text-transform: uppercase;
      letter-spacing: .08em; color: #6d768a;
    }
    .field { display: flex; flex-direction: column; gap: 6px; }
    .field.wide { grid-column: 1 / -1; }
    .field label { font-size: 0.78rem; color: #8b93a7; }
    .field input, .field select, .field textarea {
      padding: 8px 10px; border-radius: 8px; border: 1px solid #333b4d;
      background: #1a1f2a; color: #e6e9ef; font-size: 0.88rem;
    }
    .field textarea { min-height: 64px; resize: vertical; }
    .spell-hint { font-size: 0.8rem; color: #7bc96f; grid-column: 1 / -1; }
    .form-actions { grid-column: 1 / -1; display: flex; gap: 8px; flex-wrap: wrap; }
    .btn.primary { background: #2f5d3a; border-color: #3f7a4c; }
    .btn.danger { background: #4a2a2a; border-color: #6a3a3a; }
    .audio-mini { grid-column: 1 / -1; border-top: 1px solid #232a38; padding-top: 12px; }
    #unitActions { display: none; }
    audio.track-audio { display: none; }
  </style>
</head>
<body>
  <div class="app">
    <header class="topbar">
      <div class="brand">
        <h1>WC2r Audio Tool</h1>
        <p id="subtitle">Unit voices</p>
      </div>
      <div class="tabs">
        <button type="button" class="tab active" data-tab="audio">Audio</button>
        <button type="button" class="tab" data-tab="units">Units</button>
      </div>
      <div class="top-actions" id="audioActions">
        <input id="search" type="search" placeholder="Search samples…"/>
      </div>
      <div class="top-actions" id="unitActions">
        <button type="button" class="btn" id="newUnit">New unit</button>
        <button type="button" class="btn ghost" id="openPresets">Open presets folder</button>
      </div>
    </header>
    <div class="panel active" id="panel-audio">
    <div class="main">
      <aside class="sidebar">
        <h2>Units</h2>
        <div id="unitNav"></div>
      </aside>
      <section class="content">
        <div id="longSamplePanel" class="long-panel hidden">
          <div class="long-panel-head">
            <div class="long-panel-head-text">
              <h3 id="longSampleTitle">Long sample</h3>
              <div class="long-meta" id="longSampleMeta"></div>
            </div>
            <button type="button" class="long-panel-close" id="closeLongPanel" title="Close sample">×</button>
          </div>
          <div id="longSamplePlayer"></div>
          <div class="long-actions">
            <button type="button" class="btn-sm" id="openLongWav">Open WAV file</button>
          </div>
        </div>
        <div class="col-head">
          <span>File</span><span>Original</span><span>Custom A</span><span>Custom B</span>
        </div>
        <div id="sampleList"></div>
      </section>
    </div>
    </div>
    <div class="panel" id="panel-units">
      <div class="units-main">
        <aside class="sidebar">
          <h2>Custom units</h2>
          <div id="presetNav"></div>
        </aside>
        <section class="unit-form" id="unitForm">
          <div class="empty" style="grid-column:1/-1">Select a unit or create a new one.</div>
        </section>
      </div>
    </div>
    <div class="status" id="status"></div>
  </div>
  <script>
    const DATA = __DATA_JSON__;
    const CATALOG = __CATALOG_JSON__;
    const MIN_SPEED = 0.5, MAX_SPEED = 2.0, SPEED_STEP = 0.05;
    const MIN_VOLUME = 0.0, MAX_VOLUME = 2.0, VOLUME_STEP = 0.05;
    const TRIM_STEP = 0.05;
    let activeUnit = DATA.units[0]?.name || '';
    let currentTab = 'audio';
    let activePresetId = (CATALOG.presets[0] && CATALOG.presets[0].id) || '';
    let draft = CATALOG.presets[0] ? JSON.parse(JSON.stringify(CATALOG.presets[0])) : null;
    let showLongUnit = null;
    let activeAudio = null;
    let activePlayBtn = null;
    let activeTrack = null;
    let progressRaf = null;
    const speedMap = { ...(DATA.playbackSpeeds || {}) };

    function setStatus(msg, ok) {
      const el = document.getElementById('status');
      el.textContent = msg;
      el.className = 'status' + (ok === true ? ' ok' : ok === false ? ' err' : '');
    }

    function formatClock(sec) {
      if (!isFinite(sec) || sec < 0) return '0:00';
      const m = Math.floor(sec / 60);
      const s = Math.floor(sec % 60);
      return m + ':' + String(s).padStart(2, '0');
    }

    function ensureTrackSettings(sampleId) {
      if (!speedMap[sampleId]) {
        speedMap[sampleId] = {
          original: 1, custom1: 1, custom2: 1,
          volume: { original: 1, custom1: 1, custom2: 1 },
          trim: { original: { start: 0, end: 0 }, custom1: { start: 0, end: 0 }, custom2: { start: 0, end: 0 } },
        };
      } else {
        if (!speedMap[sampleId].volume) speedMap[sampleId].volume = { original: 1, custom1: 1, custom2: 1 };
        if (!speedMap[sampleId].trim) speedMap[sampleId].trim = { original: { start: 0, end: 0 }, custom1: { start: 0, end: 0 }, custom2: { start: 0, end: 0 } };
      }
    }

    function getTrim(sampleId, kind) {
      ensureTrackSettings(sampleId);
      const t = speedMap[sampleId].trim[kind] || { start: 0, end: 0 };
      return { start: t.start || 0, end: t.end || 0 };
    }

    function clampTrim(value) {
      return Math.round(Math.max(0, value) * 100) / 100;
    }

    function clampTrimPair(start, end, durSec) {
      let s = clampTrim(start);
      let e = clampTrim(end);
      if (durSec && durSec > 0) {
        s = Math.min(s, durSec);
        e = Math.min(e, Math.max(0, durSec - s - 0.05));
        if (s + e > durSec - 0.05) e = Math.max(0, durSec - s - 0.05);
      }
      return { start: s, end: e };
    }

    function formatTrimLabel(seconds) {
      return clampTrim(seconds).toFixed(2) + 's';
    }

    function effectiveDurationSec(durSec, sampleId, kind) {
      if (durSec == null || !isFinite(durSec) || durSec <= 0) return null;
      const { start, end } = getTrim(sampleId, kind);
      return Math.max(0, durSec - start - end);
    }

    function getPlayBounds(audio, sampleId, kind) {
      const dur = (audio && audio.duration && isFinite(audio.duration)) ? audio.duration : 0;
      const { start, end } = getTrim(sampleId, kind);
      const safeStart = Math.min(start, Math.max(0, dur - 0.05));
      const endTime = Math.max(safeStart + 0.05, dur - end);
      return { start: safeStart, endTime, length: Math.max(0.05, endTime - safeStart) };
    }

    function getSpeed(sampleId, kind) {
      const e = speedMap[sampleId];
      if (!e) return 1;
      const v = e[kind];
      return (typeof v === 'number' && v > 0) ? v : 1;
    }

    function getVolume(sampleId, kind) {
      ensureTrackSettings(sampleId);
      const v = speedMap[sampleId].volume[kind];
      return (typeof v === 'number' && v >= 0) ? v : 1;
    }

    function clampSpeed(rate) {
      return Math.round(Math.max(MIN_SPEED, Math.min(MAX_SPEED, rate)) * 100) / 100;
    }

    function clampVolume(level) {
      return Math.round(Math.max(MIN_VOLUME, Math.min(MAX_VOLUME, level)) * 100) / 100;
    }

    function formatVolumeLabel(level) {
      return Math.round(clampVolume(level) * 100) + '%';
    }

    function previewVolume(level) {
      return Math.min(1, clampVolume(level));
    }

    async function saveSpeed(sampleId, kind, rate, title, unit) {
      ensureTrackSettings(sampleId);
      speedMap[sampleId][kind] = rate;
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      if (sample?.selectedCustom === kind) {
        sample.selectedPlaybackSpeed = rate;
        updatePickedMetaDisplay(sampleId);
        updateSampleDurDisplay(sampleId);
      }
      const row = findSampleRow(sampleId);
      if (row) row.querySelectorAll(`.track[data-kind="${kind}"]`).forEach(updateTrackDurLabel);
      try {
        await fetch('/api/playback-speed', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ sample_id: sampleId, kind, rate, title, unit }),
        });
      } catch (_) { /* ignore */ }
    }

    async function saveVolume(sampleId, kind, level, title, unit) {
      ensureTrackSettings(sampleId);
      speedMap[sampleId].volume[kind] = level;
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      if (sample?.selectedCustom === kind) {
        sample.selectedPlaybackVolume = level;
        updatePickedMetaDisplay(sampleId);
      }
      try {
        await fetch('/api/playback-volume', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ sample_id: sampleId, kind, volume: level, title, unit }),
        });
      } catch (_) { /* ignore */ }
    }

    async function saveTrim(sampleId, kind, trim, title, unit) {
      ensureTrackSettings(sampleId);
      speedMap[sampleId].trim[kind] = trim;
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      if (sample?.selectedCustom === kind) {
        sample.selectedTrimStart = trim.start;
        sample.selectedTrimEnd = trim.end;
        updatePickedMetaDisplay(sampleId);
        updateSampleDurDisplay(sampleId);
      }
      const row = findSampleRow(sampleId);
      if (row) row.querySelectorAll(`.track[data-kind="${kind}"]`).forEach(updateTrackDurLabel);
      try {
        await fetch('/api/playback-trim', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ sample_id: sampleId, kind, trim, title, unit }),
        });
      } catch (_) { /* ignore */ }
    }

    function pickedMetaText(speed, volume, trimStart, trimEnd) {
      let text = `${speed.toFixed(2)}× · ${formatVolumeLabel(volume)}`;
      const ts = trimStart || 0;
      const te = trimEnd || 0;
      if (ts > 0 || te > 0) text += ` · ✂${ts.toFixed(2)}/${te.toFixed(2)}`;
      return text;
    }

    function pickedSlotHtml(s) {
      if (!s.selectedCustom) {
        return '<span class="sample-picked-slot" aria-hidden="true"></span>';
      }
      const kind = s.selectedCustom;
      const spd = s.selectedPlaybackSpeed ?? getSpeed(s.id, kind);
      const vol = s.selectedPlaybackVolume ?? getVolume(s.id, kind);
      const trim = getTrim(s.id, kind);
      const ts = s.selectedTrimStart ?? trim.start;
      const te = s.selectedTrimEnd ?? trim.end;
      return `<span class="sample-picked-slot"><span class="sample-picked" aria-label="Use this selected">✓</span><span class="sample-picked-speed">${pickedMetaText(spd, vol, ts, te)}</span></span>`;
    }

    function updatePickedMetaDisplay(sampleId) {
      const row = findSampleRow(sampleId);
      const metaEl = row?.querySelector('.sample-picked-speed');
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      if (!metaEl || !sample?.selectedCustom) return;
      const kind = sample.selectedCustom;
      const spd = sample.selectedPlaybackSpeed ?? getSpeed(sampleId, kind);
      const vol = sample.selectedPlaybackVolume ?? getVolume(sampleId, kind);
      const trim = getTrim(sampleId, kind);
      const ts = sample.selectedTrimStart ?? trim.start;
      const te = sample.selectedTrimEnd ?? trim.end;
      metaEl.textContent = pickedMetaText(spd, vol, ts, te);
    }

    function stopProgressLoop() {
      if (progressRaf) cancelAnimationFrame(progressRaf);
      progressRaf = null;
    }

    function updateTrackProgress(track, audio) {
      if (!track || !audio) return;
      const bar = track.querySelector('.progress');
      const cur = track.querySelector('.time-current');
      const total = track.querySelector('.time-total');
      const sampleId = track.dataset.sampleId;
      const kind = track.dataset.kind;
      const bounds = getPlayBounds(audio, sampleId, kind);
      if (bounds.length > 0) {
        if (bar && !bar._seeking) {
          bar.value = ((audio.currentTime - bounds.start) / bounds.length) * 100;
        }
        if (total) total.textContent = formatClock(bounds.length);
      }
      if (cur) cur.textContent = formatClock(Math.max(0, audio.currentTime - bounds.start));
    }

    function startProgressLoop(track, audio) {
      stopProgressLoop();
      const sampleId = track.dataset.sampleId;
      const kind = track.dataset.kind;
      function tick() {
        if (activeAudio !== audio) return;
        const bounds = getPlayBounds(audio, sampleId, kind);
        if (audio.currentTime >= bounds.endTime - 0.02) {
          audio.pause();
          const btn = track.querySelector('.play');
          if (btn) onAudioEnded(btn, track, audio);
          return;
        }
        updateTrackProgress(track, audio);
        progressRaf = requestAnimationFrame(tick);
      }
      progressRaf = requestAnimationFrame(tick);
    }

    function resetTrackProgress(track) {
      if (!track) return;
      const bar = track.querySelector('.progress');
      const cur = track.querySelector('.time-current');
      if (bar) { bar.value = 0; bar._seeking = false; }
      if (cur) cur.textContent = '0:00';
    }

    function stopAll() {
      stopProgressLoop();
      document.querySelectorAll('audio.track-audio').forEach(a => { a.pause(); a.currentTime = 0; });
      document.querySelectorAll('.play.playing').forEach(b => b.classList.remove('playing'));
      if (activeTrack) resetTrackProgress(activeTrack);
      activeAudio = null;
      activePlayBtn = null;
      activeTrack = null;
    }

    function onAudioEnded(btn, track, audio) {
      btn.classList.remove('playing');
      stopProgressLoop();
      resetTrackProgress(track);
      if (activeAudio === audio) {
        activeAudio = null;
        activePlayBtn = null;
        activeTrack = null;
      }
    }

    function playAudio(btn) {
      const track = btn.closest('.track');
      if (!track) return;
      const audio = track.querySelector('audio.track-audio');
      if (!audio || !audio.getAttribute('src')) return;
      const sampleId = track.dataset.sampleId;
      const kind = track.dataset.kind;
      const rate = getSpeed(sampleId, kind);
      const volume = previewVolume(getVolume(sampleId, kind));

      if (activePlayBtn === btn && activeAudio === audio && !audio.paused) {
        audio.pause();
        btn.classList.remove('playing');
        stopProgressLoop();
        return;
      }
      if (activePlayBtn === btn && activeAudio === audio && audio.paused) {
        audio.playbackRate = rate;
        audio.volume = volume;
        const bounds = getPlayBounds(audio, sampleId, kind);
        if (audio.currentTime < bounds.start || audio.currentTime >= bounds.endTime) {
          audio.currentTime = bounds.start;
        }
        audio.play().catch(err => setStatus(err.message, false));
        btn.classList.add('playing');
        startProgressLoop(track, audio);
        return;
      }

      stopAll();
      audio.onended = () => onAudioEnded(btn, track, audio);
      audio.onloadedmetadata = () => updateTrackProgress(track, audio);
      audio.playbackRate = rate;
      audio.volume = volume;
      const bounds = getPlayBounds(audio, sampleId, kind);
      audio.currentTime = bounds.start;
      audio.play().catch(err => setStatus(err.message, false));
      btn.classList.add('playing');
      activeAudio = audio;
      activePlayBtn = btn;
      activeTrack = track;
      updateTrackProgress(track, audio);
      startProgressLoop(track, audio);
    }

    function fmtDurationJs(seconds) {
      if (seconds == null || !isFinite(seconds) || seconds < 0) return '—';
      if (seconds < 60) return seconds.toFixed(2) + 's';
      const minutes = Math.floor(seconds / 60);
      return `${minutes}:${(seconds % 60).toFixed(2).padStart(5, '0')}`;
    }

    function formatDurPair(durSec, durFmt, speed, sampleId, kind) {
      const effective = (sampleId && kind) ? effectiveDurationSec(durSec, sampleId, kind) : durSec;
      const original = durFmt || fmtDurationJs(effective);
      if (effective == null || !isFinite(effective) || effective <= 0) return original;
      const rate = (typeof speed === 'number' && speed > 0) ? speed : 1;
      const adjusted = fmtDurationJs(effective / rate);
      return `${original} : ${adjusted}`;
    }

    function formatDurPairHtml(durSec, durFmt, speed, sampleId, kind) {
      const effective = (sampleId && kind) ? effectiveDurationSec(durSec, sampleId, kind) : durSec;
      const trim = (sampleId && kind) ? getTrim(sampleId, kind) : { start: 0, end: 0 };
      const hasTrim = (trim.start || 0) > 0 || (trim.end || 0) > 0;
      const original = hasTrim ? fmtDurationJs(effective) : (durFmt || fmtDurationJs(effective));
      if (effective == null || !isFinite(effective) || effective <= 0) return original;
      const rate = (typeof speed === 'number' && speed > 0) ? speed : 1;
      const adjusted = fmtDurationJs(effective / rate);
      return `${original} : <span class="dur-adjusted">${adjusted}</span>`;
    }

    function updateTrackDurLabel(track) {
      if (!track) return;
      const durSec = parseFloat(track.dataset.durSec);
      const durFmt = track.dataset.durFmt || '';
      const el = track.querySelector('.dur');
      if (!el) return;
      el.innerHTML = formatDurPairHtml(
        isFinite(durSec) ? durSec : null,
        durFmt,
        getSpeed(track.dataset.sampleId, track.dataset.kind),
        track.dataset.sampleId,
        track.dataset.kind,
      );
    }

    function trackHtml(src, durFmt, sampleId, kind, durSec) {
      if (!src) {
        return '<div class="track track-empty"><span class="dur">—</span><button type="button" class="play missing" disabled>— none</button></div>';
      }
      const safe = src.replace(/"/g, '&quot;');
      const spd = getSpeed(sampleId, kind).toFixed(2);
      const vol = formatVolumeLabel(getVolume(sampleId, kind));
      const trim = getTrim(sampleId, kind);
      const safeId = sampleId.replace(/"/g, '&quot;');
      const durAttr = (durSec != null && isFinite(durSec)) ? ` data-dur-sec="${durSec}"` : '';
      const durLabel = formatDurPairHtml(durSec, durFmt, getSpeed(sampleId, kind), sampleId, kind);
      return `<div class="track" data-sample-id="${safeId}" data-kind="${kind}" data-dur-fmt="${(durFmt || '').replace(/"/g, '&quot;')}"${durAttr}>
        <audio class="track-audio" preload="metadata" src="${safe}"></audio>
        <div class="player-row">
          <button type="button" class="play">▶ Play</button>
        </div>
        <div class="dur-row"><span class="dur">${durLabel}</span></div>
        <input type="range" class="progress" min="0" max="100" step="0.1" value="0">
        <div class="time-row"><span class="time-current">0:00</span><span class="time-total">0:00</span></div>
        <div class="speed-row">
          <span class="adjust-label">Spd</span>
          <button type="button" class="btn-sm speed-down" data-id="${safeId}" data-kind="${kind}">−</button>
          <span class="speed-val">${spd}×</span>
          <button type="button" class="btn-sm speed-up" data-id="${safeId}" data-kind="${kind}">+</button>
        </div>
        <div class="volume-row">
          <span class="adjust-label">Vol</span>
          <button type="button" class="btn-sm vol-down" data-id="${safeId}" data-kind="${kind}">−</button>
          <span class="vol-val">${vol}</span>
          <button type="button" class="btn-sm vol-up" data-id="${safeId}" data-kind="${kind}">+</button>
        </div>
        <div class="trim-row">
          <span class="adjust-label">In</span>
          <button type="button" class="btn-sm trim-in-down" data-id="${safeId}" data-kind="${kind}">−</button>
          <span class="trim-val trim-in-val">${formatTrimLabel(trim.start)}</span>
          <button type="button" class="btn-sm trim-in-up" data-id="${safeId}" data-kind="${kind}">+</button>
          <span class="trim-gap"></span>
          <span class="adjust-label">Out</span>
          <button type="button" class="btn-sm trim-out-down" data-id="${safeId}" data-kind="${kind}">−</button>
          <span class="trim-val trim-out-val">${formatTrimLabel(trim.end)}</span>
          <button type="button" class="btn-sm trim-out-up" data-id="${safeId}" data-kind="${kind}">+</button>
        </div>
      </div>`;
    }

    function specsOverlayHtml(specs) {
      if (!specs) return '';
      const gameClass = specs.gameReady ? 'ok' : 'warn';
      return `<div class="specs-overlay">
        <h4>Audio specs</h4>
        <div class="specs-grid">
          <span>Format</span><strong>${specs.format}</strong>
          <span>Channels</span><strong>${specs.channelsLabel}</strong>
          <span>Sample rate</span><strong>${specs.rateLabel}</strong>
          <span>Bit depth</span><strong>${specs.bitsLabel}</strong>
          <span>Duration</span><strong>${specs.durationFmt}</strong>
          <span>File size</span><strong>${specs.sizeLabel}</strong>
          <span>WC2 ready</span><strong class="${gameClass}">${specs.gameReadyLabel}</strong>
        </div>
      </div>
      <button type="button" class="specs-btn" title="Show audio specs">ⓘ</button>`;
    }

    function audioBoxHtml(label, bodyHtml, footerHtml, specs, extraClass, slotKey) {
      const slotAttr = slotKey ? ` data-slot-key="${slotKey}"` : '';
      return `<div class="audio-box${extraClass || ''}"${slotAttr}>
        <div class="box-label">${label}</div>
        <div class="box-body">${bodyHtml}</div>
        <div class="box-footer">${footerHtml}</div>
        ${specsOverlayHtml(specs)}
      </div>`;
    }

    function findSampleRow(sampleId) {
      return [...document.querySelectorAll('.sample')].find(el => el.dataset.id === sampleId) || null;
    }

    function toggleSpecs(btn) {
      const overlay = btn.closest('.audio-box')?.querySelector('.specs-overlay');
      const open = btn.classList.toggle('active');
      overlay?.classList.toggle('visible', open);
    }

    function originalBoxHtml(s) {
      const safeId = s.id.replace(/"/g, '&quot;');
      const isSelected = s.selectedCustom === 'original';
      const footer = `<label class="use-radio"><input type="checkbox" name="use-${safeId}" class="use-pick" data-id="${safeId}" data-slot-key="original"${isSelected ? ' checked' : ''}> Use this</label><div class="custom-actions-spacer" aria-hidden="true"></div>`;
      return audioBoxHtml(
        'Original',
        trackHtml(s.vanilla, s.vanillaDurFmt, s.id, 'original', s.vanillaDur),
        footer,
        s.vanillaSpecs,
        `${isSelected ? ' selected' : ''}`,
        'original',
      );
    }

    function adjustSpeed(track, delta) {
      const sampleId = track.dataset.sampleId;
      const kind = track.dataset.kind;
      const next = clampSpeed(getSpeed(sampleId, kind) + delta);
      const valEl = track.querySelector('.speed-val');
      if (valEl) valEl.textContent = next.toFixed(2) + '×';
      updateTrackDurLabel(track);
      if (activeTrack === track && activeAudio) activeAudio.playbackRate = next;
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      saveSpeed(sampleId, kind, next, sample?.file, sample?.unit);
    }

    function adjustVolume(track, delta) {
      const sampleId = track.dataset.sampleId;
      const kind = track.dataset.kind;
      const next = clampVolume(getVolume(sampleId, kind) + delta);
      const valEl = track.querySelector('.vol-val');
      if (valEl) valEl.textContent = formatVolumeLabel(next);
      if (activeTrack === track && activeAudio) activeAudio.volume = previewVolume(next);
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      saveVolume(sampleId, kind, next, sample?.file, sample?.unit);
    }

    function adjustTrim(track, edge, delta) {
      const sampleId = track.dataset.sampleId;
      const kind = track.dataset.kind;
      const durSec = parseFloat(track.dataset.durSec);
      const cur = getTrim(sampleId, kind);
      let start = cur.start;
      let end = cur.end;
      if (edge === 'in') start = clampTrim(start + delta);
      else end = clampTrim(end + delta);
      const next = clampTrimPair(start, end, isFinite(durSec) ? durSec : null);
      const inEl = track.querySelector('.trim-in-val');
      const outEl = track.querySelector('.trim-out-val');
      if (inEl) inEl.textContent = formatTrimLabel(next.start);
      if (outEl) outEl.textContent = formatTrimLabel(next.end);
      updateTrackDurLabel(track);
      if (activeTrack === track && activeAudio) {
        const bounds = getPlayBounds(activeAudio, sampleId, kind);
        if (activeAudio.currentTime < bounds.start) activeAudio.currentTime = bounds.start;
        if (activeAudio.currentTime >= bounds.endTime) {
          activeAudio.pause();
          const btn = track.querySelector('.play');
          if (btn) onAudioEnded(btn, track, activeAudio);
        } else {
          updateTrackProgress(track, activeAudio);
        }
      }
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      saveTrim(sampleId, kind, next, sample?.file, sample?.unit);
    }

    async function uploadCustom(sampleId, slot, file) {
      if (!file) return;
      const ext = file.name.toLowerCase();
      if (!ext.endsWith('.wav') && !ext.endsWith('.mp3')) {
        setStatus('WAV or MP3 only', false);
        return;
      }
      const fd = new FormData();
      fd.append('sample_id', sampleId);
      fd.append('slot', String(slot));
      fd.append('file', file);
      setStatus('Saving…');
      try {
        const res = await fetch('/api/replacer', { method: 'POST', body: fd });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Failed');
        applyUploadedCustom(data);
        setStatus(data.converted ? 'MP3 converted to WAV — press ▶ to play' : 'Custom WAV loaded — press ▶ to play', true);
      } catch (err) { setStatus(err.message, false); }
    }

    function applyUploadedCustom(data) {
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === data.sample_id);
      if (!sample) {
        location.reload();
        return;
      }
      const slotKey = data.slotKey;
      sample[slotKey] = { url: data.url, dur: data.dur, durFmt: data.durFmt, specs: data.specs || null };
      if (slotKey === 'custom1') sample.hasCustom1 = true;
      if (slotKey === 'custom2') sample.hasCustom2 = true;
      renderSamples();
      renderNav();
    }

    function applyRemovedCustom(sampleId, slotNum) {
      const slotKey = slotNum === 1 ? 'custom1' : 'custom2';
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      if (!sample) {
        location.reload();
        return;
      }
      sample[slotKey] = null;
      if (slotKey === 'custom1') sample.hasCustom1 = false;
      if (slotKey === 'custom2') sample.hasCustom2 = false;
      if (sample.selectedCustom === slotKey) {
        sample.selectedCustom = '';
        sample.selectedPlaybackSpeed = null;
        sample.selectedPlaybackVolume = null;
        sample.selectedTrimStart = null;
        sample.selectedTrimEnd = null;
      }
      DATA.customCount = DATA.units
        .flatMap(u => u.samples)
        .filter(s => s.hasCustom1 || s.hasCustom2).length;
      stopAll();
      renderSamples();
      renderNav();
    }

    async function removeCustom(sampleId, slot) {
      setStatus('Removing…');
      try {
        const res = await fetch('/api/replacer/' + encodeURIComponent(sampleId) + '?slot=' + slot, { method: 'DELETE' });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Failed');
        applyRemovedCustom(sampleId, Number(slot));
        setStatus('Custom audio removed', true);
      } catch (err) { setStatus(err.message, false); }
    }

    async function setCustomSelection(sampleId, slotKey) {
      setStatus('Saving…');
      try {
        const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
        const res = await fetch('/api/selection', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            sample_id: sampleId,
            choice: slotKey,
            title: sample?.file,
            unit: sample?.unit,
            playbackSpeed: getSpeed(sampleId, slotKey),
            playbackVolume: getVolume(sampleId, slotKey),
            trimStart: getTrim(sampleId, slotKey).start,
            trimEnd: getTrim(sampleId, slotKey).end,
          }),
        });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Failed');
        if (sample) {
          sample.selectedCustom = slotKey;
          sample.selectedPlaybackSpeed = data.playbackSpeed ?? getSpeed(sampleId, slotKey);
          sample.selectedPlaybackVolume = data.playbackVolume ?? getVolume(sampleId, slotKey);
          sample.selectedTrimStart = data.trimStart ?? getTrim(sampleId, slotKey).start;
          sample.selectedTrimEnd = data.trimEnd ?? getTrim(sampleId, slotKey).end;
        }
        applySelectionUi(sampleId, slotKey, sample?.selectedPlaybackSpeed, sample?.selectedPlaybackVolume, sample?.selectedTrimStart, sample?.selectedTrimEnd);
        renderNav();
        setStatus(`Selection saved (${pickedMetaText(
          data.playbackSpeed ?? getSpeed(sampleId, slotKey),
          data.playbackVolume ?? getVolume(sampleId, slotKey),
          data.trimStart ?? getTrim(sampleId, slotKey).start,
          data.trimEnd ?? getTrim(sampleId, slotKey).end,
        )})`, true);
      } catch (err) { setStatus(err.message, false); }
    }

    async function clearCustomSelection(sampleId) {
      setStatus('Saving…');
      try {
        const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
        const res = await fetch('/api/selection', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ sample_id: sampleId, choice: '', title: sample?.file, unit: sample?.unit }),
        });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Failed');
        if (sample) {
          sample.selectedCustom = '';
          sample.selectedPlaybackSpeed = null;
          sample.selectedPlaybackVolume = null;
          sample.selectedTrimStart = null;
          sample.selectedTrimEnd = null;
        }
        applySelectionUi(sampleId, '', null, null, null, null);
        renderNav();
        setStatus('Selection cleared', true);
      } catch (err) { setStatus(err.message, false); }
    }

    function updateSampleDurDisplay(sampleId) {
      const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
      const row = findSampleRow(sampleId);
      const old = row?.querySelector('.sample-dur');
      if (!sample || !old) return;
      const tmp = document.createElement('div');
      tmp.innerHTML = sampleDurHtml(sample);
      old.replaceWith(tmp.firstElementChild);
    }

    function applySelectionUi(sampleId, slotKey, playbackSpeed, playbackVolume, trimStart, trimEnd) {
      const row = findSampleRow(sampleId);
      if (!row) {
        renderSamples();
        return;
      }
      row.querySelectorAll('.audio-box').forEach(el => {
        el.classList.toggle('selected', !!slotKey && el.dataset.slotKey === slotKey);
      });
      row.querySelectorAll('.use-pick').forEach(r => {
        r.checked = !!slotKey && r.dataset.slotKey === slotKey;
      });
      row.classList.toggle('has-selection', !!slotKey);
      const nameCol = row.querySelector('.sample-name-col');
      if (!nameCol) return;
      const pickedSlot = nameCol.querySelector('.sample-picked-slot');
      if (pickedSlot) {
        if (slotKey) {
          const spd = playbackSpeed ?? getSpeed(sampleId, slotKey);
          const vol = playbackVolume ?? getVolume(sampleId, slotKey);
          const trim = getTrim(sampleId, slotKey);
          const ts = trimStart ?? trim.start;
          const te = trimEnd ?? trim.end;
          pickedSlot.innerHTML = `<span class="sample-picked" aria-label="Use this selected">✓</span><span class="sample-picked-speed">${pickedMetaText(spd, vol, ts, te)}</span>`;
          pickedSlot.removeAttribute('aria-hidden');
        } else {
          pickedSlot.innerHTML = '';
          pickedSlot.setAttribute('aria-hidden', 'true');
        }
      }
      updateSampleDurDisplay(sampleId);
    }

    function sampleDurHtml(s) {
      let durSec = s.vanillaDur;
      let durFmt = s.vanillaDurFmt;
      let speed = getSpeed(s.id, 'original');
      let kind = 'original';
      if (s.selectedCustom === 'original') {
        kind = 'original';
        speed = s.selectedPlaybackSpeed ?? getSpeed(s.id, 'original');
      } else if (s.selectedCustom === 'custom1' && s.custom1) {
        kind = 'custom1';
        durSec = s.custom1.dur;
        durFmt = s.custom1.durFmt;
        speed = s.selectedPlaybackSpeed ?? getSpeed(s.id, 'custom1');
      } else if (s.selectedCustom === 'custom2' && s.custom2) {
        kind = 'custom2';
        durSec = s.custom2.dur;
        durFmt = s.custom2.durFmt;
        speed = s.selectedPlaybackSpeed ?? getSpeed(s.id, 'custom2');
      }
      return `<span class="sample-dur">${formatDurPairHtml(durSec, durFmt, speed, s.id, kind)}</span>`;
    }

    function customSlotHtml(s, slotNum) {
      const slotKey = slotNum === 1 ? 'custom1' : 'custom2';
      const slot = s[slotKey];
      const hasFile = !!(slot && slot.url);
      const isSelected = s.selectedCustom === slotKey;
      const safeId = s.id.replace(/"/g, '&quot;');
      const label = slotNum === 1 ? 'Custom A' : 'Custom B';
      const body = hasFile
        ? trackHtml(slot.url, slot.durFmt, s.id, slotKey, slot.dur)
        : '<div class="box-empty">Drop WAV/MP3 here</div>';
      const footer = hasFile
        ? `<label class="use-radio"><input type="checkbox" name="use-${safeId}" class="use-pick" data-id="${safeId}" data-slot-key="${slotKey}"${isSelected ? ' checked' : ''}> Use this</label>
           <div class="custom-actions">
             <label class="btn-sm upload">Replace<input type="file" accept=".wav,.mp3,audio/wav,audio/mpeg" data-sample-id="${safeId}" data-slot="${slotNum}"></label>
             <button type="button" class="btn-sm warn del" data-sample-id="${safeId}" data-slot="${slotNum}">Remove</button>
           </div>`
        : `<label class="use-radio"><input type="checkbox" name="use-${safeId}" class="use-pick" data-id="${safeId}" data-slot-key="${slotKey}" disabled> Use this</label>
           <div class="custom-actions">
             <label class="btn-sm upload">Load audio<input type="file" accept=".wav,.mp3,audio/wav,audio/mpeg" data-sample-id="${safeId}" data-slot="${slotNum}"></label>
           </div>`;
      const boxClass = ` custom-box${hasFile ? '' : ' empty'}${isSelected ? ' selected' : ''}`;
      return `<div class="audio-box${boxClass}" data-sample-id="${safeId}" data-slot="${slotNum}" data-slot-key="${slotKey}">
        <div class="box-label">${label}</div>
        <div class="box-body">${body}</div>
        <div class="box-footer">${footer}</div>
        ${specsOverlayHtml(hasFile && slot.specs ? slot.specs : null)}
      </div>`;
    }

    function openLongSample(unitName) {
      const unit = DATA.units.find(u => u.name === unitName);
      if (!unit?.longSample) return;
      showLongUnit = unitName;
      activeUnit = unitName;
      stopAll();
      renderNav();
      renderLongPanel();
      renderSamples();
    }

    function renderLongPanel() {
      const panel = document.getElementById('longSamplePanel');
      if (!showLongUnit) {
        panel.classList.add('hidden');
        return;
      }
      const unit = DATA.units.find(u => u.name === showLongUnit);
      if (!unit?.longSample) {
        panel.classList.add('hidden');
        return;
      }
      const ls = unit.longSample;
      panel.classList.remove('hidden');
      document.getElementById('longSampleTitle').textContent =
        `${showLongUnit} — long sample (${ls.durFmt})`;
      document.getElementById('longSampleMeta').textContent =
        `${ls.filename} · mono 22050 Hz WAV`;
      document.getElementById('longSamplePlayer').innerHTML = audioBoxHtml(
        'Long sample',
        trackHtml(ls.url, ls.durFmt, `long/${showLongUnit}`, 'original', ls.dur),
        '<span class="box-footer-spacer"></span>',
        ls.specs,
        '',
      );
    }

    function unitPickedCount(u) {
      return u.samples.filter(s => s.selectedCustom).length;
    }

    function renderNav() {
      const nav = document.getElementById('unitNav');
      nav.innerHTML = DATA.units.map(u => {
        const picked = unitPickedCount(u);
        return `
        <div class="unit-row">
          <input type="checkbox"${u.complete ? ' checked' : ''} disabled title="${u.complete ? 'All samples have a custom WAV' : 'Not all samples filled yet'}"/>
          <button class="unit-btn${u.name === activeUnit ? ' active' : ''}" data-unit="${u.name}">
            ${u.name}<span class="count${picked ? ' has-picks' : ''}" title="Use this selections">${picked}/${u.samples.length}</span>
          </button>
          ${u.longSample
            ? `<button type="button" class="btn-sm unit-sample" data-unit="${u.name}">Sample</button>`
            : '<span class="unit-sample-spacer" aria-hidden="true"></span>'}
        </div>`;
      }).join('');
      nav.querySelectorAll('.unit-btn').forEach(btn => {
        btn.onclick = () => {
          activeUnit = btn.dataset.unit;
          renderNav();
          renderLongPanel();
          renderSamples();
        };
      });
      nav.querySelectorAll('.unit-sample').forEach(btn => {
        btn.onclick = (e) => {
          e.stopPropagation();
          openLongSample(btn.dataset.unit);
        };
      });
      const unit = DATA.units.find(u => u.name === activeUnit);
      const picked = unit ? unitPickedCount(unit) : 0;
      document.getElementById('subtitle').textContent =
        `${DATA.total} samples · ${DATA.customCount} with custom · ✓${picked}/${unit?.samples.length || 0} picked in ${activeUnit}`;
    }

    function renderSamples() {
      const unit = DATA.units.find(u => u.name === activeUnit);
      const list = document.getElementById('sampleList');
      const q = document.getElementById('search').value.trim().toLowerCase();
      if (!unit) { list.innerHTML = '<div class="empty">No unit selected</div>'; return; }
      const filtered = unit.samples.filter(s =>
        !q || s.file.toLowerCase().includes(q) || s.id.toLowerCase().includes(q));
      if (!filtered.length) {
        list.innerHTML = '<div class="empty">No samples match your search</div>';
        return;
      }
      list.innerHTML = filtered.map(s => {
        const hasAny = s.hasCustom1 || s.hasCustom2;
        return `
        <article class="sample${hasAny ? ' has-custom' : ''}${s.selectedCustom ? ' has-selection' : ''}" data-id="${s.id}">
          <div class="sample-name-col">
            <div class="sample-name">${s.file}</div>
            ${sampleDurHtml(s)}
            ${pickedSlotHtml(s)}
          </div>
          ${originalBoxHtml(s)}
          ${customSlotHtml(s, 1)}
          ${customSlotHtml(s, 2)}
        </article>`;
      }).join('');
      renderNav();
    }

    const content = document.querySelector('.content');

    content.addEventListener('click', async e => {
      const pick = e.target.closest('.use-pick') || e.target.closest('.use-radio')?.querySelector('.use-pick');
      if (pick && !pick.disabled) {
        e.preventDefault();
        const sampleId = pick.dataset.id;
        const slotKey = pick.dataset.slotKey;
        const sample = DATA.units.flatMap(u => u.samples).find(s => s.id === sampleId);
        if (sample?.selectedCustom === slotKey) {
          await clearCustomSelection(sampleId);
        } else {
          await setCustomSelection(sampleId, slotKey);
        }
        return;
      }
      const specsBtn = e.target.closest('.specs-btn');
      if (specsBtn) {
        e.preventDefault();
        toggleSpecs(specsBtn);
        return;
      }
      const play = e.target.closest('button.play');
      if (play && !play.disabled) {
        playAudio(play);
        return;
      }
      const down = e.target.closest('.speed-down');
      if (down) {
        adjustSpeed(down.closest('.track'), -SPEED_STEP);
        return;
      }
      const up = e.target.closest('.speed-up');
      if (up) {
        adjustSpeed(up.closest('.track'), SPEED_STEP);
        return;
      }
      const volDown = e.target.closest('.vol-down');
      if (volDown) {
        adjustVolume(volDown.closest('.track'), -VOLUME_STEP);
        return;
      }
      const volUp = e.target.closest('.vol-up');
      if (volUp) {
        adjustVolume(volUp.closest('.track'), VOLUME_STEP);
        return;
      }
      const trimInDown = e.target.closest('.trim-in-down');
      if (trimInDown) {
        adjustTrim(trimInDown.closest('.track'), 'in', -TRIM_STEP);
        return;
      }
      const trimInUp = e.target.closest('.trim-in-up');
      if (trimInUp) {
        adjustTrim(trimInUp.closest('.track'), 'in', TRIM_STEP);
        return;
      }
      const trimOutDown = e.target.closest('.trim-out-down');
      if (trimOutDown) {
        adjustTrim(trimOutDown.closest('.track'), 'out', -TRIM_STEP);
        return;
      }
      const trimOutUp = e.target.closest('.trim-out-up');
      if (trimOutUp) {
        adjustTrim(trimOutUp.closest('.track'), 'out', TRIM_STEP);
        return;
      }
      const del = e.target.closest('.del');
      if (del) {
        await removeCustom(del.dataset.sampleId, del.dataset.slot || '1');
        return;
      }
    });

    content.addEventListener('change', async e => {
      if (e.target.type === 'file' && e.target.files[0]) {
        await uploadCustom(e.target.dataset.sampleId, e.target.dataset.slot || '1', e.target.files[0]);
        e.target.value = '';
      }
    });

    content.addEventListener('pointerdown', e => {
      const bar = e.target.closest('.progress');
      if (bar) bar._seeking = true;
    });
    content.addEventListener('input', e => {
      const bar = e.target.closest('.progress');
      if (!bar) return;
      const track = bar.closest('.track');
      const audio = track?.querySelector('audio.track-audio');
      if (!audio || !audio.duration) return;
      const bounds = getPlayBounds(audio, track.dataset.sampleId, track.dataset.kind);
      audio.currentTime = bounds.start + (parseFloat(bar.value) / 100) * bounds.length;
      updateTrackProgress(track, audio);
    });
    content.addEventListener('pointerup', e => {
      const bar = e.target.closest('.progress');
      if (bar) bar._seeking = false;
    });

    content.addEventListener('dragover', e => {
      const zone = e.target.closest('.audio-box.custom-box');
      if (!zone) return;
      e.preventDefault();
      zone.classList.add('drag-over');
    });
    content.addEventListener('dragleave', e => {
      const zone = e.target.closest('.audio-box.custom-box');
      if (zone) zone.classList.remove('drag-over');
    });
    content.addEventListener('drop', e => {
      const zone = e.target.closest('.audio-box.custom-box');
      if (!zone) return;
      e.preventDefault();
      zone.classList.remove('drag-over');
      const file = [...(e.dataTransfer.files || [])].find(f => {
        const n = f.name.toLowerCase();
        return n.endsWith('.wav') || n.endsWith('.mp3');
      });
      if (file) uploadCustom(zone.dataset.sampleId, zone.dataset.slot, file);
      else setStatus('Drop a .wav or .mp3 file', false);
    });

    document.getElementById('search').oninput = () => { stopAll(); renderSamples(); };

    function closeLongPanel() {
      showLongUnit = null;
      stopAll();
      renderLongPanel();
    }

    document.getElementById('openLongWav').onclick = async () => {
      if (!showLongUnit) return;
      try {
        await fetch('/api/open-long-sample', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ unit: showLongUnit }),
        });
      } catch (err) { setStatus(err.message, false); }
    };
    document.getElementById('closeLongPanel').onclick = closeLongPanel;


    function switchTab(tab) {
      currentTab = tab;
      document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tab));
      document.getElementById('panel-audio').classList.toggle('active', tab === 'audio');
      document.getElementById('panel-units').classList.toggle('active', tab === 'units');
      document.getElementById('audioActions').style.display = tab === 'audio' ? 'flex' : 'none';
      document.getElementById('unitActions').style.display = tab === 'units' ? 'flex' : 'none';
      if (tab === 'units') { renderPresetNav(); renderUnitForm(); }
      else {
        const unit = DATA.units.find(u => u.name === activeUnit);
        const picked = unit ? unitPickedCount(unit) : 0;
        document.getElementById('subtitle').textContent =
          `${DATA.total} samples · ${DATA.customCount} with custom · ✓${picked}/${unit?.samples.length || 0} picked in ${activeUnit}`;
      }
    }
    document.querySelectorAll('.tab').forEach(t => t.onclick = () => switchTab(t.dataset.tab));

    function blankDraft() {
      return {
        id: '',
        displayName: 'New Hero',
        baseUnitType: 0x0C,
        audioBank: CATALOG.audioBanks.includes('Human') ? 'Human' : (CATALOG.audioBanks[0] || ''),
        stats: { ...CATALOG.defaultStats },
        notes: ''
      };
    }

    function previewPlay(btn, src) {
      if (!src) return;
      stopAll();
      const audio = new Audio(src);
      audio.onended = () => btn.classList.remove('playing');
      audio.play();
      btn.classList.add('playing');
      activePlayBtn = btn;
    }

    function renderPresetNav() {
      const nav = document.getElementById('presetNav');
      if (!CATALOG.presets.length) {
        nav.innerHTML = '<div class="empty" style="padding:12px">No presets yet</div>';
        return;
      }
      nav.innerHTML = CATALOG.presets.map(p => `
        <button type="button" class="unit-btn${p.id === activePresetId ? ' active' : ''}" data-id="${p.id}">
          ${p.displayName}<span class="count">0x${Number(p.baseUnitType).toString(16).padStart(2,'0')}</span>
        </button>`).join('');
      nav.querySelectorAll('.unit-btn').forEach(btn => {
        btn.onclick = () => {
          activePresetId = btn.dataset.id;
          draft = JSON.parse(JSON.stringify(CATALOG.presets.find(p => p.id === activePresetId)));
          renderPresetNav();
          renderUnitForm();
        };
      });
    }

    function renderUnitForm() {
      const form = document.getElementById('unitForm');
      if (!draft) {
        form.innerHTML = '<div class="empty" style="grid-column:1/-1">Select a unit or create a new one.</div>';
        return;
      }
      const typeOpts = CATALOG.baseUnitTypes.map(t =>
        `<option value="${t.id}" ${Number(draft.baseUnitType)===t.id?'selected':''}>${t.group} — ${t.label}</option>`
      ).join('');
      const bankOpts = CATALOG.audioBanks.map(b =>
        `<option value="${b}" ${draft.audioBank===b?'selected':''}>${b}</option>`
      ).join('');
      const selectedType = CATALOG.baseUnitTypes.find(t => t.id === Number(draft.baseUnitType));
      const statsHtml = CATALOG.statFields.map(f => `
        <div class="field">
          <label>${f.label}</label>
          <input type="number" data-stat="${f.key}" value="${draft.stats?.[f.key] ?? CATALOG.defaultStats[f.key]}"/>
        </div>`).join('');
      const bank = DATA.units.find(u => u.name === draft.audioBank);
      const audioPreview = (bank?.samples || []).slice(0, 8).map(s => {
        const custom = s.custom1?.url || s.custom2?.url || '';
        const customDur = s.custom1?.durFmt || s.custom2?.durFmt || '—';
        return `<article class="sample" style="grid-template-columns:140px 1fr 1fr;margin-bottom:6px">
          <div class="sample-name">${s.file}</div>
          <button type="button" class="play" onclick="previewPlay(this, '${s.vanilla}')">▶ Original ${s.vanillaDurFmt||''}</button>
          <button type="button" class="play${!custom?' missing':''}" ${custom?`onclick="previewPlay(this, '${custom}')"`:'disabled'}>▶ Custom ${customDur}</button>
        </article>`;
      }).join('') || '<div class="empty">No samples in this audio bank — pick another or fill Audio tab</div>';

      form.innerHTML = `
        <div class="field"><label>Display name</label>
          <input id="fName" value="${(draft.displayName||'').replace(/"/g,'&quot;')}"/></div>
        <div class="field"><label>Preset id</label>
          <input id="fId" value="${(draft.id||'').replace(/"/g,'&quot;')}" placeholder="auto from name"/></div>
        <div class="field wide"><label>Base unit type (spells / behavior)</label>
          <select id="fType">${typeOpts}</select></div>
        <div class="spell-hint">${selectedType ? ('Spells: ' + selectedType.spells) : ''}</div>
        <div class="field wide"><label>Audio bank (same units as Audio tab)</label>
          <select id="fBank">${bankOpts}</select></div>
        <h3>Stats (UDTA on maps using this preset)</h3>
        ${statsHtml}
        <div class="field wide"><label>Notes</label>
          <textarea id="fNotes">${draft.notes||''}</textarea></div>
        <div class="form-actions">
          <button type="button" class="btn primary" id="saveUnit">Save for map editor</button>
          <button type="button" class="btn danger" id="deleteUnit" ${draft.id?'':'disabled'}>Delete</button>
        </div>
        <div class="audio-mini">
          <h3>Audio preview — edit WAVs in the Audio tab</h3>
          ${audioPreview}
        </div>`;

      document.getElementById('subtitle').textContent =
        `Custom units · ${CATALOG.presets.length} presets · War2 Content Studio → Custom`;

      const syncDraft = () => {
        draft.displayName = document.getElementById('fName').value;
        draft.id = document.getElementById('fId').value;
        draft.baseUnitType = Number(document.getElementById('fType').value);
        draft.audioBank = document.getElementById('fBank').value;
        draft.notes = document.getElementById('fNotes').value;
        draft.stats = draft.stats || {};
        form.querySelectorAll('[data-stat]').forEach(inp => { draft.stats[inp.dataset.stat] = Number(inp.value); });
      };
      document.getElementById('fType').onchange = () => { syncDraft(); renderUnitForm(); };
      document.getElementById('fBank').onchange = () => { syncDraft(); renderUnitForm(); };
      document.getElementById('saveUnit').onclick = async () => {
        syncDraft();
        setStatus('Saving preset…');
        try {
          const res = await fetch('/api/presets', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(draft) });
          const data = await res.json();
          if (!res.ok) throw new Error(data.error || 'Save failed');
          CATALOG.presets = data.presets;
          activePresetId = data.saved.id;
          draft = JSON.parse(JSON.stringify(data.saved));
          renderPresetNav();
          renderUnitForm();
          setStatus('Saved — available in War2 Content Studio under Custom', true);
        } catch (err) { setStatus(err.message, false); }
      };
      document.getElementById('deleteUnit').onclick = async () => {
        if (!draft.id) return;
        if (!confirm('Delete preset ' + draft.displayName + '?')) return;
        const res = await fetch('/api/presets/' + encodeURIComponent(draft.id), { method: 'DELETE' });
        const data = await res.json();
        CATALOG.presets = data.presets;
        activePresetId = CATALOG.presets[0]?.id || '';
        draft = activePresetId ? JSON.parse(JSON.stringify(CATALOG.presets[0])) : null;
        renderPresetNav();
        renderUnitForm();
        setStatus('Deleted', true);
      };
    }

    document.getElementById('newUnit').onclick = () => {
      draft = blankDraft();
      activePresetId = '';
      switchTab('units');
      renderPresetNav();
      renderUnitForm();
    };
    document.getElementById('openPresets').onclick = () => fetch('/api/open-presets-folder', { method: 'POST' });


    renderNav();
    renderLongPanel();
    renderSamples();
  </script>
</body>
</html>"""


def load_config() -> dict:
    if CONFIG_PATH.is_file():
        try:
            return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            pass
    return {}


def save_config(data: dict) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(json.dumps(data, indent=2), encoding="utf-8")


def split_sample_id(sample_id: str) -> tuple[str, str]:
    parts = sample_id.split("/", 1)
    if len(parts) != 2 or not parts[0] or not parts[1]:
        raise ValueError("Invalid sample id")
    return parts[0], parts[1]


def parse_slot(raw: str | int | None) -> int:
    try:
        slot = int(raw or 1)
    except (TypeError, ValueError):
        slot = 1
    return 1 if slot != 2 else 2


def replacer_filename(wav_name: str, slot: int) -> str:
    if slot == 1:
        return wav_name
    stem = Path(wav_name).stem
    suffix = Path(wav_name).suffix
    return f"{stem}__2{suffix}"


def replacer_path(sample_id: str, slot: int = 1) -> Path:
    unit, name = split_sample_id(sample_id)
    return REPLACER_DIR / unit / replacer_filename(name, slot)


def replacer_url(sample_id: str, slot: int) -> str:
    unit, name = split_sample_id(sample_id)
    rel = f"{unit}/{replacer_filename(name, slot)}"
    path = replacer_path(sample_id, slot)
    version = int(path.stat().st_mtime) if path.is_file() else 0
    return f"/audio/replacer/{rel}?v={version}"


def resolve_ffmpeg() -> str:
    found = shutil.which("ffmpeg")
    if found:
        return found
    try:
        import imageio_ffmpeg  # type: ignore[import-untyped]

        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError as exc:
        raise RuntimeError(
            "MP3 conversion requires ffmpeg. Install ffmpeg or run: pip install imageio-ffmpeg"
        ) from exc


def convert_audio_to_game_wav(source: Path, dest: Path) -> None:
    ffmpeg = resolve_ffmpeg()
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(source),
        "-ac",
        str(GAME_WAV_CHANNELS),
        "-ar",
        str(GAME_WAV_RATE),
        "-sample_fmt",
        "s16",
        str(dest),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)  # noqa: S603
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "Conversion failed").strip()
        raise RuntimeError(detail[-500:])
    if not dest.is_file():
        raise RuntimeError("Conversion produced no output file")


def save_replacer_upload(upload_name: str, payload: bytes, dest: Path) -> None:
    ext = Path(upload_name or "").suffix.lower()
    if ext not in ALLOWED_UPLOAD_EXTENSIONS:
        raise ValueError("WAV or MP3 files only")

    dest.parent.mkdir(parents=True, exist_ok=True)

    if ext == ".wav":
        dest.write_bytes(payload)
        return

    with tempfile.TemporaryDirectory() as tmp:
        mp3_path = Path(tmp) / "upload.mp3"
        mp3_path.write_bytes(payload)
        convert_audio_to_game_wav(mp3_path, dest)


def load_selection_entries() -> dict[str, dict]:
    if not SELECTIONS_PATH.is_file():
        return {}
    try:
        raw = json.loads(SELECTIONS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}

    store: dict[str, dict] = {}
    entries = raw.get("samples", raw) if isinstance(raw, dict) else {}
    if not isinstance(entries, dict):
        return store

    for sample_id, entry in entries.items():
        if sample_id == "version":
            continue
        choice = ""
        playback_speed = None
        playback_volume = None
        trim_start = None
        trim_end = None
        title = ""
        unit = ""
        if isinstance(entry, str):
            choice = entry.strip()
        elif isinstance(entry, dict):
            choice = str(entry.get("choice", "")).strip()
            title = str(entry.get("title") or "")
            unit = str(entry.get("unit") or "")
            if entry.get("playbackSpeed") is not None:
                playback_speed = clamp_speed(float(entry["playbackSpeed"]))
            if entry.get("playbackVolume") is not None:
                playback_volume = clamp_volume(float(entry["playbackVolume"]))
            if entry.get("trimStart") is not None:
                trim_start = clamp_trim(float(entry["trimStart"]))
            if entry.get("trimEnd") is not None:
                trim_end = clamp_trim(float(entry["trimEnd"]))
        if choice not in VALID_CUSTOM_SLOTS:
            continue
        detail = {"choice": choice, "title": title, "unit": unit}
        if playback_speed is not None:
            detail["playbackSpeed"] = playback_speed
        if playback_volume is not None:
            detail["playbackVolume"] = playback_volume
        if trim_start is not None:
            detail["trimStart"] = trim_start
        if trim_end is not None:
            detail["trimEnd"] = trim_end
        store[sample_id] = detail
    return store


def load_selections() -> dict[str, str]:
    return {sample_id: entry["choice"] for sample_id, entry in load_selection_entries().items()}


def selection_playback_speed(sample_id: str, choice: str) -> float:
    speeds = load_playback_speeds()
    entry = speeds.get(sample_id, {})
    return clamp_speed(float(entry.get(choice, 1.0)))


def get_selection_detail(sample_id: str) -> dict | None:
    entry = load_selection_entries().get(sample_id)
    if not entry:
        return None
    choice = entry["choice"]
    if "playbackSpeed" in entry:
        speed = clamp_speed(float(entry["playbackSpeed"]))
    else:
        speed = selection_playback_speed(sample_id, choice)
    if "playbackVolume" in entry:
        volume = clamp_volume(float(entry["playbackVolume"]))
    else:
        volume = selection_playback_volume(sample_id, choice)
    trim = selection_playback_trim(sample_id, choice)
    if "trimStart" in entry:
        trim_start = clamp_trim(float(entry["trimStart"]))
    else:
        trim_start = trim["start"]
    if "trimEnd" in entry:
        trim_end = clamp_trim(float(entry["trimEnd"]))
    else:
        trim_end = trim["end"]
    return {
        "choice": choice,
        "playbackSpeed": speed,
        "playbackVolume": volume,
        "trimStart": trim_start,
        "trimEnd": trim_end,
        "title": entry.get("title", ""),
        "unit": entry.get("unit", ""),
    }


def selection_source(sample_id: str, choice: str) -> dict:
    if choice == "original":
        path = VANILLA_DIR / sample_id
        return {"type": "vanilla", "slot": None, "path": path}
    if choice == "custom1":
        return {"type": "replacer", "slot": 1, "path": replacer_path(sample_id, 1)}
    if choice == "custom2":
        return {"type": "replacer", "slot": 2, "path": replacer_path(sample_id, 2)}
    raise ValueError("Invalid choice")


def app_relative(path: Path) -> str:
    try:
        return path.relative_to(APP_DIR).as_posix()
    except ValueError:
        return str(path)


def write_mod_export() -> None:
    export_samples: dict[str, dict] = {}
    for sample_id, entry in sorted(load_selection_entries().items()):
        choice = entry["choice"]
        try:
            parsed_unit, parsed_title = split_sample_id(sample_id)
        except ValueError:
            parsed_unit, parsed_title = "", sample_id
        unit = entry.get("unit") or parsed_unit
        filename = entry.get("title") or parsed_title
        speed = entry.get("playbackSpeed")
        if speed is None:
            speed = selection_playback_speed(sample_id, choice)
        else:
            speed = clamp_speed(float(speed))
        volume = entry.get("playbackVolume")
        if volume is None:
            volume = selection_playback_volume(sample_id, choice)
        else:
            volume = clamp_volume(float(volume))
        trim = selection_playback_trim(sample_id, choice)
        trim_start = entry.get("trimStart")
        if trim_start is None:
            trim_start = trim["start"]
        else:
            trim_start = clamp_trim(float(trim_start))
        trim_end = entry.get("trimEnd")
        if trim_end is None:
            trim_end = trim["end"]
        else:
            trim_end = clamp_trim(float(trim_end))
        try:
            source = selection_source(sample_id, choice)
        except ValueError:
            continue
        if not source["path"].is_file():
            continue
        export_samples[sample_id] = {
            "unit": unit,
            "file": filename,
            "choice": choice,
            "playbackSpeed": speed,
            "playbackVolume": volume,
            "trimStart": trim_start,
            "trimEnd": trim_end,
            "sourceType": source["type"],
            "sourceSlot": source["slot"],
            "sourceRelative": app_relative(source["path"]),
            "sourceAbsolute": str(source["path"].resolve()),
            "gameRelativePath": f"x86/Data/Gamesfx/{sample_id}",
        }
    payload = {
        "version": 1,
        "tool": APP_NAME,
        "updatedAt": datetime.now(timezone.utc).isoformat(),
        "appDir": str(APP_DIR.resolve()),
        "selectionCount": len(export_samples),
        "samples": export_samples,
    }
    MOD_EXPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    MOD_EXPORT_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def _write_selection_store(store_entries: dict[str, dict]) -> None:
    SELECTIONS_PATH.parent.mkdir(parents=True, exist_ok=True)
    SELECTIONS_PATH.write_text(
        json.dumps({"version": 1, "samples": dict(sorted(store_entries.items()))}, indent=2),
        encoding="utf-8",
    )
    write_mod_export()


def sync_selection_playback_speed(sample_id: str, kind: str, rate: float) -> None:
    if kind not in VALID_CUSTOM_SLOTS:
        return
    if not SELECTIONS_PATH.is_file():
        return
    try:
        raw = json.loads(SELECTIONS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    if not isinstance(raw, dict) or not isinstance(raw.get("samples"), dict):
        return
    entry = raw["samples"].get(sample_id)
    if not isinstance(entry, dict) or str(entry.get("choice", "")).strip() != kind:
        return
    entry["playbackSpeed"] = clamp_speed(rate)
    _write_selection_store(raw["samples"])


def sync_selection_playback_volume(sample_id: str, kind: str, level: float) -> None:
    if kind not in VALID_CUSTOM_SLOTS:
        return
    if not SELECTIONS_PATH.is_file():
        return
    try:
        raw = json.loads(SELECTIONS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    if not isinstance(raw, dict) or not isinstance(raw.get("samples"), dict):
        return
    entry = raw["samples"].get(sample_id)
    if not isinstance(entry, dict) or str(entry.get("choice", "")).strip() != kind:
        return
    entry["playbackVolume"] = clamp_volume(level)
    _write_selection_store(raw["samples"])


def sync_selection_playback_trim(sample_id: str, kind: str, trim_start: float, trim_end: float) -> None:
    if kind not in VALID_CUSTOM_SLOTS:
        return
    if not SELECTIONS_PATH.is_file():
        return
    try:
        raw = json.loads(SELECTIONS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    if not isinstance(raw, dict) or not isinstance(raw.get("samples"), dict):
        return
    entry = raw["samples"].get(sample_id)
    if not isinstance(entry, dict) or str(entry.get("choice", "")).strip() != kind:
        return
    entry["trimStart"] = clamp_trim(trim_start)
    entry["trimEnd"] = clamp_trim(trim_end)
    _write_selection_store(raw["samples"])


def set_sample_selection(
    sample_id: str,
    choice: str,
    *,
    title: str | None = None,
    unit: str | None = None,
    playback_speed: float | None = None,
    playback_volume: float | None = None,
    trim_start: float | None = None,
    trim_end: float | None = None,
) -> tuple[str, float, float, float, float]:
    if choice not in VALID_CUSTOM_SLOTS:
        raise ValueError("Invalid choice")
    if choice == "custom1" and not replacer_path(sample_id, 1).is_file():
        raise ValueError("Selected slot has no custom WAV")
    if choice == "custom2" and not replacer_path(sample_id, 2).is_file():
        raise ValueError("Selected slot has no custom WAV")
    if choice == "original" and not (VANILLA_DIR / sample_id).is_file():
        raise ValueError("Original sample not found")
    parsed_unit, parsed_title = split_sample_id(sample_id)
    speed = (
        clamp_speed(playback_speed)
        if playback_speed is not None
        else selection_playback_speed(sample_id, choice)
    )
    volume = (
        clamp_volume(playback_volume)
        if playback_volume is not None
        else selection_playback_volume(sample_id, choice)
    )
    trim = selection_playback_trim(sample_id, choice)
    resolved_trim_start = (
        clamp_trim(trim_start) if trim_start is not None else trim["start"]
    )
    resolved_trim_end = clamp_trim(trim_end) if trim_end is not None else trim["end"]
    store_entries: dict[str, dict] = {}
    if SELECTIONS_PATH.is_file():
        try:
            raw = json.loads(SELECTIONS_PATH.read_text(encoding="utf-8"))
            if isinstance(raw, dict) and isinstance(raw.get("samples"), dict):
                store_entries = raw["samples"]
        except json.JSONDecodeError:
            pass
    store_entries[sample_id] = {
        "title": title or parsed_title,
        "unit": unit or parsed_unit,
        "choice": choice,
        "playbackSpeed": speed,
        "playbackVolume": volume,
        "trimStart": resolved_trim_start,
        "trimEnd": resolved_trim_end,
    }
    _write_selection_store(store_entries)
    return choice, speed, volume, resolved_trim_start, resolved_trim_end


def clear_sample_selection(sample_id: str) -> None:
    if not SELECTIONS_PATH.is_file():
        write_mod_export()
        return
    try:
        raw = json.loads(SELECTIONS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    if not isinstance(raw, dict) or not isinstance(raw.get("samples"), dict):
        return
    if sample_id not in raw["samples"]:
        return
    del raw["samples"][sample_id]
    _write_selection_store(raw["samples"])


def clamp_speed(rate: float) -> float:
    return max(MIN_PLAYBACK_SPEED, min(MAX_PLAYBACK_SPEED, round(float(rate), 2)))


def clamp_volume(level: float) -> float:
    return max(MIN_PLAYBACK_VOLUME, min(MAX_PLAYBACK_VOLUME, round(float(level), 2)))


def clamp_trim(value: float) -> float:
    return max(0.0, round(float(value), 2))


def _default_trim_edge() -> dict[str, float]:
    return {"start": 0.0, "end": 0.0}


def _default_trim_block() -> dict[str, dict[str, float]]:
    return {
        "original": _default_trim_edge(),
        "custom1": _default_trim_edge(),
        "custom2": _default_trim_edge(),
    }


def _normalize_trim_edge(raw: object) -> dict[str, float]:
    if isinstance(raw, dict):
        return {
            "start": clamp_trim(float(raw.get("start", 0.0))),
            "end": clamp_trim(float(raw.get("end", 0.0))),
        }
    return _default_trim_edge()


def _normalize_trim_block(entry: dict) -> dict[str, dict[str, float]]:
    trim_raw = entry.get("trim")
    if isinstance(trim_raw, dict):
        return {
            "original": _normalize_trim_edge(trim_raw.get("original")),
            "custom1": _normalize_trim_edge(trim_raw.get("custom1")),
            "custom2": _normalize_trim_edge(trim_raw.get("custom2")),
        }
    return _default_trim_block()


def _default_volume_block() -> dict[str, float]:
    return {"original": 1.0, "custom1": 1.0, "custom2": 1.0}


def _normalize_volume_block(entry: dict) -> dict[str, float]:
    vol_raw = entry.get("volume")
    if isinstance(vol_raw, dict):
        return {
            "original": clamp_volume(float(vol_raw.get("original", 1.0))),
            "custom1": clamp_volume(float(vol_raw.get("custom1", 1.0))),
            "custom2": clamp_volume(float(vol_raw.get("custom2", 1.0))),
        }
    return _default_volume_block()


def _read_track_entries() -> dict[str, dict]:
    if not SPEEDS_PATH.is_file():
        return {}
    try:
        raw = json.loads(SPEEDS_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}
    entries = raw.get("samples", raw) if isinstance(raw, dict) else {}
    if not isinstance(entries, dict):
        return {}
    store: dict[str, dict] = {}
    for sample_id, entry in entries.items():
        if sample_id == "version" or not isinstance(entry, dict):
            continue
        store[sample_id] = dict(entry)
    return store


def _write_track_entries(entries: dict[str, dict]) -> None:
    normalized: dict[str, dict] = {}
    for sample_id, entry in sorted(entries.items()):
        normalized[sample_id] = {
            "title": str(entry.get("title") or ""),
            "unit": str(entry.get("unit") or ""),
            "original": clamp_speed(float(entry.get("original", 1.0))),
            "custom1": clamp_speed(float(entry.get("custom1", 1.0))),
            "custom2": clamp_speed(float(entry.get("custom2", 1.0))),
            "volume": _normalize_volume_block(entry),
            "trim": _normalize_trim_block(entry),
        }
    SPEEDS_PATH.parent.mkdir(parents=True, exist_ok=True)
    SPEEDS_PATH.write_text(
        json.dumps({"version": 1, "samples": normalized}, indent=2),
        encoding="utf-8",
    )


def load_playback_speeds() -> dict[str, dict]:
    store: dict[str, dict] = {}
    for sample_id, entry in _read_track_entries().items():
        try:
            unit, title = split_sample_id(sample_id)
        except ValueError:
            unit = str(entry.get("unit") or "")
            title = str(entry.get("title") or "")
        store[sample_id] = {
            "original": clamp_speed(float(entry.get("original", entry.get("row", 1.0)))),
            "custom1": clamp_speed(float(entry.get("custom1", entry.get("custom", 1.0)))),
            "custom2": clamp_speed(float(entry.get("custom2", 1.0))),
            "volume": _normalize_volume_block(entry),
            "trim": _normalize_trim_block(entry),
            "unit": unit or str(entry.get("unit") or ""),
            "title": title or str(entry.get("title") or ""),
        }
    return store


def selection_playback_trim(sample_id: str, choice: str) -> dict[str, float]:
    settings = load_playback_speeds()
    entry = settings.get(sample_id, {})
    trim = entry.get("trim", _default_trim_block())
    edge = trim.get(choice, _default_trim_edge()) if isinstance(trim, dict) else _default_trim_edge()
    if isinstance(edge, dict):
        return {
            "start": clamp_trim(float(edge.get("start", 0.0))),
            "end": clamp_trim(float(edge.get("end", 0.0))),
        }
    return _default_trim_edge()


def selection_playback_volume(sample_id: str, choice: str) -> float:
    settings = load_playback_speeds()
    entry = settings.get(sample_id, {})
    volume = entry.get("volume", _default_volume_block())
    return clamp_volume(float(volume.get(choice, 1.0)))


def set_playback_speed(
    sample_id: str,
    rate: float,
    *,
    kind: str = "original",
    title: str | None = None,
    unit: str | None = None,
) -> float:
    speed_kind = kind if kind in {"original", "custom1", "custom2"} else "original"
    clamped = clamp_speed(rate)
    parsed_unit, parsed_title = split_sample_id(sample_id)
    entries = _read_track_entries()
    prev = entries.get(sample_id, {})
    entries[sample_id] = {
        "title": title or prev.get("title", parsed_title),
        "unit": unit or prev.get("unit", parsed_unit),
        "original": clamped if speed_kind == "original" else float(prev.get("original", 1.0)),
        "custom1": clamped if speed_kind == "custom1" else float(prev.get("custom1", 1.0)),
        "custom2": clamped if speed_kind == "custom2" else float(prev.get("custom2", 1.0)),
        "volume": _normalize_volume_block(prev),
        "trim": _normalize_trim_block(prev),
    }
    _write_track_entries(entries)
    sync_selection_playback_speed(sample_id, speed_kind, clamped)
    return clamped


def set_playback_volume(
    sample_id: str,
    level: float,
    *,
    kind: str = "original",
    title: str | None = None,
    unit: str | None = None,
) -> float:
    volume_kind = kind if kind in {"original", "custom1", "custom2"} else "original"
    clamped = clamp_volume(level)
    parsed_unit, parsed_title = split_sample_id(sample_id)
    entries = _read_track_entries()
    prev = entries.get(sample_id, {})
    volume = _normalize_volume_block(prev)
    volume[volume_kind] = clamped
    entries[sample_id] = {
        "title": title or prev.get("title", parsed_title),
        "unit": unit or prev.get("unit", parsed_unit),
        "original": float(prev.get("original", 1.0)),
        "custom1": float(prev.get("custom1", 1.0)),
        "custom2": float(prev.get("custom2", 1.0)),
        "volume": volume,
        "trim": _normalize_trim_block(prev),
    }
    _write_track_entries(entries)
    sync_selection_playback_volume(sample_id, volume_kind, clamped)
    return clamped


def set_playback_trim(
    sample_id: str,
    trim_start: float,
    trim_end: float,
    *,
    kind: str = "original",
    title: str | None = None,
    unit: str | None = None,
) -> dict[str, float]:
    trim_kind = kind if kind in {"original", "custom1", "custom2"} else "original"
    start = clamp_trim(trim_start)
    end = clamp_trim(trim_end)
    parsed_unit, parsed_title = split_sample_id(sample_id)
    entries = _read_track_entries()
    prev = entries.get(sample_id, {})
    trim = _normalize_trim_block(prev)
    trim[trim_kind] = {"start": start, "end": end}
    entries[sample_id] = {
        "title": title or prev.get("title", parsed_title),
        "unit": unit or prev.get("unit", parsed_unit),
        "original": float(prev.get("original", 1.0)),
        "custom1": float(prev.get("custom1", 1.0)),
        "custom2": float(prev.get("custom2", 1.0)),
        "volume": _normalize_volume_block(prev),
        "trim": trim,
    }
    _write_track_entries(entries)
    sync_selection_playback_trim(sample_id, trim_kind, start, end)
    return {"start": start, "end": end}


def detect_gamesfx() -> Path | None:
    config = load_config()
    saved = config.get("gamesfxPath")
    if saved and Path(saved).is_dir():
        return Path(saved)
    for candidate in DEFAULT_GAMESFX_CANDIDATES:
        if candidate.is_dir():
            return candidate
    return None


def validate_gamesfx(path: Path) -> None:
    if not path.is_dir():
        raise ValueError(f"Folder not found: {path}")
    wav_count = sum(
        1
        for sub in path.iterdir()
        if sub.is_dir() and sub.name not in SKIP_DIRS
        for _ in sub.glob("*.wav")
    )
    if wav_count == 0:
        raise ValueError("No unit voice WAV files found (expected subfolders like Human/, Knight/, …)")


def copy_vanilla_tree(gamesfx: Path, dest: Path) -> int:
    count = 0
    dest.mkdir(parents=True, exist_ok=True)
    for sub in sorted(gamesfx.iterdir()):
        if not sub.is_dir() or sub.name in SKIP_DIRS:
            continue
        out_dir = dest / sub.name
        out_dir.mkdir(parents=True, exist_ok=True)
        for wav in sorted(sub.glob("*.wav")):
            shutil.copy2(wav, out_dir / wav.name)
            count += 1
    return count


def sync_audio(gamesfx_path: Path) -> int:
    validate_gamesfx(gamesfx_path)
    count = copy_vanilla_tree(gamesfx_path, VANILLA_DIR)
    config = load_config()
    config["gamesfxPath"] = str(gamesfx_path)
    config["sampleCount"] = count
    save_config(config)
    ensure_unit_long_samples()
    return count


def wav_duration_seconds(path: Path) -> float | None:
    if not path.is_file():
        return None
    try:
        with wave.open(str(path), "rb") as w:
            rate = w.getframerate()
            if rate <= 0:
                return None
            return w.getnframes() / float(rate)
    except wave.Error:
        return None


def fmt_duration(seconds: float | None) -> str:
    if seconds is None:
        return "—"
    if seconds < 60:
        return f"{seconds:.2f}s"
    minutes = int(seconds // 60)
    return f"{minutes}:{seconds % 60:05.2f}"


def fmt_file_size(num_bytes: int) -> str:
    if num_bytes < 1024:
        return f"{num_bytes} B"
    if num_bytes < 1024 * 1024:
        return f"{num_bytes / 1024:.1f} KB"
    return f"{num_bytes / (1024 * 1024):.2f} MB"


def wav_specs(path: Path) -> dict | None:
    if not path.is_file():
        return None
    try:
        with wave.open(str(path), "rb") as w:
            channels = w.getnchannels()
            rate = w.getframerate()
            sample_width = w.getsampwidth()
            frames = w.getnframes()
        duration = frames / float(rate) if rate > 0 else 0.0
        size_bytes = path.stat().st_size
        bits = sample_width * 8
        game_ready = channels == GAME_WAV_CHANNELS and rate == GAME_WAV_RATE and sample_width == 2
        return {
            "format": "PCM WAV",
            "channels": channels,
            "channelsLabel": "Mono" if channels == 1 else f"{channels} channels",
            "rate": rate,
            "rateLabel": f"{rate:,} Hz",
            "bits": bits,
            "bitsLabel": f"{bits}-bit",
            "durationFmt": fmt_duration(duration),
            "sizeLabel": fmt_file_size(size_bytes),
            "gameReady": game_ready,
            "gameReadyLabel": "Yes" if game_ready else "No (mono 22050 16-bit)",
        }
    except (OSError, wave.Error):
        return None


def replacer_slot_payload(sample_id: str, slot: int, path: Path) -> dict:
    duration = wav_duration_seconds(path)
    return {
        "url": replacer_url(sample_id, slot),
        "dur": duration,
        "durFmt": fmt_duration(duration),
        "specs": wav_specs(path),
    }


def _read_wav_frames(path: Path) -> tuple[tuple[int, int, int], bytes]:
    with wave.open(str(path), "rb") as w:
        params = w.getparams()
        frames = w.readframes(w.getnframes())
    return (params.nchannels, params.sampwidth, params.framerate), frames


def long_sample_filename(unit: str) -> str:
    return f"{unit}_long.wav"


def iter_vanilla_units() -> list[str]:
    if not VANILLA_DIR.is_dir():
        return []
    return sorted(
        sub.name
        for sub in VANILLA_DIR.iterdir()
        if sub.is_dir() and sub.name not in SKIP_DIRS and any(sub.glob("*.wav"))
    )


def _ffmpeg_filter_wav(source: Path, dest: Path, af_filter: str) -> None:
    ffmpeg = resolve_ffmpeg()
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(source),
        "-af",
        af_filter,
        "-ac",
        str(GAME_WAV_CHANNELS),
        "-ar",
        str(GAME_WAV_RATE),
        "-sample_fmt",
        "s16",
        str(dest),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)  # noqa: S603
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "ffmpeg failed").strip()
        raise RuntimeError(detail)


def build_unit_long_sample(unit: str, filename: str, *, min_seconds: float = MIN_LONG_SAMPLE_SECONDS) -> Path:
    source_dir = VANILLA_DIR / unit
    if not source_dir.is_dir():
        raise ValueError(f"No vanilla samples for unit: {unit}")
    exclude = LONG_SAMPLE_EXCLUDE.get(unit, frozenset())
    sources = sorted(src for src in source_dir.glob("*.wav") if src.name not in exclude)
    if not sources:
        raise ValueError(f"No WAV files for unit: {unit}")

    dest = DEMOS_DIR / filename
    DEMOS_DIR.mkdir(parents=True, exist_ok=True)

    af_filter = LONG_SAMPLE_AF_FILTER.get(unit)
    ref_params: tuple[int, int, int] | None = None
    one_pass: list[bytes] = []

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for src in sources:
            read_path = src
            if af_filter:
                filtered = tmp_dir / src.name
                _ffmpeg_filter_wav(src, filtered, af_filter)
                read_path = filtered
            params, frames = _read_wav_frames(read_path)
            if ref_params is None:
                ref_params = params
            elif params != ref_params:
                raise ValueError(f"Incompatible WAV format in {src.name}")
            one_pass.append(frames)

    assert ref_params is not None
    nchannels, sampwidth, framerate = ref_params

    pass_bytes = b"".join(one_pass)
    bytes_per_second = nchannels * sampwidth * framerate

    output = pass_bytes
    while len(output) / bytes_per_second < min_seconds:
        output += pass_bytes

    with wave.open(str(dest), "wb") as out:
        out.setnchannels(nchannels)
        out.setsampwidth(sampwidth)
        out.setframerate(framerate)
        out.writeframes(output)
    return dest


def ensure_unit_long_samples() -> None:
    for unit in iter_vanilla_units():
        try:
            build_unit_long_sample(unit, long_sample_filename(unit))
        except (ValueError, OSError) as exc:
            print(f"Warning: could not build long sample for {unit}: {exc}")


def long_sample_info(unit: str) -> dict | None:
    filename = long_sample_filename(unit)
    path = DEMOS_DIR / filename
    if not path.is_file():
        return None
    duration = wav_duration_seconds(path)
    return {
        "url": f"/audio/demos/{filename}",
        "filename": filename,
        "path": str(path),
        "dur": duration,
        "durFmt": fmt_duration(duration),
        "specs": wav_specs(path),
    }


def collect_samples() -> list[dict]:
    samples: list[dict] = []
    selection_entries = load_selection_entries()
    if not VANILLA_DIR.is_dir():
        return samples
    for sub in sorted(VANILLA_DIR.iterdir()):
        if not sub.is_dir() or sub.name in SKIP_DIRS:
            continue
        for wav in sorted(sub.glob("*.wav")):
            rel = f"{sub.name}/{wav.name}"
            vanilla_path = VANILLA_DIR / sub.name / wav.name
            path1 = replacer_path(rel, 1)
            path2 = replacer_path(rel, 2)
            has_custom1 = path1.is_file()
            has_custom2 = path2.is_file()
            vanilla_dur = wav_duration_seconds(vanilla_path)
            detail = selection_entries.get(rel)
            selected = ""
            selected_playback_speed = None
            selected_playback_volume = None
            selected_trim_start = None
            selected_trim_end = None
            if detail:
                selected = detail["choice"]
                if selected == "custom1" and not has_custom1:
                    selected = ""
                elif selected == "custom2" and not has_custom2:
                    selected = ""
                else:
                    selected_playback_speed = detail.get("playbackSpeed")
                    if selected_playback_speed is None:
                        selected_playback_speed = selection_playback_speed(rel, selected)
                    else:
                        selected_playback_speed = clamp_speed(float(selected_playback_speed))
                    selected_playback_volume = detail.get("playbackVolume")
                    if selected_playback_volume is None:
                        selected_playback_volume = selection_playback_volume(rel, selected)
                    else:
                        selected_playback_volume = clamp_volume(float(selected_playback_volume))
                    trim = selection_playback_trim(rel, selected)
                    selected_trim_start = detail.get("trimStart")
                    if selected_trim_start is None:
                        selected_trim_start = trim["start"]
                    else:
                        selected_trim_start = clamp_trim(float(selected_trim_start))
                    selected_trim_end = detail.get("trimEnd")
                    if selected_trim_end is None:
                        selected_trim_end = trim["end"]
                    else:
                        selected_trim_end = clamp_trim(float(selected_trim_end))
            custom1 = replacer_slot_payload(rel, 1, path1) if has_custom1 else None
            custom2 = replacer_slot_payload(rel, 2, path2) if has_custom2 else None
            samples.append(
                {
                    "id": rel,
                    "file": wav.name,
                    "unit": sub.name,
                    "vanilla": f"/audio/vanilla/{rel}",
                    "vanillaSpecs": wav_specs(vanilla_path),
                    "custom1": custom1,
                    "custom2": custom2,
                    "hasCustom1": has_custom1,
                    "hasCustom2": has_custom2,
                    "selectedCustom": selected,
                    "selectedPlaybackSpeed": selected_playback_speed,
                    "selectedPlaybackVolume": selected_playback_volume,
                    "selectedTrimStart": selected_trim_start,
                    "selectedTrimEnd": selected_trim_end,
                    "vanillaDur": vanilla_dur,
                    "vanillaDurFmt": fmt_duration(vanilla_dur),
                }
            )
    return samples


def build_payload(samples: list[dict]) -> dict:
    by_unit: dict[str, list[dict]] = defaultdict(list)
    for s in samples:
        by_unit[s["unit"]].append(s)
    units = []
    for name in sorted(by_unit):
        unit_samples = by_unit[name]
        custom_filled = sum(1 for s in unit_samples if s["hasCustom1"] or s["hasCustom2"])
        picked_count = sum(1 for s in unit_samples if s.get("selectedCustom"))
        units.append(
            {
                "name": name,
                "samples": unit_samples,
                "complete": custom_filled == len(unit_samples) and len(unit_samples) > 0,
                "customFilled": custom_filled,
                "pickedCount": picked_count,
                "longSample": long_sample_info(name),
            }
        )
    return {
        "total": len(samples),
        "customCount": sum(1 for s in samples if s["hasCustom1"] or s["hasCustom2"]),
        "playbackSpeeds": load_playback_speeds(),
        "units": units,
    }


def build_html(samples: list[dict]) -> str:
    payload = build_payload(samples)
    banks = [u["name"] for u in payload["units"]]
    catalog = unit_presets.catalog_payload(banks)
    return (
        APP_HTML.replace("__DATA_JSON__", json.dumps(payload)).replace(
            "__CATALOG_JSON__", json.dumps(catalog)
        )
    )


def open_app_window(url: str) -> None:
    app_url = url.rstrip("/")
    candidates = [
        ["msedge", f"--app={app_url}"],
        ["chrome", f"--app={app_url}"],
        [r"C:\Program Files\Microsoft\Edge\Application\msedge.exe", f"--app={app_url}"],
        [r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe", f"--app={app_url}"],
        [r"C:\Program Files\Google\Chrome\Application\chrome.exe", f"--app={app_url}"],
    ]
    for cmd in candidates:
        exe = cmd[0]
        if Path(exe).is_file() or shutil.which(exe):
            try:
                subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)  # noqa: S603
                return
            except OSError:
                continue
    webbrowser.open(url)


def read_json_body(handler: BaseHTTPRequestHandler) -> dict:
    length = int(handler.headers.get("Content-Length", 0))
    raw = handler.rfile.read(length) if length else b"{}"
    try:
        return json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError:
        return {}


def _parse_content_disposition(value: str) -> tuple[str | None, str | None]:
    name = None
    filename = None
    for part in value.split(";"):
        part = part.strip()
        if part.startswith("name="):
            name = part.split("=", 1)[1].strip().strip('"')
        elif part.startswith("filename="):
            filename = part.split("=", 1)[1].strip().strip('"')
    return name, filename


def parse_multipart_form(body: bytes, content_type: str) -> dict[str, str | tuple[str | None, bytes]]:
    match = re.search(r"boundary=([^;\s]+)", content_type)
    if not match:
        raise ValueError("Missing multipart boundary")
    boundary = match.group(1).strip('"')
    delimiter = b"--" + boundary.encode("ascii", errors="ignore")
    fields: dict[str, str | tuple[str | None, bytes]] = {}

    for section in body.split(delimiter):
        chunk = section.strip(b"\r\n")
        if not chunk or chunk == b"--":
            continue
        header_end = chunk.find(b"\r\n\r\n")
        if header_end < 0:
            continue
        headers = chunk[:header_end].decode("utf-8", errors="replace")
        data = chunk[header_end + 4 :]
        if data.endswith(b"\r\n"):
            data = data[:-2]

        field_name = None
        filename = None
        for line in headers.split("\r\n"):
            if line.lower().startswith("content-disposition:"):
                field_name, filename = _parse_content_disposition(line.split(":", 1)[1].strip())
                break
        if not field_name:
            continue
        if filename is not None:
            fields[field_name] = (filename, data)
        else:
            fields[field_name] = data.decode("utf-8", errors="replace")
    return fields


class Handler(BaseHTTPRequestHandler):
    def _json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _wav(self, path: Path) -> None:
        if not path.is_file():
            self.send_error(404)
            return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        clean = unquote(self.path.split("?", 1)[0])
        if clean in ("/setup", "/setup.html"):
            page = SETUP_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)
            return
        if clean == "/api/status":
            config = load_config()
            self._json(
                200,
                {
                    "ready": len(collect_samples()) > 0,
                    "sampleCount": len(collect_samples()),
                    "gamesfxPath": config.get("gamesfxPath", ""),
                },
            )
            return
        if clean == "/api/detect-gamesfx":
            found = detect_gamesfx()
            self._json(200, {"path": str(found) if found else ""})
            return
        if clean in ("/", "/index.html"):
            samples = collect_samples()
            if not samples:
                self.send_response(302)
                self.send_header("Location", "/setup")
                self.end_headers()
                return
            page = build_html(samples).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)
            return
        if clean.startswith("/audio/vanilla/"):
            self._wav(VANILLA_DIR / clean[len("/audio/vanilla/"):].replace("/", "\\"))
            return
        if clean.startswith("/audio/replacer/"):
            self._wav(REPLACER_DIR / clean[len("/audio/replacer/"):].replace("/", "\\"))
            return
        if clean.startswith("/audio/demos/"):
            self._wav(DEMOS_DIR / clean[len("/audio/demos/"):].replace("/", "\\"))
            return
        self.send_error(404)

    def do_POST(self) -> None:
        clean = unquote(self.path.split("?", 1)[0])
        if clean == "/api/sync":
            body = read_json_body(self)
            path_str = (body.get("gamesfxPath") or "").strip()
            if not path_str:
                self._json(400, {"error": "gamesfxPath required"})
                return
            try:
                count = sync_audio(Path(path_str))
                self._json(200, {"ok": True, "count": count})
            except (ValueError, OSError) as exc:
                self._json(400, {"error": str(exc)})
            return
        if clean == "/api/replacer":
            ctype = self.headers.get("Content-Type", "")
            if "multipart/form-data" not in ctype:
                self._json(400, {"error": "Bad form"})
                return
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b""
            try:
                form = parse_multipart_form(body, ctype)
            except ValueError as exc:
                self._json(400, {"error": str(exc)})
                return
            sample_id = str(form.get("sample_id", "")).strip()
            slot = parse_slot(str(form.get("slot", "1")))
            file_field = form.get("file")
            if not sample_id or "/" not in sample_id:
                self._json(400, {"error": "Invalid sample"})
                return
            if not isinstance(file_field, tuple):
                self._json(400, {"error": "No file uploaded"})
                return
            upload_name, payload = file_field
            if not payload:
                self._json(400, {"error": "No file uploaded"})
                return
            ext = Path((upload_name or "").lower()).suffix
            if ext not in ALLOWED_UPLOAD_EXTENSIONS:
                self._json(400, {"error": "WAV or MP3 files only"})
                return
            dest = replacer_path(sample_id, slot)
            try:
                save_replacer_upload(upload_name or "upload.wav", payload, dest)
            except (ValueError, RuntimeError, OSError) as exc:
                self._json(400, {"error": str(exc)})
                return
            slot_key = "custom1" if slot == 1 else "custom2"
            slot_payload = replacer_slot_payload(sample_id, slot, dest)
            self._json(
                200,
                {
                    "ok": True,
                    "sample_id": sample_id,
                    "slot": slot,
                    "slotKey": slot_key,
                    "url": slot_payload["url"],
                    "dur": slot_payload["dur"],
                    "durFmt": slot_payload["durFmt"],
                    "specs": slot_payload["specs"],
                    "path": str(dest),
                    "converted": ext == ".mp3",
                },
            )
            return
        if clean == "/api/selection":
            body = read_json_body(self)
            sample_id = (body.get("sample_id") or "").strip()
            choice = (body.get("choice") or "").strip()
            if not sample_id or "/" not in sample_id:
                self._json(400, {"error": "Invalid sample"})
                return
            if not choice:
                clear_sample_selection(sample_id)
                self._json(200, {"ok": True, "choice": ""})
                return
            try:
                playback_speed = body.get("playbackSpeed")
                playback_volume = body.get("playbackVolume")
                trim_start = body.get("trimStart")
                trim_end = body.get("trimEnd")
                parsed_speed = float(playback_speed) if playback_speed is not None else None
                parsed_volume = float(playback_volume) if playback_volume is not None else None
                parsed_trim_start = float(trim_start) if trim_start is not None else None
                parsed_trim_end = float(trim_end) if trim_end is not None else None
                resolved, speed, volume, t_start, t_end = set_sample_selection(
                    sample_id,
                    choice,
                    title=(body.get("title") or None),
                    unit=(body.get("unit") or None),
                    playback_speed=parsed_speed,
                    playback_volume=parsed_volume,
                    trim_start=parsed_trim_start,
                    trim_end=parsed_trim_end,
                )
                self._json(
                    200,
                    {
                        "ok": True,
                        "choice": resolved,
                        "playbackSpeed": speed,
                        "playbackVolume": volume,
                        "trimStart": t_start,
                        "trimEnd": t_end,
                    },
                )
            except ValueError as exc:
                self._json(400, {"error": str(exc)})
            return
        if clean == "/api/playback-speed":
            body = read_json_body(self)
            sample_id = (body.get("sample_id") or "").strip()
            if not sample_id or "/" not in sample_id:
                self._json(400, {"error": "Invalid sample"})
                return
            try:
                rate = set_playback_speed(
                    sample_id,
                    float(body.get("rate", 1.0)),
                    kind=str(body.get("kind") or "original"),
                    title=(body.get("title") or None),
                    unit=(body.get("unit") or None),
                )
                self._json(200, {"ok": True, "rate": rate})
            except (TypeError, ValueError) as exc:
                self._json(400, {"error": str(exc)})
            return
        if clean == "/api/playback-volume":
            body = read_json_body(self)
            sample_id = (body.get("sample_id") or "").strip()
            if not sample_id or "/" not in sample_id:
                self._json(400, {"error": "Invalid sample"})
                return
            try:
                level = set_playback_volume(
                    sample_id,
                    float(body.get("volume", 1.0)),
                    kind=str(body.get("kind") or "original"),
                    title=(body.get("title") or None),
                    unit=(body.get("unit") or None),
                )
                self._json(200, {"ok": True, "volume": level})
            except (TypeError, ValueError) as exc:
                self._json(400, {"error": str(exc)})
            return
        if clean == "/api/playback-trim":
            body = read_json_body(self)
            sample_id = (body.get("sample_id") or "").strip()
            if not sample_id or "/" not in sample_id:
                self._json(400, {"error": "Invalid sample"})
                return
            try:
                trim_raw = body.get("trim") or {}
                if not isinstance(trim_raw, dict):
                    raise ValueError("Invalid trim")
                trim = set_playback_trim(
                    sample_id,
                    float(trim_raw.get("start", 0.0)),
                    float(trim_raw.get("end", 0.0)),
                    kind=str(body.get("kind") or "original"),
                    title=(body.get("title") or None),
                    unit=(body.get("unit") or None),
                )
                self._json(200, {"ok": True, "trim": trim})
            except (TypeError, ValueError) as exc:
                self._json(400, {"error": str(exc)})
            return
        if clean == "/api/open-long-sample":
            body = read_json_body(self)
            unit = (body.get("unit") or "").strip()
            info = long_sample_info(unit)
            if not info:
                self._json(404, {"error": "Long sample not found"})
                return
            path = Path(info["path"])
            if not path.is_file():
                self._json(404, {"error": "Long sample file missing"})
                return
            subprocess.Popen(["explorer", "/select,", str(path)])  # noqa: S603,S607
            self._json(200, {"ok": True, "path": str(path)})
            return
        if clean == "/api/open-presets-folder":
            unit_presets.ensure_presets_dir()
            subprocess.Popen(["explorer", str(unit_presets.PRESETS_DIR)])  # noqa: S603,S607
            self._json(200, {"ok": True})
            return
        if clean == "/api/presets":
            body = read_json_body(self)
            try:
                saved = unit_presets.save_preset(body)
                self._json(200, {"ok": True, "saved": saved, "presets": unit_presets.load_presets()})
            except ValueError as exc:
                self._json(400, {"error": str(exc)})
            return
        self.send_error(404)

    def do_DELETE(self) -> None:
        clean = unquote(self.path.split("?", 1)[0])
        if clean.startswith("/api/replacer/"):
            sample_id = clean[len("/api/replacer/"):]
            query = self.path.split("?", 1)[1] if "?" in self.path else ""
            slot = 1
            for part in query.split("&"):
                if part.startswith("slot="):
                    slot = parse_slot(part.split("=", 1)[1])
            dest = replacer_path(sample_id, slot)
            if dest.is_file():
                dest.unlink()
            choice_key = "custom1" if slot == 1 else "custom2"
            if load_selections().get(sample_id) == choice_key:
                clear_sample_selection(sample_id)
            self._json(200, {"ok": True})
            return
        if clean.startswith("/api/presets/"):
            preset_id = clean[len("/api/presets/"):]
            unit_presets.delete_preset(preset_id)
            self._json(200, {"ok": True, "presets": unit_presets.load_presets()})
            return
        self.send_error(404)

    def log_message(self, format: str, *args) -> None:
        return


def free_stale_port() -> None:
    import subprocess as sp

    try:
        out = sp.check_output(["netstat", "-ano"], text=True, stderr=sp.DEVNULL)  # noqa: S603
        for line in out.splitlines():
            if f":{PORT}" in line and "LISTENING" in line:
                pid = line.split()[-1]
                if pid.isdigit():
                    sp.run(["taskkill", "/F", "/PID", pid], stdout=sp.DEVNULL, stderr=sp.DEVNULL)  # noqa: S603
    except Exception:
        pass


def main() -> None:
    REPLACER_DIR.mkdir(parents=True, exist_ok=True)
    unit_presets.ensure_presets_dir()
    VANILLA_DIR.mkdir(parents=True, exist_ok=True)
    DEMOS_DIR.mkdir(parents=True, exist_ok=True)
    ensure_unit_long_samples()
    write_mod_export()

    class ReuseServer(ThreadingHTTPServer):
        allow_reuse_address = True

    for attempt in range(2):
        try:
            server = ReuseServer(("127.0.0.1", PORT), Handler)
            break
        except OSError:
            if attempt == 0:
                free_stale_port()
            else:
                raise SystemExit(f"Port {PORT} is in use. Close other {APP_NAME} windows.")

    samples = collect_samples()
    url = f"http://127.0.0.1:{PORT}/"
    if samples:
        print(f"{APP_NAME}: {url}  ({len(samples)} samples)")
    else:
        print(f"{APP_NAME}: {url}  (setup required)")
    print("Close this window to stop.")
    open_app_window(url if samples else f"{url}setup")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopped.")


if __name__ == "__main__":
    main()
