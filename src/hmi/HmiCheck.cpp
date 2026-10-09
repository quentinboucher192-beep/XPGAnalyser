#include "HmiCheck.hpp"
#include "HmiOverload.hpp"   // 1.11.20 : les surcharges
#include "HmiActionKinds.hpp"   // 1.11.7 : Maths, le clavier virtuel
#include <cmath>
#include "HmiPopupParams.hpp"
#include "HmiObjectAlarms.hpp"
#include "HmiAlarmGroups.hpp"
#include "HmiTwin.hpp"
#include "HmiZones.hpp"
#include "HmiLoginMenu.hpp"
#include "HmiNavigation.hpp"
#include "HmiScript.hpp"
#include "HmiDecl.hpp"         // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites

#include "HmiEdit.hpp"
#include "HmiExpr.hpp"
#include "HmiAssets.hpp"
#include "HmiSymbols.hpp"
#include "HmiTemplates.hpp"
#include "HmiWidgets.hpp"
#include "HmiForms.hpp"
#include "HmiRuntime.hpp"
#include "HmiPublicVars.hpp"
#include "HmiQr.hpp"
#include "HmiAlarmViews.hpp"
#include "HmiCharts.hpp"
#include "HmiControls.hpp"
#include "HmiExport.hpp"
#include "HmiProduction.hpp"
#include "HmiDisplay.hpp"      // lot 13
#include "HmiTypes.hpp"        // lot 16
#include "HmiEnums.hpp"        // 1.10 (S1) : knownType connait les enumerations
#include "HmiOperators.hpp"    // 1.10 (integration I2) : les constats des operateurs (S2) dans Compiler
#include "HmiLanguages.hpp"    // lot 13
#include "HmiQuality.hpp"      // lot 13
#include "HmiScenarios.hpp"    // lot 13
#include "HmiSignature.hpp"    // lot 13
#include "HmiComm.hpp"         // lot 14
#include "HmiEquipment.hpp"    // lot 15
#include "HmiExprCheck.hpp"    // ---- Lot API 8 : les expressions impossibles ----
#include "HmiPackage.hpp"      // ---- Lot API 8 : finitions (les modeles de vues du projet) ----
#include "HmiScriptCheck.hpp"  // 1.10 : les erreurs des scripts, a leur place
#include "../sim/Interpreter.hpp"
#include "HmiDuplicate.hpp"   // 1.10.2 (chantier D) : les reperes $Nom$
#include "HmiMarkers.hpp"     // 1.11 (REP) : les reperes $...$, transparents pour le calcul
#include "HmiApiVars.hpp"     // 1.11.1 (API-M) : les variables de l'automate sous API.

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace hmi {

namespace {

// Les proprietes dont la valeur est un texte a trous plutot qu'une constante.
bool isTemplateKey(std::string_view key) { return key == "text"; }

void add(std::vector<Issue>& out, Issue::Severity s, std::string cat, Id view, Id object, std::string prop,
         std::string msg, Id script = kNoId, int line = 0) {
    out.push_back(Issue{s, std::move(cat), view, object, std::move(prop), std::move(msg), script, line});
}

Issue::Severity severityOf(ScriptDiagnostic::Severity s) {
    switch (s) {
        case ScriptDiagnostic::Severity::Info:    return Issue::Severity::Info;
        case ScriptDiagnostic::Severity::Warning: return Issue::Severity::Warning;
        case ScriptDiagnostic::Severity::Error:   return Issue::Severity::Error;
    }
    return Issue::Severity::Error;
}

// "action 2 (Clic)" : ou est l'action, dans le rapport.
std::string actionLabel(std::size_t index, const Action& a) {
    return "action " + std::to_string(index + 1) + " (" + std::string(triggerLabel(a.trigger)) + ")";
}

} // namespace

std::string_view toString(Issue::Severity s) noexcept {
    switch (s) {
        case Issue::Severity::Info:    return "Information";
        case Issue::Severity::Warning: return "Avertissement";
        case Issue::Severity::Error:   return "Erreur";
    }
    return "Erreur";
}

IssueCounts count(const std::vector<Issue>& issues) {
    IssueCounts c;
    for (const auto& i : issues) {
        if (i.severity == Issue::Severity::Error) ++c.errors;
        else if (i.severity == Issue::Severity::Warning) ++c.warnings;
        else ++c.infos;
    }
    return c;
}

namespace {
// Les chaines entre apostrophes ou guillemets d'une expression.
std::vector<std::string> quotedStrings(std::string_view e) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < e.size(); ++i) {
        if (e[i] != '\'' && e[i] != '"') continue;
        const auto end = e.find(e[i], i + 1);
        if (end == std::string_view::npos) break;
        out.emplace_back(e.substr(i + 1, end - i - 1));
        i = end;
    }
    return out;
}
} // namespace

namespace {
std::string upperText(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

std::string trimmedCopy(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Un nom lu ou ecrit : variable IHM, variable de l'automate, ou fonction IHM.
// Lot 8 : dans une vue qui declare des parametres (Moteur := Pompes[0]), le nom
// d'un parametre se lit comme ce qu'il designe - un chemin (sa racine doit
// exister) ou une valeur ('A', 3 : connue d'elle-meme).
bool known(const Project& p, const NameExists& plc, std::string_view name, const View* v = nullptr) {
    if (v)
        if (const auto* prm = v->param(name)) {
            const std::string def = trimmedCopy(prm->defaultValue);
            if (def.empty() || !isVariablePath(def)) return true;
            const auto roots = scanRoots(def);
            if (roots.empty()) return true;
            std::string a(roots.front()), b(name);
            for (auto& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            for (auto& c : b) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return a == b ? (p.variable(name) || !plc || plc(name)) : known(p, plc, roots.front(), nullptr);
        }
    if (v) {
        // 1.11.13 : une popup d'un symbole connait les parametres du symbole (ceux de l'instance
        // qui l'ouvre, depuis la 1.11.10) : {Nom}, {Vanne.POSITION} dans Pop_Vanne de S_Vanne.
        if (v->ownerSymbol != kNoId)
            if (const View* owner = p.view(v->ownerSymbol); owner && owner != v && owner->param(name)) return known(p, plc, name, owner);
        // 1.11.13 : une instance de symbole de la vue est une racine (Vanne_4.GetActiveCount(30),
        // depuis la 1.11.11) ; l'appel lui-meme est verifie a part (la fonction, ses arguments).
        if (const Object* o = v->objectByName(name); o && o->kind == Kind::SymbolInstance) return true;
    }
    // Lot 9 : SYS et les noms de vue ouvrent les variables systeme et d'instances
    // (le chemin entier est verifie a part, par checkPublicVars).
    // 1.10 (decision 15, S1) : le nom seul d'une valeur d'enumeration (m := Auto) n'est pas une
    // variable inexistante ; Compiler (le dialecte) dit s'il est employe a la bonne place.
    const auto enumValue = [&] {
        for (const auto* e : enumerations(p))
            if (enumValueByName(*e, name)) return true;
        return false;
    };
    // 1.11.1 (API-M) : API est l'espace des variables de l'automate (API.<globale>,
    // API.<Unite>.<variable>) ; le chemin entier est verifie par le modele (HmiApiVars).
    return p.variable(name) || isHmiFunction(name) || pub::isSysRoot(name) || pub::viewNamed(p, name) || enumValue() || !plc
        || plc(name) || upperText(name) == "API";
}

// Un cycle dans un graphe de noms : "A -> B -> A", une fois par cycle.
void cycles(const std::map<std::string, std::vector<std::string>>& graph,
            const std::function<void(const std::vector<std::string>&)>& found) {
    std::set<std::string> done;
    std::set<std::string> reported;
    std::vector<std::string> path;
    std::function<void(const std::string&)> visit = [&](const std::string& n) {
        if (const auto at = std::find(path.begin(), path.end(), n); at != path.end()) {
            std::vector<std::string> loop(at, path.end());
            loop.push_back(n);
            // Le meme cycle vu depuis un autre point : une seule fois.
            std::vector<std::string> key(loop.begin(), loop.end() - 1);
            std::sort(key.begin(), key.end());
            std::string k;
            for (const auto& x : key) k += x + "|";
            if (reported.insert(k).second) found(loop);
            return;
        }
        if (done.count(n)) return;
        path.push_back(n);
        if (const auto it = graph.find(n); it != graph.end())
            for (const auto& m : it->second) visit(m);
        path.pop_back();
        done.insert(n);
    };
    for (const auto& [n, _] : graph) visit(n);
}

std::string arrowJoin(const std::vector<std::string>& names) {
    std::string s;
    for (std::size_t i = 0; i < names.size(); ++i) s += (i ? " \xE2\x86\x92 " : "") + names[i];
    return s;
}

void checkActions(const Project& p, const NameExists& plc, const View& v, const Object* o,
                  const std::vector<Action>& list, std::vector<Issue>& out) {
    using S = Issue::Severity;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& a = list[i];
        const std::string where = actionLabel(i, a);
        const Id obj = o ? o->id : kNoId;
        // 1.11 (REP) : les $ d'un repere sont transparents ($V[2]$.Cmd se lit V[2].Cmd) ;
        // l'ancien $Vanne$ qui n'est pas une variable : l'erreur ordinaire, et la phrase.
        const std::string target = markers::strip(a.target, operationWritesVariable(a.operation) ? markers::Mode::Expression : markers::Mode::Text);
        if (triggerWatches(a.trigger) && a.watch.empty())
            add(out, S::Error, "Action", v.id, obj, where, "aucune expression surveill\xC3\xA9" "e");
        if ((a.trigger == Trigger::Click || a.trigger == Trigger::DoubleClick || a.trigger == Trigger::LongPress) && !o)
            add(out, S::Warning, "Action", v.id, obj, where, "un clic se fait sur un objet : cette action de vue ne partira jamais");
        if (operationWritesVariable(a.operation)) {
            if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucune variable \xC3\xA0 \xC3\xA9" "crire");
            else if (!known(p, plc, target, &v) && !known(p, plc, scanRoots(target).empty() ? target : scanRoots(target).front(), &v))
                add(out, S::Error, "Action", v.id, obj, where,
                    "variable inexistante : " + target
                        + dup::markerHint(a.target, scanRoots(target).empty() ? target : scanRoots(target).front()));
        }
        if (operationOpensView(a.operation)) {
            const auto* opened = p.viewByName(target);
            if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucune vue \xC3\xA0 ouvrir");
            else if (!opened) add(out, S::Error, "Action", v.id, obj, where, "vue '" + target + "' introuvable" + dup::markerHint(a.target, target));
            else if (a.operation == Operation::Popup && opened->id == v.id)
                add(out, S::Error, "Action", v.id, obj, where, "r\xC3\xA9" "f\xC3\xA9rence circulaire : la vue s'ouvre elle-m\xC3\xAAme en popup");
        }
        if (a.operation == Operation::CallScript) {
            if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucun script \xC3\xA0 appeler");
            else if (!p.generalScript(target))
                add(out, S::Error, "Action", v.id, obj, where, "script g\xC3\xA9n\xC3\xA9ral '" + target + "' introuvable" + dup::markerHint(a.target, target));
        }
        if ((a.operation == Operation::Assign || a.operation == Operation::Log || a.operation == Operation::RunScript) && a.value.empty())
            add(out, S::Error, "Action", v.id, obj, where, "valeur vide");
        // Les noms lus par les expressions de l'action.
        std::vector<std::string> roots;
        for (const auto* e : {&a.watch, &a.guard}) if (!e->empty()) for (auto& r : scanRoots(*e)) roots.push_back(r);
        if ((a.operation == Operation::Assign || a.operation == Operation::Increment || a.operation == Operation::Decrement)
            && !a.value.empty())
            for (auto& r : scanRoots(a.value)) roots.push_back(r);
        if (a.operation == Operation::Log) for (auto& r : TextTemplate::compile(a.value).roots()) roots.push_back(r);
        if (a.operation == Operation::RunScript)
            for (const auto& u : scriptNames(a.value)) roots.push_back(u.name);
        std::set<std::string> said;
        const std::string fieldsText = a.watch + "\n" + a.guard + "\n" + a.value;
        for (const auto& r : roots)
            if (!known(p, plc, r, &v) && said.insert(r).second)
                add(out, S::Error, "Variable", v.id, obj, where, "variable inexistante dans le programme : " + r + dup::markerHint(fieldsText, r));
    }
}
} // namespace

// La programmation generale, les scripts de vue, les actions, et les
// references circulaires entre eux.
void checkPrograms(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    std::set<std::string> varNames;
    for (const auto& var : p.programs.variables) {
        const std::string key = [&] { std::string u = var.name; for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return u; }();
        if (!isIdentifier(var.name)) add(out, S::Error, "Variable IHM", kNoId, kNoId, var.name, "nom invalide : '" + var.name + "'");
        else if (!varNames.insert(key).second) add(out, S::Error, "Variable IHM", kNoId, kNoId, var.name, "variable IHM en double : " + var.name);
        if (plc && plc(var.name))
            add(out, S::Warning, "Variable IHM", kNoId, kNoId, var.name,
                "'" + var.name + "' existe aussi dans l'automate : la variable IHM la masque");
        // Lot 16 : un type IHM, un tableau (bornes, cases), un dossier.
        std::string why;
        if (!types::validType(p, var.type, &why)) {
            add(out, S::Error, "Variable IHM", kNoId, kNoId, var.name, var.name + " : " + why);
        } else if (types::isComposite(var.type)) {
            const auto f = types::flatten(p, var.name, var.type, var.initial, var.packBools);
            if (!f.ok()) add(out, S::Error, "Variable IHM", kNoId, kNoId, var.name, f.error);
        }
        if (!var.folder.empty() && !types::validFolder(var.folder, &why))
            add(out, S::Error, "Variable IHM", kNoId, kNoId, var.name, var.name + " : " + why);
        if (!var.bound() && !var.places.empty())
            add(out, S::Info, "Variable IHM", kNoId, kNoId, var.name, var.name + " : des adresses de membres, mais la variable n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement");
    }
    // Lot 16 : les types IHM - noms, membres, types des membres, cycles.
    std::set<std::string> typeNames;
    for (const auto& ty : p.programs.types) {
        const std::string cat = "Type IHM";
        if (!isIdentifier(ty.name)) add(out, S::Error, cat, kNoId, kNoId, ty.name, "nom de type invalide : '" + ty.name + "'");
        else if (types::isElementary(ty.name)) add(out, S::Error, cat, kNoId, kNoId, ty.name, ty.name + " : le nom d'un type \xC3\xA9l\xC3\xA9mentaire");
        else if (!typeNames.insert(upperText(ty.name)).second) add(out, S::Error, cat, kNoId, kNoId, ty.name, "type IHM en double : " + ty.name);
        if (ty.members.empty()) add(out, S::Warning, cat, kNoId, kNoId, ty.name, ty.name + " : aucun membre");
        std::set<std::string> members;
        for (const auto& m : ty.members) {
            std::string why;
            if (!isIdentifier(m.name)) add(out, S::Error, cat, kNoId, kNoId, ty.name, ty.name + " : nom de membre invalide '" + m.name + "'");
            else if (!members.insert(upperText(m.name)).second) add(out, S::Error, cat, kNoId, kNoId, ty.name, ty.name + " : membre en double " + m.name);
            if (!types::validType(p, m.type, &why)) add(out, S::Error, cat, kNoId, kNoId, ty.name, ty.name + "." + m.name + " : " + why);
            if (types::property(m.name))
                add(out, S::Info, cat, kNoId, kNoId, ty.name,
                    ty.name + "." + m.name + " : un membre de ce nom passe avant la propri\xC3\xA9t\xC3\xA9 " + m.name);
            if (!m.initial.empty()) {
                const auto e = Expression::compile(m.initial);
                if (!e.valid()) add(out, S::Error, cat, kNoId, kNoId, ty.name, ty.name + "." + m.name + " : valeur initiale " + m.initial + " : " + e.error());
            }
        }
        if (const auto cycle = types::cycleOf(p, ty.name); !cycle.empty())
            add(out, S::Error, cat, kNoId, kNoId, ty.name, ty.name + " se contient lui-m\xC3\xAAme : " + cycle);
    }
    // Les scripts : les noms qu'ils lisent et ecrivent doivent exister.
    const auto names = [&](const Script& sc, Id view) {
        if (sc.lang != ScriptLang::ST) return;
        const View* owner = view != kNoId ? p.view(view) : nullptr;
        const std::string code = decl::codeOf(sc);                    // 1.11.18 (lot 3) : ses declarations du modele
        const auto dialect = scriptcheck::dialectDeclared(code);      // 1.10 : le dialecte IHM (S1) n'est pas inexistant
        for (const auto& u : scriptNames(code))
            if (!known(p, plc, u.name, owner) && !dialect.count(upperText(u.name)))
                add(out, S::Error, "Script", view, kNoId, sc.name, "variable inexistante : " + u.name, sc.id, u.line);
        // Lot 16 : les structures et tableaux IHM - membres, indices constants, proprietes.
        for (const auto& pp : types::pathProblems(p, code))
            add(out, S::Error, "Script", view, kNoId, sc.name, pp.message, sc.id, pp.line);
        for (const auto& called : scriptCalls(sc.body))
            if (!p.generalScript(called))
                add(out, S::Error, "Script", view, kNoId, sc.name, "IHM_APPELER : script '" + called + "' introuvable", sc.id);
        for (const auto& target : scriptViews(sc.body))
            if (!p.viewByName(target))
                add(out, S::Error, "Script", view, kNoId, sc.name, "vue '" + target + "' introuvable", sc.id);
    };
    std::set<std::string> scriptNamesSeen;
    for (const auto& sc : p.programs.scripts) {
        if (!isIdentifier(sc.name)) add(out, S::Error, "Script", kNoId, kNoId, sc.name, "nom invalide : '" + sc.name + "'", sc.id);
        else if (!scriptNamesSeen.insert(sc.name).second) add(out, S::Error, "Script", kNoId, kNoId, sc.name, "script en double : " + sc.name, sc.id);
        if (std::find(std::begin(kGeneralEvents), std::end(kGeneralEvents), sc.event) == std::end(kGeneralEvents))
            add(out, S::Error, "Script", kNoId, kNoId, sc.name, "d\xC3\xA9" "clencheur inconnu : " + sc.event, sc.id);
        if (sc.event == "Cyclique" && sc.periodMs < 10)
            add(out, S::Error, "Script", kNoId, kNoId, sc.name, "p\xC3\xA9riode trop courte (10 ms au moins)", sc.id);
        if (sc.event == "Changement" && sc.watch.empty())
            add(out, S::Error, "Script", kNoId, kNoId, sc.name, "aucune expression surveill\xC3\xA9" "e", sc.id);
        if (sc.event == "Changement" && !sc.watch.empty())
            for (const auto& r : scanRoots(sc.watch))
                if (!known(p, plc, r)) add(out, S::Error, "Script", kNoId, kNoId, sc.name, "variable inexistante : " + r, sc.id);
        names(sc, kNoId);
    }
    for (const auto& v : p.views) {
        for (const auto& sc : v.scripts) {
            if (std::find(std::begin(kViewEvents), std::end(kViewEvents), sc.event) == std::end(kViewEvents))
                add(out, S::Error, "Script", v.id, kNoId, sc.name, "\xC3\xA9v\xC3\xA9nement de vue inconnu : " + sc.event, sc.id);
            names(sc, v.id);
        }
        checkActions(p, plc, v, nullptr, v.actions, out);
        for (const auto& o : v.objects)
            if (!o.actions.empty()) checkActions(p, plc, v, &o, o.actions, out);
    }

    // ---- lot 7 : les fonctions IHM (nom, retour, variables, appels en boucle)
    const auto fnClashes = overload::clashes(p.programs.functions);   // 1.11.20 : les surcharges mal distinguees
    std::map<std::string, std::vector<std::string>> fnCalls;
    const auto upperOf = [](std::string_view v) {
        std::string u(v);
        for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return u;
    };
    for (const auto& f : p.programs.functions) {
        const auto issue = [&](Issue::Severity sev, std::string msg, int line = 0) {
            Issue i;
            i.severity = sev;
            i.category = "Fonction";
            i.property = f.name;
            i.message = std::move(msg);
            i.item = f.id;
            i.line = line;
            out.push_back(std::move(i));
        };
        const std::string key = upperOf(f.name);
        if (!isIdentifier(f.name)) issue(S::Error, "nom invalide : '" + f.name + "'");
        // 1.11.20 : plusieurs fonctions du meme nom sont des surcharges ; de la meme forme, une faute.
        for (const auto& c : fnClashes)
            if (c.function == f.id) issue(S::Error, c.message);
        if (key.rfind("IHM_", 0) == 0) issue(S::Error, "le pr\xC3\xA9" "fixe IHM_ est r\xC3\xA9serv\xC3\xA9 aux fonctions de l'IHM");
        if (p.variable(f.name)) issue(S::Error, "une variable IHM porte d\xC3\xA9j\xC3\xA0 ce nom : " + f.name);
        if (plc && plc(f.name)) issue(S::Warning, "'" + f.name + "' existe aussi dans l'automate : la fonction IHM passe avant");
        const std::string code = decl::codeOf(f);                     // 1.11.18 (lot 3) : ses parametres du modele
        const auto dialect = scriptcheck::dialectDeclared(code);      // 1.10 : le dialecte IHM (S1)
        for (const auto& u : scriptNames(code))
            if (upperOf(u.name) != key && !known(p, plc, u.name) && !dialect.count(upperOf(u.name)))
                issue(S::Error, "variable inexistante : " + u.name, u.line);
        auto& to = fnCalls[f.name];
        for (const auto& c : scriptCallees(code))
            if (const auto* g = p.functionByName(c.name)) to.push_back(g->name);
    }
    cycles(fnCalls, [&](const std::vector<std::string>& loop) {
        const auto* f = p.functionByName(loop.front());
        Issue i;
        // Permis (borne a 8 niveaux, Fact(5) passe) mais a surveiller : une
        // recursion sans fin est coupee, et la cible automate l'interdit.
        i.severity = S::Warning;
        i.category = "Fonction";
        i.property = loop.front();
        i.message = "appel r\xC3\xA9" "cursif : " + arrowJoin(loop) + " (la simulation coupe au-del\xC3\xA0 de 8 niveaux)";
        i.item = f ? f->id : kNoId;
        out.push_back(std::move(i));
    });

    // References circulaires 1 : des scripts qui s'appellent en boucle.
    std::map<std::string, std::vector<std::string>> calls;
    for (const auto& sc : p.programs.scripts) calls[sc.name] = scriptCalls(sc.body);
    cycles(calls, [&](const std::vector<std::string>& loop) {
        const auto* sc = p.generalScript(loop.front());
        add(out, S::Error, "Script", kNoId, kNoId, loop.front(),
            "r\xC3\xA9" "f\xC3\xA9rence circulaire : " + arrowJoin(loop) + " (la simulation couperait l'appel)", sc ? sc->id : kNoId);
    });
    // References circulaires 2 : des vues qui, a leur ouverture, en ouvrent une
    // autre qui revient a la premiere.
    std::map<std::string, std::vector<std::string>> opens;
    for (const auto& v : p.views) {
        auto& to = opens[v.name];
        const auto collect = [&](const std::vector<Action>& list) {
            for (const auto& a : list)
                if (a.trigger == Trigger::ViewOpen && operationOpensView(a.operation) && !a.target.empty()) to.push_back(a.target);
        };
        collect(v.actions);
        for (const auto& o : v.objects) collect(o.actions);
        for (const auto& sc : v.scripts)
            if (sc.event == "OnOpen") for (auto& t : scriptViews(sc.body)) to.push_back(t);
    }
    cycles(opens, [&](const std::vector<std::string>& loop) {
        const auto* v = p.viewByName(loop.front());
        add(out, S::Error, "Vue", v ? v->id : kNoId, kNoId, {},
            "navigation circulaire \xC3\xA0 l'ouverture : " + arrowJoin(loop) + " (la simulation s'arr\xC3\xAAterait apr\xC3\xA8s 8 navigations)");
    });
}

namespace {

void addItem(std::vector<Issue>& out, Issue::Severity s, std::string cat, std::string name, std::string msg, Id item,
             Id view = kNoId, Id object = kNoId) {
    Issue i;
    i.severity = s;
    i.category = std::move(cat);
    i.view = view;
    i.object = object;
    i.property = std::move(name);
    i.message = std::move(msg);
    i.item = item;
    out.push_back(std::move(i));
}

std::vector<std::string> splitPens(const std::string& list) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= list.size()) {
        const auto at = list.find(';', from);
        std::string pen = list.substr(from, at == std::string::npos ? std::string::npos : at - from);
        while (!pen.empty() && pen.front() == ' ') pen.erase(pen.begin());
        while (!pen.empty() && pen.back() == ' ') pen.pop_back();
        if (!pen.empty()) out.push_back(pen);
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return out;
}

} // namespace

// Lot 4 : alarmes, recettes, securite, courbes, historiques - et les actions
// qui les visent.
// 1.10.2 (AL) : les groupes d'alarmes et les liens des groupes des objets. Compiler
// et Generer les disent tous deux (compile() et generateWith() l'appellent).
void checkAlarmGroupLinks(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    std::set<std::string> groupNames;
    for (const auto& g : p.alarmGroups) {
        if (g.name.empty()) addItem(out, S::Error, "Groupe d'alarmes", g.name, "groupe d'alarmes sans nom", g.id);
        else if (!groupNames.insert(g.name).second)
            addItem(out, S::Error, "Groupe d'alarmes", g.name, "groupe d'alarmes en double : " + g.name, g.id);
    }
    for (const auto* l : danglingAlarmGroupLinks(p))
        addItem(out, S::Error, "Groupe d'alarmes", l->group,
                "le groupe d'objets " + l->objectGroup + " est li\xC3\xA9 au groupe " + l->group
                    + ", qui n'existe plus (supprim\xC3\xA9 ou renomm\xC3\xA9)", kNoId);
    const auto all = objectAlarms(p);
    // 1.10.2 (AL) : un lien dont le groupe d'objets ne porte plus d'alarme (objet ou vue
    // renomme, supprime ; symbole absent) ne lie plus rien.
    for (const auto& l : p.alarmGroupLinks) {
        const std::string& g = l.objectGroup;
        if (g.find('*') != std::string::npos) continue;
        bool found = false;
        if (g.rfind("symbole:", 0) == 0) {
            found = p.viewByName(g.substr(8)) != nullptr;
        } else {
            for (const auto& oa : all)
                if (oa.objectGroup == g || (oa.objectGroup.size() > g.size() && oa.objectGroup.compare(0, g.size(), g) == 0
                                            && oa.objectGroup[g.size()] == '.')) {
                    found = true;
                    break;
                }
        }
        if (!found)
            addItem(out, S::Warning, "Groupe d'alarmes", l.group,
                    "le groupe d'objets " + g + " (li\xC3\xA9 \xC3\xA0 " + l.group + ") ne porte aucune alarme : objet renomm\xC3\xA9 ou supprim\xC3\xA9 ?",
                    kNoId);
    }
}

void checkSupervision(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    // 1.9 : une alarme d'objet (generee : Vue.Objet.Alarme) se vise aussi par son nom.
    std::set<std::string> generatedNames;
    bool generatedReady = false;
    const auto generatedAlarm = [&](const std::string& name) {
        if (!generatedReady) {
            for (const auto& oa : objectAlarms(p)) generatedNames.insert(oa.def.name);
            generatedReady = true;
        }
        return generatedNames.count(name) > 0;
    };
    const auto unknownRoots = [&](const std::string& expr, const View* v = nullptr) {
        std::vector<std::string> missing;
        for (const auto& r : scanRoots(expr)) if (!known(p, plc, r, v)) missing.push_back(r);
        return missing;
    };
    // ---- alarmes
    std::set<std::string> alarmNames;
    for (const auto& a : p.alarms) {
        if (!isIdentifier(a.name)) addItem(out, S::Error, "Alarme", a.name, "nom invalide : '" + a.name + "'", a.id);
        else if (!alarmNames.insert(a.name).second) addItem(out, S::Error, "Alarme", a.name, "alarme en double : " + a.name, a.id);
        if (a.condition.empty()) {
            addItem(out, S::Error, "Alarme", a.name, "aucune condition : l'alarme n'appara\xC3\xAEtra jamais", a.id);
        } else {
            const auto e = Expression::compile(a.condition);
            if (!e.valid()) addItem(out, S::Error, "Alarme", a.name, "condition " + a.condition + " : " + e.error(), a.id);
            for (const auto& r : unknownRoots(a.condition))
                addItem(out, S::Error, "Alarme", a.name, "variable inexistante dans la condition : " + r, a.id);
        }
        const auto t = TextTemplate::compile(a.message);
        for (const auto& err : t.errors()) addItem(out, S::Error, "Alarme", a.name, "message : " + err, a.id);
        for (const auto& r : t.roots())
            if (!known(p, plc, r)) addItem(out, S::Error, "Alarme", a.name, "variable inexistante dans le message : " + r, a.id);
        if (a.priority < 1 || a.priority > kAlarmPriorities)
            addItem(out, S::Error, "Alarme", a.name, "priorit\xC3\xA9 " + std::to_string(a.priority) + " hors de 1 \xC3\xA0 4", a.id);
        const auto& cats = alarmCategories();
        if (std::find(cats.begin(), cats.end(), a.category) == cats.end())
            addItem(out, S::Warning, "Alarme", a.name, "cat\xC3\xA9gorie inconnue : " + a.category, a.id);
        if (a.delayMs < 0) addItem(out, S::Error, "Alarme", a.name, "temporisation n\xC3\xA9gative", a.id);
    }
    // ---- recettes
    std::set<std::string> recipeNames;
    for (const auto& r : p.recipes) {
        if (r.name.empty()) addItem(out, S::Error, "Recette", r.name, "recette sans nom", r.id);
        else if (!recipeNames.insert(r.name).second) addItem(out, S::Error, "Recette", r.name, "recette en double : " + r.name, r.id);
        if (r.fields.empty()) addItem(out, S::Warning, "Recette", r.name, "aucun \xC3\xA9l\xC3\xA9ment", r.id);
        std::vector<std::pair<bool, std::pair<double, double>>> bounds;
        for (const auto& f : r.fields) {
            if (f.variable.empty()) addItem(out, S::Warning, "Recette", r.name, f.name + " : aucune variable", r.id);
            else for (const auto& root : unknownRoots(f.variable))
                addItem(out, S::Error, "Recette", r.name, f.name + " : variable inexistante " + root, r.id);
            double lo = 0, hi = 0;
            const bool hasLo = !f.min.empty(), hasHi = !f.max.empty();
            if (hasLo && !parseNumber(f.min, lo)) addItem(out, S::Error, "Recette", r.name, f.name + " : minimum illisible " + f.min, r.id);
            if (hasHi && !parseNumber(f.max, hi)) addItem(out, S::Error, "Recette", r.name, f.name + " : maximum illisible " + f.max, r.id);
            if (hasLo && hasHi && lo > hi) addItem(out, S::Error, "Recette", r.name, f.name + " : minimum sup\xC3\xA9rieur au maximum", r.id);
            bounds.push_back({hasLo || hasHi, {hasLo ? lo : -1e300, hasHi ? hi : 1e300}});
        }
        std::set<std::string> sets;
        for (const auto& rec : r.records) {
            if (!sets.insert(rec.name).second) addItem(out, S::Error, "Recette", r.name, "jeu en double : " + rec.name, r.id);
            for (std::size_t i = 0; i < rec.values.size() && i < r.fields.size(); ++i) {
                const auto& value = rec.values[i];
                if (value.empty()) continue;
                const auto e = Expression::compile(value);
                if (!e.valid()) {
                    addItem(out, S::Error, "Recette", r.name, rec.name + " / " + r.fields[i].name + " : " + value + " illisible", r.id);
                    continue;
                }
                double n = 0;
                if (bounds[i].first && parseNumber(value, n) && (n < bounds[i].second.first || n > bounds[i].second.second))
                    addItem(out, S::Error, "Recette", r.name,
                            rec.name + " / " + r.fields[i].name + " : " + value + " hors bornes [" + r.fields[i].min + " ; " + r.fields[i].max + "]", r.id);
            }
        }
    }
    // ---- securite
    const auto& sec = p.security;
    std::set<std::string> logins;
    const auto& perms = permissionNames();
    for (const auto& role : sec.roles)
        for (const auto& perm : role.permissions)
            if (std::find(perms.begin(), perms.end(), perm) == perms.end())
                addItem(out, S::Warning, "S\xC3\xA9" "curit\xC3\xA9", role.name, "r\xC3\xB4le " + role.name + " : permission inconnue " + perm, kNoId);
    for (const auto& g : sec.groups)
        for (const auto& roleName : g.roles)
            if (!p.role(roleName))
                addItem(out, S::Error, "S\xC3\xA9" "curit\xC3\xA9", g.name, "groupe " + g.name + " : r\xC3\xB4le '" + roleName + "' introuvable", g.id);
    for (const auto& u : sec.users) {
        std::string key = u.login;
        for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!isIdentifier(u.login)) addItem(out, S::Error, "Utilisateur", u.login, "identifiant invalide : '" + u.login + "'", u.id);
        else if (!logins.insert(key).second) addItem(out, S::Error, "Utilisateur", u.login, "identifiant en double : " + u.login, u.id);
        if (!p.group(u.group)) addItem(out, S::Error, "Utilisateur", u.login, "groupe introuvable", u.id);
        if (u.protection == "classique" && u.passwordHash.empty())
            addItem(out, S::Error, "Utilisateur", u.login, "aucun mot de passe : il ne pourra pas se connecter", u.id);
        else if (u.protection == "dynamique" && u.secret.empty())
            addItem(out, S::Error, "Utilisateur", u.login, "aucun secret pour le code dynamique", u.id);
        else if (u.protection == "expression") {
            if (u.expression.empty()) addItem(out, S::Error, "Utilisateur", u.login, "aucune expression d'autorisation", u.id);
            else {
                const auto e = Expression::compile(u.expression);
                if (!e.valid()) addItem(out, S::Error, "Utilisateur", u.login, "autorisation " + u.expression + " : " + e.error(), u.id);
                for (const auto& r : unknownRoots(u.expression))
                    addItem(out, S::Error, "Utilisateur", u.login, "variable inexistante dans l'autorisation : " + r, u.id);
            }
        } else if (u.protection != "classique" && u.protection != "dynamique")
            addItem(out, S::Error, "Utilisateur", u.login, "protection inconnue : " + u.protection, u.id);
    }
    if (sec.enabled && sec.users.empty())
        addItem(out, S::Warning, "S\xC3\xA9" "curit\xC3\xA9", {}, "s\xC3\xA9" "curit\xC3\xA9 active sans utilisateur : les objets prot\xC3\xA9g\xC3\xA9s ne r\xC3\xA9pondront pas", kNoId);
    if (!sec.startUser.empty() && !p.userByLogin(sec.startUser))
        addItem(out, S::Error, "S\xC3\xA9" "curit\xC3\xA9", sec.startUser, "utilisateur de d\xC3\xA9part introuvable : " + sec.startUser, kNoId);
    int maxLevel = 0;
    for (const auto& g : sec.groups) maxLevel = std::max(maxLevel, g.level);
    std::size_t protectedObjects = 0;
    // ---- objets : niveau d'acces, autorisation, courbes, historiques ; actions du lot 4
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            const int access = static_cast<int>(o.number("access", 0));
            if (access > 0) ++protectedObjects;
            if (sec.enabled && access > maxLevel)
                add(out, S::Warning, "S\xC3\xA9" "curit\xC3\xA9", v.id, o.id, "access",
                    "niveau d'acc\xC3\xA8s " + std::to_string(access) + " : aucun groupe ne l'atteint (maximum " + std::to_string(maxLevel) + ")");
            if (const auto auth = o.text("auth"); !auth.empty()) {
                const auto e = Expression::compile(auth);
                if (!e.valid()) add(out, S::Error, "S\xC3\xA9" "curit\xC3\xA9", v.id, o.id, "auth", "autorisation " + auth + " : " + e.error());
                for (const auto& r : unknownRoots(auth, &v))
                    add(out, S::Error, "S\xC3\xA9" "curit\xC3\xA9", v.id, o.id, "auth", "variable inexistante dans l'autorisation : " + r);
            }
            if (o.kind == Kind::Trend) {
                const auto pens = splitPens(o.text("variables"));
                const bool history = o.text("mode").rfind("historique", 0) == 0;
                const auto source = o.text("source");
                if (pens.empty()) add(out, S::Warning, "Courbe", v.id, o.id, "variables", "aucune plume : la courbe restera vide");
                if (!history || source.empty())
                    for (const auto& pen : pens)
                        for (const auto& r : unknownRoots(pen, &v))
                            add(out, S::Error, "Courbe", v.id, o.id, "variables", "variable inexistante : " + r);
                if (history && !source.empty() && !p.externalByName(source))
                    add(out, S::Warning, "Courbe", v.id, o.id, "source", "fichier externe '" + source + "' introuvable dans le projet");
                if (history && source.empty())
                    for (const auto& pen : pens)
                        if (std::find(p.history.archived.begin(), p.history.archived.end(), pen) == p.history.archived.end())
                            add(out, S::Warning, "Courbe", v.id, o.id, "variables",
                                pen + " n'est pas archiv\xC3\xA9" "e (Configuration > Historiques) : pas de mesures \xC3\xA0 relire");
                if (o.number("duration", 60) <= 0) add(out, S::Error, "Courbe", v.id, o.id, "duration", "dur\xC3\xA9" "e nulle");
                if (o.text("scale", "fixe") == "fixe" && o.number("ymin", 0) >= o.number("ymax", 100))
                    add(out, S::Error, "Courbe", v.id, o.id, "ymin", "\xC3\xA9" "chelle : le minimum doit \xC3\xAAtre sous le maximum");
            }
        }
        const auto actionsOf = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& source = list[i];
                const std::string where = actionLabel(i, source);
                const Id obj = o ? o->id : kNoId;
                // 1.11.1 (REP) : la cible d'une action qui n'ecrit pas (alarme, recette, utilisateur) se
                // lit sans les $ de ses reperes, comme a l'execution (Runtime::fire : mode texte, celui
                // de dup::fields) ; le message cite le nom sans ses $.
                Action a = source;
                if (!operationWritesVariable(a.operation)) a.target = markers::strip(a.target, markers::Mode::Text);
                if (a.operation == Operation::AckAlarm && !a.target.empty() && a.target != "*" && a.target.rfind("groupe:", 0) != 0
                    && !p.alarmByName(a.target) && !generatedAlarm(a.target))
                    add(out, S::Error, "Action", v.id, obj, where, "alarme '" + a.target + "' introuvable");
                if (a.operation == Operation::LoadRecipe) {
                    const auto* r = p.recipeByName(a.target);
                    if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucune recette \xC3\xA0 charger");
                    else if (!r) add(out, S::Error, "Action", v.id, obj, where, "recette '" + a.target + "' introuvable");
                    else if (a.value.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucun jeu \xC3\xA0 charger");
                    else if (const std::string jeu = markers::strip(a.value, markers::Mode::Text);   // 1.11 (REP-9) : sans ses $
                             !r->record(jeu) && Expression::compile(jeu).valid() == false)
                        add(out, S::Error, "Action", v.id, obj, where, "jeu '" + jeu + "' introuvable dans " + r->name);
                }
                if (a.operation == Operation::ChangeUser && !a.target.empty() && a.target != "-"
                    && a.target != "D\xC3\xA9" "connexion" && a.target != "Deconnexion" && !p.userByLogin(a.target))
                    add(out, S::Error, "Action", v.id, obj, where, "utilisateur '" + a.target + "' introuvable");
            }
        };
        actionsOf(nullptr, v.actions);
        for (const auto& o : v.objects) if (!o.actions.empty()) actionsOf(&o, o.actions);
    }
    if (!sec.enabled && protectedObjects > 0)
        addItem(out, S::Info, "S\xC3\xA9" "curit\xC3\xA9", {},
                std::to_string(protectedObjects) + " objet(s) ont un niveau d'acc\xC3\xA8s : ignor\xC3\xA9 tant que la s\xC3\xA9" "curit\xC3\xA9 est \xC3\xA9teinte", kNoId);
    // ---- historiques : les variables archivees
    for (const auto& expr : p.history.archived) {
        const auto e = Expression::compile(expr);
        if (!e.valid()) addItem(out, S::Error, "Historique", expr, "variable archiv\xC3\xA9" "e " + expr + " : " + e.error(), kNoId);
        for (const auto& r : unknownRoots(expr))
            addItem(out, S::Error, "Historique", expr, "variable archiv\xC3\xA9" "e inexistante : " + r, kNoId);
    }
}

