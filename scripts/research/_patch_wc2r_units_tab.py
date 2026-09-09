from pathlib import Path

path = Path(r"C:\Users\mauri\AppData\Local\WC2rAudioTool\audio_tool.py")
text = path.read_text(encoding="utf-8")
orig = text

if "import unit_presets" not in text:
    text = text.replace(
        "from urllib.parse import unquote\n",
        "from urllib.parse import unquote\n\nimport unit_presets\n",
        1,
    )

css = """
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
"""
marker = "    audio.track-audio { display: none; }\n  </style>\n</head>\n<body>\n  <div class=\"app\">\n    <header class=\"topbar\">"
if " .tabs { display: flex;" not in text:
    if marker not in text:
        raise SystemExit("CSS marker not found")
    text = text.replace(marker, css + marker, 1)

old_top = """    <header class=\"topbar\">
      <div class=\"brand\">
        <h1>WC2r Audio Tool</h1>
        <p id=\"subtitle\">Unit voices</p>
      </div>
      <div class=\"top-actions\">
        <input id=\"search\" type=\"search\" placeholder=\"Search samples…\"/>
      </div>
    </header>
    <div class=\"main\">"""
new_top = """    <header class=\"topbar\">
      <div class=\"brand\">
        <h1>WC2r Audio Tool</h1>
        <p id=\"subtitle\">Unit voices</p>
      </div>
      <div class=\"tabs\">
        <button type=\"button\" class=\"tab active\" data-tab=\"audio\">Audio</button>
        <button type=\"button\" class=\"tab\" data-tab=\"units\">Units</button>
      </div>
      <div class=\"top-actions\" id=\"audioActions\">
        <input id=\"search\" type=\"search\" placeholder=\"Search samples…\"/>
      </div>
      <div class=\"top-actions\" id=\"unitActions\">
        <button type=\"button\" class=\"btn\" id=\"newUnit\">New unit</button>
        <button type=\"button\" class=\"btn ghost\" id=\"openPresets\">Open presets folder</button>
      </div>
    </header>
    <div class=\"panel active\" id=\"panel-audio\">
    <div class=\"main\">"""
if 'data-tab="audio"' not in text:
    if old_top not in text:
        raise SystemExit("topbar marker not found")
    text = text.replace(old_top, new_top, 1)

old_status = """    </div>
    <div class=\"status\" id=\"status\"></div>
  </div>
  <script>
    const DATA = __DATA_JSON__;"""
new_status = """    </div>
    </div>
    <div class=\"panel\" id=\"panel-units\">
      <div class=\"units-main\">
        <aside class=\"sidebar\">
          <h2>Custom units</h2>
          <div id=\"presetNav\"></div>
        </aside>
        <section class=\"unit-form\" id=\"unitForm\">
          <div class=\"empty\" style=\"grid-column:1/-1\">Select a unit or create a new one.</div>
        </section>
      </div>
    </div>
    <div class=\"status\" id=\"status\"></div>
  </div>
  <script>
    const DATA = __DATA_JSON__;
    const CATALOG = __CATALOG_JSON__;"""
if "const CATALOG = __CATALOG_JSON__" not in text:
    if old_status not in text:
        raise SystemExit("status marker not found")
    text = text.replace(old_status, new_status, 1)

needle = "    let activeUnit = DATA.units[0]?.name || '';\n    let showLongUnit = null;"
insert = """    let activeUnit = DATA.units[0]?.name || '';
    let currentTab = 'audio';
    let activePresetId = (CATALOG.presets[0] && CATALOG.presets[0].id) || '';
    let draft = CATALOG.presets[0] ? JSON.parse(JSON.stringify(CATALOG.presets[0])) : null;
    let showLongUnit = null;"""
if "let currentTab = 'audio'" not in text:
    if needle not in text:
        raise SystemExit("activeUnit needle not found")
    text = text.replace(needle, insert, 1)

units_js_path = Path(__file__).with_name("_units_tab_fragment.js")
# fragment inlined below
units_js = r'''
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

'''

boot = """    renderNav();
    renderLongPanel();
    renderSamples();
  </script>"""
boot2 = units_js + """
    renderNav();
    renderLongPanel();
    renderSamples();
  </script>"""
if "function switchTab(tab)" not in text:
    if boot not in text:
        raise SystemExit("boot marker not found")
    text = text.replace(boot, boot2, 1)

old_build = '''def build_html(samples: list[dict]) -> str:
    return APP_HTML.replace("__DATA_JSON__", json.dumps(build_payload(samples)))'''
new_build = '''def build_html(samples: list[dict]) -> str:
    payload = build_payload(samples)
    banks = [u["name"] for u in payload["units"]]
    catalog = unit_presets.catalog_payload(banks)
    return (
        APP_HTML.replace("__DATA_JSON__", json.dumps(payload)).replace(
            "__CATALOG_JSON__", json.dumps(catalog)
        )
    )'''
if "unit_presets.catalog_payload" not in text:
    if old_build not in text:
        raise SystemExit("build_html not found")
    text = text.replace(old_build, new_build, 1)

old_post_end = '''        if clean == "/api/open-long-sample":
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
        self.send_error(404)'''
new_post_end = '''        if clean == "/api/open-long-sample":
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
        self.send_error(404)'''
if 'clean == "/api/presets"' not in text:
    if old_post_end not in text:
        raise SystemExit("post end not found")
    text = text.replace(old_post_end, new_post_end, 1)

old_del = '''            if load_selections().get(sample_id) == choice_key:
                clear_sample_selection(sample_id)
            self._json(200, {"ok": True})
            return
        self.send_error(404)

    def log_message(self, format: str, *args) -> None:'''
new_del = '''            if load_selections().get(sample_id) == choice_key:
                clear_sample_selection(sample_id)
            self._json(200, {"ok": True})
            return
        if clean.startswith("/api/presets/"):
            preset_id = clean[len("/api/presets/"):]
            unit_presets.delete_preset(preset_id)
            self._json(200, {"ok": True, "presets": unit_presets.load_presets()})
            return
        self.send_error(404)

    def log_message(self, format: str, *args) -> None:'''
if 'clean.startswith("/api/presets/")' not in text:
    if old_del not in text:
        raise SystemExit("delete end not found")
    text = text.replace(old_del, new_del, 1)

if "unit_presets.ensure_presets_dir()" not in text.split("def main")[-1]:
    text = text.replace(
        "def main() -> None:\n    REPLACER_DIR.mkdir(parents=True, exist_ok=True)",
        "def main() -> None:\n    REPLACER_DIR.mkdir(parents=True, exist_ok=True)\n    unit_presets.ensure_presets_dir()",
        1,
    )

if text == orig:
    raise SystemExit("No changes applied")
path.write_text(text, encoding="utf-8")
import ast

ast.parse(text)
print("patched ok", path.stat().st_size)
