#!/usr/bin/env python3
"""
The Hammer - neural brain trainer
=================================
Trains the small GRU drummer that runs inside The Hammer on the
Groove MIDI Dataset (Google Magenta, CC BY 4.0): 13.6 hours of professional
drummers playing to a click on a Roland TD-11, 1,150 MIDI files.

  python train_brain.py --out ../model/hammer_brain.json \
         --playbook playbook_data --my-midi my_midi                   # full training
  python train_brain.py --synthetic --epochs 2 --out /tmp/test.json  # smoke test

Three kinds of training data:
  1. Groove MIDI Dataset  - real drummers: feel, timing, dynamics (rock/funk/jazz/blues/soul/hip-hop...)
  2. Playbook bars        - exported by `HammerRender --export-playbook`: teaches the
                            styles GMD lacks (metalcore, death metal, orchestral, cajon, minimal...)
  3. my_midi/<style>/*.mid - YOUR grooves (e-kit recordings, programmed parts,
                            transcriptions you made). Weighted x4 so they really count.

The exported JSON is read by Source/Brain/NeuralEngine.cpp. It contains
reference input/output vectors so the C++ engine can prove it computes
exactly what PyTorch computed (HammerRender --brain <file>).

Model input per 16th-note step (67 values):
  [0:27]  what the drummer played on the previous step (9 lanes x hit/vel/offset)
  [27:45] style mix (18 GMD styles)
  [45:57] Hammer style (12, one-hot)
  [57]    intensity (bar density)
  [58:61] what the Ears hear at this step: low / mid / high accent
  [61:66] position in bar (sin/cos 16ths, sin/cos beat, downbeat flag)
  [66]    fill flag
Output (27): 9 hit logits, 9 velocity pre-sigmoids, 9 micro-timing pre-tanh.
"""
import argparse, csv, io, json, math, os, random, sys, time, urllib.request, zipfile
import numpy as np

GMD_URL = "https://storage.googleapis.com/magentadata/datasets/groove/groove-v1.0.0-midionly.zip"
STYLES = ["rock", "funk", "jazz", "latin", "hiphop", "soul", "pop", "afrobeat", "reggae",
          "blues", "country", "punk", "neworleans", "gospel", "dance", "middleeastern", "afrocuban", "highlife"]
LANES = ["kick", "snare", "hh_closed", "hh_open", "tom_low", "tom_mid", "tom_high", "crash", "ride"]
# Roland TD-11 -> 9 classes (same reduction as Magenta GrooVAE)
PITCH_TO_CLASS = {36: 0, 35: 0, 38: 1, 37: 1, 40: 1, 42: 2, 22: 2, 44: 2, 46: 3, 26: 3,
                  43: 4, 58: 4, 47: 5, 45: 5, 50: 6, 48: 6, 49: 7, 52: 7, 55: 7, 57: 7, 51: 8, 53: 8, 59: 8}
HAMMER_STYLES = ["Jazz", "Rock", "Metalcore", "Death Metal", "Indie", "Fusion Jazz",
                 "Funk", "Groove", "Blues", "Orchestral", "Minimal", "Cajon"]
# GMD primary style -> closest Hammer style (others: none)
GMD_TO_HAMMER = {"rock": 1, "funk": 6, "jazz": 0, "blues": 8, "soul": 7, "hiphop": 7, "pop": 4}
N_STYLE = len(STYLES) + len(HAMMER_STYLES)
N_IN, N_OUT, STEPS_PER_BAR = 27 + N_STYLE + 1 + 3 + 5 + 1, 27, 16


# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------
class Performance:
    """svec: 30-value style vector (18 GMD mix + 12 Hammer one-hot). fill: bool or per-step array."""
    __slots__ = ("hits", "vel", "off", "svec", "fill", "split", "weight")

    def __init__(self, hits, vel, off, svec, fill, split, weight=1):
        self.hits, self.vel, self.off, self.svec, self.fill, self.split, self.weight = hits, vel, off, svec, fill, split, weight


def style_vector(gmd_index=-1, hammer_index=-1, gmd_mix=None):
    v = np.zeros(N_STYLE, np.float32)
    if gmd_mix is not None:
        m = np.asarray(gmd_mix, np.float32)
        if m.sum() > 0:
            v[:len(STYLES)] = m / m.sum()
    elif gmd_index >= 0:
        v[gmd_index] = 1.0
    if hammer_index >= 0:
        v[len(STYLES) + hammer_index] = 1.0
    return v


