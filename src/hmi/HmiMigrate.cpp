// =============================================================================
//  hmi/HmiMigrate.cpp - 1.11.18 (refonte des scripts, lot 4) : la migration des blocs VAR
//  (voir HmiMigrate.hpp)
// =============================================================================
#include "HmiMigrate.hpp"

#include "HmiDecl.hpp"
#include "HmiOperators.hpp"
#include "HmiScript.hpp"
#include "HmiSymbols.hpp"

#include <algorithm>
#include <cctype>

namespace hmi::migrate {

namespace {

constexpr std::size_t npos = std::string::npos;

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
bool sameName(std::string_view a, std::string_view b) { return upper(a) == upper(b); }

// Le code sans ses blocs du premier niveau. Un bloc seul sur ses lignes les emporte ;
// ses commentaires rattaches a aucune declaration restent, a sa place. Les lignes vides
// laissees en tete partent.
std::string withoutBlocks(std::string_view code, const std::vector<decl::Block>& blocks) {
    std::string out;
    std::size_t at = 0;
    for (const auto& b : blocks) {
        std::size_t from = b.begin, to = b.end;
        std::size_t lineStart = from;
        while (lineStart > 0 && code[lineStart - 1] != '\n') --lineStart;
        std::size_t after = to;
        while (after < code.size() && (code[after] == ' ' || code[after] == '\t' || code[after] == '\r')) ++after;
        const bool aloneBefore = code.substr(lineStart, from - lineStart).find_first_not_of(" \t") == std::string_view::npos;
        const bool aloneAfter = after >= code.size() || code[after] == '\n';
        std::string indent;
        if (aloneBefore && aloneAfter && lineStart >= at) {
            indent = std::string(code.substr(lineStart, from - lineStart));
            from = lineStart;
            to = after < code.size() ? after + 1 : after;
        }
        out.append(code.substr(at, from - at));
        for (const auto& c : b.comments)
            out += indent + (c.find("*)") == npos ? "(* " + c + " *)" : "// " + c) + "\n";
        at = to;
    }
    out.append(code.substr(at));
    std::size_t k = 0;
    for (;;) {
        const auto nl = out.find('\n', k);
        if (nl == npos || out.find_first_not_of(" \t\r", k) < nl) break;
        k = nl + 1;
    }
    return out.substr(k);
}

// Le genre de code : il decide de la conversion.
enum class Host : std::uint8_t { Script, Function, Override, Operator };

// Les blocs d'un code, convertis ; faux (et `it.why`) : il ne migre pas.
bool convert(const decl::Extract& x, Host host, Item& it) {
    for (const auto& d : x.decls) {
        const bool param = decl::isParameter(d.section);
        if (param && host == Host::Script) {
            it.why = "il d\xC3\xA9" "clare des param\xC3\xA8tres (" + std::string(decl::keyword(d.section))
                   + ") : seule une fonction en a - il ne tourne pas aujourd'hui";
            return false;
        }
        if (param && host == Host::Operator) {
            it.why = "il d\xC3\xA9" "clare des param\xC3\xA8tres (" + std::string(decl::keyword(d.section)) + ") : ses op\xC3\xA9randes sont A et B";
            return false;
        }
        if (param && host == Host::Override) continue;   // ceux de la fonction redefinie (verifies a part)
        Declaration m;
        m.name = d.name;
        m.type = d.type;
        m.value = d.initial;
        m.description = d.comment;
        m.visibility = Visibility::Private;              // D11 : elle n'etait vue que de son code
        if (param) {
            m.kind = DeclKind::Parameter;
            m.mode = d.section == decl::Section::InOut ? PassMode::InOut : d.section == decl::Section::Output ? PassMode::Out : PassMode::In;
            if (d.constant)
                it.notes.push_back({false, d.name + " : param\xC3\xA8tre CONSTANT - un param\xC3\xA8tre d'entr\xC3\xA9" "e (la fonction ne l'\xC3\xA9" "crit pas)"});
        } else if (d.constant) {
            if (d.initial.empty()) {
                it.why = "la constante " + d.name + " n'a pas de valeur";
                return false;
            }
            m.kind = DeclKind::Constant;
        } else {
            m.kind = DeclKind::Variable;
            m.storage = host == Host::Script && d.section == decl::Section::Var ? Storage::Kept : Storage::Execution;
            if (d.retain)
                it.notes.push_back({true, host == Host::Script
                                              ? d.name + " : VAR RETAIN devient Conserv\xC3\xA9" "e (gard\xC3\xA9" "e d'une ex\xC3\xA9" "cution \xC3\xA0 l'autre, "
                                                "comme avant) ; Persistante seulement sur ton choix (d\xC3\xA9" "cision D3)"
                                              : d.name + " : RETAIN est sans effet dans une fonction (elle n'a pas de m\xC3\xA9moire)"});
        }
        if (!m.description.empty()) ++it.comments;
        if (!m.value.empty()) ++it.defaults;
        it.decls.push_back(std::move(m));
    }
    return true;
}

// Les parametres d'une liste, dans l'ordre (pour comparer une redefinition a sa fonction).
std::string paramsText(const std::vector<Declaration>& decls) {
    std::string s;
    for (const auto& d : decls)
        if (d.kind == DeclKind::Parameter)
            s += upper(d.name) + ":" + upper(d.type) + ":" + std::string(passModeKey(d.mode)) + ":" + d.value + ";";
    return s;
}
std::string paramsText(const decl::Extract& x) {
    std::string s;
    for (const auto* d : x.parameters()) {
        const PassMode m = d->section == decl::Section::InOut ? PassMode::InOut : d->section == decl::Section::Output ? PassMode::Out : PassMode::In;
        s += upper(d->name) + ":" + upper(d->type) + ":" + std::string(passModeKey(m)) + ":" + d->initial + ";";
    }
    return s;
}

// Un code : lu, converti, son corps sans blocs. Rien (pas d'item) : il n'a pas de bloc.
bool examine(const std::string& body, const std::vector<Declaration>& already, Host host, Place place, Item& it,
             decl::Extract* read = nullptr) {
    auto x = decl::extract(body);
    if (x.blocks.empty()) return false;
    it.place = std::move(place);
    if (!x.errors.empty()) {
        const auto& e = x.errors.front();
        it.why = (e.line ? "ligne " + std::to_string(e.line) + " : " : std::string{}) + e.message;
        if (read) *read = std::move(x);
        return true;
    }
    if (!convert(x, host, it)) {
        it.decls.clear();
        if (read) *read = std::move(x);
        return true;
    }
    // Un nom deja declare dans le modele (un code deja migre en partie) : laisse tel quel.
    for (const auto& d : it.decls)
        if (std::any_of(already.begin(), already.end(), [&](const Declaration& a) { return sameName(a.name, d.name); })) {
            it.why = d.name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "clar\xC3\xA9" "e dans le mod\xC3\xA8le";
            it.decls.clear();
            if (read) *read = std::move(x);
            return true;
        }
    it.body = withoutBlocks(body, x.blocks);
    it.migrated = true;
    std::size_t kept = 0;
    for (const auto& b : x.blocks) kept += b.comments.size();
    if (kept) it.notes.push_back({false, std::to_string(kept) + " commentaire" + (kept > 1 ? "s" : "") + " sans d\xC3\xA9" "claration, gard\xC3\xA9"
                                             + (kept > 1 ? "s" : "") + " dans le code"});
    if (!x.functions.empty())
        it.notes.push_back({true, std::to_string(x.functions.size()) + " fonction" + (x.functions.size() > 1 ? "s" : "")
                                      + " interne" + (x.functions.size() > 1 ? "s" : "")
                                      + " (FUNCTION... END_FUNCTION) gard\xC3\xA9" "e" + (x.functions.size() > 1 ? "s" : "")
                                      + " dans le code, avec ses blocs : elles deviendront des fonctions du script (lot 8)"});
    for (const auto& d : it.decls)
        if (!localTypeSupported(d.type) && !richLocalType(d.type, [](std::string_view) { return true; }))
            it.notes.push_back({true, d.name + " : type " + d.type + " - contr\xC3\xB4l\xC3\xA9 \xC3\xA0 la compilation"});
    if (read) *read = std::move(x);
    return true;
}

} // namespace

std::size_t Plan::migrated() const noexcept {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const Item& i) { return i.migrated; }));
}
std::size_t Plan::skipped() const noexcept { return items.size() - migrated(); }
std::size_t Plan::declarations() const noexcept {
    std::size_t n = 0;
    for (const auto& i : items)
        if (i.migrated) n += i.decls.size();
    return n;
}
std::size_t Plan::attentions() const noexcept {
    std::size_t n = 0;
    for (const auto& i : items)
        for (const auto& note : i.notes) n += note.attention ? 1 : 0;
    return n;
}

