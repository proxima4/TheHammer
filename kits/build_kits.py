#!/usr/bin/env python3
"""
The Hammer - free kit pack builder
==================================
Downloads freely licensed drum libraries and turns them into ready-to-load
Hammer kit folders (stereo mixdowns, one folder per drum, velocity layers and
round robins preserved). Runs in GitHub Actions (build-kits workflow) or locally:

    python build_kits.py --src ./sources --out ./TheHammer-Kits

Kits produced (all free to use and redistribute with credit):
  DRS Kit            DrumGizmo DRSKit (CC BY 4.0) - versatile jazz/rock kit, sticks
  DRS Kit Brushes    DrumGizmo DRSKit whisker articulations (CC BY 4.0)
  Muldjord Metal Kit DrumGizmo MuldjordKit (CC BY 4.0) - Tama Superstar, double kick, china
  Big Rusty Drums    Karoryfer Samples (CC0) - huge 1980s kit
  VSCO Orchestral    Versilian Studios VSCO 2 CE (CC0) - timpani, concert bass drum, clash, gong
  VCSL Cajon         Versilian Community Sample Library (CC0) - cajon, bongos, congas, shaker, tambourine
"""
import argparse, json, os, re, subprocess, sys
from collections import defaultdict
import numpy as np
import soundfile as sf

REPOS = {
    "drs": "https://github.com/sfzinstruments/DrumGizmo.DRSKit.git",
    "muldjord": "https://github.com/sfzinstruments/DrumGizmo.MuldjordKit.git",
    "rusty": "https://github.com/sfzinstruments/karoryfer.big-rusty-drums.git",
    "vsco": "https://github.com/sgossner/VSCO-2-CE.git",
    "vcsl": "https://github.com/sgossner/VCSL.git",
}
SPARSE = {
    "drs": ["DrumGizmo/DRSKit/Samples"],
    "muldjord": ["DrumGizmo/MuldjordKit/Samples"],
    "rusty": ["/LICENSE", "Samples/kick_24", "Samples/snare_14", "Samples/tom_14", "Samples/tom_15", "Samples/tom_18",
              "Samples/hihat_14", "Samples/crash_17", "Samples/crash_sizzle_17", "Samples/ride_22", "Samples/china_18"],
    "vsco": ["Percussion"],
    "vcsl": ["Idiophones/Struck Idiophones/Cajon", "Idiophones/Struck Idiophones/Claps",
             "Idiophones/Struck Idiophones/Shaker, Small", "Idiophones/Struck Idiophones/Tambourine 1",
             "Membranophones/Struck Membranophones/Bongos", "Membranophones/Struck Membranophones/Conga"],
}
MAX_LEN = {"kick": 2.5, "snare": 2.5, "rim": 1.5, "hihat_closed": 1.2, "hihat_pedal": 1.0, "hihat_open": 4.0,
           "tom_high": 3.5, "tom_mid": 3.5, "tom_floor": 4.0, "crash": 6.0, "crash2": 6.0, "ride": 6.0,
           "ride_bell": 5.0, "china": 5.0, "splash": 3.0, "clap": 1.5, "cowbell": 2.0, "shaker": 1.0,
           "tambourine": 2.0, "perc": 2.0}
MAX_HITS = 40


def fetch(name, src):
    dest = os.path.join(src, name)
    if os.path.isdir(dest):
        return dest
    print(f"== cloning {name}")
    subprocess.check_call(["git", "clone", "--depth", "1", "--filter=blob:none", "--sparse", REPOS[name], dest])
    subprocess.check_call(["git", "-C", dest, "sparse-checkout", "set", "--no-cone"] + SPARSE[name])
    return dest


def read(path):
    data, sr = sf.read(path, always_2d=True, dtype="float32")
    if data.shape[1] == 1:
        data = np.repeat(data, 2, axis=1)
    return data[:, :2], sr


def pan_gains(pan):
    a = (pan + 1) * np.pi / 4
    return np.cos(a) * np.sqrt(2), np.sin(a) * np.sqrt(2)


def mix(files_by_mic, mics):
    """files_by_mic: {mic: path}; mics: {mic: (gain, pan[, stereo])}. mic None = an already-mixed stereo file."""
    out, sr = None, None
    for mic, path in files_by_mic.items():
        if mic is None:
            g, p, stereo = 1.0, 0.0, True
        else:
            spec = mics.get(mic, (0.0, 0.0))
            g, p, stereo = spec[0], spec[1], len(spec) > 2 and bool(spec[2])
        if g <= 0:
            continue
        d, s = read(path)
        sr = sr or s
        if stereo:
            st = d * g
        else:
            l, r = pan_gains(p)
            mono = d.mean(axis=1)
            st = np.stack([mono * l * g, mono * r * g], 1)
        if out is None:
            out = st
        else:
            n = max(len(out), len(st))
            out = np.pad(out, ((0, n - len(out)), (0, 0))) + np.pad(st, ((0, n - len(st)), (0, 0)))
    return out, sr