// ============================================================== lot 6 ======
//  Ecrans modeles, en-tetes et pieds ; gestionnaire de recettes, image
//  animee, cases de tableau ; les actions sur les ressources.
namespace {
void checkLot6(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    if (const auto* start = p.view(p.config.startView); start && isTemplateRole(start->role))
        add(out, S::Error, "Mod\xC3\xA8le", start->id, kNoId, {},
            "la vue de d\xC3\xA9marrage " + start->name + " est un " + std::string(viewRoleLabel(start->role)) + " : on ne l'ouvre pas seule");
    for (const auto& v : p.views) {
        if (std::find(std::begin(kViewRoles), std::end(kViewRoles), v.role) == std::end(kViewRoles))
            add(out, S::Error, "Mod\xC3\xA8le", v.id, kNoId, {}, "r\xC3\xB4le inconnu : '" + v.role + "'");
        std::string broken;
        (void)templateChain(p, v, &broken);
        if (!broken.empty()) add(out, S::Error, "Mod\xC3\xA8le", v.id, kNoId, "modele", broken);
        const auto band = [&](bool shown, Id chosen, std::string_view role, const View* found, const char* what) {
            if (!shown || v.role == role) return;
            if (found) {
                if (found->height * 2 > v.height)
                    add(out, S::Warning, "Mod\xC3\xA8le", v.id, kNoId, what,
                        std::string(what) + " " + found->name + " : " + std::to_string(found->height) + " de haut pour une vue de "
                            + std::to_string(v.height) + " (il couvre plus de la moiti\xC3\xA9)");
                return;
            }
            if (chosen != kNoId)
                add(out, S::Error, "Mod\xC3\xA8le", v.id, kNoId, what,
                    std::string(what) + " : le mod\xC3\xA8le choisi n'existe plus ou n'est plus un mod\xC3\xA8le " + std::string(role));
            else
                add(out, S::Warning, "Mod\xC3\xA8le", v.id, kNoId, what,
                    std::string(what) + " coch\xC3\xA9, mais le projet n'a aucun mod\xC3\xA8le de ce genre");
        };
        band(v.showHeader, v.header, "entete", headerOf(p, v), "en-t\xC3\xAAte");
        band(v.showFooter, v.footer, "pied", footerOf(p, v), "pied de page");
        if (v.role == "modele" && viewsUsing(p, v.id).empty())
            add(out, S::Info, "Mod\xC3\xA8le", v.id, kNoId, {}, "\xC3\xA9" "cran mod\xC3\xA8le utilis\xC3\xA9 par aucune vue");
        // Un nom present deux fois dans la vue composee : les actions qui visent
        // un objet par son nom (Lier un tableau) prendraient le mauvais.
        if (inherits(p, v)) {
            const View composed = compose(p, v);
            std::map<std::string, int> seen;
            for (const auto& o : composed.objects) ++seen[o.name];
            for (const auto& o : v.objects)
                if (seen[o.name] > 1)
                    add(out, S::Warning, "Mod\xC3\xA8le", v.id, o.id, {}, "le nom " + o.name + " existe aussi dans un mod\xC3\xA8le de la vue");
        }
        // ---- les objets du lot 6
        for (const auto& o : v.objects) {
            if (o.kind == Kind::RecipeManager) {
                const auto name = markers::strip(o.text("recipe"), markers::Mode::Text);   // 1.11 (REP-1) : sans ses $
                if (name.empty()) add(out, S::Warning, "Recette", v.id, o.id, "recipe", "gestionnaire sans recette : il restera vide");
                else if (!p.recipeByName(name)) add(out, S::Error, "Recette", v.id, o.id, "recipe", "recette '" + name + "' introuvable");
                if (const auto* b = o.find("buttons"))
                    for (const auto& label : splitSemicolons(b->value))
                        if (!label.empty() && std::find(std::begin(kRecipeButtons), std::end(kRecipeButtons), label) == std::end(kRecipeButtons))
                            add(out, S::Error, "Recette", v.id, o.id, "buttons",
                                "bouton inconnu : '" + label + "' (Ajouter, Modifier, Supprimer, Appliquer, Lire)");
            }
            if (o.kind == Kind::AnimatedImage) {
                const auto states = parseImageStates(o.text("states"));
                if (states.empty()) add(out, S::Info, "Image anim\xC3\xA9" "e", v.id, o.id, "states", "aucun \xC3\xA9tat : seule l'image par d\xC3\xA9" "faut s'affiche");
                for (std::size_t k = 0; k < states.size(); ++k) {
                    const std::string where = "\xC3\xA9tat " + std::to_string(k + 1);
                    if (states[k].images.empty()) add(out, S::Warning, "Image anim\xC3\xA9" "e", v.id, o.id, "states", where + " : aucune image");
                    for (const auto& img : states[k].images) {
                        const auto* res = p.resourceByName(img);
                        if (!res) add(out, S::Warning, "Ressource", v.id, o.id, "states", where + " : image '" + img + "' introuvable dans le projet");
                        else if (res->kind() != MediaKind::Image)
                            add(out, S::Error, "Ressource", v.id, o.id, "states", where + " : '" + img + "' n'est pas une image");
                    }
                    if (plc)
                        for (const auto& r : scanRoots(states[k].condition))
                            if (!known(p, plc, r, &v)) add(out, S::Error, "Variable", v.id, o.id, "states", where + " : variable inexistante : " + r);
                }
            }
            if (o.kind == Kind::Table && plc) {
                const auto rows = parseCells(o.text("cells"));
                std::set<std::string> said;
                for (const auto& row : rows)
                    for (const auto& cell : row) {
                        std::vector<std::string> roots;
                        if (cellIsExpression(cell)) roots = scanRoots(cell.substr(1));
                        else if (cellIsTemplate(cell)) roots = TextTemplate::compile(cell).roots();
                        for (const auto& r : roots)
                            if (!known(p, plc, r, &v) && said.insert(r).second)
                                add(out, S::Error, "Variable", v.id, o.id, "cells", "case : variable inexistante dans le programme : " + r);
                    }
            }
        }
        // ---- les actions sur les ressources
        const View composed = inherits(p, v) ? compose(p, v) : v;
        const auto actionsOf = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& a = list[i];
                const std::string where = actionLabel(i, a);
                const Id obj = o ? o->id : kNoId;
                if (operationOpensView(a.operation)) {
                    if (const auto* t = p.viewByName(a.target); t && isTemplateRole(t->role))
                        add(out, S::Error, "Action", v.id, obj, where,
                            t->name + " est un " + std::string(viewRoleLabel(t->role)) + " : on n'y navigue pas, on en h\xC3\xA9rite");
                    // Lot 10 : un symbole se pose (une instance), il ne s'ouvre pas.
                    else if (t && isSymbolView(*t))
                        add(out, S::Warning, "Action", v.id, obj, where,
                            t->name + " est un symbole : on en pose des instances (biblioth\xC3\xA8que), on n'y navigue pas");
                }
                switch (a.operation) {
                    case Operation::RequestResource:
                        if (!a.target.empty()) {
                            const auto* var = p.variable(a.target);
                            if (!var && !known(p, plc, a.target, &v))
                                add(out, S::Error, "Action", v.id, obj, where, "variable inexistante : " + a.target);
                            else if (var && var->type != "STRING")
                                add(out, S::Warning, "Action", v.id, obj, where,
                                    a.target + " est " + var->type + " : le nom d'une ressource va dans une variable STRING");
                        }
                        if (a.value.empty()) add(out, S::Info, "Action", v.id, obj, where, "aucun filtre : tout fichier sera accept\xC3\xA9");
                        break;
                    case Operation::BindTable: {
                        const Object* table = nullptr;
                        for (const auto& c : composed.objects) if (c.name == a.target) table = &c;
                        if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucun tableau \xC3\xA0 lier");
                        else if (!table) add(out, S::Error, "Action", v.id, obj, where, "tableau '" + a.target + "' introuvable dans la vue");
                        else if (table->kind != Kind::Table) add(out, S::Error, "Action", v.id, obj, where, a.target + " n'est pas un tableau");
                        // 1.11 (REP-9) : le nom du fichier sans les $ de ses reperes, comme a l'execution.
                        const std::string file = markers::strip(a.value, markers::Mode::Text);
                        if (!file.empty() && !p.externalByName(file) && !Expression::compile(file).valid())
                            add(out, S::Error, "Action", v.id, obj, where, "fichier externe '" + file + "' introuvable");
                        break;
                    }
                    case Operation::GifPlay:
                    case Operation::GifPause:
                    case Operation::GifStop:
                    case Operation::GifReplay: {
                        // Lot 16 : un GIF anime de la vue (une popup peut en viser un de la vue du dessous : un avertissement).
                        const Object* gif = nullptr;
                        for (const auto& c : composed.objects)
                            if (upperText(c.name) == upperText(a.target)) gif = &c;
                        if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucun GIF anim\xC3\xA9 vis\xC3\xA9");
                        else if (!gif) {
                            bool elsewhere = false;
                            for (const auto& other : p.views)
                                for (const auto& c : other.objects)
                                    if (c.kind == Kind::AnimatedGif && upperText(c.name) == upperText(a.target)) elsewhere = true;
                            if (elsewhere)
                                add(out, S::Warning, "Action", v.id, obj, where,
                                    "GIF anim\xC3\xA9 '" + a.target + "' dans une autre vue : il faut qu'elle soit ouverte (popup ou vue du dessous)");
                            else
                                add(out, S::Error, "Action", v.id, obj, where, "GIF anim\xC3\xA9 '" + a.target + "' introuvable dans la vue");
                        } else if (gif->kind != Kind::AnimatedGif) {
                            add(out, S::Error, "Action", v.id, obj, where, a.target + " n'est pas un GIF anim\xC3\xA9");
                        }
                        if (a.operation == Operation::GifReplay && !trimmedCopy(a.value).empty()) {
                            double n = 0;
                            if (parseNumber(trimmedCopy(a.value), n)) {
                                if (n < 0) add(out, S::Error, "Action", v.id, obj, where, "nombre de tours n\xC3\xA9gatif : " + a.value);
                            } else if (!Expression::compile(a.value).valid()) {
                                add(out, S::Error, "Action", v.id, obj, where, "nombre de tours illisible : " + a.value);
                            }
                        }
                        break;
                    }
                    case Operation::PlaySound: {
                        const auto* res = p.resourceByName(a.target);
                        if (a.target.empty()) add(out, S::Error, "Action", v.id, obj, where, "aucun son \xC3\xA0 jouer");
                        else if (!res && !Expression::compile(a.target).valid())
                            add(out, S::Error, "Action", v.id, obj, where, "son '" + a.target + "' introuvable dans les ressources");
                        else if (res && res->kind() != MediaKind::Sound)
                            add(out, S::Error, "Action", v.id, obj, where, "'" + a.target + "' n'est pas un son");
                        break;
                    }
                    default: break;
                }
            }
        };
        actionsOf(nullptr, v.actions);
        for (const auto& o : v.objects) if (!o.actions.empty()) actionsOf(&o, o.actions);
    }
}
} // namespace

namespace {
// Lot 8 : "12,40" ou "12, 40" : une place de popup donnee en pixels.
bool isXY(std::string_view s) {
    const auto comma = s.find(',');
    if (comma == std::string_view::npos) return false;
    double x = 0, y = 0;
    return parseNumber(s.substr(0, comma), x) && parseNumber(s.substr(comma + 1), y);
}
bool validPlacement(std::string_view s) {
    if (s.empty() || isXY(s)) return true;
    return std::find(std::begin(kPopupPlacements), std::end(kPopupPlacements), s) != std::end(kPopupPlacements);
}

// Lot 8 : les popups (role, reglages, parametres), les actions qui les ouvrent,
// et les objets de saisie et des utilisateurs.
void checkLot8(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    if (const auto* start = p.view(p.config.startView); start && start->role == "popup")
        add(out, S::Warning, "Popup", start->id, kNoId, {},
            "la vue de d\xC3\xA9marrage " + start->name + " est une popup : elle s'affichera comme une vue, sans sa barre de titre");
    std::size_t userObjects = 0;
    for (const auto& v : p.views) {
        // ---- les parametres declares
        std::set<std::string> seen;
        for (const auto& prm : v.params) {
            std::string u = prm.name;
            for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!isIdentifier(prm.name))
                add(out, S::Error, "Param\xC3\xA8tre", v.id, kNoId, "parametres", "nom de param\xC3\xA8tre invalide : '" + prm.name + "'");
            else if (!seen.insert(u).second)
                add(out, S::Error, "Param\xC3\xA8tre", v.id, kNoId, "parametres", "param\xC3\xA8tre d\xC3\xA9" "clar\xC3\xA9 deux fois : " + prm.name);
            else if (p.variable(prm.name) || (plc && plc(prm.name)))
                add(out, S::Warning, "Param\xC3\xA8tre", v.id, kNoId, "parametres",
                    "le param\xC3\xA8tre " + prm.name + " masque la variable du m\xC3\xAAme nom dans cette vue");
            const std::string def = trimmedCopy(prm.defaultValue);
            if (!def.empty() && isVariablePath(def) && plc)
                if (const auto roots = scanRoots(def); !roots.empty() && !known(p, plc, roots.front()))
                    add(out, S::Error, "Param\xC3\xA8tre", v.id, kNoId, "parametres",
                        prm.name + " := " + def + " : variable inexistante dans le programme : " + roots.front());
        }
        // ---- une popup : sa place, et de quoi la fermer
        if (v.role == "popup") {
            if (!validPlacement(v.popup.placement))
                add(out, S::Warning, "Popup", v.id, kNoId, "popup_position", "position inconnue : '" + v.popup.placement + "' (centr\xC3\xA9" "e)");
            if (v.width > p.config.width || v.height > p.config.height)
                add(out, S::Warning, "Popup", v.id, kNoId, {},
                    "popup de " + std::to_string(v.width) + " x " + std::to_string(v.height) + " : plus grande que l'\xC3\xA9" "cran ("
                        + std::to_string(p.config.width) + " x " + std::to_string(p.config.height) + ")");
            const bool cross = v.popup.titleBar && v.popup.closeButton;
            bool button = false;
            const auto closes = [](const std::vector<Action>& list) {
                for (const auto& a : list)
                    if (a.operation == Operation::ClosePopup || a.operation == Operation::CloseAllPopups
                        || a.operation == Operation::ChangePopup || a.operation == Operation::PreviousPopup
                        || a.operation == Operation::Navigate)
                        return true;
                return false;
            };
            button = closes(v.actions);
            for (const auto& o : v.objects) button = button || closes(o.actions);
            for (const auto& sc : v.scripts) {
                std::string u = sc.body;
                for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (u.find("IHM_FERMER_POPUP") != std::string::npos || u.find("IHM_NAVIGUER") != std::string::npos) button = true;
            }
            if (!cross && !v.popup.closeOutside && !button)
                add(out, S::Warning, "Popup", v.id, kNoId, {},
                    "rien ne ferme cette popup : ni croix, ni clic dehors, ni bouton \xC2\xAB Fermer la popup \xC2\xBB");
        }
        // ---- les actions qui ouvrent des vues : parametres et place
        const auto actionsOf = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& a = list[i];
                const std::string where = actionLabel(i, a);
                const Id obj = o ? o->id : kNoId;
                const auto* target = operationOpensView(a.operation) ? p.viewByName(a.target) : nullptr;
                if (a.operation == Operation::Navigate && target && target->role == "popup")
                    add(out, S::Warning, "Action", v.id, obj, where,
                        target->name + " est une popup : Naviguer la montrerait comme une vue ; \xC2\xAB Ouvrir une popup \xC2\xBB l'ouvre par-dessus");
                if (a.operation == Operation::Popup && !validPlacement(a.placement))
                    add(out, S::Warning, "Action", v.id, obj, where, "position inconnue : '" + a.placement + "'");
                if (a.operation == Operation::CenterPopup && !a.target.empty() && !p.viewByName(a.target))
                    add(out, S::Error, "Action", v.id, obj, where, "vue '" + a.target + "' introuvable");
                if (target && operationTakesArguments(a.operation) && !a.value.empty()) {
                    const auto given = parseArguments(a.value);
                    if (given.empty())
                        add(out, S::Error, "Action", v.id, obj, where, "param\xC3\xA8tres illisibles : " + a.value + " (Nom := valeur; ...)");
                    for (const auto& [name, value] : given) {
                        // 1.11.22 : une faute (un parametre retire ou renomme casse ceux qui l'ouvrent avec lui).
                        if (target->params.empty())
                            add(out, S::Error, "Action", v.id, obj, where,
                                target->name + " ne d\xC3\xA9" "clare aucun param\xC3\xA8tre : retirez " + name + " de l'appel, ou d\xC3\xA9" "clarez-le dans " + target->name);
                        else if (!target->param(name))
                            add(out, S::Error, "Action", v.id, obj, where,
                                "param\xC3\xA8tre inconnu de " + target->name + " : " + name + " (retir\xC3\xA9 ou renomm\xC3\xA9 ? corrigez l'appel)");
                        // Ce que l'appelant relie doit exister (chez lui : ses propres parametres comptent).
                        for (const auto& r : scanRoots(markers::strip(value)))   // 1.11.7 : $V[0]$ -> V
                            if (!known(p, plc, r, &v))
                                add(out, S::Error, "Variable", v.id, obj, where, name + " := " + value + " : variable inexistante : " + r);
                    }
                }
            }
        };
        actionsOf(nullptr, v.actions);
        for (const auto& o : v.objects) if (!o.actions.empty()) actionsOf(&o, o.actions);
        // ---- les objets du lot 8
        for (const auto& o : v.objects) {
            switch (o.kind) {
                case Kind::InputField: {
                    if (o.text("variable").empty())
                        add(out, S::Error, "Saisie", v.id, o.id, "variable", "champ de saisie sans variable : il n'\xC3\xA9" "crira rien");
                    double lo = 0, hi = 0;
                    if (parseNumber(o.text("min"), lo) && parseNumber(o.text("max"), hi) && lo > hi)
                        add(out, S::Error, "Saisie", v.id, o.id, "min", "minimum au-dessus du maximum : aucune saisie ne passera");
                    for (const char* key : {"min", "max"}) {
                        const std::string b = trimmedCopy(o.text(key));
                        double x = 0;
                        if (!b.empty() && !parseNumber(b, x))
                            for (const auto& r : scanRoots(b))
                                if (!known(p, plc, r, &v))
                                    add(out, S::Error, "Variable", v.id, o.id, key, std::string(key) + " : variable inexistante : " + r);
                    }
                    if (o.number("maxLength", 32) < 1) add(out, S::Error, "Saisie", v.id, o.id, "maxLength", "longueur maximale nulle");
                    break;
                }
                case Kind::LoginPanel: {
                    ++userObjects;
                    const auto after = o.text("afterLogin");
                    if (!after.empty()) {
                        const auto* t = p.viewByName(after);
                        if (!t) add(out, S::Error, "Utilisateurs", v.id, o.id, "afterLogin", "vue '" + after + "' introuvable");
                        else if (t->role != "vue")
                            add(out, S::Warning, "Utilisateurs", v.id, o.id, "afterLogin",
                                after + " est un " + std::string(viewRoleLabel(t->role)) + " : on navigue vers une vue ordinaire");
                    }
                    break;
                }
                case Kind::PasswordChange:
                    ++userObjects;
                    if (o.number("minLength", 6) < 1) add(out, S::Warning, "Utilisateurs", v.id, o.id, "minLength", "longueur minimale nulle");
                    break;
                case Kind::UserManager:
                    ++userObjects;
                    if (const auto* b = o.find("buttons"))
                        for (const auto& label : splitSemicolons(b->value))
                            if (!label.empty() && std::find(std::begin(kUserButtons), std::end(kUserButtons), label) == std::end(kUserButtons))
                                add(out, S::Error, "Utilisateurs", v.id, o.id, "buttons",
                                    "bouton inconnu : '" + label + "' (Ajouter, Modifier, Supprimer, Activer, Mot de passe)");
                    break;
                case Kind::LogoutButton:
                case Kind::UserInfo:
                    ++userObjects;
                    break;
                default:
                    break;
            }
        }
    }
    if (!p.security.enabled && userObjects > 0)
        addItem(out, S::Warning, "S\xC3\xA9" "curit\xC3\xA9", {},
                std::to_string(userObjects) + " objet(s) des utilisateurs (connexion, gestion...) : la s\xC3\xA9" "curit\xC3\xA9 est "
                "\xC3\xA9teinte, ils n'auront rien \xC3\xA0 faire (Configuration > Utilisateurs)", kNoId);
}

