// =============================================================================
//  app/hmi/HmiParamPanes.cpp - 1.9 : les parametres des popups dans l'editeur
// -----------------------------------------------------------------------------
//  Voir HmiParamPanes.hpp. Les textes de l'ecran suivent la maquette 1.9
//  (scenes P1 a P6) : Parametres | Infos, pastilles REF / COPIE / LES DEUX,
//  "Parametres de <popup>", "Tous les parametres en mode les deux",
//  "modifie, pas encore applique".
// =============================================================================
#include "HmiParamPanes.hpp"
#include "../../hmi/HmiRoles.hpp"   // 1.12.19 : l'invite d'une case vide (defaultInvite)

#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../project/RenamePlan.hpp"
#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "HmiTypePicker.hpp"   // 1.11.19 (refonte, lot 6) : "Choisir un type..."
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : les champs a expression, partout pareils

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>

namespace app::hmiparams {

namespace {

using PG = ui::PropertyGrid;
using hmi::ParamMode;
using hmi::ViewParam;

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool same(std::string_view a, std::string_view b) { return upper(a) == upper(b); }

std::map<hmi::Id, Part>& parts() {
    static std::map<hmi::Id, Part> m;
    return m;
}
std::function<void(const std::string&, const std::string&)>& openHost() {
    static std::function<void(const std::string&, const std::string&)> h;
    return h;
}

// Le groupe d'un type ("type de base", "type IHM", "DDT de l'API", "tableau").
std::string typeGroup(const hmi::Project& p, const std::string& type, const hmi::params::PlcTypes& plc) {
    if (trim(type).empty()) return "type de base (ANY)";
    // 1.12.4 : une liste, un vecteur, un dictionnaire, un tuple - dans la memoire de l'IHM.
    if (hmi::types::isRich(type)) return "type objet (dans la m\xC3\xA9moire de l'IHM)";
    const std::string u = upper(type);
    if (u.rfind("ARRAY", 0) == 0) return "tableau";
    for (const auto& b : hmi::params::baseTypes())
        if (b == u || (b == "STRING" && u.rfind("STRING[", 0) == 0)) return "type de base";
    for (const auto& t : p.programs.types)
        if (same(t.name, type)) return "type IHM";
    if (plc.names)
        for (const auto& n : plc.names())
            if (same(n, type)) return "DDT de l'API";
    return "type inconnu";
}

std::string shownType(const std::string& type) { return trim(type).empty() ? std::string("ANY") : type; }

// "ARRAY[1..10] OF REAL" -> ("1..10", "REAL") ; sinon ("", type).
std::pair<std::string, std::string> splitArray(const std::string& type) {
    const std::string u = upper(type);
    if (u.rfind("ARRAY", 0) != 0) return {{}, type};
    const auto open = type.find('['), close = type.find(']');
    const auto of = u.find(" OF ");
    if (open == std::string::npos || close == std::string::npos || of == std::string::npos || close < open) return {{}, type};
    return {trim(std::string_view(type).substr(open + 1, close - open - 1)), trim(std::string_view(type).substr(of + 4))};
}

PG::Property prop(std::string name, std::string value, PG::ValueType t, std::string help = {},
                  std::vector<std::string> choices = {}, std::function<bool(std::string_view)> commit = nullptr) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = commit ? t : PG::ValueType::ReadOnly;
    p.description = std::move(help);
    p.enumValues = std::move(choices);
    p.commit = std::move(commit);
    return p;
}

hmi::View* viewOf(hmi::Project& p, hmi::Id id) { return p.view(id); }

// 1.10 (chantier K) : ce qu'attend un parametre de ce type (l'invite, l'aide a la saisie).
ui::exprfield::Expect expectOfType(const std::string& declared) {
    const std::string t = upper(trim(declared));
    if (t == "BOOL" || t == "EBOOL") return ui::exprfield::Expect::Bool;
    if (t.rfind("STRING", 0) == 0) return ui::exprfield::Expect::Text;
    if (t == "TIME") return ui::exprfield::Expect::Time;
    for (const char* n : {"INT", "DINT", "UINT", "UDINT", "SINT", "USINT", "REAL", "LREAL", "WORD", "DWORD", "BYTE"})
        if (t == n) return ui::exprfield::Expect::Number;
    return ui::exprfield::Expect::Value;
}

// Le controle de l'inspecteur (hmiExpressionError) : "" si l'expression peut
// marcher dans `in` (ses parametres connus), sinon le premier probleme. Les
// chemins de l'automate ne se calculent qu'une fois par fiche.
struct FxCheck {
    const hmi::Project&    project;
    const domain::Project* plc;
    std::shared_ptr<const hmi::exprcheck::PlcPaths> paths{};
    std::string operator()(const hmi::View& in, const std::string& expr) {
        if (!paths) paths = std::make_shared<const hmi::exprcheck::PlcPaths>(hmiPlcPaths(plc));
        return hmiExpressionError(in, {}, expr, plc, &project, nullptr, paths.get());
    }
};

} // namespace

// ------------------------------------------------------------------ les DDT -----
namespace {
std::function<std::shared_ptr<const domain::Project>()>& programHost() {
    static std::function<std::shared_ptr<const domain::Project>()> h;
    return h;
}
} // namespace
void setProgram(std::function<std::shared_ptr<const domain::Project>()> plc) { programHost() = std::move(plc); }
std::shared_ptr<const domain::Project> program() { return programHost() ? programHost()() : nullptr; }

hmi::params::PlcTypes plcTypesOf(const domain::Project* plc) {
    hmi::params::PlcTypes out;
    if (!plc) return out;
    // Une copie des noms et des membres (le programme peut etre relu ensuite).
    auto ddts = std::make_shared<std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>>();
    for (const auto& dt : plc->derivedTypes) {
        std::vector<std::pair<std::string, std::string>> members;
        for (const auto vi : dt.fields)
            if (vi < plc->variables.size()) {
                const auto& v = plc->variables[vi];
                members.emplace_back(std::string(plc->strings.text(v.name)), std::string(plc->strings.text(v.type.name)));
            }
        ddts->emplace_back(std::string(plc->strings.text(dt.name)), std::move(members));
    }
    auto types = std::make_shared<const project::rename::PlcTypes>(*plc);
    out.names = [ddts] {
        std::vector<std::string> n;
        for (const auto& d : *ddts) n.push_back(d.first);
        return n;
    };
    out.members = [ddts](std::string_view type) {
        for (const auto& d : *ddts)
            if (same(d.first, type)) return d.second;
        return std::vector<std::pair<std::string, std::string>>{};
    };
    out.rootType = [types](std::string_view root) {
        const auto r = types->root(root);
        return r.is == project::rename::RootIs::Plc ? r.type : std::string{};
    };
    out.memberType = [types](std::string_view type, std::string_view member) { return types->memberType(type, member); };
    out.isType = [ddts](std::string_view type) {
        return std::any_of(ddts->begin(), ddts->end(), [&](const auto& d) { return same(d.first, type); });
    };
    return out;
}

