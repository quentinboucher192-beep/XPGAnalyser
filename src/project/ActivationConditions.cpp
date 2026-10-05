// =============================================================================
//  project/ActivationConditions.cpp - 1.11 (R111) : les conditions d'activation
//  manquantes des projets importes avant la 1.8.0 (voir le .hpp)
// =============================================================================
#include "ActivationConditions.hpp"
#include "ProjectStore.hpp"

namespace project {

namespace {

bool blank(std::string_view s) {
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    return true;
}

bool hasCondition(const domain::Project& p, const domain::Section& s) {
    return (s.activationCondition != 0 && !blank(p.strings.text(s.activationCondition)))
        || (s.logicCondition != 0 && !blank(p.strings.text(s.logicCondition)));
}

} // namespace

MissingConditions missingTaskConditions(const domain::Project& p, const Manifest& m) {
    MissingConditions out;
    // Un projet importe d'un .XPG : son product est celui du fichier d'origine
    // (« Control Expert V15.3 - 230214C ») ; un projet cree ici dit XpgAnalyzer.
    const bool imported = !blank(m.product) && m.product.rfind("XpgAnalyzer", 0) != 0;
    // Importe avant la 1.8.0 : son dossier a ete cree avant sa livraison
    // ("AAAA-MM-JJ hh:mm:ss" se compare comme du texte). Sans date : un projet ancien.
    const bool before180 = m.created.empty() || m.created < kConditionsSince;
    bool anyTaskCondition = false;
    for (const auto& s : p.sections) {
        if (s.isSubroutine) continue;
        // Une section de tache : a elle seule (le magasin lui donne un Pou de genre
        // Section) ; une section d'unite : dans un Pou ProgramUnit ; d'un DFB : ni l'un ni l'autre.
        const bool taskSection = s.owner == domain::kNoIndex
                              || (s.owner < p.pous.size() && p.pous[s.owner].kind == domain::PouKind::Section);
        if (taskSection) {
            if (hasCondition(p, s)) anyTaskCondition = true;
            else out.sections.emplace_back(p.strings.text(s.name));
        } else if (s.owner < p.pous.size() && p.pous[s.owner].kind == domain::PouKind::ProgramUnit && hasCondition(p, s)) {
            ++out.unitConditions;
        }
    }
    out.suspected = imported && before180 && !anyTaskCondition && !out.sections.empty();
    if (!out.suspected) {
        out.sections.clear();
        return out;
    }
    out.message = "Les sections de t\xC3\xA2" "che de ce projet n'ont pas leurs conditions d'activation (projet import\xC3\xA9 avant la 1.8.0) : "
                  "en simulation, elles tournent \xC3\xA0 chaque cycle. Fichier \xE2\x80\xBA Importer un .XPG (nouveau MAST)\xE2\x80\xA6 les r\xC3\xA9tablit.";
    return out;
}

} // namespace project