// ---- lot 9 : les commandes et les afficheurs ------------------------------------------
//  Ce qui ne marcherait pas : une commande sans variable, des choix vides, des
//  bornes a l'envers, des etats, des zones ou des plages illisibles, un code QR
//  trop long ; et les noms que lisent leurs cases a expression (retour d'etat,
//  voyant, condition du compteur, consigne, seuils).
// ---- 1.9 : les parametres des popups ---------------------------------------------
//  Le type de chaque parametre (connu ?), sa valeur par defaut (du bon type ?),
//  le mode d'un parametre de symbole (seul Reference vaut) ; les arguments de
//  chaque action Ouvrir une popup / Changer de popup (manquant, type
//  incompatible, pas une variable pour un parametre qu'on ecrit) ; l'action
//  Appliquer copie sur reference (dans une popup, un parametre Les deux).
void checkParams19(const Project& p, const exprcheck::PlcPaths& paths, std::vector<Issue>& out) {
    using S = Issue::Severity;
    params::PlcTypes plcTypes;
    plcTypes.rootType = paths.rootType;
    plcTypes.memberType = paths.memberType;
    plcTypes.isType = paths.isStruct;
    const bool programKnown = static_cast<bool>(paths.isStruct);
    for (const auto& v : p.views) {
        for (const auto& prm : v.params) {
            if (!prm.type.empty() && !params::typeKnown(p, prm.type, plcTypes) && programKnown)
                add(out, S::Error, "Param\xC3\xA8tre", v.id, kNoId, "parametres",
                    "le param\xC3\xA8tre " + prm.name + " a un type inconnu : " + prm.type + " (ni de base, ni IHM, ni DDT de l'API)");
            const std::string def = trimmedCopy(prm.defaultValue);
            if (!def.empty()) {
                const std::string given = params::expressionType(p, nullptr, def, plcTypes);
                if (!params::typeAccepts(prm.type, given))
                    add(out, S::Error, "Param\xC3\xA8tre", v.id, kNoId, "parametres",
                        "la valeur par d\xC3\xA9" "faut de " + prm.name + " (" + def + ", " + params::normalizedType(given)
                            + ") ne convient pas au type " + params::normalizedType(prm.type));
            }
            if (isSymbolView(v) && prm.mode != ParamMode::Reference)
                add(out, S::Warning, "Param\xC3\xA8tre", v.id, kNoId, "parametres",
                    "le param\xC3\xA8tre " + prm.name + " du symbole est en mode " + std::string(params::paramModeLabel(prm.mode))
                        + " : seul R\xC3\xA9" "f\xC3\xA9rence vaut pour un symbole (l'instance est d\xC3\xA9velopp\xC3\xA9" "e)");
        }
        const auto actionsOf = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& a = list[i];
                const std::string where = actionLabel(i, a);
                const Id obj = o ? o->id : kNoId;
                if (a.operation == Operation::Popup || a.operation == Operation::ChangePopup) {
                    const auto* target = p.viewByName(a.target);
                    if (!target) continue;
                    for (const auto& pb : params::checkArguments(p, &v, *target, a.value, plcTypes)) {
                        if (pb.kind == params::ArgumentProblem::Kind::UnknownParam) continue;   // deja dit (lot 8)
                        add(out, pb.error ? S::Error : S::Warning, "Action", v.id, obj, where, pb.message);
                    }
                }
                // 1.11.7 : Maths - des references (des chemins de variables), une formule qui se lit.
                if (a.operation == Operation::Maths) {
                    for (const auto& is : actionkinds::checkMaths(actionkinds::params(a), a.value))
                        add(out, is.warning ? S::Warning : S::Error, "Action", v.id, obj, where, "maths : " + is.why);
                    if (trimmedCopy(a.target).empty()) add(out, S::Error, "Action", v.id, obj, where, "maths : aucune variable pour le r\xC3\xA9sultat");
                }
                // 1.11.7 : le clavier virtuel - une variable a saisir, des limites qui se lisent.
                if (a.operation == Operation::Keyboard) {
                    if (trimmedCopy(a.target).empty()) add(out, S::Error, "Action", v.id, obj, where, "clavier virtuel : aucune variable \xC3\xA0 saisir");
                    const auto k = actionkinds::keyboardSpec(a);
                    for (const auto* e : {&k.min, &k.max}) {
                        double x = 0;
                        if (!trimmedCopy(*e).empty() && !parseNumber(trimmedCopy(*e), x) && !Expression::compile(*e).valid())
                            add(out, S::Error, "Action", v.id, obj, where, "clavier virtuel : la limite \xC2\xAB " + *e + " \xC2\xBB ne se lit pas");
                    }
                }
                if (a.operation == Operation::ApplyCopy) {
                    const std::string t = trimmedCopy(a.target);
                    if (v.role != "popup") {
                        add(out, S::Error, "Action", v.id, obj, where,
                            "appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence : seulement dans une popup (" + v.name + " n'en est pas une)");
                        continue;
                    }
                    if (t.empty() || t == "*") {
                        const bool any = std::any_of(v.params.begin(), v.params.end(),
                                                     [](const ViewParam& x) { return x.mode == ParamMode::Both; });
                        if (!any)
                            add(out, S::Error, "Action", v.id, obj, where,
                                "appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence : la popup n'a aucun param\xC3\xA8tre en mode Les deux");
                    } else if (const auto* prm = v.param(t); !prm) {
                        add(out, S::Error, "Action", v.id, obj, where,
                            "appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence : param\xC3\xA8tre inconnu : " + t);
                    } else if (prm->mode != ParamMode::Both) {
                        add(out, S::Error, "Action", v.id, obj, where,
                            "appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence : " + prm->name + " est en mode "
                                + std::string(params::paramModeLabel(prm->mode)) + " ; choisis Les deux");
                    }
                }
            }
        };
        actionsOf(nullptr, v.actions);
        for (const auto& o : v.objects) if (!o.actions.empty()) actionsOf(&o, o.actions);
    }
}

void checkLot9(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto numberOrExpr = [](const std::string& s) {
        double x = 0;
        return !s.empty() && !parseNumber(s, x);
    };
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            const std::string label = std::string(kindLabel(o.kind));
            const auto issue = [&](S sev, const char* key, std::string msg) { add(out, sev, label, v.id, o.id, key, std::move(msg)); };
            // Les cases qui sont une expression (retour d'etat, voyant, condition du
            // compteur) : leurs noms doivent exister. Les bornes, la consigne et les
            // seuils sont des nombres (une expression s'y pose avec "=").
            for (const char* key : {"state", "lamp", "condition"}) {
                const std::string text = trimmedCopy(o.text(key));
                if (text.empty()) continue;
                for (const auto& r : scanRoots(text))
                    if (!known(p, plc, r, &v)) issue(S::Error, key, std::string(key) + " : variable inexistante dans le programme : " + r);
            }
            if (kindWritesVariable(o.kind) || kindShowsValue(o.kind))
                for (const char* key : {"min", "max", "step", "setpoint", "lowAlarm", "low", "high", "highAlarm"}) {
                    const auto* prop = o.find(key);
                    if (prop && prop->expr.empty() && numberOrExpr(trimmedCopy(prop->value)))
                        issue(S::Error, key, std::string(key) + " : '" + prop->value + "' n'est pas un nombre (une expression s'\xC3\xA9" "crit avec =)");
                }
            if (kindWritesVariable(o.kind)) {
                const bool noVar = trimmedCopy(o.text("variable")).empty();
                if (o.kind == Kind::WeeklySchedule) {
                    if (noVar && trimmedCopy(o.text("output")).empty())
                        issue(S::Warning, "variable", "programmateur sans variable ni sortie : ses plages ne servent \xC3\xA0 rien en marche");
                    const std::string out1 = trimmedCopy(o.text("output"));
                    if (!out1.empty())
                        for (const auto& r : scanRoots(out1))
                            if (!known(p, plc, r, &v)) issue(S::Error, "output", "sortie : variable inexistante dans le programme : " + r);
                } else if (noVar) {
                    issue(S::Warning, "variable", "commande sans variable : elle n'\xC3\xA9" "crira rien");
                }
            }
            double lo = 0, hi = 0;
            const bool bounds = parseNumber(o.text("min"), lo) && parseNumber(o.text("max"), hi);
            switch (o.kind) {
                case Kind::Selector: case Kind::ComboBox: case Kind::RadioGroup: {
                    const char* key = o.kind == Kind::Selector ? "positions" : "items";
                    const auto choices = choicesOf(o);
                    if (choices.empty()) { issue(S::Error, key, "aucun choix : rien \xC3\xA0 s\xC3\xA9lectionner"); break; }
                    const auto values = listItems(o.text("values"));
                    if (!values.empty() && values.size() != choices.size())
                        issue(S::Warning, "values", std::to_string(values.size()) + " valeur(s) \xC3\xA9" "crite(s) pour " + std::to_string(choices.size())
                                                        + " choix : les choix sans valeur \xC3\xA9" "crivent leur rang");
                    if (o.kind == Kind::Selector && choices.size() > 6 && o.text("style") != "boutons")
                        issue(S::Warning, "positions", "plus de 6 positions sur un s\xC3\xA9lecteur rotatif : le style \xC2\xAB boutons \xC2\xBB se lit mieux");
                    break;
                }
                case Kind::Slider: case Kind::Knob:
                    if (bounds && lo >= hi) issue(S::Error, "min", "minimum au-dessus du maximum : la course est vide");
                    if (o.number("step", 1) < 0) issue(S::Error, "step", "pas n\xC3\xA9gatif");
                    else if (bounds && o.number("step", 1) > hi - lo) issue(S::Warning, "step", "pas plus grand que la course");
                    break;
                case Kind::Bargraph: case Kind::Dial: case Kind::Thermometer: {
                    if (bounds && lo >= hi) issue(S::Error, "min", "minimum au-dessus du maximum");
                    if (o.kind == Kind::Thermometer) break;
                    std::string why;
                    const auto zones = parseZones(o.text("zones"), &why);
                    if (!why.empty()) issue(S::Error, "zones", "zones illisibles : " + why);
                    else if (bounds)
                        for (const auto& z : zones)
                            if (z.to < lo || z.from > hi)
                                issue(S::Warning, "zones", "zone " + formatNumber(z.from) + "-" + formatNumber(z.to) + " hors de l'\xC3\xA9" "chelle "
                                                               + formatNumber(lo) + "-" + formatNumber(hi));
                    break;
                }
                case Kind::MultiStateIndicator: case Kind::MultiStateText: {
                    std::string why;
                    const auto states = parseStateList(o.text("stateList"), &why);
                    if (!why.empty()) issue(S::Error, "stateList", "\xC3\xA9tats illisibles : " + why);
                    else if (states.empty()) issue(S::Warning, "stateList", "aucun \xC3\xA9tat : l'objet montrera toujours le m\xC3\xAAme aspect");
                    break;
                }
                case Kind::WeeklySchedule: {
                    const int res = resolutionMinutes(o.text("resolution", "30 min"));
                    WeekSchedule ws;
                    std::string why;
                    if (!trimmedCopy(o.text("schedule")).empty() && !parseSchedule(o.text("schedule"), res, ws, &why))
                        issue(S::Error, "schedule", "plages illisibles : " + why);
                    break;
                }
                case Kind::SevenSegment: {
                    const double digits = o.number("digits", 4), decimals = o.number("decimals", 1);
                    if (digits < 1 || digits > 16) issue(S::Warning, "digits", "de 1 \xC3\xA0 16 chiffres");
                    else if (decimals >= digits) issue(S::Warning, "decimals", "plus de d\xC3\xA9" "cimales que de chiffres : tout est apr\xC3\xA8s la virgule");
                    break;
                }
                case Kind::QrCode: {
                    const std::string text = o.text("text");
                    const auto p1 = o.find("text");
                    if (text.empty() && (!p1 || p1->expr.empty())) { issue(S::Warning, "text", "code QR sans texte"); break; }
                    const std::string level = o.text("ecLevel", "M");
                    const int cap = qrCapacity(level.empty() ? 'M' : level[0]);
                    if (text.find('{') == std::string::npos && static_cast<int>(text.size()) > cap)
                        issue(S::Error, "text", std::to_string(text.size()) + " octets : trop long pour un code QR (" + std::to_string(cap)
                                                    + " au plus en correction " + level + ")");
                    break;
                }
                // Lot 10 : les symboles de synoptique.
                case Kind::Tank: case Kind::GasBottle: case Kind::Silo: case Kind::Hopper: case Kind::Cylinder:
                    if (bounds && lo >= hi) issue(S::Error, "min", "minimum au-dessus du maximum : le niveau ne se dessinera pas");
                    break;
                case Kind::Pipe:
                    if (o.number("thickness", 14) <= 0) issue(S::Warning, "thickness", "tube d'\xC3\xA9paisseur nulle : il ne se voit pas");
                    break;
                case Kind::IsaInstrument:
                    if (trimmedCopy(o.text("function")).empty())
                        issue(S::Warning, "function", "instrument sans lettres ISA (PT, TIC, FT...) : sa bulle est vide");
                    break;
                case Kind::SystemButton: {
                    // Lot 10 : l'onglet ouvert ; une action "Parametres systeme" au clic ferait double emploi.
                    const std::string tab = o.text("tab", "R\xC3\xA9glages");
                    if (tab != "R\xC3\xA9glages" && tab != "Diagnostic" && tab != "Simulation")   // 1.9 : la page Simulation
                        issue(S::Warning, "tab", "onglet inconnu \xC2\xAB " + tab + " \xC2\xBB (R\xC3\xA9glages, Diagnostic ou Simulation) : le menu s'ouvrira sur R\xC3\xA9glages");
                    const bool twice = std::any_of(o.actions.begin(), o.actions.end(), [](const Action& a) {
                        return a.trigger == Trigger::Click && a.operation == Operation::ShowSystem;
                    });
                    if (twice) issue(S::Info, "actions", "l'objet ouvre d\xC3\xA9j\xC3\xA0 le menu Param\xC3\xA8tres syst\xC3\xA8me : l'action \xC2\xAB Param\xC3\xA8tres syst\xC3\xA8me \xC2\xBB au clic est de trop");
                    break;
                }
                case Kind::Marquee:
                    if (o.number("speed", 60) <= 0) issue(S::Warning, "speed", "vitesse nulle : le texte ne d\xC3\xA9" "filera pas");
                    break;
                case Kind::TrendArrow:
                    if (o.number("window", 10) <= 0) issue(S::Warning, "window", "fen\xC3\xAAtre nulle : la tendance sera toujours stable");
                    break;
                case Kind::Button: {
                    const std::string mode = o.text("confirmMode", "aucune");
                    if (mode == "aucune") break;
                    const bool clicks = std::any_of(o.actions.begin(), o.actions.end(), [](const Action& a) { return a.trigger == Trigger::Click; });
                    if (!clicks) issue(S::Warning, "confirmMode", "confirmation (" + mode + ") sans action au clic : il n'y a rien \xC3\xA0 confirmer");
                    if (mode == "maintien" && o.number("holdMs", 2000) < 200) issue(S::Warning, "holdMs", "maintien de moins de 200 ms : ce n'est plus une confirmation");
                    break;
                }
                default:
                    break;
            }
        }
}

// ---- lot 9 : les variables systeme et d'instances ---------------------------------------
//  Partout ou un nom se lit ou s'ecrit : SYS.X doit exister et ne s'ecrit
//  pas ; Vue.Objet.Propriete doit designer une vue, un objet (le sien ou un
//  objet emprunte a son modele) et une propriete ou une information ; ce qui
//  s'ecrit (une affectation dans un script, l'action Affecter, la variable
//  d'une commande, la sortie d'un programmateur, une recette) doit etre R/W.
void checkPublicVars(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    std::map<Id, View> composed;
    const auto borrowed = [&](const View& v) -> const View* {
        if (!inherits(p, v)) return &v;
        auto it = composed.find(v.id);
        if (it == composed.end()) it = composed.emplace(v.id, compose(p, v)).first;
        return &it->second;
    };
    // Un chemin : `write` s'il est ecrit. Le rapport dit ou (vue, objet, propriete).
    const auto check = [&](const std::string& path, bool write, const std::function<void(std::string)>& report) {
        const auto dot = path.find('.');
        if (dot == std::string::npos) return;
        const View* pv = pub::viewNamed(p, std::string_view(path).substr(0, dot));
        const auto r = pub::resolve(p, path, pv ? borrowed(*pv) : nullptr);
        using W = pub::Resolved::What;
        if (r.what == W::None) return;
        if (r.what == W::Unknown) { report(r.error); return; }
        if (r.what == W::Incomplete) { report(path + " : chemin incomplet (Vue.Objet.Propri\xC3\xA9t\xC3\xA9)"); return; }
        if (write && r.access == pub::Access::Read)
            report(path + (r.what == W::Sys ? std::string(" : variable syst\xC3\xA8me, en lecture seule")
                                            : std::string(" : en lecture seule (") + std::string(pub::accessLabel(r.access)) + ")"));
    };
    // Les trous d'un texte a trous : {expression:format}.
    const auto holes = [](const std::string& text) {
        std::vector<std::string> out1;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '{') continue;
            if (i + 1 < text.size() && text[i + 1] == '{') { ++i; continue; }
            const auto end = text.find('}', i + 1);
            if (end == std::string::npos) break;
            std::string inner = text.substr(i + 1, end - i - 1);
            // Le format apres le dernier ':' hors chaine (0.0, marche|arret).
            bool quoted = false;
            std::size_t cut = std::string::npos;
            for (std::size_t k = 0; k < inner.size(); ++k) {
                if (inner[k] == '\'') quoted = !quoted;
                else if (inner[k] == ':' && !quoted && (k + 1 >= inner.size() || inner[k + 1] != '=')) cut = k;
            }
            if (cut != std::string::npos) inner.resize(cut);
            out1.push_back(inner);
            i = end;
        }
        return out1;
    };
    const auto code = [&](const std::string& text, const std::function<void(std::string, int)>& report) {
        for (const auto& u : scriptPaths(text)) check(u.path, u.assigned, [&](std::string m) { report(std::move(m), u.line); });
    };
    const auto expr = [&](const std::string& text, bool write, const std::function<void(std::string)>& report) {
        // 1.11.1 (REP) : scriptPaths lit sans les $ ; la cible ecrite aussi ($Vanne$.Ouv ecrit Vanne.Ouv).
        for (const auto& u : scriptPaths(text)) check(u.path, write && u.path == trimmedCopy(markers::strip(text)), report);
    };
    const auto tpl = [&](const std::string& text, const std::function<void(std::string)>& report) {
        for (const auto& h : holes(text)) expr(h, false, report);
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            for (const auto& prop : o.props) {
                const auto rep = [&](std::string m) { add(out, S::Error, "Variable", v.id, o.id, prop.key, std::move(m)); };
                if (!prop.expr.empty()) expr(prop.expr, false, rep);
                if (prop.key == "text") tpl(prop.value, rep);
                if (prop.key == "variable" && !prop.value.empty()) expr(prop.value, kindWritesVariable(o.kind), rep);
                if (prop.key == "output" && !prop.value.empty()) expr(prop.value, true, rep);
                if (prop.key == "state" || prop.key == "lamp" || prop.key == "condition") expr(prop.value, false, rep);
            }
            for (std::size_t i = 0; i < o.actions.size(); ++i) {
                const auto& a = o.actions[i];
                const std::string where = actionLabel(i, a);
                const auto rep = [&](std::string m) { add(out, S::Error, "Action", v.id, o.id, where, std::move(m)); };
                if (operationWritesVariable(a.operation) && !a.target.empty()) expr(a.target, true, rep);
                expr(a.watch, false, rep);
                expr(a.guard, false, rep);
                if (a.operation == Operation::Assign || a.operation == Operation::Increment || a.operation == Operation::Decrement)
                    expr(a.value, false, rep);
                if (a.operation == Operation::Log) tpl(a.value, rep);
                if (a.operation == Operation::RunScript) code(a.value, [&](std::string m, int line) { rep((line ? "ligne " + std::to_string(line) + " : " : std::string{}) + m); });
            }
        }
        for (std::size_t i = 0; i < v.actions.size(); ++i) {
            const auto& a = v.actions[i];
            const std::string where = actionLabel(i, a);
            const auto rep = [&](std::string m) { add(out, S::Error, "Action", v.id, kNoId, where, std::move(m)); };
            if (operationWritesVariable(a.operation) && !a.target.empty()) expr(a.target, true, rep);
            expr(a.watch, false, rep);
            expr(a.guard, false, rep);
            if (a.operation == Operation::Assign) expr(a.value, false, rep);
            if (a.operation == Operation::Log) tpl(a.value, rep);
            if (a.operation == Operation::RunScript) code(a.value, [&](std::string m, int line) { rep((line ? "ligne " + std::to_string(line) + " : " : std::string{}) + m); });
        }
        for (const auto& sc : v.scripts)
            if (sc.lang == ScriptLang::ST)
                code(decl::codeOf(sc), [&](std::string m, int line) { add(out, S::Error, "Script", v.id, kNoId, sc.name, std::move(m), sc.id, line); });
    }
    for (const auto& sc : p.programs.scripts) {
        if (sc.lang == ScriptLang::ST)
            code(decl::codeOf(sc), [&](std::string m, int line) { add(out, S::Error, "Script", kNoId, kNoId, sc.name, std::move(m), sc.id, line); });
        if (sc.event == "Changement") expr(sc.watch, false, [&](std::string m) { add(out, S::Error, "Script", kNoId, kNoId, sc.name, std::move(m), sc.id); });
    }
    for (const auto& f : p.programs.functions)
        code(decl::codeOf(f), [&](std::string m, int line) {
            Issue i;
            i.severity = S::Error;
            i.category = "Fonction";
            i.property = f.name;
            i.message = std::move(m);
            i.item = f.id;
            i.line = line;
            out.push_back(std::move(i));
        });
    for (const auto& a : p.alarms) {
        expr(a.condition, false, [&](std::string m) { addItem(out, S::Error, "Alarme", a.name, "condition : " + m, a.id); });
        tpl(a.message, [&](std::string m) { addItem(out, S::Error, "Alarme", a.name, "message : " + m, a.id); });
    }
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields)
            expr(f.variable, true, [&](std::string m) { addItem(out, S::Error, "Recette", r.name, f.name + " : " + m, r.id); });
    for (const auto& a : p.history.archived)
        expr(a, false, [&](std::string m) { addItem(out, S::Error, "Historique", "archivees", m, kNoId); });
}
} // namespace

// ---- lot 10 : les symboles reutilisables ------------------------------------------
//  Un symbole : pose quelque part, qui ne se contient pas, pas vue de demarrage.
//  Une instance : son symbole existe et en est un ; ses arguments sont des
//  parametres du symbole, et les variables qu'ils citent existent ; chaque
//  parametre recoit une valeur (l'argument, ou sa valeur par defaut).
void checkSymbols(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    for (const auto& v : p.views) {
        if (isSymbolView(v)) {
            if (instancesOf(p, v.name).empty())
                add(out, S::Info, "Symbole", v.id, kNoId, {}, "symbole pos\xC3\xA9 nulle part (aucune instance)");
            if (symbolContains(p, v, v.name))
                add(out, S::Error, "Symbole", v.id, kNoId, {},
                    "le symbole se contient lui-m\xC3\xAAme (directement ou par un autre) : son dessin s'arr\xC3\xAAte au "
                        + std::to_string(kMaxSymbolDepth) + "e niveau");
            if (p.config.startView == v.id)
                add(out, S::Error, "Symbole", v.id, kNoId, {}, "vue de d\xC3\xA9marrage : un symbole se pose dans une vue, il ne s'ouvre pas");
            if (!isIdentifier(v.name))
                add(out, S::Warning, "Symbole", v.id, kNoId, {}, "nom de symbole '" + v.name + "' : lettres, chiffres et _ seulement");
        }
        for (const auto& o : v.objects) {
            if (o.kind != Kind::SymbolInstance) continue;
            const std::string name = trimmedCopy(o.text("symbol"));
            if (name.empty()) {
                add(out, S::Error, "Symbole", v.id, o.id, "symbol", "instance sans symbole : choisis-en un (propri\xC3\xA9t\xC3\xA9 Symbole)");
                continue;
            }
            const View* sv = p.viewByName(name);
            if (!sv) {
                add(out, S::Error, "Symbole", v.id, o.id, "symbol", "symbole introuvable : " + name);
                continue;
            }
            if (!isSymbolView(*sv)) {
                add(out, S::Error, "Symbole", v.id, o.id, "symbol",
                    name + " n'est pas un symbole (r\xC3\xB4le " + std::string(viewRoleLabel(sv->role)) + ")");
                continue;
            }
            if (o.number("w") <= 0 || o.number("h") <= 0)
                add(out, S::Error, "Symbole", v.id, o.id, "w", "instance de taille nulle : elle ne se voit pas");
            std::string declared;
            for (const auto& prm : sv->params) declared += (declared.empty() ? "" : ", ") + prm.name;
            const auto given = parseArguments(o.text("params"));
            for (const auto& [argName, value] : given) {
                if (!sv->param(argName)) {
                    add(out, S::Error, "Symbole", v.id, o.id, "params",        // 1.11.22 : une faute (le parametre a pu etre retire)
                        "argument inconnu du symbole " + name + " : " + argName
                            + (declared.empty() ? std::string(" (il ne d\xC3\xA9" "clare aucun param\xC3\xA8tre)") : " (ses param\xC3\xA8tres : " + declared + ")"));
                    continue;
                }
                if (plc)
                    for (const auto& r : scanRoots(value))
                        if (!known(p, plc, r, &v))
                            add(out, S::Error, "Variable", v.id, o.id, "params", "argument " + argName + " : variable inexistante dans le programme : " + r);
            }
            for (const auto& prm : sv->params) {
                bool has = !trimmedCopy(prm.defaultValue).empty();
                for (const auto& g : given) has = has || sv->param(g.first) == &prm;
                if (!has)
                    add(out, S::Warning, "Symbole", v.id, o.id, "params",
                        "param\xC3\xA8tre " + prm.name + " sans valeur (ni argument, ni valeur par d\xC3\xA9" "faut) : ses expressions ne se lieront pas");
            }
        }
    }
}