def quantise(notes, bpm, total_seconds):
    """notes: list of (time_s, pitch, velocity) -> (T,9) hit / velocity / offset arrays on a 16th grid."""
    step = 60.0 / bpm / 4.0
    T = max(STEPS_PER_BAR, int(math.ceil(total_seconds / step)) + 1)
    T = int(math.ceil(T / STEPS_PER_BAR) * STEPS_PER_BAR)
    hits = np.zeros((T, 9), np.float32)
    vel = np.zeros((T, 9), np.float32)
    off = np.zeros((T, 9), np.float32)
    for t, pitch, v in notes:
        c = PITCH_TO_CLASS.get(pitch)
        if c is None:
            continue
        s = int(round(t / step))
        if s < 0 or s >= T:
            continue
        o = (t - s * step) / step  # -0.5 .. 0.5
        vv = v / 127.0
        if vv > vel[s, c]:
            hits[s, c] = 1.0
            vel[s, c] = vv
            off[s, c] = max(-0.5, min(0.5, o))
    return hits, vel, off


def load_gmd(zip_path):
    import pretty_midi
    perfs = []
    with zipfile.ZipFile(zip_path) as z:
        info_name = [n for n in z.namelist() if n.endswith("info.csv")][0]
        root = info_name[: -len("info.csv")]
        rows = list(csv.DictReader(io.TextIOWrapper(z.open(info_name), encoding="utf-8")))
        for i, r in enumerate(rows):
            if r.get("time_signature", "4-4") != "4-4":
                continue
            try:
                pm = pretty_midi.PrettyMIDI(io.BytesIO(z.read(root + r["midi_filename"])))
            except Exception as e:  # noqa
                print("  skip", r["midi_filename"], e)
                continue
            notes = [(n.start, n.pitch, n.velocity) for inst in pm.instruments for n in inst.notes]
            if not notes:
                continue
            bpm = float(r["bpm"])
            h, v, o = quantise(notes, bpm, pm.get_end_time())
            primary = r["style"].split("/")[0]
            gi = STYLES.index(primary) if primary in STYLES else -1
            svec = style_vector(gi, GMD_TO_HAMMER.get(primary, -1))
            perfs.append(Performance(h, v, o, svec, r["beat_type"] == "fill", r["split"]))
            if (i + 1) % 200 == 0:
                print(f"  parsed {i + 1}/{len(rows)}")
    return perfs


def synthetic_dataset(n=240, seed=1):
    """Fake 'drummers' for smoke tests: rock/funk/jazz-ish patterns with noise."""
    rng = np.random.default_rng(seed)
    perfs = []
    for k in range(n):
        style = [0, 1, 2, 11][k % 4]
        bars = 8
        T = bars * 16
        h = np.zeros((T, 9), np.float32)
        for t in range(T):
            p = t % 16
            if style == 2:  # swing ride
                if p in (0, 4, 7, 8, 12, 15): h[t, 8] = 1
                if p in (4, 12): h[t, 2] = 1
            else:
                if p % 2 == 0: h[t, 2] = 1
                if p in (0, 8) or (style == 1 and p in (3, 10)): h[t, 0] = 1
                if p in (4, 12): h[t, 1] = 1
                if style == 11 and p % 4 == 2: h[t, 0] = 1
        fill = k % 5 == 0
        if fill:
            for t in range(T - 8, T):
                h[t, :] = 0; h[t, 1 + (t % 3) * 2 if (t % 3) else 1] = 1
        h *= (rng.random(h.shape) > 0.05)
        v = h * (0.5 + 0.5 * rng.random(h.shape)).astype(np.float32)
        o = h * (rng.normal(0, 0.08, h.shape)).clip(-0.5, 0.5).astype(np.float32)
        split = "validation" if k % 10 == 0 else "train"
        svec = style_vector(style, {0: 1, 1: 6, 2: 0, 11: 2}[style])
        perfs.append(Performance(h.astype(np.float32), v.astype(np.float32), o.astype(np.float32), svec, fill, split))
    return perfs


