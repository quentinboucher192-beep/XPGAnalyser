#include "HmiAssist.hpp"
#include "../../hmi/HmiNatives.hpp"   // 1.12.0 : les natives de l'IHM (completion, signature, survol)
#include "../../core/Edition.hpp"     // 1.12.0 : XPGAnalyser IHM - ni API, ni bibliotheque de l'automate
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : l'aide a la saisie selon le type du champ
#include "HmiParamPanes.hpp"   // 1.9 : les parametres de la popup (type, mode)
#include "../../hmi/HmiTypes.hpp"

#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiApiVars.hpp"          // 1.11.1 (API-M, API-V) : les variables de l'automate sous API.
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiLog.hpp"              // 1.11.14 : IHM_LOG et ses niveaux
#include "../../hmi/HmiEnums.hpp"            // 1.10 (chantier K2) : les enumerations (E)
#include "../../hmi/HmiOperators.hpp"        // 1.10 (chantier K2) : les operateurs (S2)
#include "../../hmi/HmiScriptCheck110.hpp"   // 1.10 (chantier K2) : le langage des scripts (S1)
#include "../../hmi/HmiSymbols.hpp"           // 1.11.10 : les fonctions des symboles
#include "../../project/BlockLibrary.hpp"
#include "../../project/EditCommands.hpp"
#include "../../ui/widgets/ColorPalette.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : les types, un seul catalogue

#include <algorithm>
#include <set>
#include <cctype>
#include <chrono>
#include <optional>