// ------------------------------------------------------- la sous-section -----
Part shownPart(hmi::Id view) {
    const auto it = parts().find(view);
    return it == parts().end() ? Part::List : it->second;
}
void showPart(hmi::Id view, Part part) { parts()[view] = part; }

bool isParamField(std::string_view field) noexcept { return field.rfind(kFieldPrefix, 0) == 0; }

void setOpenObjectHost(std::function<void(const std::string&, const std::string&)> host) { openHost() = std::move(host); }
void openObject(const std::string& view, const std::string& object) {
    if (openHost()) openHost()(view, object);
}

Target parseTarget(const std::string& value) {
    const auto slash = value.find('/');
    if (slash == std::string::npos) return {value, {}};
    return {value.substr(0, slash), value.substr(slash + 1)};
}

std::vector<PG::Category> paramCategories(const hmi::Project& p, const hmi::View& v, const domain::Project* plcProject,
                                          const Commit& commit) {
    std::vector<PG::Category> out;
    const bool symbol = hmi::isSymbolView(v);
    if (v.role != "popup" && !symbol && v.params.empty()) return out;
    const auto plc = plcTypesOf(plcProject);
    const auto field = [commit](std::string f) {
        return [commit, f](std::string_view s) { return commit && commit(f, std::string(s)); };
    };
    const auto openers = hmi::params::popupOpeners(p, v);
    FxCheck fx{p, plcProject};
    const std::string listName = "Param\xC3\xA8tres (" + std::to_string(v.params.size()) + ")";
    const std::string infoName = "Infos (" + std::to_string(openers.size()) + (openers.size() > 1 ? " appels)" : " appel)");
    const Part part = shownPart(v.id);

    PG::Category sec;
    sec.name = "Param\xC3\xA8tres";
    sec.properties.push_back(prop("Montrer", part == Part::List ? listName : infoName, PG::ValueType::Enum,
                                  "Param\xC3\xA8tres : la liste (nom, type, mode, valeur par d\xC3\xA9" "faut, description). "
                                  "Infos : qui ouvre cette popup, o\xC3\xB9 chaque param\xC3\xA8tre est employ\xC3\xA9.",
                                  {listName, infoName}, field("param19:partie")));
    if (part == Part::List) {
        sec.properties.push_back(prop("Ajouter", "", PG::ValueType::Text,
                                      "Tape le nom du nouveau param\xC3\xA8tre (Moteur, Consigne) : il s'ajoute en fin de liste, "
                                      "en R\xC3\xA9" "f\xC3\xA9rence, type ANY.",
                                      {}, field("param19:ajouter")));
        // Les types proposes : base, types IHM, DDT de l'API (dans cet ordre) - sans hote (un essai) ;
        // 1.12.14 : avec lui, la case Type ouvre le selecteur de types (les formes, les types imbriques).
        std::vector<std::string> typeChoices;
        for (const auto& c : hmi::params::proposedTypes(p, plc)) typeChoices.push_back(c.name);
        std::vector<std::string> modeChoices;
        for (const auto m : hmi::params::kParamModes) modeChoices.emplace_back(hmi::params::paramModeLabel(m));
        for (std::size_t i = 0; i < v.params.size(); ++i) {
            const ViewParam& prm = v.params[i];
            const std::string key = "param19:" + std::to_string(i) + ":";
            PG::Category row;
            row.name = prm.name + "  \xC2\xB7  " + shownType(prm.type) + "  \xC2\xB7  "
                       + std::string(hmi::params::paramModeBadge(symbol ? ParamMode::Reference : prm.mode));
            row.pill = modePill(symbol ? ParamMode::Reference : prm.mode);   // 1.9 (chantier U) : la pastille du titre (P1)
            row.properties.push_back(prop("Nom", prm.name, PG::ValueType::Text,
                                          "Renommer met \xC3\xA0 jour chaque emploi : la vue (propri\xC3\xA9t\xC3\xA9s, actions, scripts, "
                                          "titre), et pour un symbole son code (fonctions, alarmes, popups, red\xC3\xA9" "finitions) ; "
                                          "l'argument de chaque instance et de chaque action qui l'ouvre. Un seul Ctrl+Z.",
                                          {}, field(key + "nom")));
            // 1.12.4 : UNE SEULE LIGNE Type - le type entier dit s'il est un tableau (ARRAY[1..10] OF REAL),
            // une liste, un dictionnaire, un tuple (plus de ligne Tableau a cote).
            std::vector<std::string> choices = typeChoices;
            const std::string shown = shownType(prm.type);
            if (std::find(choices.begin(), choices.end(), shown) == choices.end()) choices.insert(choices.begin(), shown);
            row.properties.push_back(prop("Type", shown, PG::ValueType::Enum,
                                          typeGroup(p, prm.type, plc) + ". Types de base, types IHM du projet (structures, "
                                          "\xC3\xA9num\xC3\xA9rations), DDT de l'API" + (plc.names ? std::string(" (import\xC3\xA9s du programme)")
                                                                                  : std::string(" (aucun programme charg\xC3\xA9)"))
                                              + ". Un clic ouvre le s\xC3\xA9lecteur de types : un tableau (ARRAY[1..10] OF REAL), une liste, "
                                                "un vecteur, une MAP, un tuple, imbriqu\xC3\xA9s \xC3\xA0 volont\xC3\xA9 (MAP[STRING] OF VECTOR OF T_Cuve).",
                                          choices, field(key + "type")));
            // 1.12.14 : la case ouvre le selecteur de types (« Choisir un type... » de toujours, sans la liste).
            if (typepicker::available())
                if (auto commit = row.properties.back().commit) row.properties.back().dialog = [commit] { (void)commit(typepicker::kChoose); };
            if (symbol)
                row.properties.push_back(prop("Mode", "R\xC3\xA9" "f\xC3\xA9rence (REF)", PG::ValueType::ReadOnly,
                                              "Un symbole n'a que le mode R\xC3\xA9" "f\xC3\xA9rence : l'instance est d\xC3\xA9velopp\xC3\xA9" "e en texte."));
            else
                row.properties.push_back(prop("Mode", std::string(hmi::params::paramModeLabel(prm.mode)), PG::ValueType::Enum,
                                              std::string(hmi::params::paramModeHelp(prm.mode)), modeChoices, field(key + "mode")));
            // 1.9 (chantier U) : la pastille de couleur tient lieu de valeur (P1).
            row.properties.back().pill = modePill(symbol ? ParamMode::Reference : prm.mode);
            row.properties.back().pillOnly = true;
            {
                // 1.9 (decision 7) : un chemin ou un calcul porte la pastille fx (rouge s'il ne
                // peut pas marcher : inconnu, syntaxe, type qui ne convient pas) ; un litteral, non.
                std::string help = "Sert dans l'\xC3\xA9" "diteur et quand l'appelant ne donne pas ce param\xC3\xA8tre.";
                const std::string def = trim(prm.defaultValue);
                std::string error;
                if (!def.empty()) {
                    const std::string given = hmi::params::expressionType(p, nullptr, def, plc);
                    if (!hmi::params::typeAccepts(prm.type, given))
                        error = "la valeur par d\xC3\xA9" "faut (" + def + ", " + hmi::params::normalizedType(given)
                                + ") ne convient pas au type " + hmi::params::normalizedType(prm.type);
                }
                // 1.12.19 (la regle des cases) : une valeur fixe se montre telle qu'on la tape (Pompe, sans
                // apostrophes) ; une formule avec son = et sa pastille. Avant : 'Pompe' a taper, le = retire.
                const bool expression = !def.empty() && !hmi::isLiteralArgument(def);
                if (expression) {
                    if (const auto e = fx(v, def); !e.empty()) error = e;
                }
                auto d = prop("Valeur par d\xC3\xA9" "faut", expression ? def : hmi::shownLiteral(def), PG::ValueType::Text,
                              error.empty() ? help : error + ". " + help, {}, field(key + "defaut"));
                if (expression) d.expression = def;
                d.exprError = error;
                ui::exprfield::mark(d, expectOfType(prm.type));
                row.properties.push_back(std::move(d));
            }
            row.properties.push_back(prop("Description", prm.description, PG::ValueType::Text, {}, {}, field(key + "description")));
            std::vector<std::string> moves;
            if (i > 0) moves.emplace_back("Monter");
            if (i + 1 < v.params.size()) moves.emplace_back("Descendre");
            moves.emplace_back("Supprimer");
            row.properties.push_back(prop("Ordre", "\xE2\x80\xA6", PG::ValueType::Enum,
                                          "Monter, Descendre : l'ordre des arguments propos\xC3\xA9s. Supprimer : retire le param\xC3\xA8tre ; "
                                          "employ\xC3\xA9, une question liste ses emplois d'abord. Ses emplois et les arguments qui lui sont "
                                          "donn\xC3\xA9s restent et deviennent des fautes de compilation, \xC3\xA0 corriger.",
                                          moves, field(key + "ordre")));
            // 1.12.18 (planche 9) : le menu du titre du parametre - Renommer partout, Monter, Descendre,
            // Retirer (les memes que la liste Ordre), avec leurs touches.
            {
                const auto ordre = row.properties.back().commit;
                row.menuRight = shown + (symbol ? std::string(" \xC2\xB7 ") + v.name : std::string{});
                PG::Category::Action rename;
                rename.label = "Renommer partout\xE2\x80\xA6";
                rename.shortcut = "F2";
                rename.edit = "Nom";
                rename.icon = ui::Icon::Rename;
                row.actions.push_back(std::move(rename));
                const auto move = [&](std::string label, std::string keyName, std::string value, bool ok, std::string why) {
                    PG::Category::Action a;
                    a.label = std::move(label);
                    a.shortcut = std::move(keyName);
                    a.enabled = ok && static_cast<bool>(ordre);
                    a.why = std::move(why);
                    a.run = [ordre, value] { if (ordre) (void)ordre(value); };
                    return a;
                };
                row.actions.push_back(move("Monter", "Alt+\xE2\x86\x91", "Monter", i > 0, "d\xC3\xA9j\xC3\xA0 le premier"));
                row.actions.push_back(move("Descendre", "Alt+\xE2\x86\x93", "Descendre", i + 1 < v.params.size(), "d\xC3\xA9j\xC3\xA0 le dernier"));
                auto remove = move("Retirer\xE2\x80\xA6", "Suppr", "Supprimer", true, {});
                remove.icon = ui::Icon::Delete;
                row.actions.push_back(std::move(remove));
            }
            row.expanded = v.params.size() <= 4;
            sec.children.push_back(std::move(row));
        }
        // Les trois modes, toujours expliques sous la liste.
        PG::Category modes;
        modes.name = "Les modes";
        for (const auto m : hmi::params::kParamModes)
            modes.properties.push_back(prop(std::string(hmi::params::paramModeBadge(m)), std::string(hmi::params::paramModeHelp(m)),
                                            PG::ValueType::ReadOnly, std::string(hmi::params::paramModeHelp(m))));
        sec.children.push_back(std::move(modes));
    } else {
        // Qui ouvre cette popup : vue > objet > geste -> operation, et les arguments.
        PG::Category who;
        who.name = "Qui ouvre cette popup";
        if (openers.empty())
            who.properties.push_back(prop("Aucune action", "aucune action Ouvrir une popup ne la nomme", PG::ValueType::ReadOnly));
        for (const auto& o : openers) {
            const auto* caller = p.viewByName(o.view);
            // Rouge : une erreur, ou un argument dont l'expression ne peut pas marcher (decision 7) ;
            // un avertissement est dit dans l'aide.
            std::string trouble, warning;
            for (const auto& pb : hmi::params::checkArguments(p, caller, v, o.arguments, plc)) {
                std::string& to = pb.error ? trouble : warning;
                to += (to.empty() ? "" : " ; ") + pb.message;
            }
            for (const auto& g : hmi::parseArguments(o.arguments))
                if (const std::string a = trim(g.second); !a.empty() && v.param(g.first))
                    if (const auto e = fx(caller ? *caller : v, a); !e.empty())
                        trouble += (trouble.empty() ? "" : " ; ") + g.first + " : " + e;
            auto row = prop(o.view + (o.object.empty() ? std::string{} : " \xE2\x80\xBA " + o.object) + " \xE2\x80\xBA " + o.gesture
                                + " \xE2\x86\x92 " + o.operation,
                            o.arguments.empty() ? std::string("(aucun argument)") : o.arguments, PG::ValueType::Enum,
                            (trouble.empty() ? std::string{} : trouble + ". ") + (warning.empty() ? std::string{} : warning + ". ")
                                + "Un clic y m\xC3\xA8ne.",
                            {"Aller \xC3\xA0 l'objet"}, field("param19:aller:" + o.view + "/" + o.object));
            if (!trouble.empty()) {
                row.expression = o.arguments;
                row.exprError = trouble;
            }
            who.properties.push_back(std::move(row));
        }
        sec.children.push_back(std::move(who));
        PG::Category uses;
        uses.name = "O\xC3\xB9 chaque param\xC3\xA8tre est employ\xC3\xA9";
        for (const auto& prm : v.params) {
            const auto list = hmi::params::paramUses(v, prm.name);
            if (list.empty()) {
                uses.properties.push_back(prop(prm.name, "employ\xC3\xA9 nulle part", PG::ValueType::ReadOnly));
                continue;
            }
            for (const auto& u : list)
                uses.properties.push_back(prop(prm.name + " \xC2\xB7 " + (u.object.empty() ? v.name : u.object) + " \xC2\xB7 " + u.where,
                                               u.text, PG::ValueType::Enum, "Un clic y m\xC3\xA8ne.", {"Aller \xC3\xA0 l'objet"},
                                               field("param19:aller:" + v.name + "/" + u.object)));
        }
        sec.children.push_back(std::move(uses));
    }
    out.push_back(std::move(sec));
    return out;
}

