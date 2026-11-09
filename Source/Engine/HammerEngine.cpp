#include "HammerEngine.h"
#include <cmath>

namespace hammer
{
HammerEngine::HammerEngine (Mode m) : mode (m)
{
    conductor.setNeural (&neural);
    neural.loadDefault();
    for (auto& mt : meters) mt.store (0.f);
}

void HammerEngine::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (32, block);
    analyzer.prepare (sr);
    drums.prepare (sr, maxBlock);
    kits.setSampleRate (sr);
    delayLine.setSize (2, (int) (sr * 0.6) + maxBlock);
    delayLine.clear();
    delayWrite = 0;
    reset();
}

void HammerEngine::reset()
{
    for (auto& b : bars) b.index = INT64_MIN;
    expectedPpq = -1e9;
    wasPlaying = false;
    numNoteOffs = 0;
    lastReactivePpq = -1e9;
}

HammerEngine::BarSlot* HammerEngine::slotFor (int64_t idx)
{
    return &bars[(size_t) (((idx % 3) + 3) % 3)];
}

int HammerEngine::latencySamples (const EngineSettings& s) const
{
    return mode == Mode::Mixer ? (int) std::lround (s.lookaheadMs * 0.001 * sr) : 0;
}

juce::String HammerEngine::kitDisplayName() const
{
    const int k = effKit.load();
    if (k >= KitSample1)
        if (auto* d = const_cast<KitSlots&> (kits).get (k - KitSample1)) return d->name;
    return kitName (k);
}

bool HammerEngine::exportMidi (const juce::File& f, int nBars) const
{
    return recorder.exportLastBars (f, nBars, lastBpb, lastBpm);
}

void HammerEngine::copyDisplay (DisplaySnapshot& out) const
{
    const juce::SpinLock::ScopedLockType sl (displayLock);
    out.bar = display.bar; out.ticksPerBar = display.ticksPerBar; out.role = display.role;
    out.numHits = display.numHits;
    for (int i = 0; i < display.numHits; ++i) out.hits[(size_t) i] = display.hits[(size_t) i];
}

void HammerEngine::publishDisplay (const BarSlot& b)
{
    const juce::SpinLock::ScopedTryLockType tl (displayLock);
    if (! tl.isLocked()) return;
    display.bar = b.index; display.ticksPerBar = b.bar.ticksPerBar; display.role = b.bar.role;
    display.numHits = b.bar.numHits;
    for (int i = 0; i < b.bar.numHits; ++i) display.hits[(size_t) i] = b.bar.hits[(size_t) i];
}

// ---------------------------------------------------------------------------
void HammerEngine::chooseKit (const EngineSettings& s, int kitParam, bool autoKit)
{
    const StyleDef& st = StyleLibrary::get (s.style);
    int k = kitParam;
    if (autoKit)
    {
        const int slot = kits.slotWithTag (st.kitTag);
        k = slot >= 0 ? KitSample1 + slot : st.defaultKit;
    }
    SampleKitData* data = nullptr;
    if (k >= KitSample1)
    {
        data = kits.get (k - KitSample1);
        if (data == nullptr) k = st.defaultKit >= KitSample1 ? KitStudio : st.defaultKit;
    }
    voicing = s.style;
    if (s.style == Jazz && lastEffIntensity < 0.32f) voicing = 100; // brushes on quiet jazz
    effKit = k;
    drums.setKit (k, voicing, data);
}

// ---------------------------------------------------------------------------
void HammerEngine::listenToAudio (const float* L, const float* R, int n, double ppqStart, double pps, bool playing)
{
    const int ne = analyzer.process (L, R, n, events, OnsetAnalyzer::kMaxEventsPerBlock);
    for (int i = 0; i < 3; ++i) meters[i] = analyzer.bandLevel (i);
    if (! playing) return;
    for (int i = 0; i < ne; ++i)
        listening.addOnset (ppqStart + events[i].sampleOffset * pps, events[i].band, events[i].strength);
    listening.addEnergy (ppqStart, analyzer.bandEnergyDb (0), analyzer.bandEnergyDb (1), analyzer.bandEnergyDb (2));
}