namespace app::assist {

namespace {

// 1.11.1 (API-V) : le modele des variables de l'automate sous API. (API-M),
// pour l'aide a la saisie - nul sans programme installe (installProgram) ou
// si `plc` n'est pas lui. Defini avec les grilles, pres de g_plc.
const hmi::apivars::Model* apiModel(const hmi::Project& hp, const domain::Project* plc);
// Le projet IHM a-t-il lui-meme une variable ou une vue nommee API ? Elle
// garde son sens (un projet d'avant la 1.11.1 se lit a l'identique).
bool hmiOwnsApi(const hmi::Project& hp) {
    const auto api = [](std::string_view n) {
        return n.size() == 3 && std::toupper(static_cast<unsigned char>(n[0])) == 'A' && std::toupper(static_cast<unsigned char>(n[1])) == 'P'
            && std::toupper(static_cast<unsigned char>(n[2])) == 'I';
    };
    return std::any_of(hp.programs.variables.begin(), hp.programs.variables.end(), [&](const auto& v) { return api(v.name); })
        || std::any_of(hp.views.begin(), hp.views.end(), [&](const auto& v) { return api(v.name); });
}

bool identChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// 0 : le nom commence par ce qui est tape ; 1 : le contient ; -1 : non.
int matchRank(std::string_view text, const std::string& needleLower) {
    if (needleLower.empty()) return 0;
    const auto at = lower(text).find(needleLower);
    if (at == std::string::npos) return -1;
    return at == 0 ? 0 : 1;
}

// Le nom appele devant la parenthese ouvrante `open`.
std::string calleeAt(std::string_view text, std::size_t open) {
    std::size_t end = open;
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    std::size_t begin = end;
    while (begin > 0 && identChar(text[begin - 1])) --begin;
    return upper(text.substr(begin, end - begin));
}

// Le chemin qui finit a `end` (exclu) : a, a.b, a[i + 1].b ; vide sinon.
std::size_t pathStart(std::string_view text, std::size_t end) {
    std::size_t i = end;
    int depth = 0;
    while (i > 0) {
        const char c = text[i - 1];
        if (depth > 0) {
            if (c == ']') ++depth;
            else if (c == '[') --depth;
            --i;
            continue;
        }
        if (c == ']') { ++depth; --i; continue; }
        if (identChar(c) || c == '.' || c == '^') { --i; continue; }   // 1.10 (K2) : p^.Membre
        break;
    }
    return i;
}

// 1.10.2 (chantier A, demande de D) : le mot en cours suit-il un repere de
// Dupliquer ($Vanne$.F0, $Vanne$[1].F0, $Vanne$.) ou est-il dedans ($Van) ?
// Un '$' colle au chemin qui finit au curseur, ou dans le mot lui-meme.
bool afterMarker(std::string_view before, std::string_view prefix) {
    if (prefix.find('$') != std::string_view::npos) return true;
    const std::size_t start = pathStart(before, before.size());
    return start > 0 && before[start - 1] == '$';
}

// "Armoires[0].ana" -> {"Armoires", indexe}, {"ana", non}
struct Segment { std::string name; bool indexed{false}; };
std::vector<Segment> segmentsOf(std::string_view path) {
    std::vector<Segment> out;
    Segment cur;
    int depth = 0;
    for (const char c : path) {
        if (depth > 0) {
            if (c == '[') ++depth;
            else if (c == ']') --depth;
            continue;
        }
        if (c == '[') { ++depth; cur.indexed = true; continue; }
        if (c == '.') { out.push_back(cur); cur = {}; continue; }
        if (c != ' ' && c != '\t') cur.name += c;
    }
    out.push_back(cur);
    return out;
}

// Un tableau : sa classe le dit, ou son nom ("ARRAY[0..1] OF armoire") quand
// la classe a ete remplacee (un tableau de DFB devient FunctionBlock). Le type
// element (elementType) est rempli aussi pour une DDT simple : il ne suffit pas.
bool isArray(const domain::Project& p, const domain::TypeRef& t) {
    if (t.klass == domain::TypeClass::Array) return true;
    const auto n = p.strings.text(t.name);
    return n.size() >= 5 && iequals(n.substr(0, 5), "ARRAY");
}

const domain::Variable* globalVariable(const domain::Project& p, std::string_view name) {
    for (const auto& v : p.variables)
        if ((v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant)
            && iequals(p.strings.text(v.name), name))
            return &v;
    return nullptr;
}

// Les membres d'un type : ceux d'une DDT, les parametres d'un DFB, ou ceux
// d'un bloc de la bibliotheque (TON : IN, PT, Q, ET).
struct Field { std::string name, type, comment; const domain::Variable* var{nullptr}; };
std::vector<Field> fieldsOf(const domain::Project& p, const domain::TypeRef& t) {
    std::vector<Field> out;
    auto add = [&](domain::Index vi) {
        if (vi >= p.variables.size()) return;
        const auto& v = p.variables[vi];
        out.push_back({std::string(p.strings.text(v.name)), std::string(p.strings.text(v.type.name)),
                       std::string(p.strings.text(v.comment)), &v});
    };
    if (t.derivedIndex < p.derivedTypes.size()) {
        for (const auto vi : p.derivedTypes[t.derivedIndex].fields) add(vi);
        return out;
    }
    if (t.fbTypeIndex < p.pous.size()) {
        const auto& pou = p.pous[t.fbTypeIndex];
        for (const auto vi : pou.parameters) add(vi);
        for (const auto vi : pou.locals)
            if (vi < p.variables.size() && p.variables[vi].scope == domain::VariableScope::Public) add(vi);
        return out;
    }
    const auto typeName = p.strings.text(t.elementType ? t.elementType : t.name);
    if (const auto* b = project::BlockLibrary::shared().find(typeName); b && b->kind == project::BlockKind::FunctionBlock)
        for (const auto& prm : b->parameters)
            out.push_back({prm.name, prm.type, std::string(prm.directionLabel()), nullptr});
    return out;
}

// Le type au bout d'un chemin ; faux si le chemin ne se resout pas.
bool resolve(const domain::Project& p, std::string_view path, domain::TypeRef& type, bool& pendingArray,
             Field* last = nullptr) {
    const auto segs = segmentsOf(path);
    if (segs.empty() || segs.front().name.empty()) return false;
    const auto* root = globalVariable(p, segs.front().name);
    if (!root) return false;
    type = root->type;
    pendingArray = isArray(p, type) && !segs.front().indexed;
    if (last) *last = {std::string(p.strings.text(root->name)), std::string(p.strings.text(root->type.name)),
                       std::string(p.strings.text(root->comment)), root};
    for (std::size_t i = 1; i < segs.size(); ++i) {
        if (pendingArray) return false;               // tab.x : il manque l'indice
        const auto fields = fieldsOf(p, type);
        const auto it = std::find_if(fields.begin(), fields.end(),
                                     [&](const Field& f) { return iequals(f.name, segs[i].name); });
        if (it == fields.end()) return false;
        if (last) *last = *it;
        if (it->var) type = it->var->type;
        else {
            type = domain::TypeRef{};                  // un parametre de bloc de bibliotheque : elementaire
        }
        pendingArray = it->var && isArray(p, type) && !segs[i].indexed;
    }
    return true;
}

// Les fonctions IHM_, dans l'ordre ou on les cherche.
std::vector<Function> makeFunctions() {
    return {
        {"IHM_NAVIGUER", {"vue : STRING", "transition : STRING (facultatif)", "param\xC3\xA8tres : STRING (facultatif)"}, "BOOL",
         "affiche une autre vue (transition : Fondu, Glissement, Zoom, Rotation... ; param\xC3\xA8tres : 'Moteur := Pompes[3]')",
         "IHM_NAVIGUER('|')", true},
        {"IHM_POPUP", {"vue : STRING", "param\xC3\xA8tres : STRING (facultatif)", "position : STRING (facultatif)"}, "BOOL",
         "ouvre une popup, avec ses param\xC3\xA8tres ('Moteur := Pompes[3]') et sa place (centre, objet, haut-droite, "
         "derniere, x,y...) ; d\xC3\xA9j\xC3\xA0 ouverte, elle revient devant", "IHM_POPUP('|')", true},
        {"IHM_CHANGER_POPUP", {"vue : STRING", "param\xC3\xA8tres : STRING (facultatif)"}, "BOOL",
         "remplace la popup du dessus par une autre (ou la m\xC3\xAAme, d'autres param\xC3\xA8tres), au m\xC3\xAAme endroit",
         "IHM_CHANGER_POPUP('|')", true},
        {"IHM_POPUP_PRECEDENTE", {}, "BOOL", "revient \xC3\xA0 la popup d'avant le changement, \xC3\xA0 sa place",
         "IHM_POPUP_PRECEDENTE()|", false},
        {"IHM_CENTRER_POPUP", {"vue : STRING (facultatif)"}, "BOOL", "ram\xC3\xA8ne une popup (celle du dessus) au milieu de la vue",
         "IHM_CENTRER_POPUP()|", false},
        {"IHM_FERMER_POPUP", {}, "BOOL", "ferme la popup du dessus", "IHM_FERMER_POPUP()|", false},
        {"IHM_FERMER_POPUPS", {}, "BOOL", "ferme toutes les popups", "IHM_FERMER_POPUPS()|", false},
        {"IHM_POPUP_OUVERTE", {"vue : STRING"}, "BOOL", "vrai si cette popup est ouverte", "IHM_POPUP_OUVERTE('|')", true},
        {"IHM_JOURNAL", {"message : STRING"}, "BOOL",
         "\xC3\xA9" "crit une ligne dans le journal ; {Variable} ou {Variable:0.0} y met sa valeur", "IHM_JOURNAL('|')", false},
        // 1.11.14 : la Console (le panneau du bas)
        {"IHM_LOG", {"niveau : NIVEAU_LOG (TRACE, DEBUG, INFO, SUCCESS, WARNING, ERROR, CRITICAL)", "message : STRING"}, "BOOL",
         "\xC3\xA9" "crit une ligne dans la Console, avec l'heure, le niveau, la source et la ligne du code ; "
         "{Variable} ou {Variable:0.0} y met sa valeur", "IHM_LOG(|", true},
        {"IHM_APPELER", {"script : STRING"}, "BOOL", "ex\xC3\xA9" "cute tout de suite un script g\xC3\xA9n\xC3\xA9ral",
         "IHM_APPELER('|')", true},
        {"IHM_SON", {"son : STRING"}, "BOOL", "joue un son des ressources (WAV, MP3, OGG)", "IHM_SON('|')", true},
        {"IHM_VUE", {}, "STRING", "le nom de la vue affich\xC3\xA9" "e", "IHM_VUE()|", false},
        {"IHM_TEMPS", {}, "TIME", "le temps \xC3\xA9" "coul\xC3\xA9 depuis le lancement de l'IHM", "IHM_TEMPS()|", false},
        {"IHM_UTILISATEUR", {}, "STRING", "l'identifiant de l'utilisateur connect\xC3\xA9 ('' sans)", "IHM_UTILISATEUR()|", false},
        {"IHM_NOM_UTILISATEUR", {}, "STRING", "le nom de l'utilisateur connect\xC3\xA9", "IHM_NOM_UTILISATEUR()|", false},
        {"IHM_GROUPE", {}, "STRING", "le groupe de l'utilisateur connect\xC3\xA9", "IHM_GROUPE()|", false},
        {"IHM_NIVEAU", {}, "INT", "le niveau d'acc\xC3\xA8s de l'utilisateur connect\xC3\xA9 (0 sans)", "IHM_NIVEAU()|", false},
        {"IHM_DECONNECTER", {}, "BOOL", "d\xC3\xA9" "connecte l'utilisateur", "IHM_DECONNECTER()|", false},
        {"IHM_PARAMETRES_SYSTEME", {"onglet : STRING (facultatif)"}, "BOOL",
         "ouvre le menu natif Param\xC3\xA8tres syst\xC3\xA8me ('Diagnostic' : sur cet onglet ; sinon R\xC3\xA9glages)",
         "IHM_PARAMETRES_SYSTEME()|", false},
        // lot 11
        {"IHM_METTRE_DE_COTE", {"alarme : STRING", "minutes : REAL (facultatif, 0 : sans limite)", "raison : STRING (facultatif)"}, "INT",
         "met une alarme de c\xC3\xB4t\xC3\xA9 (ou 'groupe:Zone') : elle n'appara\xC3\xAEt plus jusqu'\xC3\xA0 la fin du d\xC3\xA9lai ; "
         "rend le nombre mis de c\xC3\xB4t\xC3\xA9", "IHM_METTRE_DE_COTE('|')", false},
        {"IHM_REMETTRE", {"alarme : STRING"}, "INT", "remet en service une alarme mise de c\xC3\xB4t\xC3\xA9 ('*' : toutes)",
         "IHM_REMETTRE('|')", false},
        {"IHM_FAIRE_TAIRE", {}, "BOOL", "coupe le son des alarmes jusqu'\xC3\xA0 la prochaine apparition", "IHM_FAIRE_TAIRE()|", false},
        // Lot API 8 : le 3e argument - lance par un clic, ou l'enregistrer se demande (FALSE : jamais).
        {"IHM_EXPORTER", {"source : STRING", "fichier : STRING (facultatif)", "demander : BOOL (facultatif, TRUE)"}, "BOOL",
         "exporte des donn\xC3\xA9" "es (alarmes, historique, \xC3\xA9v\xC3\xA9nements, syst\xC3\xA8me, mesures, recette:Nom, objet:Nom) "
         "dans exports/ du projet ; l'extension (.csv, .xlsx, .pdf) choisit le format ; lanc\xC3\xA9 par un clic, il demande o\xC3\xB9 "
         "l'enregistrer (FALSE en 3e argument : jamais) ; un script qui tourne seul ne demande jamais", "IHM_EXPORTER('|')", false},
        // lot 12 : l'historique de navigation, la vue d'accueil
        {"IHM_PRECEDENTE", {}, "BOOL", "revient \xC3\xA0 la vue d'avant (l'historique de navigation) ; vrai si la vue a chang\xC3\xA9",
         "IHM_PRECEDENTE()|", false},
        {"IHM_SUIVANTE", {}, "BOOL", "repart vers la vue quitt\xC3\xA9" "e par Pr\xC3\xA9" "c\xC3\xA9" "dent", "IHM_SUIVANTE()|", false},
        {"IHM_ACCUEIL", {}, "BOOL", "ouvre la vue d'accueil : celle du groupe de l'utilisateur connect\xC3\xA9, sinon la vue de d\xC3\xA9marrage",
         "IHM_ACCUEIL()|", false},
        // lot 12 : le menu natif de connexion
        {"IHM_MENU_CONNEXION", {"onglet : STRING (facultatif)"}, "BOOL",
         "ouvre le menu natif de connexion ('Comptes', 'Acc\xC3\xA8s', 'Journal', 'Mon compte' : cet onglet, s'il se voit) ; "
         "vrai si l'onglet demand\xC3\xA9 est montr\xC3\xA9",
         "IHM_MENU_CONNEXION()|", false},
        // lot 13 : la langue
        {"IHM_LANGUE", {"langue : STRING"}, "BOOL",
         "change la langue de l'IHM : un code ('en', 'de'), un nom ('English') ou 'suivante' ; vrai si elle est connue "
         "(Configuration > Langues). La langue en cours : SYS.Language",
         "IHM_LANGUE('|')", false},
        {"IHM_THEME", {"theme : STRING (facultatif)"}, "BOOL",
         "change le th\xC3\xA8me de l'IHM : 'jour' (clair), 'nuit' (les couleurs de la conception) ; sans argument, l'autre. "
         "Le th\xC3\xA8me en cours : SYS.Theme",
         "IHM_THEME('|')", false},
        // lot 16 : le GIF anime
        {"IHM_GIF_JOUER", {"objet : STRING", "tours : INT (facultatif)"}, "BOOL",
         "joue un GIF anim\xC3\xA9 de la vue (ou d'une popup ouverte) ; en pause, il reprend l\xC3\xA0 o\xC3\xB9 il s'\xC3\xA9tait arr\xC3\xAAt\xC3\xA9. "
         "Avec un nombre de tours : il repart du d\xC3\xA9" "but pour ces tours (0 : sans fin). Vrai si le GIF existe",
         "IHM_GIF_JOUER('|')", false},
        {"IHM_GIF_PAUSE", {"objet : STRING"}, "BOOL", "fige un GIF anim\xC3\xA9 sur l'image montr\xC3\xA9" "e (IHM_GIF_JOUER le reprend)",
         "IHM_GIF_PAUSE('|')", false},
        {"IHM_GIF_ARRETER", {"objet : STRING"}, "BOOL", "ram\xC3\xA8ne un GIF anim\xC3\xA9 \xC3\xA0 sa premi\xC3\xA8re image ; il attend",
         "IHM_GIF_ARRETER('|')", false},
        {"IHM_GIF_REJOUER", {"objet : STRING", "tours : INT"}, "BOOL",
         "rejoue un GIF anim\xC3\xA9 depuis le d\xC3\xA9" "but, pour ce nombre de tours (0 : sans fin). Ses membres : Vue.Objet.Playing, .Frame, "
         ".FrameCount, .Loops",
         "IHM_GIF_REJOUER('|', 3)", false},
        // lot 15 : les equipements du reseau
        {"IHM_EQUIPEMENT_OK", {"\xC3\xA9quipement : STRING"}, "BOOL",
         "vrai si l'\xC3\xA9quipement (Configuration \xE2\x80\xBA \xC3\x89quipements) est actif et r\xC3\xA9pond",
         "IHM_EQUIPEMENT_OK('|')", true},
        {"IHM_EQUIPEMENT_PING", {"\xC3\xA9quipement : STRING"}, "REAL", "le dernier ping de l'\xC3\xA9quipement, en ms (-1 : pas de r\xC3\xA9ponse)",
         "IHM_EQUIPEMENT_PING('|')", true},
        // 1.9 : les esclaves simules
        {"IHM_ESCLAVE_SIMULE", {"\xC3\xA9quipement : STRING"}, "BOOL",
         "vrai si l'IHM lit en ce moment cet \xC3\xA9quipement sur son esclave simul\xC3\xA9 (ou s'il n'est que simul\xC3\xA9). "
         "Sa structure : SYS.Slave.<nom>.Read, .Fallback, .RealOnline",
         "IHM_ESCLAVE_SIMULE('|')", true},
    };
}

// Les mots du langage : une infobulle n'a rien a en dire.
bool isKeyword(std::string_view w) {
    static const char* kWords[] = {
        "IF", "THEN", "ELSIF", "ELSE", "END_IF", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE", "END_WHILE", "REPEAT",
        "UNTIL", "END_REPEAT", "CASE", "OF", "END_CASE", "EXIT", "RETURN", "AND", "OR", "XOR", "NOT", "MOD", "TRUE",
        "FALSE", "VAR", "VAR_TEMP", "END_VAR", "BOOL", "INT", "DINT", "UINT", "UDINT", "REAL", "LREAL", "WORD", "DWORD",
        "BYTE", "TIME", "STRING", "EBOOL",
        // 1.10 (chantier K) : le dialecte des scripts de l'IHM
        "FUNCTION", "END_FUNCTION", "MAP", "ARRAY", "REF_TO", "REFERENCE", "POINTER", "NULL", "VAR_INPUT",
        "EACH", "IN", "VAR_IN_OUT", "VAR_OUTPUT", "MAP_ITERATOR",
        // 1.12.1
        "CONTINUE", "TRY", "CATCH", "END_TRY", "ENTRE", "ET"};
    const auto u = upper(w);
    for (const char* k : kWords) if (u == k) return true;
    return false;
}

std::string hmiVariableDetail(const hmi::Variable& v) {
    return v.type + "  \xC2\xB7  IHM" + (v.folder.empty() ? std::string{} : "  \xC2\xB7  " + v.folder)
         + (v.description.empty() ? std::string{} : "  \xC2\xB7  " + v.description);
}

// Lot 16 : apres le point d'une structure ou d'un tableau IHM - les membres, les
// proprietes publiques (avec leur valeur). Faux : le chemin n'en est pas un.
bool hmiCompositeMembers(const hmi::Project& hp, const std::string& path, const std::string& needle,
                         const std::function<void(Item)>& push) {
    namespace ty = hmi::types;
    const std::string type = ty::typeOfPath(hp, path);
    if (type.empty()) return false;
    ty::Spec spec;
    if (!ty::parseSpec(type, spec) || (!spec.array() && ty::isElementary(spec.element))) return true;   // une case simple : rien
    const auto flat = ty::flatten(hp, "x", type, {}, true);
    const ty::Aggregate* agg = flat.ok() && !flat.aggregates.empty() ? &flat.aggregates.back() : nullptr;
    int order = 0;
    if (!spec.array())
        for (const auto& m : ty::membersOf(hp, type)) {
            const int r = matchRank(m.name, needle);
            if (r < 0) continue;
            Item it;
            it.text = m.name;
            it.detail = ty::normalized(m.type) + (m.description.empty() ? std::string{} : "  \xC2\xB7  " + m.description);
            it.kind = Kind::Member;
            it.rank = r * 100 + order++;
            push(std::move(it));
        }
    for (const auto& pi : ty::properties()) {
        if (pi.arrayOnly && !spec.array()) continue;
        if (pi.twoD && spec.dims != 2) continue;
        const int r = matchRank(std::string(pi.name), needle);
        if (r < 0) continue;
        std::string value;
        sim::Value v;
        if (agg && ty::propertyValue(*agg, pi.name, v)) value = " (" + v.display() + ")";
        Item it;
        it.text = std::string(pi.name);
        it.detail = std::string(pi.type) + "  \xC2\xB7  " + std::string(pi.help) + value;
        it.kind = Kind::SysVariable;
        it.rank = r * 100 + 50 + order++;
        push(std::move(it));
    }
    return true;
}

// Lot 7 : "INT  .  locale (VAR, gardee)", "REAL  .  parametre".
std::string localSection(const hmi::LocalVar& l) {
    switch (l.section) {
        case hmi::LocalVar::Section::Var:   return "locale (VAR, gard\xC3\xA9" "e)";
        case hmi::LocalVar::Section::Temp:  return "locale (VAR_TEMP)";
        case hmi::LocalVar::Section::Input: return "param\xC3\xA8tre (VAR_INPUT)";
        case hmi::LocalVar::Section::InOut: return "param\xC3\xA8tre (VAR_IN_OUT, par r\xC3\xA9" "f\xC3\xA9rence)";   // 1.11.20
        case hmi::LocalVar::Section::Output: return "param\xC3\xA8tre (VAR_OUTPUT, sortie)";
    }
    return "locale";
}
std::string localDetail(const hmi::LocalVar& l) {
    return l.type + "  \xC2\xB7  " + localSection(l) + (l.initial.empty() ? std::string{} : "  \xC2\xB7  := " + l.initial);
}

// Une fonction du projet : "Moyenne(a, b)" s'insere avec le curseur entre
// les parentheses (apres elles si elle n'a pas de parametre).
Item userFunctionItem(const hmi::HmiFunction& f, int rank) {
    Item it;
    it.text = f.name;
    // Court : la liste est etroite ; la signature complete vient avec "(".
    std::string about = f.description;
    if (about.size() > 48) {
        std::size_t cut = 45;
        while (cut > 0 && (static_cast<unsigned char>(about[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
        about = about.substr(0, cut) + "...";
    }
    it.detail = "fonction du projet  \xC2\xB7  " + (f.returnType.empty() ? std::string("sans retour") : f.returnType)
              + (about.empty() ? std::string{} : "  \xC2\xB7  " + about);
    it.kind = Kind::UserFunction;
    it.rank = rank;
    const bool params = !hmi::splitDeclarations(hmi::decl::codeOf(f), true).parameters().empty();   // 1.11.18 : le modele aussi ; 1.11.20 : E/S, sorties
    it.insert = f.name + "()";
    it.caret = params ? f.name.size() + 1 : it.insert.size();
    return it;
}

// ---- 1.10 (chantier K) : le dialecte des scripts de l'IHM (decisions 13 et 13 bis) ----
// Le texte apres le curseur, pose par attach() le temps d'un appel : une
// fonction interne se declare avant ou apres son emploi.
thread_local std::string_view tAfterCaret;

struct ScriptFunction {
    std::string name, params, ret;
};

std::string squeezed(std::string_view s) {
    std::string out;
    bool space = false;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { space = !out.empty(); continue; }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// Les FUNCTION Nom(parametres) : TYPE d'un script (hors commentaires et chaines).
std::vector<ScriptFunction> scriptFunctions(std::string_view text) {
    std::vector<ScriptFunction> out;
    constexpr auto npos = std::string_view::npos;
    const std::size_t n = text.size();
    const auto blank = [&](std::size_t k) {
        while (k < n && (text[k] == ' ' || text[k] == '\t')) ++k;
        return k;
    };
    std::size_t i = 0;
    while (i < n) {
        if (text.compare(i, 2, "(*") == 0) { const auto e = text.find("*)", i + 2); i = e == npos ? n : e + 2; continue; }
        if (text.compare(i, 2, "//") == 0) { const auto e = text.find('\n', i); i = e == npos ? n : e + 1; continue; }
        if (text[i] == '\'' || text[i] == '"') { const auto e = text.find(text[i], i + 1); i = e == npos ? n : e + 1; continue; }
        if (!identChar(text[i])) { ++i; continue; }
        std::size_t j = i;
        while (j < n && identChar(text[j])) ++j;
        if (!iequals(text.substr(i, j - i), "FUNCTION")) { i = j; continue; }
        std::size_t k = blank(j);
        const std::size_t s = k;
        while (k < n && identChar(text[k])) ++k;
        ScriptFunction f;
        f.name = std::string(text.substr(s, k - s));
        k = blank(k);
        if (k < n && text[k] == '(') {
            int depth = 0;
            std::size_t p = k;
            for (; p < n; ++p) {
                if (text[p] == '(') ++depth;
                else if (text[p] == ')' && --depth == 0) break;
            }
            f.params = squeezed(text.substr(k + 1, std::min(p, n) - k - 1));
            k = blank(p < n ? p + 1 : n);
        }
        if (k < n && text[k] == ':') {
            k = blank(k + 1);
            std::size_t e = k;
            while (e < n && text[e] != '\n' && text[e] != '\r' && text[e] != ';' && text.compare(e, 2, "(*") != 0
                   && text.compare(e, 2, "//") != 0)
                ++e;
            f.ret = squeezed(text.substr(k, e - k));
            k = e;
        }
        if (!f.name.empty() && std::none_of(out.begin(), out.end(), [&](const ScriptFunction& o) { return iequals(o.name, f.name); }))
            out.push_back(std::move(f));
        i = k;
    }
    return out;
}

// ---- 1.10 (chantier K2) : le langage de S1, les operateurs de S2, les enumerations ----
//  Les mots et les fonctions du dialecte viennent de S1 (lang110::builtins() et
//  snippets(), HmiScriptCheck110.hpp) : K ne decide que du genre (l'icone) et de
//  la place du curseur. Les conversions et les operateurs viennent de S2
//  (HmiOperators.hpp), les enumerations de E (HmiEnums.hpp).
namespace lang = hmi::lang110;

const std::string kDot{"  \xC2\xB7  "};
const std::string kEllipsis{"\xE2\x80\xA6"};

// Le genre d'un modele de S1 : un type (ARRAY, MAP...), un mot (NULL), sinon une structure.
Kind snippetKind(std::string_view name) {
    static constexpr std::string_view kTypes[] = {"ARRAY", "MAP", "REF_TO", "POINTER TO", "MAP_ITERATOR"};
    if (name == "NULL") return Kind::Keyword;
    for (const auto t : kTypes)
        if (name == t) return Kind::Type;
    return Kind::Structure;
}

// Une fonction du dialecte (MAP_HAS...) : "MAP_HAS()", le curseur entre les
// parentheses si elle prend un argument.
Item builtinItem(const lang::Builtin& b, int rank) {
    Item it;
    it.text = std::string(b.name);
    it.detail = std::string(b.signature) + kDot + std::string(b.help);
    it.kind = Kind::Function;
    it.rank = rank;
    it.insert = it.text + "()";
    const auto open = b.signature.find('(');
    const bool args = open != std::string_view::npos && open + 1 < b.signature.size() && b.signature[open + 1] != ')';
    it.caret = args ? it.text.size() + 1 : it.insert.size();
    return it;
}

// 1.12.0 : une native de l'IHM (LIMIT, CONCAT, RGB, RANDOM...) : son appel, le curseur
// entre les parentheses si elle prend un argument ; le detail : sa signature, sa phrase.
Item nativeItem(const hmi::natives::Function& f, int rank) {
    Item it;
    it.text = std::string(f.name);
    it.detail = hmi::natives::shortSignature(f) + (f.returns.empty() ? std::string{} : " : " + std::string(f.returns)) + kDot + std::string(f.summary);
    it.kind = Kind::Function;
    it.rank = rank;
    it.insert = it.text + "()";
    it.caret = f.params.empty() ? it.insert.size() : it.text.size() + 1;
    return it;
}

// Un modele de S1 (FUNCTION, FOR EACH, ARRAY...) : son texte ; le curseur sur sa
// ligne vide (le corps d'une boucle), sinon au bout de sa premiere ligne.
Item snippetItem(const lang::Builtin& s, int rank) {
    Item it;
    it.text = std::string(s.name);
    it.detail = "scripts IHM" + kDot + std::string(s.help);
    it.kind = snippetKind(s.name);
    it.rank = rank;
    const std::string body(s.signature);
    if (body != it.text) {
        it.insert = body;
        const auto blank = body.find("\n    \n");
        const auto eol = body.find('\n');
        it.caret = blank != std::string::npos ? blank + 5 : (eol != std::string::npos ? eol : body.size());
    }
    return it;
}

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
    return std::string(s.substr(b, e - b));
}

// Le code, commentaires et chaines blanchis (les places et les lignes restent).
std::string blanked(std::string_view s) {
    std::string out(s);
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        std::size_t e = i + 1;
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            e = s.find("*)", i + 2);
            e = e == std::string_view::npos ? s.size() : e + 2;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            e = s.find('\n', i);
            e = e == std::string_view::npos ? s.size() : e;
        } else if (c == '\'' || c == '"') {
            while (e < s.size() && s[e] != c) e += (s[e] == '$' && e + 1 < s.size()) ? 2 : 1;
            e = std::min(s.size(), e + 1);
        } else {
            ++i;
            continue;
        }
        for (std::size_t k = i; k < e; ++k)
            if (out[k] != '\n') out[k] = ' ';
        i = e;
    }
    return out;
}

// La ligne `n` d'un texte (1 = la premiere).
std::string_view lineOf(std::string_view text, int n) {
    std::size_t at = 0;
    for (int k = 1; k < n; ++k) {
        const auto nl = text.find('\n', at);
        if (nl == std::string_view::npos) return {};
        at = nl + 1;
    }
    const auto end = text.find('\n', at);
    return text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
}

// "POINTER TO T", "REF_TO T", "REFERENCE TO T" -> "T" ; vide sinon.
std::string pointee(std::string_view type) {
    const std::string t = trimmed(type), u = upper(t);
    for (const std::string_view k : {std::string_view("POINTER TO "), std::string_view("REF_TO "), std::string_view("REFERENCE TO ")})
        if (u.rfind(k, 0) == 0) return trimmed(std::string_view(t).substr(k.size()));
    return {};
}
// "MAP[K] OF V" -> K et V.
bool mapTypes(std::string_view type, std::string& key, std::string& value) {
    const std::string t = trimmed(type), u = upper(t);
    if (u.rfind("MAP", 0) != 0 || u.rfind("MAP_", 0) == 0) return false;
    const auto open = t.find('['), close = t.find(']');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    key = trimmed(std::string_view(t).substr(open + 1, close - open - 1));
    const auto of = u.find(" OF ", close);
    value = of == std::string::npos ? std::string{} : trimmed(std::string_view(t).substr(of + 4));
    return true;
}
// "ARRAY[0..9] OF E" -> "E" ; vide sinon.
std::string arrayElement(std::string_view type) {
    const std::string t = trimmed(type), u = upper(t);
    if (u.rfind("ARRAY", 0) != 0) return {};
    const auto close = t.find(']');
    const auto of = u.find(" OF ", close == std::string::npos ? 0 : close);
    return of == std::string::npos ? std::string{} : trimmed(std::string_view(t).substr(of + 4));
}

// "1  .  Automatique" : le nombre et le texte d'une valeur (et sa description).
std::string valueDetail(const hmi::HmiEnumValue& v) {
    std::string d = std::to_string(v.value) + kDot + hmi::enumText(v);
    if (!v.description.empty()) d += kDot + v.description;
    return d;
}
// "4 valeurs : Arret, Auto, Manu, Defaut"
std::string valuesSummary(const hmi::HmiType& e) {
    std::string s = std::to_string(e.values.size()) + (e.values.size() > 1 ? " valeurs" : " valeur");
    for (std::size_t i = 0; i < e.values.size() && i < 6; ++i) s += (i ? ", " : " : ") + e.values[i].name;
    if (e.values.size() > 6) s += ", " + kEllipsis;
    return s;
}
// Le meilleur rang des deux textes (0 : commence par ce qui est tape ; 1 : le contient ; -1).
int bestRank(std::string_view a, std::string_view b, const std::string& needle) {
    const int ra = matchRank(a, needle), rb = matchRank(b, needle);
    if (ra < 0) return rb;
    if (rb < 0) return ra;
    return std::min(ra, rb);
}

// Le type au bout d'un chemin, depuis le type de sa racine : ".x", "[i]", "^".
std::string walkType(const hmi::Project& hp, std::string type, std::string_view rest) {
    std::size_t i = 0;
    while (i < rest.size() && !type.empty()) {
        const char c = rest[i];
        if (c == '^') {
            type = pointee(type);
            ++i;
            continue;
        }
        if (c == '[') {
            int depth = 1;
            for (++i; i < rest.size() && depth > 0; ++i) depth += rest[i] == '[' ? 1 : rest[i] == ']' ? -1 : 0;
            std::string k, v;
            type = mapTypes(type, k, v) ? v : arrayElement(type);
            continue;
        }
        if (c == '.') {
            std::size_t e = ++i;
            while (e < rest.size() && identChar(rest[e])) ++e;
            const auto name = rest.substr(i, e - i);
            i = e;
            std::string next;
            for (const auto& m : hmi::types::membersOf(hp, type))
                if (iequals(m.name, name)) { next = m.type; break; }
            if (next.empty())                                  // 1.10.1 (U2) : un symbole (a.Debit)
                for (const auto& m : hmi::operandMembers(hp, type))
                    if (iequals(m.name, name)) { next = m.type; break; }
            type = std::move(next);
            continue;
        }
        ++i;
    }
    return type;
}

// Les parametres d'un en-tete tel qu'ecrit ("a : T_MODE; VAR_IN_OUT b : REAL") : noms et types.
std::vector<std::pair<std::string, std::string>> headerParams(std::string_view params) {
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t at = 0; at < params.size();) {
        auto semi = params.find(';', at);
        if (semi == std::string_view::npos) semi = params.size();
        std::string piece = trimmed(params.substr(at, semi - at));
        at = semi + 1;
        for (const std::string_view mode : {std::string_view("VAR_IN_OUT "), std::string_view("VAR_OUTPUT "), std::string_view("VAR_INPUT ")})
            if (upper(piece).rfind(mode, 0) == 0) piece = trimmed(std::string_view(piece).substr(mode.size()));
        const auto colon = piece.find(':');
        if (colon != std::string::npos)
            out.emplace_back(trimmed(std::string_view(piece).substr(0, colon)), trimmed(std::string_view(piece).substr(colon + 1)));
    }
    return out;
}

// Ce que le script declare a l'endroit du curseur, pour connaitre le type d'un nom.
struct Scope {
    const hmi::Project* hp{nullptr};
    const hmi::View*    view{nullptr};    // la vue dont on ecrit un script (une instance : son symbole)
    bool                script{false};
    std::string         whole, clean;     // tout le texte (avant ET apres le curseur), et blanchi
    int                 line{1};          // la ligne du curseur
    lang::Analysis      analysis;         // S1 : les fonctions internes, les noms de FOR EACH
    hmi::ScriptParts    parts;            // les locales du script
    // 1.10.1 (U2) : le script d'un operateur - a, b, Resultat (et TO_X) et leurs types.
    std::vector<hmi::OperatorName> opNames;

    // Un nom declare par le script la ou est le curseur : parametre ou locale de la
    // fonction interne, variable de FOR EACH, locale du script ; `type` : son type.
    bool declared(std::string_view name, std::string& type, int depth = 0) const {
        if (!script || name.empty()) return false;
        // 1.10.1 (U2) : les operandes et le resultat d'un operateur (ses parametres).
        for (const auto& n : opNames)
            if (iequals(n.name, name)) { type = n.type; return true; }
        for (const auto& f : analysis.functions) {
            if (line < f.firstLine || line > f.lastLine) continue;
            for (const auto& p : f.params)
                if (iequals(p.name, name)) { type = p.type; return true; }
            for (const auto& l : f.locals)
                if (iequals(l.name, name)) { type = l.type; return true; }
            if (iequals(f.name, name) && !f.returnType.empty()) { type = f.returnType; return true; }
            if (!f.params.empty()) continue;
            // Pas encore lisible par le simulateur (en cours d'ecriture) : l'en-tete tel qu'ecrit.
            for (const auto& g : scriptFunctions(whole))
                if (iequals(g.name, f.name))
                    for (const auto& [n, t] : headerParams(g.params))
                        if (iequals(n, name)) { type = t; return true; }
        }
        for (const auto& n : analysis.names) {
            if (!iequals(n.name, name) || line < n.firstLine || line > n.lastLine) continue;
            type = n.type.empty() ? forEachType(n, depth) : n.type;
            return true;
        }
        if (const auto* l = parts.local(name)) { type = l->type; return true; }
        return false;
    }
    // FOR EACH v IN T_MODE (v : T_MODE) ; FOR EACH k, v IN m (MAP[K] OF V : k : K, v : V) ;
    // FOR EACH x IN t (ARRAY[..] OF E : x : E ; i, x : DINT et E).
    std::string forEachType(const lang::LocalName& n, int depth) const {
        if (depth > 4) return {};
        const std::string l(lineOf(clean, n.firstLine));
        const std::string u = upper(l);
        const auto each = u.find("EACH");
        const auto in = each == std::string::npos ? std::string::npos : u.find(" IN ", each);
        if (in == std::string::npos) return {};
        std::vector<std::string> vars;
        for (std::size_t at = each + 4; at <= in;) {
            auto comma = l.find(',', at);
            if (comma == std::string::npos || comma > in) comma = in;
            vars.push_back(trimmed(std::string_view(l).substr(at, comma - at)));
            at = comma + 1;
        }
        const auto doAt = u.find(" DO", in + 4);
        const std::string coll = trimmed(std::string_view(l).substr(in + 4, (doAt == std::string::npos ? l.size() : doAt) - in - 4));
        std::size_t pos = 0;
        while (pos < vars.size() && !iequals(vars[pos], n.name)) ++pos;
        if (pos == vars.size() || coll.empty() || iequals(coll, n.name)) return {};
        if (const auto* e = hmi::findEnumeration(*hp, coll)) return vars.size() == 1 ? e->name : std::string{};
        const std::string ct = typeOf(coll, depth + 1);
        std::string k, v;
        if (mapTypes(ct, k, v)) return vars.size() == 2 ? (pos == 0 ? k : v) : std::string{};
        const std::string el = arrayElement(ct);
        if (!el.empty()) return vars.size() == 2 ? (pos == 0 ? std::string("DINT") : el) : el;
        return {};
    }
    // Le type d'un nom ou d'un chemin (p^.Debit, t[2], v.x) : le script d'abord, puis
    // les variables IHM et les instances de symboles (S2, operandTypeOf).
    std::string typeOf(std::string_view path, int depth = 0) const {
        const std::string p = trimmed(path);
        if (p.empty() || depth > 6) return {};
        const auto cut = p.find_first_of(".[^");
        const std::string root = p.substr(0, cut);
        std::string type;
        if (!declared(root, type, depth)) {
            if (p.find('^') == std::string::npos) return hmi::operandTypeOf(*hp, view, p);
            type = hmi::operandTypeOf(*hp, view, root);
        }
        return cut == std::string::npos ? type : walkType(*hp, type, std::string_view(p).substr(cut));
    }
    // Le type rendu par la fonction interne ou est le curseur (vide : aucune).
    std::string returnTypeAt() const {
        for (const auto& f : analysis.functions) {
            if (!script || line < f.firstLine || line > f.lastLine) continue;
            if (!f.returnType.empty()) return f.returnType;
            for (const auto& g : scriptFunctions(whole))
                if (iequals(g.name, f.name)) return g.ret;
        }
        return {};
    }
    // Le type du parametre `index` d'une fonction interne, ou d'une fonction IHM du projet.
    std::string paramTypeOf(std::string_view name, std::size_t index) const {
        if (const auto* f = script ? analysis.function(name) : nullptr) {
            if (index < f->params.size()) return f->params[index].type;
            if (f->params.empty())
                for (const auto& g : scriptFunctions(whole))
                    if (iequals(g.name, name)) {
                        const auto ps = headerParams(g.params);
                        return index < ps.size() ? ps[index].second : std::string{};
                    }
            return {};
        }
        if (const auto* hf = hp->functionByName(name)) {
            const auto decls = hmi::splitDeclarations(hmi::decl::codeOf(*hf), true, [this](std::string_view t) { return lang::knownHmiType(hp, t); });
            const auto ins = decls.parameters();      // 1.11.20 : E/S et sorties comprises, dans l'ordre
            return index < ins.size() ? ins[index]->type : std::string{};
        }
        return {};
    }
    // Une variable de ce type, la ou est le curseur (le selecteur d'un CASE propose).
    std::string variableOfType(std::string_view type) const {
        if (script) {
            for (const auto& f : analysis.functions) {
                if (line < f.firstLine || line > f.lastLine) continue;
                for (const auto& p : f.params)
                    if (iequals(trimmed(p.type), type)) return p.name;
                for (const auto& l : f.locals)
                    if (iequals(trimmed(l.type), type)) return l.name;
            }
            for (const auto& l : parts.locals)
                if (iequals(trimmed(l.type), type)) return l.name;
        }
        for (const auto& v : hp->programs.variables)
            if (iequals(v.type, type)) return v.name;
        return {};
    }
};

// 1.10.1 (U2) : ce qu'est le nom, en court pour la liste (le role entier est dans la
// legende et au survol) : "l'objet", "l'autre operande", "l'objet converti", "le resultat".
std::string shortRole(std::string_view role) {
    std::size_t cut = role.size();
    for (const std::string_view stop : {std::string_view(" de type"), std::string_view(","), std::string_view(" ("), std::string_view(" sous le nom")})
        if (const auto at = role.find(stop); at != std::string_view::npos && at < cut) cut = at;
    return std::string(role.substr(0, cut));
}

// 1.10.1 (U2) : les noms du script de l'operateur `op` (a, b, Resultat, TO_X), avec leurs types.
std::vector<hmi::OperatorName> operatorScope(const hmi::Project& hp, hmi::Id op) {
    if (op == hmi::kNoId) return {};
    hmi::OperatorOwner owner;
    const auto* o = hmi::operatorById(hp, op, &owner);
    return o ? hmi::operatorNames(*o, owner.name) : std::vector<hmi::OperatorName>{};
}

Scope makeScope(const hmi::Project& hp, std::string_view before, bool script, hmi::Id op = hmi::kNoId) {
    Scope sc;
    sc.hp = &hp;
    sc.view = hmiparams::assistView(hp);
    sc.script = script;
    if (script) sc.opNames = operatorScope(hp, op);
    sc.whole = std::string(before);
    if (script) sc.whole += tAfterCaret;
    sc.clean = blanked(sc.whole);
    sc.line = 1 + static_cast<int>(std::count(before.begin(), before.end(), '\n'));
    if (script) {
        sc.analysis = lang::analyze(sc.whole, {}, {}, lang::enumValuesOf(&hp), &hp);
        sc.parts = hmi::splitDeclarations(sc.whole, false, [&hp](std::string_view t) { return lang::knownHmiType(&hp, t); });
    }
    return sc;
}

// Le chemin qui finit `text`, blancs de fin sautes ("x := v1 " -> "v1") ; vide sinon.
std::string pathBefore(std::string_view text) {
    std::size_t end = text.size();
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    const std::size_t start = pathStart(text, end);
    if (start >= end || !(std::isalpha(static_cast<unsigned char>(text[start])) != 0 || text[start] == '_')) return {};
    return std::string(text.substr(start, end - start));
}

// Dans un CASE sur une enumeration, a la place d'une etiquette (une ligne vide de la
// liste des branches, apres "CASE m OF", apres "T_MODE#Auto, ") : l'enumeration.
const hmi::HmiType* caseEnumeration(const Scope& sc, std::string_view before) {
    const std::string b = blanked(before);
    const auto nl = b.rfind('\n');
    const std::string line = trimmed(std::string_view(b).substr(nl == std::string::npos ? 0 : nl + 1));
    const std::string lu = upper(line);
    const bool afterOf = lu.size() >= 3 && lu.compare(lu.size() - 3, 3, " OF") == 0 && lu.rfind("CASE", 0) == 0;
    const bool labels = !line.empty() && line.back() == ',' && line.find_first_of(":;=(") == std::string::npos;
    if (!line.empty() && !afterOf && !labels) return nullptr;
    // Le bloc ouvert le plus proche doit etre un CASE (pas un IF dans une branche).
    std::vector<std::pair<std::string, std::size_t>> blocks;
    for (std::size_t i = 0; i < b.size();) {
        if (!identChar(b[i])) { ++i; continue; }
        std::size_t e = i;
        while (e < b.size() && identChar(b[e])) ++e;
        const std::string w = upper(std::string_view(b).substr(i, e - i));
        if (w == "CASE" || w == "IF" || w == "WHILE" || w == "REPEAT" || w == "FOR") blocks.emplace_back(w, e);
        else if (!blocks.empty() && w == "END_" + blocks.back().first) blocks.pop_back();
        i = e;
    }
    if (blocks.empty() || blocks.back().first != "CASE") return nullptr;
    const std::size_t from = blocks.back().second;
    const std::string u = upper(b);
    std::size_t of = u.find("OF", from);
    while (of != std::string::npos && ((of > 0 && identChar(u[of - 1])) || (of + 2 < u.size() && identChar(u[of + 2])))) of = u.find("OF", of + 2);
    if (of == std::string::npos) return nullptr;
    const std::string selector = trimmed(std::string_view(b).substr(from, of - from));
    if (selector.empty()) return nullptr;
    return hmi::findEnumeration(*sc.hp, sc.typeOf(selector));
}

// "m := |", "m = |", "m <> |" (m une enumeration) : l'enumeration attendue.
const hmi::HmiType* expectedEnumeration(const Scope& sc, std::string_view before) {
    std::size_t end = before.size();
    while (end > 0 && (before[end - 1] == ' ' || before[end - 1] == '\t')) --end;
    // RETURN | dans une fonction interne qui rend une enumeration.
    {
        std::size_t w = end;
        while (w > 0 && identChar(before[w - 1])) --w;
        if (end - w == 6 && iequals(before.substr(w, 6), "RETURN")) return hmi::findEnumeration(*sc.hp, sc.returnTypeAt());
    }
    // F( ou F(a, | : l'argument d'une fonction interne (ou IHM du projet) qui attend une enumeration.
    if (end > 0 && (before[end - 1] == '(' || before[end - 1] == ',')) {
        const std::string_view b = std::string_view(sc.clean).substr(0, std::min(end, sc.clean.size()));
        int depth = 0;
        std::size_t commas = 0, k = b.size();
        for (; k > 0; --k) {
            const char ch = b[k - 1];
            if (ch == ';') { k = 0; break; }
            if (ch == ')') ++depth;
            else if (ch == '(') { if (depth == 0) break; --depth; }
            else if (ch == ',' && depth == 0) ++commas;
        }
        if (k > 0) return hmi::findEnumeration(*sc.hp, sc.paramTypeOf(calleeAt(b, k - 1), commas));
        return nullptr;
    }
    std::size_t op = 0;
    if (end >= 2 && (before.substr(end - 2, 2) == ":=" || before.substr(end - 2, 2) == "<>")) op = end - 2;
    else if (end >= 1 && before[end - 1] == '=' && !(end >= 2 && (before[end - 2] == '<' || before[end - 2] == '>' || before[end - 2] == ':')))
        op = end - 1;
    else return nullptr;
    const std::string path = pathBefore(before.substr(0, op));
    if (path.empty()) return nullptr;
    return hmi::findEnumeration(*sc.hp, sc.typeOf(path));
}

// "FOR EACH v IN |" : la collection est a ecrire.
bool forEachHeader(std::string_view before) {
    const auto nl = before.rfind('\n');
    const std::string line = upper(before.substr(nl == std::string_view::npos ? 0 : nl + 1));
    const auto each = line.find("FOR EACH ");
    const auto in = line.rfind(" IN ");
    return each != std::string::npos && in != std::string::npos && in > each && trimmed(std::string_view(line).substr(in + 4)).empty();
}

// "it := MAP_BEGIN(m)" dans le script : `name` est un iterateur sur `map`.
bool iteratorOf(std::string_view clean, std::string_view name, std::string& map) {
    const std::string u = upper(clean), n = upper(name);
    for (std::size_t at = u.find(n); at != std::string::npos; at = u.find(n, at + 1)) {
        if ((at > 0 && identChar(u[at - 1])) || (at + n.size() < u.size() && identChar(u[at + n.size()]))) continue;
        std::size_t k = at + n.size();
        const auto skip = [&] { while (k < u.size() && (u[k] == ' ' || u[k] == '\t')) ++k; };
        skip();
        if (u.compare(k, 2, ":=") != 0) continue;
        k += 2;
        skip();
        if (u.compare(k, 9, "MAP_BEGIN") != 0) continue;
        k += 9;
        skip();
        if (k >= u.size() || u[k] != '(') continue;
        ++k;
        skip();
        std::size_t e = k;
        while (e < u.size() && (identChar(u[e]) || u[e] == '.' || u[e] == '^')) ++e;
        map = std::string(clean.substr(k, e - k));
        return true;
    }
    return false;
}

// Les membres d'un type : une structure IHM, une DDT de l'automate. Faux : ni l'un ni l'autre.
bool typeMembers(const hmi::Project& hp, const domain::Project* plc, const std::string& type, const std::string& needle,
                 const std::function<void(Item)>& push) {
    int order = 0;
    const auto add = [&](const std::string& name, std::string detail) {
        const int r = matchRank(name, needle);
        if (r < 0) return;
        Item it;
        it.text = name;
        it.detail = std::move(detail);
        it.kind = Kind::Member;
        it.rank = r * 100 + order++;
        push(std::move(it));
    };
    if (const auto ms = hmi::types::membersOf(hp, type); !ms.empty()) {
        for (const auto& m : ms) add(m.name, hmi::types::normalized(m.type) + (m.description.empty() ? std::string{} : kDot + m.description));
        return true;
    }
    // 1.10.1 (U2) : un symbole (l'operande d'un operateur de symbole) - ses parametres,
    // puis ce que publie une instance (comme Pompe_3.).
    if (const auto ms = hmi::operandMembers(hp, type); !ms.empty()) {
        for (const auto& m : ms) add(m.name, m.type + kDot + m.help);
        return true;
    }
    if (!plc) return false;
    for (const auto& d : plc->derivedTypes) {
        if (!iequals(plc->strings.text(d.name), type)) continue;
        for (const auto vi : d.fields) {
            if (vi >= plc->variables.size()) continue;
            const auto& v = plc->variables[vi];
            const std::string comment(plc->strings.text(v.comment));
            add(std::string(plc->strings.text(v.name)), std::string(plc->strings.text(v.type.name)) + (comment.empty() ? std::string{} : kDot + comment));
        }
        return true;
    }
    return false;
}

// Apres un point, un nom du script : "it." (un iterateur de MAP) -> Key, Value ;
// "p^." (un pointeur, une reference) -> les membres du type vise ; une locale d'un
// type IHM -> ses membres ; un tableau -> Length. Faux : le chemin n'est pas au script.
bool dialectMembers(const Scope& sc, const domain::Project* plc, const std::string& path, const std::string& needle,
                    const std::function<void(Item)>& push) {
    const auto cut = path.find_first_of(".[^");
    const std::string root = path.substr(0, cut);
    std::string rootType;
    const bool local = sc.declared(root, rootType);
    if (cut == std::string::npos) {
        std::string map;
        const bool begun = iteratorOf(sc.clean, root, map);   // "it := MAP_BEGIN(m)" : meme sans declaration lue
        if (begun || (local && iequals(trimmed(rootType), "MAP_ITERATOR"))) {
            std::string kt, vt;
            if (!map.empty()) (void)mapTypes(sc.typeOf(map), kt, vt);
            const auto add = [&](const char* name, const std::string& type, const std::string& what) {
                const int r = matchRank(name, needle);
                if (r < 0) return;
                Item it;
                it.text = name;
                it.detail = (type.empty() ? std::string("?") : type) + kDot + what + (map.empty() ? std::string{} : " (" + map + ")");
                it.kind = Kind::Member;
                it.rank = r;
                push(std::move(it));
            };
            add("Key", kt, "la cl\xC3\xA9 de l'\xC3\xA9l\xC3\xA9ment o\xC3\xB9 est l'it\xC3\xA9rateur");
            add("Value", vt, "sa valeur");
            return true;
        }
    }
    if (!local && path.find('^') == std::string::npos) return false;
    const std::string type = sc.typeOf(path);
    if (type.empty()) return true;                     // un nom du script, d'un type inconnu : rien
    if (upper(type).rfind("ARRAY", 0) == 0) {
        if (const int r = matchRank("Length", needle); r >= 0) {
            Item it;
            it.text = "Length";
            it.detail = "DINT" + kDot + "le nombre d'\xC3\xA9l\xC3\xA9ments du tableau";
            it.kind = Kind::Member;
            it.rank = r;
            push(std::move(it));
        }
        return true;
    }
    std::string k, v;
    if (mapTypes(type, k, v)) return true;             // une MAP : m[cle], MAP_... ; pas de membres
    (void)typeMembers(*sc.hp, plc, type, needle, push);
    return true;
}

// Les branches du modele "CASE avec toutes les valeurs" : le texte de la correction
// "Ajouter les valeurs manquantes" de S1 (Finding::fixText) pour un CASE vide sur
// cette enumeration, decale de quatre espaces comme les modeles de S1.
std::string allValuesBranches(const hmi::Project& hp, const hmi::HmiType& e) {
    const std::string probe = "VAR\n    x : " + e.name + ";\nEND_VAR\nCASE x OF\nEND_CASE;\n";
    std::string fix;
    for (const auto& f : lang::analyze(probe, {}, {}, lang::enumValuesOf(&hp), &hp).findings)
        if (f.fixLine > 0 && !f.fixText.empty()) { fix = f.fixText; break; }
    if (fix.empty())                                    // le meme texte, si S1 ne le donne pas
        for (const auto& v : e.values) fix += e.name + "#" + v.name + ": ;\n";
    std::string out;
    for (std::size_t at = 0; at < fix.size();) {
        auto nl = fix.find('\n', at);
        if (nl == std::string::npos) nl = fix.size();
        const std::string l = trimmed(std::string_view(fix).substr(at, nl - at));
        if (!l.empty()) out += "    " + l + "\n";
        at = nl + 1;
    }
    return out;
}

// La signature d'une fonction interne du script, ou d'une fonction du dialecte (S1).
bool dialectSignature(std::string_view code, std::string_view name, ui::MultiLineText::Signature& out) {
    const auto a = lang::analyze(code);
    if (a.function(name)) {
        // 1.11.20 : toutes les fonctions internes de ce nom (des surcharges).
        bool first = true;
        for (const auto& f : a.functions) {
            if (!iequals(f.name, name)) continue;
            ui::MultiLineText::Signature one;
            one.name = f.name;
            for (const auto& p : f.params)
                one.parameters.push_back(std::string(p.mode == lang::Param::Mode::InOut    ? "VAR_IN_OUT "
                                                     : p.mode == lang::Param::Mode::Output ? "VAR_OUTPUT "
                                                                                           : "")
                                         + p.name + " : " + p.type);
            one.returns = f.returnType;
            if (first) out = std::move(one);
            else out.overloads.push_back(std::move(one));
            first = false;
        }
        return true;
    }
    for (const auto& b : lang::builtins()) {
        if (!iequals(b.name, name)) continue;
        out.name = std::string(b.name);
        out.parameters.clear();
        const auto open = b.signature.find('('), close = b.signature.rfind(')');
        if (open != std::string_view::npos && close != std::string_view::npos && close > open + 1)
            for (std::size_t at = open + 1; at < close;) {
                auto comma = b.signature.find(',', at);
                if (comma == std::string_view::npos || comma > close) comma = close;
                out.parameters.push_back(trimmed(b.signature.substr(at, comma - at)));
                at = comma + 1;
            }
        const auto colon = close == std::string_view::npos ? std::string_view::npos : b.signature.find(" : ", close);
        out.returns = colon == std::string_view::npos ? std::string{} : std::string(b.signature.substr(colon + 3));
        return true;
    }
    return false;
}

// Les cles litterales deja ecrites pour la MAP `name` : name['cle'], MAP_HAS(name, 'cle'),
// MAP_GET(name, 'cle', ...), MAP_REMOVE(name, 'cle') (dans le code, pas les commentaires).
std::vector<std::string> mapKeys(std::string_view text, std::string_view clean, std::string_view name) {
    std::vector<std::string> out;
    const auto add = [&](std::string k) {
        if (!k.empty() && k.find('\n') == std::string::npos && std::find(out.begin(), out.end(), k) == out.end()) out.push_back(std::move(k));
    };
    const auto literalAt = [&](std::size_t k) -> std::string {
        while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
        if (k >= text.size() || text[k] != '\'') return {};
        const auto e = text.find('\'', k + 1);
        return e == std::string_view::npos ? std::string{} : std::string(text.substr(k + 1, e - k - 1));
    };
    const std::string u = upper(clean), n = upper(name);
    if (n.empty()) return out;
    for (std::size_t at = u.find(n); at != std::string::npos; at = u.find(n, at + 1)) {
        if ((at > 0 && (identChar(u[at - 1]) || u[at - 1] == '.')) || (at + n.size() < u.size() && identChar(u[at + n.size()]))) continue;
        std::size_t k = at + n.size();
        while (k < u.size() && (u[k] == ' ' || u[k] == '\t')) ++k;
        if (k < u.size() && u[k] == '[') { add(literalAt(k + 1)); continue; }
        if (k >= u.size() || u[k] != ',') continue;
        std::size_t b = at;
        while (b > 0 && (u[b - 1] == ' ' || u[b - 1] == '\t')) --b;
        if (b == 0 || u[b - 1] != '(') continue;
        std::size_t e = b - 1;
        while (e > 0 && (u[e - 1] == ' ' || u[e - 1] == '\t')) --e;
        std::size_t w = e;
        while (w > 0 && identChar(u[w - 1])) --w;
        if (u.compare(w, 4, "MAP_") == 0) add(literalAt(k + 1));
    }
    return out;
}

// Ce que le langage de S1, les operateurs de S2 et les enumerations ajoutent la ou
// l'on ecrit du code. `full` : un script ; sinon un champ (une expression), ou seules
// les valeurs d'une enumeration, ses types et les conversions ont leur place.
void dialectCode(const Scope& sc, std::string_view before, const std::string& needle, bool full,
                 const std::function<void(Item)>& push) {
    const auto& hp = *sc.hp;
    const auto values = [&](const hmi::HmiType& e, bool label, const std::string& where) {
        int i = 0;
        for (const auto& v : e.values) {
            const std::string lit = e.name + "#" + v.name;
            const int r = bestRank(v.name, lit, needle);
            if (r < 0) { ++i; continue; }
            Item it;
            it.text = label ? lit + ":" : lit;
            it.detail = where + valueDetail(v);
            it.kind = Kind::Type;
            it.rank = -1000 + r * 100 + i++;
            it.insert = label ? lit + ": " : lit;
            push(std::move(it));
        }
    };
    // (1) Dans un CASE sur une enumeration, a la place d'une etiquette : ses valeurs d'abord.
    if (full)
        if (const auto* e = caseEnumeration(sc, before)) values(*e, true, "dans ce CASE" + kDot);
    // (2) Le type attendu est une enumeration (m := , m = , m <> ) : ses valeurs d'abord.
    if (const auto* e = expectedEnumeration(sc, before)) values(*e, false, std::string{});
    // (3) FOR EACH v IN | : les enumerations, puis les MAP et les tableaux du script.
    if (full && forEachHeader(before)) {
        int i = 0;
        for (const auto* e : hmi::enumerations(hp)) {
            const int r = matchRank(e->name, needle);
            if (r < 0) continue;
            Item it;
            it.text = e->name;
            it.detail = "\xC3\xA9num\xC3\xA9ration" + kDot + valuesSummary(*e) + " (dans l'ordre)";
            it.kind = Kind::DerivedType;
            it.rank = -1000 + r * 100 + i++;
            push(std::move(it));
        }
        for (const auto& l : sc.parts.locals) {
            std::string k, v;
            if (!mapTypes(l.type, k, v) && arrayElement(l.type).empty()) continue;
            const int r = matchRank(l.name, needle);
            if (r < 0) continue;
            Item it;
            it.text = l.name;
            it.detail = l.type + kDot + localSection(l);
            it.kind = Kind::LocalVariable;
            it.rank = -900 + r * 100 + i++;
            push(std::move(it));
        }
    }
    // (4) Apres un nom suivi d'un blanc (Ctrl+Espace) : les operateurs de son type (S2).
    if (full && needle.empty() && !before.empty() && (before.back() == ' ' || before.back() == '\t')) {
        const std::string path = pathBefore(before);
        const std::string type = path.empty() ? std::string{} : sc.typeOf(path);
        int i = 0;
        if (!type.empty())
            for (const auto& s : hmi::operatorSuggestions(hp, type)) {
                if (s.insert.rfind("TO_", 0) == 0) continue;      // une conversion s'ecrit TO_xxx(v), pas apres v
                Item it;
                it.text = s.label;
                it.detail = s.help;
                it.kind = Kind::Function;
                it.rank = -800 + i++;
                it.insert = s.insert;
                push(std::move(it));
            }
    }
    // (5) TO_ : les conversions des types et des symboles (S2), TO_<enumeration>(entier),
    // puis les conversions de base.
    if (needle.size() >= 2 && needle.compare(0, 2, "to") == 0) {
        const auto conversion = [&](std::string text, std::string detail, const std::string& fn, int rank) {
            Item it;
            it.text = std::move(text);
            it.detail = std::move(detail);
            it.kind = Kind::Function;
            it.rank = rank;
            it.insert = fn + "()";
            it.caret = fn.size() + 1;
            push(std::move(it));
        };
        for (const auto& s : hmi::allConversions(hp)) {
            std::string fn = s.insert;
            if (!fn.empty() && fn.back() == '(') fn.pop_back();
            if (const int r = matchRank(fn, needle); r >= 0) conversion(s.label, s.help, fn, r * 2 - 1);
        }
        for (const auto* e : hmi::enumerations(hp)) {
            const std::string fn = "TO_" + e->name;
            if (const int r = matchRank(fn, needle); r >= 0)
                conversion(fn + "(DINT) : " + e->name, "fourni" + kDot + "un entier qui est une valeur de " + e->name + " (contr\xC3\xB4l\xC3\xA9)",
                           fn, r * 2);
        }
        static constexpr std::string_view kBase[] = {"TO_INT", "TO_DINT", "TO_REAL", "TO_LREAL", "TO_STRING", "TO_BOOL", "TO_TIME"};
        for (const auto b : kBase)
            if (const int r = matchRank(b, needle); r >= 0)
                conversion(std::string(b) + "(x)", "conversion de base" + kDot + "norme", std::string(b), r * 2 + 1);
    }
    // (6) Une enumeration : son nom propose "T_MODE#" (la liste se rouvre sur ses valeurs).
    if (!needle.empty())
        for (const auto* e : hmi::enumerations(hp)) {
            const int r = matchRank(e->name, needle);
            if (r < 0) continue;
            Item it;
            it.text = e->name + "#" + kEllipsis;
            it.detail = "\xC3\xA9num\xC3\xA9ration" + kDot + valuesSummary(*e);
            it.kind = Kind::DerivedType;
            it.rank = r * 2;
            it.insert = e->name + "#";
            it.chain = true;
            push(std::move(it));
        }
    if (!full) return;
    // (7) Les fonctions internes du script (S1 : declarees avant ou apres, avec leur signature) ;
    // en tete celles dont le retour est le type attendu (x := ...).
    {
        std::string expected;
        {
            std::size_t end = before.size();
            while (end > 0 && (before[end - 1] == ' ' || before[end - 1] == '\t')) --end;
            if (end >= 2 && before.substr(end - 2, 2) == ":=")
                if (const std::string target = pathBefore(before.substr(0, end - 2)); !target.empty()) expected = trimmed(sc.typeOf(target));
        }
        const auto scanned = scriptFunctions(sc.whole);
        for (const auto& f : sc.analysis.functions) {
            const int r = matchRank(f.name, needle);
            if (r < 0) continue;
            std::string sig = f.signature().substr(std::min(f.name.size(), f.signature().size()));
            bool params = !f.params.empty();
            if (f.params.empty() && f.returnType.empty())    // pas encore lisible (en cours d'ecriture) : son en-tete
                for (const auto& g : scanned)
                    if (iequals(g.name, f.name)) {
                        sig = "(" + g.params + ")" + (g.ret.empty() ? std::string{} : " : " + g.ret);
                        params = !g.params.empty();
                    }
            Item it;
            it.text = f.name;
            it.detail = "fonction du script" + kDot + sig + (sig.find(") : ") == std::string::npos ? "  sans retour" : "") + kDot + "ligne "
                      + std::to_string(f.firstLine);
            it.kind = Kind::UserFunction;
            it.rank = !expected.empty() && iequals(trimmed(f.returnType), expected) ? -500 + r : r * 2 - 1;
            it.insert = f.name + "()";
            it.caret = params ? f.name.size() + 1 : it.insert.size();
            push(std::move(it));
        }
    }
    if (needle.empty()) return;
    // (8) Les modeles des enumerations : CASE avec toutes les valeurs, FOR EACH v IN T_MODE.
    for (const auto* e : hmi::enumerations(hp)) {
        std::string sel = sc.variableOfType(e->name);
        const bool known = !sel.empty();
        if (!known) sel = "m";
        const std::string head = "CASE " + sel + " OF\n";
        const std::string branches = allValuesBranches(hp, *e);
        Item cs;
        cs.text = "CASE " + sel + " OF " + kEllipsis + " toutes les valeurs de " + e->name + " " + kEllipsis + " END_CASE";
        if (const int r = matchRank(cs.text, needle); r >= 0) {
            cs.detail = "mod\xC3\xA8le" + kDot + "une branche par valeur (" + std::to_string(e->values.size()) + ")";
            cs.kind = Kind::Structure;
            cs.rank = r * 2;
            cs.insert = head + branches + "END_CASE;";
            const auto colon = branches.find(": ");
            cs.caret = known && colon != std::string::npos ? head.size() + colon + 2 : 5 + sel.size();
            push(std::move(cs));
        }
        const std::string loop = "FOR EACH v IN " + e->name + " DO";
        Item fe;
        fe.text = loop + " " + kEllipsis + " END_FOR";
        if (const int r = matchRank(fe.text, needle); r >= 0) {
            fe.detail = "it\xC3\xA9rateur" + kDot + "les " + std::to_string(e->values.size()) + " valeurs, dans l'ordre";
            fe.kind = Kind::Structure;
            fe.rank = r * 2;
            fe.insert = loop + "\n    \nEND_FOR;";
            fe.caret = loop.size() + 5;
            push(std::move(fe));
        }
    }
    // (9) Le langage de S1 : ses fonctions (MAP_..., REF, ADR, TO_UPPER...) et ses modeles.
    for (const auto& b : lang::builtins())
        if (const int r = matchRank(b.name, needle); r >= 0) push(builtinItem(b, r * 2 + 1));
    for (const auto& s : lang::snippets())
        if (const int r = matchRank(s.name, needle); r >= 0) push(snippetItem(s, r * 2 + 1));
}
// ---- fin 1.10 (chantier K2) ----

} // namespace

// ---------------------------------------------------------------- contexte ---
Where locate(std::string_view before, bool templateText) {
    Where w;
    w.from = before.size();

    auto memberOrCode = [&](Context fallback, std::size_t lowest) {
        // "a.b." : les membres de a.b.
        if (!before.empty() && before.back() == '.') {
            const auto start = std::max(lowest, pathStart(before, before.size() - 1));
            if (start < before.size() - 1) {
                const char first = before[start];
                if (std::isalpha(static_cast<unsigned char>(first)) != 0 || first == '_') {
                    w.context = Context::Member;
                    w.path = std::string(before.substr(start, before.size() - 1 - start));
                    return;
                }
            }
        }
        w.context = fallback;
    };

    if (templateText) {
        // Un texte a trous : seul l'interieur d'une accolade ouverte propose,
        // et pas son format ({Niveau:0.0}, {Etat:Oui|Non}).
        const auto open = before.rfind('{');
        if (open == std::string_view::npos || before.find('}', open) != std::string_view::npos
            || before.find(':', open) != std::string_view::npos) {
            w.context = Context::Text;
            return w;
        }
        memberOrCode(Context::Placeholder, open + 1);
        if (w.context == Context::Placeholder) w.from = before.size();
        return w;
    }

    // Du code ST : commentaires, chaines, parentheses (et leurs virgules).
    struct Paren { std::size_t at; std::size_t commas; };
    std::vector<Paren> parens;
    bool inBlock = false, inLine = false, inString = false;
    std::size_t quote = 0;
    // Lot 7 : un bloc VAR ... END_VAR ouvert, et le debut de la declaration en cours.
    bool inDecl = false;
    std::size_t declaration = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const char c = before[i];
        const char n = i + 1 < before.size() ? before[i + 1] : '\0';
        if (inBlock) { if (c == '*' && n == ')') { inBlock = false; ++i; } continue; }
        if (inLine) { if (c == '\n') inLine = false; continue; }
        if (inString) {
            if (c == '$' && n != '\0') { ++i; continue; }          // $' $$ $N : un caractere echappe
            if (c == '\'') inString = false;
            continue;
        }
        if (c == '(' && n == '*') { inBlock = true; ++i; continue; }
        if (c == '/' && n == '/') { inLine = true; ++i; continue; }
        if (c == '\'') { inString = true; quote = i; continue; }
        if (identChar(c) && (i == 0 || !identChar(before[i - 1]))) {
            std::size_t e = i;
            while (e < before.size() && identChar(before[e])) ++e;
            const auto word = upper(before.substr(i, e - i));
            if (e < before.size() && (word == "VAR" || word == "VAR_TEMP" || word == "VAR_INPUT")) { inDecl = true; declaration = e; }
            else if (word == "END_VAR") inDecl = false;
            i = e - 1;
            continue;
        }
        if (c == ';' && inDecl) { declaration = i + 1; continue; }
        if (c == '(') { parens.push_back({i, 0}); continue; }
        if (c == ')') { if (!parens.empty()) parens.pop_back(); continue; }
        if (c == ',' && !parens.empty()) ++parens.back().commas;
    }
    if (inBlock || inLine) { w.context = Context::Comment; return w; }
    if (inDecl && !inString) {
        // "x : |" : un type ; le nom, ou la valeur initiale (":= ...") : rien.
        const auto current = before.substr(declaration);
        const auto colon = current.find(':');
        const bool typing = colon != std::string_view::npos && current.find(":=") == std::string_view::npos
                         && current.find_first_not_of(" \t", colon + 1) == std::string_view::npos;
        w.context = typing ? Context::LocalType : Context::Text;
        return w;
    }
    if (inString) {
        w.context = Context::Text;
        if (parens.empty()) return w;
        const auto callee = calleeAt(before, parens.back().at);
        const auto argument = parens.back().commas;
        if ((callee == "IHM_NAVIGUER" || callee == "IHM_POPUP" || callee == "IHM_CHANGER_POPUP" || callee == "IHM_CENTRER_POPUP"
             || callee == "IHM_POPUP_OUVERTE") && argument == 0)
            w.context = Context::ViewName;
        else if (callee == "IHM_NAVIGUER" && argument == 1) w.context = Context::Transition;
        else if (callee == "IHM_APPELER" && argument == 0) w.context = Context::ScriptName;
        else if (callee == "IHM_SON" && argument == 0) w.context = Context::SoundName;
        else if ((callee == "IHM_ESCLAVE_SIMULE" || callee == "IHM_EQUIPEMENT_OK" || callee == "IHM_EQUIPEMENT_PING") && argument == 0)
            w.context = Context::EquipmentName;                                    // 1.9
        else if (callee == "IHM_JOURNAL" || (callee == "IHM_LOG" && argument == 1)) {
            const auto text = before.substr(quote + 1);
            const auto open = text.rfind('{');
            if (open != std::string_view::npos && text.find('}', open) == std::string_view::npos
                && text.find(':', open) == std::string_view::npos) {
                memberOrCode(Context::Placeholder, quote + 1 + open + 1);
                if (w.context == Context::Placeholder) w.from = before.size();
            }
            return w;
        }
        if (w.context != Context::Text) w.from = quote + 1;
        return w;
    }
    // 1.11.14 : IHM_LOG(| - le niveau (INFO, ERROR...), pas une variable.
    if (!parens.empty() && parens.back().commas == 0 && calleeAt(before, parens.back().at) == "IHM_LOG") {
        std::size_t b = before.size();
        while (b > 0 && identChar(before[b - 1])) --b;
        if (before.find_first_not_of(" \t", parens.back().at + 1) >= b) {
            w.context = Context::LogLevel;
            w.from = b;
            return w;
        }
    }
    memberOrCode(Context::Code, 0);
    return w;
}

// ------------------------------------------------------------ propositions ---
// ---- lot 9 : les variables systeme et d'instances ---------------------------------------
//  Apres "SYS." : les variables systeme ; apres "Vue." : ses objets et ses
//  variables (Open, X...) ; apres "Vue.Objet." : ce que l'objet publie.
bool publicMembers(const hmi::Project& hp, const std::string& path, const std::string& needle,
                   const std::function<void(Item)>& push) {
    const auto add = [&](std::string text, std::string detail, Kind kind, bool chain = false, int bias = 0) {
        const int r = matchRank(text, needle);
        if (r < 0) return;
        // La liste est etroite : le detail s'arrete (sans couper un caractere).
        if (detail.size() > 76) {
            std::size_t cut = 72;
            while (cut > 0 && (static_cast<unsigned char>(detail[cut]) & 0xC0) == 0x80) --cut;
            detail = detail.substr(0, cut) + "...";
        }
        Item it;
        it.text = std::move(text);
        it.detail = std::move(detail);
        it.kind = kind;
        it.rank = r + bias;
        if (chain) {
            it.insert = it.text + ".";
            it.chain = true;
        }
        push(std::move(it));
    };
    if (hmi::pub::isSysRoot(path)) {
        for (const auto& v : hmi::pub::kSysVars)
            if (hmi::pub::sysDomainShown(v.domain))     // 1.12.0 : XPGAnalyser IHM - ni Automate, ni Communication
            add(std::string(v.name), std::string(v.type) + "  \xC2\xB7  R  \xC2\xB7  " + std::string(hmi::pub::kSysDomains[v.domain]) + "  \xC2\xB7  " + std::string(v.text),
                Kind::SysVariable);
        // 1.9 : les structures des esclaves simules (le point pose, leurs noms ensuite).
        if (!hmi::pub::slaveEquipments(hp).empty())
            add(std::string(hmi::pub::kSlaveRoot), "structures  \xC2\xB7  une par esclave simul\xC3\xA9 : SYS.Slave." + hmi::pub::slaveKeysText(hp),
                Kind::SysVariable, true);
        return true;
    }
    // 1.9 : SYS.Slave. - les esclaves du projet ; SYS.Slave.<nom>. - leurs seize membres.
    {
        const auto dot = path.find('.');
        if (dot != std::string::npos && hmi::pub::isSysRoot(std::string_view(path).substr(0, dot))) {
            const std::string rest = path.substr(dot + 1);
            const auto dot2 = rest.find('.');
            if (!hmi::pub::same(std::string_view(rest).substr(0, dot2), hmi::pub::kSlaveRoot)) return true;
            if (dot2 == std::string::npos) {
                for (const auto* e : hmi::pub::slaveEquipments(hp))
                    add(hmi::slaveKey(e->name),
                        std::string(e->linkedSlave() ? "esclave li\xC3\xA9" : "seulement simul\xC3\xA9") + "  \xC2\xB7  " + e->twinLabel() + "  \xC2\xB7  16 membres",
                        Kind::Instance, true);
                return true;
            }
            const std::string key = rest.substr(dot2 + 1);
            if (key.find('.') != std::string::npos) return true;         // SYS.Slave.X.Membre. : rien apres
            if (!hmi::pub::slaveEquipment(hp, key)) return true;
            for (const auto& m : hmi::pub::kSlaveMembers)
                add(std::string(m.name), std::string(m.type) + "  \xC2\xB7  R  \xC2\xB7  " + std::string(m.text), Kind::SysVariable);
            return true;
        }
    }
    const auto dot = path.find('.');
    const hmi::View* v = hmi::pub::viewNamed(hp, std::string_view(path).substr(0, dot));
    if (!v) return false;
    const hmi::View shown = hmi::inherits(hp, *v) ? hmi::compose(hp, *v) : *v;
    if (dot == std::string::npos) {
        for (const auto& m : hmi::pub::viewMembers(*v))
            add(m.name, m.type + "  \xC2\xB7  " + std::string(hmi::pub::accessLabel(m.access)) + "  \xC2\xB7  " + m.text, Kind::InstanceVariable);
        for (const auto& o : shown.objects)
            if (!o.name.empty()) {
                // 1.10.2 (chantier A) : une instance de symbole annonce ses parametres.
                const auto ps = hmi::pub::instanceParams(hp, o);
                std::string names;
                for (const auto& ip : ps) names += (names.empty() ? "" : ", ") + ip.name;
                add(o.name, std::string(hmi::kindLabel(o.kind)) + "  \xC2\xB7  "
                                + (names.empty() ? "ses variables : " + v->name + "." + o.name + ".Visible..." : "ses param\xC3\xA8tres : " + names),
                    Kind::Instance, true, -5);   // 1.10.2 : les objets avant les variables de la vue
            }
        return true;
    }
    const std::string objectPart = path.substr(dot + 1);
    const auto dot2 = objectPart.find('.');
    if (dot2 != std::string::npos) {
        // 1.11.1 (decision 108) : Vue.Objet.Alarmes. - ses alarmes ; Vue.Objet.Alarmes.<alarme>. -
        // ses membres. Ailleurs (Vue.Objet.Propriete.) : rien apres.
        const std::string after = objectPart.substr(dot2 + 1);
        const auto dot3 = after.find('.');
        if (!hmi::pub::isAlarmsRoot(std::string_view(after).substr(0, dot3))) return true;
        const hmi::Object* o = nullptr;
        for (const auto& x : shown.objects)
            if (hmi::pub::same(x.name, std::string_view(objectPart).substr(0, dot2))) { o = &x; break; }
        if (!o || !hmi::pub::keyOf(*o, std::string_view(after).substr(0, dot3)).empty()) return true;
        const auto alarms = hmi::pub::objectAlarmNames(hp, *v, *o);
        const std::string rest = dot3 == std::string::npos ? std::string{} : after.substr(dot3 + 1);
        for (const auto& a : alarms)
            if (hmi::pub::same(a.local, rest)) {
                for (const auto& m : hmi::pub::kAlarmMembers)
                    add(std::string(m.name), std::string(m.type) + "  \xC2\xB7  " + std::string(hmi::pub::accessLabel(m.access)) + "  \xC2\xB7  "
                                                 + std::string(m.text),
                        Kind::InstanceVariable);
                return true;
            }
        std::set<std::string> seen;
        for (const auto& a : alarms) {
            if (!rest.empty() && !(a.local.size() > rest.size() + 1 && hmi::pub::same(std::string_view(a.local).substr(0, rest.size() + 1), rest + ".")))
                continue;
            const std::string tail = rest.empty() ? a.local : a.local.substr(rest.size() + 1);
            const std::string seg = tail.substr(0, tail.find('.'));
            if (!seen.insert(lower(seg)).second) continue;
            add(seg, "alarme  \xC2\xB7  " + a.full + (a.enabled ? std::string{} : std::string("  \xC2\xB7  d\xC3\xA9" "coch\xC3\xA9" "e")),
                Kind::Instance, true);
        }
        return true;
    }
    const std::string& objectName = objectPart;
    for (const auto& o : shown.objects) {
        if (!hmi::pub::same(o.name, objectName)) continue;
        // 1.10.2 (chantier A) : une instance de symbole - ses parametres d'abord
        // (ce qu'ils relient), puis ses proprietes.
        for (const auto& ip : hmi::pub::instanceParams(hp, o)) {
            add(ip.name, ip.type + "  \xC2\xB7  R  \xC2\xB7  param\xC3\xA8tre  \xC2\xB7  "
                             + (ip.argument.empty() ? std::string("sans argument") : (ip.given ? "relie " : "par d\xC3\xA9" "faut ") + ip.argument)
                             + (ip.description.empty() ? std::string{} : "  \xC2\xB7  " + ip.description),
                Kind::InstanceVariable, false, -10);
        }
        for (const auto& m : hmi::pub::objectMembers(o, &shown))
            add(m.name, m.type + "  \xC2\xB7  " + std::string(hmi::pub::accessLabel(m.access)) + "  \xC2\xB7  "
                            + (m.text.empty() ? (m.value.empty() ? std::string("propri\xC3\xA9t\xC3\xA9") : m.value) : m.text),
                Kind::InstanceVariable);
        // 1.11.1 (decision 108) : son groupe d'alarmes interne, puis ses alarmes (Alarmes.).
        if (hmi::pub::hasAlarmGroup(o)) {
            for (const auto& i : hmi::pub::kAlarmInfo)
                add(std::string(i.name), std::string(i.type) + "  \xC2\xB7  R  \xC2\xB7  " + std::string(i.text), Kind::InstanceVariable);
            const auto alarms = hmi::pub::objectAlarmNames(hp, *v, o);
            std::string names;
            for (std::size_t k = 0; k < alarms.size() && k < 4; ++k) names += (names.empty() ? "" : ", ") + alarms[k].local;
            if (alarms.size() > 4) names += "...";
            if (!alarms.empty())
                add(std::string(hmi::pub::kAlarmsRoot), "ses alarmes : " + names + "  \xC2\xB7  " + v->name + "." + o.name + ".Alarmes.<alarme>.Active",
                    Kind::Instance, true, -5);
        }
        return true;
    }
    return true;
}

// 1.11.2 (API-V) : le corps d'avant ; suggest() (plus bas) range ensuite chaque
// proposition (sa nature, sa provenance) - ce corps a plusieurs sorties.
static std::vector<Item> suggestItems(const hmi::Project& hp, const domain::Project* plc, std::string_view before,
                                      std::string_view prefix, bool code, bool templateText, hmi::Id op) {
    std::vector<Item> out;
    // 1.10.2 (chantier A, demande de D) : un repere ($Vanne$) n'est pas un nom.
    // Rien n'est propose - donc rien n'est remplace a Entree - apres lui ni
    // dedans : $Vanne$.F0 devenait $Vanne$.Commands_TOR16_I4_F0.
    if (afterMarker(before, prefix)) return out;
    const auto where = locate(before, templateText);
    const auto needle = lower(prefix);
    if (!code) op = hmi::kNoId;

    auto push = [&](Item it) { out.push_back(std::move(it)); };

    // Les noms dans une chaine : filtres sur tout ce qui est tape depuis le
    // guillemet, qui est aussi ce que le choix remplace.
    auto named = [&](const std::string& name, std::string detail, Kind kind) {
        const std::string typed = std::string(before.substr(where.from)) + std::string(prefix);
        const int r = matchRank(name, lower(typed));
        if (r < 0) return;
        Item it;
        it.text = name;
        it.detail = std::move(detail);
        it.kind = kind;
        it.rank = r;
        it.extend = before.size() - where.from;
        push(std::move(it));
    };

    // 1.10 (chantier K2) : ce que le script declare (fonctions internes, locales, FOR EACH),
    // lu une fois, seulement si on en a besoin.
    std::optional<Scope> scopeStore;
    const auto scope = [&]() -> const Scope& {
        if (!scopeStore) scopeStore = makeScope(hp, before, code, op);
        return *scopeStore;
    };
    // 1.10 (chantier K2) : m[ ou m[' (m une MAP du script) : les cles deja ecrites (S1).
    if (code && !before.empty() && (where.context == Context::Code || where.context == Context::Text)) {
        std::size_t open = std::string_view::npos, quote = std::string_view::npos;
        if (where.context == Context::Code && before.back() == '[') open = before.size() - 1;
        else if (where.context == Context::Text) {
            quote = before.rfind('\'');
            if (quote != std::string_view::npos && quote > 0 && before[quote - 1] == '[') open = quote - 1;
        }
        const std::string path = open == std::string_view::npos ? std::string{} : pathBefore(before.substr(0, open));
        std::string kt, vt;
        if (!path.empty() && mapTypes(scope().typeOf(path), kt, vt)) {
            const std::string typed = lower(quote == std::string_view::npos ? std::string(prefix)
                                                                            : std::string(before.substr(quote + 1)) + std::string(prefix));
            int i = 0;
            for (const auto& key : mapKeys(scope().whole, scope().clean, path)) {
                const int r = matchRank(key, typed);
                if (r < 0) continue;
                Item it;
                it.text = "'" + key + "'";
                it.detail = "cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crite" + kDot + path + " : MAP[" + kt + "] OF " + vt;
                it.kind = Kind::Type;
                it.rank = r * 100 + i++;
                it.insert = it.text;
                it.extend = quote == std::string_view::npos ? 0 : before.size() - quote;
                push(std::move(it));
            }
            if (!out.empty()) return out;
        }
    }
    // Un nombre (10, 5.0, 16#FF) n'est pas un nom : rien a proposer.
    const bool number = !prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix.front())) != 0;
    switch (where.context) {
        case Context::Comment:
        case Context::Text:
            return out;
        case Context::Code:
        case Context::Placeholder:
        case Context::Member:
        case Context::LocalType:
            if (number) return out;
            break;
        default: break;
    }
    // 1.10 (chantier K2) : "T_MODE#" - les valeurs de l'enumeration, avec leur nombre et
    // leur texte (dans l'ordre de declaration) ; rien d'autre apres un litteral type (T#, 16#).
    if (where.context == Context::Code && !before.empty() && before.back() == '#') {
        std::size_t b = before.size() - 1;
        while (b > 0 && identChar(before[b - 1])) --b;
        const auto* e = hmi::findEnumeration(hp, before.substr(b, before.size() - 1 - b));
        // 1.11.14 : NIVEAU_LOG# - les niveaux de IHM_LOG.
        if (!e && iequals(before.substr(b, before.size() - 1 - b), hmi::kLogLevelType)) {
            for (int k = 0; k < hmi::kLogLevelCount; ++k) {
                const auto level = static_cast<hmi::LogLevel>(k);
                const int r = matchRank(std::string(hmi::logLevelName(level)), needle);
                if (r < 0) continue;
                Item it;
                it.text = std::string(hmi::kLogLevelType) + "#" + std::string(hmi::logLevelName(level));
                it.detail = "niveau " + std::string(hmi::logLevelLabel(level)) + " de IHM_LOG";
                it.kind = Kind::LogLevel;
                it.rank = r * 100 + k;
                it.insert = std::string(hmi::logLevelName(level));
                push(std::move(it));
            }
            return out;
        }
        // 1.12.1 : les enumerations natives (TRANSITION#, ALIGNEMENT#...) : leurs valeurs, leur
        // nombre, leur mot.
        if (const auto* ne = e ? nullptr : hmi::natives::nativeEnum(before.substr(b, before.size() - 1 - b))) {
            int i = 0;
            for (const auto& v : ne->values) {
                const int r = matchRank(v.name, needle);
                if (r < 0) { ++i; continue; }
                Item it;
                it.text = std::string(ne->name) + "#" + std::string(v.name);
                it.detail = "= " + std::to_string(v.number) + kDot + std::string(v.text) + kDot + "native";
                it.kind = Kind::Type;
                it.rank = r * 100 + i++;
                it.insert = std::string(v.name);
                push(std::move(it));
            }
            return out;
        }
        if (!e) return out;
        const bool label = code && caseEnumeration(scope(), before.substr(0, b)) == e;   // une etiquette de CASE : "Auto: "
        int i = 0;
        for (const auto& v : e->values) {
            const int r = matchRank(v.name, needle);
            if (r < 0) { ++i; continue; }
            Item it;
            it.text = e->name + "#" + v.name;
            it.detail = valueDetail(v);
            it.kind = Kind::Type;
            it.rank = r * 100 + i++;
            it.insert = label ? v.name + ": " : v.name;
            push(std::move(it));
        }
        return out;
    }
    switch (where.context) {
        case Context::Comment:
        case Context::Text:
            return out;
        case Context::ViewName:
            for (const auto& v : hp.views)
                named(v.name, v.description.empty() ? "vue  " + std::to_string(v.width) + " x " + std::to_string(v.height)
                                                    : v.description,
                      Kind::View);
            break;
        case Context::Transition:
            for (const auto k : hmi::kTransitionKinds) named(std::string(hmi::transitionLabel(k)), "transition", Kind::Transition);
            break;
        case Context::LogLevel:
            // 1.11.14 : IHM_LOG(| - les sept niveaux, du plus bavard au plus grave.
            for (int k = 0; k < hmi::kLogLevelCount; ++k) {
                const auto level = static_cast<hmi::LogLevel>(k);
                const int r = matchRank(std::string(hmi::logLevelName(level)), lower(std::string(prefix)));
                if (r < 0) continue;
                Item it;
                it.text = std::string(hmi::logLevelName(level));
                it.detail = "niveau " + std::string(hmi::logLevelLabel(level)) + " (NIVEAU_LOG)";
                it.kind = Kind::LogLevel;
                it.rank = r * 100 + k;
                it.insert = it.text + ", '";
                it.caret = it.insert.size();
                push(std::move(it));
            }
            return out;
        case Context::LocalType:
            for (const auto& t : hmi::typereg::baseRegistry().names(hmi::typereg::UseDeclaration)) {
                const int r = matchRank(t, needle);
                if (r < 0) continue;
                Item it;
                it.text = std::string(t);
                it.detail = "type \xC3\xA9l\xC3\xA9mentaire";
                it.kind = Kind::Type;
                it.rank = r;
                it.insert = std::string(t) + ";";
                push(std::move(it));
            }
            // 1.10 (chantier K2) : les types du dialecte de S1 (MAP, ARRAY, REF_TO, POINTER TO...),
            // des la premiere lettre (sans rien de tape : les types elementaires, comme en 1.9) ;
            // les types IHM du projet, structures et enumerations (une locale de ce type).
            for (const auto& s : hmi::lang110::snippets()) {
                if (snippetKind(s.name) != Kind::Type || needle.empty()) continue;
                const int r = matchRank(s.name, needle);
                if (r >= 0) push(snippetItem(s, r + 1));
            }
            for (const auto& t : hp.programs.types) {
                const int r = matchRank(t.name, needle);
                if (r < 0) continue;
                Item it;
                it.text = t.name;
                it.detail = hmi::isEnumeration(t) ? "\xC3\xA9num\xC3\xA9ration IHM" + kDot + valuesSummary(t)
                                                  : "structure IHM" + kDot + std::to_string(t.members.size()) + (t.members.size() == 1 ? " membre" : " membres");   // 1.11 (R111, T3-11)
                it.kind = Kind::DerivedType;
                it.rank = r + 1;
                it.insert = t.name + ";";
                push(std::move(it));
            }
            break;
        case Context::ScriptName:
            for (const auto& s : hp.programs.scripts)
                named(s.name, std::string(hmi::eventLabel(s.event)) + "  \xC2\xB7  " + std::string(hmi::scriptLangKey(s.lang))
                                  + (s.description.empty() ? std::string{} : "  \xC2\xB7  " + s.description),
                      Kind::Script);
            break;
        case Context::SoundName:
            for (const auto& r : hp.assets.resources)
                if (r.kind() == hmi::MediaKind::Sound) named(r.name, r.format + (r.detail.empty() ? "" : "  \xC2\xB7  " + r.detail), Kind::Sound);
            break;
        case Context::EquipmentName: {
            // 1.9 : IHM_ESCLAVE_SIMULE(' - les equipements qui ont un esclave simule ;
            // IHM_EQUIPEMENT_OK(', IHM_EQUIPEMENT_PING(' - tous.
            const auto paren = where.from > 0 ? before.rfind('(', where.from - 1) : std::string_view::npos;
            const bool slavesOnly = paren != std::string_view::npos && calleeAt(before, paren) == "IHM_ESCLAVE_SIMULE";
            for (const auto& e : hp.equipments) {
                if (slavesOnly && !(e.modbus() && e.hasTwin())) continue;
                const std::string kind = e.simulated ? "seulement simul\xC3\xA9" : e.linkedSlave() ? "esclave simul\xC3\xA9 li\xC3\xA9" : std::string(hmi::equipmentTypeLabel(e.type));
                named(e.name, kind + "  \xC2\xB7  " + e.host + (e.modbus() && e.hasTwin() ? "  \xC2\xB7  SYS.Slave." + hmi::slaveKey(e.name) : std::string{}),
                      Kind::Equipment);
            }
            break;
        }
        case Context::Member: {
            // 1.11.1 (API-V) : API. - les unites et les globales ; API.<Unite>. - ses
            // variables (Publiques, Privees, E/S) ; API.<instance>. - ses membres.
            // Chacune avec son type et son acces (L/E, L, sans adresse), dans
            // l'ordre du modele d'API-M (Model::propose).
            if (hmi::apivars::isApiPath(where.path + ".") && !hmiOwnsApi(hp))
                if (const auto* model = apiModel(hp, plc)) {
                    int i = 0;
                    for (const auto& pr : model->propose(where.path + "." + std::string(prefix))) {
                        using PK = hmi::apivars::Proposal::Kind;
                        Item it;
                        it.text = pr.text;
                        it.detail = pr.type;
                        if (!pr.accessText.empty()) it.detail += kDot + pr.accessText;
                        if (!pr.group.empty()) it.detail += kDot + pr.group;
                        if (!pr.comment.empty()) it.detail += kDot + pr.comment;
                        it.kind = pr.kind == PK::Unit ? Kind::ApiUnit : pr.kind == PK::Root ? Kind::Api
                                : pr.kind == PK::Variable ? Kind::PlcVariable : Kind::Member;
                        it.rank = i++;
                        if (pr.kind == PK::Unit) {
                            it.insert = pr.text + ".";
                            it.chain = true;
                        }
                        push(std::move(it));
                    }
                    break;
                }
            // 1.10 (chantier K2) : un nom du script - it. (Key, Value), p^. (les membres du
            // type vise), une locale d'un type IHM (ses membres), un tableau (Length).
            if (code && dialectMembers(scope(), plc, where.path, needle, push)) break;
            // 1.10 (chantier K) : t.Length d'un tableau du script (dialecte IHM) : une
            // locale ou un parametre de fonction interne declare "t : ARRAY[...]".
            if (code && !where.path.empty() && std::all_of(where.path.begin(), where.path.end(), identChar)) {
                bool array = false;
                for (std::size_t at = before.find(where.path); at != std::string_view::npos && !array;
                     at = before.find(where.path, at + 1)) {
                    const std::size_t end = at + where.path.size();
                    if ((at > 0 && identChar(before[at - 1])) || (end < before.size() && identChar(before[end]))) continue;
                    std::size_t k = end;
                    while (k < before.size() && (before[k] == ' ' || before[k] == '\t')) ++k;
                    if (k >= before.size() || before[k] != ':' || (k + 1 < before.size() && before[k + 1] == '=')) continue;
                    ++k;
                    while (k < before.size() && (before[k] == ' ' || before[k] == '\t')) ++k;
                    array = upper(before.substr(k, 5)) == "ARRAY";
                }
                if (array) {
                    if (const int r = matchRank("Length", needle); r >= 0) {
                        Item it;
                        it.text = "Length";
                        it.detail = "DINT  \xC2\xB7  le nombre d'\xC3\xA9l\xC3\xA9ments du tableau";
                        it.kind = Kind::Member;
                        it.rank = r;
                        push(std::move(it));
                    }
                    break;
                }
            }
            // 1.11.10 : Vanne_3. (dans sa vue), Vue_Vannes.Vanne_3. (partout), SUPER. (dans le
            // symbole) - les fonctions de l'instance, en tete ; ses variables publiques suivent.
            {
                const hmi::View* sym = nullptr;
                const hmi::Object* inst = nullptr;
                hmi::InstanceAt at;
                const auto* av = hmiparams::editedView(hp);   // meme sans parametre
                if (av && hmi::isSymbolView(*av) && upper(where.path) == hmi::kSuperName) sym = av;
                else if (av && hmi::instanceAt(hp, av->name + "." + where.path, at)) { sym = at.symbol; inst = &at.instance; }
                else if (hmi::instanceAt(hp, where.path, at)) { sym = at.symbol; inst = &at.instance; }
                if (const auto* owner = av ? hmi::popupOwner(hp, *av) : nullptr; !sym && owner) {
                    hmi::InstanceAt in;
                    if (hmi::instanceAt(hp, std::string(hmi::kInstanceAlias) + "." + where.path, in)) sym = in.symbol;
                }
                for (const auto& f : sym ? sym->functions : std::vector<hmi::HmiFunction>{}) {
                    const int r = matchRank(f.name, needle);
                    if (r < 0) continue;
                    Item it = userFunctionItem(f, r);
                    const bool over = inst && f.isVirtual && hmi::functionOverride(*inst, f.name);
                    it.detail = "fonction de " + sym->name + "  \xC2\xB7  " + (f.returnType.empty() ? std::string("sans retour") : f.returnType)
                              + (over ? "  \xC2\xB7  red\xC3\xA9" "finie ici" : f.isVirtual ? "  \xC2\xB7  virtuelle" : "");
                    push(std::move(it));
                }
            }
            // 1.9 : Moteur. - les membres du type declare d'un parametre de la popup.
            if (const auto* pv = hmiparams::assistView(hp)) {
                const auto items = hmiparams::assistItems(hp, *pv, where.path + "." + std::string(prefix), plc);
                if (!items.empty() || pv->param(where.path)) {
                    for (const auto& [name, detail] : items) {
                        Item it;
                        it.text = name;
                        it.detail = detail;
                        it.kind = Kind::Member;
                        it.rank = matchRank(name, needle);
                        push(std::move(it));
                    }
                    if (!items.empty()) break;
                }
            }
            // Lot 9 : SYS., Vue., Vue.Objet. - les variables publiques.
            if (publicMembers(hp, where.path, needle, push)) break;
            // Lot 16 : Four1., Fours[i].Vannes., Consignes. - membres et proprietes.
            if (hmiCompositeMembers(hp, where.path, needle, push)) break;
            if (!plc) break;
            std::vector<MemberInfo> list;
            if (!members(*plc, where.path, list)) break;
            for (const auto& m : list) {
                const int r = matchRank(m.name, needle);
                if (r < 0) continue;
                Item it;
                it.text = m.name;
                it.detail = m.type + (m.comment.empty() ? std::string{} : "  \xC2\xB7  " + m.comment);
                it.kind = Kind::Member;
                it.rank = r;
                push(std::move(it));
            }
            break;
        }
        case Context::Placeholder:
        case Context::Code: {
            const bool full = code && where.context == Context::Code;
            // 1.10.1 (U2) : le script d'un operateur - a, b, Resultat (et TO_X) en tete,
            // chacun avec son type et ce qu'il est (Ctrl+Espace sans rien de tape aussi).
            if (full && op != hmi::kNoId) {
                int i = 0;
                for (const auto& n : scope().opNames) {
                    const int r = matchRank(n.name, needle);
                    if (r < 0) {
                        ++i;
                        continue;
                    }
                    Item it;
                    it.text = n.name;
                    it.detail = n.type + kDot + shortRole(n.role);
                    it.kind = Kind::LocalVariable;
                    it.rank = -3000 + r * 10 + i++;
                    push(std::move(it));
                }
            }
            // Lot 7 : les variables locales du script d'abord (un nom local
            // cache la variable IHM ou de l'automate du meme nom).
            if (code) {
                // 1.10 (K2) : une locale d'un type IHM (structure, enumeration) aussi.
                const auto parts = hmi::splitDeclarations(before, true, [&hp](std::string_view t) { return lang::knownHmiType(&hp, t); });
                for (const auto& l : parts.locals) {
                    const int r = matchRank(l.name, needle);
                    if (r < 0) continue;
                    Item it;
                    it.text = l.name;
                    it.detail = localDetail(l);
                    it.kind = Kind::LocalVariable;
                    it.rank = r * 2;
                    push(std::move(it));
                }
            }
            // 1.9 : les parametres de la popup d'abord, avec leur type et leur mode.
            if (const auto* pv = hmiparams::assistView(hp))
                for (const auto& [name, detail] : hmiparams::assistItems(hp, *pv, std::string(prefix), plc)) {
                    const int r = matchRank(name, needle);
                    if (r < 0) continue;
                    Item it;
                    it.text = name;
                    it.detail = detail;
                    it.kind = Kind::LocalVariable;
                    it.rank = r * 2 - 1;
                    push(std::move(it));
                }
            for (const auto& v : hp.programs.variables) {
                const int r = matchRank(v.name, needle);
                if (r < 0) continue;
                Item it;
                it.text = v.name;
                it.detail = hmiVariableDetail(v);
                it.kind = Kind::HmiVariable;
                it.rank = r * 2;
                push(std::move(it));
            }
            // 1.11.1 (API-V, decision 104) : API, les variables de l'automate
            // (API.<globale>, API.<Unite>.<variable>, les instances DDT et DFB).
            // Des A ou AP, en tete - avant IHM_APPELER, MAP... ; le point pose,
            // la liste se rouvre sur les unites et les globales.
            if (const int r = matchRank("API", needle); r >= 0 && !needle.empty() && core::hasApi()) {   // 1.12.0 : pas dans XPGAnalyser IHM
                Item it;
                it.text = "API";
                it.detail = "variables de l'automate : API.<globale>, API.<Unit\xC3\xA9>.<variable>, instances DDT et DFB";
                it.kind = Kind::Api;
                it.rank = r == 0 ? -2000 : r * 2 + 1;
                it.insert = "API.";
                it.chain = true;
                push(std::move(it));
            }
            // Lot 9 : SYS (les variables systeme) et les vues (leurs objets et
            // leurs variables) ; le point pose, la liste se rouvre.
            if (const int r = matchRank("SYS", needle); r >= 0 && !needle.empty()) {
                Item it;
                it.text = "SYS";
                it.detail = "variables syst\xC3\xA8me : SYS.UserName, SYS.CurrentView, SYS.Hour...";
                it.kind = Kind::SysVariable;
                it.rank = r * 2 + 1;
                it.insert = "SYS.";
                it.chain = true;
                push(std::move(it));
            }
            if (!needle.empty())
                for (const auto& v : hp.views) {
                    const int r = matchRank(v.name, needle);
                    if (r < 0) continue;
                    Item it;
                    it.text = v.name;
                    it.detail = "vue  \xC2\xB7  ses objets et leurs variables : " + v.name + ".Objet.Visible...";
                    it.kind = Kind::Instance;
                    it.rank = r * 2 + 1;
                    it.insert = v.name + ".";
                    it.chain = true;
                    push(std::move(it));
                }
            if (full)
                for (const auto& f : functions()) {
                    const int r = matchRank(f.name, needle);
                    if (r < 0) continue;
                    Item it;
                    it.text = std::string(f.name);
                    it.detail = "fonction IHM  \xC2\xB7  " + std::string(f.help);
                    it.kind = Kind::HmiFunction;
                    it.rank = r * 2;
                    it.insert = std::string(f.snippet);
                    if (const auto bar = it.insert.find('|'); bar != std::string::npos) {
                        it.insert.erase(bar, 1);
                        it.caret = bar;
                    }
                    it.chain = f.chain;
                    push(std::move(it));
                }
            // 1.12.0 : les natives de l'IHM (le catalogue de la branche Natives) - hors IHM_
            // (au-dessus) et hors dialecte (MAP_, REF : dialectCode) ; dans un champ, celles
            // qui s'ecrivent dans une expression.
            for (const auto& f : hmi::natives::functions()) {
                if (f.name.rfind("IHM_", 0) == 0 || f.category == "map" || f.category == "ref" || lang::isBuiltin(f.name)) continue;   // le dialecte les propose (dialectCode)
                if (!full && !f.expression) continue;
                const int r = matchRank(f.name, needle);
                if (r < 0) continue;
                push(nativeItem(f, r * 2 + 1));       // a egalite avec la bibliotheque, la native (poussee avant) reste
            }
            // 1.12.1 : les enumerations natives par leur nom (ALIGNEMENT# : puis ses valeurs) - par
            // leur debut seulement (Marche ne propose pas MODE_MARCHE# : Entree le prendrait).
            if (needle.size() >= 2)
                for (const auto& ne : hmi::natives::enums()) {
                    const int r = matchRank(ne.name, needle);
                    if (r != 0) continue;
                    Item it;
                    it.text = std::string(ne.name) + "#";
                    it.detail = "\xC3\xA9" "num\xC3\xA9" "ration native" + kDot + std::string(ne.summary);
                    it.kind = Kind::Type;
                    it.rank = r * 2 + 1;
                    it.insert = it.text;
                    it.chain = true;
                    push(std::move(it));
                }
            // 1.10 (chantier K2) : le langage de S1 (fonctions internes du script, MAP_...,
            // modeles), les operateurs de S2 (TO_xxx, + - ...) et les enumerations (valeurs
            // d'abord dans un CASE ou apres m :=, le CASE avec toutes les valeurs, FOR EACH).
            if (where.context == Context::Code) dialectCode(scope(), before, needle, full, push);
            // Lot 7 : les fonctions IHM du projet (aussi dans les champs : une
            // expression de vue peut appeler celles qui rendent une valeur).
            for (const auto& f : hp.programs.functions) {
                if (!full && f.returnType.empty()) continue;
                const int r = matchRank(f.name, needle);
                if (r < 0) continue;
                push(userFunctionItem(f, r * 2));
            }
            // 1.11.10 : dans un symbole (ou une de ses popups), ses fonctions par leur nom.
            if (const auto* av = hmiparams::editedView(hp)) {   // meme sans parametre
                const hmi::View* sym = hmi::isSymbolView(*av) ? av : hmi::popupOwner(hp, *av);
                for (const auto& f : sym ? sym->functions : std::vector<hmi::HmiFunction>{}) {
                    if (!full && f.returnType.empty()) continue;
                    const int r = matchRank(f.name, needle);
                    if (r < 0) continue;
                    Item it = userFunctionItem(f, r * 2);
                    it.detail = "fonction de " + sym->name + "  \xC2\xB7  " + (f.returnType.empty() ? std::string("sans retour") : f.returnType)
                              + (f.isVirtual ? "  \xC2\xB7  virtuelle" : "");
                    push(std::move(it));
                }
            }
            if (plc) {
                for (const auto& s : project::suggestionsFor(*plc, domain::kNoIndex, prefix)) {
                    Kind kind = Kind::PlcVariable;
                    switch (s.kind) {
                        case project::Suggestion::Kind::Variable:        kind = Kind::PlcVariable; break;
                        case project::Suggestion::Kind::LocatedVariable: kind = Kind::PlcLocated; break;
                        case project::Suggestion::Kind::Function:        kind = Kind::Function; break;
                        case project::Suggestion::Kind::FunctionBlock:   kind = Kind::Block; break;
                        case project::Suggestion::Kind::DerivedType:     kind = Kind::DerivedType; break;
                        case project::Suggestion::Kind::Type:            kind = Kind::Type; break;
                        case project::Suggestion::Kind::Keyword:
                            kind = s.insert.empty() ? Kind::Keyword : Kind::Structure;
                            break;
                    }
                    const bool variable = kind == Kind::PlcVariable || kind == Kind::PlcLocated;
                    // 1.12.0 : XPGAnalyser IHM - le langage (mots, modeles, types), pas l'automate :
                    // ni ses variables, ni ses DDT, ni les blocs et fonctions de sa bibliotheque
                    // (les fonctions de l'IHM sont ses natives, proposees plus haut).
                    if (!core::hasApi() && (variable || kind == Kind::Function || kind == Kind::Block || kind == Kind::DerivedType)) continue;
                    // Un champ : des variables et les operateurs d'une expression.
                    if (!full && !variable) {
                        if (where.context == Context::Placeholder || kind != Kind::Keyword) continue;
                        static const char* kOps[] = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE"};
                        if (std::none_of(std::begin(kOps), std::end(kOps), [&](const char* k) { return s.text == k; })) continue;
                    }
                    Item it;
                    it.text = s.text;
                    it.detail = variable ? s.detail + "  \xC2\xB7  automate" : s.detail;
                    it.kind = kind;
                    it.rank = s.rank * 2 + 1;
                    it.insert = s.insert;
                    it.caret = s.caret;
                    push(std::move(it));
                }
            }
            break;
        }
    }

    // Le meme nom deux fois (une variable IHM et une globale de l'automate) :
    // c'est la variable IHM que le script lit, elle seule reste.
    std::stable_sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return lower(a.text) < lower(b.text);
    });
    std::vector<Item> unique;
    unique.reserve(out.size());
    for (auto& it : out) {
        const bool seen = std::any_of(unique.begin(), unique.end(), [&](const Item& u) { return iequals(u.text, it.text); });
        if (!seen) unique.push_back(std::move(it));
    }
    return unique;
}

