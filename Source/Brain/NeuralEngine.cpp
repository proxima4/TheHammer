#include "NeuralEngine.h"
#include <juce_core/juce_core.h>
#include <cmath>

#if HAMMER_HAS_BRAIN
 #include "BinaryData.h"
#endif

namespace hammer
{
static inline float sigm (float x) { return 1.f / (1.f + std::exp (-x)); }

// ---------------------------------------------------------------------------
static bool readFloatArray (const juce::var& v, std::vector<float>& out, size_t expected)
{
    const auto* arr = v.getArray();
    if (arr == nullptr) return false;
    out.clear();
    out.reserve ((size_t) arr->size());
    for (const auto& e : *arr)
    {
        if (const auto* inner = e.getArray()) // allow nested [[...],[...]]
        {
            for (const auto& x : *inner) out.push_back ((float) (double) x);
        }
        else out.push_back ((float) (double) e);
    }
    return expected == 0 || out.size() == expected;
}

bool GruNetwork::loadFromJson (const std::string& json, std::string& error)
{
    valid = false;
    layers.clear();
    juce::var root;
    auto res = juce::JSON::parse (juce::String (json), root);
    if (res.failed()) { error = "JSON parse error: " + res.getErrorMessage().toStdString(); return false; }
    if (root.getProperty ("format", "").toString() != "hammer-brain-v2") { error = "not a hammer-brain-v2 file (retrain with the current trainer)"; return false; }
    const int inSize = (int) root.getProperty ("input_size", 0);
    hidden = (int) root.getProperty ("hidden_size", 0);
    const int outSize = (int) root.getProperty ("output_size", 0);
    if (inSize != kNeuralIn || outSize != kNeuralOut || hidden <= 0 || hidden > 1024)
    {
        error = "model shape mismatch (in " + std::to_string (inSize) + ", out " + std::to_string (outSize) + ")";
        return false;
    }
    const auto* ls = root.getProperty ("layers", juce::var()).getArray();
    if (ls == nullptr || ls->isEmpty()) { error = "no layers"; return false; }
    int prevIn = inSize;
    for (const auto& lv : *ls)
    {
        Layer L;
        L.in = prevIn; L.h = hidden;
        const size_t H3 = (size_t) (3 * hidden);
        if (! readFloatArray (lv.getProperty ("w_ih", {}), L.wih, H3 * (size_t) L.in)
            || ! readFloatArray (lv.getProperty ("w_hh", {}), L.whh, H3 * (size_t) hidden)
            || ! readFloatArray (lv.getProperty ("b_ih", {}), L.bih, H3)
            || ! readFloatArray (lv.getProperty ("b_hh", {}), L.bhh, H3))
        { error = "bad layer weights"; return false; }
        L.state.assign ((size_t) hidden, 0.f);
        L.gx.assign (H3, 0.f);
        L.gh.assign (H3, 0.f);
        layers.push_back (std::move (L));
        prevIn = hidden;
    }
    if (! readFloatArray (root.getProperty ("out_w", {}), outW, (size_t) (outSize * hidden))
        || ! readFloatArray (root.getProperty ("out_b", {}), outB, (size_t) outSize))
    { error = "bad output layer"; return false; }
    buf.assign ((size_t) hidden, 0.f);

    testIn.clear(); testOut.clear();
    const auto test = root.getProperty ("test", juce::var());
    if (const auto* ti = test.getProperty ("inputs", {}).getArray())
        if (const auto* to = test.getProperty ("outputs", {}).getArray())
            if (ti->size() == to->size())
                for (int i = 0; i < ti->size(); ++i)
                {
                    std::vector<float> a, b;
                    if (readFloatArray ((*ti)[i], a, (size_t) kNeuralIn) && readFloatArray ((*to)[i], b, (size_t) kNeuralOut))
                    { testIn.push_back (a); testOut.push_back (b); }
                }

    const auto meta = root.getProperty ("meta", juce::var());
    description = "GRU " + std::to_string (layers.size()) + "x" + std::to_string (hidden);
    if (meta.hasProperty ("trained_on"))
        description += ", trained on " + meta.getProperty ("trained_on", "").toString().toStdString();
    if (meta.hasProperty ("val_loss"))
        description += ", val loss " + juce::String ((double) meta.getProperty ("val_loss", 0.0), 3).toStdString();
    valid = true;
    return true;
}

void GruNetwork::resetState()
{
    for (auto& L : layers) std::fill (L.state.begin(), L.state.end(), 0.f);
}

void GruNetwork::decayState (float k)
{
    for (auto& L : layers) for (auto& s : L.state) s *= k;
}

void GruNetwork::step (const float* in, float* out)
{
    const float* x = in;
    for (auto& L : layers)
    {
        const int H = L.h, I = L.in;
        for (int r = 0; r < 3 * H; ++r)
        {
            const float* wi = &L.wih[(size_t) r * (size_t) I];
            float a = L.bih[(size_t) r];
            for (int c = 0; c < I; ++c) a += wi[c] * x[c];
            L.gx[(size_t) r] = a;
            const float* wh = &L.whh[(size_t) r * (size_t) H];
            float b = L.bhh[(size_t) r];
            for (int c = 0; c < H; ++c) b += wh[c] * L.state[(size_t) c];
            L.gh[(size_t) r] = b;
        }
        for (int j = 0; j < H; ++j)
        {
            const float rg = sigm (L.gx[(size_t) j] + L.gh[(size_t) j]);
            const float zg = sigm (L.gx[(size_t) (H + j)] + L.gh[(size_t) (H + j)]);
            const float ng = std::tanh (L.gx[(size_t) (2 * H + j)] + rg * L.gh[(size_t) (2 * H + j)]);
            L.state[(size_t) j] = (1.f - zg) * ng + zg * L.state[(size_t) j];
        }
        x = L.state.data();
    }
    for (int o = 0; o < kNeuralOut; ++o)
    {
        const float* w = &outW[(size_t) o * (size_t) hidden];
        float a = outB[(size_t) o];
        for (int c = 0; c < hidden; ++c) a += w[c] * x[c];
        out[o] = a;
    }
}

float GruNetwork::runSelfTest()
{
    if (! valid || testIn.empty()) return -1.f;
    resetState();
    float maxErr = 0.f, o[kNeuralOut];
    for (size_t i = 0; i < testIn.size(); ++i)
    {
        step (testIn[i].data(), o);
        for (int k = 0; k < kNeuralOut; ++k) maxErr = std::max (maxErr, std::abs (o[k] - testOut[i][(size_t) k]));
    }
    resetState();
    return maxErr;
}

// ===========================================================================
NeuralEngine::NeuralEngine()
{
    memory.clear (48);
    statusText = "Neural: no model - using Playbook improviser";
}

void NeuralEngine::loadDefault()
{
    // A user-trained model in Documents/APT Industrial/The Hammer/Brain wins over the built-in one.
    auto userFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile ("APT Industrial").getChildFile ("The Hammer")
                        .getChildFile ("Brain").getChildFile ("hammer_brain.json");
    if (userFile.existsAsFile() && loadFile (userFile.getFullPathName().toStdString()))
        return;
#if HAMMER_HAS_BRAIN
    std::string err;
    std::string json (BinaryData::hammer_brain_json, (size_t) BinaryData::hammer_brain_jsonSize);
    if (net.loadFromJson (json, err))
    {
        lastSelfTestError = net.runSelfTest();
        statusText = "Neural: " + net.description;
        return;
    }
    statusText = "Neural: built-in model failed (" + err + ")";
#endif
}

bool NeuralEngine::loadFile (const std::string& path)
{
    juce::File f (path);
    if (! f.existsAsFile()) return false;
    std::string err;
    if (! net.loadFromJson (f.loadFileAsString().toStdString(), err))
    {
        statusText = "Neural: " + f.getFileName().toStdString() + " failed (" + err + ")";
        return false;
    }
    lastSelfTestError = net.runSelfTest();
    statusText = "Neural: " + net.description + " [" + f.getFileName().toStdString() + "]";
    return true;
}

void NeuralEngine::reset()
{
    net.resetState();
    for (auto& p : prevStep) p = 0.f;
    haveMemory = false;
    improviser.reset();
}

void NeuralEngine::onStyleChange()
{
    net.decayState (0.4f);
    improviser.reset();
    haveMemory = false;
}

int NeuralEngine::neuralToLane (int nl, int styleId, float aggression)
{
    const bool metal = styleId == Metalcore || styleId == DeathMetal;
    switch (styleId)
    {
        case Cajon:
        {
            static const int m[kNeuralLanes] = { Kick, Snare, Rim, Shaker, TomMid, TomMid, TomHigh, Tambourine, Shaker };
            return m[nl];
        }
        case Orchestral:
        {
            static const int m[kNeuralLanes] = { Kick, Snare, Snare, Ride, TomFloor, TomMid, TomHigh, Crash, Ride };
            return m[nl];
        }
        case Minimal:
        {
            static const int m[kNeuralLanes] = { Kick, Clap, HatClosed, HatOpen, Perc, Rim, Perc, Crash, Ride };
            return m[nl];
        }
        default:
        {
            static const int m[kNeuralLanes] = { Kick, Snare, HatClosed, HatOpen, TomFloor, TomMid, TomHigh, Crash, Ride };
            if (nl == 7 && metal && aggression > 0.75f) return China;
            return m[nl];
        }
    }
}

int NeuralEngine::laneToNeural (int lane, int styleId)
{
    // must stay the inverse of neuralToLane() so training data and playback agree
    switch (styleId)
    {
        case Cajon:
            switch (lane)
            {
                case Kick: return 0; case Snare: return 1; case Rim: return 2;
                case Shaker: case HatOpen: case HatClosed: return 3;
                case TomFloor: return 4; case TomMid: return 5; case TomHigh: return 6;
                case Tambourine: case Crash: case Crash2: case China: case Splash: return 7;
                case Ride: case RideBell: return 8;
                default: return -1;
            }
        case Orchestral:
            switch (lane)
            {
                case Kick: return 0; case Snare: return 1; case HatClosed: return 2;
                case TomFloor: return 4; case TomMid: return 5; case TomHigh: return 6;
                case Crash: case Crash2: case China: return 7;
                case Ride: case RideBell: case HatOpen: return 8;
                default: return -1;
            }
        case Minimal:
            switch (lane)
            {
                case Kick: return 0; case Clap: case Snare: return 1; case HatClosed: case Shaker: return 2;
                case HatOpen: return 3; case Perc: return 4; case Rim: return 5;
                case Crash: case Crash2: return 7; case Ride: case RideBell: return 8;
                default: return -1;
            }
        default:
            switch (lane)
            {
                case Kick: return 0; case Snare: case Rim: case Clap: return 1;
                case HatClosed: case HatPedal: return 2; case HatOpen: return 3;
                case TomFloor: return 4; case TomMid: return 5; case TomHigh: return 6;
                case Crash: case Crash2: case China: case Splash: return 7;
                case Ride: case RideBell: return 8;
                default: return -1;
            }
    }
}

void NeuralEngine::generate (const BrainContext& ctx, LaneGrid& g, Rng& rng, const float (*thr)[kMaxTicks], float temperature)
{
    if (! net.isValid())
    {
        generateFallback (ctx, g, rng);
        return;
    }
    g.clear (ctx.ticksPerBar);
    const int steps = ctx.ticksPerBar / 3;
    float in[kNeuralIn], out[kNeuralOut];

    // style mix (normalised)
    float mix[kNumGmdStyles]; float msum = 0.f;
    for (int i = 0; i < kNumGmdStyles; ++i) { mix[i] = ctx.style->gmdMix[i]; msum += mix[i]; }
    for (int i = 0; i < kNumGmdStyles; ++i) mix[i] = msum > 0.f ? mix[i] / msum : 0.f;

    const float listenW = ctx.lf.hasData ? (0.25f + 0.75f * ctx.follow) : 0.f;
    const float temp = std::max (0.35f, temperature);
    constexpr float kTwoPi = 6.28318530718f;

    for (int s = 0; s < steps; ++s)
    {
        const int tick = s * 3;
        int k = 0;
        for (int i = 0; i < 27; ++i) in[k++] = prevStep[i];
        for (int i = 0; i < kNumGmdStyles; ++i) in[k++] = mix[i];
        for (int i = 0; i < NumStyles; ++i) in[k++] = (i == ctx.styleId) ? 1.f : 0.f;
        in[k++] = ctx.intensity;
        for (int b = 0; b < 3; ++b)
        {
            float a = 0.f;
            if (ctx.accents != nullptr)
            {
                a = ctx.accents[b][tick];
                if (tick + 1 < ctx.ticksPerBar) a = std::max (a, ctx.accents[b][tick + 1] * 0.7f);
                if (tick > 0) a = std::max (a, ctx.accents[b][tick - 1] * 0.7f);
            }
            in[k++] = a * listenW;
        }
        const float sp = (float) (s % 16);
        in[k++] = std::sin (kTwoPi * sp / 16.f);
        in[k++] = std::cos (kTwoPi * sp / 16.f);
        in[k++] = std::sin (kTwoPi * (float) (s % 4) / 4.f);
        in[k++] = std::cos (kTwoPi * (float) (s % 4) / 4.f);
        in[k++] = (s % 16) == 0 ? 1.f : 0.f;
        in[k++] = (ctx.fillFromTick >= 0 && tick >= ctx.fillFromTick) ? 1.f : 0.f;

        net.step (in, out);

        for (int nl = 0; nl < kNeuralLanes; ++nl)
        {
            const float p = sigm (out[nl] / temp);
            const float v = sigm (out[9 + nl]);
            const float off = 0.5f * std::tanh (out[18 + nl]); // fraction of a 16th
            const int lane = neuralToLane (nl, ctx.styleId, ctx.aggression);

            // position in ticks, snapped to the style's grid
            float pos = (float) tick + off * 3.f;
            const int grid = ctx.style->tripletGrid ? 4 : 3;
            int snapped = (int) std::lround (pos / (float) grid) * grid;
            if (snapped >= ctx.ticksPerBar) snapped = ctx.ticksPerBar - grid;
            if (snapped < 0) snapped = 0;
            const float resid = pos - (float) snapped;

            const bool hit = p > thr[lane][snapped];
            if (p > g.prob[lane][snapped])
            {
                g.prob[lane][snapped] = p;
                g.vel[lane][snapped] = v;
                g.offs[lane][snapped] = resid;
            }
            prevStep[nl] = hit ? 1.f : 0.f;
            prevStep[9 + nl] = hit ? v : 0.f;
            prevStep[18 + nl] = hit ? off : 0.f;
        }
    }
}

void NeuralEngine::generateFallback (const BrainContext& ctx, LaneGrid& g, Rng& rng)
{
    // "Playbook improviser": a second, freer drummer built from the playbook that
    // remembers its last bar and mutates it (Markov-style continuity).
    BrainContext c = ctx;
    c.follow = ctx.follow * 0.5f;
    c.improvise = std::min (1.f, ctx.improvise + 0.35f);
    improviser.generate (c, g, rng);
    const int tpb = ctx.ticksPerBar;
    if (haveMemory && memory.ticksPerBar == tpb && ! ctx.phraseStart)
    {
        for (int l = 0; l < NumLanes; ++l)
            for (int t = 0; t < tpb; ++t)
            {
                g.prob[l][t] = 0.55f * g.prob[l][t] + 0.45f * memory.prob[l][t];
                g.vel[l][t] = std::max (g.vel[l][t], memory.vel[l][t] * 0.95f);
            }
    }
    // mutations: displace a kick, add a syncopated snare, drop a hat
    const float im = c.improvise;
    for (int t = 3; t < tpb - 3; t += 3)
    {
        if (g.prob[Kick][t] > 0.5f && t % kTicksPerBeat != 0 && rng.chance (0.15f * im))
        {
            const int nt = t + (rng.chance (0.5f) ? 3 : -3);
            g.prob[Kick][nt] = g.prob[Kick][t]; g.vel[Kick][nt] = g.vel[Kick][t]; g.prob[Kick][t] = 0.f;
        }
        if (rng.chance (0.04f * im * (0.4f + ctx.intensity)) && g.prob[Snare][t] < 0.2f)
            g.set (Snare, t, 0.8f, 0.3f + 0.4f * rng.uni());
    }
    memory = g;
    haveMemory = true;
}
} // namespace hammer
