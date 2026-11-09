#include "SampleKit.h"
#include <map>
#include <algorithm>
#include <cmath>

namespace hammer
{
// ---------------------------------------------------------------------------
int SampleKitLoader::laneForFolderName (const juce::String& raw)
{
    const auto n = raw.toLowerCase().removeCharacters (" -_.");
    struct A { const char* name; int lane; };
    static const A aliases[] = {
        { "kick", Kick }, { "bassdrum", Kick }, { "bd", Kick }, { "kdrum", Kick }, { "cajonbass", Kick }, { "bass", Kick },
        { "snare", Snare }, { "sd", Snare }, { "slap", Snare }, { "cajonslap", Snare },
        { "rim", Rim }, { "sidestick", Rim }, { "crossstick", Rim }, { "rimshot", Rim }, { "tip", Rim }, { "cajontip", Rim },
        { "hihatclosed", HatClosed }, { "hhclosed", HatClosed }, { "hatclosed", HatClosed }, { "closedhat", HatClosed }, { "hihat", HatClosed }, { "ch", HatClosed },
        { "hihatopen", HatOpen }, { "hhopen", HatOpen }, { "hatopen", HatOpen }, { "openhat", HatOpen }, { "oh", HatOpen },
        { "hihatpedal", HatPedal }, { "hhpedal", HatPedal }, { "hihatfoot", HatPedal }, { "pedal", HatPedal }, { "foot", HatPedal },
        { "tomhigh", TomHigh }, { "tom1", TomHigh }, { "hightom", TomHigh }, { "bongo", TomHigh },
        { "tommid", TomMid }, { "tom2", TomMid }, { "midtom", TomMid }, { "conga", TomMid },
        { "tomfloor", TomFloor }, { "tom3", TomFloor }, { "floortom", TomFloor }, { "floor", TomFloor }, { "tomlow", TomFloor }, { "timpani", TomFloor },
        { "crash", Crash }, { "crash1", Crash }, { "crash2", Crash2 }, { "crashb", Crash2 },
        { "ride", Ride }, { "ridebell", RideBell }, { "bell", RideBell },
        { "china", China }, { "gong", China }, { "splash", Splash }, { "triangle", Splash },
        { "clap", Clap }, { "claps", Clap }, { "handclap", Clap },
        { "cowbell", Cowbell }, { "shaker", Shaker }, { "cabasa", Shaker }, { "maracas", Shaker },
        { "tambourine", Tambourine }, { "tamb", Tambourine },
        { "perc", Perc }, { "percussion", Perc }, { "claves", Perc }, { "woodblock", Perc }, { "clave", Perc }
    };
    for (const auto& a : aliases) if (n == a.name) return a.lane;
    for (int l = 0; l < NumLanes; ++l)
        if (n == juce::String (laneName (l)).toLowerCase().removeCharacters (" -_./")) return l;
    return -1;
}

juce::File SampleKitLoader::defaultKitsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("APT Industrial").getChildFile ("The Hammer").getChildFile ("Kits");
}

// ---------------------------------------------------------------------------
static juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager fm;
    static std::once_flag once;
    std::call_once (once, [] { fm.registerBasicFormats(); });
    return fm;
}

