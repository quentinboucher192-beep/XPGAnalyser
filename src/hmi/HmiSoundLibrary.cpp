// =============================================================================
//  hmi/HmiSoundLibrary.cpp - 1.24.0 : la bibliotheque de sons (voir HmiSoundLibrary.hpp)
// =============================================================================
#include "HmiSoundLibrary.hpp"

#include "HmiAssets.hpp"
#include "HmiGuide.hpp"         // fold (la recherche)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

namespace hmi::soundlib {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---- la synthese ---------------------------------------------------------------------------------------------------------
//  Des notes mises bout a bout, chacune avec sa forme, son balayage (f0 -> f1), son volume, une attaque et une fin douces
//  (pas de clic), ou une decroissance (une cloche). Les formes carrees et en dents de scie sont faites de quelques
//  harmoniques : un son franc, sans le crissement d'un vrai carre a 22 kHz.
enum class Wave : std::uint8_t { Sine, Square, Triangle, Saw };

class Synth {
public:
    void tone(double f0, double f1, double seconds, Wave wave, double volume, double attack = 0.005, double release = 0.02,
              double decay = 0.0) {
        const auto n = static_cast<std::size_t>(seconds * kRate);
        double phase = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double f = f0 + (f1 - f0) * (seconds > 0 ? t / seconds : 0.0);
            phase += 2.0 * kPi * f / kRate;
            double v = shape(wave, phase);
            double env = 1.0;
            if (attack > 0 && t < attack) env = t / attack;
            if (release > 0 && t > seconds - release) env = std::min(env, std::max(0.0, (seconds - t) / release));
            if (decay > 0) env *= std::exp(-t * decay);
            out_.push_back(static_cast<float>(v * env * volume));
        }
    }
    // Une cloche : la fondamentale et deux partiels, qui s'eteignent.
    void bell(double f, double seconds, double volume, double decay = 5.0) {
        const auto n = static_cast<std::size_t>(seconds * kRate);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double v = std::sin(2 * kPi * f * t) + 0.45 * std::sin(2 * kPi * f * 2.0 * t) * std::exp(-t * 3.0)
                           + 0.2 * std::sin(2 * kPi * f * 3.01 * t) * std::exp(-t * 6.0);
            double env = std::exp(-t * decay);
            if (t < 0.004) env *= t / 0.004;
            if (t > seconds - 0.02) env *= std::max(0.0, (seconds - t) / 0.02);
            out_.push_back(static_cast<float>(v / 1.65 * env * volume));
        }
    }
    // Un bruit bref (un clic), toujours le meme (une suite pseudo-aleatoire fixe).
    void noise(double seconds, double volume) {
        const auto n = static_cast<std::size_t>(seconds * kRate);
        std::uint32_t x = 0x12345678u;
        for (std::size_t i = 0; i < n; ++i) {
            x = x * 1664525u + 1013904223u;
            const double v = (static_cast<double>(x >> 8) / static_cast<double>(1u << 24)) * 2.0 - 1.0;
            const double env = std::exp(-static_cast<double>(i) / kRate * 180.0);
            out_.push_back(static_cast<float>(v * env * volume));
        }
    }
    void silence(double seconds) { out_.insert(out_.end(), static_cast<std::size_t>(seconds * kRate), 0.f); }
    // Ajouter `other` a partir de `at` secondes (deux notes ensemble).
    void mixAt(const Synth& other, double at) {
        const auto start = static_cast<std::size_t>(at * kRate);
        if (out_.size() < start + other.out_.size()) out_.resize(start + other.out_.size(), 0.f);
        for (std::size_t i = 0; i < other.out_.size(); ++i) out_[start + i] += other.out_[i];
    }
    [[nodiscard]] std::vector<std::int16_t> pcm() const {
        std::vector<std::int16_t> s;
        s.reserve(out_.size());
        for (const float v : out_) s.push_back(static_cast<std::int16_t>(std::lround(std::clamp(v, -1.f, 1.f) * 32000.f)));
        return s;
    }

private:
    static double shape(Wave w, double ph) {
        switch (w) {
            case Wave::Sine: return std::sin(ph);
            case Wave::Square: return (std::sin(ph) + std::sin(3 * ph) / 3 + std::sin(5 * ph) / 5 + std::sin(7 * ph) / 7) / 1.1;
            case Wave::Triangle: return (std::sin(ph) - std::sin(3 * ph) / 9 + std::sin(5 * ph) / 25) / 1.0;
            case Wave::Saw: return (std::sin(ph) - std::sin(2 * ph) / 2 + std::sin(3 * ph) / 3 - std::sin(4 * ph) / 4 + std::sin(5 * ph) / 5) / 1.3;
        }
        return 0.0;
    }
    std::vector<float> out_;
};