// ============================================================== 1.9 =========
//  Les alarmes des objets (HmiObjectAlarms.hpp) : chaque alarme d'un symbole
//  verifiee une fois dans le symbole (nom, condition, membres, texte a trous) ;
//  sur une instance, seulement ce que son argument rend faux ; deux alarmes de
//  meme nom sur un objet ; une surcharge qui nomme une alarme qui n'existe plus ;
//  une condition surchargee invalide ; une popup a parametres (rien de genere).
void checkObjectAlarms(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto unknownIn = [&](const std::string& expr, const View* v, const View* symbol) {
        std::set<std::string> missing;
        for (const auto& r : scanRoots(expr))
            if (!(symbol && symbol->param(r)) && !known(p, plc, r, v)) missing.insert(r);
        return missing;
    };
    // ---- dans chaque symbole, une fois
    std::map<std::string, std::set<std::string>> symbolMissing;   // "Sym/Alarme" -> les racines deja dites
    for (const auto& v : p.views) {
        if (!isSymbolView(v)) continue;
        // 1.11.20 : ses fonctions de meme nom (des surcharges) : de formes differentes, pas virtuelles.
        for (const auto& c : overload::clashes(v.functions))
            for (const auto& f : v.functions)
                if (f.id == c.function) add(out, S::Error, "Fonction", v.id, kNoId, v.name + "." + f.name, c.message);
        std::set<std::string> names;
        for (const auto& a : v.alarms) {
            const std::string where = "alarme " + a.name;
            if (!isIdentifier(a.name)) add(out, S::Error, "Alarme", v.id, kNoId, {}, "alarme du symbole : nom invalide '" + a.name + "'");
            else if (!names.insert(a.name).second) add(out, S::Error, "Alarme", v.id, kNoId, {}, "deux alarmes " + a.name + " dans le symbole");
            auto& said = symbolMissing[v.name + "/" + a.name];
            if (a.condition.empty()) {
                add(out, S::Error, "Alarme", v.id, kNoId, {}, where + " : aucune condition, elle n'appara\xC3\xAEtra jamais");
            } else {
                const auto e = Expression::compile(a.condition);
                if (!e.valid()) add(out, S::Error, "Alarme", v.id, kNoId, {}, where + " : condition " + a.condition + " : " + e.error());
                for (const auto& r : unknownIn(a.condition, &v, &v)) {
                    add(out, S::Error, "Alarme", v.id, kNoId, {}, where + " : variable inexistante dans la condition : " + r);
                    said.insert(r);
                }
            }
            const auto t = TextTemplate::compile(a.message);
            for (const auto& err : t.errors()) add(out, S::Error, "Alarme", v.id, kNoId, {}, where + " : message : " + err);
            for (const auto& r : t.roots())
                if (!v.param(r) && !known(p, plc, r, &v)) {
                    add(out, S::Error, "Alarme", v.id, kNoId, {}, where + " : variable inexistante dans le message : " + r);
                    said.insert(r);
                }
            if (a.priority < 1 || a.priority > kAlarmPriorities)
                add(out, S::Warning, "Alarme", v.id, kNoId, {}, where + " : priorit\xC3\xA9 hors de 1 \xC3\xA0 4");
        }
    }
    // ---- les alarmes generees
    const auto all = objectAlarms(p);
    std::map<std::string, int> seen;
    for (const auto& oa : all) {
        const std::string where = "alarme " + oa.def.name;
        if (++seen[oa.def.name] == 2)
            add(out, S::Error, "Alarme", oa.viewId, oa.objectId, {}, "deux alarmes de m\xC3\xAAme nom sur l'objet : " + oa.def.name);
        // 1.11 (REP) : une condition a repere ($V[0]$.Def) se verifie comme les autres.
        const View* v = p.view(oa.viewId);
        const bool conditionOverridden = std::find(oa.overridden.begin(), oa.overridden.end(), "condition") != oa.overridden.end();
        if (conditionOverridden) {
            if (oa.def.condition.empty()) {
                add(out, S::Error, "Alarme", oa.viewId, oa.objectId, {}, where + " : condition surcharg\xC3\xA9" "e vide");
            } else {
                const auto e = Expression::compile(oa.def.condition);
                if (!e.valid())
                    add(out, S::Error, "Alarme", oa.viewId, oa.objectId, {},
                        where + " : condition surcharg\xC3\xA9" "e " + oa.def.condition + " : " + e.error());
                for (const auto& r : unknownIn(oa.def.condition, v, nullptr))
                    add(out, S::Error, "Alarme", oa.viewId, oa.objectId, {}, where + " : variable inexistante dans la condition surcharg\xC3\xA9" "e : " + r);
            }
        } else if (oa.source == ObjectAlarmSource::Symbol && !oa.def.condition.empty()) {
            // Ce que l'argument de l'instance rend faux (le reste est dit dans le symbole).
            const auto it = symbolMissing.find(oa.symbol + "/" + oa.localName);
            for (const auto& r : unknownIn(oa.def.condition, v, nullptr))
                if (it == symbolMissing.end() || !it->second.count(r))
                    add(out, S::Error, "Alarme", oa.viewId, oa.objectId, "params", where + " : variable inexistante dans la condition : " + r);
        }
    }
    // ---- les surcharges qui ne visent plus rien ; les popups a parametres
    for (const auto& v : p.views) {
        bool carriers = false;
        for (const auto& o : v.objects) {
            carriers = carriers || kindIsSynoptic(o.kind) || o.kind == Kind::SymbolInstance;
            if (o.alarmOverrides.empty() || !viewGeneratesAlarms(v)) continue;
            std::set<std::pair<std::string, std::string>> exists;
            for (const auto& oa : objectAlarmsOf(p, v, o)) exists.insert({oa.path, oa.localName});
            for (const auto& la : libraryAlarms(o)) exists.insert({std::string{}, la.def.name});
            for (const auto& ov : o.alarmOverrides)
                if (!exists.count({ov.path, ov.alarm}))
                    add(out, S::Warning, "Alarme", v.id, o.id, {},
                        "surcharge de l'alarme " + (ov.path.empty() ? std::string{} : ov.path + ".") + ov.alarm
                            + " : cette alarme n'existe plus, la surcharge est ignor\xC3\xA9" "e");
        }
        if (carriers && !isSymbolView(v) && !v.params.empty())
            add(out, S::Info, "Alarme", v.id, kNoId, {},
                "vue \xC3\xA0 param\xC3\xA8tres : les alarmes de ses objets ne sont pas g\xC3\xA9n\xC3\xA9r\xC3\xA9" "es (leurs arguments ne sont connus qu'\xC3\xA0 l'ouverture)");
    }
}

// ============================================================== lot 11 ======
//  Les graphiques (leurs expressions, leurs echelles, la courbe de reference,
//  les etats), les objets des alarmes (les zones, l'alarme de la consigne),
//  la production (les compteurs, la recette, la source d'un export), les
//  consignes et les sons des alarmes, les actions du lot (mettre de cote,
//  exporter) ; la vanne reglante et les seuils des contenants.
namespace {

bool numberOrEmpty(const Object& o, std::string_view key, double& out) {
    const auto* p = o.find(key);
    if (!p || !p->expr.empty()) return false;
    const std::string t = trimmedCopy(p->value);
    return !t.empty() && parseNumber(t, out);
}

bool validExportSource(const Project& p, const View& v, std::string_view source, std::string* why) {
    std::string s = trimmedCopy(source);
    for (const auto src : kExportSources) if (s == src) return true;
    if (s == "evenements" || s == "systeme") return true;
    if (s.rfind("recette:", 0) == 0) {
        if (p.recipeByName(trimmedCopy(s.substr(8)))) return true;
        if (why) *why = "recette '" + trimmedCopy(s.substr(8)) + "' introuvable";
        return false;
    }
    if (s.rfind("objet:", 0) == 0) {
        const std::string name = trimmedCopy(s.substr(6));
        if (v.objectByName(name)) return true;
        if (why) *why = "objet '" + name + "' introuvable dans la vue " + v.name;
        return false;
    }
    if (why) *why = "source inconnue : " + s;
    return false;
}

} // namespace

void checkLot11(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    // 1.9 : une alarme d'objet (generee : Vue.Objet.Alarme) se vise aussi par son nom.
    std::set<std::string> generatedNames;
    bool generatedReady = false;
    const auto generatedAlarm = [&](const std::string& name) {
        if (!generatedReady) {
            for (const auto& oa : objectAlarms(p)) generatedNames.insert(oa.def.name);
            generatedReady = true;
        }
        return generatedNames.count(name) > 0;
    };
    std::set<std::string> groups;
    for (const auto& a : p.alarms) if (!a.group.empty()) groups.insert(a.group);
    // 1.9 : un filtre (groupe d'objet, motif, liste, symbole:) prend-il au moins une
    // alarme, du projet ou d'un objet ? Les alarmes des objets, faites une fois.
    std::vector<ObjectAlarm> generatedList;
    bool generatedListReady = false;
    const auto generatedAlarms = [&]() -> const std::vector<ObjectAlarm>& {
        if (!generatedListReady) {
            generatedList = objectAlarms(p);
            generatedListReady = true;
        }
        return generatedList;
    };
    const auto filterTakes = [&](const std::string& filter) {
        if (groups.count(filter)) return true;
        for (const auto& a : p.alarms) if (alarmGroupMatches(a.group, {}, {}, filter)) return true;
        for (const auto& oa : generatedAlarms())
            if (oa.active && alarmGroupMatches(oa.def.group, oa.objectGroup, oa.symbols, filter)) return true;
        return false;
    };
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            const std::string label = std::string(kindLabel(o.kind));
            const auto issue = [&](S sev, const char* key, std::string msg) { add(out, sev, label, v.id, o.id, key, std::move(msg)); };
            const auto exprCheck = [&](const char* key, const std::string& e, const std::string& what) {
                const auto c = Expression::compile(e);
                if (!c.valid()) { issue(S::Error, key, what + " " + e + " : " + c.error()); return; }
                for (const auto& r : scanRoots(e))
                    if (!known(p, plc, r, &v)) issue(S::Error, key, what + " : variable inexistante dans le programme : " + r);
            };
            const auto listCheck = [&](const char* key, const char* empty) {
                const auto items = chartItems(o, key);
                if (items.empty() && empty) issue(S::Warning, key, empty);
                for (const auto& it : items) exprCheck(key, it.expression, "\xC3\xA9l\xC3\xA9ment");
                return items.size();
            };
            double lo = 0, hi = 0;
            const bool bounds = numberOrEmpty(o, "min", lo) && numberOrEmpty(o, "max", hi);
            switch (o.kind) {
                // ---- les graphiques
                case Kind::BarChart: case Kind::PieChart: {
                    listCheck("variables", "aucune valeur : le graphique restera vide");
                    if (o.kind == Kind::BarChart && o.text("scale", "fixe") == "fixe" && bounds && lo >= hi)
                        issue(S::Error, "min", "\xC3\xA9" "chelle : le minimum doit \xC3\xAAtre sous le maximum");
                    double low = 0, high = 0;
                    if (o.kind == Kind::BarChart && numberOrEmpty(o, "low", low) && numberOrEmpty(o, "high", high) && low >= high)
                        issue(S::Warning, "low", "seuil bas au-dessus du seuil haut");
                    if (o.kind == Kind::PieChart && o.number("hole", 0) > 90) issue(S::Warning, "hole", "trou de plus de 90 % : l'anneau ne se voit plus");
                    break;
                }
                case Kind::RadarChart: {
                    const auto n = listCheck("variables", "aucun axe : le radar restera vide");
                    if (n > 0 && n < 3) issue(S::Info, "variables", "moins de trois axes : un radar se lit mal (des barres ?)");
                    const auto refs = chartItems(o, "references");
                    for (const auto& r : refs) exprCheck("references", r.expression, "consigne");
                    if (!refs.empty() && refs.size() != n)
                        issue(S::Warning, "references", std::to_string(refs.size()) + " valeur(s) de consigne pour " + std::to_string(n) + " axe(s)");
                    if (bounds && lo >= hi) issue(S::Error, "min", "\xC3\xA9" "chelle : le minimum doit \xC3\xAAtre sous le maximum");
                    break;
                }
                case Kind::XYChart: {
                    listCheck("variables", "aucune courbe (Y) : le graphique restera vide");
                    const std::string x = trimmedCopy(o.find("xVariable") && !o.find("xVariable")->expr.empty() ? o.find("xVariable")->expr : o.text("xVariable"));
                    if (x.empty()) issue(S::Error, "xVariable", "aucune variable X : aucun point ne sera trac\xC3\xA9");
                    else exprCheck("xVariable", x, "variable X");
                    if (const std::string r = trimmedCopy(o.text("reset")); !r.empty()) exprCheck("reset", r, "remise \xC3\xA0 z\xC3\xA9ro");
                    std::vector<std::pair<double, double>> ref;
                    std::string why;
                    if (!parseXYPoints(o.text("reference"), ref, &why)) issue(S::Error, "reference", "courbe de r\xC3\xA9" "f\xC3\xA9rence : " + why);
                    if (o.text("scale", "fixe") == "fixe") {
                        if (o.number("xmin", 0) >= o.number("xmax", 100)) issue(S::Error, "xmin", "axe X : le minimum doit \xC3\xAAtre sous le maximum");
                        if (o.number("ymin", 0) >= o.number("ymax", 100)) issue(S::Error, "ymin", "axe Y : le minimum doit \xC3\xAAtre sous le maximum");
                    }
                    if (o.number("maxPoints", 300) < 2) issue(S::Warning, "maxPoints", "moins de deux points gard\xC3\xA9s : pas de trace");
                    break;
                }
                case Kind::StateChart: {
                    listCheck("variables", "aucune ligne : le chronogramme restera vide");
                    std::string why;
                    (void)parseStateList(o.text("stateList"), &why);
                    if (!why.empty()) issue(S::Error, "stateList", "\xC3\xA9tats illisibles : " + why);
                    if (o.number("duration", 60) <= 0) issue(S::Error, "duration", "dur\xC3\xA9" "e nulle");
                    break;
                }
                case Kind::Histogram: {
                    const std::string var = trimmedCopy(o.find("variable") && !o.find("variable")->expr.empty() ? o.find("variable")->expr : o.text("variable"));
                    if (var.empty()) issue(S::Error, "variable", "aucune variable mesur\xC3\xA9" "e : l'histogramme restera vide");
                    else if (!Expression::compile(var).valid()) issue(S::Error, "variable", "variable mesur\xC3\xA9" "e illisible : " + var);
                    if (bounds && lo >= hi) issue(S::Error, "min", "\xC3\xA9" "chelle : le minimum doit \xC3\xAAtre sous le maximum");
                    const double bins = o.number("bins", 10);
                    if (bins < 1 || bins > 200) issue(S::Warning, "bins", "de 1 \xC3\xA0 200 classes");
                    double low = 0, high = 0;
                    const bool hasLow = numberOrEmpty(o, "low", low), hasHigh = numberOrEmpty(o, "high", high);
                    if (hasLow && hasHigh && low >= high) issue(S::Error, "low", "tol\xC3\xA9rance basse au-dessus de la tol\xC3\xA9rance haute");
                    if (bounds && ((hasLow && (low < lo || low > hi)) || (hasHigh && (high < lo || high > hi))))
                        issue(S::Warning, "low", "une tol\xC3\xA9rance est hors de l'\xC3\xA9" "chelle : elle ne se dessinera pas");
                    if (o.number("samplePeriod", 1000) < 50) issue(S::Warning, "samplePeriod", "moins de 50 ms entre deux mesures : ramen\xC3\xA9 \xC3\xA0 50 ms");
                    break;
                }
                // ---- les objets des alarmes
                case Kind::AlarmBanner: case Kind::AlarmCounter: case Kind::AlarmSummary: case Kind::AlarmInstruction: case Kind::AlarmStats: {
                    if (p.alarms.empty() && generatedAlarms().empty())
                        issue(S::Info, "group", "aucune alarme d\xC3\xA9" "finie (Configuration > Alarmes) : l'objet restera vide");
                    const auto* g = o.find("group");
                    if (g && g->expr.empty() && !trimmedCopy(g->value).empty() && trimmedCopy(g->value) != "*" && !filterTakes(trimmedCopy(g->value)))
                        issue(S::Warning, "group", "aucune alarme du groupe \xC2\xAB " + trimmedCopy(g->value) + " \xC2\xBB");
                    if (o.kind == Kind::AlarmBanner && o.text("show").find("filement") != std::string::npos && o.number("period", 4000) < 500)
                        issue(S::Warning, "period", "d\xC3\xA9" "filement de moins de 500 ms : illisible");
                    if (o.kind == Kind::AlarmSummary) {
                        for (const auto& z : splitSemicolons(o.text("groups"))) {
                            const std::string zone = trimmedCopy(z);
                            if (!zone.empty() && !filterTakes(zone)) issue(S::Warning, "groups", "zone \xC2\xAB " + zone + " \xC2\xBB : aucune alarme de ce groupe");
                        }
                        if (zonesOf(p, o.text("groups")).empty())
                            issue(S::Warning, "groups", "aucune zone : les alarmes n'ont pas de groupe (Configuration > Alarmes)");
                    }
                    if (o.kind == Kind::AlarmInstruction) {
                        const std::string name = trimmedCopy(o.text("alarm"));
                        const auto* a = name.empty() ? nullptr : p.alarmByName(name);
                        if (!name.empty() && !a) issue(S::Error, "alarm", "alarme '" + name + "' introuvable");
                        else if (a && a->instruction.empty()) issue(S::Info, "alarm", "l'alarme " + name + " n'a pas de consigne");
                        else if (name.empty() && std::none_of(p.alarms.begin(), p.alarms.end(), [](const AlarmDef& d) { return !d.instruction.empty(); }))
                            issue(S::Info, "alarm", "aucune alarme n'a de consigne (Configuration > Alarmes : Consigne)");
                    }
                    if (o.kind == Kind::AlarmStats) {
                        if (o.number("top", 5) < 1) issue(S::Warning, "top", "aucune ligne demand\xC3\xA9" "e");
                        const std::string range = o.text("range", "depuis le lancement");
                        if (range.find("lancement") == std::string::npos && !p.history.alarms)
                            issue(S::Warning, "range", "l'historique des alarmes n'est pas tenu (Configuration > Historiques) : seulement depuis le lancement");
                    }
                    break;
                }
                // ---- la production
                case Kind::ProductionCounter: {
                    for (const char* key : {"good", "bad", "running"}) {
                        const auto* prop = o.find(key);
                        const std::string e = trimmedCopy(prop ? (prop->expr.empty() ? prop->value : prop->expr) : std::string{});
                        if (e.empty()) {
                            if (std::string_view(key) == "good") issue(S::Error, key, "aucun compteur de pi\xC3\xA8" "ces bonnes : rien ne sera compt\xC3\xA9");
                            continue;
                        }
                        exprCheck(key, e, key);
                    }
                    double ideal = 0;
                    if (numberOrEmpty(o, "idealRate", ideal) && ideal <= 0)
                        issue(S::Warning, "idealRate", "sans cadence nominale, la performance (et le TRS) vaut 0");
                    std::vector<int> shifts;
                    std::string why;
                    if (!parseShifts(o.text("shifts"), shifts, &why)) issue(S::Error, "shifts", "d\xC3\xA9" "buts de poste : " + why);
                    break;
                }
                case Kind::VariableTable:
                    listCheck("variables", "aucune variable : le tableau restera vide");
                    break;
                case Kind::RecipeEditor: {
                    const std::string name = trimmedCopy(markers::strip(o.text("recipe"), markers::Mode::Text));   // 1.11 (REP-1)
                    const auto* r = name.empty() ? nullptr : p.recipeByName(name);
                    if (name.empty()) issue(S::Error, "recipe", "aucune recette : l'\xC3\xA9" "diteur restera vide");
                    else if (!r) issue(S::Error, "recipe", "recette '" + name + "' introuvable");
                    else if (r->records.empty()) issue(S::Info, "recipe", "la recette " + name + " n'a aucun jeu : \xC2\xAB Nouveau \xC2\xBB en cr\xC3\xA9" "era un");
                    for (const auto& b : splitSemicolons(o.text("buttons"))) {
                        const std::string t = trimmedCopy(b);
                        if (!t.empty() && std::find(std::begin(kRecipeEditorButtons), std::end(kRecipeEditorButtons), t) == std::end(kRecipeEditorButtons))
                            issue(S::Warning, "buttons", "bouton inconnu : " + t + " (Enregistrer, Appliquer, Lire, Annuler, Nouveau)");
                    }
                    break;
                }
                case Kind::ExportButton: {
                    std::string why;
                    if (!validExportSource(p, v, o.text("exportSource", "alarmes"), &why)) issue(S::Error, "exportSource", "donn\xC3\xA9" "es export\xC3\xA9" "es : " + why);
                    if (!exportFormatFrom(o.text("fileFormat", "CSV"))) issue(S::Warning, "fileFormat", "format inconnu : " + o.text("fileFormat") + " (CSV sera \xC3\xA9" "crit)");
                    const auto errors = TextTemplate::compile(o.text("fileName")).errors();
                    if (!errors.empty()) issue(S::Error, "fileName", "nom du fichier : " + errors.front());
                    if (trimmedCopy(o.text("fileName")).empty()) issue(S::Info, "fileName", "nom vide : le fichier s'appellera export");
                    break;
                }
                // ---- la vanne reglante, les seuils
                case Kind::Valve: {
                    const auto* op = o.find("opening");
                    if (op && op->expr.empty() && !trimmedCopy(op->value).empty()) {
                        double x = 0;
                        if (!parseNumber(trimmedCopy(op->value), x)) issue(S::Error, "opening", "ouverture : '" + op->value + "' n'est pas un nombre (une expression s'\xC3\xA9" "crit avec =)");
                        else if (x < 0 || x > 100) issue(S::Warning, "opening", "ouverture hors de 0 \xC3\xA0 100 %");
                    }
                    if (o.text("valveType").rfind("r\xC3\xA9glante", 0) == 0 && (!op || (op->expr.empty() && trimmedCopy(op->value).empty())))
                        issue(S::Info, "opening", "vanne r\xC3\xA9glante sans ouverture : elle se dessine en tout ou rien");
                    for (const char* key : {"opening", "moving"})
                        if (const auto* pr = o.find(key); pr && pr->expr.empty() && !trimmedCopy(pr->value).empty()) {
                            double x = 0;
                            if (!parseNumber(trimmedCopy(pr->value), x)) exprCheck(key, trimmedCopy(pr->value), key);
                        }
                    break;
                }
                // ---- 1.10.4 : la vanne 3 voies - sa voie active (un entier, ou deux booleens)
                case Kind::ThreeWayValve: {
                    const std::string mode = o.text("positionMode", "entier");
                    const bool twoBools = mode.rfind("deux", 0) == 0;
                    if (!twoBools && mode != "entier")
                        issue(S::Warning, "positionMode", "position par '" + mode + "' inconnue : entier ou deux bool\xC3\xA9" "ens (entier est pris)");
                    if (twoBools) {
                        const auto* a = o.find("positionA");
                        const auto* b = o.find("positionB");
                        const auto empty = [&](const Prop* pr) { return !pr || (pr->expr.empty() && trimmedCopy(pr->value).empty()); };
                        if (empty(a) && empty(b))
                            issue(S::Info, "positionA", "position par deux bool\xC3\xA9" "ens, A et B vides : la vanne reste ferm\xC3\xA9" "e");
                    } else if (const auto* pr = o.find("value"); pr && pr->expr.empty() && !trimmedCopy(pr->value).empty()) {
                        const std::string t = trimmedCopy(pr->value);
                        double x = 0;
                        const bool number = parseNumber(t, x);
                        const long n = number ? std::lround(x) : -1;
                        const bool known = number ? (n == 0 || n == 1 || n == 2 || n == 3 || n == 12 || n == 13 || n == 23 || n == 21 || n == 31 || n == 32)
                                                  : (t.find("1-2") != std::string::npos || t.find("1-3") != std::string::npos
                                                     || t.find("2-3") != std::string::npos || t.find("2-1") != std::string::npos
                                                     || t.find("3-1") != std::string::npos || t.find("3-2") != std::string::npos);
                        if (!known && parseBool(t, false) != parseBool(t, true))
                            issue(S::Warning, "value", "voie active '" + t + "' inconnue : 0 ferm\xC3\xA9" "e, 1 (ou 12) 1-2, 2 (ou 13) 1-3, 3 (ou 23) 2-3 - elle se dessine ferm\xC3\xA9" "e");
                    }
                    if (const auto* pr = o.find("moving"); pr && pr->expr.empty() && !trimmedCopy(pr->value).empty()) {
                        double x = 0;
                        if (!parseNumber(trimmedCopy(pr->value), x)) exprCheck("moving", trimmedCopy(pr->value), "moving");
                    }
                    break;
                }
                case Kind::Tank: case Kind::Silo: case Kind::Hopper: case Kind::GasBottle: {
                    double a = 0, b = 0;
                    for (const char* key : {"lowAlarm", "low", "high", "highAlarm"}) {
                        const auto* pr = o.find(key);
                        if (pr && pr->expr.empty() && !trimmedCopy(pr->value).empty() && !parseNumber(trimmedCopy(pr->value), a))
                            issue(S::Error, key, std::string(key) + " : '" + pr->value + "' n'est pas un nombre");
                    }
                    if (numberOrEmpty(o, "low", a) && numberOrEmpty(o, "high", b) && a >= b) issue(S::Warning, "low", "seuil bas au-dessus du seuil haut");
                    if (numberOrEmpty(o, "lowAlarm", a) && numberOrEmpty(o, "low", b) && a > b) issue(S::Warning, "lowAlarm", "alarme basse au-dessus du seuil bas");
                    if (numberOrEmpty(o, "high", a) && numberOrEmpty(o, "highAlarm", b) && a > b) issue(S::Warning, "highAlarm", "seuil haut au-dessus de l'alarme haute");
                    break;
                }
                default:
                    break;
            }
            // Les actions du lot 11.
            for (std::size_t i = 0; i < o.actions.size(); ++i) {
                const auto& a = o.actions[i];
                const std::string where = actionLabel(i, a);
                if ((a.operation == Operation::ShelveAlarm || a.operation == Operation::UnshelveAlarm) && !a.target.empty() && a.target != "*"
                    && a.target.rfind("groupe:", 0) != 0 && !p.alarmByName(a.target) && !generatedAlarm(a.target))
                    add(out, S::Error, "Action", v.id, o.id, where, "alarme '" + a.target + "' introuvable");
                if (a.operation == Operation::ShelveAlarm) {
                    const std::string minutes = trimmedCopy(a.value.substr(0, a.value.find(';')));
                    double m = 0;
                    if (!minutes.empty() && !parseNumber(minutes, m) && !Expression::compile(minutes).valid())
                        add(out, S::Error, "Action", v.id, o.id, where, "dur\xC3\xA9" "e illisible : " + minutes);
                    else if (parseNumber(minutes, m) && m > p.alarmSettings.maxShelveMin)
                        add(out, S::Info, "Action", v.id, o.id, where,
                            "au-del\xC3\xA0 de la limite (" + std::to_string(p.alarmSettings.maxShelveMin) + " min) : elle sera limit\xC3\xA9" "e");
                }
                if (a.operation == Operation::Export) {
                    std::string why;
                    if (!validExportSource(p, v, a.target.empty() ? std::string("alarmes") : a.target, &why))
                        add(out, S::Error, "Action", v.id, o.id, where, "exporter : " + why);
                }
            }
        }
    // Les consignes des alarmes, les sons.
    for (const auto& a : p.alarms) {
        if (a.instruction.empty()) continue;
        const auto t = TextTemplate::compile(a.instruction);
        for (const auto& err : t.errors()) addItem(out, S::Error, "Alarme", a.name, "consigne : " + err, a.id);
        for (const auto& r : t.roots())
            if (!known(p, plc, r)) addItem(out, S::Error, "Alarme", a.name, "variable inexistante dans la consigne : " + r, a.id);
    }
    bool anySound = false;
    for (std::size_t k = 0; k < p.alarmSettings.sounds.size(); ++k) {
        const std::string& name = p.alarmSettings.sounds[k];
        if (name.empty()) continue;
        anySound = true;
        const auto* r = p.resourceByName(name);
        if (!r) addItem(out, S::Error, "Alarme", name, "son de la priorit\xC3\xA9 " + std::to_string(k + 1) + " introuvable : " + name, kNoId);
        else if (r->kind() != MediaKind::Sound)
            addItem(out, S::Error, "Alarme", name, "priorit\xC3\xA9 " + std::to_string(k + 1) + " : " + name + " n'est pas un son", kNoId);
    }
    if (p.alarmSettings.repeatS > 0 && !anySound)
        addItem(out, S::Warning, "Alarme", {}, "r\xC3\xA9p\xC3\xA9tition du son r\xC3\xA9gl\xC3\xA9" "e, mais aucun son d'alarme (Configuration > Alarmes)", kNoId);
}