std::string fieldLabel(std::string_view field, const std::string& value) {
    const std::string f(field);
    if (f == "param19:ajouter") return "Ajouter le param\xC3\xA8tre " + trim(value);
    if (f.size() > 4 && f.compare(f.size() - 4, 4, ":nom") == 0) return "Renommer le param\xC3\xA8tre en " + trim(value);
    if (f.size() > 6 && f.compare(f.size() - 6, 6, ":ordre") == 0)
        return value == "Supprimer" ? std::string("Supprimer le param\xC3\xA8tre") : "D\xC3\xA9placer le param\xC3\xA8tre";
    return "Modifier un param\xC3\xA8tre";
}

FieldResult applyParamField(hmi::Project& p, hmi::Id viewId, std::string_view field, const std::string& value) {
    FieldResult r;
    r.label = fieldLabel(field, value);
    hmi::View* v = viewOf(p, viewId);
    if (!v || !isParamField(field)) { r.ok = false; r.why = "vue introuvable"; return r; }
    const std::string rest(field.substr(kFieldPrefix.size()));
    if (rest == "ajouter") {
        const std::string name = trim(value);
        if (name.empty()) { r.ok = false; r.why = "tape un nom"; return r; }
        if (!hmi::isIdentifier(name)) { r.ok = false; r.why = "param\xC3\xA8tre : nom invalide '" + name + "'"; return r; }
        if (v->param(name)) { r.ok = false; r.why = "'" + name + "' existe d\xC3\xA9j\xC3\xA0"; return r; }
        ViewParam prm;
        prm.name = name;
        v->params.push_back(std::move(prm));
        return r;
    }
    const auto colon = rest.find(':');
    if (colon == std::string::npos) { r.ok = false; r.why = "champ inconnu"; return r; }
    std::size_t index = 0;
    if (colon == 0 || colon > 6) { r.ok = false; r.why = "champ inconnu"; return r; }
    for (std::size_t k = 0; k < colon; ++k) {
        if (!std::isdigit(static_cast<unsigned char>(rest[k]))) { r.ok = false; r.why = "champ inconnu"; return r; }
        index = index * 10 + static_cast<std::size_t>(rest[k] - '0');
    }
    if (index >= v->params.size()) { r.ok = false; r.why = "param\xC3\xA8tre introuvable"; return r; }
    const std::string what = rest.substr(colon + 1);
    ViewParam& prm = v->params[index];
    if (what == "nom") {
        const std::string name = trim(value);
        if (name == prm.name) return r;
        if (!hmi::isIdentifier(name)) { r.ok = false; r.why = "param\xC3\xA8tre : nom invalide '" + name + "'"; return r; }
        for (std::size_t k = 0; k < v->params.size(); ++k)
            if (k != index && same(v->params[k].name, name)) { r.ok = false; r.why = "'" + name + "' existe d\xC3\xA9j\xC3\xA0"; return r; }
        const std::string from = prm.name, viewName = v->name;
        (void)hmi::params::renameParam(p, viewName, from, name);
        return r;
    }
    // 1.11.19 (lot 6) : le type entier, tableau compris (le selecteur de types le donne ainsi).
    if (what == "type_complet") {
        std::string t = trim(value);
        if (upper(t) == "ANY") t.clear();
        prm.type = t;
        return r;
    }
    if (what == "type") {
        if (value == typepicker::kChoose || !typepicker::formOfChoice(value).empty()) {
            r.ok = false;
            r.why = "le s\xC3\xA9lecteur de types n'est pas ouvert ici";
            return r;
        }
        // 1.12.4 : une seule ligne Type - le type entier ; un type de la liste le remplace (REAL apres
        // ARRAY[1..10] OF REAL : un REAL ; Tableau... remet un tableau).
        std::string t = trim(value);
        if (upper(t) == "ANY") t.clear();
        prm.type = t.empty() ? t : hmi::types::normalized(t);
        return r;
    }
    if (what == "tableau") {
        const auto [bounds, element] = splitArray(prm.type);
        (void)bounds;
        std::string b = trim(value);
        if (b.empty()) { prm.type = element; return r; }
        if (b.find("..") == std::string::npos) { r.ok = false; r.why = "les bornes s'\xC3\xA9" "crivent a..b (1..10)"; return r; }
        prm.type = "ARRAY[" + b + "] OF " + (trim(element).empty() ? std::string("ANY") : element);
        return r;
    }
    if (what == "mode") {
        if (hmi::isSymbolView(*v)) { r.ok = false; r.why = "un symbole n'a que le mode R\xC3\xA9" "f\xC3\xA9rence"; return r; }
        prm.mode = hmi::params::paramModeFrom(value);
        return r;
    }
    if (what == "defaut") {
        // 1.12.19 (la regle des cases) : lue comme toute case de valeur (hmi::cellValue) - =formule ; un
        // nom connu (un autre parametre, une variable) ; sinon une valeur fixe du type du parametre :
        // Pompe -> 'Pompe', 2,5 -> 2.5, VRAI -> TRUE, 5s -> T#5s. Une saisie inchangee (la valeur montree
        // sans apostrophes) garde ce qui etait ecrit.
        const std::string typed = trim(value);
        if (typed.empty()) { prm.defaultValue.clear(); return r; }
        if (hmi::isLiteralArgument(prm.defaultValue) && hmi::shownLiteral(prm.defaultValue) == typed) return r;
        const auto cell = hmi::cellValue(&p, prm.type, typed, [v](std::string_view n) { return v->param(n) != nullptr; });
        if (!cell.ok()) { r.ok = false; r.why = cell.why; return r; }
        prm.defaultValue = cell.text;
        return r;
    }
    if (what == "description") { prm.description = value; return r; }
    if (what == "ordre") {
        // 1.11.10 : les instances d'un symbole et les appelants suivent (hmi::params).
        const std::string viewName = v->name;
        if (value == "Supprimer") { (void)hmi::params::removeParam(p, viewName, index); return r; }
        if (value == "Monter" && index > 0) { (void)hmi::params::moveParam(p, viewName, index, index - 1); return r; }
        if (value == "Descendre" && index + 1 < v->params.size()) { (void)hmi::params::moveParam(p, viewName, index, index + 1); return r; }
        r.ok = false;
        r.why = "rien \xC3\xA0 d\xC3\xA9placer";
        return r;
    }
    r.ok = false;
    r.why = "champ inconnu";
    return r;
}