// ---- 1.11.2 (API-V, decisions 161 et 163) : la nature et la provenance d'une proposition ----
//  LES DEUX TABLES : les pictogrammes, les couleurs et les mots ne vivent qu'ici.
//  C'est la scene 4 de la maquette de MQ5 (« Icones de saisie ») : la forme dit ce
//  que c'est (un pictogramme au trait, dans un carre de 16 x 16, ses chemins SVG
//  recopies), la couleur dit d'ou ca vient. Trois natures que la maquette ne montre
//  pas prennent ses icones d'onglets : l'unite de programme (« API · Unites de
//  programme »), la racine API (« API ») ; le son est dessine dans le meme trait.
namespace {
using Seg = ui::KindBadge::Seg;
using Dot = ui::KindBadge::Dot;
using DK = ui::KindBadge::DotKind;
using Picto = ui::KindBadge::Picto;
template <std::size_t N, std::size_t M>
constexpr Picto picto(const Seg (&s)[N], const Dot (&d)[M]) { return { s, N, d, M }; }
template <std::size_t N>
constexpr Picto picto(const Seg (&s)[N]) { return { s, N, nullptr, 0 }; }

// variable : M2.5 4.5h11v7h-11z + un carre plein (4.6, 6.6, 2.8)
constexpr Seg kVarS[] = { {2.5f, 4.5f, 13.5f, 4.5f}, {13.5f, 4.5f, 13.5f, 11.5f}, {13.5f, 11.5f, 2.5f, 11.5f}, {2.5f, 11.5f, 2.5f, 4.5f} };
constexpr Dot kVarD[] = { {6.f, 8.f, 1.4f, DK::Square} };
// membre : M3 2.5V10h3 M6.5 7.5h7v6h-7z
constexpr Seg kMemS[] = { {3.f, 2.5f, 3.f, 10.f}, {3.f, 10.f, 6.f, 10.f},
                          {6.5f, 7.5f, 13.5f, 7.5f}, {13.5f, 7.5f, 13.5f, 13.5f}, {13.5f, 13.5f, 6.5f, 13.5f}, {6.5f, 13.5f, 6.5f, 7.5f} };
// constante : M2.5 4.5h11v7h-11z M5.5 7h5 M5.5 9h5
constexpr Seg kConS[] = { {2.5f, 4.5f, 13.5f, 4.5f}, {13.5f, 4.5f, 13.5f, 11.5f}, {13.5f, 11.5f, 2.5f, 11.5f}, {2.5f, 11.5f, 2.5f, 4.5f},
                          {5.5f, 7.f, 10.5f, 7.f}, {5.5f, 9.f, 10.5f, 9.f} };
// type : M6.5 2.5H4.5v11h2 M9.5 2.5h2v11h-2 ; valeur d'enumeration : le meme et un disque (8, 8, 1.4)
constexpr Seg kTypS[] = { {6.5f, 2.5f, 4.5f, 2.5f}, {4.5f, 2.5f, 4.5f, 13.5f}, {4.5f, 13.5f, 6.5f, 13.5f},
                          {9.5f, 2.5f, 11.5f, 2.5f}, {11.5f, 2.5f, 11.5f, 13.5f}, {11.5f, 13.5f, 9.5f, 13.5f} };
constexpr Dot kEnuD[] = { {8.f, 8.f, 1.4f, DK::Disc} };
// fonction : M4 4h8v8H4z M1.5 8H4 M12 8h2.5 ; methode : la meme et un disque (8, 8, 1.7)
constexpr Seg kFunS[] = { {4.f, 4.f, 12.f, 4.f}, {12.f, 4.f, 12.f, 12.f}, {12.f, 12.f, 4.f, 12.f}, {4.f, 12.f, 4.f, 4.f},
                          {1.5f, 8.f, 4.f, 8.f}, {12.f, 8.f, 14.5f, 8.f} };
constexpr Dot kMetD[] = { {8.f, 8.f, 1.7f, DK::Disc} };
// mot-cle (une cle) : un cercle (5, 8, 2.5) M7.5 8h6.5 M12 8v2.5 M14 8v2
constexpr Seg kKeyS[] = { {7.5f, 8.f, 14.f, 8.f}, {12.f, 8.f, 12.f, 10.5f}, {14.f, 8.f, 14.f, 10.f} };
constexpr Dot kKeyD[] = { {5.f, 8.f, 2.5f, DK::Ring} };
// vue : M2 3h12v10H2z M2 5.5h12
constexpr Seg kVueS[] = { {2.f, 3.f, 14.f, 3.f}, {14.f, 3.f, 14.f, 13.f}, {14.f, 13.f, 2.f, 13.f}, {2.f, 13.f, 2.f, 3.f}, {2.f, 5.5f, 14.f, 5.5f} };
// objet (un cube) : M8 2.5l5 2.5v6L8 13.5 3 11V5z M3 5l5 2.5L13 5 M8 7.5v6
constexpr Seg kObjS[] = { {8.f, 2.5f, 13.f, 5.f}, {13.f, 5.f, 13.f, 11.f}, {13.f, 11.f, 8.f, 13.5f}, {8.f, 13.5f, 3.f, 11.f},
                          {3.f, 11.f, 3.f, 5.f}, {3.f, 5.f, 8.f, 2.5f}, {3.f, 5.f, 8.f, 7.5f}, {8.f, 7.5f, 13.f, 5.f}, {8.f, 7.5f, 8.f, 13.5f} };
// propriete d'objet (deux curseurs) : M2.5 5h11 M2.5 11h11, deux disques (6, 5) et (10.5, 11), 1.7
constexpr Seg kProS[] = { {2.5f, 5.f, 13.5f, 5.f}, {2.5f, 11.f, 13.5f, 11.f} };
constexpr Dot kProD[] = { {6.f, 5.f, 1.7f, DK::Disc}, {10.5f, 11.f, 1.7f, DK::Disc} };
// alarme : M8 2.5L14.5 13.5h-13z M8 6.5v3.5, un disque (8, 11.6, 0.7)
constexpr Seg kAlaS[] = { {8.f, 2.5f, 14.5f, 13.5f}, {14.5f, 13.5f, 1.5f, 13.5f}, {1.5f, 13.5f, 8.f, 2.5f}, {8.f, 6.5f, 8.f, 10.f} };
constexpr Dot kAlaD[] = { {8.f, 11.6f, 0.7f, DK::Disc} };
// script : M5.5 4.5L2 8l3.5 3.5 M10.5 4.5L14 8l-3.5 3.5 M9.2 3.5l-2.4 9
constexpr Seg kScrS[] = { {5.5f, 4.5f, 2.f, 8.f}, {2.f, 8.f, 5.5f, 11.5f}, {10.5f, 4.5f, 14.f, 8.f}, {14.f, 8.f, 10.5f, 11.5f},
                          {9.2f, 3.5f, 6.8f, 12.5f} };
// unite de programme (l'onglet « API · Unites de programme ») : M2 3h12v10H2z M4.5 6.5L6.5 8l-2 1.5 M8 10h3
constexpr Seg kUniS[] = { {2.f, 3.f, 14.f, 3.f}, {14.f, 3.f, 14.f, 13.f}, {14.f, 13.f, 2.f, 13.f}, {2.f, 13.f, 2.f, 3.f},
                          {4.5f, 6.5f, 6.5f, 8.f}, {6.5f, 8.f, 4.5f, 9.5f}, {8.f, 10.f, 11.f, 10.f} };
// la racine API (l'onglet « API ») : M2 4h12v8H2z M4.5 6.5v3 M7 6.5v3 M9.5 6.5h2
constexpr Seg kApiS[] = { {2.f, 4.f, 14.f, 4.f}, {14.f, 4.f, 14.f, 12.f}, {14.f, 12.f, 2.f, 12.f}, {2.f, 12.f, 2.f, 4.f},
                          {4.5f, 6.5f, 4.5f, 9.5f}, {7.f, 6.5f, 7.f, 9.5f}, {9.5f, 6.5f, 11.5f, 6.5f} };
// son (un haut-parleur, absent de la maquette) : M2.5 6h2.5l3.5-3v10l-3.5-3H2.5z, deux ondes
constexpr Seg kSonS[] = { {2.5f, 6.f, 5.f, 6.f}, {5.f, 6.f, 8.5f, 3.f}, {8.5f, 3.f, 8.5f, 13.f}, {8.5f, 13.f, 5.f, 10.f},
                          {5.f, 10.f, 2.5f, 10.f}, {2.5f, 10.f, 2.5f, 6.f},
                          {10.5f, 6.f, 11.5f, 8.f}, {11.5f, 8.f, 10.5f, 10.f}, {12.5f, 4.5f, 14.f, 8.f}, {14.f, 8.f, 12.5f, 11.5f} };

constexpr Picto kPictos[kNatureCount] = {
    picto(kVarS, kVarD), picto(kMemS), picto(kConS), picto(kTypS), picto(kTypS, kEnuD), picto(kFunS), picto(kFunS, kMetD),
    picto(kKeyS, kKeyD), picto(kVueS), picto(kObjS), picto(kProS, kProD), picto(kAlaS, kAlaD), picto(kScrS),
    picto(kUniS), picto(kApiS), picto(kSonS),
};
constexpr NatureLook kNatures[kNatureCount] = {
    {"variable", &kPictos[0]},                                            // Variable
    {"membre", &kPictos[1]},                                              // Member
    {"constante", &kPictos[2]},                                           // Constant
    {"type", &kPictos[3]},                                                // Type
    {"valeur d'\xC3\xA9num\xC3\xA9ration", &kPictos[4]},                     // EnumValue
    {"fonction", &kPictos[5]},                                            // Function
    {"m\xC3\xA9thode", &kPictos[6]},                                        // Method
    {"mot-cl\xC3\xA9", &kPictos[7]},                                        // Keyword
    {"vue", &kPictos[8]},                                                 // View
    {"objet", &kPictos[9]},                                               // Object
    {"propri\xC3\xA9t\xC3\xA9 d'objet", &kPictos[10]},                        // Property
    {"alarme", &kPictos[11]},                                             // Alarm
    {"script", &kPictos[12]},                                             // Script
    {"unit\xC3\xA9 de programme", &kPictos[13]},                            // Unit
    {"variables de l'automate", &kPictos[14]},                            // Root (API)
    {"son", &kPictos[15]},                                                // Resource
};
// Les couleurs de la maquette (decision 163).
constexpr OriginLook kOrigins[kOriginCount] = {
    {"automate (API.)", "automate", gfx::Color::rgb(0x60A5FA)},                 // Plc
    {"IHM", "IHM", gfx::Color::rgb(0x2DD4BF)},                                  // Hmi
    {"objet", "objet", gfx::Color::rgb(0xC084FC)},                              // Object : le violet des methodes
    {"syst\xC3\xA8me (SYS.)", "syst\xC3\xA8me", gfx::Color::rgb(0xF5B75A)},          // System
    {"standard CEI 61131-3", "standard", gfx::Color::rgb(0xA0A0A0)},            // Iec
};
// La cle des couleurs du pied de la liste : dans l'ordre de la maquette.
const ui::BadgeKeyEntry kKey[kOriginCount] = {
    {kOrigins[0].key, kOrigins[0].color}, {kOrigins[1].key, kOrigins[1].color}, {kOrigins[2].key, kOrigins[2].color},
    {kOrigins[3].key, kOrigins[3].color}, {kOrigins[4].key, kOrigins[4].color},
};
[[maybe_unused]] const bool kKeySet = (ui::setBadgeKey(kKey, kOriginCount), true);

std::string_view firstSegment(std::string_view path) {
    const auto dot = path.find_first_of(".[");
    return dot == std::string_view::npos ? path : path.substr(0, dot);
}

// La provenance des membres d'un chemin ("API.Armoires[0]", "Recette", "SYS.Slave.E1", "Vue.Objet").
Origin memberOrigin(const hmi::Project& hp, std::string_view path) {
    const auto root = firstSegment(path);
    if (hmi::pub::isSysRoot(root)) return Origin::System;
    if (iequals(root, "API") && !hmiOwnsApi(hp)) return Origin::Plc;
    for (const auto& v : hp.programs.variables) if (iequals(v.name, root)) return Origin::Hmi;
    if (hmi::pub::viewNamed(hp, root)) return Origin::Object;
    return Origin::Plc;   // un nom nu de l'automate (Armoires[0].ana)
}

void classify(Item& it, const hmi::Project& hp, const domain::Project* plc, const Where& where) {
    const bool member = where.context == Context::Member;
    const auto set = [&](Nature n, Origin o) { it.nature = n; it.origin = o; };
    switch (it.kind) {
        case Kind::HmiVariable:   set(Nature::Variable, Origin::Hmi); break;
        case Kind::PlcVariable:
        case Kind::PlcLocated: {
            const auto* g = plc ? globalVariable(*plc, it.text) : nullptr;
            const bool constant = g && g->scope == domain::VariableScope::Constant
                                  && (!member || iequals(where.path, "API"));
            set(constant ? Nature::Constant : Nature::Variable, Origin::Plc);
            break;
        }
        case Kind::Member:        set(Nature::Member, member ? memberOrigin(hp, where.path) : Origin::Plc); break;
        case Kind::HmiFunction:   set(Nature::Function, Origin::Hmi); break;
        case Kind::Function:      // une fonction du dialecte de l'IHM (MAP_GET, REF...) : IHM ; sinon le standard (LIMIT)
            set(Nature::Function, lang::isBuiltin(it.text) ? Origin::Hmi : Origin::Iec);
            break;
        case Kind::Block:
            set(Nature::Function, project::BlockLibrary::shared().find(it.text) ? Origin::Iec : Origin::Plc);
            break;
        case Kind::Keyword:
        case Kind::Structure:     // TRUE, FALSE : des constantes du standard (la maquette)
            set(iequals(it.text, "TRUE") || iequals(it.text, "FALSE") ? Nature::Constant : Nature::Keyword, Origin::Iec);
            break;
        case Kind::Type:
            if (const auto hash = it.text.find('#'); hash != std::string::npos && hash > 0)
                // T_MODE#Auto : une valeur d'enumeration, de l'IHM ou de l'automate.
                set(Nature::EnumValue, lang::knownHmiType(&hp, std::string_view(it.text).substr(0, hash)) ? Origin::Hmi : Origin::Plc);
            else if (!it.text.empty() && it.text.front() == '\'')
                set(Nature::Constant, Origin::Hmi);              // la cle deja ecrite d'une MAP du script
            else
                set(Nature::Type, Origin::Iec);                  // BOOL, INT, TIME...
            break;
        case Kind::DerivedType:   set(Nature::Type, lang::knownHmiType(&hp, it.text) ? Origin::Hmi : Origin::Plc); break;
        case Kind::View:          set(Nature::View, Origin::Hmi); break;
        case Kind::Script:        set(Nature::Script, Origin::Hmi); break;
        case Kind::Sound:         set(Nature::Resource, Origin::Hmi); break;
        case Kind::Transition:    set(Nature::EnumValue, Origin::Hmi); break;
        case Kind::LocalVariable: set(Nature::Variable, Origin::Hmi); break;
        case Kind::UserFunction:  set(Nature::Function, Origin::Hmi); break;
        case Kind::SysVariable:   set(Nature::Variable, Origin::System); break;
        case Kind::Instance:
            if (it.detail.rfind("alarme", 0) == 0) set(Nature::Alarm, Origin::Object);          // Vue.Objet.Alarmes.<alarme>
            else if (member && hmi::pub::isSysRoot(firstSegment(where.path))) set(Nature::Object, Origin::System);
            else set(Nature::Object, Origin::Hmi);                                                // Vue.<objet>
            break;
        case Kind::InstanceVariable: set(Nature::Property, Origin::Object); break;
        case Kind::Equipment:     set(Nature::Object, Origin::Hmi); break;
        case Kind::Api:           set(Nature::Root, Origin::Plc); break;
        case Kind::ApiUnit:       set(Nature::Unit, Origin::Plc); break;
        case Kind::LogLevel:      set(Nature::EnumValue, Origin::Hmi); break;      // 1.11.14
    }
}
} // namespace