// ---- lot 12 : la navigation et la structure ------------------------------------------------
namespace {
// "Armoires[0].prete" -> "Armoires" : la racine d'un chemin de variable.
std::string rootOf(const std::string& path) {
    const auto end = path.find_first_of(".[ ");
    return path.substr(0, end);
}
} // namespace

void checkLot12(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    std::set<std::string> groups;
    for (const auto& a : p.alarms) if (!a.group.empty()) groups.insert(a.group);
    // Les vues parentes : introuvables, en boucle.
    for (const auto& v : p.views) {
        if (v.upView == kNoId) continue;
        if (!p.view(v.upView)) { add(out, S::Warning, "Vue", v.id, kNoId, "vue_parente", "vue parente introuvable (fil d'Ariane)"); continue; }
        std::set<Id> seen{v.id};
        for (const View* up = p.view(v.upView); up; up = up->upView != kNoId ? p.view(up->upView) : nullptr) {
            if (!seen.insert(up->id).second) {
                add(out, S::Warning, "Vue", v.id, kNoId, "vue_parente", "les vues parentes tournent en rond : le fil d'Ariane s'arr\xC3\xAAte \xC3\xA0 la boucle");
                break;
            }
        }
    }
    // Les vues de demarrage des groupes.
    for (const auto& g : p.security.groups)
        if (g.startView != kNoId && !p.view(g.startView))
            addItem(out, S::Warning, "Utilisateurs", g.name, "groupe " + g.name + " : sa vue de d\xC3\xA9marrage n'existe plus", g.id);
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            const std::string label = std::string(kindLabel(o.kind));
            const auto issue = [&](S sev, const char* key, std::string msg) { add(out, sev, label, v.id, o.id, key, std::move(msg)); };
            // Un enfant d'un conteneur a onglets sur une page qui n'existe pas.
            if (const auto* parent = o.parent != kNoId ? v.object(o.parent) : nullptr; parent && parent->kind == Kind::TabContainer) {
                const auto pages = tabLabels(*parent).size();
                if (tabPageOf(o) > static_cast<int>(pages))
                    issue(S::Warning, "tabPage", "page " + std::to_string(tabPageOf(o)) + " : le conteneur " + parent->name + " n'en a que "
                                                     + std::to_string(pages) + " (l'objet ne se verra jamais)");
            }
            // Un style nomme inconnu.
            if (const auto* st = o.find("namedStyle"); st && !trimmedCopy(st->value).empty() && !p.styleByName(trimmedCopy(st->value)))
                issue(S::Warning, "namedStyle", "style '" + trimmedCopy(st->value) + "' introuvable (IHM > Styles)");
            switch (o.kind) {
                case Kind::NavBar: {
                    const auto names = listItems(o.text("views"));
                    for (const auto& n : names) {
                        const View* target = p.viewByName(n);
                        if (!target) issue(S::Error, "views", "vue '" + n + "' introuvable");
                        else if (target->role != "vue") issue(S::Warning, "views", n + " n'est pas une vue ordinaire (" + std::string(viewRoleLabel(target->role)) + ")");
                    }
                    if (navItems(&p, o).empty()) issue(S::Warning, "views", "aucune vue : la barre restera vide");
                    const auto labels = splitSemicolons(o.text("labels"));
                    if (!names.empty() && labels.size() > names.size())
                        issue(S::Info, "labels", "plus de libell\xC3\xA9s que de vues : les derniers ne servent pas");
                    break;
                }
                case Kind::Breadcrumb:
                    if (o.text("trail").rfind("hi", 0) == 0 && std::none_of(p.views.begin(), p.views.end(), [](const View& x) { return x.upView != kNoId; }))
                        issue(S::Info, "trail", "aucune vue n'a de vue parente : le fil ne montrera que la vue courante (et l'accueil)");
                    break;
                case Kind::TabContainer: {
                    const auto pages = static_cast<int>(tabLabels(o).size());
                    const auto* pg = o.find("page");
                    double n = 1;
                    if (pg && pg->expr.empty() && (!parseNumber(pg->value, n) || n < 1 || n > pages))
                        issue(S::Warning, "page", "onglet montr\xC3\xA9 hors de 1 \xC3\xA0 " + std::to_string(pages));
                    const std::string var = trimmedCopy(o.text("variable"));
                    if (!var.empty() && !known(p, plc, rootOf(var), &v)) issue(S::Error, "variable", "variable inexistante : " + var);
                    break;
                }
                case Kind::CollapsiblePanel: {
                    const std::string var = trimmedCopy(o.text("variable"));
                    if (!var.empty() && !known(p, plc, rootOf(var), &v)) issue(S::Error, "variable", "variable inexistante : " + var);
                    if (o.number("headerHeight", 34) >= o.number("h", 100)) issue(S::Warning, "headerHeight", "le bandeau prend toute la hauteur : aucun contenu visible");
                    break;
                }
                case Kind::ScrollPanel:
                    if (o.number("contentWidth", 0) < 0 || o.number("contentHeight", 0) < 0)
                        issue(S::Error, "contentHeight", "taille du contenu n\xC3\xA9gative");
                    if (v.childrenOf(o.id).empty()) issue(S::Info, "contentHeight", "aucun objet dans le panneau (double-clic pour y entrer, puis poser)");
                    break;
                case Kind::ZoneMap: {
                    const std::string img = trimmedCopy(markers::strip(o.text("image"), markers::Mode::Text));   // 1.11 (REP-1)
                    if (!img.empty() && !p.resourceByName(img)) issue(S::Error, "image", "image '" + img + "' introuvable dans les ressources");
                    const auto zones = parseMapZones(o.text("mapZones"));
                    if (zones.empty()) issue(S::Warning, "mapZones", "aucune zone (onglet Contenu)");
                    for (const auto& z : zones) {
                        if (z.points.size() < 3) issue(S::Error, "mapZones", "zone '" + z.name + "' : il faut au moins 3 points (x,y en %)");
                        for (const auto& [x, y] : z.points)
                            if (x < 0 || x > 100 || y < 0 || y > 100) {
                                issue(S::Warning, "mapZones", "zone '" + z.name + "' : un point hors de 0 \xC3\xA0 100 %");
                                break;
                            }
                        if (!z.view.empty() && !p.viewByName(z.view)) issue(S::Error, "mapZones", "zone '" + z.name + "' : vue '" + z.view + "' introuvable");
                        if (!groups.empty() && !groups.count(z.alarmGroup()))
                            issue(S::Info, "mapZones", "zone '" + z.name + "' : aucune alarme du groupe " + z.alarmGroup() + " (elle restera calme)");
                    }
                    const std::string var = trimmedCopy(o.text("variable"));
                    if (!var.empty() && !known(p, plc, rootOf(var), &v)) issue(S::Error, "variable", "variable inexistante : " + var);
                    break;
                }
                case Kind::LoginMenuButton: {
                    // L'onglet ouvert ; une action "Menu de connexion" au clic ferait double emploi.
                    const std::string tab = trimmedCopy(o.text("tab", "Connexion"));
                    const bool knownTab = std::any_of(std::begin(kLoginTabs), std::end(kLoginTabs), [&](const LoginTabSpec& t) { return t.label == tab; });
                    if (!tab.empty() && !knownTab)
                        issue(S::Warning, "tab", "onglet inconnu \xC2\xAB " + tab + " \xC2\xBB (Connexion, Mon compte, Comptes, Acc\xC3\xA8s, Journal) : le menu s'ouvrira sur Connexion");
                    const bool twice = std::any_of(o.actions.begin(), o.actions.end(), [](const Action& a) {
                        return a.trigger == Trigger::Click && a.operation == Operation::ShowLogin;
                    });
                    if (twice) issue(S::Info, "actions", "l'objet ouvre d\xC3\xA9j\xC3\xA0 le menu de connexion : l'action \xC2\xAB Menu de connexion \xC2\xBB au clic est de trop");
                    break;
                }
                default:
                    break;
            }
        }
    // Lot 12 : le menu de connexion sans securite montre tout a tout le monde.
    bool loginMenu = false;
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            if (o.kind == Kind::LoginMenuButton) loginMenu = true;
            for (const auto& a : o.actions) if (a.operation == Operation::ShowLogin) loginMenu = true;
        }
    if (loginMenu && !p.security.enabled)
        addItem(out, S::Info, "Utilisateurs", "S\xC3\xA9" "curit\xC3\xA9",
                "s\xC3\xA9" "curit\xC3\xA9 inactive : le menu de connexion montre tous ses onglets \xC3\xA0 tout le monde (comptes, acc\xC3\xA8s)", kNoId);
    if (loginMenu && p.security.enabled && p.security.users.empty())
        addItem(out, S::Warning, "Utilisateurs", "S\xC3\xA9" "curit\xC3\xA9", "le menu de connexion n'a aucun compte \xC3\xA0 proposer", kNoId);
}

// ============================================================== lot 14 ======
//  LA COMMUNICATION. La table des adresses (des lignes illisibles, en double,
//  qui se recouvrent) ; relie a un automate reel, les variables de l'automate
//  que l'IHM utilise et qui n'ont pas d'adresse Modbus (elles resteraient
//  "mauvaises" en marche) ; la lecture seule avec des commandes ; les objets de
//  la communication sur le simulateur.
void checkComm(const Project& p, const NameExists& plc, const comm::Plan* plan, const std::function<bool(std::string_view)>& scalar,
               const apivars::Model* api, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto& cm = p.comm;
    const std::string cat = "Communication";
    std::set<std::string> seen;
    for (const auto& a : cm.addresses) {
        std::string key = comm::keyOf(a.variable);
        if (key.empty()) {
            add(out, S::Error, cat, kNoId, kNoId, "adresses", "une ligne de la table des adresses sans variable");
            continue;
        }
        if (!seen.insert(key).second) add(out, S::Error, cat, kNoId, kNoId, a.variable, a.variable + " : deux lignes dans la table des adresses");
        if (plan && !plan->resolve(a.variable)) {
            const auto why = plan->whyNot(a.variable);
            add(out, S::Error, cat, kNoId, kNoId, a.variable, a.variable + " (" + a.address + ") : " + (why.empty() ? std::string("refus\xC3\xA9" "e") : why));
        }
    }
    if (plan) {
        // Deux variables qui partagent des mots (ou un bit) : l'une ecrase l'autre.
        struct Span {
            comm::Area area;
            std::size_t a, b;
            int bit;
            const comm::Point* p;
        };
        std::vector<Span> spans;
        for (const auto& pt : plan->points())
            spans.push_back({pt.area, pt.offset, pt.offset + pt.span(), pt.encoding == comm::Encoding::BitOfWord ? pt.bit : -1, &pt});
        std::sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) {
            return x.area != y.area ? x.area < y.area : x.a < y.a;
        });
        std::size_t reported = 0;
        for (std::size_t i = 0; i < spans.size() && reported < 20; ++i)
            for (std::size_t j = i + 1; j < spans.size() && spans[j].area == spans[i].area && spans[j].a < spans[i].b && reported < 20; ++j) {
                // Des bits d'un mot, et ce mot : voulu (un mot d'etat et ses bits).
                if (spans[i].bit >= 0 || spans[j].bit >= 0) continue;
                if (spans[i].p->origin != "table" && spans[j].p->origin != "table") continue;   // le programme : son affaire
                ++reported;
                add(out, S::Warning, cat, kNoId, kNoId, spans[j].p->name,
                    spans[i].p->name + " (" + spans[i].p->address + ") et " + spans[j].p->name + " (" + spans[j].p->address
                        + ") se recouvrent : l'une \xC3\xA9" "crase l'autre");
            }
    }
    bool writers = false;
    for (const auto& v : p.views)
        for (const auto& o : v.objects) writers = writers || kindWritesVariable(o.kind);
    if (cm.modbus()) {
        if (cm.host.empty()) add(out, S::Error, cat, kNoId, kNoId, "hote", "Modbus TCP sans adresse d'automate");
        if (plan) {
            std::size_t missing = 0;
            // 1.11.1 (API-M) : API.X se lit comme X sur la liaison ; une variable IHM
            // ou une vue nommee API garde son sens (decision D3 du journal d'API-M).
            const bool apiSpace = !p.variable(apivars::kRoot) && !pub::viewNamed(p, apivars::kRoot);
            for (const auto& written : comm::projectPlcPaths(p)) {
                const bool viaApi = apiSpace && apivars::isApiPath(written);
                const std::string path = viaApi ? apivars::stripApi(written) : written;
                if (path.empty() || plan->resolve(path)) continue;
                const auto why = plan->whyNot(path);
                const std::string root = path.substr(0, path.find_first_of(".["));
                if (viaApi) {
                    // Un chemin API. inconnu : la verification des expressions le dit (une erreur).
                    if (!api || !api->resolve(written).ok) continue;
                } else if (why.empty() && !(plc && plc(root))) {
                    continue;   // pas une variable de l'automate
                }
                if (scalar && !scalar(path)) continue;              // une structure, un bloc : ses membres comptent
                if (++missing > 200) break;
                if (viaApi) {
                    const auto r = api->resolve(written);
                    // 1.11.1 (decision 122) : la liaison ne la lit ni ne l'ecrit (pas de melange
                    // simule / reel) ; le remede : le Simulateur, ou une adresse.
                    // 1.11.2 (R1111-15) : la raison du plan sans son conseil d'adresse (le remede le dit).
                    const auto reason = apivars::withoutAddressAdvice(why);
                    add(out, S::Warning, cat, kNoId, kNoId, r.path,
                        r.path + " n'a pas d'adresse : lue en simulation seulement (la liaison Modbus ne la lit ni ne l'\xC3\xA9" "crit"
                            + (reason.empty() ? std::string{} : " : " + reason) + ")"
                            + " - pour la voir calcul\xC3\xA9" "e : passe la Communication en Simulateur ; pour la lire sur l'automate :"
                              " donne-lui une adresse dans le programme de l'automate (Control Expert, puis Fichier \xE2\x80\xBA Importer)"
                              " ou une ligne dans Configuration \xE2\x80\xBA Communication");
                    continue;
                }
                add(out, S::Warning, cat, kNoId, kNoId, path,
                    path + " : pas d'adresse Modbus" + (why.empty() ? std::string{} : " (" + why + ")")
                        + " - l'IHM ne pourra pas la lire (Configuration > Communication : Proposer)");
            }
        }
        if (!cm.writes && writers)
            add(out, S::Info, cat, kNoId, kNoId, "ecritures",
                "lecture seule : les commandes de l'IHM ne changeront rien dans l'automate (Configuration > Communication)");
        if (cm.demoServer && cm.port == cm.demoPort && (cm.host == "127.0.0.1" || cm.host == "localhost"))
            add(out, S::Info, cat, kNoId, kNoId, "hote",
                "l'IHM lit le serveur de d\xC3\xA9monstration (le simulateur en Modbus TCP) : un essai de la liaison, pas l'automate r\xC3\xA9" "el");
    } else {
        for (const auto& v : p.views)
            for (const auto& o : v.objects)
                if (o.kind == Kind::CommStatus || o.kind == Kind::PlcDiagnostic) {
                    add(out, S::Info, cat, v.id, o.id, {},
                        o.name + " : l'IHM lit le simulateur - l'objet le dira (Configuration > Communication : Modbus TCP pour un automate r\xC3\xA9" "el)");
                    break;
                }
    }
}

// Lot 14 : le poste d'exploitation. Tant qu'on n'y a pas touche, rien a dire.
void checkStation(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto& st = p.station;
    if (st == Station{}) return;
    const std::string cat = "Poste d'exploitation";
    std::set<int> seen;
    for (const auto& e : st.screens) {
        const std::string where = "ecran " + std::to_string(e.display);
        if (!seen.insert(e.display).second) add(out, S::Error, cat, kNoId, kNoId, where, "l'\xC3\xA9" "cran " + std::to_string(e.display) + " est d\xC3\xA9" "fini deux fois");
        if (e.display < 2) add(out, S::Error, cat, kNoId, kNoId, where, "un \xC3\xA9" "cran secondaire porte un num\xC3\xA9ro de 2 \xC3\xA0 9 (1 : le principal)");
        const View* v = nullptr;
        for (const auto& x : p.views)
            if (x.name == e.view) v = &x;
        if (!v) add(out, S::Error, cat, kNoId, kNoId, where, "l'\xC3\xA9" "cran " + std::to_string(e.display) + " montre la vue \xC2\xAB " + e.view + " \xC2\xBB, introuvable");
        else if (v->role != "vue")
            add(out, S::Warning, cat, kNoId, kNoId, where,
                "l'\xC3\xA9" "cran " + std::to_string(e.display) + " montre \xC2\xAB " + e.view + " \xC2\xBB, qui n'est pas une vue ordinaire (" + v->role + ")");
    }
    if (st.kiosk && st.exitHash.empty())
        add(out, S::Info, cat, kNoId, kNoId, "sortie",
            "kiosque sans mot de passe de sortie : n'importe qui peut quitter le poste (Configuration > Poste d'exploitation)");
    if (!st.kiosk && !st.fullScreen)
        add(out, S::Info, cat, kNoId, kNoId, "plein_ecran", "ni plein \xC3\xA9" "cran ni kiosque : le poste est une fen\xC3\xAAtre ordinaire");
}

