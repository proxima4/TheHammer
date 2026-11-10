// ============================================================================
//  HammerRender - offline renderer + self-test for The Hammer.
//    HammerRender --selftest            run engine checks (used by CI)
//    HammerRender --render <outdir>     render a demo WAV + MIDI per style,
//                                       playing along to a synthetic band
//    HammerRender --brain <file.json>   load + verify a trained brain
// ============================================================================
#include "../Source/Engine/HammerEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstring>
#include <juce_events/juce_events.h>
#include <cstdio>

using namespace hammer;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else std::printf ("  ok:   %s\n", msg); } while (0)

// ---------------------------------------------------------------------------
// A little synthetic band to play along to: bass, chord stabs, picking noise.
// Verse (quiet) for 8 bars, then chorus (loud, busier) for 8 bars.
// ---------------------------------------------------------------------------
struct Band
{
    double sr; double bpm; int bpb = 4;
    float bassPhase = 0, bassEnv = 0, bassFreq = 55, chordEnv = 0, pickEnv = 0;
    float chordPh[3] {};
    uint32_t rnd = 99;
    Biquad pickHp;
    Band (double s, double b) : sr (s), bpm (b) { pickHp.setHighpass (sr, 5000); }

    void render (float* L, float* R, int n, double ppq0)
    {
        const double pps = bpm / 60.0 / sr;
        for (int i = 0; i < n; ++i)
        {
            const double ppq = ppq0 + i * pps;
            const double prev = ppq - pps;
            const int bar = (int) std::floor (ppq / bpb);
            const bool chorus = (bar / 8) % 2 == 1;
            auto crossed = [&] (double pos) { // pos within bar (beats)
                const double a = std::fmod (prev, (double) bpb), b = std::fmod (ppq, (double) bpb);
                return (a < pos && b >= pos) || (a > b && (pos <= b || pos > a));
            };
            // bass: 1, 2&, 3, (4a in chorus)
            static const double bassHits[] = { 0.0, 1.5, 2.0, 3.75 };
            for (int k = 0; k < (chorus ? 4 : 3); ++k)
                if (crossed (bassHits[k])) { bassEnv = chorus ? 1.f : 0.6f; bassFreq = (bar % 4 == 2) ? 73.4f : 55.f; }
            // chord stabs on 2 and 4 (verse) / every off-beat (chorus)
            if (crossed (1.0) || crossed (3.0) || (chorus && (crossed (0.5) || crossed (2.5)))) chordEnv = chorus ? 0.8f : 0.45f;
            // picking: 8ths (verse) / 16ths (chorus)
            const double sub = chorus ? 0.25 : 0.5;
            if (std::floor (ppq / sub) != std::floor (prev / sub)) pickEnv = chorus ? 0.35f : 0.18f;

            bassPhase += bassFreq / (float) sr; if (bassPhase > 1) bassPhase -= 1;
            const float bass = std::sin (6.2831853f * bassPhase) * bassEnv * 0.5f;
            bassEnv *= 0.99985f;
            float chord = 0;
            const float cf[3] = { 220.f, 277.2f, 329.6f };
            for (int c = 0; c < 3; ++c) { chordPh[c] += cf[c] / (float) sr; if (chordPh[c] > 1) chordPh[c] -= 1; chord += (chordPh[c] < 0.5f ? 1.f : -1.f); }
            chord *= chordEnv * 0.08f; chordEnv *= 0.9993f;
            rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
            float pick = pickHp.process ((float) (int32_t) rnd / 2147483648.f) * pickEnv; pickEnv *= 0.997f;
            L[i] = bass + chord + pick * 0.6f;
            R[i] = bass + chord * 0.8f + pick;
        }
    }
};

static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (! os) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (! w) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct RenderResult { int hits = 0; float peak = 0; bool nan = false; int lanesUsed = 0; };