const NatureLook& lookOf(Nature n) noexcept {
    const auto i = static_cast<std::size_t>(n);
    return kNatures[i < kNatureCount ? i : 0];
}
const OriginLook& lookOf(Origin o) noexcept {
    const auto i = static_cast<std::size_t>(o);
    return kOrigins[i < kOriginCount ? i : 0];
}

ui::KindBadge badgeOf(Nature n, Origin o) {
    const auto& nl = lookOf(n);
    const auto& ol = lookOf(o);
    ui::KindBadge b;
    b.picto = nl.picto;
    b.color = ol.color;
    b.legend = std::string(nl.label) + "  \xC2\xB7  " + std::string(ol.label);
    b.accent = n == Nature::Method;     // decision 156 : une methode d'objet, en violet
    return b;
}
ui::KindBadge badgeOf(const Item& it) { return badgeOf(it.nature, it.origin); }

std::vector<Item> suggest(const hmi::Project& hp, const domain::Project* plc, std::string_view before,
                          std::string_view prefix, bool code, bool templateText, hmi::Id op) {
    auto items = suggestItems(hp, plc, before, prefix, code, templateText, op);
    if (items.empty()) return items;
    const auto where = locate(before, templateText);
    for (auto& it : items) classify(it, hp, plc, where);
    return items;
}