// ------------------------------------------------- l'action Ouvrir une popup -----
std::string withArgument(const hmi::View& popup, const std::string& arguments, const std::string& param, const std::string& text) {
    auto given = hmi::parseArguments(arguments);
    bool found = false;
    for (auto& g : given)
        if (same(g.first, param)) { g.second = trim(text); found = true; }
    if (!found) given.emplace_back(param, trim(text));
    // L'ordre des parametres de la popup, puis les autres (inconnus de la popup).
    std::string out;
    const auto add = [&](const std::string& n, const std::string& t) {
        if (trim(t).empty()) return;
        out += (out.empty() ? "" : "; ") + n + " := " + t;
    };
    for (const auto& prm : popup.params)
        for (const auto& g : given)
            if (same(g.first, prm.name)) add(prm.name, g.second);
    for (const auto& g : given)
        if (!popup.param(g.first)) add(g.first, g.second);
    return out;
}

std::string argumentsSummary(const hmi::Project& p, const hmi::View* caller, const hmi::View& popup, const std::string& arguments,
                             const domain::Project* plcProject) {
    const auto given = hmi::parseArguments(arguments);
    std::size_t byDefault = 0;
    for (const auto& prm : popup.params) {
        const bool has = std::any_of(given.begin(), given.end(), [&](const auto& g) { return same(g.first, prm.name) && !trim(g.second).empty(); });
        if (!has && !trim(prm.defaultValue).empty()) ++byDefault;   // sans defaut : a revoir (checkArguments)
    }
    std::set<std::string> toCheck;
    for (const auto& pb : hmi::params::checkArguments(p, caller, popup, arguments, plcTypesOf(plcProject))) toCheck.insert(upper(pb.param));
    // 1.9 (decision 7) : un argument a la pastille fx rouge est aussi a revoir.
    FxCheck fx{p, plcProject};
    for (const auto& g : given)
        if (!trim(g.second).empty() && popup.param(g.first) && !toCheck.count(upper(g.first))
            && !fx(caller ? *caller : popup, trim(g.second)).empty())
            toCheck.insert(upper(g.first));
    std::string out = std::to_string(popup.params.size());
    if (byDefault) out += " \xC2\xB7 " + std::to_string(byDefault) + " par d\xC3\xA9" "faut";
    if (!toCheck.empty()) out += " \xC2\xB7 " + std::to_string(toCheck.size()) + " \xC3\xA0 revoir";
    return out;
}