// Lot 14 : les notifications, les rapports, l'acces web.
void checkNotify(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto& n = p.notify;
    const std::string cat = "Notifications";
    const bool anyReport = std::any_of(p.reports.begin(), p.reports.end(), [](const Report& r) { return !r.recipients.empty(); });
    if (!n.enabled && !anyReport) return;
    const auto lowerOf = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    if (n.enabled && n.recipients.empty())
        add(out, S::Warning, cat, kNoId, kNoId, {}, "notifications actives, sans destinataire : personne ne sera pr\xC3\xA9venu");
    const bool mail = !n.smtpHost.empty(), sms = !n.smsUrl.empty();
    if (n.enabled && !mail && !sms) add(out, S::Error, cat, kNoId, kNoId, {}, "notifications actives, sans relais SMTP ni passerelle SMS");
    if (sms && lowerOf(n.smsUrl).rfind("http://", 0) != 0)
        add(out, S::Error, cat, kNoId, kNoId, {}, "la passerelle SMS doit \xC3\xAAtre en http:// (" + n.smsUrl + ")");
    if (sms && n.smsUrl.find("{numero}") == std::string::npos && n.smsBody.find("{numero}") == std::string::npos)
        add(out, S::Warning, cat, kNoId, kNoId, {}, "la passerelle SMS ne cite pas {numero} : \xC3\xA0 qui partent les SMS ?");
    std::set<std::string> groups;
    for (const auto& a : p.alarms) groups.insert(lowerOf(a.group));
    std::set<std::string> names;
    for (const auto& r : n.recipients) {
        if (!names.insert(lowerOf(r.name)).second) add(out, S::Error, cat, kNoId, kNoId, r.name, "deux destinataires s'appellent " + r.name);
        if (!r.enabled) continue;
        if (r.email.empty() && r.phone.empty()) add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : ni courriel ni t\xC3\xA9l\xC3\xA9phone");
        if (!r.email.empty() && !mail) add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : un courriel, mais aucun relais SMTP");
        if (!r.phone.empty() && !sms) add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : un t\xC3\xA9l\xC3\xA9phone, mais aucune passerelle SMS");
        if (!r.email.empty() && r.email.find('@') == std::string::npos) add(out, S::Error, cat, kNoId, kNoId, r.name, r.name + " : courriel illisible (" + r.email + ")");
        std::string cur;
        for (const char c : r.groups + ";") {
            if (c == ';' || c == ',') {
                std::string g = cur;
                while (!g.empty() && g.front() == ' ') g.erase(g.begin());
                while (!g.empty() && g.back() == ' ') g.pop_back();
                if (!g.empty() && !groups.count(lowerOf(g)))
                    add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : aucune alarme du groupe \xC2\xAB " + g + " \xC2\xBB");
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (r.duty && r.dutyDays.find_first_of("1234567") == std::string::npos)
            add(out, S::Error, cat, kNoId, kNoId, r.name, r.name + " : d'astreinte, mais aucun jour");
    }
    if (n.testBox && n.enabled && (n.smtpHost == "127.0.0.1" || n.smtpHost == "localhost"))
        add(out, S::Info, cat, kNoId, kNoId, {}, "les notifications vont \xC3\xA0 la bo\xC3\xAEte d'essai de ce poste : personne n'est r\xC3\xA9" "ellement pr\xC3\xA9venu");
}

void checkReports(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const std::string cat = "Rapport";
    std::set<std::string> names;
    for (const auto& r : p.reports) {
        std::string lower = r.name;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!names.insert(lower).second) add(out, S::Error, cat, kNoId, kNoId, r.name, "deux rapports s'appellent " + r.name);
        if (!r.alarms && !r.production && !r.events && r.measures.empty() && p.history.archived.empty())
            add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : rien \xC3\xA0 mettre dedans (ni alarmes, ni mesures, ni production)");
        std::string cur;
        for (const char c : r.recipients + ";") {
            if (c == ';' || c == ',') {
                std::string d = cur;
                while (!d.empty() && d.front() == ' ') d.erase(d.begin());
                while (!d.empty() && d.back() == ' ') d.pop_back();
                if (!d.empty()) {
                    const auto* rec = p.recipientByName(d);
                    if (!rec) add(out, S::Error, cat, kNoId, kNoId, r.name, r.name + " : destinataire \xC2\xAB " + d + " \xC2\xBB inconnu (Configuration > Notifications)");
                    else if (rec->email.empty()) add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : " + d + " n'a pas de courriel");
                }
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!r.recipients.empty() && p.notify.smtpHost.empty())
            add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : des destinataires, mais aucun relais SMTP (Configuration > Notifications)");
        for (const auto& m : [&] {
                 std::vector<std::string> list;
                 std::string c2;
                 for (const char c : r.measures + ";") {
                     if (c == ';' || c == ',') {
                         while (!c2.empty() && c2.front() == ' ') c2.erase(c2.begin());
                         while (!c2.empty() && c2.back() == ' ') c2.pop_back();
                         if (!c2.empty()) list.push_back(c2);
                         c2.clear();
                     } else {
                         c2 += c;
                     }
                 }
                 return list;
             }())
            if (std::find(p.history.archived.begin(), p.history.archived.end(), m) == p.history.archived.end())
                add(out, S::Warning, cat, kNoId, kNoId, r.name, r.name + " : " + m + " n'est pas archiv\xC3\xA9" "e (Configuration > Historiques) : pas de mesures");
    }
}

void checkWeb(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto& w = p.web;
    if (!w.enabled) return;
    const std::string cat = "Acc\xC3\xA8s web";
    if (w.login && p.security.users.empty())
        add(out, S::Error, cat, kNoId, kNoId, {}, "acc\xC3\xA8s web avec connexion, mais aucun utilisateur (Configuration > Utilisateurs)");
    if (!w.login && w.control)
        add(out, S::Warning, cat, kNoId, kNoId, {}, "acc\xC3\xA8s web sans connexion, avec commande : n'importe quel navigateur du r\xC3\xA9seau pilote l'IHM");
    if (!w.login && w.allInterfaces)
        add(out, S::Info, cat, kNoId, kNoId, {}, "acc\xC3\xA8s web sans connexion, ouvert au r\xC3\xA9seau : tout le r\xC3\xA9seau voit l'IHM");
    if (w.port == p.comm.demoPort && p.comm.demoServer)
        add(out, S::Error, cat, kNoId, kNoId, {}, "le port " + std::to_string(w.port) + " est aussi celui du serveur de d\xC3\xA9monstration");
    if (p.notify.testBox && (w.port == p.notify.testPort || w.port == p.notify.testPort + 1))
        add(out, S::Error, cat, kNoId, kNoId, {}, "le port " + std::to_string(w.port) + " est aussi celui de la bo\xC3\xAEte d'essai");
}

// ============================================================== lot 15 ======
//  Les equipements du reseau et les variables IHM qui s'y lient.
void checkEquipments(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const std::string cat = "\xC3\x89quipement";
    const auto upperOf = [](std::string t) {
        for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return t;
    };
    std::set<std::string> names;
    std::map<std::string, std::string> endpoints;       // "hote:port:esclave" -> nom
    for (const auto& e : p.equipments) {
        if (e.name.empty()) {
            add(out, S::Error, cat, kNoId, kNoId, {}, "\xC3\xA9quipement sans nom (" + e.host + ")");
            continue;
        }
        if (!names.insert(upperOf(e.name)).second) add(out, S::Error, cat, kNoId, kNoId, e.name, "nom d'\xC3\xA9quipement en double : " + e.name);
        if (!e.enabled) {
            add(out, S::Info, cat, kNoId, kNoId, e.name, e.name + " est d\xC3\xA9sactiv\xC3\xA9 : ses variables gardent leur valeur initiale");
            continue;
        }
        if (!e.simulated) {
            std::uint32_t ip = 0;
            if (e.host.empty()) {
                add(out, S::Error, cat, kNoId, kNoId, e.name, e.name + " : sans adresse IP");
            } else if (!equip::parseIpv4(e.host, ip)) {
                const bool name = std::all_of(e.host.begin(), e.host.end(), [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.';
                });
                if (!name) add(out, S::Error, cat, kNoId, kNoId, e.name, e.name + " : adresse illisible \xC2\xAB " + e.host + " \xC2\xBB (192.168.1.30)");
                else add(out, S::Info, cat, kNoId, kNoId, e.name, e.name + " : adresse donn\xC3\xA9" "e par un nom (" + e.host + ") - il faut un DNS sur le poste");
            } else if (ip == 0 || (ip >> 28) >= 0xE) {
                add(out, S::Error, cat, kNoId, kNoId, e.name, e.name + " : " + e.host + " n'est pas l'adresse d'un \xC3\xA9quipement");
            } else if ((ip >> 24) == 127) {
                add(out, S::Info, cat, kNoId, kNoId, e.name, e.name + " : " + e.host + " est ce PC lui-m\xC3\xAAme");
            }
        }
        const auto bound = equip::boundVariables(p, e);
        if (e.modbus()) {
            if (e.port < 1 || e.port > 65535) add(out, S::Error, cat, kNoId, kNoId, e.name, e.name + " : port " + std::to_string(e.port) + " impossible");
            if (!e.simulated) {
                const std::string key = upperOf(e.host) + ":" + std::to_string(e.port) + ":" + std::to_string(e.unit);
                if (const auto it = endpoints.find(key); it != endpoints.end())
                    add(out, S::Warning, cat, kNoId, kNoId, e.name, e.name + " et " + it->second + " ont la m\xC3\xAAme adresse (" + e.host + ":" + std::to_string(e.port)
                                                                        + ", esclave " + std::to_string(e.unit) + ")");
                else endpoints.emplace(key, e.name);
            }
            if (bound.empty())
                add(out, S::Info, cat, kNoId, kNoId, e.name, e.name + " : aucune variable IHM n'y est li\xC3\xA9" "e, l'IHM n'en lit rien");
        }
        // Deux variables sur les memes registres. Lot 16 : case par case - une
        // structure liee se compare a ses voisines, et ses membres entre eux.
        // Lot 17 : un chevauchement n'est un probleme que s'il y a une ecriture
        // (qui ecrit vraiment : une action, un champ de saisie, un script, une
        // recette) - deux ecritures : erreur ; une ecriture et une lecture :
        // attention ; des lectures : rien. Et une case hors des zones declarees.
        if (e.modbus()) {
            const auto map = zones::buildMap(p, e);
            std::size_t shown = 0;
            for (const auto& c : map.conflicts) {
                if (c.severity < 2 || shown >= 20) continue;
                ++shown;
                const auto& b = map.vars[c.b];
                add(out, c.severity == 3 ? S::Error : S::Warning, "Variable IHM", kNoId, kNoId, b.root,
                    e.name + " : " + zones::conflictText(map, c) + (c.severity == 3 ? " - la derni\xC3\xA8re \xC3\xA9" "crite efface l'autre"
                                                                                    : " (voulu ? une consigne relue)"));
            }
            std::size_t outside = 0;
            for (const auto& v : map.vars) {
                if (!v.outOfZone || outside >= 20) continue;
                ++outside;
                add(out, S::Error, "Variable IHM", kNoId, kNoId, v.root,
                    v.name + " (" + v.address + ") est hors des zones de " + e.name + " (" + zones::summary(e.zones)
                        + ") : l'appareil r\xC3\xA9pondra \xC2\xAB adresse ill\xC3\xA9gale \xC2\xBB");
            }
        }
        // Lot 17 : le jumeau (1.9 : a l'ecran, l'esclave simule).
        if (e.hasTwin()) {
            const std::string tcat = "Esclave simul\xC3\xA9";
            std::uint32_t tip = 0;
            if (!e.twinHost.empty() && !equip::parseIpv4(e.twinHost, tip))
                add(out, S::Error, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : adresse simul\xC3\xA9" "e illisible \xC2\xAB " + e.twinHost + " \xC2\xBB");
            for (const auto& bh : e.behaviors) {
                std::string why;
                if (!twin::validBehavior(bh, &why)) {
                    add(out, S::Error, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : comportement " + std::string(behaviorKindLabel(bh.kind)) + " sur " + bh.address + " : " + why);
                    continue;
                }
                comm::Point pt;
                if (e.zones.declared && equip::placeEquipmentAddress(bh.address, equip::typeOfName(bh.type), pt)) {
                    const auto t1 = zones::tableOf(pt.area);
                    if (!zones::contains(e.zones.of(t1), pt.offset, pt.bits() ? 1u : std::max<std::uint32_t>(1, pt.size)))
                        add(out, S::Warning, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : comportement sur " + bh.address + ", hors des zones : personne ne pourra le lire");
                }
            }
            if (e.twinException != 0 && (e.twinException < 1 || e.twinException > 11))
                add(out, S::Warning, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : exception forc\xC3\xA9" "e " + std::to_string(e.twinException) + " (Modbus : 1 \xC3\xA0 11)");
            if (e.twinExpose) {
                if (e.twinExposePort == p.web.port && p.web.enabled)
                    add(out, S::Error, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : le port " + std::to_string(e.twinExposePort) + " est aussi celui de l'acc\xC3\xA8s web");
                if (p.comm.demoServer && e.twinExposePort == p.comm.demoPort)
                    add(out, S::Error, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : le port " + std::to_string(e.twinExposePort) + " est aussi celui du serveur de d\xC3\xA9monstration");
                for (const auto& o : p.equipments)
                    if (o.id != e.id && o.hasTwin() && o.twinExpose && o.twinExposePort == e.twinExposePort && o.id < e.id)
                        add(out, S::Error, tcat, kNoId, kNoId, e.name, e.twinLabel() + " et " + o.twinLabel() + " sont visibles sur le m\xC3\xAAme port " + std::to_string(e.twinExposePort));
                add(out, S::Info, tcat, kNoId, kNoId, e.name, e.twinLabel() + " est visible sur le vrai r\xC3\xA9seau (port " + std::to_string(e.twinExposePort)
                                                                  + ") : un autre ma\xC3\xAEtre peut l'interroger");
            }
            if (!e.twinResponds) add(out, S::Info, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : panne simul\xC3\xA9" "e (il ne r\xC3\xA9pond plus)");
            // Lot 18 : les cases forcees - on ne livre pas un jumeau force sans le savoir.
            std::string forcedList;
            std::size_t forcedCount = 0;
            for (const auto& f : e.forcings) {
                std::vector<twin::TwinBank::ForcedCell> cells;
                std::string why;
                if (!twin::forcedCells(f, e.wordOrder != "fort", cells, &why)) {
                    add(out, S::Error, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : for\xC3\xA7" "age sur \xC2\xAB " + f.address + " \xC2\xBB : " + why);
                    continue;
                }
                if (e.zones.declared && !cells.empty() && !zones::contains(e.zones.of(cells.front().table), cells.front().offset, static_cast<std::uint32_t>(cells.size())))
                    add(out, S::Warning, tcat, kNoId, kNoId, e.name, e.twinLabel() + " : for\xC3\xA7" "age sur " + f.address + ", hors des zones : personne ne pourra le lire");
                ++forcedCount;
                if (forcedCount <= 4) forcedList += (forcedList.empty() ? "" : ", ") + f.address + " = " + twin::forcingText(f);
            }
            if (forcedCount)
                add(out, S::Warning, tcat, kNoId, kNoId, e.name,
                    e.twinLabel() + " : " + std::to_string(forcedCount) + " case" + (forcedCount > 1 ? "s" : "") + " forc\xC3\xA9" "e" + (forcedCount > 1 ? "s" : "") + " (" + forcedList
                        + (forcedCount > 4 ? ", ..." : "") + ") : ses \xC3\xA9" "critures sont refus\xC3\xA9" "es (exception 04) \xE2\x80\x94 \xC2\xAB D\xC3\xA9" "forcer tout \xC2\xBB (Valeurs simul\xC3\xA9" "es) avant de livrer");
        } else if (!e.forcings.empty()) {
            add(out, S::Info, "Esclave simul\xC3\xA9", kNoId, kNoId, e.name, e.name + " : " + std::to_string(e.forcings.size()) + " for\xC3\xA7" "age(s) gard\xC3\xA9(s) sans esclave simul\xC3\xA9 (sans effet)");
        }
    }
    for (const auto& v : p.programs.variables) {
        if (!v.bound()) continue;
        const std::string vcat = "Variable IHM";
        const auto* e = p.equipmentByName(v.equipment);
        if (!e) {
            add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " est li\xC3\xA9" "e \xC3\xA0 un \xC3\xA9quipement inconnu : " + v.equipment);
            continue;
        }
        if (!e->modbus()) {
            add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " est li\xC3\xA9" "e \xC3\xA0 " + e->name + ", un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas de variables");
            continue;
        }
        if (v.address.empty()) {
            add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " : sans adresse sur " + e->name);
            continue;
        }
        // Lot 16 : une structure ou un tableau lie - chaque case doit avoir sa place.
        if (types::isComposite(v.type)) {
            if (v.scaled())
                add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " : une structure ou un tableau ne se met pas \xC3\xA0 l'\xC3\xA9" "chelle");
            std::vector<std::string> whys;
            std::string error;
            const auto leaves = types::leafVariables(p, v, nullptr, &whys, &error);
            if (!error.empty()) continue;             // dit par les variables IHM
            std::size_t refused = 0;
            for (std::size_t i = 0; i < leaves.size(); ++i) {
                if (!leaves[i].bound()) continue;           // 1.11.8 : un membre interne n'a pas de place
                std::string why = i < whys.size() ? whys[i] : std::string{};
                comm::Point pt;
                if (why.empty()) (void)equip::placeEquipmentAddress(leaves[i].address, equip::registerType(leaves[i]), pt, &why);
                if (why.empty()) continue;
                if (++refused <= 5) add(out, S::Error, vcat, kNoId, kNoId, v.name, leaves[i].name + " (" + e->name + ") : " + why);
            }
            if (refused > 5)
                add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " : " + std::to_string(refused - 5) + " autre(s) case(s) sans place sur " + e->name);
            // Le chemin relatif d'une case (Four1.Vannes[2].Position -> Vannes[2].Position ; V[0].NOM -> [0].NOM).
            const auto relOf = [&](const Variable& l) {
                const std::string rest = l.name.substr(std::min(l.name.size(), v.name.size()));
                return !rest.empty() && rest.front() == '.' ? rest.substr(1) : rest;
            };
            for (const auto& pl : v.places) {
                bool found = false;
                for (const auto& l : leaves)                 // 1.11.8 : un membre compose aussi (son depart)
                    if (types::memberCovers(pl.path, relOf(l))) found = true;
                if (!found)
                    add(out, S::Warning, vcat, kNoId, kNoId, v.name, v.name + " : l'adresse corrig\xC3\xA9" "e de \xC2\xAB " + pl.path
                                                                           + " \xC2\xBB ne d\xC3\xA9signe aucun membre");
            }
            // 1.11.8 : un membre interne qui ne designe rien (le type a change, un membre renomme).
            for (const auto& m : v.internal) {
                bool found = false;
                for (const auto& l : leaves)
                    if (types::memberCovers(m, relOf(l))) found = true;
                if (!found)
                    add(out, S::Warning, vcat, kNoId, kNoId, v.name, v.name + " : le membre interne \xC2\xAB " + m
                                                                           + " \xC2\xBB ne d\xC3\xA9signe aucun membre");
            }
            continue;
        }
        comm::Point pt;
        std::string why;
        if (!equip::placeEquipmentAddress(v.address, equip::registerType(v), pt, &why)) {
            add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " (" + e->name + ") : " + why);
            continue;
        }
        if (v.scaled() && (v.type == "BOOL" || v.type == "STRING" || v.type == "TIME"))
            add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " : une variable " + v.type + " ne se met pas \xC3\xA0 l'\xC3\xA9" "chelle");
        else if (v.scaled() && v.engMax == v.engMin)
            add(out, S::Error, vcat, kNoId, kNoId, v.name, v.name + " : \xC3\xA9" "chelle min et max \xC3\xA9gales");
        if (!pt.writable && !v.readOnly)
            add(out, S::Info, vcat, kNoId, kNoId, v.name,
                v.name + " : " + equip::modiconText(pt) + " se lit seulement - une \xC3\xA9" "criture sera refus\xC3\xA9" "e");
    }
}

// ============================================================== lot 13 ======
//  La signature electronique, la politique des mots de passe, le badge.
namespace {

bool userMay(const Project& p, const User& u, std::string_view permission) {
    const UserGroup* g = nullptr;
    for (const auto& x : p.security.groups) if (x.id == u.group) g = &x;
    if (!g) return false;
    for (const auto& roleName : g->roles)
        for (const auto& r : p.security.roles)
            if (r.name == roleName && std::find(r.permissions.begin(), r.permissions.end(), permission) != r.permissions.end()) return true;
    return false;
}
int levelOf(const Project& p, const User& u) {
    for (const auto& g : p.security.groups) if (g.id == u.group) return g.level;
    return 0;
}
std::string lowerCopy(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
// `var := ...` dans un code ST (sans casse, `var` en minuscules).
bool codeWrites(const std::string& lowerBody, const std::string& var) {
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ']'; };
    for (auto at = lowerBody.find(var); at != std::string::npos; at = lowerBody.find(var, at + 1)) {
        if (at > 0 && ident(lowerBody[at - 1])) continue;
        std::size_t i = at + var.size();
        if (i < lowerBody.size() && (std::isalnum(static_cast<unsigned char>(lowerBody[i])) || lowerBody[i] == '_' || lowerBody[i] == '.' || lowerBody[i] == '['))
            continue;
        while (i < lowerBody.size() && std::isspace(static_cast<unsigned char>(lowerBody[i]))) ++i;
        if (lowerBody.compare(i, 2, ":=") == 0) return true;
    }
    return false;
}

} // namespace

void checkLot13(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto& sec = p.security;
    int maxLevel = 0;
    for (const auto& g : sec.groups) maxLevel = std::max(maxLevel, g.level);
    std::size_t active = 0;
    for (const auto& u : sec.users) active += u.enabled;
    // Les scripts qui tournent en boucle : ce qu'ils ecrivent ne tient pas.
    std::vector<std::pair<std::string, std::string>> cyclic;
    for (const auto& s : p.programs.scripts)
        if (s.event == "Cyclique") cyclic.emplace_back(s.name, lowerCopy(s.body));
    for (const auto& v : p.views)
        for (const auto& s : v.scripts)
            if (s.event == "OnCycle") cyclic.emplace_back(v.name + "/" + s.name, lowerCopy(s.body));
    bool anySignature = false;
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            const std::string mode = signatureMode(o);
            if (mode == "aucune") continue;
            const auto issue = [&](S sev, const char* key, std::string msg) {
                add(out, sev, "S\xC3\xA9" "curit\xC3\xA9", v.id, o.id, key, std::move(msg));
            };
            if (!kindSignable(o.kind)) {
                issue(S::Info, "signature", "signature r\xC3\xA9gl\xC3\xA9" "e sur un objet qui ne commande rien : elle ne joue pas");
                continue;
            }
            anySignature = true;
            if (o.kind == Kind::IlluminatedButton && o.text("operation", "basculer") == "impulsion")
                issue(S::Warning, "signature",
                      "bouton \xC3\xA0 impulsion : la commande part \xC3\xA0 l'appui, la signature ne s'y applique pas (elle est ignor\xC3\xA9" "e)");
            if (active == 0) {
                issue(S::Error, "signature", "aucun compte actif : personne ne peut signer, la commande ne partira jamais");
            } else if (mode == "double") {
                const int level = static_cast<int>(std::clamp(o.number("signatureLevel", 3), 0.0, 99.0));
                std::size_t visas = 0;
                for (const auto& u : sec.users) visas += u.enabled && levelOf(p, u) >= level;
                if (level > maxLevel)
                    issue(S::Error, "signatureLevel", "visa du niveau " + std::to_string(level) + " : aucun groupe ne l'atteint (maximum "
                                                          + std::to_string(maxLevel) + ") - la double signature est impossible");
                else if (visas == 0)
                    issue(S::Error, "signatureLevel", "aucun compte actif du niveau " + std::to_string(level) + " ou plus : personne ne peut viser");
                else if (active < 2)
                    issue(S::Error, "signature", "un seul compte actif : la double signature demande deux personnes");
            }
            const std::string var = trimmedCopy(o.text("variable"));
            if (!var.empty()) {
                const std::string lv = lowerCopy(var);
                for (const auto& [name, body] : cyclic)
                    if (codeWrites(body, lv)) {
                        issue(S::Warning, "variable", "la variable " + var + " est aussi \xC3\xA9" "crite par le script cyclique " + name
                                                          + " : la valeur sign\xC3\xA9" "e ne tiendra pas");
                        break;
                    }
            }
        }
    if (anySignature && !p.history.audit)
        addItem(out, S::Info, "Historiques", "Journal d'audit",
                "des commandes demandent une signature, mais le journal d'audit est \xC3\xA9teint : la signature ne laisse de trace que dans le journal syst\xC3\xA8me",
                kNoId);
    if (!sec.enabled) return;
    const std::string where = "S\xC3\xA9" "curit\xC3\xA9";
    if (sec.lockAttempts > 0 && sec.lockMinutes == 0) {
        const bool someAdmin = std::any_of(sec.users.begin(), sec.users.end(), [&](const User& u) { return u.enabled && userMay(p, u, "Administrer"); });
        if (!someAdmin)
            addItem(out, S::Error, "Utilisateurs", where,
                    "verrouillage sans dur\xC3\xA9" "e, et aucun compte actif ne peut administrer : un compte verrouill\xC3\xA9 le resterait", kNoId);
    }
    if (sec.lockAttempts == 1)
        addItem(out, S::Info, "Utilisateurs", where, "verrouillage au premier \xC3\xA9" "chec : une faute de frappe suffit", kNoId);
    const std::size_t badges = static_cast<std::size_t>(std::count_if(sec.users.begin(), sec.users.end(), [](const User& u) { return !u.badge.empty(); }));
    if (sec.badgeLogin && badges == 0)
        addItem(out, S::Warning, "Utilisateurs", where, "connexion par badge coch\xC3\xA9" "e, mais aucun compte n'a de badge", kNoId);
    if (!sec.badgeLogin && badges > 0)
        addItem(out, S::Info, "Utilisateurs", where,
                std::to_string(badges) + " compte(s) ont un badge, mais la connexion par badge n'est pas coch\xC3\xA9" "e : il ne sert pas", kNoId);
    if (sec.autoLogoutMin > 0 && sec.logoutWarnS >= sec.autoLogoutMin * 60)
        addItem(out, S::Info, "Utilisateurs", where,
                "l'avertissement (" + std::to_string(sec.logoutWarnS) + " s) dure autant que la d\xC3\xA9" "connexion automatique ("
                    + std::to_string(sec.autoLogoutMin) + " min) : il s'affiche d\xC3\xA8s la derni\xC3\xA8re action",
                kNoId);
    if (sec.pwMaxAgeDays > 0) {
        std::size_t undated = 0;
        for (const auto& u : sec.users) undated += u.enabled && u.protection == "classique" && !u.passwordHash.empty() && u.passwordSet.empty();
        if (undated > 0)
            addItem(out, S::Info, "Utilisateurs", where,
                    std::to_string(undated) + " compte(s) sans date de mot de passe : ils ne p\xC3\xA9riment qu'apr\xC3\xA8s un premier changement", kNoId);
    }
}

// Lot 13 : les langues - les codes, la langue de demarrage, ce qui est traduit,
// les traductions qui perdent un trou, les selecteurs, les actions et les scripts
// qui visent une langue que le projet n'a pas.
void checkLanguages(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    const auto& l = p.languages;
    const auto issue = [&](S sev, std::string prop, std::string msg) {
        add(out, sev, "Langue", kNoId, kNoId, std::move(prop), std::move(msg));
    };
    const auto nameOf = [&](const Language& lang) { return lang.name.empty() ? languageName(lang.code) : lang.name; };
    if (l.list.empty()) issue(S::Error, {}, "aucune langue : il faut au moins celle dans laquelle le projet est \xC3\xA9" "crit");
    std::set<std::string> seen;
    for (const auto& lang : l.list) {
        if (!validLanguageCode(lang.code))
            issue(S::Error, lang.code, "code de langue \xC2\xAB " + lang.code + " \xC2\xBB invalide : deux ou trois lettres (fr, en, de), "
                                       "un sous-code facultatif (pt-BR)");
        if (!seen.insert(lowerCopy(lang.code)).second) issue(S::Error, lang.code, "la langue " + lang.code + " est deux fois dans la liste");
    }
    if (!l.startLanguage.empty() && !l.find(l.startLanguage))
        issue(S::Error, l.startLanguage, "langue de d\xC3\xA9marrage \xC2\xAB " + l.startLanguage + " \xC2\xBB : elle n'est pas dans la liste des langues");
    const auto holesText = [](std::vector<std::string> h) {
        std::string s;
        for (const auto& x : h) s += (s.empty() ? "{" : ", {") + x + "}";
        return s.empty() ? std::string("aucun") : s;
    };
    if (l.list.size() > 1) {
        const std::string source = nameOf(l.list.front());
        for (const auto& c : coverage(p)) {
            const std::size_t missing = c.total - c.translated;
            issue(missing ? S::Warning : S::Info, c.code,
                  c.name + " (" + c.code + ") : " + std::to_string(c.translated) + " texte(s) traduit(s) sur " + std::to_string(c.total)
                      + (missing ? " - les " + std::to_string(missing) + " autre(s) restent en " + source + " (Configuration > Langues)"
                                 : std::string(" : tout est traduit")));
        }
        for (const auto& t : translatableTexts(p)) {
            auto a = templateHoles(t.source);
            std::sort(a.begin(), a.end());
            for (std::size_t i = 1; i < l.list.size(); ++i) {
                const std::string tr = translationOf(l, l.list[i].code, t.source);
                if (tr.empty()) continue;
                auto b = templateHoles(tr);
                std::sort(b.begin(), b.end());
                if (a != b)
                    issue(S::Warning, l.list[i].code,
                          "\xC2\xAB " + t.source + " \xC2\xBB en " + l.list[i].code + " : la traduction n'a pas les m\xC3\xAAmes trous ("
                              + holesText(a) + " \xE2\x86\x92 " + holesText(b) + ") - elle ne montrera pas la m\xC3\xAAme chose");
            }
        }
    }
    // Une langue visee par son code : "en", 'en'.
    const auto known = [&](std::string code) {
        if (code.size() >= 2 && (code.front() == '\'' || code.front() == '"') && code.back() == code.front()) code = code.substr(1, code.size() - 2);
        const std::string low = lowerCopy(code);
        if (low.empty() || low == "suivante" || low == "next" || l.find(code)) return true;
        for (const auto& lang : l.list) if (lowerCopy(lang.name) == low) return true;
        return !validLanguageCode(code);     // une expression : jugee en marche
    };
    const auto actions = [&](const View& v, const Object* o, const std::vector<Action>& list) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& a = list[i];
            if (a.operation != Operation::SetLanguage) continue;
            std::string target = a.target;
            while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back()))) target.pop_back();
            while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front()))) target.erase(target.begin());
            if (!known(target))
                add(out, S::Error, "Action", v.id, o ? o->id : kNoId, actionLabel(i, a),
                    "Changer de langue : \xC2\xAB " + target + " \xC2\xBB n'est pas une langue du projet (Configuration > Langues)");
        }
    };
    for (const auto& v : p.views) {
        actions(v, nullptr, v.actions);
        for (const auto& o : v.objects) {
            actions(v, &o, o.actions);
            if (o.kind != Kind::LanguageSelector) continue;
            if (l.list.size() < 2)
                add(out, S::Warning, "Objet", v.id, o.id, "languages",
                    "s\xC3\xA9lecteur de langue, mais le projet n'a qu'une langue (Configuration > Langues)");
            for (const auto& code : listItems(o.text("languages")))
                if (!l.find(code))
                    add(out, S::Warning, "Objet", v.id, o.id, "languages",
                        "langue \xC2\xAB " + code + " \xC2\xBB : elle n'est pas dans le projet, son bouton n'appara\xC3\xAEt pas");
        }
    }
    // IHM_LANGUE('xx') dans un script.
    const auto scan = [&](const Script& sc, Id view) {
        const std::string low = lowerCopy(sc.body);
        for (auto at = low.find("ihm_langue"); at != std::string::npos; at = low.find("ihm_langue", at + 1)) {
            std::size_t i = at + 10;
            while (i < low.size() && std::isspace(static_cast<unsigned char>(low[i]))) ++i;
            if (i >= low.size() || low[i] != '(') continue;
            ++i;
            while (i < low.size() && std::isspace(static_cast<unsigned char>(low[i]))) ++i;
            if (i >= low.size() || low[i] != '\'') continue;
            const auto close = sc.body.find('\'', i + 1);
            if (close == std::string::npos) continue;
            const std::string code = sc.body.substr(i + 1, close - i - 1);
            if (!known(code))
                add(out, S::Warning, "Script", view, kNoId, sc.name,
                    "IHM_LANGUE('" + code + "') : pas une langue du projet (Configuration > Langues) - l'appel rendra FALSE", sc.id);
        }
    };
    for (const auto& sc : p.programs.scripts) scan(sc, kNoId);
    for (const auto& v : p.views)
        for (const auto& sc : v.scripts) scan(sc, v.id);
}

// Lot 13 : l'affichage - les unites et formats des variables, les reglages du
// lancement, l'action Changer de theme et IHM_THEME.
void checkDisplay(const Project& p, const NameExists& plc, std::vector<Issue>& out) {
    using S = Issue::Severity;
    std::set<std::string> seen;
    for (const auto& d : p.displays) {
        const auto issue = [&](S sev, std::string msg) { add(out, sev, "Unit\xC3\xA9", kNoId, kNoId, d.path, std::move(msg)); };
        if (!validDisplayPath(d.path)) {
            issue(S::Error, "\xC2\xAB " + d.path + " \xC2\xBB n'est pas un chemin de variable (Pression, Armoires[].ana.PT1.mes)");
            continue;
        }
        if (!seen.insert(lowerCopy(d.path)).second) issue(S::Error, d.path + " a deux lignes (Configuration > Unit\xC3\xA9s et formats)");
        if (!d.format.empty() && !looksLikeFormat(d.format))
            issue(S::Error, d.path + " : format \xC2\xAB " + d.format + " \xC2\xBB illisible (0, 0.0, 0.00, 000, 0.0%)");
        std::size_t end = 0;
        while (end < d.path.size() && d.path[end] != '.' && d.path[end] != '[') ++end;
        const std::string root = d.path.substr(0, end);
        // Un parametre d'une vue (Armoire dans une popup) : connu la ou il est declare.
        const bool param = std::any_of(p.views.begin(), p.views.end(), [&](const View& v) { return v.param(root) != nullptr; });
        if (!param && !known(p, plc, root)) issue(S::Warning, d.path + " : variable inconnue (ni IHM, ni de l'automate)");
        else if (displayUses(p, d).empty()) issue(S::Info, d.path + " : aucun objet ne la montre encore");
        if (d.unit.empty() && d.format.empty()) issue(S::Info, d.path + " : ni unit\xC3\xA9 ni format - la ligne ne change rien");
    }
    const auto& c = p.config;
    if (c.textScale < 50 || c.textScale > 300)
        add(out, S::Warning, "Configuration", kNoId, kNoId, "texte", "taille du texte " + std::to_string(c.textScale) + " % : de 50 \xC3\xA0 300");
    if (c.colorMode != "normal" && c.colorMode != "daltonien")
        add(out, S::Warning, "Configuration", kNoId, kNoId, "couleurs", "couleurs \xC2\xAB " + c.colorMode + " \xC2\xBB : normal ou daltonien");
    if (c.theme != "nuit" && c.theme != "jour")
        add(out, S::Warning, "Configuration", kNoId, kNoId, "theme", "th\xC3\xA8me \xC2\xAB " + c.theme + " \xC2\xBB : nuit ou jour");
    const auto themeOk = [](std::string t) {
        t = lowerCopy(trimmedCopy(t));
        if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') t = t.substr(1, t.size() - 2);
        return t.empty() || t == "jour" || t == "nuit" || t == "bascule" || t == "clair" || t == "sombre";
    };
    const auto actions = [&](const View& v, const Object* o, const std::vector<Action>& list) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].operation == Operation::SetTheme && !themeOk(list[i].target))
                add(out, S::Error, "Action", v.id, o ? o->id : kNoId, actionLabel(i, list[i]),
                    "Changer de th\xC3\xA8me : \xC2\xAB " + list[i].target + " \xC2\xBB - jour, nuit, ou vide (l'autre)");
    };
    for (const auto& v : p.views) {
        actions(v, nullptr, v.actions);
        for (const auto& o : v.objects) actions(v, &o, o.actions);
    }
    const auto scan = [&](const Script& sc, Id view) {
        const std::string low = lowerCopy(sc.body);
        for (auto at = low.find("ihm_theme"); at != std::string::npos; at = low.find("ihm_theme", at + 1)) {
            std::size_t i = at + 9;
            while (i < low.size() && std::isspace(static_cast<unsigned char>(low[i]))) ++i;
            if (i >= low.size() || low[i] != '(') continue;
            ++i;
            while (i < low.size() && std::isspace(static_cast<unsigned char>(low[i]))) ++i;
            if (i >= low.size() || low[i] != '\'') continue;
            const auto close = sc.body.find('\'', i + 1);
            if (close == std::string::npos) continue;
            const std::string t = sc.body.substr(i + 1, close - i - 1);
            if (!themeOk(t))
                add(out, S::Warning, "Script", view, kNoId, sc.name, "IHM_THEME('" + t + "') : jour, nuit ou bascule - l'appel rendra FALSE", sc.id);
        }
    };
    for (const auto& sc : p.programs.scripts) scan(sc, kNoId);
    for (const auto& v : p.views)
        for (const auto& sc : v.scripts) scan(sc, v.id);
}

// ---- Lot API 8 : corrections des captures ----
// Les proprietes (lot 11) dont le TEXTE est une expression - le moteur les lit
// par sourceOf (Compteur_Bons, Marche) : le compteur de production (pieces
// bonnes, rebuts, en marche) et la courbe XY (abscisse, remise a zero).
// Compiler et les expressions impossibles ne lisaient que "state", "lamp" et
// "condition" : un nom inconnu dans "En marche" d'un compteur TRS passait.
namespace {
std::vector<const char*> valueExpressionKeys(Kind k) {
    switch (k) {
        case Kind::ProductionCounter: return {"good", "bad", "running"};
        case Kind::XYChart:           return {"xVariable", "reset"};
        default:                      return {};
    }
}
} // namespace
// ---- fin Lot API 8 : corrections des captures ----