bool needed(const Project& p) {
    bool any = false;
    const auto has = [&](const std::string& body) {
        if (!any && body.find_first_of("Vv") != npos && !decl::extract(body).blocks.empty()) any = true;
    };
    for (const auto& s : p.programs.scripts)
        if (s.lang == ScriptLang::ST) has(s.body);
    for (const auto& f : p.programs.functions) has(f.body);
    for (const auto& t : p.programs.types)
        for (const auto& o : t.operators) has(o.body);
    for (const auto& v : p.views) {
        for (const auto& s : v.scripts)
            if (s.lang == ScriptLang::ST) has(s.body);
        for (const auto& f : v.functions) has(f.body);
        for (const auto& o : v.operators) has(o.body);
        for (const auto& o : v.objects)
            for (const auto& fo : o.functionOverrides) has(fo.body);
    }
    return any;
}

Plan plan(const Project& p, const std::function<bool(const Place&)>& only) {
    Plan out;
    const auto wanted = [&](const Place& at) { return !only || only(at); };
    const auto take = [&](const std::string& body, const std::vector<Declaration>& already, Host host, Place place) {
        if (!wanted(place)) return;
        Item it;
        if (examine(body, already, host, std::move(place), it)) out.items.push_back(std::move(it));
    };
    for (const auto& s : p.programs.scripts) {
        if (s.lang != ScriptLang::ST) continue;
        take(s.body, s.decls, Host::Script, {Place::Kind::Script, kNoId, kNoId, kNoId, s.id, {}, "script " + s.name});
    }
    for (const auto& f : p.programs.functions)
        take(f.body, f.decls, Host::Function, {Place::Kind::Function, kNoId, kNoId, kNoId, f.id, {}, "fonction " + f.name});
    for (const auto& t : p.programs.types)
        for (const auto& o : t.operators)
            take(o.body, o.decls, Host::Operator,
                 {Place::Kind::TypeOperator, kNoId, kNoId, t.id, o.id, {}, "op\xC3\xA9rateur " + operatorSignature(o) + " (type " + t.name + ")"});
    for (const auto& v : p.views) {
        for (const auto& s : v.scripts) {
            if (s.lang != ScriptLang::ST) continue;
            take(s.body, s.decls, Host::Script, {Place::Kind::ViewScript, v.id, kNoId, kNoId, s.id, {}, v.name + "." + s.event});
        }
        for (const auto& o : v.operators)
            take(o.body, o.decls, Host::Operator,
                 {Place::Kind::SymbolOperator, v.id, kNoId, kNoId, o.id, {}, "op\xC3\xA9rateur " + operatorSignature(o) + " (" + v.name + ")"});
        // UNE FONCTION DE SYMBOLE ET SES REDEFINITIONS MIGRENT ENSEMBLE : une redefinition
        // lit les parametres de sa fonction ; si l'une garde les siens dans son code, la
        // fonction garde aussi les siens (sinon ils seraient declares deux fois).
        for (const auto& f : v.functions) {
            struct Over { Place at; const FunctionOverride* fo; };
            std::vector<Over> family;
            for (const auto& w : p.views)
                for (const auto& o : w.objects)
                    for (const auto& fo : o.functionOverrides)
                        if (sameName(fo.function, f.name) && symbolOf(p, o) == &v)
                            family.push_back({{Place::Kind::Override, w.id, o.id, kNoId, kNoId, fo.function,
                                               w.name + "/" + o.name + "." + fo.function + " (red\xC3\xA9" "finition)"}, &fo});
            const Place at{Place::Kind::SymbolFunction, v.id, kNoId, kNoId, f.id, {}, "fonction " + v.name + "." + f.name};
            const bool asked = wanted(at) || std::any_of(family.begin(), family.end(), [&](const Over& x) { return wanted(x.at); });
            if (!asked) continue;
            Item base;
            const bool baseRead = examine(f.body, f.decls, Host::Function, at, base);
            bool baseNow = baseRead && base.migrated;
            const std::string theirs = baseNow ? paramsText(base.decls) : paramsText(f.decls);
            const bool modelParams = baseNow || !theirs.empty();
            std::vector<Item> overrides;
            std::vector<bool> ownParams;              // la redefinition declare des parametres dans son code
            for (const auto& x : family) {
                Item it;
                decl::Extract read;
                if (!examine(x.fo->body, x.fo->decls, Host::Override, x.at, it, &read)) continue;
                const std::string mine = paramsText(read);
                if (it.migrated && !mine.empty()) {
                    if (!modelParams) it.why = "sa fonction " + f.name + " garde ses param\xC3\xA8tres dans son code";
                    else if (mine != theirs) it.why = "ses param\xC3\xA8tres diff\xC3\xA8rent de ceux de la fonction " + f.name;
                    if (!it.why.empty()) {
                        it.migrated = false;
                        it.decls.clear();
                        it.body.clear();
                        it.notes.clear();
                    }
                }
                // Ses parametres, les memes que ceux de la fonction : leur bloc s'en va, elle les
                // recoit de la fonction (le rapport et la question le disent).
                if (it.migrated && !mine.empty()) {
                    std::string names;
                    for (const auto* d : read.parameters()) names += (names.empty() ? "" : ", ") + d->name;
                    it.notes.push_back({false, "ses param\xC3\xA8tres (" + names + ") : ceux de " + v.name + "." + f.name});
                }
                // Une redefinition qui garde ses parametres dans son code : la fonction aussi.
                if (baseNow && !it.migrated && !mine.empty()) {
                    baseNow = false;
                    base.migrated = false;
                    base.why = "une red\xC3\xA9" "finition ne peut pas suivre : " + x.at.label + " (" + it.why + ")";
                    base.decls.clear();
                    base.body.clear();
                    base.notes.clear();
                }
                ownParams.push_back(!mine.empty());
                overrides.push_back(std::move(it));
            }
            // La fonction ne migre pas (ou plus) et n'a pas de parametre dans le modele : ses
            // redefinitions gardent leurs parametres dans leur code (et donc ce code).
            if (!baseNow && paramsText(f.decls).empty())
                for (std::size_t i = 0; i < overrides.size(); ++i)
                    if (overrides[i].migrated && ownParams[i]) {
                        overrides[i].migrated = false;
                        overrides[i].why = "sa fonction " + f.name + " garde ses param\xC3\xA8tres dans son code"
                                         + (base.why.empty() ? std::string{} : " (" + base.why + ")");
                        overrides[i].decls.clear();
                        overrides[i].body.clear();
                        overrides[i].notes.clear();
                    }
            if (baseRead) out.items.push_back(std::move(base));
            for (auto& it : overrides) out.items.push_back(std::move(it));
        }
    }
    return out;
}