PG::Category argumentsCategory(const hmi::Project& p, const hmi::View* caller, const hmi::View& popup, const std::string& arguments,
                               const domain::Project* plcProject,
                               const std::function<bool(const std::string&, const std::string&)>& setArgument) {
    const auto plc = plcTypesOf(plcProject);
    FxCheck fx{p, plcProject};
    PG::Category cat;
    cat.name = "Param\xC3\xA8tres de " + popup.name + "  (" + argumentsSummary(p, caller, popup, arguments, plcProject) + ")";
    const auto given = hmi::parseArguments(arguments);
    const auto problems = hmi::params::checkArguments(p, caller, popup, arguments, plc);
    if (popup.params.empty())
        cat.properties.push_back(prop("Aucun", "cette vue ne d\xC3\xA9" "clare aucun param\xC3\xA8tre", PG::ValueType::ReadOnly));
    for (const auto& prm : popup.params) {
        std::string arg;
        for (const auto& g : given)
            if (same(g.first, prm.name)) arg = trim(g.second);
        std::string help = std::string(hmi::params::paramModeBadge(prm.mode)) + " \xC2\xB7 "
                           + std::string(hmi::params::paramModeHelp(prm.mode));
        if (!prm.description.empty()) help = prm.description + ". " + help;
        // Une erreur (rouge) ; un avertissement (dit dans l'aide, pas en rouge : ca peut marcher).
        std::string error, warning;
        for (const auto& pb : problems)
            if (same(pb.param, prm.name)) {
                std::string& to = pb.error ? error : warning;
                to += (to.empty() ? "" : " ; ") + pb.message;
            }
        // 1.9 (decision 7) : chaque argument est une expression (pastille fx), lue chez
        // l'appelant (ses parametres connus) : rouge si elle ne peut pas marcher.
        if (!arg.empty() && error.empty()) error = fx(caller ? *caller : popup, arg);
        std::string shown = arg;
        if (arg.empty())
            help = (trim(prm.defaultValue).empty() ? std::string("Pas donn\xC3\xA9 et sans valeur par d\xC3\xA9" "faut : donne-le. ")
                                                   : "Pas donn\xC3\xA9 : la popup re\xC3\xA7oit sa valeur par d\xC3\xA9" "faut " + prm.defaultValue + ". ")
                   + help;
        else {
            const std::string t = hmi::params::expressionType(p, caller, arg, plc);
            if (error.empty() && warning.empty() && !t.empty()) help = "\xE2\x9C\x93 " + t + ". " + help;
        }
        if (!warning.empty()) help = warning + ". " + help;
        auto row = prop(prm.name + "  \xC2\xB7  " + shownType(prm.type) + "  \xC2\xB7  " + std::string(hmi::params::paramModeBadge(prm.mode)),
                        shown, PG::ValueType::Text, error.empty() ? help : error + ". " + help, {},
                        [setArgument, name = prm.name](std::string_view s) { return setArgument && setArgument(name, std::string(s)); });
        if (!arg.empty() || !error.empty()) {
            row.expression = arg.empty() ? std::string("?") : arg;
            row.exprError = error;
        }
        row.pill = modePill(prm.mode);   // 1.9 (chantier U) : la pastille de couleur (P3)
        // 1.9 (chantier U) : pas donne - la valeur par defaut, en gris dans la case (P3).
        if (arg.empty() && error.empty() && !trim(prm.defaultValue).empty())
            row.placeholder = hmi::roles::defaultInvite(hmi::shownLiteral(trim(prm.defaultValue)));   // 1.12.19 : sans apostrophes
        // 1.10 (chantier K) : un argument est une expression - X, clic droit, effacer le
        // retirent (la popup recoit alors sa valeur par defaut). Pas donne et en
        // erreur ("?") : rien a retirer.
        if (!arg.empty() || error.empty()) ui::exprfield::markWhole(row, expectOfType(prm.type), false);
        cat.properties.push_back(std::move(row));
    }
    // Les arguments donnes que la popup ne declare pas.
    for (const auto& pb : problems)
        if (pb.kind == hmi::params::ArgumentProblem::Kind::UnknownParam) {
            auto row = prop(pb.param + "  \xC2\xB7  inconnu", "", PG::ValueType::ReadOnly, pb.message);
            for (const auto& g : given)
                if (same(g.first, pb.param)) row.value = g.second;
            row.expression = row.value;
            row.exprError = pb.message;
            cat.properties.push_back(std::move(row));
        }
    return cat;
}