struct Recipe {
    const char* id;
    const char* label;
    const char* category;
    const char* words;
    const char* description;
    std::function<void(Synth&)> make;
};

const char* const kAlarms = "Alarmes";
const char* const kNotify = "Notifications";
const char* const kUi = "Interface";
const char* const kProcess = "Proc\xC3\xA9" "d\xC3\xA9";

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> all = {
        // ---- Alarmes : de la plus grave a la plus douce ----
        {"alarme-critique", "Alarme critique", kAlarms, "urgence danger sirene evacuation priorite 1 critique",
         "Une sir\xC3\xA8ne \xC3\xA0 deux tons, insistante : l'alarme qu'on ne peut pas ignorer (priorit\xC3\xA9 la plus haute).",
         [](Synth& s) {
             for (int k = 0; k < 4; ++k) {
                 s.tone(960, 960, 0.25, Wave::Square, 0.55, 0.004, 0.01);
                 s.tone(680, 680, 0.25, Wave::Square, 0.55, 0.004, 0.01);
             }
         }},
        {"alarme-haute", "Alarme haute priorit\xC3\xA9", kAlarms, "alarme haute priorite rapide impulsions 2",
         "Deux salves de trois bips rapides : une alarme haute priorit\xC3\xA9, \xC3\xA0 traiter tout de suite.",
         [](Synth& s) {
             for (int g = 0; g < 2; ++g) {
                 for (int k = 0; k < 3; ++k) {
                     s.tone(1000, 1000, 0.10, Wave::Square, 0.5);
                     s.silence(0.07);
                 }
                 s.silence(0.25);
             }
         }},
        {"alarme-moyenne", "Alarme priorit\xC3\xA9 moyenne", kAlarms, "alarme moyenne priorite 3",
         "Deux bips plus longs : une alarme de priorit\xC3\xA9 moyenne.",
         [](Synth& s) {
             for (int k = 0; k < 2; ++k) {
                 s.tone(800, 800, 0.22, Wave::Triangle, 0.55);
                 s.silence(0.16);
             }
         }},
        {"alarme-basse", "Alarme basse priorit\xC3\xA9", kAlarms, "alarme basse priorite 4 douce",
         "Un son doux et unique : une alarme basse priorit\xC3\xA9, pour information.",
         [](Synth& s) { s.tone(620, 620, 0.45, Wave::Sine, 0.6, 0.01, 0.2, 2.5); }},
        {"sirene-montante", "Sir\xC3\xA8ne montante", kAlarms, "sirene montante balayage evacuation",
         "Un balayage qui monte, deux fois : une \xC3\xA9vacuation, un danger qui arrive.",
         [](Synth& s) {
             s.tone(420, 1250, 0.9, Wave::Saw, 0.5, 0.02, 0.03);
             s.tone(420, 1250, 0.9, Wave::Saw, 0.5, 0.02, 0.05);
         }},
        {"klaxon", "Klaxon", kAlarms, "klaxon corne avertisseur grave",
         "Une corne grave et rauque : un d\xC3\xA9marrage de machine, une zone dangereuse.",
         [](Synth& s) {
             Synth a, b;
             a.tone(415, 415, 0.8, Wave::Saw, 0.45, 0.01, 0.05);
             b.tone(523, 523, 0.8, Wave::Saw, 0.3, 0.01, 0.05);
             s.mixAt(a, 0);
             s.mixAt(b, 0);
         }},
        {"bip-continu", "Bip continu", kAlarms, "bip continu tonalite fixe",
         "Une tonalit\xC3\xA9 fixe d'une seconde et demie (\xC3\xA0 r\xC3\xA9p\xC3\xA9ter pour un son continu).",
         [](Synth& s) { s.tone(1000, 1000, 1.5, Wave::Sine, 0.5, 0.01, 0.03); }},
        // ---- Notifications ----
        {"notification", "Notification", kNotify, "notification message nouveau",
         "Deux notes qui montent : un message, une information nouvelle.",
         [](Synth& s) {
             s.bell(880, 0.18, 0.6, 7.0);
             s.bell(1320, 0.45, 0.6, 5.0);
         }},
        {"information", "Information", kNotify, "information cloche douce",
         "Une cloche douce : une information sans urgence.",
         [](Synth& s) { s.bell(660, 0.8, 0.7, 4.0); }},
        {"succes", "Succ\xC3\xA8s", kNotify, "succes reussi valide ok termine",
         "Trois notes qui montent : une op\xC3\xA9ration r\xC3\xA9ussie.",
         [](Synth& s) {
             s.bell(523, 0.12, 0.55, 8.0);
             s.bell(659, 0.12, 0.55, 8.0);
             s.bell(784, 0.45, 0.6, 5.0);
         }},
        {"erreur", "Erreur", kNotify, "erreur echec refuse",
         "Deux notes qui descendent : une op\xC3\xA9ration qui a \xC3\xA9" "chou\xC3\xA9.",
         [](Synth& s) {
             s.tone(440, 440, 0.16, Wave::Triangle, 0.55);
             s.silence(0.04);
             s.tone(330, 330, 0.3, Wave::Triangle, 0.55, 0.005, 0.12);
         }},
        {"avertissement", "Avertissement", kNotify, "avertissement attention",
         "Trois bips moyens : attention, quelque chose demande un regard.",
         [](Synth& s) {
             for (int k = 0; k < 3; ++k) {
                 s.tone(720, 720, 0.12, Wave::Triangle, 0.5);
                 s.silence(0.1);
             }
         }},
        {"acquittement", "Acquittement", kNotify, "acquittement acquitte pris en compte",
         "Un double tintement bref : l'alarme est acquitt\xC3\xA9" "e.",
         [](Synth& s) {
             s.bell(1200, 0.07, 0.45, 20.0);
             s.silence(0.03);
             s.bell(1500, 0.2, 0.45, 12.0);
         }},
        // ---- Interface ----
        {"clic", "Clic", kUi, "clic bouton tic",
         "Un clic sec : un bouton appuy\xC3\xA9.",
         [](Synth& s) {
             s.noise(0.012, 0.6);
             s.tone(2400, 1800, 0.02, Wave::Sine, 0.25, 0.001, 0.01);
         }},
        {"bip-court", "Bip court", kUi, "bip court",
         "Un bip bref : une saisie prise en compte.",
         [](Synth& s) { s.tone(1000, 1000, 0.08, Wave::Sine, 0.55, 0.003, 0.02); }},
        {"bip-double", "Bip double", kUi, "bip double deux",
         "Deux bips brefs : une confirmation.",
         [](Synth& s) {
             s.tone(1100, 1100, 0.07, Wave::Sine, 0.55, 0.003, 0.02);
             s.silence(0.06);
             s.tone(1100, 1100, 0.07, Wave::Sine, 0.55, 0.003, 0.02);
         }},
        {"touche", "Touche", kUi, "touche clavier tactile",
         "Un toc tr\xC3\xA8s bref : une touche d'un clavier \xC3\xA0 l'\xC3\xA9" "cran.",
         [](Synth& s) { s.tone(1600, 1400, 0.03, Wave::Sine, 0.45, 0.001, 0.015); }},
        {"refus", "Refus", kUi, "refus interdit impossible buzzer",
         "Un bourdonnement grave : un geste refus\xC3\xA9 (droits insuffisants, valeur hors plage).",
         [](Synth& s) { s.tone(190, 190, 0.28, Wave::Square, 0.45, 0.005, 0.05); }},
        // ---- Procede ----
        {"demarrage", "D\xC3\xA9marrage", kProcess, "demarrage marche mise en route moteur",
         "Un balayage qui monte : une machine qui d\xC3\xA9marre.",
         [](Synth& s) { s.tone(300, 900, 0.6, Wave::Triangle, 0.55, 0.02, 0.08); }},
        {"arret", "Arr\xC3\xAAt", kProcess, "arret stop fin",
         "Un balayage qui descend : une machine qui s'arr\xC3\xAAte.",
         [](Synth& s) { s.tone(900, 300, 0.6, Wave::Triangle, 0.55, 0.02, 0.08); }},
        {"cycle-termine", "Cycle termin\xC3\xA9", kProcess, "cycle termine fin lot recette",
         "Trois notes claires : un cycle, un lot, une recette termin\xC3\xA9s.",
         [](Synth& s) {
             s.bell(784, 0.16, 0.55, 7.0);
             s.bell(988, 0.16, 0.55, 7.0);
             s.bell(1175, 0.6, 0.6, 4.0);
         }},
        {"carillon", "Carillon", kProcess, "carillon ding dong porte accueil",
         "Ding-dong : une arriv\xC3\xA9" "e, une porte, un poste qui appelle.",
         [](Synth& s) {
             s.bell(659, 0.45, 0.65, 3.5);
             s.bell(523, 0.9, 0.65, 3.0);
         }},
    };
    return all;
}

