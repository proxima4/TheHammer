#!/usr/bin/env python3
"""
The Hammer - drum tab reader
============================
Reads ordinary ASCII drum tablature (the style used on most tab sites and on
Wikipedia's "Drum tablature" page) and turns it into bars The Hammer's neural
brain can learn from. It can also write a MIDI file so you can drag it into
FL Studio and hear that the tab was understood.

    python tab_reader.py my_tabs/funk/funky_drummer_break.txt            # show what it read
    python tab_reader.py my_tabs/funk/funky_drummer_break.txt --midi out.mid

TAB FORMAT (see my_tabs/README.txt for the full cheat sheet)

    title: Basic rock beat
    style: rock          (optional - otherwise the folder name is used)
    tempo: 110           (optional - only used for the preview MIDI)

       1e+a2e+a3e+a4e+a
    HH|x-x-x-x-x-x-x-x-|x-x-x-x-x-x-x-o-|
    SN|----o-------o---|----o-------o---|
    B |o-------o-o-----|o-------o-o-----|  x4

  * one line per drum, a label then bars between | bars; lines stacked
    together form a block, blank lines separate blocks
  * 16 characters per bar = 16th notes, 12 = triplets, 8 = 8ths, 32 = 32nds
  * "x4" (or x2, x8 ...) at the end of a block repeats the whole block
  * rests: - . or space
"""
import math, os, re, sys

# --- line labels -> lane family --------------------------------------------
LABELS = {
    "crash": ["CC", "C", "CR", "CY", "CRASH", "C1", "CR1"],
    "crash2": ["C2", "CR2"],
    "hihat": ["HH", "H", "HI", "HAT", "HC", "HHC"],
    "hhpedal": ["HF", "FH", "HP", "PH", "HHF", "FOOT"],
    "ride": ["RD", "R", "RC", "RIDE", "RI"],
    "china": ["CH", "CN", "CHINA"],
    "splash": ["SP", "SPL", "SPLASH"],
    "snare": ["SN", "S", "SD", "SNR", "SNARE"],
    "tom1": ["T1", "HT", "T", "TOM1", "TT"],
    "tom2": ["T2", "MT", "TOM2"],
    "floor": ["T3", "T4", "FT", "F", "LT", "FLOOR", "TOM3"],
    "kick": ["B", "BD", "K", "KD", "KICK", "BASS", "BAS"],
    "cowbell": ["CB", "COW", "COWBELL"],
    "tamb": ["TA", "TAMB", "TB"],
    "shaker": ["SH", "SK", "SHAKER"],
}
LABEL_TO_FAMILY = {a: fam for fam, al in LABELS.items() for a in al}
REST = set("-. _")
NORMAL, ACCENT, GHOST = 0.72, 0.95, 0.3

# Hammer lane names (shared with train_brain.py) and their General MIDI notes
GM_NOTE = {"kick": 36, "snare": 38, "rim": 37, "hhc": 42, "hhp": 44, "hho": 46, "high": 48, "mid": 45, "floor": 43,
           "crash": 49, "china": 52, "splash": 55, "ride": 51, "bell": 53, "cowbell": 56, "tamb": 54, "shaker": 70}


def symbol(family, ch):
    """(lane, velocity) for one tab character on one drum line, or None."""
    if ch in REST or ch in "#|":
        return None
    if family == "hihat":
        return {"x": ("hhc", NORMAL), "X": ("hhc", ACCENT), "o": ("hho", NORMAL), "O": ("hho", ACCENT),
                "g": ("hhc", GHOST), "+": ("hhc", NORMAL), "f": ("hhp", NORMAL)}.get(ch)
    if family == "hhpedal":
        return ("hhp", ACCENT if ch.isupper() else NORMAL) if ch.lower() in "xo" else None
    if family == "ride":
        if ch in "bB":
            return ("bell", ACCENT if ch == "B" else NORMAL)
        return {"x": ("ride", NORMAL), "X": ("ride", ACCENT), "o": ("ride", NORMAL), "O": ("ride", ACCENT), "g": ("ride", GHOST)}.get(ch)
    if family in ("crash", "crash2", "china", "splash"):
        lane = {"crash": "crash", "crash2": "crash", "china": "china", "splash": "splash"}[family]
        if ch in "cC":
            lane = "china"
        if ch in "sS":
            lane = "splash"
        if ch in "xXoOcCsS":
            return (lane, ACCENT if ch.isupper() else 0.85)
        if ch == "g":
            return (lane, GHOST)
        return None
    if family in ("cowbell", "tamb", "shaker"):
        return ({"cowbell": "cowbell", "tamb": "tamb", "shaker": "shaker"}[family], ACCENT if ch.isupper() else NORMAL) if ch.lower() in "xo" else None
    # drums
    lane = {"snare": "snare", "tom1": "high", "tom2": "mid", "floor": "floor", "kick": "kick"}[family]
    if family == "snare" and ch in "xX@":
        return ("rim", ACCENT if ch == "X" else 0.8)          # cross-stick / rim click
    table = {"o": NORMAL, "O": ACCENT, "g": GHOST, "G": 0.4, "f": 0.85, "F": ACCENT, "d": 0.75, "D": 0.9,
             "b": 0.55, "B": 0.9, "x": NORMAL, "X": ACCENT, "@": 0.8}
    return (lane, table[ch]) if ch in table else None