std::size_t apply(Project& p, const Plan& plan) {
    std::size_t changed = 0;
    const auto put = [&](std::vector<Declaration>& decls, std::string& body, const Item& it) {
        for (auto d : it.decls) {
            d.id = p.allocate();
            decls.push_back(std::move(d));
        }
        body = it.body;
        ++changed;
    };
    for (const auto& it : plan.items) {
        if (!it.migrated) continue;
        const Place& at = it.place;
        switch (at.kind) {
            case Place::Kind::Script:
                for (auto& s : p.programs.scripts)
                    if (s.id == at.id) put(s.decls, s.body, it);
                break;
            case Place::Kind::Function:
                for (auto& f : p.programs.functions)
                    if (f.id == at.id) put(f.decls, f.body, it);
                break;
            case Place::Kind::TypeOperator:
                for (auto& t : p.programs.types)
                    if (t.id == at.type)
                        for (auto& o : t.operators)
                            if (o.id == at.id) put(o.decls, o.body, it);
                break;
            case Place::Kind::ViewScript:
            case Place::Kind::SymbolFunction:
            case Place::Kind::SymbolOperator:
            case Place::Kind::Override:
                for (auto& v : p.views) {
                    if (v.id != at.view) continue;
                    if (at.kind == Place::Kind::ViewScript)
                        for (auto& s : v.scripts)
                            if (s.id == at.id) put(s.decls, s.body, it);
                    if (at.kind == Place::Kind::SymbolFunction)
                        for (auto& f : v.functions)
                            if (f.id == at.id) put(f.decls, f.body, it);
                    if (at.kind == Place::Kind::SymbolOperator)
                        for (auto& o : v.operators)
                            if (o.id == at.id) put(o.decls, o.body, it);
                    if (at.kind == Place::Kind::Override)
                        for (auto& o : v.objects)
                            if (o.id == at.object)
                                for (auto& fo : o.functionOverrides)
                                    if (sameName(fo.function, at.function)) put(fo.decls, fo.body, it);
                }
                break;
        }
    }
    return changed;
}