static RenderResult renderStyle (int style, double bpm, int barsTotal, bool withBand, float brain, const juce::File* wavOut,
                                 const juce::File* midOut, int kitOverride = -1, float follow = 0.6f, const juce::File* sampleKit = nullptr)
{
    const double sr = 44100.0; const int block = 256;
    auto engPtr = std::make_unique<HammerEngine> (HammerEngine::Mode::Tool); // heap: Windows stacks are only 1 MB
    auto& eng = *engPtr;
    eng.prepare (sr, block);
    if (sampleKit != nullptr)
    {
        eng.kits.requestLoad (0, *sampleKit);
        for (int i = 0; i < 600 && eng.kits.get (0) == nullptr; ++i) juce::Thread::sleep (50);
        std::printf ("  kit: %s\n", eng.kits.statusOf (0).toRawUTF8());
        kitOverride = KitSample1;
    }
    Band band (sr, bpm);
    EngineSettings s;
    s.style = style; s.intensity = 0.5f; s.aggression = (style == Metalcore || style == DeathMetal) ? 0.7f : 0.4f;
    s.fills = 0.55f; s.solos = 0.15f; s.follow = follow; s.improvise = 0.45f; s.brain = brain; s.humanize = 0.6f;
    SoundSettings snd;
    const int total = (int) (barsTotal * 4 * 60.0 / bpm * sr);
    juce::AudioBuffer<float> out (2, total), in (2, block);
    out.clear();
    RenderResult r;
    bool laneSeen[NumLanes] {};
    double ppq = 0.0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        in.clear();
        if (withBand) band.render (in.getWritePointer (0), in.getWritePointer (1), n, ppq);
        float* oL = out.getWritePointer (0, pos);
        float* oR = out.getWritePointer (1, pos);
        // mix the band quietly into the demo so you can hear the drummer following it
        if (withBand) for (int i = 0; i < n; ++i) { oL[i] += in.getSample (0, i) * 0.5f; oR[i] += in.getSample (1, i) * 0.5f; }
        TransportInfo t; t.playing = true; t.valid = true; t.ppq = ppq; t.bpm = bpm;
        HammerEngine::IO io;
        io.analysisL = in.getReadPointer (0); io.analysisR = in.getReadPointer (1);
        io.outL = oL; io.outR = oR; io.numSamples = n;
        eng.process (t, s, snd, kitOverride >= 0 ? kitOverride : KitStudio, kitOverride < 0, 1, io);
        DisplaySnapshot d; eng.copyDisplay (d);
        for (int i = 0; i < d.numHits; ++i) laneSeen[d.hits[(size_t) i].lane] = true;
        ppq += n * bpm / 60.0 / sr;
    }
    r.hits = eng.recorder.numEvents();
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (c, i);
            if (! std::isfinite (v)) r.nan = true;
            r.peak = std::max (r.peak, std::abs (v));
        }
    for (bool b : laneSeen) r.lanesUsed += b ? 1 : 0;
    if (r.peak > 0.98f) out.applyGain (0.98f / r.peak);
    if (wavOut) writeWav (*wavOut, out, sr);
    if (midOut) eng.exportMidi (*midOut, barsTotal);
    return r;
}