// ---- Lot API 8 : les expressions impossibles ----
// Chaque expression dynamique du projet, relue par exprcheck : ce qui se lit
// mais ne peut pas marcher devient une ERREUR a son endroit exact (vue, objet,
// propriete ; alarme, recette, utilisateur, script). Compiler les ajoute aux
// siennes (sans la syntaxe, qu'il dit deja) ; Generer aussi (avec la syntaxe :
// une IHM generee ne part pas avec une expression illisible). Un nom inconnu
// deja dit par Generer ("variable inexistante") n'est pas dit deux fois : le
// message existant recoit le nom le plus proche.
namespace {
struct ExprWalker {
    const Project&      p;
    const NameExists&   plc;
    std::vector<Issue>& out;
    bool                syntax;    // Generer : la syntaxe aussi
    exprcheck::PlcPaths paths{};   // les chemins de l'automate (vides : pas verifies)
    const CompileFocus* focus{nullptr};   // 1.11.13 : une partie seulement (nul : tout)
    [[nodiscard]] bool animOn(Id v) const { return !focus || focus->anim(v); }
    [[nodiscard]] bool actOn(Id v) const { return !focus || focus->act(v); }

    struct Where {
        const View* view{nullptr};
        Id          object{kNoId};
        std::string category;
        std::string property;
        Id          item{kNoId};
        Id          script{kNoId};
    };

    exprcheck::Context context(const View* v) const {
        exprcheck::Context c;
        c.project = &p;
        c.view = v;
        const Project* pp = &p;
        const NameExists* names = &plc;
        c.known = [pp, names, v](std::string_view root) { return known(*pp, *names, root, v); };
        c.plc = paths;
        return c;
    }

    void push(const Where& w, std::string msg, const std::string& unknownName = {}, const std::string& suggestion = {},
              const std::string& source = {}, Issue::Severity severity = Issue::Severity::Error) {
        Issue i;
        i.severity = severity;                          // 1.11.21 : un avertissement d'appel (exprcheck::Problem::warning)
        i.category = w.category;
        i.view = w.view ? w.view->id : kNoId;
        i.object = w.object;
        i.property = w.property;
        i.message = std::move(msg);
        i.item = w.item;
        i.script = w.script;
        for (auto& o : out) {
            if (o.severity != Issue::Severity::Error || o.view != i.view || o.object != i.object || o.item != i.item) continue;
            if (!unknownName.empty() && o.message.find("inexistante") != std::string::npos && o.message.find(unknownName) != std::string::npos) {
                if (!suggestion.empty() && o.message.find(suggestion) == std::string::npos) o.message += " : veux-tu dire " + suggestion + " ?";
                return;
            }
            if (o.property == i.property && (o.message == i.message || (!source.empty() && o.message.find(source) != std::string::npos))) return;
        }
        out.push_back(std::move(i));
    }

    // 1.11 (REP, decision 80) : la phrase de Dupliquer si un repere de `s` a pour racine
    // un nom inconnu (l'ancien $Vanne$), quoi que dise l'analyse.
    std::string unknownMarkerHint(const Where& w, const std::string& s) const {
        if (s.find('$') == std::string::npos) return {};
        const auto c = context(w.view);
        for (const auto& m : dup::markersIn(s)) {
            const auto roots = scanRoots(m.name);
            if (!roots.empty() && c.known && !c.known(roots.front())) return " \xE2\x80\x94 " + std::string(dup::kMarkerHint);
        }
        return {};
    }

    void expr(const Where& w, const std::string& source, exprcheck::Want want, const std::string& prefix = {}) {
        const std::string s = trimmedCopy(source);
        if (s.empty()) return;   // 1.11 (REP) : un repere se verifie comme le reste (ses $ sont transparents)
        const auto e = Expression::compile(s);
        if (!e.valid()) {
            // 1.11 (REP, decision 80) : une expression qui ne s'analyse pas (=$Vanne$.Ouv : "une
            // valeur est attendue") garde la phrase de son ancien repere ; si l'erreur est deja dite
            // (Compiler : compile), la phrase s'y ajoute, et "Remplacer..." s'y accroche.
            const std::string hint = unknownMarkerHint(w, s);
            if (!hint.empty())
                for (auto& o : out)
                    if (o.severity == Issue::Severity::Error && o.view == (w.view ? w.view->id : kNoId) && o.object == w.object
                        && o.item == w.item && o.message.find(s) != std::string::npos
                        && o.message.find(dup::kMarkerHint) == std::string::npos) {
                        o.message += hint;
                        return;
                    }
            if (syntax || !hint.empty()) push(w, prefix + s + " : " + e.error() + hint, {}, {}, s);
            return;
        }
        for (auto& pb : exprcheck::check(context(w.view), s, want))
            push(w, prefix + s + " : " + pb.message + dup::markerHint(s, pb.unknownName), pb.unknownName, pb.suggestion, {},
                 pb.warning ? Issue::Severity::Warning : Issue::Severity::Error);
    }
    void text(const Where& w, const std::string& value, const std::string& prefix = {}) {
        for (const auto& piece : exprcheck::templateExpressions(value)) expr(w, piece, exprcheck::Want::Any, prefix + "{" + piece + "} : ");
    }
    void target(const Where& w, const std::string& target, const std::string& prefix) {
        const std::string s = trimmedCopy(target);
        if (s.empty()) return;
        for (auto& pb : exprcheck::checkTarget(context(w.view), s))
            push(w, prefix + s + " : " + pb.message + dup::markerHint(s, pb.unknownName), pb.unknownName, pb.suggestion);
    }

    void actions(const View& v, const Object* o, const std::vector<Action>& list) {
        using W = exprcheck::Want;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& a = list[i];
            const Where w{&v, o ? o->id : kNoId, "Action", actionLabel(i, a)};
            expr(w, a.watch, W::Any, "expression surveill\xC3\xA9" "e ");
            expr(w, a.guard, W::Bool, "condition ");
            const bool writes = a.operation == Operation::Toggle || a.operation == Operation::Set || a.operation == Operation::Reset
                             || a.operation == Operation::Increment || a.operation == Operation::Decrement || a.operation == Operation::Assign;
            if (a.operation == Operation::Assign) expr(w, a.value, W::Any, "valeur ");
            if (a.operation == Operation::Increment || a.operation == Operation::Decrement) expr(w, a.value, W::Number, "pas ");
            if (writes) target(w, a.target, "cible ");
            if (a.operation == Operation::Log) text(w, a.value, "message ");
            if (operationTakesArguments(a.operation)) {
                const View* opened = p.viewByName(trimmedCopy(a.target));
                for (const auto& [name, value] : parseArguments(a.value)) {
                    expr(w, value, W::Any, "param\xC3\xA8tre " + name + " : ");
                    if (opened && !opened->param(name))           // 1.11.22 : un parametre retire du popup casse l'appel
                        push(w, "param\xC3\xA8tre " + name + " : " + opened->name + " n'a pas (ou plus) ce param\xC3\xA8tre - corrigez l'appel, "
                                "ou d\xC3\xA9" "clarez-le dans " + opened->name);
                }
            }
        }
    }

    void run() {
        using W = exprcheck::Want;
        for (const auto& v : p.views) {
            const bool anim = animOn(v.id), act = actOn(v.id);   // 1.11.13 : le filtre
            if (!anim && !act) continue;
            for (const auto& o : v.objects) {
                if (!anim) { if (!o.actions.empty()) actions(v, &o, o.actions); continue; }
                for (const auto& prop : o.props) {
                    const Where w{&v, o.id, "Expression", prop.key};
                    if (!prop.expr.empty()) expr(w, prop.expr, exprcheck::wantOf(prop.key));
                    if (isTemplateKey(prop.key)) text(Where{&v, o.id, "Texte", prop.key}, prop.value);
                    if (prop.key == "cells" && prop.expr.empty()) {
                        const auto rows = parseCells(prop.value);
                        for (std::size_t r = 0; r < rows.size(); ++r)
                            for (std::size_t c = 0; c < rows[r].size(); ++c) {
                                const auto& cell = rows[r][c];
                                const std::string where = "case [" + std::to_string(r + 1) + "," + std::to_string(c + 1) + "] ";
                                if (cellIsExpression(cell)) expr(w, cell.substr(1), W::Any, where);
                                else if (cellIsTemplate(cell)) text(w, cell, where);
                            }
                    }
                    if (prop.key == "states" && prop.expr.empty()) {
                        const auto states = parseImageStates(prop.value);
                        for (std::size_t k = 0; k < states.size(); ++k)
                            expr(w, states[k].condition, W::Bool, "\xC3\xA9tat " + std::to_string(k + 1) + " : ");
                    }
                }
                // Lot 9 : les cases qui sont une expression (retour d'etat, voyant, condition du compteur).
                if (kindWritesVariable(o.kind) || o.kind == Kind::HourMeter)
                    for (const char* key : {"state", "lamp", "condition"})
                        if (const auto* prop = o.find(key); prop && prop->expr.empty())
                            expr(Where{&v, o.id, "Expression", key}, prop->value, W::Any);
                // ---- Lot API 8 : corrections des captures ---- le texte-expression du lot 11.
                for (const char* key : valueExpressionKeys(o.kind))
                    if (const auto* prop = o.find(key); prop && prop->expr.empty())
                        expr(Where{&v, o.id, "Expression", key}, prop->value, W::Any);
                // Ce que l'objet ecrit (un champ de saisie, une commande) : une variable qu'on peut ecrire.
                if (kindWritesVariable(o.kind)) target(Where{&v, o.id, "Expression", "variable"}, o.text("variable"), "variable \xC3\xA9" "crite ");
                if (o.kind == Kind::InputField)
                    for (const char* key : {"min", "max"}) {
                        const std::string b = trimmedCopy(o.text(key));
                        double x = 0;
                        if (!b.empty() && !parseNumber(b, x)) expr(Where{&v, o.id, "Expression", key}, b, W::Number);
                    }
                if (!o.actions.empty() && act) actions(v, &o, o.actions);
            }
            if (act) actions(v, nullptr, v.actions);
            if (!anim) continue;
            if (v.role == "popup" && !v.popup.title.empty()) text(Where{&v, kNoId, "Popup", "popup_libelle"}, v.popup.title, "titre ");
            for (const auto& prm : v.params) {
                const std::string def = trimmedCopy(prm.defaultValue);
                if (!def.empty()) expr(Where{&v, kNoId, "Param\xC3\xA8tre", "parametres"}, def, W::Any, prm.name + " := ");
            }
        }
        for (const auto& sc : p.programs.scripts)
            if (sc.event == "Changement" && (!focus || focus->scripts.count(sc.id)))
                expr(Where{nullptr, kNoId, "Script", sc.name, kNoId, sc.id}, sc.watch, W::Any, "expression surveill\xC3\xA9" "e ");
        for (const auto& a : p.alarms) {
            if (focus && !focus->alarms.count(a.id)) continue;
            const Where w{nullptr, kNoId, "Alarme", a.name, a.id};
            expr(w, a.condition, W::Bool, "condition ");
            text(w, a.message, "message ");
        }
        for (const auto& r : p.recipes) {
            if (focus && !focus->recipes.count(r.id)) continue;
            for (const auto& rec : r.records)
                for (std::size_t i = 0; i < rec.values.size(); ++i)
                    expr(Where{nullptr, kNoId, "Recette", r.name, r.id}, rec.values[i], W::Any,
                         rec.name + " / " + (i < r.fields.size() ? r.fields[i].name : std::to_string(i + 1)) + " : ");
        }
        if (!focus || focus->users)
            for (const auto& u : p.security.users)
                if (u.protection == "expression") expr(Where{nullptr, kNoId, "Utilisateur", u.login, u.id}, u.expression, W::Bool, "autorisation ");
        // Les variables archivees (Configuration > Historiques) et les plumes des
        // courbes (sauf une courbe historique lue dans un fichier externe).
        if (!focus || focus->rest)
            for (const auto& archived : p.history.archived)
                expr(Where{nullptr, kNoId, "Historique", archived}, archived, W::Any, "variable archiv\xC3\xA9" "e ");
        for (const auto& v : p.views)
            for (const auto& o : v.objects) {
                if (o.kind != Kind::Trend || !animOn(v.id)) continue;
                if (o.text("mode").rfind("historique", 0) == 0 && !o.text("source").empty()) continue;
                for (const auto& pen : splitPens(o.text("variables"))) expr(Where{&v, o.id, "Courbe", "variables"}, pen, W::Any, "plume ");
            }
        finitions();     // ---- Lot API 8 : finitions (les formats d'unites, les symboles hors des vues) ----
    }

    // ---- Lot API 8 : finitions (les formats d'unites, les symboles hors des vues) ----
    //  Configuration > Unites et formats : le format de chaque ligne et son
    //  chemin (membres, indices constants ; [] : tous les elements), a la ligne
    //  en cause. Les symboles : chaque argument d'une instance ; la definition
    //  (la vue du symbole) relue AVEC les valeurs par defaut de ses parametres
    //  (Armoire := Armoires[0] : Armoire.ana.PT9 devient un chemin de
    //  l'automate, verifie) ; chaque instance relue avec SES arguments - ce que
    //  ses arguments seuls rendent impossible est dit sur l'instance
    //  (propriete params), ce qui est faux dans le symbole l'est une fois, sur
    //  le symbole. Les ecrans modeles, en-tetes et pieds sont des vues : deja lus.
    struct Site {
        std::string     category;
        std::string     key;
        std::string     text;
        exprcheck::Want want{exprcheck::Want::Any};
        bool            templ{false};    // un texte a trous : chaque trou
        bool            write{false};    // la cible d'une ecriture
        std::string     prefix;
    };

    static std::vector<Site> sitesOf(const Object& o) {
        using W = exprcheck::Want;
        std::vector<Site> s;
        for (const auto& prop : o.props) {
            if (!prop.expr.empty()) s.push_back(Site{"Expression", prop.key, prop.expr, exprcheck::wantOf(prop.key), false, false, {}});
            if (isTemplateKey(prop.key)) s.push_back(Site{"Texte", prop.key, prop.value, W::Any, true, false, {}});
        }
        if (kindWritesVariable(o.kind) || o.kind == Kind::HourMeter)
            for (const char* key : {"state", "lamp", "condition"})
                if (const auto* prop = o.find(key); prop && prop->expr.empty())
                    s.push_back(Site{"Expression", key, prop->value, W::Any, false, false, {}});
        // ---- Lot API 8 : corrections des captures ---- le texte-expression du lot 11.
        for (const char* key : valueExpressionKeys(o.kind))
            if (const auto* prop = o.find(key); prop && prop->expr.empty())
                s.push_back(Site{"Expression", key, prop->value, W::Any, false, false, {}});
        if (kindWritesVariable(o.kind)) s.push_back(Site{"Expression", "variable", o.text("variable"), W::Any, false, true, "variable \xC3\xA9" "crite "});
        if (o.kind == Kind::Trend)
            for (const auto& pen : splitPens(o.text("variables"))) s.push_back(Site{"Courbe", "variables", pen, W::Any, false, false, "plume "});
        for (std::size_t i = 0; i < o.actions.size(); ++i) {
            const auto& a = o.actions[i];
            const std::string label = actionLabel(i, a);
            s.push_back(Site{"Action", label, a.guard, W::Bool, false, false, "condition "});
            if (a.operation == Operation::Assign) s.push_back(Site{"Action", label, a.value, W::Any, false, false, "valeur "});
            const bool writes = a.operation == Operation::Toggle || a.operation == Operation::Set || a.operation == Operation::Reset
                             || a.operation == Operation::Increment || a.operation == Operation::Decrement || a.operation == Operation::Assign;
            if (writes) s.push_back(Site{"Action", label, a.target, W::Any, false, true, "cible "});
        }
        return s;
    }

    // Les problemes d'un site (sans les dire) : (le texte lu, le probleme). Les
    // parametres remplaces par `args` (nul : tels quels). La syntaxe est dite
    // ailleurs (le site lui-meme).
    std::vector<std::pair<std::string, exprcheck::Problem>> problemsOf(const View& in, const Site& s, const SymbolArguments* args) const {
        std::vector<std::pair<std::string, exprcheck::Problem>> found;
        const auto one = [&](const std::string& source, exprcheck::Want want, const std::string& shown) {
            const std::string t = trimmedCopy(source);
            if (t.empty() || !Expression::compile(t).valid()) return;
            for (auto& pb : s.write ? exprcheck::checkTarget(context(&in), t) : exprcheck::check(context(&in), t, want))
                found.emplace_back(shown.empty() ? t : shown, std::move(pb));
        };
        if (s.templ) {
            const std::string text = args ? substituteInTemplate(s.text, *args) : s.text;
            for (const auto& piece : exprcheck::templateExpressions(text)) one(piece, exprcheck::Want::Any, "{" + piece + "}");
        } else {
            one(args ? substituteParams(s.text, *args) : s.text, s.want, {});
        }
        return found;
    }

    static void replaceAll(std::string& text, const std::string& from, const std::string& to) {
        if (from.empty() || from == to) return;
        for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    }

    void finitions() {
        using W = exprcheck::Want;
        // Les formats d'unites.
        for (const auto& d : p.displays) {
            if (focus && !focus->rest) break;   // 1.11.13 : le filtre
            const Where w{nullptr, kNoId, "Unit\xC3\xA9", d.path};
            // Le meme message que Generer (checkDisplay) : dit une fois ; Compiler le dit aussi.
            if (!d.format.empty() && !looksLikeFormat(d.format))
                push(w, d.path + " : format \xC2\xAB " + d.format + " \xC2\xBB illisible (0, 0.0, 0.00, 000, 0.0%)");
            const std::string path = trimmedCopy(d.path);
            if (path.empty() || !validDisplayPath(path)) continue;        // Generer le dit (pas un chemin)
            std::size_t end = 0;
            while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
            if (!known(p, plc, path.substr(0, end))) continue;             // Generer le dit (variable inconnue)
            // [] : n'importe quel element - un indice calcule, les membres restent verifies.
            std::string sample = path;
            replaceAll(sample, "[]", "[SYS.Seconde]");
            for (auto& pb : exprcheck::check(context(nullptr), sample, W::Any)) {
                replaceAll(pb.message, "SYS.Seconde", "");
                push(w, "chemin " + path + " : " + pb.message, pb.unknownName, pb.suggestion, {},
                     pb.warning ? Issue::Severity::Warning : Issue::Severity::Error);
            }
        }
        // Les symboles.
        for (const auto& v : p.views)
            for (const auto& o : v.objects) {
                if (o.kind != Kind::SymbolInstance || !animOn(v.id)) continue;
                const View* sv = symbolOf(p, o);
                if (!sv) continue;                                           // checkSymbols le dit
                for (const auto& [name, value] : parseArguments(o.text("params"))) {
                    if (sv->param(name)) expr(Where{&v, o.id, "Symbole", "params"}, value, W::Any, "argument " + name + " := ");
                    else push(Where{&v, o.id, "Symbole", "params"},     // 1.11.22 : un parametre retire du symbole casse l'instance
                              "argument " + name + " : " + sv->name + " n'a pas (ou plus) ce param\xC3\xA8tre - retirez-le de l'instance "
                              "(inspecteur, Arguments), ou d\xC3\xA9" "clarez-le dans le symbole");
                }
            }
        for (const auto& sv : p.views) {
            if (!isSymbolView(sv) || !animOn(sv.id)) continue;
            SymbolArguments defaults;
            std::string shownDefaults;
            for (const auto& prm : sv.params) {
                const std::string def = trimmedCopy(prm.defaultValue);
                if (def.empty()) continue;
                defaults.emplace_back(prm.name, def);
                shownDefaults += (shownDefaults.empty() ? "" : ", ") + prm.name + " := " + def;
            }
            // Les instances posees dans une vue (pas dans un autre symbole : leurs
            // arguments y sont des parametres de celui-la).
            std::vector<std::pair<const View*, const Object*>> instances;
            std::vector<std::set<std::string>> argProblems;     // ce que ses arguments seuls disent deja
            for (const auto& [iv, inst] : instancesOf(p, sv.name)) {
                if (isSymbolView(*iv)) continue;
                instances.emplace_back(iv, inst);
                argProblems.emplace_back();
                for (const auto& [name, value] : parseArguments(inst->text("params")))
                    if (sv.param(name) && Expression::compile(trimmedCopy(value)).valid())
                        for (const auto& pb : exprcheck::check(context(iv), trimmedCopy(value), W::Any)) argProblems.back().insert(pb.message);
            }
            for (const auto& c : sv.objects) {
                if (c.kind == Kind::SymbolInstance) continue;               // un symbole dans le symbole : son instance le dit
                for (const auto& site : sitesOf(c)) {
                    std::set<std::string> plain, withDefaults;
                    for (const auto& [t, pb] : problemsOf(sv, site, nullptr)) plain.insert(pb.message);
                    if (!defaults.empty())
                        for (const auto& [t, pb] : problemsOf(sv, site, &defaults)) {
                            withDefaults.insert(pb.message);
                            if (plain.count(pb.message) || !pb.unknownName.empty()) continue;
                            push(Where{&sv, c.id, site.category, site.key}, site.prefix + t + " : " + pb.message + " (avec " + shownDefaults + ")",
                                 {}, {}, {}, pb.warning ? Issue::Severity::Warning : Issue::Severity::Error);
                        }
                    for (std::size_t k = 0; k < instances.size(); ++k) {
                        const auto& [iv, inst] = instances[k];
                        const auto args = symbolArguments(sv, *inst, &p);
                        if (args.empty()) continue;
                        for (const auto& [t, pb] : problemsOf(sv, site, &args)) {
                            // Un nom inconnu, un indice de l'argument : l'argument le dit ; faux
                            // dans le symbole : le symbole le dit.
                            if (plain.count(pb.message) || argProblems[k].count(pb.message) || !pb.unknownName.empty()) continue;
                            std::string back = pb.message;
                            for (const auto& [name, value] : args)
                                for (const auto& [dn, dv] : defaults)
                                    if (dn == name) replaceAll(back, value, dv);
                            if (withDefaults.count(back)) continue;
                            push(Where{iv, inst->id, "Symbole", "params"},
                                 "symbole " + sv.name + ", " + (c.name.empty() ? std::string("objet") : c.name) + " (" + site.key + ") : "
                                     + site.prefix + t + " : " + pb.message,
                                 {}, {}, {}, pb.warning ? Issue::Severity::Warning : Issue::Severity::Error);
                        }
                    }
                }
            }
        }
        // Les modeles de vues gardes dans le projet (ihm/modeles, lot 20) : leurs vues,
        // leurs symboles, leurs ecrans modeles - des definitions hors des vues du
        // projet, relues comme le petit projet qu'elles sont. Les noms n'y sont pas
        // verifies (les variables de l'automate ne voyagent pas ; on les choisit a la
        // creation) : le reste l'est (fonctions, types, couleurs, divisions...).
        for (const auto& tpl : p.viewTemplates) {
            if (!tpl.data || tpl.data->empty() || (focus && !focus->rest)) continue;
            auto pack = pkg::fromZip(*tpl.data);
            if (!pack) continue;
            std::vector<Issue> inner;
            const NameExists anyName = [](std::string_view) { return true; };
            ExprWalker{pack->content, anyName, inner, syntax, {}}.run();
            for (const auto& i : inner) {
                if (i.severity != Issue::Severity::Error) continue;
                std::string place;
                if (const View* v = pack->content.view(i.view)) {
                    place = v->name;
                    if (const Object* o = v->object(i.object)) place += "." + (o->name.empty() ? "objet " + std::to_string(o->id) : o->name);
                }
                if (!i.property.empty()) place += (place.empty() ? "" : " ") + std::string("(") + i.property + ")";
                push(Where{nullptr, kNoId, "Mod\xC3\xA8le de vue", tpl.name}, "mod\xC3\xA8le " + tpl.name + ", " + (place.empty() ? i.category : place) + " : " + i.message);
            }
        }
    }
    // ---- fin Lot API 8 : finitions ----
};
} // namespace

namespace {
// 1.11 (REP) : dans "Modeles d'objets", un modele peut garder un repere qui n'est pas
// encore une variable ($Vanne$, que Dupliquer... remplira) : ce constat n'y est pas dit.
void dropTemplateHints(const Project& p, std::vector<Issue>& out) {
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](const Issue& i) {
                                 const View* v = i.view != kNoId ? p.view(i.view) : nullptr;
                                 return v && dup::isTemplatesFolder(v->folder) && dup::isUnreplacedIssueMessage(i.message);
                             }),
              out.end());
}

// 1.11.1 (API-M) : le modele des variables de l'automate (API.), l'acces calcule
// sur la liaison de CE projet (sa table des adresses, Modbus ou simulateur) : l'appli
// le construit une fois pour le programme, Compiler et Generer suivent la
// Configuration > Communication du moment.
exprcheck::PlcPaths boundTo(const Project& p, const exprcheck::PlcPaths& in) {
    if (!in.api) return in;
    auto out = in;
    out.api = std::make_shared<const apivars::Model>(in.api->withLink(p));
    return out;
}

// 1.11.1 (API-M, decision D3) : une variable IHM ou une vue nommee API garde son sens
// (un projet de la 1.11.0 se calcule a l'identique) ; elle cache alors l'espace API.
// des variables de l'automate : le dire, une fois (l'automate connu).
void checkApiName(const Project& p, const exprcheck::PlcPaths& paths, std::vector<Issue>& out) {
    if (!paths.api) return;
    const auto* var = p.variable(apivars::kRoot);
    const View* view = var ? nullptr : pub::viewNamed(p, apivars::kRoot);
    if (!var && !view) return;
    const std::string what = var ? "la variable IHM " + var->name : "la vue " + view->name;
    add(out, Issue::Severity::Warning, var ? "Variable" : "Vue", view ? view->id : kNoId, kNoId, var ? var->name : std::string{},
        what + " passe avant les variables de l'automate : " + (var ? "API.X la lit, elle" : "API.X y lit ses objets")
            + " ; renomme-la pour lire l'automate par API.<variable>");
}
} // namespace

namespace {
std::vector<Issue> compileFocused(const Project& p, const CompileFocus* f);   // 1.11.13 : plus bas
} // namespace

std::vector<Issue> expressionIssues(const Project& p, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPathsIn) {
    std::vector<Issue> out;
    const auto plcPaths = boundTo(p, plcPathsIn);
    ExprWalker{p, plcHasName, out, true, plcPaths}.run();
    dropTemplateHints(p, out);
    return out;
}

std::vector<Issue> compileWith(const Project& p, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPathsIn) {
    return compileWith(p, plcHasName, plcPathsIn, nullptr);
}

std::vector<Issue> compileWith(const Project& p, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPaths,
                               const CompileFocus& focus) {
    return compileWith(p, plcHasName, plcPaths, &focus);
}

// 1.11.13 : les deux formes passent ici ; `focus` nul : tout.
std::vector<Issue> compileWith(const Project& p, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPathsIn,
                               const CompileFocus* focus) {
    auto out = compileFocused(p, focus);
    const auto plcPaths = boundTo(p, plcPathsIn);
    ExprWalker{p, plcHasName, out, false, plcPaths, focus}.run();
    if (!focus || focus->variables) checkApiName(p, plcPaths, out);   // 1.11.1 (API-M) : une variable ou une vue nommee API
    // 1.10 : les erreurs des scripts (noms, membres, appels, ecritures, types), a leur place.
    for (auto& i : scriptcheck::projectIssues(p, plcHasName, plcPaths, focus)) out.push_back(std::move(i));
    // 1.10 (decision 14 ; integration I2, pour N et S2) : les operateurs des symboles
    // et des types IHM - en double, operandes ou retour impossibles, la syntaxe et le
    // resultat de leur script (hmi::operatorIssues ; un DDT de l'automate est une
    // cible permise). Issue::item = l'operateur, Issue::line = la ligne de son
    // script : le double-clic l'y ouvre (l'ecran, openHmiIssue).
    for (const auto& oi : operatorIssues(p, plcPaths.isStruct)) {
        if (focus && !focus->operatorOwners.count(oi.owner.id)) continue;   // 1.11.13 : le filtre
        Issue i;
        i.severity = oi.error ? Issue::Severity::Error : Issue::Severity::Warning;
        i.category = "Op\xC3\xA9rateur";
        i.view = oi.owner.kind == OperatorOwner::Kind::Symbol ? oi.owner.id : kNoId;
        i.property = oi.signature;
        i.message = oi.owner.label() + " \xC2\xB7 " + oi.signature + (oi.line > 0 ? ", ligne " + std::to_string(oi.line) : std::string{})
                  + " : " + oi.message;
        i.item = oi.id;
        i.line = oi.line;
        i.column = oi.column;
        i.length = oi.length;
        out.push_back(std::move(i));
    }
    dropTemplateHints(p, out);
    return out;
}