def parse_line(line):
    """'HH|x-x-|x-x-| x4' -> (family, [bar strings], repeat) or None."""
    m = re.match(r"^\s*([A-Za-z][A-Za-z0-9]{0,5})\s*[:|]?\s*\|(.*)$", line)
    if not m:
        return None
    fam = LABEL_TO_FAMILY.get(m.group(1).upper())
    if fam is None:
        return None
    body = m.group(2)
    repeat = 1
    last = body.rfind("|")
    if last >= 0:
        rm = re.match(r"^\s*\(?\s*[xX×*]\s*(\d+)\s*\)?\s*$", body[last + 1:])
        if rm:
            repeat = int(rm.group(1))
            body = body[: last + 1]
    bars = [b for b in body.split("|")]
    # drop empty fragments at the ends (from leading/trailing |)
    while bars and bars[-1].strip() == "":
        bars.pop()
    while bars and bars[0] == "":
        bars.pop(0)
    return fam, bars, repeat


def parse_tab(text):
    """Returns {"title", "style", "tempo", "bars": [[(pos16, lane, vel), ...], ...], "warnings": [...]}."""
    meta = {"title": "", "style": "", "tempo": 100.0}
    warnings = []
    blocks, cur = [], []
    for raw in text.splitlines():
        line = raw.rstrip("\n")
        hm = re.match(r"^\s*(title|style|tempo|bpm)\s*[:=]\s*(.+)$", line, re.I)
        if hm:
            k = hm.group(1).lower()
            v = hm.group(2).strip()
            if k in ("tempo", "bpm"):
                try:
                    meta["tempo"] = float(re.findall(r"[\d.]+", v)[0])
                except IndexError:
                    pass
            else:
                meta[k] = v
            continue
        p = parse_line(line)
        if p:
            cur.append(p)
        elif cur and line.strip() == "":
            blocks.append(cur); cur = []
        # count lines ("1e+a2e+a") and any other text are ignored
    if cur:
        blocks.append(cur)

    bars_out = []
    for bi, block in enumerate(blocks):
        nbars = max(len(b) for _, b, _ in block)
        repeat = max(r for _, _, r in block)
        block_bars = []
        for k in range(nbars):
            events = []
            for fam, bars, _ in block:
                if k >= len(bars):
                    continue
                s = bars[k]
                L = len(s)
                if L == 0:
                    continue
                if L not in (4, 6, 8, 12, 16, 24, 32, 48):
                    warnings.append(f"block {bi + 1} bar {k + 1} line {fam}: {L} characters (expected 8, 12, 16, 24 or 32)")
                for i, ch in enumerate(s):
                    sym = symbol(fam, ch)
                    if sym:
                        events.append((i * 16.0 / L, sym[0], sym[1]))
            block_bars.append(sorted(events))
        bars_out.extend(block_bars * repeat)
    meta["bars"] = bars_out
    meta["warnings"] = warnings
    return meta


def to_grid_text(bars, max_bars=4):
    """Pretty 16th-note grid for checking what was read."""
    lanes = ["crash", "china", "splash", "ride", "bell", "hho", "hhc", "hhp", "high", "mid", "floor", "snare", "rim", "kick", "cowbell", "tamb", "shaker"]
    used = [l for l in lanes if any(e[1] == l for b in bars[:max_bars] for e in b)]
    out = []
    for lane in used:
        row = ""
        for b in bars[:max_bars]:
            cells = ["-"] * 16
            for pos, ln, vel in b:
                if ln == lane:
                    s = int(round(pos)) % 16
                    cells[s] = "X" if vel > 0.9 else ("g" if vel < 0.45 else "x")
            row += "|" + "".join(cells)
        out.append(f"  {lane:6s} {row}|")
    return "\n".join(out)


def write_midi(parsed, path):
    import pretty_midi
    tempo = parsed["tempo"] or 100.0
    pm = pretty_midi.PrettyMIDI(initial_tempo=tempo)
    inst = pretty_midi.Instrument(program=0, is_drum=True, name="The Hammer tab")
    sixteenth = 60.0 / tempo / 4
    for b, events in enumerate(parsed["bars"]):
        for pos, lane, vel in events:
            t = (b * 16 + pos) * sixteenth
            inst.notes.append(pretty_midi.Note(int(max(1, min(127, vel * 127))), GM_NOTE[lane], t, t + sixteenth * 0.5))
    pm.instruments.append(inst)
    pm.write(path)


def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    path = sys.argv[1]
    with open(path, encoding="utf-8", errors="replace") as f:
        parsed = parse_tab(f.read())
    print(f"{os.path.basename(path)}: '{parsed['title']}'  style: {parsed['style'] or '(from folder)'}  "
          f"tempo: {parsed['tempo']:.0f}  bars: {len(parsed['bars'])}")
    for w in parsed["warnings"]:
        print("  warning:", w)
    print(to_grid_text(parsed["bars"]))
    if "--midi" in sys.argv:
        out = sys.argv[sys.argv.index("--midi") + 1]
        write_midi(parsed, out)
        print("wrote", out)


if __name__ == "__main__":
    main()