static int selfTest()
{
    std::printf ("== Style library\n");
    for (int s = 0; s < NumStyles; ++s)
    {
        const auto& st = StyleLibrary::get (s);
        bool okLen = true;
        for (int g = 0; g < st.numGrooves; ++g)
            for (const auto& lp : st.grooves[g].lanes)
                if (lp.steps && (int) std::strlen (lp.steps) != st.grooves[g].steps)
                {
                    std::printf ("    %s / %s lane %s: %d chars, expected %d\n", st.name, st.grooves[g].name, laneName (lp.lane),
                                 (int) std::strlen (lp.steps), st.grooves[g].steps);
                    okLen = false;
                }
        CHECK (okLen, (juce::String (st.name) + " groove strings have correct length").toRawUTF8());
    }

    std::printf ("== Neural brain\n");
    {
        auto nePtr = std::make_unique<NeuralEngine>(); auto& ne = *nePtr; ne.loadDefault();
        std::printf ("    %s\n", ne.status().c_str());
        if (ne.hasModel())
        {
            CHECK (ne.lastSelfTestError >= 0.f && ne.lastSelfTestError < 1e-3f, "C++ GRU matches PyTorch reference outputs");
        }
        else std::printf ("    (no trained model embedded - Playbook improviser in use)\n");
    }

    std::printf ("== Engine renders every style (with and without listening)\n");
    for (int s = 0; s < NumStyles; ++s)
    {
        for (float brain : { 0.f, 0.5f, 1.f })
        {
            const auto r = renderStyle (s, s == DeathMetal ? 200.0 : (s == Jazz ? 150.0 : 110.0), 6, true, brain, nullptr, nullptr);
            const auto msg = juce::String (styleName (s)) + " brain=" + juce::String (brain, 1) + ": " + juce::String (r.hits) + " hits, peak "
                             + juce::String (r.peak, 2) + ", lanes " + juce::String (r.lanesUsed);
            CHECK (r.hits > 20 && ! r.nan && r.peak > 0.01f, msg.toRawUTF8());
        }
    }
    std::printf ("== Drum machines + modeled kits\n");
    for (int k = 0; k <= KitOrchestralModel; ++k)
    {
        const auto r = renderStyle (Rock, 120.0, 3, false, 0.f, nullptr, nullptr, k);
        CHECK (r.hits > 10 && ! r.nan && r.peak > 0.01f, (juce::String (kitName (k)) + " makes sound").toRawUTF8());
    }

    std::printf ("== Stop behaviour (APT standard: Stop silences everything)\n");
    {
        const double sr = 44100.0; const int block = 256;
        auto engPtr = std::make_unique<HammerEngine> (HammerEngine::Mode::Drummer);
        auto& eng = *engPtr;
        eng.prepare (sr, block);
        EngineSettings s; s.style = Rock; s.intensity = 0.9f; s.aggression = 0.9f;
        SoundSettings snd; snd.room = 1.f; snd.roomSize = 1.f;
        juce::AudioBuffer<float> out (2, block);
        double ppq = 0.0;
        auto run = [&] (bool playing, int blocks, juce::MidiBuffer* midi = nullptr) {
            float peak = 0.f;
            for (int i = 0; i < blocks; ++i)
            {
                out.clear();
                TransportInfo t; t.playing = playing; t.valid = true; t.ppq = ppq; t.bpm = 120.0;
                HammerEngine::IO io; io.outL = out.getWritePointer (0); io.outR = out.getWritePointer (1); io.numSamples = block;
                io.midiIn = (i == 0) ? midi : nullptr;
                eng.process (t, s, snd, KitStudio, false, 1, io);
                if (playing) ppq += block * 120.0 / 60.0 / sr;
                peak = std::max (peak, out.getMagnitude (0, block));
            }
            return peak;
        };
        const float playingPeak = run (true, 600);          // ~3.5 s of crash-heavy rock with a huge room
        run (false, 9);                                       // Stop pressed: ~50 ms
        const float after = run (false, 20);                  // the next ~120 ms
        CHECK (playingPeak > 0.05f, "drums were playing");
        CHECK (after < 1.0e-4f, (juce::String ("silent within 50 ms of Stop (residual ") + juce::String (after, 6) + ")").toRawUTF8());
        juce::MidiBuffer live; live.addEvent (juce::MidiMessage::noteOn (1, 49, (juce::uint8) 120), 0);
        run (false, 1, &live);
        const float tail = run (false, 50);                   // 300 ms later the crash still rings
        CHECK (tail > 1.0e-3f, "live hit while stopped still rings out (demo mode)");
    }

    std::printf ("== Shared link round trip\n");
    {
        SharedLink w, rd;
        const bool ok = w.open (8) && rd.open (8);
        CHECK (ok, "open link channel 8");
        LinkEvent e {}; e.ppq = 12.5; e.band[0] = 0.9f; e.kind = 0;
        w.publishEvent (e);
        float m[3] = { 0.1f, 0.2f, 0.3f };
        w.publishTransport (12.5, 128.0, true, 4, m);
        LinkEvent got[8];
        const int n = rd.poll (got, 8);
        CHECK (n == 1 && std::abs (got[0].ppq - 12.5) < 1e-9 && got[0].band[0] > 0.89f, "event arrives intact");
        CHECK (rd.writerAlive() && std::abs (rd.writerBpm() - 128.0) < 1e-9, "heartbeat + tempo visible");
    }

    std::printf ("== Sample kit loader (folder + SFZ)\n");
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hammer_kit_test");
        dir.deleteRecursively();
        dir.getChildFile ("kick").createDirectory();
        dir.getChildFile ("snare").createDirectory();
        juce::AudioBuffer<float> b (2, 4410);
        for (int v = 1; v <= 3; ++v)
            for (int rr = 1; rr <= 2; ++rr)
            {
                for (int i = 0; i < 4410; ++i) { const float x = std::sin (i * 0.02f) * std::exp (-i / 800.f) * 0.3f * v; b.setSample (0, i, x); b.setSample (1, i, x); }
                writeWav (dir.getChildFile ("kick").getChildFile ("kick_v" + juce::String (v) + "_rr" + juce::String (rr) + ".wav"), b, 44100.0);
                writeWav (dir.getChildFile ("snare").getChildFile ("sn_" + juce::String (v * 10 + rr) + ".wav"), b, 44100.0);
            }
        juce::String err;
        auto kit = SampleKitLoader::load (dir, 48000.0, err);
        CHECK (kit != nullptr, ("folder kit loads " + err).toRawUTF8());
        if (kit)
        {
            CHECK (kit->lanes[Kick].layers.size() == 3 && kit->lanes[Kick].layers[0].rr.size() == 2, "kick: 3 velocity layers x 2 round robins (from names)");
            CHECK (kit->lanes[Snare].present(), "snare: layers clustered by loudness");
            CHECK (kit->lanes[Kick].layers[0].loudness < kit->lanes[Kick].layers[2].loudness, "layers sorted quiet -> loud");
        }
        dir.getChildFile ("kit.sfz").replaceWithText (
            "// test\n#define $KICK 36\n<control> default_path=kick/\n<group> key=$KICK\n"
            "<region> sample=kick_v1_rr1.wav hivel=60\n<region> sample=kick_v3_rr1.wav lovel=61 volume=-3\n");
        auto sfz = SampleKitLoader::load (dir.getChildFile ("kit.sfz"), 44100.0, err);
        CHECK (sfz != nullptr && sfz->lanes[Kick].layers.size() == 2, "SFZ: #define, default_path, two velocity layers on key 36");
        dir.deleteRecursively();
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL GOOD" : "PROBLEMS FOUND", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

static int renderDemos (const juce::File& outDir)
{
    outDir.createDirectory();
    struct Demo { int style; double bpm; };
    const Demo demos[] = {
        { Jazz, 160 }, { Rock, 120 }, { Metalcore, 140 }, { DeathMetal, 200 }, { Indie, 128 }, { FusionJazz, 105 },
        { Funk, 100 }, { Groove, 88 }, { Blues, 92 }, { Orchestral, 96 }, { Minimal, 124 }, { Cajon, 104 } };
    for (const auto& d : demos)
    {
        const auto base = juce::String (styleName (d.style)).replaceCharacter (' ', '_');
        auto wav = outDir.getChildFile (base + ".wav");
        auto mid = outDir.getChildFile (base + ".mid");
        const auto r = renderStyle (d.style, d.bpm, 16, true, 0.35f, &wav, &mid);
        std::printf ("%-12s %5.0f bpm  %4d hits  peak %.2f  -> %s\n", styleName (d.style), d.bpm, r.hits, r.peak, wav.getFileName().toRawUTF8());
    }
    // machines showcase
    const int machines[] = { Kit808, Kit909, KitCR78, KitLinn };
    for (int k : machines)
    {
        auto wav = outDir.getChildFile (juce::String (kitName (k)).upToFirstOccurrenceOf (" ", false, false) + "_Groove.wav");
        renderStyle (Groove, 92, 8, false, 0.2f, &wav, nullptr, k);
        std::printf ("%-12s machine    -> %s\n", kitName (k), wav.getFileName().toRawUTF8());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Playbook -> training data. Writes one JSON per style with sessions of bars
// in the neural brain's 9-lane, 16th-note vocabulary (offsets carry swing and
// humanised timing). Styles the Groove MIDI Dataset doesn't cover get more bars.
// ---------------------------------------------------------------------------
static int exportPlaybook (const juce::File& dir, float scale)
{
    dir.createDirectory();
    Rng rng (2026);
    for (int style = 0; style < NumStyles; ++style)
    {
        const auto& st = StyleLibrary::get (style);
        const bool covered = style == Jazz || style == Rock || style == Funk || style == Groove || style == Blues || style == Indie;
        const int totalBars = (int) ((covered ? 400 : 2000) * scale);
        juce::String json;
        json << "{\"hammer_style\":" << style << ",\"name\":\"" << st.name << "\",\"gmd_mix\":[";
        for (int i = 0; i < kNumGmdStyles; ++i) json << (i ? "," : "") << st.gmdMix[i];
        json << "],\"sessions\":[";
        int written = 0, sessions = 0, events = 0;
        while (written < totalBars)
        {
            auto cPtr = std::make_unique<Conductor>(); // fresh drummer per session (no neural: pure playbook)
            auto& c = *cPtr;
            auto lsPtr = std::make_unique<ListeningState>();
            auto& ls = *lsPtr;          // nothing heard: the training script derives listening features itself
            EngineSettings es;
            es.style = style; es.brain = 0.f; es.follow = 0.f;
            es.intensity = rng.range (0.08f, 1.f);
            es.aggression = rng.range (0.1f, 1.f);
            es.fills = rng.range (0.15f, 0.95f);
            es.solos = rng.chance (0.3f) ? rng.range (0.2f, 0.7f) : 0.f;
            es.improvise = rng.range (0.15f, 0.95f);
            es.humanize = rng.range (0.35f, 0.9f);
            es.phraseBars = rng.chance (0.5f) ? 4 : 8;
            const Section secs[] = { Section::Auto, Section::Auto, Section::Verse, Section::Chorus, Section::Breakdown, Section::Intro, Section::Bridge };
            es.section = secs[rng.below (7)];
            const float bpm = juce::jlimit (60.f, 250.f, rng.range (std::max (55.f, st.minBpm), std::min (260.f, st.maxBpm)));
            const int bars = std::min (32, totalBars - written);
            json << (sessions ? "," : "") << "{\"bpm\":" << juce::String (bpm, 1) << ",\"bars\":[";
            const float sixteenthMs = 60000.f / bpm / 4.f;
            for (int b = 0; b < bars; ++b)
            {
                if (b % 8 == 0 && b > 0) es.intensity = juce::jlimit (0.05f, 1.f, es.intensity + rng.range (-0.25f, 0.25f));
                BarRequest req;
                req.barIndex = b; req.beatsPerBar = 4; req.bpm = bpm; req.settings = &es; req.listening = &ls;
                req.nowPpq = b * 4.0; req.ensembleKit = style == Orchestral;
                Bar bar;
                c.generateBar (req, bar);
                json << (b ? "," : "") << "{\"role\":" << bar.role << ",\"e\":[";
                bool first = true;
                for (int i = 0; i < bar.numHits; ++i)
                {
                    const Hit& h = bar.hits[(size_t) i];
                    const int cls = NeuralEngine::laneToNeural (h.lane, style);
                    if (cls < 0) continue;
                    const float pos16 = ((float) h.tick + h.offsetTicks) / 3.f + h.offsetMs / sixteenthMs;
                    int step = (int) std::lround (pos16);
                    float off = pos16 - (float) step;
                    if (step < 0 || step >= 16) continue;
                    off = juce::jlimit (-0.5f, 0.5f, off);
                    json << (first ? "" : ",") << "[" << step << "," << cls << "," << juce::String (h.velocity, 3) << "," << juce::String (off, 3) << "]";
                    first = false; ++events;
                }
                json << "]}";
            }
            json << "]}";
            written += bars; ++sessions;
        }
        json << "]}";
        auto f = dir.getChildFile ("playbook_" + juce::String (style) + "_" + juce::String (st.name).removeCharacters (" ") + ".json");
        f.replaceWithText (json);
        std::printf ("%-12s %5d bars in %3d sessions, %6d hits -> %s\n", st.name, written, sessions, events, f.getFileName().toRawUTF8());
    }
    return 0;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); // show progress even if something crashes (CI logs)
    juce::ScopedJuceInitialiser_GUI init; // message manager for JUCE helpers
    if (argc >= 2 && std::strcmp (argv[1], "--selftest") == 0) return selfTest();
    if (argc >= 3 && std::strcmp (argv[1], "--render") == 0) return renderDemos (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 3 && std::strcmp (argv[1], "--brain") == 0)
    {
        auto nePtr = std::make_unique<NeuralEngine>(); auto& ne = *nePtr;
        const bool ok = ne.loadFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).getFullPathName().toStdString());
        std::printf ("%s\nself-test max error: %g\n", ne.status().c_str(), ne.lastSelfTestError);
        return ok && ne.lastSelfTestError >= 0.f && ne.lastSelfTestError < 1e-3f ? 0 : 1;
    }
    if (argc >= 3 && std::strcmp (argv[1], "--export-playbook") == 0)
        return exportPlaybook (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), argc >= 4 ? (float) std::atof (argv[3]) : 1.f);
    if (argc >= 6 && std::strcmp (argv[1], "--render-kit") == 0)
    {
        // --render-kit <out.wav> <kit folder|sfz> <style index> <bpm>
        const auto wav = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        const auto kit = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        const auto mid = wav.withFileExtension ("mid");
        const auto r = renderStyle (std::atoi (argv[4]), std::atof (argv[5]), 16, true, 0.35f, &wav, &mid, -1, 0.6f, &kit);
        std::printf ("%s: %d hits, peak %.2f\n", wav.getFileName().toRawUTF8(), r.hits, r.peak);
        return r.hits > 0 ? 0 : 1;
    }
    std::printf ("usage: HammerRender --selftest | --render <outdir> | --render-kit <out.wav> <kit> <style> <bpm> | --export-playbook <dir> [scale] | --brain <model.json>\n");
    return 2;
}