namespace {
// 1.10 : Generer dit deja certaines fautes des scripts sans leur colonne
// ("variable inexistante : X", "SYS.X : variable systeme, en lecture seule") :
// le constat precis prend leur place (meme script, meme ligne, meme nom).
void mergeScriptIssues(std::vector<Issue>& out, std::vector<Issue> found) {
    for (auto& f : found) {
        const std::string head = f.message.substr(0, f.message.find(' '));
        bool merged = false;
        for (auto& o : out) {
            if (o.column != 0 || o.script != f.script || o.item != f.item || o.view != f.view || o.object != f.object || o.line != f.line)
                continue;
            if (o.category != f.category) continue;
            if (o.message != f.message && (head.empty() || o.message.find(head) == std::string::npos)) continue;
            // Le message de Generer reste (on le connait) ; le nom proche s'y ajoute.
            if (const auto near = f.message.find(" : veux-tu dire "); near != std::string::npos && o.message.find("veux-tu dire") == std::string::npos)
                o.message += f.message.substr(near);
            o.column = f.column;
            o.length = f.length;
            merged = true;
            break;
        }
        if (!merged) out.push_back(std::move(f));
    }
}
} // namespace
// ---- fin Lot API 8 : les expressions impossibles ----

std::vector<Issue> generate(const Project& p, const NameExists& plcHasName, const std::string& projectFolder) {
    GenerateOptions opt;
    opt.projectFolder = projectFolder;
    return generateWith(p, plcHasName, opt);
}

std::vector<Issue> generateWith(const Project& p, const NameExists& plcHasName, const GenerateOptions& optionsIn) {
    // 1.11.1 (API-M) : l'acces des variables de l'automate suit la liaison de ce projet.
    GenerateOptions options = optionsIn;
    options.plcPaths = boundTo(p, optionsIn.plcPaths);
    const std::string& projectFolder = options.projectFolder;
    using S = Issue::Severity;
    std::vector<Issue> out;
    if (p.views.empty()) add(out, S::Warning, "Projet", kNoId, kNoId, {}, "le projet IHM n'a aucune vue");
    if (p.config.startView == kNoId && !p.views.empty())
        add(out, S::Warning, "Projet", kNoId, kNoId, {}, "aucune vue de d\xC3\xA9marrage : la simulation partira de la premi\xC3\xA8re");
    else if (p.config.startView != kNoId && !p.view(p.config.startView))
        add(out, S::Error, "Projet", kNoId, kNoId, {}, "la vue de d\xC3\xA9marrage n'existe plus");

    // 1.11 (REP) : un repere ne bloque plus Generer (ses $ sont transparents).

    std::set<std::string> viewNames;
    std::set<Id> ids;
    for (const auto& v : p.views) {
        if (v.name.empty()) add(out, S::Error, "Vue", v.id, kNoId, {}, "vue sans nom");
        else if (!viewNames.insert(v.name).second) add(out, S::Error, "Vue", v.id, kNoId, {}, "nom de vue en double : " + v.name);
        if (v.width <= 0 || v.height <= 0) add(out, S::Error, "Vue", v.id, kNoId, {}, "taille invalide");
        if (v.layers.empty()) add(out, S::Error, "Vue", v.id, kNoId, {}, "aucun calque");
        if (!ids.insert(v.id).second) add(out, S::Error, "Vue", v.id, kNoId, {}, "identifiant en double");

        std::map<std::string, Id> names;
        for (const auto& o : v.objects) {
            if (!ids.insert(o.id).second)
                add(out, S::Error, "Objet", v.id, o.id, {}, "identifiant " + std::to_string(o.id) + " en double");
            if (!edit::validName(o.name))
                add(out, S::Error, "Objet", v.id, o.id, {}, "nom invalide : '" + o.name + "'");
            else if (const auto [it, fresh] = names.emplace(o.name, o.id); !fresh)
                add(out, S::Error, "Objet", v.id, o.id, {}, "nom en double dans la vue : " + o.name);
            if (v.layerRank(o.layer) < 0) add(out, S::Error, "Objet", v.id, o.id, {}, "calque introuvable");
            if (o.parent != kNoId) {
                const auto* g = v.object(o.parent);
                if (!g) add(out, S::Error, "Objet", v.id, o.id, {}, "groupe parent introuvable");
                else if (g->kind != Kind::Group && !kindHoldsChildren(g->kind))     // lot 12 : onglets, cadre, panneaux
                    add(out, S::Error, "Objet", v.id, o.id, {}, "le parent '" + g->name + "' n'est ni un groupe ni un conteneur");
            }
            // References circulaires : un objet qui finit par se contenir.
            Id cur = o.parent;
            for (int guard = 0; cur != kNoId; ++guard) {
                if (cur == o.id || guard > 64) {
                    add(out, S::Error, "Objet", v.id, o.id, {}, "r\xC3\xA9" "f\xC3\xA9rence circulaire : l'objet est dans son propre groupe");
                    break;
                }
                const auto* po = v.object(cur);
                cur = po ? po->parent : kNoId;
            }
            if (o.kind == Kind::Group && v.childrenOf(o.id).empty())
                add(out, S::Warning, "Objet", v.id, o.id, {}, "groupe vide");
            if ((o.kind == Kind::Line && edit::points(o).size() < 2) || (o.kind == Kind::Polygon && edit::points(o).size() < 3))
                add(out, S::Error, "Objet", v.id, o.id, "points", "pas assez de points");
            // Les ressources citees : presentes, du bon genre, et avec leur fichier.
            for (const char* resKey : {"image", "video", "poster", "font"}) {
                const auto* prop = o.find(resKey);
                if (!prop) continue;
                const MediaKind wanted = std::string_view(resKey) == "video" ? MediaKind::Video
                                       : std::string_view(resKey) == "font"  ? MediaKind::Font
                                                                             : MediaKind::Image;
                std::vector<std::string> cited;
                if (!prop->value.empty() && !(wanted == MediaKind::Font && prop->value == "Sans")) cited.push_back(prop->value);
                if (!prop->expr.empty())
                    for (auto& q : quotedStrings(prop->expr))
                        if (!formatFromExtension(q).empty()) cited.push_back(std::move(q));
                for (const auto& name : cited) {
                    const auto* res = p.resourceByName(name);
                    if (!res)
                        add(out, S::Warning, "Ressource", v.id, o.id, resKey, "ressource '" + name + "' introuvable dans le projet");
                    else if (res->kind() != wanted)
                        add(out, S::Error, "Ressource", v.id, o.id, resKey,
                            "'" + name + "' est " + std::string(mediaKindLabel(res->kind())) + ", pas "
                                + std::string(mediaKindLabel(wanted)));
                    else if (!res->data)
                        add(out, S::Error, "Ressource", v.id, o.id, resKey, "le fichier de la ressource '" + name + "' manque");
                }
            }
            if (o.kind == Kind::AnimatedGif) {
                // Lot 16 : un GIF (pas une image fixe), une lecture et un depart qui se tiennent.
                const std::string name = markers::strip(o.text("image"), markers::Mode::Text);   // 1.11 (REP-1) : sans ses $
                const auto* res = name.empty() ? nullptr : p.resourceByName(name);
                if (name.empty() && o.find("image") && o.find("image")->expr.empty())
                    add(out, S::Warning, "Objet", v.id, o.id, "image", "aucun GIF choisi : l'objet restera vide");
                else if (res && res->format != "GIF")
                    add(out, S::Error, "Ressource", v.id, o.id, "image",
                        "'" + name + "' est une image " + res->format + " : un GIF anim\xC3\xA9 attend une ressource GIF (l'objet Image la montre)");
                else if (res && res->data && res->seconds <= 0)
                    add(out, S::Info, "Ressource", v.id, o.id, "image", "'" + name + "' n'a qu'une image : il ne s'animera pas");
                const std::string play = o.text("play", "en boucle");
                if (play == "N fois" && o.number("count", 3) < 1)
                    add(out, S::Error, "Objet", v.id, o.id, "count", "N fois : le nombre de tours doit valoir au moins 1");
                const double speed = o.number("speed", 100);
                if (speed <= 0 || speed > 2000)
                    add(out, S::Error, "Objet", v.id, o.id, "speed", "vitesse hors de 1 \xC3\xA0 2000 % : " + formatNumber(speed));
                const std::string start = o.text("start", "\xC3\xA0 l'affichage");
                if (start == "sur condition") {
                    const std::string cond = o.text("condition");
                    if (trimmedCopy(cond).empty())
                        add(out, S::Error, "Objet", v.id, o.id, "condition", "d\xC3\xA9part sur condition : la condition est vide");
                    else if (!Expression::compile(cond).valid())
                        add(out, S::Error, "Objet", v.id, o.id, "condition", "condition illisible : " + cond);
                } else if (start == "sur action") {
                    // Quelqu'un doit le jouer : une action de la vue, ou un script qui le nomme.
                    bool played = false;
                    const auto scan = [&](const std::vector<Action>& list) {
                        for (const auto& a : list)
                            if ((a.operation == Operation::GifPlay || a.operation == Operation::GifReplay) && upperText(a.target) == upperText(o.name))
                                played = true;
                    };
                    scan(v.actions);
                    for (const auto& other : v.objects) scan(other.actions);
                    const std::string needle = upperText("'" + o.name + "'");
                    for (const auto& sc : p.programs.scripts)
                        if (upperText(sc.body).find(needle) != std::string::npos) played = true;
                    for (const auto& vs : v.scripts)
                        if (upperText(vs.body).find(needle) != std::string::npos) played = true;
                    if (!played)
                        add(out, S::Warning, "Objet", v.id, o.id, "start",
                            "d\xC3\xA9part sur action, mais aucune action (Jouer le GIF) ni aucun script ne le joue");
                }
            }
            if (o.kind == Kind::Table) {
                const auto source = o.text("source");
                if (!source.empty() && !p.externalByName(source))
                    add(out, S::Warning, "Fichier externe", v.id, o.id, "source", "fichier externe '" + source + "' introuvable dans le projet");
            }
            // Les variables lues : chaque racine doit exister dans le programme.
            if (plcHasName) {
                // Racine -> la premiere propriete qui la lit : le rapport dit OU
                // corriger, pas seulement quoi.
                std::map<std::string, std::string> roots;
                for (const auto& prop : o.props) {
                    // 1.11 (REP) : un champ a repere se lit comme les autres ($V[0]$.Nom : V).
                    if (!prop.expr.empty()) for (const auto& r : scanRoots(markers::strip(prop.expr))) roots.emplace(r, prop.key);
                    if (isTemplateKey(prop.key)) for (const auto& r : TextTemplate::compile(prop.value).roots()) roots.emplace(r, prop.key);
                    if (prop.key == "variable" && !prop.value.empty()) for (const auto& r : scanRoots(markers::strip(prop.value))) roots.emplace(r, prop.key);
                }
                for (const auto& [r, key] : roots)
                    if (!known(p, plcHasName, r, &v)) {
                        const auto* prop = o.find(key);
                        const std::string hint = prop ? dup::markerHint(prop->expr, r) + (prop->expr.empty() ? dup::markerHint(prop->value, r, !isTemplateKey(key)) : std::string{}) : std::string{};
                        add(out, S::Error, "Variable", v.id, o.id, key, "variable inexistante dans le programme : " + r + hint);
                    }
            }
        }
    }
    checkPrograms(p, plcHasName, out);
    checkSupervision(p, plcHasName, out);
    checkLot6(p, plcHasName, out);
    checkLot8(p, plcHasName, out);
    checkParams19(p, options.plcPaths, out);   // 1.9 : les parametres des popups
    checkLot9(p, plcHasName, out);
    checkPublicVars(p, out);
    checkSymbols(p, plcHasName, out);   // lot 10
    checkObjectAlarms(p, plcHasName, out);   // 1.9 : les alarmes des objets
    checkAlarmGroupLinks(p, out);           // 1.10.2 (AL) : les groupes d'alarmes et les liens
    checkLot11(p, plcHasName, out);     // lot 11
    checkLot12(p, plcHasName, out);     // lot 12
    checkLot13(p, out);                 // lot 13 : la signature, la politique
    checkLanguages(p, out);             // lot 13 : les langues
    checkDisplay(p, plcHasName, out);   // lot 13 : les unites, l'affichage
    checkScenarios(p, out);             // lot 13 : les essais de reception
    checkComm(p, plcHasName, options.plan, options.plcScalar, options.plcPaths.api.get(), out);   // lot 14 : la communication
    checkStation(p, out);                                             // lot 14 : le poste d'exploitation
    checkNotify(p, out);                                              // lot 14 : les notifications
    checkReports(p, out);                                             // lot 14 : les rapports
    checkWeb(p, out);                                                 // lot 14 : l'acces web
    checkEquipments(p, out);                                          // lot 15 : les equipements du reseau
    if (p.config.quality) checkQuality(p, qualityOptions(p, options.measure), out);   // lot 13 : la qualite

    // Ressources : les inutilisees, les fichiers qui manquent.
    std::set<std::string> resourceNames;
    for (const auto& r : p.assets.resources) {
        if (!resourceNames.insert(r.name).second)
            add(out, S::Error, "Ressource", kNoId, kNoId, r.name, "nom de ressource en double : " + r.name);
        if (!r.data) add(out, S::Error, "Ressource", kNoId, kNoId, r.name, "fichier absent : " + r.name);
    }
    for (const auto* r : unusedResources(p))
        add(out, S::Info, "Ressource", kNoId, kNoId, r->name, "ressource inutilis\xC3\xA9" "e : " + r->name + " (" + formatBytes(r->bytes) + ")");
    // Fichiers externes : l'etat du disque, quand on sait ou est le projet.
    for (const auto& f : p.assets.files) {
        const auto st = externalState(f, projectFolder);
        if (st.status == ExternalStatus::Missing)
            add(out, S::Warning, "Fichier externe", kNoId, kNoId, f.name,
                "fichier absent : " + f.path + (st.resolvedPath != f.path ? "  (" + st.resolvedPath + ")" : std::string{}));
        else if (st.status == ExternalStatus::Modified)
            add(out, S::Info, "Fichier externe", kNoId, kNoId, f.name,
                "modifi\xC3\xA9 depuis le lien (" + f.modified + " -> " + st.modified + ") : \xC2\xAB Relier \xC2\xBB en prend acte");
    }
    ExprWalker{p, plcHasName, out, true, options.plcPaths}.run();   // ---- Lot API 8 : les expressions impossibles ----
    mergeScriptIssues(out, scriptcheck::projectIssues(p, plcHasName, options.plcPaths));   // 1.10 : les scripts, a leur place
    checkApiName(p, options.plcPaths, out);   // 1.11.1 (API-M) : une variable ou une vue nommee API
    // 1.11.1 (API-M) : un chemin API. sans adresse qu'un script lit est dit a sa ligne ;
    // la Communication ne le redit pas.
    {
        // (R1111-9 : lue ou ecrite, le message d'un script commence par "API.X n'a pas d'adresse : ".)
        static constexpr std::string_view kNoAddress = " n'a pas d'adresse : ";
        std::set<std::string, std::less<>> inScripts;
        for (const auto& i : out)
            if (const auto at = i.message.find(kNoAddress);
                i.script != kNoId && i.severity == S::Warning && at != std::string::npos && apivars::isApiPath(i.message.substr(0, at)))
                inScripts.insert(i.message.substr(0, at));
        if (!inScripts.empty())
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [&](const Issue& i) {
                                         if (i.script != kNoId || i.category != "Communication") return false;
                                         const auto at = i.message.find(kNoAddress);
                                         return at != std::string::npos && inScripts.count(i.message.substr(0, at)) != 0;
                                     }),
                      out.end());
    }
    dropTemplateHints(p, out);
    return out;
}

namespace {
std::vector<Issue> compileFocused(const Project& p, const CompileFocus* f);
} // namespace

std::vector<Issue> compile(const Project& p) { return compileFocused(p, nullptr); }

namespace {
// 1.11.13 : compile(), limite a une partie (le build incremental) ; `f` nul : tout.
std::vector<Issue> compileFocused(const Project& p, const CompileFocus* f) {
    using S = Issue::Severity;
    std::vector<Issue> out;
    std::size_t expressions = 0, templates = 0, scripts = 0, actions = 0;
    // 1.10 (integration, S1 -> N) : un type IHM du projet (structure) est un type
    // permis pour une locale ou un retour (sans lui : le refus de la 1.9).
    const TypeKnown knownType = [&p](std::string_view t) {
        return !types::membersOf(p, t).empty() || findEnumeration(p, t) != nullptr;   // 1.10 (S1) : une enumeration aussi
    };
    for (const auto& v : p.views) {
        const bool anim = !f || f->anim(v.id), act = !f || f->act(v.id);   // 1.11.13 : le filtre
        // 1.11 (REP) : plus d'avertissement "repere non remplace" ; un champ a repere
        // s'analyse comme les autres (ses $ sont transparents).
        for (const auto& o : v.objects)
            for (const auto& prop : o.props) {
                if (!anim) break;
                if (!prop.expr.empty()) {
                    ++expressions;
                    const auto e = Expression::compile(prop.expr);
                    if (!e.valid()) add(out, S::Error, "Expression", v.id, o.id, prop.key, prop.expr + " : " + e.error());
                }
                if (isTemplateKey(prop.key)) {
                    const auto t = TextTemplate::compile(prop.value);
                    if (t.dynamic()) ++templates;
                    for (const auto& err : t.errors()) add(out, S::Error, "Texte", v.id, o.id, prop.key, err);
                }
                // Lot 6 : les cases d'un tableau et les conditions d'une image animee.
                if (prop.key == "cells" && prop.expr.empty()) {
                    const auto rows = parseCells(prop.value);
                    for (std::size_t r = 0; r < rows.size(); ++r)
                        for (std::size_t c = 0; c < rows[r].size(); ++c) {
                            const auto& cell = rows[r][c];
                            const std::string where = "case [" + std::to_string(r + 1) + "," + std::to_string(c + 1) + "]";
                            if (cellIsExpression(cell)) {
                                ++expressions;
                                const auto e = Expression::compile(cell.substr(1));
                                if (!e.valid()) add(out, S::Error, "Expression", v.id, o.id, "cells", where + " " + cell + " : " + e.error());
                            } else if (cellIsTemplate(cell)) {
                                const auto t = TextTemplate::compile(cell);
                                if (t.dynamic()) ++templates;
                                for (const auto& err : t.errors()) add(out, S::Error, "Texte", v.id, o.id, "cells", where + " : " + err);
                            }
                        }
                }
                if (prop.key == "states" && prop.expr.empty()) {
                    const auto states = parseImageStates(prop.value);
                    for (std::size_t k = 0; k < states.size(); ++k) {
                        ++expressions;
                        const auto e = Expression::compile(states[k].condition.empty() ? std::string("TRUE") : states[k].condition);
                        if (!e.valid())
                            add(out, S::Error, "Expression", v.id, o.id, "states",
                                "\xC3\xA9tat " + std::to_string(k + 1) + " : " + states[k].condition + " : " + e.error());
                    }
                }
            }
        for (const auto& sc : v.scripts) {
            if (f && !f->scripts.count(sc.id)) continue;
            ++scripts;
            for (const auto& d : checkScript(sc, knownType))      // 1.11.18 (lot 3) : avec ses declarations du modele
                add(out, severityOf(d.severity), "Script", v.id, kNoId, sc.name, d.message, sc.id, d.line);
        }
        // Lot 8 : le titre a trous d'une popup, la valeur par defaut des
        // parametres, les bornes d'un champ de saisie.
        if (anim && v.role == "popup" && !v.popup.title.empty()) {
            const auto t = TextTemplate::compile(v.popup.title);
            if (t.dynamic()) ++templates;
            for (const auto& err : t.errors()) add(out, S::Error, "Popup", v.id, kNoId, "popup_libelle", "titre : " + err);
        }
        for (const auto& prm : v.params) {
            const std::string def = trimmedCopy(prm.defaultValue);
            if (!anim || def.empty() || isVariablePath(def)) continue;
            ++expressions;
            const auto e = Expression::compile(def);
            if (!e.valid()) add(out, S::Error, "Param\xC3\xA8tre", v.id, kNoId, "parametres", prm.name + " := " + def + " : " + e.error());
        }
        for (const auto& o : v.objects)
            if (anim && o.kind == Kind::InputField)
                for (const char* key : {"min", "max"}) {
                    const std::string b = trimmedCopy(o.text(key));
                    double x = 0;
                    if (b.empty() || parseNumber(b, x)) continue;
                    ++expressions;
                    const auto e = Expression::compile(b);
                    if (!e.valid()) add(out, S::Error, "Expression", v.id, o.id, key, b + " : " + e.error());
                }
        // Les actions : leurs expressions, leur code ST, leurs messages.
        const auto actionsOf = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& a = list[i];
                ++actions;
                const std::string where = actionLabel(i, a);
                const Id obj = o ? o->id : kNoId;
                const auto expr = [&](const std::string& src, const char* what) {
                    if (src.empty()) return;
                    const auto e = Expression::compile(src);
                    if (!e.valid()) add(out, S::Error, "Action", v.id, obj, where, std::string(what) + " " + src + " : " + e.error());
                };
                expr(a.watch, "expression surveill\xC3\xA9" "e");
                expr(a.guard, "condition");
                if (a.operation == Operation::Assign || a.operation == Operation::Increment || a.operation == Operation::Decrement)
                    expr(a.value, "valeur");
                if (a.operation == Operation::Log)
                    for (const auto& err : TextTemplate::compile(a.value).errors()) add(out, S::Error, "Action", v.id, obj, where, err);
                if (a.operation == Operation::RunScript)
                    for (const auto& d : checkScript(ScriptLang::ST, a.value, where, knownType))
                        add(out, severityOf(d.severity), "Action", v.id, obj, where,
                            (d.line ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + d.message);
                // Lot 8 : les parametres passes a la vue ouverte (un chemin, ou un calcul).
                if (operationTakesArguments(a.operation))
                    for (const auto& [name, value] : parseArguments(a.value))
                        if (!isVariablePath(markers::strip(value))) expr(value, ("param\xC3\xA8tre " + name + " :").c_str());
            }
        };
        if (act) {
            actionsOf(nullptr, v.actions);
            for (const auto& o : v.objects)
                if (!o.actions.empty()) actionsOf(&o, o.actions);
        }
    }
    for (const auto& sc : p.programs.scripts) {
        if (f && !f->scripts.count(sc.id)) continue;
        ++scripts;
        for (const auto& d : checkScript(sc, knownType))      // 1.11.18 (lot 3) : avec ses declarations du modele
            add(out, severityOf(d.severity), "Script", kNoId, kNoId, sc.name, d.message, sc.id, d.line);
        if (sc.event == "Changement" && !sc.watch.empty()) {
            const auto e = Expression::compile(sc.watch);
            if (!e.valid()) add(out, S::Error, "Script", kNoId, kNoId, sc.name, "expression surveill\xC3\xA9" "e " + sc.watch + " : " + e.error(), sc.id);
        }
    }
    // Lot 7 : le corps des fonctions IHM (declarations, code, retour).
    std::size_t functions = 0;
    for (const auto& fn : p.programs.functions) {
        if (f && !f->functions.count(fn.id)) continue;
        ++functions;
        for (const auto& d : checkFunction(fn, knownType)) {
            Issue i;
            i.severity = severityOf(d.severity);
            i.category = "Fonction";
            i.property = fn.name;
            i.message = d.message;
            i.item = fn.id;
            i.line = d.line;
            out.push_back(std::move(i));
        }
    }
    for (const auto& var : p.programs.variables)
        if (!var.initial.empty() && (!f || f->variables)) {
            // Lot 16 : un tableau de cases simples accepte une liste ("1.5, 2, 3") ;
            // une structure prend les valeurs de son type.
            types::Spec spec;
            if (types::isComposite(var.type) && types::parseSpec(var.type, spec) && !(spec.array() && types::isElementary(spec.element)))
                continue;
            const auto f = types::isComposite(var.type) ? types::flatten(p, var.name, var.type, var.initial, var.packBools) : types::Flat{};
            std::set<std::string> seen;
            const auto test = [&](const std::string& text) {
                if (text.empty() || !seen.insert(text).second) return;
                const auto e = Expression::compile(text);
                if (!e.valid()) add(out, S::Error, "Variable IHM", kNoId, kNoId, var.name, "valeur initiale " + text + " : " + e.error());
            };
            if (types::isComposite(var.type)) for (const auto& l : f.leaves) test(l.initial);
            else test(var.initial);
            if (types::isComposite(var.type) && f.ok()) {
                std::size_t n = 0;
                for (char c : var.initial) n += c == ',' ? 1 : 0;
                if (n + 1 > f.leaves.size())
                    add(out, S::Warning, "Variable IHM", kNoId, kNoId, var.name,
                        var.name + " : " + std::to_string(n + 1) + " valeurs initiales pour " + std::to_string(f.leaves.size()) + " cases (les derni\xC3\xA8res sont ignor\xC3\xA9" "es)");
            }
        }
    // Lot 4 : conditions et messages d'alarme, valeurs de recette, autorisations.
    std::size_t alarms = 0;
    for (const auto& a : p.alarms) {
        if (f && !f->alarms.count(a.id)) continue;
        ++alarms;
        if (!a.condition.empty()) {
            ++expressions;
            const auto e = Expression::compile(a.condition);
            if (!e.valid()) addItem(out, S::Error, "Alarme", a.name, "condition " + a.condition + " : " + e.error(), a.id);
        }
        const auto t = TextTemplate::compile(a.message);
        if (t.dynamic()) ++templates;
        for (const auto& err : t.errors()) addItem(out, S::Error, "Alarme", a.name, "message : " + err, a.id);
    }
    for (const auto& r : p.recipes)
        for (const auto& rec : r.records)
            for (std::size_t i = 0; i < rec.values.size(); ++i)
                if (!rec.values[i].empty() && (!f || f->recipes.count(r.id))) {
                    ++expressions;
                    const auto e = Expression::compile(rec.values[i]);
                    if (!e.valid())
                        addItem(out, S::Error, "Recette", r.name,
                                rec.name + " / " + (i < r.fields.size() ? r.fields[i].name : std::to_string(i + 1)) + " : "
                                    + rec.values[i] + " : " + e.error(), r.id);
                }
    for (const auto& u : p.security.users)
        if (u.protection == "expression" && !u.expression.empty() && (!f || f->users)) {
            ++expressions;
            const auto e = Expression::compile(u.expression);
            if (!e.valid()) addItem(out, S::Error, "Utilisateur", u.login, "autorisation " + u.expression + " : " + e.error(), u.id);
        }
    // Lot 9 : les cases qui sont une expression (retour d'etat, voyant, condition du compteur).
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            for (const char* key : {"state", "lamp", "condition"}) {
                if (f && !f->anim(v.id)) break;
                const auto* prop = o.find(key);
                if (!prop || !prop->expr.empty() || trimmedCopy(prop->value).empty()) continue;
                if (!kindWritesVariable(o.kind) && o.kind != Kind::HourMeter) continue;
                ++expressions;
                const auto e = Expression::compile(prop->value);
                if (!e.valid()) add(out, S::Error, "Expression", v.id, o.id, key, prop->value + " : " + e.error());
            }
    // ---- Lot API 8 : corrections des captures ---- la syntaxe du texte-expression du lot 11.
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            for (const char* key : valueExpressionKeys(o.kind)) {
                if (f && !f->anim(v.id)) break;
                const auto* prop = o.find(key);
                if (!prop || !prop->expr.empty() || trimmedCopy(prop->value).empty()) continue;
                ++expressions;
                const auto e = Expression::compile(prop->value);
                if (!e.valid()) add(out, S::Error, "Expression", v.id, o.id, key, prop->value + " : " + e.error());
            }
    if (f) return out;   // 1.11.13 : une partie - ni bilan, ni liens des groupes (le reste)
    add(out, S::Info, "Bilan", kNoId, kNoId, {},
        std::to_string(expressions) + " expression(s), " + std::to_string(templates) + " texte(s) \xC3\xA0 trous, "
            + std::to_string(scripts) + " script(s), " + std::to_string(actions) + " action(s), " + std::to_string(alarms)
            + " alarme(s), " + std::to_string(functions) + " fonction(s) compil\xC3\xA9(s)");
    checkAlarmGroupLinks(p, out);   // 1.10.2 (AL) : Compiler dit les liens vers un groupe absent
    return out;
}
} // namespace

} // namespace hmi
