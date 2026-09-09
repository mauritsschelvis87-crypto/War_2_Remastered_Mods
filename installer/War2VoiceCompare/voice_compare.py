"""War2 Voice Compare — audio banks + custom unit presets for Content Studio."""
from __future__ import annotations

import cgi
import json
import shutil
import subprocess
import wave
import webbrowser
from collections import defaultdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

import unit_presets

SKIP_DIRS = frozenset({"Misc", "Spells", "Bldg"})
APP_DIR = Path(__file__).resolve().parent
VANILLA_DIR = APP_DIR / "audio" / "vanilla"
ENHANCED_DIR = APP_DIR / "audio" / "enhanced"
REPLACER_DIR = APP_DIR / "replacer"
PORT = 8799

APP_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <meta name="color-scheme" content="dark"/>
  <title>War2 Content Lab</title>
  <style>
    html, body {
      margin: 0; height: 100%;
      background: #0c0e12 !important;
      color: #e6e9ef !important;
      font-family: "Segoe UI Variable", "Segoe UI", system-ui, sans-serif;
      overflow: hidden;
    }
    .app {
      display: grid;
      grid-template-rows: auto 1fr;
      height: 100vh;
      background: #0c0e12;
    }
    .topbar {
      display: flex; align-items: center; gap: 16px;
      padding: 14px 20px;
      background: linear-gradient(180deg, #151922 0%, #12151c 100%);
      border-bottom: 1px solid #2a3140;
    }
    .brand h1 { margin: 0; font-size: 1.05rem; font-weight: 650; letter-spacing: .01em; }
    .brand p { margin: 2px 0 0; font-size: 0.78rem; color: #8b93a7; }
    .tabs { display: flex; gap: 6px; margin-left: 12px; }
    .tab {
      padding: 8px 14px; border-radius: 8px; border: 1px solid transparent;
      background: transparent; color: #9aa3b5; cursor: pointer; font-size: 0.85rem;
    }
    .tab:hover { color: #e6e9ef; background: #1a2030; }
    .tab.active { color: #fff; background: #243048; border-color: #33415c; font-weight: 600; }
    .panel { display: none; min-height: 0; height: 100%; }
    .panel.active { display: grid; }
    .top-actions { margin-left: auto; display: flex; gap: 8px; align-items: center; }
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
    #search {
      width: 240px; padding: 8px 12px; border-radius: 8px;
      border: 1px solid #333b4d; background: #1a1f2a; color: #e6e9ef;
      font-size: 0.85rem;
    }
    .btn {
      padding: 8px 14px; border-radius: 8px; border: 1px solid #384055;
      background: #222836; color: #e6e9ef; cursor: pointer; font-size: 0.82rem;
    }
    .btn:hover { background: #2d3445; }
    .btn.ghost { background: transparent; }
    .main {
      display: grid; grid-template-columns: 200px 1fr;
      min-height: 0; background: #0c0e12;
    }
    .sidebar {
      background: #10141c; border-right: 1px solid #252b38;
      overflow-y: auto; padding: 10px 8px;
    }
    .sidebar h2 {
      font-size: 0.68rem; text-transform: uppercase; letter-spacing: .08em;
      color: #6d768a; margin: 8px 8px 10px; font-weight: 600;
    }
    .unit-btn {
      display: flex; justify-content: space-between; align-items: center;
      width: 100%; text-align: left; padding: 9px 12px; margin-bottom: 4px;
      border: none; border-radius: 8px; background: transparent;
      color: #c5cad6; cursor: pointer; font-size: 0.88rem;
    }
    .unit-btn:hover { background: #1a2030; color: #fff; }
    .unit-btn.active { background: #243048; color: #fff; font-weight: 600; }
    .unit-btn .count {
      font-size: 0.72rem; color: #7a8498; background: #1a2030;
      padding: 2px 7px; border-radius: 999px;
    }
    .unit-btn.active .count { background: #2f3d56; color: #b8c4dc; }
    .content {
      overflow-y: auto; padding: 16px 20px 24px;
      background: #0c0e12;
    }
    .col-head {
      display: grid; grid-template-columns: 180px 1fr 1fr 1fr;
      gap: 12px; padding: 0 14px 10px;
      font-size: 0.72rem; text-transform: uppercase; letter-spacing: .06em;
      color: #6d768a; position: sticky; top: 0; background: #0c0e12; z-index: 2;
      border-bottom: 1px solid #222833; margin-bottom: 8px;
    }
    .col-head span:nth-child(2) { color: #6b9bd1; }
    .col-head span:nth-child(3) { color: #7bc96f; }
    .col-head span:nth-child(4) { color: #e8a84a; }
    .sample {
      display: grid; grid-template-columns: 180px 1fr 1fr 1fr;
      gap: 12px; align-items: center;
      padding: 12px 14px; margin-bottom: 8px;
      background: #141820; border: 1px solid #232a38; border-radius: 12px;
    }
    .sample.custom { border-color: #4a3d28; background: #16140f; }
    .sample.hidden { display: none; }
    .sample-name { font-size: 0.9rem; font-weight: 600; word-break: break-word; }
    .sample-dur {
      display: block; margin-top: 4px; font-size: 0.74rem; font-weight: 500;
      color: #8b93a7; font-variant-numeric: tabular-nums;
    }
    .sample-dur .delta { color: #e8a84a; margin-left: 6px; }
    .dur {
      font-size: 0.74rem; color: #8b93a7; font-variant-numeric: tabular-nums;
      font-weight: 600; letter-spacing: .02em;
    }
    .dur.diff { color: #e8a84a; }
    .track {
      display: flex; flex-direction: column; gap: 6px;
    }
    .play {
      display: inline-flex; align-items: center; gap: 6px;
      padding: 7px 12px; border-radius: 8px; border: 1px solid #333b4d;
      background: #1c2230; color: #dce2ef; cursor: pointer; font-size: 0.8rem;
      width: fit-content;
    }
    .play:hover { background: #273041; }
    .play.playing { background: #2a3a52; border-color: #4a6280; }
    .play.missing { opacity: 0.45; cursor: default; }
    .custom-actions { display: flex; gap: 6px; flex-wrap: wrap; }
    .btn-sm {
      padding: 6px 10px; border-radius: 7px; border: 1px solid #384055;
      background: #222836; color: #e6e9ef; cursor: pointer; font-size: 0.76rem;
    }
    .btn-sm.warn { background: #3d2a1e; border-color: #6a4a32; }
    .btn-sm.upload { background: #2a3420; border-color: #4a5c38; color: #d8e8c8; }
    .empty { color: #6d768a; padding: 40px 20px; text-align: center; }
    .status {
      position: fixed; bottom: 0; left: 200px; right: 0;
      padding: 8px 20px; font-size: 0.8rem; color: #8b93a7;
      background: rgba(12,14,18,.92); border-top: 1px solid #222833;
    }
    .status.ok { color: #7bc96f; }
    .status.err { color: #e07070; }
    input[type=file] { display: none; }
    @media (max-width: 900px) {
      .main { grid-template-columns: 1fr; }
      .sidebar { display: none; }
      .col-head, .sample { grid-template-columns: 1fr; }
      .status { left: 0; }
    }
  </style>
</head>
<body>
  <div class="app">
    <header class="topbar">
      <div class="brand">
        <h1>War2 Content Lab</h1>
        <p id="subtitle">Audio + custom units</p>
      </div>
      <div class="tabs">
        <button class="tab active" data-tab="audio">Audio</button>
        <button class="tab" data-tab="units">Units</button>
      </div>
      <div class="top-actions" id="audioActions">
        <input id="search" type="search" placeholder="Search samples…"/>
        <button class="btn ghost" id="stopAll">Stop</button>
        <button class="btn" id="openFolder">Open custom samples folder</button>
      </div>
      <div class="top-actions" id="unitActions" style="display:none">
        <button class="btn" id="newUnit">New unit</button>
        <button class="btn ghost" id="openPresets">Open presets folder</button>
      </div>
    </header>
    <div class="panel active" id="panel-audio" style="grid-template-columns:1fr; grid-template-rows:1fr">
    <div class="main">
      <aside class="sidebar">
        <h2>Voice banks</h2>
        <div id="unitNav"></div>
      </aside>
      <section class="content">
        <div class="col-head">
          <span>File</span><span>Original</span><span>Enhanced</span><span>Your version</span>
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
          <div class="empty wide" style="grid-column:1/-1">Select a unit or create a new one.</div>
        </section>
      </div>
    </div>
    <div class="status" id="status"></div>
  </div>
  <script>
    const DATA = __DATA_JSON__;
    const CATALOG = __CATALOG_JSON__;
    let activeUnit = DATA.units[0]?.name || '';
    let activePresetId = CATALOG.presets[0]?.id || '';
    let draft = null;
    let playingBtn = null;
    let currentTab = 'audio';

    function setStatus(msg, ok) {
      const el = document.getElementById('status');
      el.textContent = msg;
      el.className = 'status' + (ok === true ? ' ok' : ok === false ? ' err' : '');
    }

    function switchTab(tab) {
      currentTab = tab;
      document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tab));
      document.getElementById('panel-audio').classList.toggle('active', tab === 'audio');
      document.getElementById('panel-units').classList.toggle('active', tab === 'units');
      document.getElementById('audioActions').style.display = tab === 'audio' ? 'flex' : 'none';
      document.getElementById('unitActions').style.display = tab === 'units' ? 'flex' : 'none';
      if (tab === 'units') { renderPresetNav(); renderUnitForm(); }
    }
    document.querySelectorAll('.tab').forEach(t => t.onclick = () => switchTab(t.dataset.tab));

    function stopAll() {
      document.querySelectorAll('audio').forEach(a => { a.pause(); a.currentTime = 0; });
      document.querySelectorAll('.play.playing').forEach(b => b.classList.remove('playing'));
      playingBtn = null;
    }

    function playAudio(btn, src) {
      if (!src) return;
      stopAll();
      let audio = btn._audio;
      if (!audio) {
        audio = new Audio(src);
        audio.onended = () => btn.classList.remove('playing');
        btn._audio = audio;
      } else {
        audio.src = src;
      }
      audio.currentTime = 0;
      audio.play();
      btn.classList.add('playing');
      playingBtn = btn;
    }

    function trackHtml(label, src, dur, durClass) {
      const cls = durClass ? `dur ${durClass}` : 'dur';
      if (!src) return `<div class="track"><span class="${cls}">—</span><button class="play missing" disabled>— none</button></div>`;
      const safe = src.replace(/"/g, '&quot;');
      return `<div class="track"><span class="${cls}">${dur || '—'}</span><button class="play" data-src="${safe}" onclick="playAudio(this, this.dataset.src)">▶ ${label}</button></div>`;
    }

    function durDelta(a, b) {
      if (a == null || b == null) return '';
      const d = b - a;
      if (Math.abs(d) < 0.005) return '';
      const sign = d > 0 ? '+' : '';
      return `<span class="delta">Δ ${sign}${d.toFixed(2)}s</span>`;
    }

    function renderNav() {
      const nav = document.getElementById('unitNav');
      nav.innerHTML = DATA.units.map(u => `
        <button class="unit-btn${u.name === activeUnit ? ' active' : ''}" data-unit="${u.name}">
          ${u.name}<span class="count">${u.samples.length}</span>
        </button>`).join('');
      nav.querySelectorAll('.unit-btn').forEach(btn => {
        btn.onclick = () => { activeUnit = btn.dataset.unit; renderNav(); renderSamples(); };
      });
      if (currentTab === 'audio') {
        document.getElementById('subtitle').textContent =
          `${DATA.total} samples · ${DATA.customCount} custom · ${activeUnit}`;
      }
    }

    function renderSamples() {
      const unit = DATA.units.find(u => u.name === activeUnit);
      const list = document.getElementById('sampleList');
      const q = document.getElementById('search').value.trim().toLowerCase();
      if (!unit) { list.innerHTML = '<div class="empty">No unit selected</div>'; return; }
      const filtered = unit.samples.filter(s => !q || s.file.toLowerCase().includes(q) || s.id.toLowerCase().includes(q));
      if (!filtered.length) {
        list.innerHTML = '<div class="empty">No samples match your search</div>';
        return;
      }
      list.innerHTML = filtered.map(s => {
        const enhDiff = (s.enhancedDur != null && s.vanillaDur != null && Math.abs(s.enhancedDur - s.vanillaDur) >= 0.005);
        const replDiff = (s.replacerDur != null && s.vanillaDur != null && Math.abs(s.replacerDur - s.vanillaDur) >= 0.005);
        const customCol = s.replacer
          ? trackHtml('Your WAV', s.replacer, s.replacerDurFmt, replDiff ? 'diff' : '') + `
            <div class="custom-actions">
              <label class="btn-sm upload">Replace<input type="file" accept=".wav" data-id="${s.id}"></label>
              <button class="btn-sm warn del" data-id="${s.id}">Remove</button>
            </div>`
          : `<div class="custom-actions">
              <span class="dur">—</span>
              <label class="btn-sm upload">Add WAV<input type="file" accept=".wav" data-id="${s.id}"></label>
            </div>`;
        return `
        <article class="sample${s.hasReplacer ? ' custom' : ''}" data-id="${s.id}">
          <div>
            <div class="sample-name">${s.file}</div>
            <span class="sample-dur">Original ${s.vanillaDurFmt}${durDelta(s.vanillaDur, s.enhancedDur)}</span>
          </div>
          ${trackHtml('Original', s.vanilla, s.vanillaDurFmt, '')}
          ${trackHtml('Enhanced', s.enhanced, s.enhancedDurFmt, enhDiff ? 'diff' : '')}
          <div class="track">${customCol}</div>
        </article>`;
      }).join('');
      if (currentTab === 'audio') {
        document.getElementById('subtitle').textContent =
          `${DATA.total} samples · ${DATA.customCount} custom · ${activeUnit}`;
      }
    }

    function blankDraft() {
      return {
        id: '',
        displayName: 'New Hero',
        baseUnitType: 0x0C,
        audioBank: CATALOG.audioBanks.includes('Knight') ? 'Knight' : (CATALOG.audioBanks[0] || ''),
        stats: { ...CATALOG.defaultStats },
        notes: ''
      };
    }

    function renderPresetNav() {
      const nav = document.getElementById('presetNav');
      if (!CATALOG.presets.length) {
        nav.innerHTML = '<div class="empty" style="padding:12px">No presets yet</div>';
        return;
      }
      nav.innerHTML = CATALOG.presets.map(p => `
        <button class="unit-btn${p.id === activePresetId ? ' active' : ''}" data-id="${p.id}">
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
        if (activePresetId) {
          draft = JSON.parse(JSON.stringify(CATALOG.presets.find(p => p.id === activePresetId) || blankDraft()));
        } else {
          form.innerHTML = '<div class="empty wide" style="grid-column:1/-1">Select a unit or create a new one.</div>';
          return;
        }
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
      const audioPreview = (bank?.samples || []).slice(0, 8).map(s => `
        <article class="sample" style="grid-template-columns: 140px 1fr 1fr; margin-bottom:6px">
          <div class="sample-name">${s.file}</div>
          ${trackHtml('Original', s.vanilla, s.vanillaDurFmt, '')}
          ${trackHtml('Yours', s.replacer || s.enhanced, s.replacer ? s.replacerDurFmt : s.enhancedDurFmt, '')}
        </article>`).join('') || '<div class="empty">No samples in this audio bank</div>';

      form.innerHTML = `
        <div class="field"><label>Display name</label>
          <input id="fName" value="${(draft.displayName||'').replace(/"/g,'&quot;')}"/></div>
        <div class="field"><label>Preset id</label>
          <input id="fId" value="${(draft.id||'').replace(/"/g,'&quot;')}" placeholder="auto from name"/></div>
        <div class="field wide"><label>Base unit type (spells / behavior)</label>
          <select id="fType">${typeOpts}</select></div>
        <div class="spell-hint">${selectedType ? ('Spells: ' + selectedType.spells) : ''}</div>
        <div class="field wide"><label>Audio bank (same as Audio tab)</label>
          <select id="fBank">${bankOpts}</select></div>
        <h3>Stats (UDTA on maps using this preset)</h3>
        ${statsHtml}
        <div class="field wide"><label>Notes</label>
          <textarea id="fNotes">${draft.notes||''}</textarea></div>
        <div class="form-actions">
          <button class="btn primary" id="saveUnit">Save for map editor</button>
          <button class="btn danger" id="deleteUnit" ${draft.id?'':'disabled'}>Delete</button>
        </div>
        <div class="audio-mini">
          <h3>Audio preview — edit WAVs in the Audio tab</h3>
          ${audioPreview}
        </div>`;

      document.getElementById('subtitle').textContent =
        `Custom units · ${CATALOG.presets.length} presets · Content Studio reads this folder`;

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

    document.getElementById('search').oninput = renderSamples;
    document.getElementById('stopAll').onclick = stopAll;
    document.getElementById('openFolder').onclick = () => fetch('/api/open-replacer-folder', { method: 'POST' });
    document.getElementById('openPresets').onclick = () => fetch('/api/open-presets-folder', { method: 'POST' });
    document.getElementById('newUnit').onclick = () => {
      draft = blankDraft();
      activePresetId = '';
      renderPresetNav();
      renderUnitForm();
      switchTab('units');
    };

    document.getElementById('sampleList').addEventListener('change', async e => {
      if (e.target.type !== 'file' || !e.target.files[0]) return;
      const fd = new FormData();
      fd.append('sample_id', e.target.dataset.id);
      fd.append('file', e.target.files[0]);
      setStatus('Saving…');
      try {
        const res = await fetch('/api/replacer', { method: 'POST', body: fd });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Failed');
        location.reload();
      } catch (err) { setStatus(err.message, false); }
    });

    document.getElementById('sampleList').addEventListener('click', async e => {
      const del = e.target.closest('.del');
      if (!del) return;
      await fetch('/api/replacer/' + encodeURIComponent(del.dataset.id), { method: 'DELETE' });
      location.reload();
    });

    if (CATALOG.presets[0]) draft = JSON.parse(JSON.stringify(CATALOG.presets[0]));
    renderNav();
    renderSamples();
  </script>
</body>
</html>"""


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
    secs = seconds % 60
    return f"{minutes}:{secs:05.2f}"


def collect_samples() -> list[dict]:
    samples: list[dict] = []
    if not VANILLA_DIR.is_dir():
        return samples
    for sub in sorted(VANILLA_DIR.iterdir()):
        if not sub.is_dir() or sub.name in SKIP_DIRS:
            continue
        for wav in sorted(sub.glob("*.wav")):
            rel = f"{sub.name}/{wav.name}"
            vanilla_path = VANILLA_DIR / sub.name / wav.name
            enhanced_path = ENHANCED_DIR / sub.name / wav.name
            replacer_path = REPLACER_DIR / sub.name / wav.name
            has_enhanced = enhanced_path.is_file()
            has_replacer = replacer_path.is_file()
            vanilla_dur = wav_duration_seconds(vanilla_path)
            enhanced_dur = wav_duration_seconds(enhanced_path) if has_enhanced else None
            replacer_dur = wav_duration_seconds(replacer_path) if has_replacer else None
            samples.append(
                {
                    "id": rel,
                    "file": wav.name,
                    "unit": sub.name,
                    "vanilla": f"/audio/vanilla/{rel}",
                    "enhanced": f"/audio/enhanced/{rel}" if has_enhanced else "",
                    "replacer": f"/audio/replacer/{rel}" if has_replacer else "",
                    "hasEnhanced": has_enhanced,
                    "hasReplacer": has_replacer,
                    "vanillaDur": vanilla_dur,
                    "enhancedDur": enhanced_dur,
                    "replacerDur": replacer_dur,
                    "vanillaDurFmt": fmt_duration(vanilla_dur),
                    "enhancedDurFmt": fmt_duration(enhanced_dur),
                    "replacerDurFmt": fmt_duration(replacer_dur),
                }
            )
    return samples


def build_payload(samples: list[dict]) -> dict:
    by_unit: dict[str, list[dict]] = defaultdict(list)
    for s in samples:
        by_unit[s["unit"]].append(s)
    units = [{"name": name, "samples": by_unit[name]} for name in sorted(by_unit)]
    return {
        "total": len(samples),
        "customCount": sum(1 for s in samples if s["hasReplacer"]),
        "units": units,
    }


def build_html(samples: list[dict]) -> str:
    payload = json.dumps(build_payload(samples))
    banks = [u["name"] for u in build_payload(samples)["units"]]
    catalog = json.dumps(unit_presets.catalog_payload(banks))
    return (
        APP_HTML.replace("__DATA_JSON__", payload).replace("__CATALOG_JSON__", catalog)
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
        if clean in ("/", "/index.html"):
            samples = collect_samples()
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
        if clean.startswith("/audio/enhanced/"):
            self._wav(ENHANCED_DIR / clean[len("/audio/enhanced/"):].replace("/", "\\"))
            return
        if clean.startswith("/audio/replacer/"):
            self._wav(REPLACER_DIR / clean[len("/audio/replacer/"):].replace("/", "\\"))
            return
        self.send_error(404)

    def do_POST(self) -> None:
        clean = unquote(self.path.split("?", 1)[0])
        if clean == "/api/replacer":
            ctype = self.headers.get("Content-Type", "")
            if "multipart/form-data" not in ctype:
                self._json(400, {"error": "Bad form"})
                return
            length = int(self.headers.get("Content-Length", 0))
            form = cgi.FieldStorage(
                fp=self.rfile,
                headers=self.headers,
                environ={"REQUEST_METHOD": "POST", "CONTENT_TYPE": ctype, "CONTENT_LENGTH": str(length)},
            )
            sample_id = form.getvalue("sample_id")
            fileitem = form["file"] if "file" in form else None
            if not sample_id or "/" not in sample_id:
                self._json(400, {"error": "Invalid sample"})
                return
            if not fileitem or not fileitem.file:
                self._json(400, {"error": "No file uploaded"})
                return
            if not (fileitem.filename or "").lower().endswith(".wav"):
                self._json(400, {"error": "WAV files only"})
                return
            dest = REPLACER_DIR / sample_id.replace("/", "\\")
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(fileitem.file.read())
            self._json(200, {"ok": True, "path": str(dest)})
            return
        if clean == "/api/open-replacer-folder":
            REPLACER_DIR.mkdir(parents=True, exist_ok=True)
            subprocess.Popen(["explorer", str(REPLACER_DIR)])  # noqa: S603,S607
            self._json(200, {"ok": True})
            return
        if clean == "/api/open-presets-folder":
            unit_presets.ensure_presets_dir()
            subprocess.Popen(["explorer", str(unit_presets.PRESETS_DIR)])  # noqa: S603,S607
            self._json(200, {"ok": True})
            return
        if clean == "/api/presets":
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length) if length else b"{}"
            try:
                body = json.loads(raw.decode("utf-8"))
                saved = unit_presets.save_preset(body)
                self._json(200, {"ok": True, "saved": saved, "presets": unit_presets.load_presets()})
            except (ValueError, json.JSONDecodeError) as exc:
                self._json(400, {"error": str(exc)})
            return
        self.send_error(404)

    def do_DELETE(self) -> None:
        clean = unquote(self.path.split("?", 1)[0])
        if clean.startswith("/api/replacer/"):
            sample_id = clean[len("/api/replacer/"):]
            dest = REPLACER_DIR / sample_id.replace("/", "\\")
            if dest.is_file():
                dest.unlink()
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


def main() -> None:
    REPLACER_DIR.mkdir(parents=True, exist_ok=True)
    unit_presets.ensure_presets_dir()
    if not VANILLA_DIR.is_dir():
        raise SystemExit("Audio missing. Run Install War2 Voice Compare on your Desktop.")
    samples = collect_samples()
    if not samples:
        raise SystemExit("No samples found.")

    import socket

    class ReuseServer(ThreadingHTTPServer):
        allow_reuse_address = True

    for attempt in range(2):
        try:
            server = ReuseServer(("127.0.0.1", PORT), Handler)
            break
        except OSError:
            if attempt == 0:
                # Free stale listener from an old session.
                import subprocess as sp
                try:
                    out = sp.check_output(
                        ["netstat", "-ano"], text=True, stderr=sp.DEVNULL  # noqa: S603
                    )
                    for line in out.splitlines():
                        if f":{PORT}" in line and "LISTENING" in line:
                            pid = line.split()[-1]
                            if pid.isdigit():
                                sp.run(  # noqa: S603
                                    ["taskkill", "/F", "/PID", pid],
                                    stdout=sp.DEVNULL,
                                    stderr=sp.DEVNULL,
                                )
                except Exception:
                    pass
            else:
                raise SystemExit(f"Port {PORT} is in use. Close other Voice Compare windows.")

    url = f"http://127.0.0.1:{PORT}/"
    print(f"War2 Content Lab: {url}  ({len(samples)} samples, {len(unit_presets.load_presets())} presets)")
    print(f"Presets folder: {unit_presets.PRESETS_DIR}")
    print("Close this window to stop.")
    open_app_window(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopped.")


if __name__ == "__main__":
    main()