void HammerEngine::pollLink (int channel)
{
    if (! link.isOpen() || link.channel() != channel) link.open (channel);
    const int ne = link.poll (linkEvents, 512);
    for (int i = 0; i < ne; ++i)
    {
        const auto& e = linkEvents[i];
        if (e.kind == 0) listening.addOnset (e.ppq, e.band, e.strength);
        else listening.addEnergy (e.ppq, e.band[0], e.band[1], e.band[2]);
    }
    const bool alive = link.writerAlive();
    linkAlive = alive;
    float m[3]; link.readMeters (m);
    for (int i = 0; i < 3; ++i) meters[i] = alive ? m[i] : meters[i].load() * 0.9f;
}

// ---------------------------------------------------------------------------
void HammerEngine::handleMidiIn (juce::MidiBuffer& in, const EngineSettings& s, int n)
{
    for (const auto meta : in)
    {
        const auto msg = meta.getMessage();
        const int pos = juce::jlimit (0, std::max (0, n - 1), meta.samplePosition);
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            switch (note)
            {
                case 24: fillNow = true; break;
                case 25: conductor.setSoloHeld (true); break;
                case 26: conductor.triggerCrash(); break;
                case 27: conductor.setStopHeld (true); break;
                case 28: conductor.setSectionOverride ((int) Section::Intro); break;
                case 29: conductor.setSectionOverride ((int) Section::Verse); break;
                case 30: conductor.setSectionOverride ((int) Section::Chorus); break;
                case 31: conductor.setSectionOverride ((int) Section::Bridge); break;
                case 32: conductor.setSectionOverride ((int) Section::Breakdown); break;
                case 33: conductor.setSectionOverride ((int) Section::Outro); break;
                case 34: conductor.toggleHalfTime(); break;
                case 35: conductor.setSectionOverride (-1); break;
                default:
                {
                    const int lane = gmNoteToLane (note);
                    if (lane >= 0) drums.trigger (lane, msg.getFloatVelocity(), pos); // play the kit from the piano roll
                    break;
                }
            }
        }
        else if (msg.isNoteOff())
        {
            if (msg.getNoteNumber() == 25) conductor.setSoloHeld (false);
            if (msg.getNoteNumber() == 27) conductor.setStopHeld (false);
        }
    }
    (void) s;
}

// ---------------------------------------------------------------------------
HammerEngine::BarSlot* HammerEngine::ensureBar (int64_t idx, const TransportInfo& t, const EngineSettings& s, double nowPpq)
{
    BarSlot* slot = slotFor (idx);
    if (slot->index == idx) return slot;
    BarRequest req;
    req.barIndex = idx;
    req.beatsPerBar = lastBpb;
    req.bpm = (float) t.bpm;
    req.settings = &s;
    req.listening = &listening;
    req.nowPpq = nowPpq;
    const int k = effKit.load();
    req.machineKit = k == Kit808 || k == Kit909 || k == KitCR78 || k == KitLinn;
    req.ensembleKit = k == KitOrchestralModel;
    if (k >= KitSample1)
        if (auto* d = kits.get (k - KitSample1)) req.ensembleKit = d->hasTag ("orchestral");
    conductor.generateBar (req, slot->bar);
    slot->index = idx;
    lastEffIntensity = conductor.status.intensity.load();
    return slot;
}

void HammerEngine::schedule (BarSlot& b, double p0, double p1, double pps, int bpb, const EngineSettings& s, IO& io)
{
    const double barStart = (double) b.index * bpb;
    const double msToPpq = lastBpm / 60000.0;
    for (int i = 0; i < b.bar.numHits; ++i)
    {
        const Hit& h = b.bar.hits[(size_t) i];
        const double hp = barStart + ((double) h.tick + h.offsetTicks) / kTicksPerBeat + h.offsetMs * msToPpq;
        if (hp < p0 || hp >= p1) continue;
        const int off = juce::jlimit (0, io.numSamples - 1, (int) ((hp - p0) / pps));
        if (s.drummerOn)
        {
            drums.trigger (h.lane, h.velocity, off);
            recorder.add (hp, h.lane, h.velocity);
            if (io.midiOut != nullptr && s.midiOut)
            {
                const int note = laneToGmNote (h.lane);
                io.midiOut->addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, (int) (h.velocity * 127.f))), off);
                if (numNoteOffs < 128) noteOffs[numNoteOffs++] = { note, off + (int) (sr * 0.05) };
            }
        }
    }
}