ui::Icon iconOf(Kind k) noexcept {
    switch (k) {
        case Kind::HmiVariable: return ui::Icon::Screen;
        case Kind::PlcVariable: return ui::Icon::Variable;
        case Kind::PlcLocated:  return ui::Icon::LocatedVariable;
        case Kind::Member:      return ui::Icon::Variable;
        case Kind::HmiFunction: return ui::Icon::Code;
        case Kind::Function:
        case Kind::Block:       return ui::Icon::FunctionBlock;
        case Kind::Keyword:
        case Kind::Structure:   return ui::Icon::Section;
        case Kind::Type:        return ui::Icon::Constant;
        case Kind::DerivedType: return ui::Icon::DerivedType;
        case Kind::View:        return ui::Icon::Screen;
        case Kind::Script:      return ui::Icon::Code;
        case Kind::Sound:       return ui::Icon::Play;
        case Kind::Transition:  return ui::Icon::Refresh;
        case Kind::LocalVariable: return ui::Icon::Variable;
        case Kind::UserFunction:  return ui::Icon::FunctionBlock;
        case Kind::SysVariable:   return ui::Icon::Settings;
        case Kind::Instance:      return ui::Icon::Screen;
        case Kind::InstanceVariable: return ui::Icon::Variable;
        case Kind::Equipment:        return ui::Icon::Network;      // 1.9
        case Kind::Api:              return ui::Icon::Cpu;          // 1.11.1 (API-V) : l'automate
        case Kind::ApiUnit:          return ui::Icon::Program;      // 1.11.1 (API-V) : une unite de programme
        case Kind::LogLevel:         return ui::Icon::Constant;     // 1.11.14 : un niveau de IHM_LOG
    }
    return ui::Icon::None;
}

