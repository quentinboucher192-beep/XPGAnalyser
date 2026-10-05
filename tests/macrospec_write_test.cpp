// tests/macrospec_write_test.cpp - lot API 6 : ecrire les lignes "#!" d'une macro.
//
//   macrospec_write_test <dossier libs/Macros>
//
// Les 31 macros relues puis reecrites sans changement : identiques a l'octet.
// Une question deplacee ne change que sa ligne "#! groupe" ; une question
// ajoutee ecrit son "#! champ", son "#! libelle" et son Ask ; Enregistrer monte
// la version et ajoute sa ligne "#! changes".
#include "../src/project/MacroSpec.hpp"
#include "../src/project/MacroSpecWriter.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace project::macro;

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

static std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::vector<std::string> linesOfText(const std::string& s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto nl = s.find('\n', from);
        out.push_back(s.substr(from, (nl == std::string::npos ? s.size() : nl) - from));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return out;
}

// Les lignes qui different, comme un diff les compte : une ligne inseree en
// decale beaucoup d'autres, qui ne different pas pour autant. La plus longue
// sous-suite commune (LCS), puis ce qui n'y est pas, du cote le plus long.
static std::size_t differingLines(const std::string& a, const std::string& b) {
    const auto la = linesOfText(a), lb = linesOfText(b);
    std::vector<std::vector<std::size_t>> t(la.size() + 1, std::vector<std::size_t>(lb.size() + 1, 0));
    for (std::size_t i = la.size(); i-- > 0;)
        for (std::size_t j = lb.size(); j-- > 0;)
            t[i][j] = la[i] == lb[j] ? t[i + 1][j + 1] + 1 : std::max(t[i + 1][j], t[i][j + 1]);
    return std::max(la.size(), lb.size()) - t[0][0];
}

static std::string keysOf(const GroupSpec* g) {
    if (!g) return "(pas de groupe)";
    std::string out;
    for (const auto& k : g->keys) out += (out.empty() ? "" : ", ") + k;
    return out;
}