std::string summary(const Plan& plan, bool before) {
    const auto plural = [](std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); };
    if (plan.items.empty()) return "aucun bloc VAR dans le code : rien \xC3\xA0 migrer";
    std::string s = (before ? plural(plan.migrated(), "code \xC3\xA0 migrer", "codes \xC3\xA0 migrer")
                            : plural(plan.migrated(), "code migr\xC3\xA9", "codes migr\xC3\xA9s")) + ", "
                  + plural(plan.declarations(), "d\xC3\xA9" "claration", "d\xC3\xA9" "clarations");
    if (plan.skipped()) s += " ; " + plural(plan.skipped(), "laiss\xC3\xA9 tel quel", "laiss\xC3\xA9s tels quels");
    if (plan.attentions()) s += " ; " + plural(plan.attentions(), "point d'attention", "points d'attention");
    return s;
}

std::string report(const Plan& plan) {
    std::string out = "Migration des d\xC3\xA9" "clarations : " + summary(plan) + "\n";
    const auto kindOf = [](const Declaration& d) -> std::string {
        if (d.kind == DeclKind::Constant) return "constante";
        if (d.kind == DeclKind::Parameter)
            return d.mode == PassMode::InOut ? "param\xC3\xA8tre E/S" : d.mode == PassMode::Out ? "param\xC3\xA8tre de sortie" : "param\xC3\xA8tre";
        return d.storage == Storage::Kept ? "Conserv\xC3\xA9" "e" : d.storage == Storage::Persistent ? "Persistante" : "Ex\xC3\xA9" "cution";
    };
    for (const auto& it : plan.items) {
        if (!it.migrated) continue;
        out += "\n" + it.place.label + " : "
             + (it.decls.empty() ? std::string("aucune d\xC3\xA9" "claration propre")
                                 : std::to_string(it.decls.size()) + " d\xC3\xA9" "claration" + (it.decls.size() > 1 ? "s" : ""));
        if (it.defaults) out += ", " + std::to_string(it.defaults) + " valeur" + (it.defaults > 1 ? "s" : "");
        if (it.comments) out += ", " + std::to_string(it.comments) + " commentaire" + (it.comments > 1 ? "s" : "") + " en documentation";
        out += "\n";
        for (const auto& d : it.decls)
            out += "    " + d.name + " : " + d.type + (d.value.empty() ? std::string{} : " := " + d.value) + "   (" + kindOf(d) + ")"
                 + (d.description.empty() ? std::string{} : "   " + d.description) + "\n";
        for (const auto& n : it.notes) out += std::string(n.attention ? "  ! " : "  - ") + n.text + "\n";
    }
    if (plan.skipped()) {
        out += "\nLaiss\xC3\xA9s tels quels :\n";
        for (const auto& it : plan.items)
            if (!it.migrated) out += "  " + it.place.label + " : " + it.why + "\n";
    }
    return out;
}

} // namespace hmi::migrate