void HammerEngine::reactiveFollow (const EngineSettings& s, double renderFrom, double hostTo, int bpb)
{
    // Only possible with lookahead: we hear the band before we have to play,
    // so we can land exactly on their hits.
    OnsetRecord rec[64];
    const int n = listening.onsetsInRange (std::max (lastReactivePpq + 1e-6, renderFrom), hostTo, rec, 64);
    for (int i = n - 1; i >= 0; --i) // oldest first
    {
        const auto& o = rec[i];
        lastReactivePpq = std::max (lastReactivePpq, o.ppq);
        if (o.ppq < renderFrom) continue;
        const int64_t bi = (int64_t) std::floor (o.ppq / bpb);
        BarSlot* slot = slotFor (bi);
        if (slot->index != bi) continue; // not generated yet: the playbook will predict it next bar
        const double inBar = (o.ppq - (double) bi * bpb) * kTicksPerBeat;
        const int tick = juce::jlimit (0, slot->bar.ticksPerBar - 1, (int) std::floor (inBar));
        const float frac = (float) (inBar - tick);
        auto near = [&] (int lane, int tol) {
            for (int k = 0; k < slot->bar.numHits; ++k)
                if (slot->bar.hits[(size_t) k].lane == lane && std::abs ((int) slot->bar.hits[(size_t) k].tick - tick) <= tol) return true;
            return false;
        };
        auto add = [&] (int lane, float vel) {
            Hit h; h.lane = (uint8_t) lane; h.tick = (uint8_t) tick; h.offsetTicks = frac; h.velocity = clamp01 (vel);
            slot->bar.add (h);
        };
        const float lo = o.band[0], mi = o.band[1], hi = o.band[2];
        if (lo > 0.55f && ! near (Kick, 1)) add (Kick, 0.6f + 0.4f * lo);
        if (std::min (lo, std::min (mi, hi)) > 0.5f && s.intensity > 0.3f && ! near (Crash, 3) && ! near (China, 3)) add (Crash, 0.75f + 0.25f * hi);
        if (mi > 0.7f && lo > 0.4f && ! near (Snare, 1)) add (Snare, 0.7f + 0.3f * mi);
    }
}