def tidy(x, sr, lane):
    if x is None:
        return None
    peak = np.max(np.abs(x)) + 1e-12
    # trim leading silence (keep 2 ms) and trailing below -70 dB
    env = np.max(np.abs(x), axis=1)
    above = np.where(env > peak * 10 ** (-60 / 20))[0]
    if len(above) == 0:
        return None
    start = max(0, above[0] - int(0.002 * sr))
    tail = np.where(env > peak * 10 ** (-70 / 20))[0][-1]
    end = min(len(x), tail + 1, start + int(MAX_LEN.get(lane, 3.0) * sr))
    x = x[start:end].copy()
    fade = min(len(x), int(0.05 * sr))
    x[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return x


def loudness(x, sr):
    win = max(16, int(0.012 * sr))
    seg = x[: int(0.12 * sr)]
    best = 0.0
    for s in range(0, max(1, len(seg) - win), win // 2):
        best = max(best, float(np.sqrt(np.mean(seg[s:s + win] ** 2))))
    return best


def centroid(x, sr):
    m = x.mean(axis=1)[: int(0.25 * sr)]
    spec = np.abs(np.fft.rfft(m * np.hanning(len(m))))
    f = np.fft.rfftfreq(len(m), 1 / sr)
    return float((spec * f).sum() / (spec.sum() + 1e-9))


def mid_share(x, sr):
    """Share of energy between 300 Hz and 2 kHz - separates cajon bass tone (low) from slap (high)."""
    m = x.mean(axis=1)[: int(0.3 * sr)]
    spec = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    f = np.fft.rfftfreq(len(m), 1 / sr)
    return float(spec[(f > 300) & (f < 2000)].sum() / (spec.sum() + 1e-12))


def thin(hits, n=MAX_HITS):
    """Keep at most n hits spread evenly across the loudness range."""
    if len(hits) <= n:
        return hits
    hits = sorted(hits, key=lambda h: h[2])
    idx = np.linspace(0, len(hits) - 1, n).round().astype(int)
    return [hits[i] for i in sorted(set(idx))]


class Kit:
    def __init__(self, folder, name, styles, license_, author, credits):
        self.folder, self.name, self.styles, self.license, self.author, self.credits = folder, name, styles, license_, author, credits
        self.lanes = defaultdict(list)  # lane -> [(name, audio, loud, sr)]
        self.gain_db = {}

    def add(self, lane, name, audio, sr):
        audio = tidy(audio, sr, lane)
        if audio is not None:
            self.lanes[lane].append((name, audio, loudness(audio, sr), sr))

    def write(self, out_root):
        if not self.lanes:
            print(f"  !! {self.name}: no samples found, skipped")
            return
        root = os.path.join(out_root, self.folder)
        os.makedirs(root, exist_ok=True)
        peak = max(np.max(np.abs(a)) for hits in self.lanes.values() for _, a, _, _ in hits) + 1e-9
        norm = 0.95 / peak
        total = 0
        for lane, hits in self.lanes.items():
            d = os.path.join(root, lane)
            os.makedirs(d, exist_ok=True)
            for name, audio, _, sr in thin(hits):
                sf.write(os.path.join(d, f"{name}.flac"), np.clip(audio * norm, -1, 1), sr, subtype="PCM_24")
                total += 1
        meta = {"name": self.name, "author": self.author, "license": self.license, "credits": self.credits,
                "styles": self.styles, "gain_db": self.gain_db}
        with open(os.path.join(root, "kit.json"), "w") as f:
            json.dump(meta, f, indent=2)
        print(f"  -> {self.name}: {total} samples in {len(self.lanes)} drums")


# ---------------------------------------------------------------------------
def drumgizmo_hits(samples_dir, instrument):
    """DrumGizmo layout: Samples/<Inst>/<N>-<Inst>-<Mic>.flac -> {N: {mic: path}}"""
    d = os.path.join(samples_dir, instrument)
    hits = defaultdict(dict)
    if not os.path.isdir(d):
        return hits
    for f in os.listdir(d):
        m = re.match(r"(\d+)-(.+)-([^-]+)\.(flac|wav)$", f)
        if m:
            hits[int(m.group(1))][m.group(3)] = os.path.join(d, f)
    return hits


def build_drs(src, out, brushes=False):
    base = os.path.join(fetch("drs", src), "DrumGizmo/DRSKit/Samples")
    mics = {"Kdrum_front": (1.0, 0.0), "Kdrum_back": (0.5, 0.0), "Snare_top": (1.0, 0.0), "Snare_bottom": (0.45, 0.0),
            "Hihat": (0.45, -0.45), "Ride": (0.4, 0.45), "Tom1": (0.8, -0.2), "Tom2": (0.8, 0.1), "Tom3": (0.85, 0.45),
            "OHL": (0.75, -0.85), "OHR": (0.75, 0.85), "AmbL": (0.45, -1.0), "AmbR": (0.45, 1.0)}
    if brushes:
        kit = Kit("DRS Kit Brushes", "DRS Kit - Brushes", ["jazz", "blues"], "CC BY 4.0", "DrumGizmo team / DRS Drums (Jes Eiler)",
                  "DRSKit by the DrumGizmo team, recorded with DRS Drums. https://drumgizmo.org")
        lanes = {"kick": ["Kdrum_without_contact"], "snare": ["Snare_whisker", "Snare_circle_whisker"], "hihat_closed": ["Hihat_closed_whisker"],
                 "hihat_open": ["Hihat_open_whisker"], "hihat_pedal": ["Hihat_foot"], "tom_high": ["Tom1_whisker"], "tom_mid": ["Tom2_whisker"],
                 "tom_floor": ["Tom3_whisker"], "crash": ["Crash_left_whisker"], "crash2": ["Crash_right_whisker"], "ride": ["Ride_whisker"],
                 "rim": ["Snare_rim"]}
    else:
        kit = Kit("DRS Kit", "DRS Kit", ["rock", "jazz", "funk", "groove", "indie"], "CC BY 4.0", "DrumGizmo team / DRS Drums (Jes Eiler)",
                  "DRSKit by the DrumGizmo team, recorded with DRS Drums. https://drumgizmo.org")
        lanes = {"kick": ["Kdrum_without_contact"], "snare": ["Snare"], "rim": ["Snare_rim"], "hihat_closed": ["Hihat_closed"],
                 "hihat_open": ["Hihat_open"], "hihat_pedal": ["Hihat_foot"], "tom_high": ["Tom1"], "tom_mid": ["Tom2"], "tom_floor": ["Tom3"],
                 "crash": ["Crash_left_shank"], "crash2": ["Crash_right_shank"], "ride": ["Ride_tip"], "ride_bell": ["Ride_tip_bell"]}
    for lane, insts in lanes.items():
        for inst in insts:
            for n, files in sorted(drumgizmo_hits(base, inst).items()):
                a, sr = mix(files, mics)
                kit.add(lane, f"{inst}_{n:03d}", a, sr)
    kit.write(out)


def build_muldjord(src, out):
    base = os.path.join(fetch("muldjord", src), "DrumGizmo/MuldjordKit/Samples")
    mics = {"KdrumL": (0.8, -0.05), "KdrumR": (0.8, 0.05), "Snare_top": (1.0, 0.0), "Snare_bottom": (0.4, 0.0),
            "Hihat": (0.45, -0.45), "RideL": (0.3, -0.5), "RideR": (0.35, 0.5), "Tom1": (0.8, -0.3), "Tom2": (0.8, -0.05),
            "Tom3": (0.8, 0.25), "Tom4": (0.85, 0.5), "OHL": (0.75, -0.85), "OHR": (0.75, 0.85), "AmbL": (0.4, -1.0),
            "AmbR": (0.4, 1.0), "Trigger": (0.0, 0.0)}
    kit = Kit("Muldjord Metal Kit", "Muldjord Metal Kit", ["metal", "rock"], "CC BY 4.0", "DrumGizmo team / Lars Muldjord",
              "MuldjordKit (Tama Superstar) by Lars Muldjord and the DrumGizmo team. https://drumgizmo.org")
    lanes = {"kick": ["KdrumL", "KdrumR"], "snare": ["Snare"], "hihat_closed": ["HihatClosed"], "hihat_open": ["HihatOpen"],
             "tom_high": ["Tom1"], "tom_mid": ["Tom2"], "tom_floor": ["Tom4"], "crash": ["CrashL"], "crash2": ["CrashR"],
             "ride": ["RideR"], "ride_bell": ["RideRBell"], "china": ["China"]}
    for lane, insts in lanes.items():
        for inst in insts:
            for n, files in sorted(drumgizmo_hits(base, inst).items()):
                a, sr = mix(files, mics)
                kit.add(lane, f"{inst}_{n:03d}", a, sr)
    kit.write(out)


def build_rusty(src, out):
    base = os.path.join(fetch("rusty", src), "Samples")
    mics = {"cl": (1.0, 0.0), "kick": (1.0, 0.0), "sn": (0.15, 0.0), "top": (1.0, 0.0), "btm": (0.45, 0.0), "oh": (0.8, 0.0, True)}
    kit = Kit("Big Rusty Drums", "Big Rusty Drums", ["rock", "indie", "groove"], "CC0", "Karoryfer Samples",
              "Big Rusty Drums by Karoryfer Samples (public domain). https://karoryfer.com")
    lanes = {"kick": ("kick_24", "kick"), "snare": ("snare_14", "center"), "rim": ("snare_14", "sidestick"),
             "hihat_closed": ("hihat_14", "cl"), "hihat_open": ("hihat_14", "open"), "hihat_pedal": ("hihat_14", "chik"),
             "tom_high": ("tom_14", "center"), "tom_mid": ("tom_15", "center"), "tom_floor": ("tom_18", "center"),
             "crash": ("crash_17", "cr"), "crash2": ("crash_sizzle_17", "cr"), "ride": ("ride_22", "rd"),
             "ride_bell": ("ride_22", "bl"), "china": ("china_18", "cn")}
    pans = {"hihat_closed": -0.4, "hihat_open": -0.4, "hihat_pedal": -0.4, "tom_high": -0.2, "tom_mid": 0.1, "tom_floor": 0.45,
            "crash": -0.6, "crash2": 0.6, "ride": 0.5, "ride_bell": 0.5, "china": -0.7}
    for lane, (inst, art) in lanes.items():
        d = os.path.join(base, inst, art)
        if not os.path.isdir(d):
            continue
        by_name = defaultdict(dict)
        for mic in os.listdir(d):
            md = os.path.join(d, mic)
            if os.path.isdir(md):
                for f in os.listdir(md):
                    if f.endswith((".flac", ".wav")):
                        by_name[os.path.splitext(f)[0]][mic] = os.path.join(md, f)
        lm = dict(mics)
        lm["cl"] = (1.0, pans.get(lane, 0.0))
        for name, files in sorted(by_name.items()):
            a, sr = mix(files, lm)
            vl = re.search(r"vl(\d+)", name); rr = re.search(r"rr(\d+)", name)
            nm = f"{lane}_vl{int(vl.group(1)) if vl else 1}_rr{int(rr.group(1)) if rr else 1}"
            kit.add(lane, nm, a, sr)
    kit.write(out)


def single_files(folder, pattern):
    if not os.path.isdir(folder):
        return []
    rx = re.compile(pattern, re.I)
    return sorted(os.path.join(folder, f) for f in os.listdir(folder) if rx.search(f))


def add_files(kit, lane, paths, rename=None):
    for p in paths:
        a, sr = read(p)
        name = os.path.splitext(os.path.basename(p))[0]
        kit.add(lane, rename(name) if rename else name, a, sr)


def build_vsco(src, out):
    perc = os.path.join(fetch("vsco", src), "Percussion")
    kit = Kit("VSCO Orchestral Percussion", "VSCO Orchestral Percussion", ["orchestral"], "CC0", "Versilian Studios",
              "VSCO 2 Community Edition by Versilian Studios (public domain). https://versilian-studios.com")
    add_files(kit, "kick", single_files(perc, r"^BDrumNewhit_v\d+_rr\d+"))
    add_files(kit, "snare", single_files(perc, r"^Snare2-HitSN_v\d+_rr\d+"))
    add_files(kit, "hihat_closed", single_files(perc, r"^Snare2-taps_v\d+_rr\d+"))     # soft snare taps on the 'hat' lane
    add_files(kit, "crash", single_files(perc, r"^cymbal-crash1_(pp|mp|mf|ff)_rr\d"))
    add_files(kit, "ride", single_files(perc, r"^susCymb1-hit_(pp|mp|f|fff)_rr\d"))
    add_files(kit, "ride_bell", single_files(perc, r"^susCymb1-hit-bell_"))
    add_files(kit, "china", single_files(perc, r"^gongHit_"))
    add_files(kit, "splash", single_files(perc, r"^Triangle3-Hit_v\d_rr\d"))
    add_files(kit, "tambourine", single_files(perc, r"^Tamb1-Hit_v\d_rr\d"))
    add_files(kit, "shaker", single_files(perc, r"^Tamb1-Shake_v\d_rr\d"))
    add_files(kit, "perc", single_files(perc, r"^Claves1_Hit_v\d_rr\d"))
    add_files(kit, "cowbell", single_files(perc, r"^Cowbell1_Hit_v\d_rr\d"))
    add_files(kit, "rim", single_files(perc, r"^Claves1_Hit_v\d_rr\d"))
    # timpani: sort the drums by pitch -> high / mid / floor lanes
    timp = os.path.join(perc, "Timpani")
    drums = defaultdict(list)
    for p in single_files(timp, r"^Timpani\d_Hit_v\d_rr\d"):
        drums[re.match(r"Timpani(\d)", os.path.basename(p)).group(1)].append(p)
    if drums:
        order = sorted(drums, key=lambda k: centroid(*read(drums[k][0])))
        pick = {"tom_floor": order[0], "tom_mid": order[len(order) // 2], "tom_high": order[-1]}
        for lane, k in pick.items():
            add_files(kit, lane, drums[k])
    kit.gain_db = {"crash": -3.0, "china": -2.0}
    kit.write(out)


def build_vcsl(src, out):
    root = fetch("vcsl", src)
    idi = os.path.join(root, "Idiophones/Struck Idiophones")
    mem = os.path.join(root, "Membranophones/Struck Membranophones")
    kit = Kit("VCSL Cajon", "VCSL Cajon & Hand Percussion", ["cajon"], "CC0", "Versilian Studios",
              "Versilian Community Sample Library (public domain). https://github.com/sgossner/VCSL")
    caj = single_files(os.path.join(idi, "Cajon"), r"\.wav$")
    groups = defaultdict(list)
    for p in caj:
        m = re.search(r"hit(\d)", os.path.basename(p), re.I)
        if m:
            groups[m.group(1)].append(p)
    if groups:
        # least mid-band energy = bass tone, most = slap, in between = soft finger tip
        order = sorted(groups, key=lambda k: np.mean([mid_share(*read(p)) for p in groups[k]]))
        dyn = {"pp": 1, "p": 2, "mp": 3, "f": 4, "ff": 5, "fff": 6}
        def ren(n):
            d = re.search(r"_(pp|p|mp|f|ff|fff)_", n + "_"); rr = re.search(r"rr(\d)", n)
            return f"cajon_v{dyn.get(d.group(1), 3) if d else 3}_rr{rr.group(1) if rr else 1}_{n}"
        add_files(kit, "kick", groups[order[0]], ren)
        add_files(kit, "snare", groups[order[-1]], ren)
        if len(order) > 2:
            add_files(kit, "rim", groups[order[len(order) // 2]], ren)
    add_files(kit, "clap", single_files(os.path.join(idi, "Claps"), r"^Clap_rr\d"))
    add_files(kit, "shaker", single_files(os.path.join(idi, "Shaker, Small"), r"ShakerDouble_(Down|Up)_rr\d"))
    add_files(kit, "hihat_closed", single_files(os.path.join(idi, "Shaker, Small"), r"ShakerHighFaster_(Down|Up)_rr\d"))
    add_files(kit, "hihat_open", single_files(os.path.join(idi, "Shaker, Small"), r"Shaker_Slap_rr\d"))
    add_files(kit, "tambourine", single_files(os.path.join(idi, "Tambourine 1"), r"Tamb1_Hit_v\d"))
    add_files(kit, "crash", single_files(os.path.join(idi, "Tambourine 1"), r"Tamb1_(Shake|Hit_v2)"))
    for lane, folder in (("tom_high", "Bongos"), ("tom_mid", "Conga")):
        files = single_files(os.path.join(mem, folder), r"\.wav$")
        add_files(kit, lane, files[:MAX_HITS])
    kit.write(out)


BUILDERS = {
    "drs": lambda s, o: build_drs(s, o, False),
    "drs-brushes": lambda s, o: build_drs(s, o, True),
    "muldjord": build_muldjord,
    "rusty": build_rusty,
    "vsco": build_vsco,
    "vcsl": build_vcsl,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="sources")
    ap.add_argument("--out", default="TheHammer-Kits")
    ap.add_argument("--only", nargs="*", default=list(BUILDERS))
    a = ap.parse_args()
    os.makedirs(a.src, exist_ok=True); os.makedirs(a.out, exist_ok=True)
    for k in a.only:
        print(f"== building {k}")
        BUILDERS[k](a.src, a.out)
    with open(os.path.join(a.out, "CREDITS.txt"), "w") as f:
        f.write(__doc__)
        f.write("\nInstall: copy these folders into  Documents/APT Industrial/The Hammer/Kits\n")


if __name__ == "__main__":
    main()