static bool readStereo (const juce::File& f, double targetRate, juce::AudioBuffer<float>& out, size_t& bytes)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0)); // cap 20 s
    juce::AudioBuffer<float> tmp ((int) std::max (1u, std::min (2u, r->numChannels)), len);
    r->read (&tmp, 0, len, 0, true, tmp.getNumChannels() > 1);
    // trim leading silence (but keep transient), and trailing silence
    int start = 0, end = len;
    {
        const float thr = 0.0008f;
        auto peakAt = [&] (int i) { float m = 0; for (int c = 0; c < tmp.getNumChannels(); ++c) m = std::max (m, std::abs (tmp.getSample (c, i))); return m; };
        while (start < len - 1 && peakAt (start) < thr) ++start;
        start = std::max (0, start - 16);
        while (end > start + 64 && peakAt (end - 1) < thr * 0.25f) --end;
    }
    const int n = end - start;
    const double ratio = r->sampleRate / targetRate;
    const int outLen = std::max (1, (int) std::ceil (n / ratio));
    out.setSize (2, outLen);
    for (int c = 0; c < 2; ++c)
    {
        const float* src = tmp.getReadPointer (std::min (c, tmp.getNumChannels() - 1), start);
        float* dst = out.getWritePointer (c);
        if (std::abs (ratio - 1.0) < 1e-6) { std::copy (src, src + std::min (n, outLen), dst); continue; }
        juce::LagrangeInterpolator li;
        li.process (ratio, src, dst, outLen, n, 0);
    }
    // tiny fade-out to avoid clicks on trimmed tails
    const int fade = std::min (outLen, 64);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < fade; ++i) out.setSample (c, outLen - fade + i, out.getSample (c, outLen - fade + i) * (1.f - (float) i / (float) fade));
    bytes += (size_t) outLen * 2 * sizeof (float);
    return true;
}

static float measureLoudness (const juce::AudioBuffer<float>& b, double sr)
{
    const int win = std::max (16, (int) (sr * 0.012));
    const int len = std::min (b.getNumSamples(), (int) (sr * 0.12));
    float best = 0.f;
    for (int s = 0; s + win <= len; s += win / 2)
    {
        double acc = 0;
        for (int c = 0; c < 2; ++c)
        {
            const float* p = b.getReadPointer (c, s);
            for (int i = 0; i < win; ++i) acc += (double) p[i] * p[i];
        }
        best = std::max (best, (float) std::sqrt (acc / (2.0 * win)));
    }
    return best;
}

// velocity marker in a file name -> rank (higher = louder), or -1
static int velocityMarker (const juce::String& nameNoExt, int& rrOut)
{
    const auto n = nameNoExt.toLowerCase();
    rrOut = 0;
    auto numberAfter = [&] (const juce::String& tag) -> int {
        int idx = n.lastIndexOf (tag);
        while (idx >= 0)
        {
            const int s = idx + tag.length();
            int e = s;
            while (e < n.length() && juce::CharacterFunctions::isDigit (n[e])) ++e;
            if (e > s && (idx == 0 || ! juce::CharacterFunctions::isLetter (n[idx - 1]) || tag.startsWith ("_")))
                return n.substring (s, e).getIntValue();
            idx = idx > 0 ? n.substring (0, idx).lastIndexOf (tag) : -1;
        }
        return -1;
    };
    const int rr = numberAfter ("rr");
    if (rr >= 0) rrOut = rr;
    int v = numberAfter ("_vl"); if (v < 0) v = numberAfter ("_v"); if (v < 0) v = numberAfter ("vel");
    if (v >= 0) return v;
    static const char* dyn[] = { "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff" };
    juce::StringArray toks; toks.addTokens (n, "_- .", "");
    for (int t = toks.size() - 1; t >= 0; --t)
        for (int d = 0; d < 8; ++d)
            if (toks[t] == dyn[d]) return d + 1;
    // DrumGizmo style "12-Snare-..." power index first
    if (toks.size() > 1 && toks[0].containsOnly ("0123456789")) return toks[0].getIntValue();
    return -1;
}