// ---------------------------------------------------------------------------
void HammerEngine::process (const TransportInfo& tIn, const EngineSettings& s, const SoundSettings& snd,
                            int kitParam, bool autoKit, int linkChannel, IO& io)
{
    const int n = io.numSamples;
    TransportInfo t = tIn;
    if ((! t.playing || ! t.valid) && internalPlaying)
    {
        t.playing = true; t.valid = true; t.ppq = internalPpq; t.bpm = s.internalBpm; t.numerator = 4; t.denominator = 4;
    }
    const int bpb = juce::jlimit (1, kMaxBeatsPerBar, t.numerator * 4 / std::max (1, t.denominator));
    if (bpb != lastBpb) { for (auto& b : bars) b.index = INT64_MIN; lastBpb = bpb; }
    listening.setMeter (bpb);
    const double bpm = std::max (20.0, t.bpm);
    lastBpm = bpm;
    const double pps = bpm / 60.0 / sr;
    const double look = mode == Mode::Mixer ? s.lookaheadMs * 0.001 * bpm / 60.0 : 0.0;

    // pending MIDI note-offs
    if (io.midiOut != nullptr)
        for (int i = 0; i < numNoteOffs; ++i)
        {
            if (noteOffs[i].samplesLeft < n)
            {
                io.midiOut->addEvent (juce::MidiMessage::noteOff (10, noteOffs[i].note), std::max (0, noteOffs[i].samplesLeft));
                noteOffs[i--] = noteOffs[--numNoteOffs];
            }
            else noteOffs[i].samplesLeft -= n;
        }

    // ---- ears
    analyzer.setSensitivity (s.earsSensitivity);
    if (mode != Mode::Drummer && io.analysisL != nullptr)
        listenToAudio (io.analysisL, io.analysisR, n, t.ppq, pps, t.playing && t.valid);
    if (mode == Mode::Drummer)
        pollLink (linkChannel);

    // ---- kit, sound, live MIDI
    chooseKit (s, kitParam, autoKit);
    drums.settings = snd;
    if (io.midiIn != nullptr) handleMidiIn (*io.midiIn, s, n);
    if (s.style != lastStyle) { lastStyle = s.style; }

    // ---- transport stop / project load: everything goes silent (APT standard).
    //      Playing the kit live while FL is stopped still rings out naturally.
    const bool stoppedNow = wasPlaying && (! t.playing || ! t.valid);
    if (stoppedNow || silenceNow.exchange (false))
    {
        drums.fadeOutAll();
        if (io.midiOut != nullptr)
        {
            for (int i = 0; i < numNoteOffs; ++i) io.midiOut->addEvent (juce::MidiMessage::noteOff (10, noteOffs[i].note), 0);
            io.midiOut->addEvent (juce::MidiMessage::allNotesOff (10), 0);
        }
        numNoteOffs = 0;
    }

    // ---- the drummer plays
    if (! t.playing || ! t.valid)
    {
        if (wasPlaying) { for (auto& b : bars) b.index = INT64_MIN; expectedPpq = -1e9; }
        wasPlaying = false;
    }
    else
    {
        const double p0 = t.ppq - look;
        const double p1 = p0 + n * pps;
        if (! wasPlaying || std::abs (p0 - expectedPpq) > 0.05)
        {
            for (auto& b : bars) b.index = INT64_MIN;
            if (wasPlaying && p0 < expectedPpq - bpb) listening.reset(); // jumped back: old memories no longer line up
            lastReactivePpq = p0;
            numNoteOffs = 0;
        }
        expectedPpq = p1;
        wasPlaying = true;

        const int64_t c = (int64_t) std::floor (p0 / bpb + 1e-9);
        BarSlot* cur = ensureBar (c, t, s, t.ppq);
        if (p1 > (double) (c + 1) * bpb - 0.5) ensureBar (c + 1, t, s, t.ppq);

        if (fillNow.exchange (false))
        {
            BarRequest req;
            req.barIndex = c; req.beatsPerBar = bpb; req.bpm = (float) bpm; req.settings = &s; req.listening = &listening; req.nowPpq = t.ppq;
            const int k = effKit.load();
            req.machineKit = k == Kit808 || k == Kit909 || k == KitCR78 || k == KitLinn;
            const int curTick = (int) ((p0 - (double) c * bpb) * kTicksPerBeat);
            conductor.injectFill (cur->bar, curTick + 1, req);
            publishDisplay (*cur);
        }
        if (look > 0.0 && s.follow >= 0.75f) reactiveFollow (s, p0, t.ppq + n * pps, bpb);

        for (auto& b : bars)
            if (b.index == c - 1 || b.index == c || b.index == c + 1)
                schedule (b, p0, p1, pps, bpb, s, io);

        playTick = (float) ((p0 - (double) c * bpb) * kTicksPerBeat);
        if (display.bar != c) publishDisplay (*cur);
        if (internalPlaying && ! tIn.playing) internalPpq += n * pps;
    }

    // ---- sound
    drums.render (io.outL, io.outR, n, io.auxL, io.auxR);
    drums.decayMeters (std::pow (0.5f, (float) n / (float) (sr * 0.08)));
}

void HammerEngine::processPassThrough (const float* inL, const float* inR, float* outL, float* outR, int n, int d)
{
    const int size = delayLine.getNumSamples();
    d = juce::jlimit (0, size - 1, d);
    float* dl = delayLine.getWritePointer (0);
    float* dr = delayLine.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        dl[delayWrite] = inL[i];
        dr[delayWrite] = inR != nullptr ? inR[i] : inL[i];
        int rp = delayWrite - d; if (rp < 0) rp += size;
        outL[i] += dl[rp];
        outR[i] += dr[rp];
        if (++delayWrite >= size) delayWrite = 0;
    }
}
} // namespace hammer
