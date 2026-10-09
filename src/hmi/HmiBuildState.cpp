// =============================================================================
//  hmi/HmiBuildState.cpp - 1.11 (C4) : voir HmiBuildState.hpp
// =============================================================================
#include "HmiBuildState.hpp"
#include "HmiDecl.hpp"   // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites

#include "HmiEnums.hpp"
#include "HmiTypes.hpp"
#include "../sim/Interpreter.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <vector>

namespace hmi::build {

namespace {

std::string langName(domain::PouLanguage l) {
    const auto s = domain::toString(l);
    return s.empty() ? std::string("?") : std::string(s);
}

// Une section ST : ce que fait le simulateur (sim::parse, le ST de l'automate).
State parseSt(const std::string& body, const std::string& name) {
    State st;
    auto program = sim::parse(body, name);
    if (program) {
        st.compile = Compile::Ok;
        st.reason  = "Compile : ST, sans erreur ; le simulateur l\xE2\x80\x99" "ex\xC3\xA9" "cute.";
        return st;
    }
    st.compile    = Compile::Error;
    st.errors     = 1;                      // le simulateur s'arrete a la premiere
    st.simMessage = program.error().message();   // tel que le Journal le cite (Runtime::prepare)
    // Comme checkScript : le contexte porte « line 4: expected END_IF » ; la ligne a part, le reste en francais.
    const std::string raw = program.error().context.empty() ? st.simMessage : program.error().context;
    std::string rest;
    st.firstLine  = splitLine(raw, &rest);
    const auto fr = frenchSimMessage(rest);
    st.reason = "En erreur" + (st.firstLine > 0 ? " (ligne " + std::to_string(st.firstLine) + ")" : std::string())
              + " : " + (fr.empty() ? st.simMessage : fr) + " ; la simulation saute cette section.";
    return st;
}

int rank(Compile c) noexcept {   // le pire d'abord : Error > NotCompilable > Ok
    switch (c) {
        case Compile::Error:         return 2;
        case Compile::NotCompilable: return 1;
        case Compile::Ok:            return 0;
    }
    return 0;
}

} // namespace

State ofSection(const domain::Project& p, const domain::Section& s) {
    const auto name = std::string(p.strings.text(s.name));
    if (s.language != domain::PouLanguage::ST) {
        State st;
        st.compile    = Compile::NotCompilable;
        st.simMessage = "section '" + name + "' is " + std::string(domain::toString(s.language))
                      + "; this simulator runs Structured Text only, so it will not be executed";
        st.reason     = "Non compilable : " + langName(s.language)
                      + " ; le simulateur n\xE2\x80\x99" "ex\xC3\xA9" "cute que le ST, cette section n\xE2\x80\x99" "est pas ex\xC3\xA9" "cut\xC3\xA9" "e.";
        return st;
    }
    return parseSt(s.body, name);
}

State ofDfbSection(const domain::Project& p, const domain::Pou& dfb, const domain::Section& s) {
    const auto typeName = std::string(p.strings.text(dfb.name));
    const auto bodyName = typeName + "." + std::string(p.strings.text(s.name));
    if (s.language != domain::PouLanguage::ST) {
        State st;
        st.compile    = Compile::NotCompilable;
        st.simMessage = "the body of DFB '" + typeName + "' is " + std::string(domain::toString(s.language))
                      + "; instances of it will store their inputs but do nothing";
        st.reason     = "Non compilable : un corps " + langName(s.language)
                      + " ; ses instances gardent leurs entr\xC3\xA9" "es mais ne font rien en simulation.";
        return st;
    }
    return parseSt(s.body, bodyName);
}

State ofDfb(const domain::Project& p, const domain::Pou& dfb) {
    State worst;
    worst.reason = "Compile : ST, sans erreur ; le simulateur l\xE2\x80\x99" "ex\xC3\xA9" "cute.";
    int errors = 0;
    for (auto si : dfb.sections) {
        if (si >= p.sections.size()) continue;
        auto st = ofDfbSection(p, dfb, p.sections[si]);
        errors += st.errors;
        if (rank(st.compile) > rank(worst.compile)) worst = std::move(st);
    }
    worst.errors = errors;
    return worst;
}

TypeKnown knownTypesOf(const Project& p) {
    // Comme Compiler (HmiCheck.cpp) et l'editeur (HmiScriptPanes.cpp) : une
    // structure ou une enumeration du projet est un type permis.
    return [&p](std::string_view t) { return !types::membersOf(p, t).empty() || findEnumeration(p, t) != nullptr; };
}

State ofScript(const Script& sc, const TypeKnown& knownType) {
    State st;
    const auto diags = checkScript(sc, knownType);                 // 1.11.18 (lot 3) : avec ses declarations du modele
    const ScriptDiagnostic* first = nullptr;
    for (const auto& d : diags)
        if (d.severity == ScriptDiagnostic::Severity::Error) {
            ++st.errors;
            if (!first) first = &d;
        }
    if (first) st.firstLine = first->line;
    if (sc.lang != ScriptLang::ST) {
        const auto lang = std::string(scriptLangKey(sc.lang));
        st.compile    = Compile::NotCompilable;
        st.generate   = Generate::No;
        st.simMessage = lang + " : \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9, non ex\xC3\xA9" "cut\xC3\xA9 en simulation";
        st.reason     = "Non compilable : " + lang + ", relu (accolades, parenth\xC3\xA8ses, cha\xC3\xAEnes) mais jamais ex\xC3\xA9" "cut\xC3\xA9";
        if (first) st.reason += " ; " + std::to_string(st.errors) + " faute" + (st.errors > 1 ? "s" : "")
                              + (first->line > 0 ? " (ligne " + std::to_string(first->line) + ")" : std::string())
                              + " : " + first->message;
        st.reason += ".";
        return st;
    }
    if (first) {
        st.compile  = Compile::Error;
        st.generate = Generate::No;
        st.simMessage = first->message;
        st.reason = "En erreur" + (first->line > 0 ? " (ligne " + std::to_string(first->line) + ")" : std::string())
                  + " : " + first->message + ".";
        return st;
    }
    st.compile  = Compile::Ok;
    st.generate = Generate::Yes;
    st.reason   = "Compile : ST, sans erreur ; le simulateur l\xE2\x80\x99" "ex\xC3\xA9" "cute.";
    return st;
}

std::string_view compileGlyph(Compile c) noexcept {
    switch (c) {
        case Compile::Ok:            return "\xE2\x9C\x93";   // ✓
        case Compile::Error:         return "\xE2\x9C\x95";   // ✕
        case Compile::NotCompilable: return "\xE2\x8A\x98";   // ⊘
    }
    return {};
}

std::string_view generateGlyph(Generate g) noexcept {
    return g == Generate::None ? std::string_view{} : std::string_view{"\xE2\x87\xA9"};   // ⇩
}

std::string tooltip(const State& st) {
    std::string t = st.reason;
    if (st.generate == Generate::Yes)
        t += "\nG\xC3\xA9" "n\xC3\xA9r\xC3\xA9 : le script part avec G\xC3\xA9n\xC3\xA9rer ; il s\xE2\x80\x99" "ex\xC3\xA9" "cute en simulation et sur le poste.";
    else if (st.generate == Generate::No)
        t += "\nNon g\xC3\xA9n\xC3\xA9r\xC3\xA9 : G\xC3\xA9n\xC3\xA9rer refuse ce qui ne tournera pas.";
    return t;
}

const std::vector<LegendRow>& legend() {
    static const std::vector<LegendRow> rows{
        {"\xE2\x9C\x93", "Compile", "ST, sans erreur ; le simulateur l\xE2\x80\x99" "ex\xC3\xA9" "cute."},
        {"\xE2\x9C\x95", "En erreur", "avec le nombre d\xE2\x80\x99" "erreurs ; la premi\xC3\xA8re est dans l\xE2\x80\x99" "infobulle ; la simulation saute la section."},
        {"\xE2\x8A\x98", "Non compilable", "LD, FBD, IL ou SFC pour une section ou un DFB ; C ou C++ pour un script."},
        {"\xE2\x87\xA9", "G\xC3\xA9n\xC3\xA9r\xC3\xA9", "le script part avec G\xC3\xA9n\xC3\xA9rer ; il s\xE2\x80\x99" "ex\xC3\xA9" "cute en simulation et sur le poste."},
        {"\xE2\x87\xA9", "Non g\xC3\xA9n\xC3\xA9r\xC3\xA9", "un script C ou C++, ou en erreur. G\xC3\xA9n\xC3\xA9rer refuse ce qui ne tournera pas.", true},
    };
    return rows;
}

// ---- Cache --------------------------------------------------------------------

void Cache::clear() {
    sections_.clear(); dfbs_.clear(); dfbSections_.clear(); scripts_.clear();
    ++generation_;
}

void Cache::rebuild(const domain::Project* plc, const Project* hmi) {
    clear();
    if (plc) {
        std::vector<bool> inDfb(plc->sections.size(), false);
        for (domain::Index pi = 0; pi < plc->pous.size(); ++pi) {
            const auto& pou = plc->pous[pi];
            if (pou.kind != domain::PouKind::FunctionBlockType) continue;   // comme Runtime::prepare : tous les types DFB
            // Le type resume ses corps (le pire, les erreurs additionnees), sans les relire.
            State worst;
            worst.reason = "Compile : ST, sans erreur ; le simulateur l\xE2\x80\x99" "ex\xC3\xA9" "cute.";
            int errors = 0;
            for (auto si : pou.sections) {
                if (si >= plc->sections.size()) continue;
                inDfb[si] = true;
                auto st = ofDfbSection(*plc, pou, plc->sections[si]);
                ++computations_;
                errors += st.errors;
                if (rank(st.compile) > rank(worst.compile)) worst = st;
                dfbSections_[si] = std::move(st);
            }
            worst.errors = errors;
            dfbs_[pi] = std::move(worst);
        }
        for (domain::Index si = 0; si < plc->sections.size(); ++si) {
            if (inDfb[si]) continue;
            sections_[si] = ofSection(*plc, plc->sections[si]);
            ++computations_;
        }
    }
    if (hmi) syncScripts(*hmi);
    ++generation_;
}

void Cache::syncScripts(const Project& hmi) {
    const auto known = knownTypesOf(hmi);
    std::unordered_set<Id> present;
    for (const auto& sc : hmi.programs.scripts) { updateScript(sc, known); present.insert(sc.id); }
    for (const auto& v : hmi.views)
        for (const auto& sc : v.scripts) { updateScript(sc, known); present.insert(sc.id); }
    // Un script supprime (ou une vue supprimee avec ses scripts) quitte le cache.
    std::vector<Id> gone;
    for (const auto& [id, e] : scripts_)
        if (!present.count(id)) gone.push_back(id);
    for (const auto id : gone) removeScript(id);
}

void Cache::updateScript(const Script& sc, const TypeKnown& knownType) {
    const auto h = std::hash<std::string>{}(decl::codeOf(sc));     // 1.11.18 (lot 3) : ses declarations aussi
    auto it = scripts_.find(sc.id);
    if (it != scripts_.end() && it->second.lang == sc.lang && it->second.bodyHash == h) return;   // rien n'a change
    ScriptEntry e;
    e.state = ofScript(sc, knownType);
    e.lang = sc.lang;
    e.bodyHash = h;
    scripts_[sc.id] = std::move(e);
    ++computations_;
    ++generation_;
}

void Cache::removeScript(Id id) {
    if (scripts_.erase(id)) ++generation_;
}

const State* Cache::section(domain::Index i) const noexcept {
    const auto it = sections_.find(i);
    return it == sections_.end() ? nullptr : &it->second;
}
const State* Cache::dfb(domain::Index i) const noexcept {
    const auto it = dfbs_.find(i);
    return it == dfbs_.end() ? nullptr : &it->second;
}
const State* Cache::dfbSection(domain::Index i) const noexcept {
    const auto it = dfbSections_.find(i);
    return it == dfbSections_.end() ? nullptr : &it->second;
}
const State* Cache::script(Id id) const noexcept {
    const auto it = scripts_.find(id);
    return it == scripts_.end() ? nullptr : &it->second.state;
}

int Cache::notCompiling() const noexcept {
    // Les sections, les corps des DFB (pas le type : il les resume) et les scripts.
    int n = 0;
    for (const auto& [i, s] : sections_)    n += s.compiles() ? 0 : 1;
    for (const auto& [i, s] : dfbSections_) n += s.compiles() ? 0 : 1;
    for (const auto& [i, e] : scripts_)     n += e.state.compiles() ? 0 : 1;
    return n;
}

int Cache::notGenerated() const noexcept {
    int n = 0;
    for (const auto& [i, e] : scripts_) n += e.state.generated() ? 0 : 1;
    return n;
}

std::string Cache::folderSummary(int notCompiling, int notGenerated) {
    std::string s;
    if (notCompiling > 0) s = std::to_string(notCompiling) + " \xE2\x9C\x95/\xE2\x8A\x98";
    if (notGenerated > 0) {
        if (!s.empty()) s += " \xC2\xB7 ";
        s += std::to_string(notGenerated) + (notGenerated > 1 ? " non g\xC3\xA9n\xC3\xA9r\xC3\xA9s" : " non g\xC3\xA9n\xC3\xA9r\xC3\xA9");
    }
    return s;
}

} // namespace hmi::build