ArgumentField argumentField(std::string_view category, std::string_view property) {
    ArgumentField out;
    constexpr std::string_view head = "Param\xC3\xA8tres de ";
    constexpr std::string_view dot = "  \xC2\xB7  ";
    if (category.substr(0, head.size()) != head) return out;
    const auto rest = category.substr(head.size());
    const auto at = property.find(dot);
    if (at == std::string_view::npos) return out;
    out.popup = trim(rest.substr(0, rest.find("  (")));
    out.param = trim(property.substr(0, at));
    if (out.popup.empty() || out.param.empty()) out = {};
    return out;
}

ui::InputText::Assist argumentAssist(ui::InputText::Assist base, std::function<const hmi::Project*()> project, hmi::Id caller,
                                     std::string popup, std::string param) {
    return [base = std::move(base), project = std::move(project), caller, popup = std::move(popup), param = std::move(param)](
               std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
        if (!base) return;
        const std::size_t first = out.size();
        base(before, from, out);
        const hmi::Project* p = project ? project() : nullptr;
        const hmi::View* pv = p ? p->viewByName(popup) : nullptr;
        const ViewParam* prm = pv ? pv->param(param) : nullptr;
        const std::string declared = prm ? trim(prm->type) : std::string{};
        if (!prm || declared.empty() || out.size() <= first) return;   // ANY : tout convient, l'ordre reste
        const auto plc = plcTypesOf(program().get());
        const hmi::View* cv = caller != hmi::kNoId ? p->view(caller) : nullptr;
        const std::string head(before.substr(0, std::min(from, before.size())));
        // Le bon type d'abord, puis les types inconnus (un debut de chemin), puis ceux qui ne conviennent pas.
        std::vector<ui::InputText::Suggestion> good, unknown, bad;
        for (std::size_t i = first; i < out.size(); ++i) {
            auto s = std::move(out[i]);
            const std::string t = hmi::params::expressionType(*p, cv, head + (s.insert.empty() ? s.text : s.insert), plc);
            if (t.empty()) unknown.push_back(std::move(s));
            else if (hmi::params::typeAcceptsFor(prm->mode, declared, t)) good.push_back(std::move(s));
            else {
                s.detail = std::string(kNotSuitable) + " : " + t + (s.detail.empty() || s.detail == t ? std::string{} : "  \xC2\xB7  " + s.detail);
                bad.push_back(std::move(s));
            }
        }
        out.resize(first);
        for (auto* list : {&good, &unknown, &bad})
            for (auto& s : *list) out.push_back(std::move(s));
    };
}

// ----------------------------------------------- Appliquer copie sur reference -----
std::vector<std::string> applyCopyChoices(const hmi::View& popup) {
    std::vector<std::string> out{std::string(kAllBoth)};
    for (const auto& prm : popup.params)
        if (prm.mode == ParamMode::Both) out.push_back(prm.name);
    return out;
}
std::string applyCopyShown(const std::string& target) {
    const std::string t = trim(target);
    return t.empty() || t == "*" ? std::string(kAllBoth) : t;
}
std::string applyCopyTarget(std::string_view shown) {
    return shown == kAllBoth || trim(shown).empty() ? std::string("*") : trim(shown);
}
std::string applyCopyWrites(const hmi::View& popup, const std::string& target) {
    const std::string t = applyCopyTarget(applyCopyShown(target));
    if (t == "*") {
        std::string names;
        for (const auto& prm : popup.params)
            if (prm.mode == ParamMode::Both) names += (names.empty() ? "" : ", ") + prm.name;
        return names.empty() ? std::string("rien : aucun param\xC3\xA8tre en mode les deux")
                             : "la copie de " + names + " dans la variable de l'appelant";
    }
    const auto* prm = popup.param(t);
    if (!prm) return "rien : " + popup.name + " n'a pas de param\xC3\xA8tre " + t;
    if (prm->mode != ParamMode::Both)
        return "rien : " + t + " est en mode " + std::string(hmi::params::paramModeLabel(prm->mode)) + " (seul Les deux s'applique)";
    return "la copie de " + t + " dans la variable de l'appelant";
}

// ----------------------------------------------------------- l'aide a la saisie -----
namespace {
hmi::Id& assistViewId() {
    static hmi::Id id = hmi::kNoId;
    return id;
}
} // namespace
void setAssistView(hmi::Id view) { assistViewId() = view; }
const hmi::View* editedView(const hmi::Project& p) {
    return assistViewId() != hmi::kNoId ? p.view(assistViewId()) : nullptr;
}

const hmi::View* assistView(const hmi::Project& p) {
    const auto* v = assistViewId() != hmi::kNoId ? p.view(assistViewId()) : nullptr;
    return v && !v->params.empty() ? v : nullptr;
}

std::vector<std::pair<std::string, std::string>> assistItems(const hmi::Project& p, const hmi::View& v, std::string_view typed,
                                                             const domain::Project* plcProject) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& s : hmi::params::paramSuggestions(p, v, typed, plcTypesOf(plcProject)))
        out.emplace_back(s.insert, (s.type.empty() ? std::string("ANY") : s.type) + "  \xC2\xB7  " + s.detail);
    return out;
}