// Un WAV PCM 16 bits mono.
Bytes wavOf(const std::vector<std::int16_t>& pcm) {
    Bytes b;
    const auto le32 = [&b](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const auto le16 = [&b](std::uint16_t v) {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto tag = [&b](const char* t) { b.insert(b.end(), t, t + 4); };
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    tag("RIFF");
    le32(36 + dataBytes);
    tag("WAVE");
    tag("fmt ");
    le32(16);
    le16(1);                                   // PCM
    le16(1);                                   // mono
    le32(kRate);
    le32(kRate * 2);
    le16(2);
    le16(16);
    tag("data");
    le32(dataBytes);
    for (const auto s : pcm) le16(static_cast<std::uint16_t>(s));
    return b;
}

std::vector<std::int16_t> synthesize(const Recipe& r) {
    Synth s;
    r.make(s);
    return s.pcm();
}

// ---- les fichiers (Mes sons) ----
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string& folderRef() {
    static std::string f;
    return f;
}
std::atomic<std::uint64_t>& revisionRef() {
    static std::atomic<std::uint64_t> r{1};
    return r;
}
bool readAll(const fs::path& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
std::pair<std::string, std::string> stemExt(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return {name, {}};
    return {name.substr(0, dot), name.substr(dot)};
}
std::string safeFileName(std::string name) {
    for (auto& ch : name)
        if (static_cast<unsigned char>(ch) < 0x20 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name.empty() ? std::string("son") : name;
}

} // namespace

// ================================================================== les sons natifs ===
const std::vector<Sound>& builtIn() {
    static const std::vector<Sound> all = [] {
        std::vector<Sound> out;
        for (const auto& r : recipes()) {
            Sound s;
            s.id = r.id;
            s.label = r.label;
            s.category = r.category;
            s.words = r.words;
            s.description = r.description;
            s.format = "WAV";
            const auto pcm = synthesize(r);
            s.seconds = static_cast<double>(pcm.size()) / kRate;
            s.data = std::make_shared<const Bytes>(wavOf(pcm));
            out.push_back(std::move(s));
        }
        return out;
    }();
    return all;
}

const std::vector<std::string>& builtInCategories() {
    static const std::vector<std::string> all = {kAlarms, kNotify, kUi, kProcess};
    return all;
}

const Sound* findBuiltIn(std::string_view id) {
    for (const auto& s : builtIn())
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<std::int16_t> samplesOf(std::string_view id) {
    for (const auto& r : recipes())
        if (r.id == id) return synthesize(r);
    return {};
}

// ================================================================== Mes sons ===
void setUserFolder(std::string folder) {
    folderRef() = std::move(folder);
    ++revisionRef();
}

const std::string& userFolder() { return folderRef(); }

std::uint64_t userRevision() noexcept { return revisionRef().load(); }

std::vector<Sound> userSounds() {
    std::vector<Sound> out;
    const std::string& folder = folderRef();
    if (folder.empty()) return out;
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(P(folder), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        if (kindOfFormat(formatFromExtension(U(it->path().filename()))) != MediaKind::Sound) continue;
        files.push_back(it->path());
    }
    // Par nom (sans l'extension : << Mon bip >> avant << Mon bip (2) >>).
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        const std::string sa = guide::fold(stemExt(U(a.filename())).first), sb = guide::fold(stemExt(U(b.filename())).first);
        return sa != sb ? sa < sb : guide::fold(U(a.filename())) < guide::fold(U(b.filename()));
    });
    for (const auto& f : files) {
        auto bytes = std::make_shared<Bytes>();
        if (!readAll(f, *bytes) || bytes->empty()) continue;
        const std::string name = U(f.filename());
        const MediaInfo m = inspectMedia(*bytes, name);
        if (m.kind != MediaKind::Sound || !m.ok()) continue;
        Sound s;
        s.id = "mes/" + name;
        s.label = stemExt(name).first;
        s.category = std::string(kMine);
        s.format = m.format;
        s.seconds = m.seconds;
        s.file = U(f);
        s.description = "Un de tes sons (" + name + ").";
        s.data = std::move(bytes);
        out.push_back(std::move(s));
    }
    return out;
}

bool addUserSound(const std::string& name, const Bytes& data, std::string* stored, bool* existed, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (existed) *existed = false;
    const std::string& folder = folderRef();
    if (folder.empty()) return fail("pas de dossier pour Mes sons");
    const std::string clean = safeFileName(name);
    const MediaInfo m = inspectMedia(data, clean);
    if (m.kind != MediaKind::Sound || !m.ok())
        return fail(clean + " n'est pas un son (WAV ou MP3)" + (m.ok() ? std::string{} : " : " + m.error));
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    for (const auto& s : userSounds())
        if (s.data && *s.data == data) {
            if (existed) *existed = true;
            if (stored) *stored = U(P(s.file).filename());
            return true;
        }
    auto [stem, ext] = stemExt(clean);
    if (ext.empty()) {
        ext = "." + m.format;
        for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::string leaf = stem + ext;
    for (int k = 2; fs::exists(P(folder) / P(leaf), ec) && k < 1000; ++k) leaf = stem + " (" + std::to_string(k) + ")" + ext;
    std::ofstream outFile(P(folder) / P(leaf), std::ios::binary);
    if (!outFile) return fail("\xC3\xA9" "criture impossible dans " + folder);
    outFile.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!outFile) return fail("\xC3\xA9" "criture impossible dans " + folder);
    outFile.close();
    ++revisionRef();
    if (stored) *stored = leaf;
    return true;
}

bool addUserFile(const std::string& path, std::string* stored, bool* existed, std::string* why) {
    Bytes data;
    if (!readAll(P(path), data) || data.empty()) {
        if (why) *why = "fichier illisible : " + path;
        return false;
    }
    return addUserSound(U(P(path).filename()), data, stored, existed, why);
}

bool removeUserSound(const Sound& s, std::string* why) {
    if (!s.mine()) {
        if (why) *why = "un son natif se garde (seuls Mes sons se retirent)";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(P(s.file), ec) || ec) {
        if (why) *why = "suppression impossible : " + s.file;
        return false;
    }
    ++revisionRef();
    return true;
}

// ================================================================== chercher, importer ===
bool matches(const Sound& s, std::string_view query) {
    const std::string hay = " " + guide::fold(s.label + " " + s.category + " " + s.words + " " + s.description + " " + s.id) + " ";
    std::string word;
    const std::string q = guide::fold(query);
    for (std::size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') {
            if (!word.empty()) {
                // Chaque mot cherche commence un mot (apres un blanc, un tiret, une parenthese).
                bool found = false;
                for (std::size_t at = hay.find(word); at != std::string::npos && !found; at = hay.find(word, at + 1)) {
                    const char before = at == 0 ? ' ' : hay[at - 1];
                    found = before == ' ' || before == '-' || before == '(' || before == '/' || before == '\'';
                }
                if (!found) return false;
            }
            word.clear();
        } else {
            word += q[i];
        }
    }
    return true;
}

std::string resourceName(const Sound& s) {
    if (s.mine()) return U(P(s.file).filename());
    return s.id + ".wav";
}

const Resource* importedAs(const Project& p, const Sound& s) {
    if (!s.data) return nullptr;
    for (const auto& r : p.assets.resources)
        if (r.data && (r.data == s.data || *r.data == *s.data)) return &r;
    return nullptr;
}

Imported importSound(Project& p, const Sound& s) {
    Imported out;
    if (!s.data) return out;
    if (const Resource* r = importedAs(p, s)) {
        out.id = r->id;
        out.name = r->name;
        out.reused = true;
        return out;
    }
    Resource r = makeResource(p, resourceName(s), s.data,
                              s.mine() ? s.file : "Biblioth\xC3\xA8que de sons \xE2\x80\xBA " + s.category + " \xE2\x80\xBA " + s.label);
    out.id = r.id;
    out.name = r.name;
    p.assets.resources.push_back(std::move(r));
    return out;
}

std::string durationText(double seconds) {
    char b[32];
    if (seconds < 10) std::snprintf(b, sizeof b, "%.2f s", seconds);
    else std::snprintf(b, sizeof b, "%.1f s", seconds);
    std::string t = b;
    // "1.40 s" -> "1,4 s" ; "0.08 s" -> "0,08 s".
    if (const auto dot = t.find('.'); dot != std::string::npos) {
        t[dot] = ',';
        auto end = t.find(' ');
        while (end > dot + 2 && t[end - 1] == '0') {
            t.erase(end - 1, 1);
            --end;
        }
    }
    return t;
}

} // namespace hmi::soundlib