void toCompletions(const std::vector<Item>& items, std::vector<ui::MultiLineText::Completion>& out) {
    for (const auto& it : items) {
        ui::MultiLineText::Completion c;
        c.text = it.text;
        c.detail = it.detail;
        c.icon = iconOf(it.kind);
        c.badge = badgeOf(it);          // 1.11.2 (API-V, decision 161)
        c.rank = it.rank;
        c.insert = it.insert;
        c.caret = it.caret;
        c.extend = it.extend;
        c.chain = it.chain;
        out.push_back(std::move(c));
    }
}

// ------------------------------------------------------------ fonctions IHM ---
const std::vector<Function>& functions() {
    static const std::vector<Function> all = makeFunctions();
    return all;
}

const Function* function(std::string_view name) noexcept {
    for (const auto& f : functions()) if (iequals(f.name, name)) return &f;
    return nullptr;
}

namespace {
// 1.11.20 : la bulle d'une fonction de l'utilisateur - tous ses parametres (E/S et sorties dites), leurs
// valeurs par defaut, son retour.
ui::MultiLineText::Signature bubbleOf(const hmi::HmiFunction& f) {
    ui::MultiLineText::Signature out;
    out.name = f.name;
    const auto parts = hmi::splitDeclarations(hmi::decl::codeOf(f), true, [](std::string_view) { return true; });
    for (const auto* in : parts.parameters())
        out.parameters.push_back(std::string(in->section == hmi::LocalVar::Section::InOut    ? "VAR_IN_OUT "
                                             : in->section == hmi::LocalVar::Section::Output ? "VAR_OUTPUT "
                                                                                             : "")
                                 + in->name + " : " + in->type + (in->initial.empty() ? std::string{} : " := " + in->initial));
    out.returns = f.returnType;
    return out;
}
} // namespace