def load_playbook(folder, rng):
    """Bars exported by `HammerRender --export-playbook <folder>`."""
    perfs, mixes = [], {}
    for f in sorted(os.listdir(folder)):
        if not f.endswith(".json"):
            continue
        with open(os.path.join(folder, f)) as fh:
            doc = json.load(fh)
        hs = doc["hammer_style"]
        mixes[hs] = doc["gmd_mix"]
        svec = style_vector(hammer_index=hs, gmd_mix=doc["gmd_mix"])
        for si, sess in enumerate(doc["sessions"]):
            T = len(sess["bars"]) * STEPS_PER_BAR
            h = np.zeros((T, 9), np.float32); v = np.zeros((T, 9), np.float32); o = np.zeros((T, 9), np.float32)
            fill = np.zeros(T, np.float32)
            for b, bar in enumerate(sess["bars"]):
                base = b * STEPS_PER_BAR
                if bar["role"] == 1:      # fill: flag the second half of the bar
                    fill[base + 8: base + 16] = 1
                elif bar["role"] == 2:    # solo
                    fill[base: base + 16] = 1
                for step, cls, vel, off in bar["e"]:
                    t = base + int(step)
                    if vel > v[t, cls]:
                        h[t, cls] = 1; v[t, cls] = vel; o[t, cls] = off
            split = "validation" if rng.random() < 0.06 else "train"
            perfs.append(Performance(h, v, o, svec, fill, split))
    print(f"  playbook: {len(perfs)} sessions from {folder}")
    return perfs, mixes


# GM drum note -> Hammer lane name, then style-aware lane -> neural class (mirrors NeuralEngine::laneToNeural)
GM_TO_LANE = {35: "kick", 36: "kick", 38: "snare", 40: "snare", 37: "rim", 39: "clap", 42: "hhc", 44: "hhp", 46: "hho",
              41: "floor", 43: "floor", 45: "mid", 47: "mid", 48: "high", 50: "high", 49: "crash", 57: "crash", 52: "china",
              55: "splash", 51: "ride", 59: "ride", 53: "bell", 54: "tamb", 69: "shaker", 70: "shaker", 82: "shaker",
              56: "cowbell", 75: "perc", 76: "perc", 77: "perc"}
DEFAULT_MAP = {"kick": 0, "snare": 1, "rim": 1, "clap": 1, "hhc": 2, "hhp": 2, "hho": 3, "floor": 4, "mid": 5, "high": 6,
               "crash": 7, "china": 7, "splash": 7, "ride": 8, "bell": 8}
STYLE_MAPS = {
    11: {"kick": 0, "snare": 1, "rim": 2, "shaker": 3, "hho": 3, "hhc": 3, "floor": 4, "mid": 5, "high": 6,
         "tamb": 7, "crash": 7, "china": 7, "splash": 7, "ride": 8, "bell": 8},                      # cajon
    9: {"kick": 0, "snare": 1, "hhc": 2, "floor": 4, "mid": 5, "high": 6, "crash": 7, "china": 7, "ride": 8, "bell": 8, "hho": 8},  # orchestral
    10: {"kick": 0, "clap": 1, "snare": 1, "hhc": 2, "shaker": 2, "hho": 3, "perc": 4, "rim": 5, "crash": 7, "ride": 8, "bell": 8},  # minimal
}


def hammer_style_from_name(name):
    key = name.lower().replace(" ", "").replace("_", "").replace("-", "")
    for i, n in enumerate(HAMMER_STYLES):
        if n.lower().replace(" ", "") == key:
            return i
    aliases = {"cajón": 11, "metal": 2, "death": 3, "fusion": 5, "orchestra": 9, "rnb": 7, "hiphop": 7, "soul": 7}
    return aliases.get(key, -1)