// ---------------------------------------------------------- en marche : le repere -----
bool copyModified(const hmi::Runtime& rt, hmi::Id view, std::string_view path) {
    std::size_t r = 0;
    while (r < path.size() && (std::isalnum(static_cast<unsigned char>(path[r])) != 0 || path[r] == '_')) ++r;
    if (r == 0) return false;
    const auto* c = rt.paramCopy(view, path.substr(0, r));
    if (!c) return false;
    const std::string key = upper(trim(path.substr(r)));
    for (const auto& [k, value] : c->values) {
        // le membre lui-meme, ou un membre de ce qu'il designe (une structure entiere)
        if (!(k == key || (k.size() > key.size() && k.compare(0, key.size(), key) == 0 && (k[key.size()] == '.' || k[key.size()] == '['))))
            continue;
        const auto cap = c->captured.find(k);
        if (cap == c->captured.end() || !cap->second.equals(value)) return true;
    }
    // 1.12.4 : un objet (le parametre entier, un membre objet) change depuis l'ouverture.
    for (const auto& [k, obj] : c->objects) {
        if (!(k == key || (k.size() > key.size() && k.compare(0, key.size(), key) == 0 && (k[key.size()] == '.' || k[key.size()] == '['))))
            continue;
        const auto cap = c->capturedObjects.find(k);
        if (obj && (cap == c->capturedObjects.end() || !cap->second || !sim::deepEquals(*cap->second, *obj))) return true;
    }
    return false;
}

void paintCopyMark(gfx::IRenderer& r, const gfx::Rect& field, float zoom, float alpha) {
    const auto a = static_cast<std::uint8_t>(std::clamp(alpha, 0.f, 1.f) * 255.f);
    const gfx::Color yellow{0xD7, 0xA8, 0x24, a};
    r.fillRect({field.x, field.y, std::max(2.f, 3.f * zoom), field.h}, yellow);
    const auto px = static_cast<std::uint16_t>(std::clamp(10.f * zoom, 8.f, 18.f));
    const std::string text = "\xE2\x9A\xA0 " + std::string(kModifiedMark);
    const auto m = r.measure(text, gfx::FontId{px});
    const float pad = 4.f, bw = m.width + 2 * pad, bh = static_cast<float>(px) + 4.f;
    const float bx = std::max(field.x, field.x + field.w - bw - 4.f), by = field.y - bh * 0.6f;
    r.fillRoundedRect({bx, by, bw, bh}, gfx::Color{0x2A, 0x24, 0x10, a}, 3.f);
    r.strokeRect({bx, by, bw, bh}, yellow, 1.f);
    r.drawText({bx + pad, by + 2.f}, text, gfx::FontId{px}, gfx::Color{0xE9, 0xC9, 0x5A, a});
}

// ------------------------------------------------- l'onglet Popups de la simulation -----
ui::ColorPill modePill(ParamMode m) {
    ui::ColorPill pill;
    pill.text = std::string(hmi::params::paramModeBadge(m));
    switch (m) {
        case ParamMode::Reference:
            pill.ink = gfx::Color::rgb(0x6CB6EC);
            pill.border = gfx::Color::rgb(0x2A7FBF);
            pill.fill = gfx::Color::rgb(0x2A7FBF).withAlpha(36);   // 14 %
            break;
        case ParamMode::Copy:
            pill.ink = gfx::Color::rgb(0x4FC4B8);
            pill.border = gfx::Color::rgb(0x2AA198);
            pill.fill = gfx::Color::rgb(0x2AA198).withAlpha(33);   // 13 %
            break;
        case ParamMode::Both:
            pill.ink = gfx::Color::rgb(0xEFFAF8);
            pill.border = gfx::Color::rgb(0x1F6FA8);
            pill.fill = gfx::Color::rgb(0x1F6FA8);
            pill.fillTo = gfx::Color::rgb(0x1D8A80);
            break;
    }
    return pill;
}