bool signature(const hmi::Project* hp, const domain::Project* plc, std::string_view name,
               ui::MultiLineText::Signature& out) {
    if (hp)
        if (const auto fs = hp->functionsNamed(name); !fs.empty()) {
            out = bubbleOf(*fs.front());
            for (std::size_t k = 1; k < fs.size(); ++k) out.overloads.push_back(bubbleOf(*fs[k]));   // 1.11.20 : ses surcharges
            return true;
        }
    return signature(plc, name, out);
}

bool signature(const domain::Project* plc, std::string_view name, ui::MultiLineText::Signature& out) {
    if (const auto* f = function(name)) {
        out.name = std::string(f->name);
        out.parameters.clear();
        for (const auto p : f->parameters) out.parameters.emplace_back(p);
        out.returns = std::string(f->returns);
        return true;
    }
    // 1.12.0 : une native de l'IHM (LIMIT, CONCAT, RGB...) : son catalogue - avant la
    // bibliotheque de l'automate (XPGAnalyser IHM n'a qu'elle).
    if (const auto* n = hmi::natives::function(name); n && std::string_view(n->name).rfind("IHM_", 0) != 0) {
        out.name = std::string(n->name);
        out.parameters.clear();
        for (const auto& p : n->params)
            out.parameters.push_back(std::string(p.name) + " : " + std::string(p.type) + (p.optional ? " (facultatif)" : ""));
        out.returns = std::string(n->returns);
        return true;
    }
    if (!plc) return false;
    project::CallSignature sig;
    if (!project::signatureFor(*plc, name, sig)) return false;
    out.name = std::move(sig.name);
    out.parameters = std::move(sig.parameters);
    out.returns = std::move(sig.returns);
    return true;
}

// ------------------------------------------------------------- description ---
bool members(const domain::Project& plc, std::string_view path, std::vector<MemberInfo>& out) {
    domain::TypeRef type;
    bool pendingArray = false;
    if (!resolve(plc, path, type, pendingArray)) return false;
    if (pendingArray) return false;
    const auto fields = fieldsOf(plc, type);
    if (fields.empty()) return false;
    for (const auto& f : fields) {
        MemberInfo m;
        m.name = f.name;
        m.type = f.type;
        m.comment = f.comment;
        m.structured = f.var && (f.var->type.derivedIndex != domain::kNoIndex || f.var->type.fbTypeIndex != domain::kNoIndex);
        out.push_back(std::move(m));
    }
    return true;
}