static const GroupSpec* groupNamed(const MacroSpec& s, const std::string& name) {
    for (const auto& g : s.groups)
        if (g.name == name) return &g;
    return nullptr;
}

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path("libs/Macros");
    std::printf("macrospec_write_test : %s\n", dir.string().c_str());

    // ---- 1. les 31 macros, reecrites sans changement ----------------------------
    std::size_t macros = 0, labels = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".mac") continue;
        ++macros;
        const auto src = readFile(e.path());
        const auto spec = parseMacroSpec(src);
        check(writeGroups(src, spec.groups, spec.advanced) == src, e.path().filename().string() + " : groupes et avances reecrits a l'identique");
        for (const auto& h : headerLines(src)) {
            if (h.word != "libelle" || h.key.empty()) continue;
            ++labels;
            check(setKeyLine(src, "libelle", h.key, h.value) == src, e.path().filename().string() + " : #! libelle " + h.key + " reecrit a l'identique");
        }
        // Deplacer une question a sa place : rien ne change non plus.
        for (const auto& g : spec.groups)
            for (std::size_t i = 0; i < g.keys.size(); ++i)
                check(moveField(src, g.keys[i], g.name, i) == src, e.path().filename().string() + " : " + g.keys[i] + " remis a sa place");
    }
    std::printf("  %zu macros, %zu libelles\n", macros, labels);
    check(macros >= 31, "les 31 macros de libs/Macros sont la (" + std::to_string(macros) + ")");

    // ---- 2. ImporterAlarmes : deplacer, ajouter, enregistrer ------------------------
    const auto src = readFile(dir / "ImporterAlarmes.mac");
    check(!src.empty(), "ImporterAlarmes.mac lu");
    const auto spec = parseMacroSpec(src);
    const auto* sections = groupNamed(spec, "Sections");
    check(sections && sections->keys == std::vector<std::string>{"sInit", "sCycle", "tache"}, "Sections = sInit, sCycle, tache");

    const auto moved = moveField(src, "tache", "Sections", 0);
    const auto spec2 = parseMacroSpec(moved);
    const auto* s2 = groupNamed(spec2, "Sections");
    check(s2 && s2->keys == std::vector<std::string>{"tache", "sInit", "sCycle"}, "tache en tete de Sections");
    check(differingLines(src, moved) == 1, "une seule ligne change (#! groupe Sections) : " + std::to_string(differingLines(src, moved)));
    check(moved.find("   #! groupe Sections = tache, sInit, sCycle") != std::string::npos, "la ligne garde son retrait");

    const auto into = moveField(src, "classeur", "Sections", 1);
    const auto spec3 = parseMacroSpec(into);           // garde : s3 pointe dedans
    const auto* s3 = groupNamed(spec3, "Sections");
    check(s3 && s3->keys == std::vector<std::string>{"sInit", "classeur", "sCycle", "tache"}, "classeur entre dans Sections, en 2e : " + keysOf(s3));

    const auto out = moveField(src, "tache", "", 0);
    const auto spec4 = parseMacroSpec(out);
    const auto* s4 = groupNamed(spec4, "Sections");
    check(s4 && s4->keys == std::vector<std::string>{"sInit", "sCycle"}, "tache sort de Sections");
    check(spec4.field("tache") && spec4.field("tache")->group.empty(), "tache : sans groupe");

    const auto fresh = moveField(src, "classeur", "Le classeur", 0);
    const auto spec5 = parseMacroSpec(fresh);
    check(groupNamed(spec5, "Le classeur") && groupNamed(spec5, "Le classeur")->keys == std::vector<std::string>{"classeur"}, "un groupe nouveau : sa ligne");
    check(differingLines(src, fresh) == 1, "une ligne ajoutee");

    const auto emptied = moveField(moveField(moveField(src, "sInit", "", 0), "sCycle", "", 0), "tache", "", 0);
    check(!groupNamed(parseMacroSpec(emptied), "Sections"), "un groupe vide : sa ligne s'en va");

    const auto adv = setAdvanced(src, "classeur", true);
    check(parseMacroSpec(adv).isAdvanced("classeur"), "classeur passe dans les reglages avances");
    check(differingLines(src, adv) == 1, "la seule ligne #! avance change");
    check(setAdvanced(src, "registre", true) == src, "registre y est deja : rien ne change");
    const auto notAdv = setAdvanced(src, "registre", false);
    check(!parseMacroSpec(notAdv).isAdvanced("registre"), "registre sort des reglages avances");

    const auto relabel = setKeyLine(src, "libelle", "tache", "La t\xC3\xA2" "che qui appelle");
    check(parseMacroSpec(relabel).field("tache")->label == "La t\xC3\xA2" "che qui appelle", "le libelle change");
    check(differingLines(src, relabel) == 1, "le libelle : une ligne");
    const auto kind = setKeyLine(src, "champ", "tache", "tache facultatif");
    check(parseMacroSpec(kind).field("tache")->optional, "le genre change : facultatif");
    const auto help = setHelp(src, "tache", "La t\xC3\xA2" "che des sections.");
    check(parseMacroSpec(help).field("tache")->help.rfind("La t\xC3\xA2" "che des sections.", 0) == 0, "l'aide : la premiere ligne #! param");

    const auto added = addField(src, "prefixe", "nom", "Pr\xC3\xA9" "fixe des alarmes", "ALM_");
    const auto spec6 = parseMacroSpec(added);
    const auto* pf = spec6.field("prefixe");
    check(pf && pf->declared && pf->kind == FieldKind::Name, "la question ajoutee est declaree (nom)");
    check(pf && pf->label == "Pr\xC3\xA9" "fixe des alarmes", "son libelle (accents compris)");
    check(pf && pf->askPreset == "ALM_", "sa valeur par defaut (l'appel Ask)");
    check(added.find("prefixe := Ask('prefixe', 'Prefixe des alarmes', 'ALM_');") != std::string::npos, "l'appel Ask, en ASCII");
    const auto askAt = added.find("prefixe := Ask("), tacheAt = added.find("tache    := Ask(");
    check(askAt != std::string::npos && tacheAt != std::string::npos && askAt > tacheAt, "apres le dernier Ask");
    check(addField(added, "prefixe", "nom", "x", "y") == added, "deux fois la meme cle : rien");

    check(nextVersion("1.10") == "1.11" && nextVersion("1.9") == "1.10" && nextVersion("2") == "2.1" && nextVersion("") == "1.0"
              && nextVersion("1.10.3") == "1.10.4",
          "nextVersion : 1.10 -> 1.11, 1.9 -> 1.10, 2 -> 2.1, vide -> 1.0");
    const auto saved = bumpVersion(src, "1.11", "Les sections d'abord dans le formulaire");
    const auto spec7 = parseMacroSpec(saved);
    check(spec7.version == "1.11", "la version monte : 1.11");
    std::size_t c111 = 0, c110 = 0;
    for (const auto& h : headerLines(saved)) {
        if (h.word == "changes" && h.key == "1.11") c111 = h.line;
        if (h.word == "changes" && h.key == "1.10") c110 = h.line;
    }
    check(c111 > 0 && c110 > c111, "#! changes 1.11 avant #! changes 1.10");
    check(differingLines(src, saved) == 2, "deux lignes : la version, et la nouvelle ligne");
    check(bumpVersion(saved, "1.11", "Les sections d'abord dans le formulaire") == saved, "enregistrer deux fois la meme : rien de plus");

    const auto lines = linesOf(src, "tache");
    const auto has = [&](std::size_t n) { return std::find(lines.begin(), lines.end(), n) != lines.end(); };
    check(has(87) && has(88) && has(89) && has(107) && has(65), "les lignes de tache : #! champ 88, #! libelle 89, #! groupe 90, Ask 108, #! param 66");

    // ---- 3. les fins de ligne Windows restent ------------------------------------------
    std::string crlf;
    for (const char c : src) {
        if (c == '\n') crlf += "\r\n";
        else crlf += c;
    }
    const auto movedCrlf = moveField(crlf, "tache", "Sections", 0);
    std::size_t lf = 0;
    for (std::size_t i = 0; i < movedCrlf.size(); ++i)
        if (movedCrlf[i] == '\n' && (i == 0 || movedCrlf[i - 1] != '\r')) ++lf;
    check(lf == 0 && differingLines(crlf, movedCrlf) == 1, "CRLF : gardees, une ligne change");
    const auto addedCrlf = addField(crlf, "prefixe", "nom", "Pr\xC3\xA9" "fixe", "");
    lf = 0;
    for (std::size_t i = 0; i < addedCrlf.size(); ++i)
        if (addedCrlf[i] == '\n' && (i == 0 || addedCrlf[i - 1] != '\r')) ++lf;
    check(lf == 0, "CRLF : les lignes ajoutees aussi");

    std::printf("%d verifications, %d echecs\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