static void buildLane (SampleLane& lane, std::vector<std::pair<juce::File, juce::AudioBuffer<float>>>& hits,
                       const std::vector<int>& markers, const std::vector<int>& rrs, double sr)
{
    struct H { float loud; int marker; int rr; juce::AudioBuffer<float>* buf; };
    std::vector<H> hs;
    for (size_t i = 0; i < hits.size(); ++i)
        hs.push_back ({ measureLoudness (hits[i].second, sr), markers[i], rrs[i], &hits[i].second });
    if (hs.empty()) return;
    const bool useMarkers = std::all_of (hs.begin(), hs.end(), [] (const H& h) { return h.marker >= 0; });
    std::map<int, std::vector<H>> groups;
    if (useMarkers)
    {
        for (auto& h : hs) groups[h.marker].push_back (h);
        // DrumGizmo kits: dozens of single hits with power index -> cluster instead
        if (groups.size() > 12) groups.clear();
    }
    if (groups.empty())
    {
        std::sort (hs.begin(), hs.end(), [] (const H& a, const H& b) { return a.loud < b.loud; });
        const int L = std::max (1, std::min (10, (int) std::ceil (hs.size() / 3.0)));
        for (size_t i = 0; i < hs.size(); ++i)
            groups[(int) ((i * (size_t) L) / hs.size())].push_back (hs[i]);
    }
    for (auto& [key, vec] : groups)
    {
        SampleLayer layer;
        float sum = 0.f;
        for (auto& h : vec) { layer.rr.push_back (std::move (*h.buf)); sum += h.loud; }
        layer.loudness = sum / (float) vec.size();
        lane.layers.push_back (std::move (layer));
    }
    std::sort (lane.layers.begin(), lane.layers.end(), [] (const SampleLayer& a, const SampleLayer& b) { return a.loudness < b.loudness; });
}

static bool isAudio (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".flac" || e == ".aif" || e == ".aiff" || e == ".ogg";
}

// ---------------------------------------------------------------------------
//  SFZ
// ---------------------------------------------------------------------------
namespace
{
int parseNote (juce::String v)
{
    v = v.trim().toLowerCase();
    if (v.isEmpty()) return -1;
    if (v.containsOnly ("0123456789-")) return v.getIntValue();
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    const juce::juce_wchar c = v[0];
    if (c < 'a' || c > 'g') return -1;
    int n = base[c - 'a'];
    int i = 1;
    if (i < v.length() && (v[i] == '#' || v[i] == 's')) { ++n; ++i; }
    else if (i < v.length() && v[i] == 'b') { --n; ++i; }
    const int oct = v.substring (i).getIntValue();
    return (oct + 1) * 12 + n; // c4 = 60
}

struct SfzRegion { std::map<juce::String, juce::String> op; };

void sfzPreprocess (const juce::File& file, juce::String& out, std::map<juce::String, juce::String>& defs, int depth)
{
    if (depth > 8 || ! file.existsAsFile()) return;
    juce::StringArray lines; lines.addLines (file.loadFileAsString());
    for (auto line : lines)
    {
        const int cm = line.indexOf ("//");
        if (cm >= 0) line = line.substring (0, cm);
        auto t = line.trim();
        if (t.startsWith ("#define"))
        {
            juce::StringArray tk; tk.addTokens (t, " \t", "");
            tk.removeEmptyStrings();
            if (tk.size() >= 3) defs[tk[1]] = tk[2];
            continue;
        }
        for (auto& d : defs) line = line.replace (d.first, d.second);
        t = line.trim();
        if (t.startsWith ("#include"))
        {
            const auto name = t.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
            sfzPreprocess (file.getParentDirectory().getChildFile (name), out, defs, depth + 1);
            continue;
        }
        out << line << "\n";
    }
}

std::vector<SfzRegion> sfzParse (const juce::File& file)
{
    juce::String text;
    std::map<juce::String, juce::String> defs;
    sfzPreprocess (file, text, defs, 0);
    std::vector<SfzRegion> regions;
    std::map<juce::String, juce::String> control, global, master, group, current;
    juce::String header;
    auto flush = [&] { if (header == "region") { SfzRegion r; r.op = control; for (auto* m : { &global, &master, &group, &current }) for (auto& kv : *m) r.op[kv.first] = kv.second; regions.push_back (r); } current.clear(); };
    int i = 0; const int n = text.length();
    while (i < n)
    {
        const auto c = text[i];
        if (c == '<')
        {
            const int e = text.indexOfChar (i, '>');
            if (e < 0) break;
            flush();
            header = text.substring (i + 1, e).trim().toLowerCase();
            if (header == "group") group.clear();
            if (header == "master") { master.clear(); group.clear(); }
            if (header == "global") { global.clear(); master.clear(); group.clear(); }
            i = e + 1;
            continue;
        }
        if (juce::CharacterFunctions::isWhitespace (c)) { ++i; continue; }
        // opcode=value ; value runs until the next "word=" or '<' or newline (sample paths may contain spaces)
        const int eq = text.indexOfChar (i, '=');
        if (eq < 0) break;
        const auto key = text.substring (i, eq).trim().toLowerCase();
        int j = eq + 1, lineEnd = text.indexOfChar (j, '\n'); if (lineEnd < 0) lineEnd = n;
        int vEnd = lineEnd;
        for (int k = j; k < lineEnd; ++k)
        {
            if (text[k] == '<') { vEnd = k; break; }
            if (text[k] == '=' )
            {
                int s = k - 1;
                while (s > j && ! juce::CharacterFunctions::isWhitespace (text[s])) --s;
                if (s > j) { vEnd = s; break; }
            }
        }
        const auto value = text.substring (j, vEnd).trim();
        std::map<juce::String, juce::String>* target = &current;
        if (header == "control") target = &control;
        else if (header == "global") target = &global;
        else if (header == "master") target = &master;
        else if (header == "group") target = &group;
        (*target)[key] = value;
        i = vEnd;
    }
    flush();
    return regions;
}
} // namespace