// ---------------------------------------------------- lot 12 : les variables ---
std::vector<hmi::design::VarInfo> designMembers(const domain::Project& plc, std::string_view path) {
    std::vector<hmi::design::VarInfo> out;
    std::vector<MemberInfo> list;
    if (!members(plc, path, list)) return out;
    for (const auto& m : list) {
        hmi::design::VarInfo v;
        v.name = m.name;
        v.type = m.type;
        v.comment = m.comment;
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<hmi::design::VarInfo> designVariables(const hmi::Project& hp, const domain::Project* plc) {
    std::vector<hmi::design::VarInfo> out;
    if (plc) {
        for (const auto& var : plc->variables) {
            if (var.scope != domain::VariableScope::Global) continue;
            const std::string name(plc->strings.text(var.name));
            if (name.empty()) continue;
            const std::string comment(plc->strings.text(var.comment));
            const auto& t = var.type;
            if (t.klass == domain::TypeClass::Array) {
                // Un petit tableau : element par element (Armoires[0], Armoires[1]).
                if (t.arrayHigh < t.arrayLow || t.arrayHigh - t.arrayLow >= 16) continue;
                const std::string element(plc->strings.text(t.elementType));
                for (std::int64_t i = t.arrayLow; i <= t.arrayHigh; ++i) {
                    hmi::design::VarInfo v;
                    v.name = name + "[" + std::to_string(i) + "]";
                    v.type = element;
                    v.comment = comment;
                    v.members = designMembers(*plc, v.name);
                    out.push_back(std::move(v));
                }
                continue;
            }
            hmi::design::VarInfo v;
            v.name = name;
            v.type = std::string(plc->strings.text(t.name));
            v.comment = comment;
            if (t.klass == domain::TypeClass::Derived || t.klass == domain::TypeClass::FunctionBlock) v.members = designMembers(*plc, name);
            out.push_back(std::move(v));
        }
    }
    for (const auto& hv : hp.programs.variables) {
        hmi::design::VarInfo v;
        v.name = hv.name;
        v.type = hv.type;
        v.comment = hv.description;
        v.plc = false;
        // Lot 16 : une structure ou un petit tableau IHM - ses cases, comme pour l'automate.
        if (hmi::types::isComposite(hv.type)) {
            const auto leaves = hmi::types::leafVariables(hp, hv);
            if (leaves.size() <= 64)
                for (const auto& l : leaves) {
                    hmi::design::VarInfo m;
                    m.name = l.name;
                    m.type = l.type;
                    m.comment = l.description;
                    m.plc = false;
                    v.members.push_back(std::move(m));
                }
        }
        out.push_back(std::move(v));
    }
    return out;
}

Description describe(const hmi::Project& hp, const domain::Project* plc, std::string_view symbol, std::string_view code,
                     hmi::Id op) {
    Description d;
    if (symbol.empty()) return d;
    if (isKeyword(symbol)) { d.keyword = true; return d; }
    const std::string name(symbol);

    // 1.10.1 (U2) : le script d'un operateur - a, b, Resultat, TO_X (et a.x : son membre).
    if (op != hmi::kNoId) {
        Scope sc;
        sc.hp = &hp;
        sc.script = true;
        sc.opNames = operatorScope(hp, op);
        const auto cut = symbol.find_first_of(".[");
        for (const auto& n : sc.opNames) {
            if (!iequals(n.name, symbol.substr(0, cut))) continue;
            d.found = true;
            if (cut == std::string_view::npos) {
                d.type = n.type;
                d.line = n.name + " : " + n.type + "   " + n.role;
            } else {
                d.type = sc.typeOf(symbol);
                d.found = !d.type.empty();
                d.line = std::string(symbol) + " : " + (d.type.empty() ? std::string("?") : d.type) + "   membre de " + n.name + " ("
                       + n.type + ")";
            }
            return d;
        }
    }

    // Lot 7 : une variable locale du script (ou un parametre de la fonction).
    if (!code.empty() && symbol.find_first_of(".[") == std::string_view::npos) {
        const auto parts = hmi::splitDeclarations(code, true);
        if (const auto* l = parts.local(symbol)) {
            d.found = true;
            d.type = l->type;
            d.line = l->name + " : " + l->type + "   " + localSection(*l)
                   + (l->initial.empty() ? std::string{} : "   initiale " + l->initial) + "   ligne " + std::to_string(l->line);
            return d;
        }
        // 1.10 (chantier K2) : une fonction interne du script (S1), sa signature et sa ligne.
        const auto a = lang::analyze(code);
        if (const auto* f = a.function(symbol)) {
            d.found = true;
            d.type = f->returnType;
            d.line = f->signature() + "   fonction du script, ligne " + std::to_string(f->firstLine)
                   + (f->returnType.empty() ? std::string("   (sans retour)") : std::string{});
            return d;
        }
    }
    // 1.10 (chantier K2) : une fonction du dialecte (S1), une enumeration IHM (E).
    for (const auto& b : lang::builtins())
        if (iequals(b.name, symbol)) {
            d.found = true;
            d.line = std::string(b.signature) + "   fonction des scripts IHM   // " + std::string(b.help);
            return d;
        }
    if (const auto* e = hmi::findEnumeration(hp, symbol)) {
        d.found = true;
        d.type = e->name;
        d.line = e->name + " : \xC3\xA9num\xC3\xA9ration IHM   " + valuesSummary(*e) + "   (" + e->name + "#" + kEllipsis + ")";
        return d;
    }
    if (const auto* f = hp.functionByName(symbol)) {
        d.found = true;
        d.type = f->returnType;
        d.line = hmi::functionSignature(*f) + "   fonction IHM du projet"
               + (f->returnType.empty() ? std::string("   (sans retour)") : std::string{});
        if (!f->description.empty()) d.line += "   // " + f->description;
        return d;
    }
    // 1.12.0 : une native de l'IHM (apres les fonctions du projet : une fonction du projet
    // de meme nom passe avant) - sa signature, sa phrase ; F1 ouvre sa fiche (Natives).
    if (const auto* n = hmi::natives::function(symbol); n && std::string_view(n->name).rfind("IHM_", 0) != 0) {
        d.found = true;
        d.type = std::string(n->returns);
        d.line = hmi::natives::signature(*n) + "   native de l'IHM   // " + std::string(n->summary);
        return d;
    }
    // 1.12.0 : une valeur d'enumeration native (TRANSITION#Fondu).
    if (const hmi::natives::NativeEnum* ne = nullptr; symbol.find('#') != std::string_view::npos) {
        const hmi::natives::EnumValue* nv = nullptr;
        if (hmi::natives::parseEnumLiteral(symbol, &ne, &nv) && ne && nv) {
            d.found = true;
            d.type = "DINT";
            d.line = std::string(ne->name) + "#" + std::string(nv->name) + " = " + std::to_string(nv->number) + "   \xC3\xA9num\xC3\xA9ration native   // "
                   + std::string(nv->text);
            return d;
        }
    }

    // Lot 9 : une variable systeme ou d'instance.
    if (symbol.find('.') != std::string_view::npos) {
        const auto dot = symbol.find('.');
        const hmi::View* pv = hmi::pub::viewNamed(hp, symbol.substr(0, dot));
        const hmi::View shown = pv && hmi::inherits(hp, *pv) ? hmi::compose(hp, *pv) : pv ? *pv : hmi::View{};
        const auto r = hmi::pub::resolve(hp, symbol, pv ? &shown : nullptr);
        using W = hmi::pub::Resolved::What;
        if (r.what == W::Sys && r.slaveMember) {
            // 1.9 : un membre d'une structure d'esclave simule (sys est nul).
            d.found = true;
            d.type = r.type;
            d.line = "SYS.Slave." + r.slave + "." + std::string(r.slaveMember->name) + " : " + r.type
                   + "   variable syst\xC3\xA8me (lecture seule)   l'esclave simul\xC3\xA9 de " + r.slaveEquipment + "   // "
                   + std::string(r.slaveMember->text);
            return d;
        }
        if (r.what == W::Sys && r.sys) {
            d.found = true;
            d.type = r.type;
            d.line = "SYS." + std::string(r.sys->name) + " : " + r.type
                   + (r.access == hmi::pub::Access::ReadWrite ? "   variable syst\xC3\xA8me (lecture et \xC3\xA9" "criture)   "
                                                                : "   variable syst\xC3\xA8me (lecture seule)   ")
                   + std::string(hmi::pub::kSysDomains[r.sys->domain]) + "   // " + std::string(r.sys->text);
            return d;
        }
        if (r.what == W::ViewMember || r.what == W::ObjectMember) {
            d.found = true;
            d.type = r.type;
            const bool rw = r.access == hmi::pub::Access::ReadWrite;
            d.line = name + " : " + r.type + (rw ? "   variable d'instance (lecture et \xC3\xA9" "criture)" : "   variable d'instance (lecture seule)");
            if (r.info) d.line += "   // " + std::string(r.info->text);
            else if (!r.param.empty())   // 1.10.2 (chantier A) : un parametre d'instance de symbole
                d.line += "   param\xC3\xA8tre de " + (r.object ? r.object->name : std::string{}) + "   // relie "
                        + (r.argument.empty() ? std::string("(rien)") : r.argument);
            else if (r.object)
                if (const auto* prop = r.object->find(r.key))
                    d.line += prop->expr.empty() ? "   valeur " + prop->value : "   = " + prop->expr;
            return d;
        }
        if (r.what == W::Unknown || r.what == W::Incomplete) {
            d.line = r.what == W::Unknown ? r.error : name + " : chemin incomplet (Vue.Objet.Propri\xC3\xA9t\xC3\xA9)";
            return d;
        }
    }
    if (const auto* v = hp.variable(symbol)) {
        d.found = true;
        d.type = v->type;
        d.line = v->name + " : " + v->type + "   variable IHM";
        if (hmi::types::isComposite(v->type)) d.line += "   " + hmi::types::summary(hp, v->type, v->packBools);      // lot 16
        else d.line += "   initiale " + (v->initial.empty() ? std::string("-") : v->initial);
        if (!v->folder.empty()) d.line += "   dossier " + v->folder;
        if (!v->description.empty()) d.line += "   // " + v->description;
        return d;
    }
    // Lot 16 : un chemin dans une structure ou un tableau IHM (Four1.Vannes[2].Position),
    // ou une propriete (Consignes.Length).
    if (const auto root = symbol.substr(0, symbol.find_first_of(".[")); root.size() < symbol.size())
        if (const auto* v = hp.variable(root); v && hmi::types::isComposite(v->type)) {
            const std::string type = hmi::types::typeOfPath(hp, symbol);
            if (!type.empty()) {
                d.found = true;
                d.type = type;
                d.line = name + " : " + type + "   membre de la variable IHM " + v->name;
                return d;
            }
            const auto dot = symbol.rfind('.');
            if (dot != std::string_view::npos)
                if (const auto* pi = hmi::types::property(symbol.substr(dot + 1))) {
                    const std::string owner = hmi::types::typeOfPath(hp, symbol.substr(0, dot));
                    if (!owner.empty()) {
                        std::string value;
                        const auto flat = hmi::types::flatten(hp, "x", owner, {}, v->packBools);
                        sim::Value pv;
                        if (flat.ok() && !flat.aggregates.empty() && hmi::types::propertyValue(flat.aggregates.back(), pi->name, pv))
                            value = " = " + pv.display();
                        d.found = true;
                        d.type = std::string(pi->type);
                        d.line = name + " : " + std::string(pi->type) + value + "   propri\xC3\xA9t\xC3\xA9 (lecture seule)   // " + std::string(pi->help);
                        return d;
                    }
                }
        }
    if (const auto* f = function(symbol)) {
        d.found = true;
        d.type = std::string(f->returns);
        std::string params;
        for (const auto p : f->parameters) params += (params.empty() ? "" : ", ") + std::string(p);
        d.line = std::string(f->name) + "(" + params + ") : " + std::string(f->returns) + "   fonction IHM   " + std::string(f->help);
        return d;
    }
    // 1.11.1 (API-V) : API.<...> - ce que le modele d'API-M en dit (type, acces,
    // adresse), ou ce qui manque ; la barre sous l'editeur ne dit plus « ni
    // variable IHM, ni variable de l'automate » pour API.V.
    if (hmi::apivars::isApiPath(symbol) && !hmiOwnsApi(hp))
        if (const auto* model = apiModel(hp, plc)) {
            const auto r = model->resolve(symbol);
            if (r.ok) {
                // 1.11.2 (API-V) : une constante (ou son membre) se lit, ne s'ecrit pas - « Lecture
                // seule », comme le cadenas de l'arbre, meme quand la table des adresses la nomme.
                using AA = hmi::apivars::Access;
                const AA access = r.constant && r.access == AA::ReadWrite ? AA::ReadOnly : r.access;
                d.found = true;
                d.type = r.type;
                d.line = r.path + (r.unitOnly ? std::string("   unit\xC3\xA9 de programme")
                                              : " : " + (r.type.empty() ? std::string("?") : r.type) + "   "
                                                    + std::string(hmi::apivars::accessLabel(access))
                                                    + (r.reference.empty() ? std::string{} : "   " + r.reference)
                                                    + "   variable de l'automate"
                                                    + (r.unit.empty() ? std::string{} : " (" + r.unit + ")"));
                return d;
            }
            d.line = name + "  -  " + (r.error.empty() ? std::string("inconnue sous API.") : r.error);
            return d;
        }
    if (plc) {
        domain::TypeRef type;
        bool pendingArray = false;
        Field last;
        if (resolve(*plc, symbol, type, pendingArray, &last)) {
            d.found = true;
            d.type = last.type;
            const bool member = symbol.find('.') != std::string_view::npos;
            d.line = name + " : " + last.type;
            if (last.var && !last.var->address.raw.empty()) d.line += "   " + last.var->address.raw;
            d.line += member ? "   membre" : "   variable de l'automate";
            if (!last.comment.empty()) d.line += "   // " + last.comment;
            return d;
        }
        if (const auto* b = project::BlockLibrary::shared().find(symbol)) {
            d.found = true;
            d.line = b->signature() + (b->returns.empty() ? std::string{} : " : " + b->returns) + "   " + b->family;
            if (!b->description.empty()) d.line += "   // " + b->description;
            return d;
        }
    }
    if (hp.generalScript(symbol)) {
        d.found = true;
        d.line = name + "   script g\xC3\xA9n\xC3\xA9ral (IHM_APPELER('" + name + "'))";
        return d;
    }
    d.line = name + (core::hasApi() ? "  -  ni variable IHM, ni variable de l'automate" : "  -  pas une variable de l'IHM");   // 1.12.0
    return d;
}

// ------------------------------------------------------------- brancher -----
std::string declarationsPrefix(const std::vector<hmi::Declaration>& decls, hmi::decl::Role role,
                               const std::vector<hmi::Declaration>* inherited) {
    if (decls.empty() && !inherited) return {};
    return hmi::decl::composeCode({}, decls, role, inherited).text;
}

void attach(ui::MultiLineText& editor, Sources src) {
    auto* ed = &editor;
    editor.setCompleteAfterDot(true);
    editor.setCompletionProvider([ed, src](std::string_view prefix, std::vector<ui::MultiLineText::Completion>& out) {
        if (ed->language() != ui::Language::StructuredText) return;
        const auto* hp = src.hmi ? src.hmi() : nullptr;
        if (!hp) return;
        const auto plc = src.plc ? src.plc() : nullptr;
        // 1.11.18 (lot 3) : les declarations du modele, devant (comme tapees en tete de la ligne 1).
        const std::string decls = src.declarations ? src.declarations() : std::string{};
        const std::string text = decls + ed->text();
        const std::size_t at = std::min(decls.size() + ed->caretOffset(), text.size());
        const std::size_t start = at >= prefix.size() ? at - prefix.size() : 0;
        tAfterCaret = std::string_view(text).substr(at);   // 1.10 (chantier K) : les fonctions declarees plus bas
        // 1.10.1 (U2) : le script d'un operateur (a, b, Resultat).
        toCompletions(suggest(*hp, plc.get(), std::string_view(text).substr(0, start), prefix, true, false, src.op ? src.op() : hmi::kNoId),
                      out);
        tAfterCaret = {};
    });
    editor.setSignatureProvider([ed, src](std::string_view name, ui::MultiLineText::Signature& out) {
        if (ed->language() != ui::Language::StructuredText) return false;
        // 1.10 (chantier K2) : une fonction interne du script, une fonction du dialecte (S1).
        if (dialectSignature((src.declarations ? src.declarations() : std::string{}) + ed->text(), name, out)) return true;
        const auto plc = src.plc ? src.plc() : nullptr;
        return signature(src.hmi ? src.hmi() : nullptr, plc.get(), name, out);
    });
    editor.setValueProvider([ed, src](std::string_view symbol, std::string& text) {
        if (ed->language() != ui::Language::StructuredText) return false;
        const auto* hp = src.hmi ? src.hmi() : nullptr;
        if (!hp) return false;
        const auto plc = src.plc ? src.plc() : nullptr;
        // Un nom connu seulement : un mot d'un commentaire ou d'une chaine n'a
        // pas d'infobulle (la barre sous le code, elle, dit "inconnu").
        const auto d = describe(*hp, plc.get(), symbol, (src.declarations ? src.declarations() : std::string{}) + ed->text(),
                                src.op ? src.op() : hmi::kNoId);   // 1.10.1 (U2) ; 1.11.18 : les declarations du modele
        if (d.keyword || !d.found) return false;
        text = d.line;
        std::string value;
        if (src.live && src.live(symbol, value)) text += "   = " + value;
        return true;
    });
}

ui::InputText::Assist fieldAssist(Sources src, bool templateText) {
    return [src, templateText](std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
        const auto* hp = src.hmi ? src.hmi() : nullptr;
        if (!hp) return;
        const auto plc = src.plc ? src.plc() : nullptr;
        std::size_t start = before.size();
        while (start > 0 && identChar(before[start - 1])) --start;
        const auto prefix = before.substr(start);
        const auto head = before.substr(0, start);
        const auto where = locate(head, templateText);
        // Une liste des la premiere lettre (un champ est court), apres un point
        // (les membres), ou dans une accolade ouverte ; 1.10 (K2) : apres "T_MODE#".
        const bool literal = head.size() > 1 && head.back() == '#' && identChar(head[head.size() - 2]);
        if (prefix.empty() && !literal && where.context != Context::Member && where.context != Context::Placeholder) return;
        auto items = suggest(*hp, plc.get(), head, prefix, false, templateText);
        from = start;
        for (auto& it : items) {
            if (it.extend) from = std::min(from, start - std::min(start, it.extend));
            ui::InputText::Suggestion s;
            s.text = it.text;
            s.detail = it.detail;
            s.icon = iconOf(it.kind);
            s.badge = badgeOf(it);      // 1.11.2 (API-V, decision 161)
            s.insert = it.insert;
            // Un membre qui est lui-meme une structure : la liste se rouvre apres "."
            // 1.10.2 (chantier A) : une vue, un objet (Vue_Armoire_A. puis V_101.) aussi.
            s.chain = it.chain;
            out.push_back(std::move(s));
        }
    };
}

// ------------------------------------------------------------- les grilles ---
namespace {

std::function<std::shared_ptr<const domain::Project>()> g_plc;
std::function<bool(std::string_view, std::string&)>     g_live;

// 1.11.1 (API-V) : le modele d'API-M, refait quand le programme ou le projet IHM
// change, et au plus toutes les deux secondes (la table des adresses a pu
// changer : l'acces montre). Le programme est tenu par le cache (g_plc).
const hmi::apivars::Model* apiModelCached(const hmi::Project& hp, const domain::Project* plc) {
    if (!plc || !g_plc) return nullptr;
    auto owned = g_plc();
    if (owned.get() != plc) return nullptr;
    struct Cache {
        std::shared_ptr<const domain::Project> plc;
        const hmi::Project* hp{nullptr};
        std::chrono::steady_clock::time_point at{};
        hmi::apivars::Model model;
    };
    static Cache cache;
    const auto now = std::chrono::steady_clock::now();
    if (cache.plc != owned || cache.hp != &hp || now - cache.at > std::chrono::seconds(2)) {
        hmi::apivars::BuildOptions options;
        options.uses = false;               // l'aide a la saisie ne montre pas les emplois
        cache.model = hmi::apivars::Model::build(owned, &hp, options);
        cache.plc = std::move(owned);
        cache.hp = &hp;
        cache.at = now;
    }
    return &cache.model;
}

const hmi::apivars::Model* apiModel(const hmi::Project& hp, const domain::Project* plc) { return apiModelCached(hp, plc); }

// Une aide qui ne regarde qu'une partie du champ : apres "=", apres le dernier ";".
ui::InputText::Assist restricted(ui::InputText::Assist base, bool needEquals, bool list) {
    return [base = std::move(base), needEquals, list](std::string_view before, std::size_t& from,
                                                      std::vector<ui::InputText::Suggestion>& out) {
        std::size_t offset = 0;
        if (needEquals) {
            if (before.empty() || before.front() != '=') return;
            offset = 1;
        }
        if (list) {
            const auto semi = before.rfind(';');
            if (semi != std::string_view::npos && semi + 1 > offset) offset = semi + 1;
        }
        while (offset < before.size() && before[offset] == ' ') ++offset;
        std::size_t f = 0;
        base(before.substr(offset), f, out);
        from = offset + f;
    };
}

} // namespace

void installProgram(std::function<std::shared_ptr<const domain::Project>()> plc,
                    std::function<bool(std::string_view, std::string&)> live) {
    g_plc = std::move(plc);
    g_live = std::move(live);
    hmiparams::setProgram(g_plc);   // 1.9 : l'editeur d'actions (arguments types)
}

Sources sourcesFor(hmi::DocumentPtr doc) {
    Sources s;
    s.hmi = [doc]() -> const hmi::Project* { return doc ? &doc->project : nullptr; };
    s.plc = [] { return g_plc ? g_plc() : std::shared_ptr<const domain::Project>{}; };
    s.live = [](std::string_view n, std::string& out) { return g_live && g_live(n, out); };
    return s;
}

// ---- 1.10 (chantier K) : l'aide a la saisie selon le type du champ ----
namespace {
using Expect = ui::exprfield::Expect;
// Le type d'une proposition, lu en tete de son detail ("BOOL  .  IHM...").
std::string detailType(std::string_view detail) {
    std::size_t end = 0;
    while (end < detail.size() && detail[end] != ' ' && static_cast<unsigned char>(detail[end]) != 0xC2) ++end;
    return upper(std::string(detail.substr(0, end)));
}
// 1 : convient ; 0 : on ne sait pas (une structure : ses membres, une fonction) ; -1 : ne convient pas.
int fits(Expect e, const std::string& type) {
    const bool isBool = type == "BOOL" || type == "EBOOL";
    const bool isNumber = hmi::typereg::isNumber(type);       // 1.11.19 (lot 6) : les nombres du registre
    const bool isTime = type == "TIME";
    const bool isText = type.rfind("STRING", 0) == 0;
    if (!isBool && !isNumber && !isTime && !isText) return 0;
    switch (e) {
        case Expect::Bool:   return isBool ? 1 : -1;
        case Expect::Number: return isNumber ? 1 : (isBool ? 0 : -1);
        case Expect::Time:   return isTime ? 1 : (isNumber ? 0 : -1);
        case Expect::Text: case Expect::Color: case Expect::View: return isText ? 1 : -1;
        default: return 0;
    }
}
struct NamedColor { const char* name; const char* hex; };
constexpr NamedColor kNamedColors[] = {
    {"rouge", "#E53935"}, {"vert", "#43A047"}, {"bleu", "#1E88E5"}, {"orange", "#FB8C00"}, {"jaune", "#FDD835"},
    {"gris", "#9E9E9E"}, {"noir", "#000000"}, {"blanc", "#FFFFFF"}, {"violet", "#8E24AA"}, {"cyan", "#00ACC1"}};
bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (prefix.size() > s.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    return true;
}
} // namespace

static ui::InputText::Assist typedAssist(ui::InputText::Assist base, Sources src, Expect expect, std::vector<std::string> choices,
                                         bool whole) {
    return [base = std::move(base), src, expect, choices = std::move(choices), whole](std::string_view before, std::size_t& from,
                                                                               std::vector<ui::InputText::Suggestion>& out) {
        // Une case "tout expression" s'ecrit comme apres un "=".
        const bool equals = whole || (!before.empty() && before.front() == '=');
        const auto* hp = src.hmi ? src.hmi() : nullptr;
        // Une valeur fixe (pas de "=") : ce qui s'ecrit dans la case - une liste, une
        // couleur, une vue - filtre par ce qu'on a tape.
        if (!equals && (expect == Expect::List || expect == Expect::Color || expect == Expect::View)) {
            const std::string typed(before);
            from = 0;
            const auto offer = [&](std::string text, std::string detail) {
                if (!typed.empty() && !startsWithNoCase(text, typed)) return;
                ui::InputText::Suggestion sg;
                sg.text = std::move(text);
                sg.detail = std::move(detail);
                out.push_back(std::move(sg));
            };
            if (expect == Expect::List)
                for (const auto& c : choices) offer(c, "choix");
            if (expect == Expect::View && hp)
                for (const auto& v : hp->views) offer(v.name, "vue" + (v.role.empty() ? std::string{} : "  \xC2\xB7  " + v.role));
            if (expect == Expect::Color) {
                if (hp)
                    for (const auto& c : projectColors(*hp, 8)) offer(c, "couleur  \xC2\xB7  dans le projet");
                for (const auto& nc : kNamedColors) {
                    if (!typed.empty() && !startsWithNoCase(nc.name, typed) && !startsWithNoCase(nc.hex, typed)) continue;
                    ui::InputText::Suggestion sg;
                    sg.text = std::string(nc.hex) + "  " + nc.name;
                    sg.insert = nc.hex;
                    sg.detail = "couleur nomm\xC3\xA9" "e";
                    out.push_back(std::move(sg));
                }
            }
            return;
        }
        if (!base) return;
        const std::size_t first = out.size();
        base(before, from, out);
        // Le bon type d'abord, puis ce qu'on ne sait pas juger (une structure, ses
        // membres), puis ce qui ne convient pas - grise par son detail.
        std::vector<ui::InputText::Suggestion> good, unknown, bad;
        for (std::size_t i = first; i < out.size(); ++i) {
            auto sg = std::move(out[i]);
            const std::string type = detailType(sg.detail);
            // La valeur actuelle en simulation, si elle existe.
            if (src.live) {
                std::string now;
                if (src.live(sg.insert.empty() ? sg.text : sg.insert, now) && !now.empty()) sg.detail += "  \xC2\xB7  = " + now;
            }
            const int f = fits(expect, type);
            if (f > 0) good.push_back(std::move(sg));
            else if (f == 0) unknown.push_back(std::move(sg));
            else {
                sg.detail = "ne convient pas : " + sg.detail;
                bad.push_back(std::move(sg));
            }
        }
        out.resize(first);
        // 1.12.1 : une liste qui prend une enumeration native (Alignement : ALIGNEMENT) :
        // ses litteraux d'abord (ALIGNEMENT#Centre), des le "=" ou le debut du nom.
        if (equals && expect == Expect::List)
            if (const auto* ne = hmi::natives::enumForWords(choices)) {
                std::size_t start = before.size();
                while (start > 0 && (std::isalnum(static_cast<unsigned char>(before[start - 1])) || before[start - 1] == '_' || before[start - 1] == '#'))
                    --start;
                const std::string typed(before.substr(start));
                std::string head(before.substr(0, start));
                while (!head.empty() && (head.back() == ' ' || head.back() == '=')) head.pop_back();
                if (typed.find('#') == std::string::npos && (!typed.empty() || head.empty())) {   // apres le # : la completion du code
                    std::vector<ui::InputText::Suggestion> lits;
                    for (const auto& v : ne->values) {
                        std::string text = std::string(ne->name) + "#" + std::string(v.name);
                        if (!typed.empty() && !startsWithNoCase(text, typed)) continue;
                        ui::InputText::Suggestion sg;
                        sg.text = std::move(text);
                        sg.detail = "\xC2\xAB " + std::string(v.argument) + " \xC2\xBB  \xC2\xB7  " + std::string(ne->name) + " (native) = "
                                  + std::to_string(v.number);
                        lits.push_back(std::move(sg));
                    }
                    if (!lits.empty()) {
                        if (good.empty() && unknown.empty() && bad.empty()) from = start;   // sinon : le debut du nom, deja
                        good.insert(good.begin(), std::make_move_iterator(lits.begin()), std::make_move_iterator(lits.end()));
                    }
                }
            }
        for (auto* list : {&good, &unknown, &bad})
            for (auto& sg : *list) out.push_back(std::move(sg));
        // Une couleur ou une vue par expression : aussi les valeurs entre quotes.
        if (equals && (expect == Expect::Color || expect == Expect::View) && hp) {
            std::size_t start = before.size();
            while (start > 0 && (std::isalnum(static_cast<unsigned char>(before[start - 1])) || before[start - 1] == '_' || before[start - 1] == '#'
                                 || before[start - 1] == '\'')) --start;
            const std::string typed(before.substr(start));
            // Rien de commence (apres une parenthese, un espace) : pas de liste.
            if (typed.empty()) return;
            const std::string bare = typed.front() != '\'' ? typed : typed.substr(1);
            const auto quoted = [&](const std::string& value, std::string detail) {
                if (!bare.empty() && !startsWithNoCase(value, bare)) return;
                ui::InputText::Suggestion sg;
                sg.text = "'" + value + "'";
                sg.detail = std::move(detail);
                out.push_back(std::move(sg));
            };
            if (expect == Expect::View)
                for (const auto& v : hp->views) quoted(v.name, "vue");
            else
                for (const auto& nc : kNamedColors) quoted(nc.hex, std::string("couleur  \xC2\xB7  ") + nc.name);
            if (out.size() > first) from = std::min(from, start);
        }
    };
}
// ---- fin 1.10 (chantier K) ----

ui::PropertyGrid::FieldAssistFor gridAssist(Sources src) {
    return [src](std::string_view, const ui::PropertyGrid::Property& p) -> ui::InputText::Assist {
        using VT = ui::PropertyGrid::ValueType;
        // 1.10 (chantier K) : une case a expression a toujours son aide (une bascule,
        // une liste ou une couleur pilotees s'editent en texte).
        const bool exprField = ui::exprfield::accepts(p);
        if (!p.commit || p.type == VT::ReadOnly) return {};
        if (!exprField && (p.type == VT::Enum || p.type == VT::Boolean)) return {};
        if (exprField) {
            const auto e = ui::exprfield::expectOf(p);
            if (e == Expect::Template) {
                auto tpl = templateOrExpression(src);
                return tpl;
            }
            // Une case "tout expression" (la valeur EST l'expression : Condition,
            // Visibilite (expression)...) : l'aide sans "=", meme vide.
            std::string name = p.name;
            if (const auto at = name.find("  \xC6\x92"); at != std::string::npos) name.erase(at);
            static const char* kWhole[] = {
                "Condition", "Condition (facultative)", "Expression surveill\xC3\xA9" "e", "Variable", "Variable API",
                "Valeur (expression)", "Expression d'autorisation", "Expression"};
            const std::string_view tag = "(expression)";
            const bool whole = (!p.expression.empty() && p.expression == p.value)
                            || std::any_of(std::begin(kWhole), std::end(kWhole), [&](const char* n) { return name == n; })
                            || (name.size() > tag.size() && name.compare(name.size() - tag.size(), tag.size(), tag) == 0);
            auto base = whole ? fieldAssist(src, false) : restricted(fieldAssist(src, false), true, false);
            return typedAssist(std::move(base), src, e, p.enumValues, whole);
        }
        std::string name = p.name;
        if (const auto at = name.find("  \xC6\x92"); at != std::string::npos) name.erase(at);   // " f" : pilotee
        static const char* kTemplates[] = {"Texte", "Message"};
        static const char* kExpressions[] = {
            "Condition", "Condition (facultative)", "Expression surveill\xC3\xA9" "e", "Variable", "Variable API",
            "Valeur (expression)", "Expression d'autorisation", "Expression"};
        static const char* kLists[] = {"Variables archiv\xC3\xA9" "es (a; b)", "Plumes : variables trac\xC3\xA9" "es (a;b)"};
        auto is = [&](const auto& names) {
            return std::any_of(std::begin(names), std::end(names), [&](const char* n) { return name == n; });
        };
        auto expression = restricted(fieldAssist(src, false), true, false);   // "=..." partout
        // "Visibilite (expression)", "Couleur dynamique (expression)"... : l'animation.
        const std::string_view tag = "(expression)";
        const bool animated = name.size() > tag.size() && name.compare(name.size() - tag.size(), tag.size(), tag) == 0;
        if (is(kExpressions) || animated) return fieldAssist(src, false);
        if (is(kLists)) return restricted(fieldAssist(src, false), false, true);
        if (is(kTemplates)) {
            return [tpl = fieldAssist(src, true), expression](std::string_view before, std::size_t& from,
                                                              std::vector<ui::InputText::Suggestion>& out) {
                if (!before.empty() && before.front() == '=') expression(before, from, out);
                else tpl(before, from, out);
            };
        }
        return expression;
    };
}

ui::InputText::Assist templateOrExpression(Sources src) {
    return [tpl = fieldAssist(src, true), expression = restricted(fieldAssist(src, false), true, false)](
               std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
        if (!before.empty() && before.front() == '=') expression(before, from, out);
        else tpl(before, from, out);
    };
}

std::vector<std::string> projectColors(const hmi::Project& p, std::size_t max) {
    std::vector<std::pair<std::string, int>> seen;
    auto add = [&](const std::string& v) {
        gfx::Color probe;
        if (!ui::parseHexColor(v, probe)) return;
        const std::string key = upper(v);
        for (auto& [k, n] : seen) if (k == key) { ++n; return; }
        seen.emplace_back(key, 1);
    };
    static const char* kKeys[] = {"fill", "stroke", "textColor", "colorOn", "colorOff", "background", "blinkColor"};
    for (const auto& v : p.views) {
        add(v.background);
        for (const auto& o : v.objects)
            for (const auto& pr : o.props)
                if (std::any_of(std::begin(kKeys), std::end(kKeys), [&](const char* k) { return pr.key == k; })) add(pr.value);
    }
    std::stable_sort(seen.begin(), seen.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::string> out;
    for (const auto& [k, n] : seen) {
        if (out.size() == max) break;
        out.push_back(k);
    }
    return out;
}

} // namespace app::assist