def load_user_midi(folder, mixes, rng, weight=4):
    """my_midi/<style>/*.mid - your own grooves, labelled by folder name."""
    import pretty_midi
    perfs = []
    if not folder or not os.path.isdir(folder):
        return perfs
    for sub in sorted(os.listdir(folder)):
        d = os.path.join(folder, sub)
        if not os.path.isdir(d):
            continue
        hs = hammer_style_from_name(sub)
        if hs < 0:
            print(f"  my_midi: folder '{sub}' doesn't match a style name - skipped")
            continue
        cmap = STYLE_MAPS.get(hs, DEFAULT_MAP)
        n_files = 0
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith((".mid", ".midi")):
                continue
            try:
                pm = pretty_midi.PrettyMIDI(os.path.join(d, f))
            except Exception as e:  # noqa
                print("  skip", f, e); continue
            ts = pm.time_signature_changes
            if ts and (ts[0].numerator, ts[0].denominator) not in ((4, 4), (2, 4)):
                print(f"  skip {f}: {ts[0].numerator}/{ts[0].denominator} (only 4/4 for now)"); continue
            _, tempi = pm.get_tempo_changes()
            bpm = float(tempi[0]) if len(tempi) else 120.0
            notes = []
            for inst in pm.instruments:
                for n in inst.notes:
                    lane = GM_TO_LANE.get(n.pitch)
                    if lane is not None and lane in cmap:
                        notes.append((n.start, n.pitch, n.velocity, cmap[lane]))
            if not notes:
                continue
            step = 60.0 / bpm / 4.0
            T = int(math.ceil(pm.get_end_time() / step / STEPS_PER_BAR) + 1) * STEPS_PER_BAR
            h = np.zeros((T, 9), np.float32); v = np.zeros((T, 9), np.float32); o = np.zeros((T, 9), np.float32)
            for t0, _, vel, c in notes:
                s_ = int(round(t0 / step))
                if 0 <= s_ < T and vel / 127 > v[s_, c]:
                    h[s_, c] = 1; v[s_, c] = vel / 127; o[s_, c] = max(-0.5, min(0.5, (t0 - s_ * step) / step))
            svec = style_vector(hammer_index=hs, gmd_mix=mixes.get(hs))
            perfs.append(Performance(h, v, o, svec, "fill" in f.lower(), "train", weight))
            n_files += 1
        print(f"  my_midi/{sub}: {n_files} files -> {HAMMER_STYLES[hs]}")
    return perfs


def make_inputs(p, start, length, rng, train=True):
    """Builds (length, 55) inputs and (length, 27) targets for a window of a performance."""
    T = p.hits.shape[0]
    idx = np.arange(start, start + length)
    valid = idx < T
    idx_c = np.minimum(idx, T - 1)
    hits = p.hits[idx_c] * valid[:, None]
    vel = p.vel[idx_c] * valid[:, None]
    off = p.off[idx_c] * valid[:, None]
    if train:  # velocity augmentation
        vel = np.clip(vel * rng.uniform(0.88, 1.12), 0, 1) * hits

    prev = np.zeros((length, 27), np.float32)
    prev_h = np.concatenate([[p.hits[start - 1] if start > 0 else np.zeros(9)], hits[:-1]])
    prev_v = np.concatenate([[p.vel[start - 1] if start > 0 else np.zeros(9)], vel[:-1]])
    prev_o = np.concatenate([[p.off[start - 1] if start > 0 else np.zeros(9)], off[:-1]])
    prev[:, 0:9], prev[:, 9:18], prev[:, 18:27] = prev_h, prev_v, prev_o

    sv = p.svec.copy()
    if train and rng.random() < 0.1:
        sv[:len(STYLES)] = 0.0       # sometimes hide the GMD part so the Hammer one-hot carries the style
    style = np.repeat(sv[None, :], length, axis=0)

    # intensity = bar density (hits per bar normalised)
    dens = np.zeros(length, np.float32)
    for b0 in range(0, length, STEPS_PER_BAR):
        seg = hits[b0:b0 + STEPS_PER_BAR]
        dens[b0:b0 + STEPS_PER_BAR] = min(1.0, seg.sum() / 40.0)
    if train:
        dens = np.clip(dens + rng.normal(0, 0.04, length), 0, 1)

    # "what the Ears hear": the band usually accents with the drums.
    lam = 0.0 if (train and rng.random() < 0.25) else (rng.uniform(0.2, 1.0) if train else 0.7)
    low = vel[:, 0]
    mid = np.max(vel[:, [1, 4, 5, 6]], axis=1)
    high = np.max(vel[:, [2, 3, 7, 8]], axis=1) * 0.8
    listen = np.stack([low, mid, high], 1) * lam
    if train and lam > 0:
        listen *= (rng.random(listen.shape) > 0.2)                       # missed accents
        fp = (rng.random(listen.shape) < 0.05) * rng.uniform(0.3, 0.8, listen.shape)
        listen = np.clip(np.maximum(listen, fp * lam) + rng.normal(0, 0.03, listen.shape), 0, 1)

    pos = (start + np.arange(length)) % 16
    beat = pos % 4
    posf = np.stack([np.sin(2 * np.pi * pos / 16), np.cos(2 * np.pi * pos / 16),
                     np.sin(2 * np.pi * beat / 4), np.cos(2 * np.pi * beat / 4), (pos == 0).astype(np.float32)], 1)
    if isinstance(p.fill, np.ndarray):
        fill = (p.fill[idx_c] * valid)[:, None].astype(np.float32)
    else:
        fill = np.full((length, 1), 1.0 if p.fill else 0.0, np.float32)

    x = np.concatenate([prev, style, dens[:, None], listen, posf, fill], 1).astype(np.float32)
    y = np.concatenate([hits, vel, off], 1).astype(np.float32)
    m = valid.astype(np.float32)
    return x, y, m