static std::unique_ptr<SampleKitData> loadSfz (const juce::File& sfz, double sr, juce::String& error, const std::atomic<bool>* cancel)
{
    auto kit = std::make_unique<SampleKitData>();
    kit->name = sfz.getFileNameWithoutExtension();
    kit->sourcePath = sfz.getFullPathName();
    kit->sampleRate = sr;
    const auto regions = sfzParse (sfz);
    if (regions.empty()) { error = "no regions in " + sfz.getFileName(); return nullptr; }

    // playable unit: lane + vel range + round robin -> list of regions (mics get summed)
    struct Unit { int lane, lovel, hivel, rr; std::vector<const SfzRegion*> regs; };
    std::map<juce::String, Unit> units;
    for (const auto& r : regions)
    {
        auto get = [&] (const char* k, const juce::String& d = {}) { auto it = r.op.find (k); return it != r.op.end() ? it->second : d; };
        if (get ("trigger", "attack") != "attack") continue;
        if (get ("sample").isEmpty()) continue;
        int lo = parseNote (get ("lokey", get ("key", "-1"))), hi = parseNote (get ("hikey", get ("key", "-1")));
        if (lo < 0 || hi < 0) continue;
        const int lovel = get ("lovel", "1").getIntValue(), hivel = get ("hivel", "127").getIntValue();
        const int rr = get ("seq_position", "1").getIntValue() * 100 + (int) (get ("lorand", "0").getFloatValue() * 99);
        for (int key = std::max (0, lo); key <= std::min (127, hi); ++key)
        {
            const int lane = gmNoteToLane (key);
            if (lane < 0) continue;
            const juce::String id = juce::String (lane) + ":" + juce::String (lovel) + ":" + juce::String (hivel) + ":" + juce::String (rr);
            auto& u = units[id];
            u.lane = lane; u.lovel = lovel; u.hivel = hivel; u.rr = rr;
            u.regs.push_back (&r);
            break; // one lane per region is enough
        }
    }
    // build layers by velocity range
    std::map<int, std::map<int, SampleLayer>> byLane; // lane -> lovel -> layer
    for (auto& [id, u] : units)
    {
        if (cancel && cancel->load()) { error = "cancelled"; return nullptr; }
        juce::AudioBuffer<float> mix;
        for (const auto* reg : u.regs)
        {
            auto get = [&] (const char* k, const juce::String& d = {}) { auto it = reg->op.find (k); return it != reg->op.end() ? it->second : d; };
            const auto path = (get ("default_path") + get ("sample")).replaceCharacter ('\\', '/');
            juce::AudioBuffer<float> b;
            if (! readStereo (sfz.getParentDirectory().getChildFile (path), sr, b, kit->bytes)) continue;
            kit->numFiles++;
            const float volDb = get ("volume", "0").getFloatValue();
            const float amp = get ("amplitude", "100").getFloatValue() / 100.f;
            const float pan = juce::jlimit (-1.f, 1.f, get ("pan", "0").getFloatValue() / 100.f);
            const float g = juce::Decibels::decibelsToGain (volDb) * amp;
            const float gl = g * std::cos ((pan + 1.f) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
            const float gr = g * std::sin ((pan + 1.f) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
            if (mix.getNumSamples() < b.getNumSamples())
            {
                juce::AudioBuffer<float> grown (2, b.getNumSamples()); grown.clear();
                for (int c = 0; c < 2; ++c) if (mix.getNumSamples() > 0) grown.copyFrom (c, 0, mix, c, 0, mix.getNumSamples());
                mix = std::move (grown);
            }
            mix.addFrom (0, 0, b, 0, 0, b.getNumSamples(), gl);
            mix.addFrom (1, 0, b, 1, 0, b.getNumSamples(), gr);
        }
        if (mix.getNumSamples() == 0) continue;
        auto& layer = byLane[u.lane][u.lovel];
        layer.rr.push_back (std::move (mix));
        if (kit->bytes > (size_t) 3000 * 1024 * 1024) { error = "kit too large (over 3 GB) - loaded partially"; break; }
    }
    for (auto& [lane, layers] : byLane)
    {
        for (auto& [lv, layer] : layers)
        {
            float s = 0.f; for (auto& b : layer.rr) s += measureLoudness (b, sr);
            layer.loudness = s / (float) std::max<size_t> (1, layer.rr.size());
            kit->lanes[lane].layers.push_back (std::move (layer));
        }
    }
    // Lanes the SFZ doesn't cover borrow from neighbours (e.g. pedal hat -> closed hat)
    if (! kit->lanes[HatPedal].present() && kit->lanes[HatClosed].present()) { kit->lanes[HatPedal] = kit->lanes[HatClosed]; kit->lanes[HatPedal].gain = 0.6f; }
    if (! kit->lanes[Crash2].present() && kit->lanes[Crash].present()) kit->lanes[Crash2] = kit->lanes[Crash];
    if (! kit->lanes[RideBell].present() && kit->lanes[Ride].present()) kit->lanes[RideBell] = kit->lanes[Ride];
    return kit;
}

// ---------------------------------------------------------------------------
std::unique_ptr<SampleKitData> SampleKitLoader::load (const juce::File& src, double sr, juce::String& error, const std::atomic<bool>* cancel)
{
    if (src.existsAsFile() && src.hasFileExtension ("sfz")) return loadSfz (src, sr, error, cancel);
    if (! src.isDirectory()) { error = "not found: " + src.getFullPathName(); return nullptr; }

    auto kit = std::make_unique<SampleKitData>();
    kit->name = src.getFileName();
    kit->sourcePath = src.getFullPathName();
    kit->sampleRate = sr;

    // kit.json metadata + optional lane folder overrides
    std::map<int, juce::String> laneFolder;
    const auto meta = src.getChildFile ("kit.json");
    if (meta.existsAsFile())
    {
        const auto v = juce::JSON::parse (meta);
        kit->name = v.getProperty ("name", kit->name).toString();
        kit->author = v.getProperty ("author", "").toString();
        kit->license = v.getProperty ("license", "").toString();
        kit->credits = v.getProperty ("credits", "").toString();
        if (const auto* st = v.getProperty ("styles", {}).getArray()) for (auto& s : *st) kit->styles.add (s.toString().toLowerCase());
        if (auto* lanes = v.getProperty ("lanes", {}).getDynamicObject())
            for (auto& p : lanes->getProperties())
            {
                const int l = laneForFolderName (p.name.toString());
                if (l >= 0) laneFolder[l] = p.value.toString();
            }
        if (auto* gains = v.getProperty ("gain_db", {}).getDynamicObject())
            for (auto& p : gains->getProperties())
            {
                const int l = laneForFolderName (p.name.toString());
                if (l >= 0) kit->lanes[l].gain = juce::Decibels::decibelsToGain ((float) (double) p.value);
            }
        if (auto* pans = v.getProperty ("pan", {}).getDynamicObject())
            for (auto& p : pans->getProperties())
            {
                const int l = laneForFolderName (p.name.toString());
                if (l >= 0) kit->lanes[l].pan = juce::jlimit (-1.f, 1.f, (float) (double) p.value);
            }
    }
    // auto-discover lane folders
    for (const auto& d : src.findChildFiles (juce::File::findDirectories, false))
    {
        const int l = laneForFolderName (d.getFileName());
        if (l >= 0 && laneFolder.find (l) == laneFolder.end()) laneFolder[l] = d.getFileName();
    }
    if (laneFolder.empty())
    {
        // maybe it's a folder that contains an .sfz
        auto sfzs = src.findChildFiles (juce::File::findFiles, false, "*.sfz");
        if (! sfzs.isEmpty()) { sfzs.sort(); return loadSfz (sfzs[0], sr, error, cancel); }
        error = "no drum folders (kick, snare, ...) or .sfz in " + src.getFileName();
        return nullptr;
    }

    for (auto& [lane, folderName] : laneFolder)
    {
        const auto dir = src.getChildFile (folderName);
        if (! dir.isDirectory()) continue;
        auto files = dir.findChildFiles (juce::File::findFiles, false);
        files.sort();
        std::vector<std::pair<juce::File, juce::AudioBuffer<float>>> hits;
        std::vector<int> markers, rrs;
        for (const auto& f : files)
        {
            if (cancel && cancel->load()) { error = "cancelled"; return nullptr; }
            if (! isAudio (f)) continue;
            juce::AudioBuffer<float> b;
            if (! readStereo (f, sr, b, kit->bytes)) continue;
            kit->numFiles++;
            int rr = 0;
            markers.push_back (velocityMarker (f.getFileNameWithoutExtension(), rr));
            rrs.push_back (rr);
            hits.emplace_back (f, std::move (b));
        }
        buildLane (kit->lanes[lane], hits, markers, rrs, sr);
    }
    if (! kit->lanes[HatPedal].present() && kit->lanes[HatClosed].present()) { kit->lanes[HatPedal] = kit->lanes[HatClosed]; kit->lanes[HatPedal].gain *= 0.6f; }
    if (! kit->lanes[Crash2].present() && kit->lanes[Crash].present()) kit->lanes[Crash2] = kit->lanes[Crash];
    if (! kit->lanes[RideBell].present() && kit->lanes[Ride].present()) kit->lanes[RideBell] = kit->lanes[Ride];
    if (kit->numFiles == 0) { error = "no readable audio files in " + src.getFileName(); return nullptr; }
    return kit;
}

std::vector<KitInfo> SampleKitLoader::scan (const juce::File& folder)
{
    std::vector<KitInfo> out;
    if (! folder.isDirectory()) return out;
    auto dirs = folder.findChildFiles (juce::File::findDirectories | juce::File::findFiles, false);
    dirs.sort();
    for (const auto& d : dirs)
    {
        KitInfo k;
        if (d.isDirectory())
        {
            k.name = d.getFileName(); k.path = d.getFullPathName();
            const auto meta = d.getChildFile ("kit.json");
            if (meta.existsAsFile())
            {
                const auto v = juce::JSON::parse (meta);
                k.name = v.getProperty ("name", k.name).toString();
                k.license = v.getProperty ("license", "").toString();
                k.author = v.getProperty ("author", "").toString();
                if (const auto* st = v.getProperty ("styles", {}).getArray()) for (auto& s : *st) k.styles.add (s.toString().toLowerCase());
            }
            else
            {
                bool any = false;
                for (const auto& c : d.findChildFiles (juce::File::findDirectories, false)) if (laneForFolderName (c.getFileName()) >= 0) { any = true; break; }
                if (! any && d.findChildFiles (juce::File::findFiles, false, "*.sfz").isEmpty()) continue;
            }
            out.push_back (k);
        }
        else if (d.hasFileExtension ("sfz"))
        {
            k.name = d.getFileNameWithoutExtension(); k.path = d.getFullPathName();
            out.push_back (k);
        }
    }
    return out;
}

// ===========================================================================
KitSlots::KitSlots() : juce::Thread ("The Hammer kit loader")
{
    for (int i = 0; i < kNumSlots; ++i) { current[i].store (nullptr); loading[i].store (false); }
    startThread (juce::Thread::Priority::low);
}

KitSlots::~KitSlots()
{
    cancelFlag = true;
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);
}

void KitSlots::setSampleRate (double sr)
{
    std::lock_guard<std::mutex> g (lock);
    if (std::abs (sr - rate) < 1.0) return;
    rate = sr;
    // reload everything at the new rate
    for (int i = 0; i < kNumSlots; ++i)
        if (loadedPath[i].isNotEmpty()) { requested[i] = loadedPath[i]; pending[i] = true; }
    wake.signal();
}

void KitSlots::requestLoad (int slot, const juce::File& source)
{
    if (slot < 0 || slot >= kNumSlots) return;
    {
        std::lock_guard<std::mutex> g (lock);
        requested[slot] = source.getFullPathName();
        pending[slot] = true;
        status[slot] = "Loading " + source.getFileName() + " ...";
    }
    wake.signal();
}

void KitSlots::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumSlots) return;
    std::lock_guard<std::mutex> g (lock);
    current[slot].store (nullptr, std::memory_order_release);
    for (auto& k : owned[slot]) retired.emplace_back (juce::Time::currentTimeMillis(), std::move (k));
    owned[slot].clear();
    loadedPath[slot].clear(); requested[slot].clear(); status[slot] = "Empty"; pending[slot] = false;
}

