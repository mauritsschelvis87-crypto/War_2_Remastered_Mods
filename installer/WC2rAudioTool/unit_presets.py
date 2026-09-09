"""Shared custom-unit presets for WC2r Audio Tool → War2 Content Studio."""

from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

APP_DIR = Path(os.environ.get("WC2R_AUDIO_TOOL_DIR", Path(__file__).resolve().parent))
PRESETS_DIR = APP_DIR / "unit-presets"
# Optional seed from a repo copy next to this file / examples folder.
REPO_EXAMPLE_DIR = Path(__file__).resolve().parent / "unit-presets-examples"

BASE_UNIT_TYPES: list[dict] = [
    {"id": 0x0A, "label": "Mage", "spells": "Flame Shield, Fireball, Slow, Invisibility, Polymorph, Blizzard", "group": "Human casters"},
    {"id": 0x0C, "label": "Paladin", "spells": "Holy Vision, Heal, Exorcism", "group": "Human casters"},
    {"id": 0x0B, "label": "Death Knight", "spells": "Raise Dead, Death Coil, Whirlwind, Haste, Unholy Armor, Death and Decay", "group": "Orc casters"},
    {"id": 0x0D, "label": "Ogre-Mage", "spells": "Eye of Kilrogg, Bloodlust, Runes", "group": "Orc casters"},
    {"id": 0x18, "label": "Khadgar", "spells": "Mage set (hero)", "group": "Human heroes"},
    {"id": 0x14, "label": "Alleria", "spells": "Ranger-style hero (no mage book)", "group": "Human heroes"},
    {"id": 0x2C, "label": "Turalyon", "spells": "Paladin-style hero", "group": "Human heroes"},
    {"id": 0x2E, "label": "Danath", "spells": "Footman-style hero", "group": "Human heroes"},
    {"id": 0x32, "label": "Lothar", "spells": "Knight-style hero", "group": "Human heroes"},
    {"id": 0x34, "label": "Uther Lightbringer", "spells": "Paladin-style hero", "group": "Human heroes"},
    {"id": 0x15, "label": "Teron Gorefiend", "spells": "Death Knight set (hero)", "group": "Orc heroes"},
    {"id": 0x17, "label": "Dentarg", "spells": "Ogre-Mage set (hero)", "group": "Orc heroes"},
    {"id": 0x19, "label": "Grom Hellscream", "spells": "Grunt-style hero", "group": "Orc heroes"},
    {"id": 0x31, "label": "Cho'gall", "spells": "Ogre-Mage set (hero)", "group": "Orc heroes"},
    {"id": 0x33, "label": "Gul'dan", "spells": "Death Knight set (hero)", "group": "Orc heroes"},
    {"id": 0x35, "label": "Zul'jin", "spells": "Axethrower-style hero", "group": "Orc heroes"},
    {"id": 0x00, "label": "Footman", "spells": "None", "group": "Basic (no spells)"},
    {"id": 0x06, "label": "Knight", "spells": "None (upgrade→Paladin)", "group": "Basic (no spells)"},
    {"id": 0x01, "label": "Grunt", "spells": "None", "group": "Basic (no spells)"},
    {"id": 0x07, "label": "Ogre", "spells": "None (upgrade→Ogre-Mage)", "group": "Basic (no spells)"},
]

DEFAULT_STATS = {
    "hitPoints": 60,
    "armor": 2,
    "basicDamage": 6,
    "piercingDamage": 3,
    "sightRange": 4,
    "attackRange": 1,
    "magic": 0,
    "buildTime": 60,
    "goldCostTenths": 60,
    "lumberCostTenths": 0,
    "oilCostTenths": 0,
    "priority": 50,
    "pointValue": 50,
}

STAT_FIELDS = [
    ("hitPoints", "Hit points"),
    ("armor", "Armor"),
    ("basicDamage", "Basic damage"),
    ("piercingDamage", "Piercing damage"),
    ("sightRange", "Sight range"),
    ("attackRange", "Attack range"),
    ("magic", "Magic (0/1)"),
    ("buildTime", "Build time (6=1s)"),
    ("goldCostTenths", "Gold cost ×10"),
    ("lumberCostTenths", "Lumber cost ×10"),
    ("oilCostTenths", "Oil cost ×10"),
    ("priority", "Priority"),
    ("pointValue", "Kill points"),
]


def slugify(name: str) -> str:
    s = re.sub(r"[^a-zA-Z0-9]+", "-", name.strip().lower()).strip("-")
    return s or "unit"


def label_for_type(type_id: int) -> str:
    for row in BASE_UNIT_TYPES:
        if row["id"] == type_id:
            return row["label"]
    return f"Type 0x{type_id:02X}"


def ensure_presets_dir() -> Path:
    PRESETS_DIR.mkdir(parents=True, exist_ok=True)
    if REPO_EXAMPLE_DIR.is_dir():
        for src in REPO_EXAMPLE_DIR.glob("*.json"):
            dest = PRESETS_DIR / src.name
            if not dest.is_file():
                shutil.copy2(src, dest)
    return PRESETS_DIR


def load_presets() -> list[dict]:
    ensure_presets_dir()
    out: list[dict] = []
    for path in sorted(PRESETS_DIR.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(data, dict) or "displayName" not in data:
            continue
        data.setdefault("id", path.stem)
        data.setdefault("stats", dict(DEFAULT_STATS))
        data.setdefault("audioBank", "Human")
        data.setdefault("baseUnitType", 0x0C)
        data.setdefault("baseUnitLabel", label_for_type(int(data["baseUnitType"])))
        data.setdefault("notes", "")
        data["_file"] = path.name
        out.append(data)
    return out


def save_preset(preset: dict) -> dict:
    ensure_presets_dir()
    display = str(preset.get("displayName") or "").strip()
    if not display:
        raise ValueError("displayName is required")
    preset_id = slugify(str(preset.get("id") or display))
    base_type = int(preset.get("baseUnitType", 0x0C))
    stats_in = preset.get("stats") if isinstance(preset.get("stats"), dict) else {}
    stats = dict(DEFAULT_STATS)
    for key in DEFAULT_STATS:
        if key in stats_in:
            stats[key] = int(stats_in[key])
    cleaned = {
        "version": 1,
        "id": preset_id,
        "displayName": display,
        "baseUnitType": base_type,
        "baseUnitLabel": label_for_type(base_type),
        "audioBank": str(preset.get("audioBank") or "Human"),
        "stats": stats,
        "notes": str(preset.get("notes") or ""),
    }
    path = PRESETS_DIR / f"{preset_id}.json"
    path.write_text(json.dumps(cleaned, indent=2) + "\n", encoding="utf-8")
    cleaned["_file"] = path.name
    return cleaned


def delete_preset(preset_id: str) -> bool:
    path = PRESETS_DIR / f"{slugify(preset_id)}.json"
    if path.is_file():
        path.unlink()
        return True
    return False


def catalog_payload(audio_banks: list[str]) -> dict:
    return {
        "baseUnitTypes": BASE_UNIT_TYPES,
        "statFields": [{"key": k, "label": lab} for k, lab in STAT_FIELDS],
        "defaultStats": DEFAULT_STATS,
        "audioBanks": audio_banks,
        "presets": load_presets(),
        "presetsDir": str(PRESETS_DIR),
    }