def windows(perfs, split, length, hop):
    out = []
    for pi, p in enumerate(perfs):
        if (split == "train") != (p.split == "train"):
            continue
        T = p.hits.shape[0]
        for s in range(0, max(1, T - STEPS_PER_BAR), hop):
            for _ in range(p.weight if split == "train" else 1):
                out.append((pi, s))
    return out


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------
def build_model(hidden, layers):
    import torch.nn as nn

    class HammerBrain(nn.Module):
        def __init__(self):
            super().__init__()
            self.gru = nn.GRU(N_IN, hidden, num_layers=layers, batch_first=True, dropout=0.1 if layers > 1 else 0.0)
            self.out = nn.Linear(hidden, N_OUT)

        def forward(self, x, h=None):
            y, h = self.gru(x, h)
            return self.out(y), h

    return HammerBrain()


def loss_fn(pred, y, m, pos_weight):
    import torch
    import torch.nn.functional as F
    hit_t, vel_t, off_t = y[..., 0:9], y[..., 9:18], y[..., 18:27]
    mask = m[..., None]
    bce = F.binary_cross_entropy_with_logits(pred[..., 0:9], hit_t, pos_weight=pos_weight, reduction="none")
    bce = (bce * mask).sum() / (mask.sum() * 9 + 1e-6)
    hm = hit_t * mask
    v = ((torch.sigmoid(pred[..., 9:18]) - vel_t) ** 2 * hm).sum() / (hm.sum() + 1e-6)
    o = ((0.5 * torch.tanh(pred[..., 18:27]) - off_t) ** 2 * hm).sum() / (hm.sum() + 1e-6)
    return bce + 0.5 * v + 2.0 * o, (bce.item(), v.item(), o.item())


def batches(perfs, wins, length, bs, rng, train):
    order = rng.permutation(len(wins)) if train else np.arange(len(wins))
    for i in range(0, len(order), bs):
        xs, ys, ms = [], [], []
        for j in order[i:i + bs]:
            pi, s = wins[j]
            x, y, m = make_inputs(perfs[pi], s, length, rng, train)
            xs.append(x); ys.append(y); ms.append(m)
        yield np.stack(xs), np.stack(ys), np.stack(ms)


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------
def r6(a):
    return [float(f"{v:.6g}") for v in np.asarray(a, np.float64).ravel()]


def export(model_state, hidden, layers, path, test_in, test_out, meta):
    doc = {
        "format": "hammer-brain-v2",
        "input_size": N_IN, "hidden_size": hidden, "output_size": N_OUT, "num_layers": layers,
        "lanes": LANES, "styles": STYLES, "hammer_styles": HAMMER_STYLES,
        "layers": [],
        "meta": meta,
    }
    for k in range(layers):
        doc["layers"].append({
            "w_ih": r6(model_state[f"gru.weight_ih_l{k}"]),
            "w_hh": r6(model_state[f"gru.weight_hh_l{k}"]),
            "b_ih": r6(model_state[f"gru.bias_ih_l{k}"]),
            "b_hh": r6(model_state[f"gru.bias_hh_l{k}"]),
        })
    doc["out_w"] = r6(model_state["out.weight"])
    doc["out_b"] = r6(model_state["out.bias"])
    doc["test"] = {"inputs": [r6(v) for v in test_in], "outputs": [r6(v) for v in test_out]}
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        json.dump(doc, f, separators=(",", ":"))
    print(f"exported {path} ({os.path.getsize(path) / 1e6:.1f} MB)")