juce::String KitSlots::pathOf (int slot) const { std::lock_guard<std::mutex> g (lock); return loadedPath[slot].isNotEmpty() ? loadedPath[slot] : requested[slot]; }
juce::String KitSlots::statusOf (int slot) const { std::lock_guard<std::mutex> g (lock); return status[slot].isNotEmpty() ? status[slot] : juce::String ("Empty"); }
bool KitSlots::isLoading (int slot) const { return loading[slot].load(); }

int KitSlots::slotWithTag (const char* tag) const
{
    for (int i = 0; i < kNumSlots; ++i)
        if (auto* k = current[i].load (std::memory_order_acquire))
            if (k->hasTag (tag)) return i;
    return -1;
}

void KitSlots::freeRetired()
{
    std::lock_guard<std::mutex> g (lock);
    const auto now = juce::Time::currentTimeMillis();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& p) { return now - p.first > 4000; }), retired.end());
}

void KitSlots::run()
{
    while (! threadShouldExit())
    {
        wake.wait (500);
        for (int i = 0; i < kNumSlots && ! threadShouldExit(); ++i)
        {
            juce::String path; double sr;
            {
                std::lock_guard<std::mutex> g (lock);
                if (! pending[i]) continue;
                pending[i] = false;
                path = requested[i]; sr = rate;
            }
            loading[i] = true;
            juce::String err;
            auto kit = SampleKitLoader::load (juce::File (path), sr, err, &cancelFlag);
            loading[i] = false;
            std::lock_guard<std::mutex> g (lock);
            if (pending[i]) continue; // superseded while loading
            if (kit == nullptr) { status[i] = "Failed: " + err; continue; }
            auto* raw = kit.get();
            raw->styleTags.clear();
            for (auto& st : raw->styles) raw->styleTags.push_back (st.toLowerCase().toStdString());
            for (auto& k : owned[i]) retired.emplace_back (juce::Time::currentTimeMillis(), std::move (k));
            owned[i].clear();
            owned[i].push_back (std::move (kit));
            current[i].store (raw, std::memory_order_release);
            loadedPath[i] = path;
            status[i] = raw->name + " (" + juce::String (raw->numFiles) + " samples, "
                        + juce::String (juce::roundToInt ((double) raw->bytes / (1024.0 * 1024.0))) + " MB)"
                        + (err.isNotEmpty() ? " - " + err : juce::String());
        }
    }
}
} // namespace hammer
