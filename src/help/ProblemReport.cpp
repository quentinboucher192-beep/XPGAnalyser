// =============================================================================
//  help/ProblemReport.cpp - 1.11 (chantier T2) : Signaler un probleme
// =============================================================================
#include "ProblemReport.hpp"

#include "../export/DocKit.hpp"

#include <cstdint>
#include <fstream>
#include <system_error>

#include <cstdio>

namespace help::report {

namespace {

constexpr const char* kEllipsis = "\xE2\x80\xA6";

bool blank(std::string_view s) {
    for (const char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    return true;
}

void count(std::string& out, int n, const char* one, const char* many) {
    if (!out.empty() && out.back() != ' ') out += ", ";
    out += std::to_string(n);
    out += ' ';
    out += n > 1 ? many : one;
}

void section(std::string& out, const char* title, const std::string& body) {
    out += title;
    out += '\n';
    out += blank(body) ? std::string("(non rempli)") : body;
    out += "\n\n";
}

} // namespace

bool canPrepare(const Form& f) { return !blank(f.what); }

std::string projectSummary(const ProjectCounts& p) {
    if (!p.open) return "Aucun projet ouvert.";
    std::string out = "Automate : ";
    count(out, p.sections, "section", "sections");
    count(out, p.dfbs, "DFB", "DFB");
    count(out, p.ddts, "DDT", "DDT");
    count(out, p.variables, "variable", "variables");
    out += ". IHM : ";
    count(out, p.views, "vue", "vues");
    count(out, p.objects, "objet", "objets");
    count(out, p.scripts, "script", "scripts");
    count(out, p.alarms, "alarme", "alarmes");
    out += '.';
    return out;
}

std::string scrubLine(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        // Un texte entre apostrophes ou guillemets : sa valeur part.
        if (c == '\'' || c == '"') {
            const auto end = line.find(c, i + 1);
            if (end != std::string_view::npos) {
                out += c; out += kEllipsis; out += c;
                i = end;
                continue;
            }
        }
        // Tranche 8 : un chemin part aussi (il peut porter le nom de l'utilisateur) :
        // un mot qui a une lettre et un separateur, et qui commence par "/", "~", "\"
        // ou une lettre de lecteur ("C:"), ou qui a deux separateurs ou plus. Une
        // date (02/10/2026, sans lettre) et "1/2" restent.
        if (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t') {
            std::size_t end = i;
            int seps = 0;
            bool letter = false;
            while (end < line.size() && line[end] != ' ' && line[end] != '\t') {
                const char d = line[end];
                if (d == '/' || d == '\\') ++seps;
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z')) letter = true;
                ++end;
            }
            const std::string_view word = line.substr(i, end - i);
            const bool drive = word.size() >= 3 && word[1] == ':' && (word[2] == '\\' || word[2] == '/');
            const bool rooted = !word.empty() && (word[0] == '/' || word[0] == '~' || word[0] == '\\' || drive);
            if (!word.empty() && seps > 0 && letter && (rooted || seps >= 2)) {
                out += kEllipsis;
                i = end - 1;
                continue;
            }
        }
        // Une affectation ou un reglage : "x := 12", "Pos=42.5" -> la valeur part.
        const bool assign = (c == ':' && i + 1 < line.size() && line[i + 1] == '=')
                         || (c == '=' && (i == 0 || (line[i - 1] != '<' && line[i - 1] != '>' && line[i - 1] != '!'
                                                     && line[i - 1] != '=' && line[i - 1] != ':'))
                             && (i + 1 >= line.size() || line[i + 1] != '='));
        if (assign) {
            if (c == ':') { out += ":="; ++i; } else out += '=';
            std::size_t j = i + 1;
            while (j < line.size() && line[j] == ' ') out += line[j++];
            std::size_t k = j;
            while (k < line.size() && line[k] != ';' && line[k] != ',') ++k;   // toute l'expression
            while (k > j && line[k - 1] == ' ') --k;
            if (k > j) out += kEllipsis;
            i = k - 1;
            continue;
        }
        out += c;
    }
    return out;
}

namespace {
// Recette 1.11 (T2, tranche 15) : une ligne du journal sans son heure (le premier
// mot) ni sa duree finale (" (564 us) ") : deux lignes de meme signature disent la
// meme chose.
std::string signatureOf(std::string_view line) {
    const std::size_t b = line.find(' ');
    std::string_view s = b == std::string_view::npos ? line : line.substr(b);
    if (!s.empty() && s.back() == ')')
        if (const auto open = s.rfind('('); open != std::string_view::npos) s = s.substr(0, open);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return std::string(s);
}
} // namespace

// Recette 1.11 (T2, tranche 15) : une IHM en marche ecrit une entree et une sortie
// de script a chaque cycle (" > script IHM : script Anime ", " < script IHM (564 us) ") ;
// le journal joint n'avait plus que cela. Une ligne repetee (ou une paire de lignes
// repetee) n'est gardee qu'une fois, avec " (xN) " ; puis les " last " dernieres.
std::vector<std::string> filterLog(const std::vector<std::string>& lines, std::size_t last) {
    struct Run { const std::string* line; std::string sig; std::size_t times; };
    std::vector<Run> runs;
    bool pairOpen = false;    // la 1re ligne d'une paire repetee vient d'etre comptee
    for (const auto& l : lines) {
        std::string sig = signatureOf(l);
        const std::size_t n = runs.size();
        if (pairOpen) {
            pairOpen = false;
            if (runs[n - 1].sig == sig) { ++runs[n - 1].times; continue; }
        }
        if (n >= 1 && runs[n - 1].sig == sig) { ++runs[n - 1].times; continue; }
        if (n >= 2 && runs[n - 2].sig == sig && runs[n - 2].times == runs[n - 1].times) {
            ++runs[n - 2].times;
            pairOpen = true;
            continue;
        }
        runs.push_back(Run{&l, std::move(sig), 1});
    }
    std::vector<std::string> out;
    const std::size_t from = runs.size() > last ? runs.size() - last : 0;
    for (std::size_t i = from; i < runs.size(); ++i) {
        std::string line = scrubLine(*runs[i].line);
        if (runs[i].times > 1) line += "  (\xC3\x97" + std::to_string(runs[i].times) + ")";
        out.push_back(std::move(line));
    }
    return out;
}

std::string text(const Form& f, const Facts& facts) {
    std::string out = "Signaler un probl\xC3\xA8me \xE2\x80\x94 XPGAnalyser ";
    out += facts.version.empty() ? std::string("?") : facts.version;
    out += "\n\n";
    section(out, "Ce qui s'est pass\xC3\xA9", f.what);
    section(out, "Comment le refaire", f.howTo);
    section(out, "Ce que tu attendais", f.expected);
    out += "Joint\n";
    if (f.withVersion) {
        out += "- Version : " + facts.version;
        if (!facts.commit.empty()) out += " (commit " + facts.commit + ")";
        out += '\n';
    }
    if (f.withSystem) out += "- Syst\xC3\xA8me : " + facts.system + '\n';
    if (f.withProject) out += "- Projet, sans ses donn\xC3\xA9" "es : " + projectSummary(facts.project) + '\n';
    if (f.withLog) {
        const auto n = filterLog(facts.log).size();
        out += "- Journal : " + std::to_string(n) + (n > 1 ? " lignes" : " ligne") + ", sans les valeurs (journal.txt)\n";
    }
    if (!f.screenshotPng.empty()) out += "- Capture : " + f.screenshotPng + '\n';
    if (!f.attachedName.empty()) out += "- Rapport : " + f.attachedName + '\n';
    if (!f.withVersion && !f.withSystem && !f.withProject && !f.withLog && f.screenshotPng.empty()
        && f.attachedName.empty())
        out += "- rien\n";
    return out;
}

std::string zipName(int year, int month, int day, int hour, int minute) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "signalement-%04d-%02d-%02d-%02d%02d.zip", year, month, day, hour, minute);
    return buf;
}