def numpy_gru_reference(state, hidden, layers, xs):
    """Independent numpy implementation (same maths as the C++ engine) - a sanity check of the export."""
    def sig(x): return 1 / (1 + np.exp(-x))
    hs = [np.zeros(hidden) for _ in range(layers)]
    outs = []
    for x in xs:
        inp = np.asarray(x, np.float64)
        for k in range(layers):
            wih, whh = state[f"gru.weight_ih_l{k}"], state[f"gru.weight_hh_l{k}"]
            bih, bhh = state[f"gru.bias_ih_l{k}"], state[f"gru.bias_hh_l{k}"]
            gx, gh = wih @ inp + bih, whh @ hs[k] + bhh
            H = hidden
            r = sig(gx[:H] + gh[:H]); z = sig(gx[H:2 * H] + gh[H:2 * H])
            n = np.tanh(gx[2 * H:] + r * gh[2 * H:])
            hs[k] = (1 - z) * n + z * hs[k]
            inp = hs[k]
        outs.append(state["out.weight"] @ inp + state["out.bias"])
    return outs


preview_mixes = {}


def sample_preview(model, out_dir, steps=64):
    """Writes a few generated grooves as MIDI so you can hear what it learned."""
    import torch
    try:
        import pretty_midi
    except ImportError:
        return
    os.makedirs(out_dir, exist_ok=True)
    pitch = [36, 38, 42, 46, 43, 47, 50, 49, 51]
    for hs in range(len(HAMMER_STYLES)):
        sname = HAMMER_STYLES[hs].replace(" ", "")
        sv = style_vector(hammer_index=hs, gmd_mix=preview_mixes.get(hs))
        h = None
        prev = np.zeros(27, np.float32)
        pm = pretty_midi.PrettyMIDI(initial_tempo=110)
        inst = pretty_midi.Instrument(program=0, is_drum=True)
        step = 60 / 110 / 4
        for s in range(steps):
            pos = s % 16
            x = np.concatenate([prev, sv, [0.5], [0, 0, 0],
                                [math.sin(2 * math.pi * pos / 16), math.cos(2 * math.pi * pos / 16),
                                 math.sin(2 * math.pi * (pos % 4) / 4), math.cos(2 * math.pi * (pos % 4) / 4), float(pos == 0)],
                                [1.0 if s >= steps - 8 else 0.0]]).astype(np.float32)
            with torch.no_grad():
                y, h = model(torch.from_numpy(x)[None, None], h)
            y = y[0, 0].numpy()
            prob = 1 / (1 + np.exp(-y[:9])); vel = 1 / (1 + np.exp(-y[9:18])); off = 0.5 * np.tanh(y[18:27])
            hit = (np.random.random(9) < prob).astype(np.float32)
            prev = np.concatenate([hit, hit * vel, hit * off]).astype(np.float32)
            for l in range(9):
                if hit[l]:
                    t = (s + off[l]) * step
                    inst.notes.append(pretty_midi.Note(int(20 + 107 * vel[l]), pitch[l], max(0, t), max(0, t) + 0.05))
        pm.instruments.append(inst)
        pm.write(os.path.join(out_dir, f"preview_{sname}.mid"))


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "model", "hammer_brain.json"))
    ap.add_argument("--data", default=None, help="path to groove-v1.0.0-midionly.zip (downloaded if missing)")
    ap.add_argument("--synthetic", action="store_true", help="smoke test with fake data")
    ap.add_argument("--playbook", default=None, help="folder from `HammerRender --export-playbook`")
    ap.add_argument("--my-midi", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "my_midi"),
                    help="folder of <style>/*.mid with your own grooves")
    ap.add_argument("--no-gmd", action="store_true", help="skip the Groove MIDI Dataset (fine-tuning experiments)")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--layers", type=int, default=2)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--window", type=int, default=64)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--preview", default=None, help="folder for generated preview MIDI files")
    ap.add_argument("--max-minutes", type=float, default=300.0)
    args = ap.parse_args()

    import torch
    torch.manual_seed(args.seed); np.random.seed(args.seed); random.seed(args.seed)
    torch.set_num_threads(max(1, os.cpu_count() or 1))
    rng = np.random.default_rng(args.seed)

    sources = []
    if args.synthetic:
        perfs = synthetic_dataset(); sources.append("synthetic smoke-test data")
    elif args.no_gmd:
        perfs = []
    else:
        zp = args.data or os.path.join(os.path.dirname(os.path.abspath(__file__)), "groove-v1.0.0-midionly.zip")
        if not os.path.exists(zp):
            print("downloading Groove MIDI Dataset ...")
            urllib.request.urlretrieve(GMD_URL, zp)
        perfs = load_gmd(zp); sources.append("Groove MIDI Dataset v1.0.0 (Google Magenta, CC BY 4.0)")
    mixes = {}
    if args.playbook and os.path.isdir(args.playbook):
        pb, mixes = load_playbook(args.playbook, rng)
        perfs += pb; sources.append("The Hammer Playbook")
    preview_mixes.update(mixes)
    mine = load_user_midi(args.my_midi, mixes, rng)
    if mine:
        perfs += mine; sources.append(f"{len(mine)} of your MIDI files")
    if not perfs:
        sys.exit("no training data")
    source = " + ".join(sources)
    print(f"{len(perfs)} performances, {sum(p.hits.shape[0] for p in perfs) // 16} bars")

    tr = windows(perfs, "train", args.window, 16)
    va = windows(perfs, "validation", args.window, 32)
    print(f"{len(tr)} training windows, {len(va)} validation windows")

    hit_rate = np.mean(np.concatenate([p.hits for p in perfs if p.split == "train"]), 0)
    pos_weight = torch.tensor(np.clip((1 - hit_rate) / np.maximum(hit_rate, 1e-3), 1, 12) ** 0.5, dtype=torch.float32)

    model = build_model(args.hidden, args.layers)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=max(1, args.epochs))
    best, best_state = 1e9, None
    t0 = time.time()
    for ep in range(args.epochs):
        model.train()
        tl, n = 0.0, 0
        for x, y, m in batches(perfs, tr, args.window, args.batch, rng, True):
            x, y, m = map(torch.from_numpy, (x, y, m))
            pred, _ = model(x)
            loss, _ = loss_fn(pred, y, m, pos_weight)
            opt.zero_grad(); loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            tl += loss.item(); n += 1
        sched.step()
        model.eval()
        vl, vn, parts = 0.0, 0, np.zeros(3)
        with torch.no_grad():
            for x, y, m in batches(perfs, va, args.window, 256, np.random.default_rng(0), False):
                x, y, m = map(torch.from_numpy, (x, y, m))
                pred, _ = model(x)
                l, pr = loss_fn(pred, y, m, pos_weight)
                vl += l.item(); vn += 1; parts += pr
        vl /= max(1, vn); parts /= max(1, vn)
        print(f"epoch {ep + 1:3d}/{args.epochs}  train {tl / max(1, n):.4f}  val {vl:.4f}  (hit {parts[0]:.3f} vel {parts[1]:.4f} timing {parts[2]:.4f})  {time.time() - t0:.0f}s", flush=True)
        if vl < best:
            best = vl
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
        if (time.time() - t0) / 60 > args.max_minutes:
            print("time budget reached")
            break

    model.load_state_dict(best_state)
    model.eval()
    state = {k: v.numpy().astype(np.float64) for k, v in best_state.items()}
    # reference vectors: a validation window, fresh state, step by step
    pi, s = (va or tr)[0]
    x, _, _ = make_inputs(perfs[pi], s, 24, np.random.default_rng(1), False)
    with torch.no_grad():
        y, _ = model(torch.from_numpy(x)[None])
    test_out = y[0].numpy()
    ref = numpy_gru_reference(state, args.hidden, args.layers, x)
    err = max(float(np.max(np.abs(np.asarray(ref) - test_out))), 0.0)
    print(f"numpy reference vs PyTorch max error: {err:.2e}")
    assert err < 1e-3, "export maths mismatch"
    meta = {"trained_on": source, "val_loss": round(best, 4), "epochs": args.epochs, "hidden": args.hidden,
            "layers": args.layers, "date": time.strftime("%Y-%m-%d"),
            "credit": "Gillick, Roberts, Engel, Eck, Bamman - Learning to Groove with Inverse Sequence Transformations (ICML 2019)"}
    export(state, args.hidden, args.layers, args.out, x, test_out, meta)
    if args.preview:
        sample_preview(model, args.preview)
        print("preview MIDI written to", args.preview)


if __name__ == "__main__":
    main()