std::vector<std::vector<std::string>> popupRows(hmi::Runtime& rt, const hmi::Project& p) {
    std::vector<std::vector<std::string>> rows;
    for (const auto& slot : rt.popupSlots()) {
        const auto* v = p.view(slot.view);
        if (!v) continue;
        // La popup, et l'objet qui l'a ouverte quand il est connu (P6 : "ouverte par Pompe_3").
        std::string title = v->name;
        if (slot.opener != hmi::kNoId)
            for (const auto& ov : p.views)
                if (const auto* o = ov.object(slot.opener)) {
                    title += " \xC2\xB7 ouverte par " + (o->name.empty() ? std::string("un objet") : o->name);
                    break;
                }
        bool first = true;
        const auto given = hmi::parseArguments(slot.arguments);
        for (const auto& prm : v->params) {
            std::vector<std::string> row{first ? title : std::string{}, prm.name, shownType(prm.type),
                                         std::string(hmi::params::paramModeBadge(prm.mode)), {}, {}};
            first = false;
            std::string arg;
            for (const auto& g : given)
                if (same(g.first, prm.name)) arg = trim(g.second);
            const auto* c = rt.paramCopy(slot.view, prm.name);
            if (!c) {
                row[4] = arg.empty() ? "'" + prm.defaultValue + "' (par d\xC3\xA9" "faut)" : arg;
                row[5] = prm.mode == ParamMode::Reference ? std::string("en direct") : std::string("pas de copie");
                rows.push_back(std::move(row));
                continue;
            }
            std::string copy, ref;
            std::size_t changed = 0;
            for (const auto& [key, value] : c->values) {
                const auto cap = c->captured.find(key);
                const bool modified = cap == c->captured.end() || !cap->second.equals(value);
                if (!modified) continue;
                ++changed;
                const auto path = c->paths.find(key);
                const std::string rel = path == c->paths.end() ? key : path->second;
                copy += (copy.empty() ? "" : " ; ") + (rel.empty() ? std::string("copie") : "copie" + rel) + " = " + value.display();
                if (!c->source.empty()) {
                    sim::Value now;
                    if (rt.environment().read(c->source + rel, now))
                        ref += (ref.empty() ? "" : " ; ") + c->source + rel + " = " + now.display() + " (pas encore \xC3\xA9" "crit)";
                }
            }
            // 1.12.4 : les objets (une liste, une MAP...) - leur valeur ecrite, s'ils ont change.
            for (const auto& [key, obj] : c->objects) {
                if (!obj) continue;
                const auto cap = c->capturedObjects.find(key);
                if (cap != c->capturedObjects.end() && cap->second && sim::deepEquals(*cap->second, *obj)) continue;
                ++changed;
                const auto path = c->paths.find(key);
                const std::string rel = path == c->paths.end() ? key : path->second;
                std::string text;
                if (!sim::literalText(*obj, text)) text = sim::display(*obj);
                copy += (copy.empty() ? "" : " ; ") + (rel.empty() ? std::string("copie") : "copie" + rel) + " = " + text;
                if (!c->source.empty()) ref += (ref.empty() ? "" : " ; ") + c->source + rel + " (pas encore \xC3\xA9" "crit)";
            }
            if (!changed) {
                const auto whole = c->values.find(std::string{});
                const auto wholeObj = c->objects.find(std::string{});
                std::string objText;
                if (wholeObj != c->objects.end() && wholeObj->second && !sim::literalText(*wholeObj->second, objText))
                    objText = sim::display(*wholeObj->second);
                copy = whole != c->values.end() ? whole->second.display()
                     : !objText.empty()          ? objText
                                                 : "captur\xC3\xA9" "e \xC3\xA0 l'ouverture";
                ref = c->source.empty() ? std::string("une valeur") : c->source;
            }
            row[4] = copy;
            row[5] = ref;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::size_t modifiedCopies(const hmi::Runtime& rt) {
    std::size_t n = 0;
    for (const auto& slot : rt.popupSlots())
        for (const auto& c : slot.copies) {
            bool changed = false;
            for (const auto& [key, value] : c.values) {
                const auto cap = c.captured.find(key);
                if (cap == c.captured.end() || !cap->second.equals(value)) { changed = true; break; }
            }
            for (const auto& [key, obj] : c.objects) {          // 1.12.4 : un objet change aussi
                if (changed || !obj) break;
                const auto cap = c.capturedObjects.find(key);
                changed = cap == c.capturedObjects.end() || !cap->second || !sim::deepEquals(*cap->second, *obj);
            }
            if (changed) ++n;
        }
    return n;
}

std::string popupsSummary(const hmi::Runtime& rt) {
    const std::size_t open = rt.popupSlots().size();
    if (!open) return {};
    std::string out = std::to_string(open) + (open > 1 ? " popups ouvertes" : " popup ouverte");
    if (const std::size_t n = modifiedCopies(rt))
        out += " \xC2\xB7 " + std::to_string(n)
               + (n > 1 ? " copies modifi\xC3\xA9" "es, pas encore appliqu\xC3\xA9" "es" : " copie modifi\xC3\xA9" "e, pas encore appliqu\xC3\xA9" "e");
    return out;
}

namespace {
const std::vector<std::string>& popupHeaders() {
    static const std::vector<std::string> h{"Popup", "Param\xC3\xA8tre", "Type", "Mode", "Valeur (copie)", "R\xC3\xA9" "f\xC3\xA9rence"};
    return h;
}
std::map<const ui::Widget*, std::string>& shownSignatures() {
    static std::map<const ui::Widget*, std::string> m;
    return m;
}
} // namespace

std::unique_ptr<ui::Widget> makePopupsTab(const std::string& id) {
    auto table = std::make_unique<ui::TableView>(id);
    table->setColumns({{popupHeaders()[0], 130.f}, {popupHeaders()[1], 120.f}, {popupHeaders()[2], 90.f}, {popupHeaders()[3], 80.f},
                       {popupHeaders()[4], 220.f}, {popupHeaders()[5], 280.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    return table;
}

void updatePopupsTab(ui::TabControl& tabs, hmi::Runtime& rt, const hmi::Project& p) {
    ui::TableView* table = nullptr;
    std::size_t index = 0;
    for (std::size_t i = 0; i < tabs.tabCount() && !table; ++i) {
        auto* page = tabs.page(i);
        const std::string& id = page ? page->id() : std::string{};
        if (id.size() >= 7 && id.compare(id.size() - 7, 7, ".popups") == 0) {
            table = dynamic_cast<ui::TableView*>(page);
            index = i;
        }
    }
    if (!table) return;
    auto rows = popupRows(rt, p);
    std::string sig;
    for (const auto& r : rows)
        for (const auto& cell : r) sig += cell + '\x1F';
    const auto [it, fresh] = shownSignatures().try_emplace(table, std::string{});
    if (!fresh && it->second == sig) return;   // rien n'a change : ni modele ni badge refaits
    it->second = sig;
    const std::size_t open = rt.popupSlots().size();
    const std::size_t modified = modifiedCopies(rt);
    // La ligne de pied (P6) : "1 popup ouverte", puis "1 copie modifiee" / "pas encore appliquee".
    const std::size_t foot = open ? rows.size() : static_cast<std::size_t>(-1);
    if (open) {
        const std::string sum = popupsSummary(rt);
        const auto dot = sum.find(" \xC2\xB7 ");
        const std::string tail = dot == std::string::npos ? std::string{} : sum.substr(dot + 4);
        const auto comma = tail.find(", ");
        rows.push_back({dot == std::string::npos ? sum : sum.substr(0, dot), {}, {}, {},
                        comma == std::string::npos ? tail : tail.substr(0, comma + 1),
                        comma == std::string::npos ? std::string{} : tail.substr(comma + 2)});
    }
    // 1.9 (chantier U) : la colonne du mode montre la pastille de couleur (P6).
    std::vector<int> modes(rows.size(), -1);
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (i != foot && rows[i].size() > 3)
            for (const auto m : hmi::params::kParamModes)
                if (rows[i][3] == hmi::params::paramModeBadge(m)) {
                    modes[i] = static_cast<int>(m);
                    rows[i][3].clear();
                }
    auto model = std::make_shared<hmikit::Rows>(popupHeaders(), std::move(rows), [foot, modified, modes](ui::RowIndex r, std::size_t c) {
        ui::CellStyle st;
        if (static_cast<std::size_t>(r) == foot) {
            st.fgTone = modified ? ui::Tone::Warning : ui::Tone::Muted;
            return st;
        }
        if (c == 3) st.fgTone = ui::Tone::Accent;
        if (c == 3 && r < modes.size() && modes[r] >= 0) st.colorLead = modePill(static_cast<ParamMode>(modes[r]));
        if (c == 5) st.fgTone = ui::Tone::Muted;
        return st;
    });
    table->setModel(model);
    tabs.setTabBadge(index, open ? std::to_string(open) : std::string{}, modified ? ui::Tone::Warning : ui::Tone::Accent);
}

} // namespace app::hmiparams