std::vector<std::pair<std::string, std::string>> zipItems(const Form& f, const Facts& facts,
                                                          const std::string& screenshotBytes) {
    std::vector<std::pair<std::string, std::string>> items;
    items.emplace_back("signalement.txt", text(f, facts));
    if (!f.screenshotPng.empty() && !screenshotBytes.empty()) items.emplace_back(f.screenshotPng, screenshotBytes);
    if (!f.attachedName.empty()) items.emplace_back(f.attachedName, f.attachedText);
    if (f.withLog) {
        std::string log;
        for (const auto& l : filterLog(facts.log)) { log += l; log += '\n'; }
        items.emplace_back("journal.txt", log);
    }
    return items;
}

namespace {
// Recette 1.11 (T2-8) : exporter::doc::zip date tous ses fichiers du 01/01/2026 a 0 h
// (un meme contenu, un meme fichier : les .docx et .xlsx en ont besoin). Le zip de
// Signaler prend la date et l'heure du signalement : dans chaque en-tete local
// (PK 03 04 : l'heure a +10, la date a +12) et du repertoire central (PK 01 02 :
// +12, +14). Le format DOS : l'heure en 5-6-5 bits (les secondes / 2), la date en
// 7-4-5 bits (l'annee depuis 1980).
void stampZip(exporter::doc::Bytes& z, const Stamp& w) {
    if (w.year < 1980 || w.month < 1 || w.month > 12 || w.day < 1 || w.day > 31) return;
    const auto time = static_cast<std::uint16_t>(((w.hour & 31) << 11) | ((w.minute & 63) << 5));
    const auto date = static_cast<std::uint16_t>((((w.year - 1980) & 127) << 9) | (w.month << 5) | w.day);
    const auto u16 = [&](std::size_t at) { return static_cast<std::size_t>(z[at] | (z[at + 1] << 8)); };
    const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
    const auto put = [&](std::size_t at, std::uint16_t v) {
        z[at] = static_cast<std::uint8_t>(v & 0xFF);
        z[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    std::size_t at = 0;
    while (at + 30 <= z.size() && u32(at) == 0x04034B50u) {           // les en-tetes locaux
        put(at + 10, time);
        put(at + 12, date);
        at += 30 + u16(at + 26) + u16(at + 28) + u32(at + 18);
    }
    while (at + 46 <= z.size() && u32(at) == 0x02014B50u) {           // le repertoire central
        put(at + 12, time);
        put(at + 14, date);
        at += 46 + u16(at + 28) + u16(at + 30) + u16(at + 32);
    }
}
} // namespace

std::filesystem::path writeZip(const std::filesystem::path& dataDir, const Form& form, const Facts& facts,
                               const std::string& screenshotBytes, const Stamp& when, std::string* why) {
    namespace fs = std::filesystem;
    const auto fail = [&](std::string msg) {
        if (why != nullptr) *why = std::move(msg);
        return fs::path{};
    };
    if (!canPrepare(form)) return fail("Dis d'abord ce qui s'est pass\xC3\xA9.");
    std::error_code ec;
    const fs::path dir = dataDir / "signalements";
    fs::create_directories(dir, ec);
    if (ec) return fail("Le dossier " + dir.string() + " ne peut pas \xC3\xAAtre cr\xC3\xA9\xC3\xA9 : " + ec.message());
    const std::string name = zipName(when.year, when.month, when.day, when.hour, when.minute);
    fs::path out = dir / name;
    const std::string stem = name.substr(0, name.size() - 4);   // sans ".zip"
    for (int n = 2; fs::exists(out, ec) && n < 100; ++n) out = dir / (stem + "-" + std::to_string(n) + ".zip");
    auto bytes = exporter::doc::zip(zipItems(form, facts, screenshotBytes));
    stampZip(bytes, when);   // recette 1.11 (T2-8) : les fichiers a l'heure du signalement, pas au 01/01/2026
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return fail("Le fichier " + out.string() + " ne peut pas \xC3\xAAtre \xC3\xA9" "crit.");
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) return fail("L'\xC3\xA9" "criture de " + out.string() + " a \xC3\xA9" "chou\xC3\xA9" "e.");
    return out;
}

} // namespace help::report
