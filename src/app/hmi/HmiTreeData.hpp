// =============================================================================
//  app/hmi/HmiTreeData.hpp - ce que l'arbre du projet deballe sous l'IHM
// -----------------------------------------------------------------------------
//  L'ARBRE MONTRE CE QUI NE SE VOIT PAS SUR LA VUE. Sous un objet, ses
//  surcharges : les proprietes pilotees par une expression (visibilite,
//  couleur dynamique...), le texte dynamique, la variable API, les actions,
//  la securite, le verrou. Un groupe montre aussi ses membres. Sous les
//  alarmes, leurs groupes puis les alarmes ; sous les utilisateurs, les groupes
//  puis les comptes, et les roles ; sous la Programmation generale, les
//  scripts, les variables IHM et toutes les variables que l'application lit
//  ou ecrit.
//
//  EN LIGNE, SANS BIBLIOTHEQUE : ViewModels.cpp est aussi compile seul dans
//  des tests (treednd, execorder_wiring) qui ne lient pas xpg_hmi. Tout ici se
//  lit dans les donnees du modele ; les libelles sont ecrits ici.
// =============================================================================
#pragma once

#include "HmiPaneKit.hpp"
#include "../../hmi/HmiDecl.hpp"        // 1.11.18 : les blocs de declaration (withoutDeclarations)
#include "../../hmi/HmiDuplicate.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../ui/Icons.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace app::hmitree {

struct Line {
    std::string text;
    ui::Icon    icon{ui::Icon::None};
    int         action{-1};        // une action : son rang dans la liste de l'objet
    std::string property;          // une propriete animee : sa cle
};

// Pas de Object::find / number ici : ils sont dans la bibliotheque xpg_hmi.
inline const hmi::Prop* propOf(const hmi::Object& o, std::string_view key) {
    for (const auto& p : o.props) if (p.key == key) return &p;
    return nullptr;
}

inline std::string_view animatedLabel(std::string_view key) {
    static const std::pair<const char*, const char*> k[] = {
        {"visible", "Visibilit\xC3\xA9"}, {"fill", "Couleur dynamique"}, {"blink", "Clignotement"},
        {"rot", "Rotation"}, {"x", "D\xC3\xA9placement X"}, {"y", "D\xC3\xA9placement Y"}, {"text", "Texte"},
        {"value", "Valeur"}, {"opacity", "Transparence"}, {"w", "Largeur"}, {"h", "Hauteur"}, {"image", "Image"},
        {"textColor", "Couleur du texte"}, {"stroke", "Contour"}, {"colorOn", "Couleur allum\xC3\xA9"},
        {"colorOff", "Couleur \xC3\xA9teint"},
        // 1.10.3 (Q1103) : les liens fx d'une vanne disaient "label = V[0].Nom" (capture du client).
        {"label", "Libell\xC3\xA9"}, {"opening", "Ouverture"}, {"moving", "En mouvement"}, {"fault", "D\xC3\xA9" "faut"},
        {"title", "Titre"}, {"tooltip", "Infobulle"}, {"unit", "Unit\xC3\xA9"},
        // 1.10.4 (K2) : les deux booleens d'une vanne 3 voies.
        {"positionA", "Bool\xC3\xA9" "en A"}, {"positionB", "Bool\xC3\xA9" "en B"}};
    for (const auto& [key2, label] : k) if (key == key2) return label;
    return key;
}

inline std::string_view triggerText(hmi::Trigger t) {
    switch (t) {
        case hmi::Trigger::Click:       return "Clic";
        case hmi::Trigger::DoubleClick: return "Double-clic";
        case hmi::Trigger::RisingEdge:  return "Front montant";
        case hmi::Trigger::FallingEdge: return "Front descendant";
        case hmi::Trigger::LongPress:   return "Appui long";
        case hmi::Trigger::ValueChange: return "Changement";
        case hmi::Trigger::ViewOpen:    return "Ouverture";
        case hmi::Trigger::ViewClose:   return "Fermeture";
        case hmi::Trigger::Timer:       return "Minuterie";
    }
    return "?";
}

inline std::string actionText(const hmi::Action& a) {
    std::string what;
    switch (a.operation) {
        case hmi::Operation::Toggle:     what = "basculer " + a.target; break;
        case hmi::Operation::Set:        what = a.target + " := TRUE"; break;
        case hmi::Operation::Reset:      what = a.target + " := FALSE"; break;
        case hmi::Operation::Increment:  what = a.target + " += " + (a.value.empty() ? "1" : a.value); break;
        case hmi::Operation::Decrement:  what = a.target + " -= " + (a.value.empty() ? "1" : a.value); break;
        case hmi::Operation::Assign:     what = a.target + " := " + a.value; break;
        case hmi::Operation::Navigate:   what = "naviguer vers " + a.target; break;
        case hmi::Operation::Popup:      what = "popup " + a.target + (a.value.empty() ? std::string{} : " (" + a.value + ")"); break;
        case hmi::Operation::ClosePopup: what = "fermer la popup"; break;
        case hmi::Operation::RunScript:  what = "script ST"; break;
        case hmi::Operation::CallScript: what = "appeler " + a.target; break;
        case hmi::Operation::Log:        what = "journal : " + a.value; break;
        case hmi::Operation::AckAlarm:   what = "acquitter " + (a.target.empty() ? std::string("les alarmes") : a.target); break;
        case hmi::Operation::LoadRecipe: what = "recette " + a.target + (a.value.empty() ? "" : " / " + a.value); break;
        case hmi::Operation::ChangeUser: what = "changer d'utilisateur"; break;
        case hmi::Operation::RequestResource:
            what = "demander une ressource" + (a.value.empty() ? std::string{} : " (" + a.value + ")")
                 + (a.target.empty() ? std::string{} : " \xE2\x86\x92 " + a.target);
            break;
        case hmi::Operation::BindTable: what = "lier " + a.target + (a.value.empty() ? std::string{} : " \xC3\xA0 " + a.value); break;
        case hmi::Operation::PlaySound: what = "son " + a.target; break;
        case hmi::Operation::ShowSystem: what = "param\xC3\xA8tres syst\xC3\xA8me"; break;
        // lot 8
        case hmi::Operation::ChangePopup:    what = "changer de popup : " + a.target; break;
        case hmi::Operation::CenterPopup:    what = "centrer la popup" + (a.target.empty() ? std::string{} : " " + a.target); break;
        case hmi::Operation::PreviousPopup:  what = "popup pr\xC3\xA9" "c\xC3\xA9" "dente"; break;
        case hmi::Operation::CloseAllPopups: what = "fermer toutes les popups"; break;
        case hmi::Operation::Logout:         what = "d\xC3\xA9" "connecter"; break;
        // lot 11
        case hmi::Operation::ShelveAlarm:    what = "mettre de c\xC3\xB4t\xC3\xA9 " + (a.target.empty() ? std::string("l'alarme choisie") : a.target); break;
        case hmi::Operation::UnshelveAlarm:  what = "remettre en service " + (a.target.empty() ? std::string("l'alarme choisie") : a.target); break;
        case hmi::Operation::SilenceAlarms:  what = "faire taire les alarmes"; break;
        case hmi::Operation::Export:         what = "exporter " + (a.target.empty() ? std::string("alarmes") : a.target); break;
        // lot 12
        case hmi::Operation::NavigateBack:    what = "vue pr\xC3\xA9" "c\xC3\xA9" "dente"; break;
        case hmi::Operation::NavigateForward: what = "vue suivante"; break;
        case hmi::Operation::NavigateHome:    what = "vue d'accueil"; break;
        case hmi::Operation::ShowLogin:
            what = "menu de connexion" + (a.value.empty() ? std::string{} : " (" + a.value + ")");
            break;
        // lot 13
        case hmi::Operation::SetLanguage:
            what = "langue " + (a.target.empty() ? std::string("suivante") : a.target);
            break;
        case hmi::Operation::SetTheme:
            what = "th\xC3\xA8me " + (a.target.empty() ? std::string("(bascule)") : a.target);
            break;
        // lot 16 : un GIF anime
        case hmi::Operation::GifPlay:   what = "jouer le GIF " + a.target; break;
        case hmi::Operation::GifPause:  what = "pause du GIF " + a.target; break;
        case hmi::Operation::GifStop:   what = "arr\xC3\xAAter le GIF " + a.target; break;
        case hmi::Operation::GifReplay: what = "rejouer le GIF " + a.target + " " + (a.value.empty() ? std::string("1") : a.value) + " fois"; break;
        // 1.9 : appliquer copie sur reference
        case hmi::Operation::ApplyCopy:
            what = "appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence " + (a.target.empty() || a.target == "*" ? std::string("(tous)") : a.target);
            break;
    }
    std::string when(triggerText(a.trigger));
    if (!a.watch.empty()) when += " (" + a.watch + ")";
    return when + " \xE2\x86\x92 " + what + (a.guard.empty() ? std::string{} : "   si " + a.guard);
}

inline bool hasTemplate(std::string_view text) {
    const auto open = text.find('{');
    return open != std::string_view::npos && text.find('}', open) != std::string_view::npos;
}

// ---- lot 6 : le contenu des objets, lu ici sans la bibliotheque IHM ---------
inline std::vector<std::string> splitChar(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
inline std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (a < b && blank(s[a])) ++a;
    while (b > a && blank(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}
// Les cases pilotees d'un tableau ("=expression" ou texte a trous), numerotees de 1.
struct DrivenCell { int row{0}, col{0}; std::string text; };
inline std::vector<DrivenCell> drivenCells(std::string_view cells) {
    std::vector<DrivenCell> out;
    if (cells.empty()) return out;
    const auto rows = splitChar(cells, '\n');
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto cols = splitChar(rows[r], '\t');
        for (std::size_t c = 0; c < cols.size(); ++c) {
            const auto& t = cols[c];
            const auto open = t.find('{');
            const bool tpl = open != std::string::npos && t.find('}', open) != std::string::npos;
            if ((!t.empty() && t.front() == '=') || tpl)
                out.push_back({static_cast<int>(r) + 1, static_cast<int>(c) + 1, t});
        }
    }
    return out;
}
// Les etats d'une image animee : (condition, images).
inline std::vector<std::pair<std::string, std::string>> imageStatesOf(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& raw : splitChar(text, '\n')) {
        const auto line = trimmed(raw);
        if (line.empty()) continue;
        const auto arrow = line.find("=>");
        if (arrow == std::string::npos) out.emplace_back("TRUE", line);
        else out.emplace_back(trimmed(std::string_view(line).substr(0, arrow)), trimmed(std::string_view(line).substr(arrow + 2)));
    }
    return out;
}

// Les surcharges d'un objet : tout ce qui n'est pas sa valeur "naturelle".
inline std::vector<Line> overridesOf(const hmi::Object& o) {
    std::vector<Line> out;
    for (const auto& p : o.props) {
        if (p.key == "auth") continue;
        if (!p.expr.empty()) {
            Line l;
            l.text = std::string(animatedLabel(p.key)) + " = " + p.expr;
            l.icon = ui::Icon::Play;
            l.property = p.key;
            out.push_back(std::move(l));
        } else if (p.key == "text" && hasTemplate(p.value)) {
            out.push_back({"Texte dynamique : " + p.value, ui::Icon::Document, -1, p.key});
        }
    }
    if (const auto* v = propOf(o, "variable"); v && !v->value.empty())
        out.push_back({"Variable API : " + v->value, ui::Icon::Variable, -1, "variable"});
    if (const auto* v = propOf(o, "variables"); v && !v->value.empty())
        out.push_back({"Plumes : " + v->value, ui::Icon::Chart, -1, "variables"});
    // Lot 6 : le contenu des tableaux, des images animees, des gestionnaires.
    if (const auto* c = propOf(o, "cells"); c && !c->value.empty())
        for (const auto& d : drivenCells(c->value))
            out.push_back({"Case [" + std::to_string(d.row) + "," + std::to_string(d.col) + "] = " + d.text, ui::Icon::Play, -1, "cells"});
    if (const auto* st = propOf(o, "states"); st && !st->value.empty()) {
        int k = 0;
        for (const auto& [cond, images] : imageStatesOf(st->value))
            out.push_back({"\xC3\x89tat " + std::to_string(++k) + " : " + cond + " \xE2\x86\x92 " + images, ui::Icon::Play, -1, "states"});
    }
    if (o.kind == hmi::Kind::RecipeManager)
        if (const auto* r = propOf(o, "recipe"); r && !r->value.empty())
            out.push_back({"Recette : " + r->value, ui::Icon::AnimationTable, -1, "recipe"});
    for (std::size_t i = 0; i < o.actions.size(); ++i)
        out.push_back({"Action : " + actionText(o.actions[i]), ui::Icon::Code, static_cast<int>(i), {}});
    const auto* accessProp = propOf(o, "access");
    const double access = accessProp ? std::strtod(accessProp->value.c_str(), nullptr) : 0.0;
    if (access > 0) out.push_back({"S\xC3\xA9" "curit\xC3\xA9 : niveau " + std::to_string(static_cast<int>(access)), ui::Icon::Lock, -1, "access"});
    if (const auto* a = propOf(o, "auth"); a && !a->value.empty())
        out.push_back({"Autorisation = " + a->value, ui::Icon::Lock, -1, "auth"});
    if (o.locked) out.push_back({"Verrouill\xC3\xA9 dans l'\xC3\xA9" "diteur", ui::Icon::Lock, -1, {}});
    if (o.hidden) out.push_back({"Cach\xC3\xA9 dans l'\xC3\xA9" "diteur", ui::Icon::Search, -1, {}});
    return out;
}

// Les objets poses directement sous `parent` (kNoId : la vue), dans l'ordre.
inline std::vector<const hmi::Object*> childrenOf(const hmi::View& v, hmi::Id parent) {
    std::vector<const hmi::Object*> out;
    for (const auto& o : v.objects) if (o.parent == parent) out.push_back(&o);
    return out;
}

// ---- 1.10.2 (chantier A) : L'OBJET DEPLIE, PAR FAMILLES (maquette M13, scene 5) ----
//  Sous un objet : Actions, Liens fx, Parametres (une instance), [Alarmes : le
//  noeud de la 1.10], Animations, Elements (un groupe, un conteneur), Securite.
//  Chaque famille a son compteur ; une famille vide n'apparait pas. Les lignes
//  restent celles de overridesOf (le rang ne change pas : le clic les retrouve).
enum class Family : std::uint8_t { Actions, Links, Params, Animations, Markers, Elements, Security, Count };

// Une animation : ce qui bouge, se cache, clignote ou change de forme ; le
// reste des proprietes pilotees par une expression est un lien fx.
inline bool isAnimationKey(std::string_view key) {
    static const char* k[] = {"rot", "x", "y", "w", "h", "blink", "opacity", "visible", "states"};
    for (const char* x : k) if (key == x) return true;
    return false;
}
inline Family familyOf(const Line& l) {
    if (l.action >= 0) return Family::Actions;
    if (l.property.empty() || l.property == "access" || l.property == "auth") return Family::Security;   // verrou, cache
    if (isAnimationKey(l.property)) return Family::Animations;
    return Family::Links;
}
inline std::string_view familyLabel(Family f) {
    switch (f) {
        case Family::Actions:    return "Actions";
        case Family::Links:      return "Liens fx";
        case Family::Params:     return "Param\xC3\xA8tres";
        case Family::Animations: return "Animations";
        case Family::Markers:    return "Rep\xC3\xA8res";
        case Family::Elements:   return "\xC3\x89l\xC3\xA9ments";
        case Family::Security:   return "S\xC3\xA9" "curit\xC3\xA9";
        case Family::Count:      break;
    }
    return {};
}
inline ui::Icon familyIcon(Family f) {
    switch (f) {
        case Family::Actions:    return ui::Icon::Code;
        case Family::Links:      return ui::Icon::Variable;
        case Family::Params:     return ui::Icon::Settings;
        case Family::Animations: return ui::Icon::Play;
        case Family::Markers:    return ui::Icon::Star;
        case Family::Elements:   return ui::Icon::Module;
        case Family::Security:   return ui::Icon::Lock;
        case Family::Count:      break;
    }
    return ui::Icon::None;
}
// Les rangs (dans overridesOf) des lignes d'une famille, dans l'ordre.
inline std::vector<std::size_t> familyRanks(const std::vector<Line>& items, Family f) {
    std::vector<std::size_t> out;
    for (std::size_t k = 0; k < items.size(); ++k) if (familyOf(items[k]) == f) out.push_back(k);
    return out;
}
// Le texte d'un parametre d'instance : "Pompe := Pompes[2]" (la valeur par
// defaut, faute d'argument, est dite telle).
inline std::string paramText(const hmi::pub::InstanceParam& ip) {
    std::string t = ip.name + (ip.type.empty() ? std::string{} : " : " + ip.type) + " := "
                  + (ip.argument.empty() ? std::string("(vide)") : ip.argument);
    if (!ip.given) t += "   (d\xC3\xA9" "faut)";
    return t;
}
// Les reperes $...$ (maquette M13 ; 1.11 : un morceau qui varie quand on duplique,
// $V[1].Ouv$, $V[0]$). 1.10.2, a la fusion de dupliquer-1102 : UN SEUL lecteur, celui
// de Dupliquer (hmi::dup::ownMarkers, sur hmi::markers : deux caracteres au moins, a
// cause des echappements ST $N, $R..., une ligne ; "$$" est un vrai $ ; 1.11 : pas
// dans une chaine). L'arbre et Dupliquer comptent donc les memes reperes, dans les memes
// champs : proprietes, expressions, actions ET alarmes de l'objet seul (pas ses
// elements, qui se deplient a leur tour). `places` : les champs qui le portent.
struct Marker { std::string name; int places{0}; };
inline std::vector<Marker> markersOf(const hmi::Object& o) {
    std::vector<Marker> out;
    for (const auto& m : hmi::dup::ownMarkers(o)) out.push_back({m.name, static_cast<int>(m.spots.size())});
    return out;
}
inline std::string markerText(const Marker& m) {
    return "$" + m.name + "$   \xC3\x97 " + std::to_string(m.places);
}

// Le nombre de lignes d'une famille de l'objet (Elements : ses enfants ;
// Parametres : ceux de son symbole ; Reperes : ses reperes).
inline std::size_t familySize(const hmi::Project& p, const hmi::View& v, const hmi::Object& o, Family f) {
    if (f == Family::Elements) {
        std::size_t n = 0;
        for (const auto& x : v.objects) if (x.parent == o.id) ++n;
        return n;
    }
    if (f == Family::Params) return hmi::pub::instanceParams(p, o).size();
    if (f == Family::Markers) return markersOf(o).size();
    return familyRanks(overridesOf(o), f).size();
}

// ---- 1.10.3 (Q1103) : LES DEUX EXPLORATEURS, UN SEUL CODE ----
//  L'arbre de l'application (ViewModels) et l'explorateur d'objets de la vue
//  (HmiObjectList) deplient un objet de la meme facon : ses familles non vides,
//  dans le meme ordre, leur titre "Actions  [2]", et leurs lignes (texte, icone,
//  et ou mene un clic : `action`, l'onglet Actions sur elle ; `property`, sa case
//  dans l'inspecteur - HmiEditor::showLine, pour les deux).
inline std::vector<Family> objectFamilies(const hmi::Project& p, const hmi::View& v, const hmi::Object& o) {
    std::vector<Family> out;
    for (int f = 0; f < static_cast<int>(Family::Count); ++f)
        if (familySize(p, v, o, static_cast<Family>(f)) > 0) out.push_back(static_cast<Family>(f));
    return out;
}
inline std::string familyTitle(const hmi::Project& p, const hmi::View& v, const hmi::Object& o, Family f) {
    return std::string(familyLabel(f)) + "  [" + std::to_string(familySize(p, v, o, f)) + "]";
}
// Une ligne de surcharge telle que l'arbre la montre : sous Actions, "Clic -> ..."
// sans "Action : ".
inline std::string lineText(const Line& l) {
    if (l.action >= 0 && l.text.rfind("Action : ", 0) == 0) return l.text.substr(9);
    return l.text;
}
// Le nom d'un endroit ou sert un repere ("value (expression)" -> "Valeur",
// "action 1 (valeur)" -> "Action 1", "alarme Defaut (message)" -> "Alarme Defaut").
inline std::string placeLabel(std::string_view where) {
    std::string_view key = where.substr(0, where.find(" ("));
    if (key.rfind("action ", 0) == 0) return "Action " + std::string(key.substr(7));
    if (key.rfind("alarme ", 0) == 0) return "Alarme " + std::string(key.substr(7));
    static const std::pair<const char*, const char*> more[] = {
        {"variable", "Variable API"}, {"state", "Retour d'\xC3\xA9tat"}, {"lamp", "Voyant"},
        {"auth", "Autorisation"}, {"params", "Arguments"}};
    for (const auto& [k, label] : more) if (key == k) return label;
    return std::string(animatedLabel(key));
}
// "$Vanne$ x 3 - Valeur, Ouverture, Libelle" (Q1103, decision 2a) : le nombre de champs, puis leurs noms.
inline std::string markerLine(const hmi::dup::MarkerInfo& m) {
    std::string s = "$" + m.name + "$ \xC3\x97 " + std::to_string(m.spots.size());
    std::vector<std::string> seen;
    for (const auto& sp : m.spots) {
        auto label = placeLabel(sp.where);
        if (std::find(seen.begin(), seen.end(), label) == seen.end()) seen.push_back(std::move(label));
    }
    for (std::size_t i = 0; i < seen.size(); ++i) s += (i ? ", " : " \xE2\x80\x94 ") + seen[i];
    return s;
}
// Ou mene le clic sur un repere : la premiere case qui l'utilise (une propriete :
// sa cle ; une action : son rang).
inline Line markerTarget(const hmi::dup::MarkerInfo& m) {
    Line l;
    l.text = markerLine(m);
    l.icon = ui::Icon::Star;
    for (const auto& sp : m.spots) {
        const std::string_view w = sp.where;
        if (w.rfind("alarme ", 0) == 0) continue;
        if (w.rfind("action ", 0) == 0) { l.action = std::atoi(std::string(w.substr(7)).c_str()) - 1; break; }
        l.property = std::string(w.substr(0, w.find(" (")));
        break;
    }
    return l;
}
// La ligne d'aide, sous les reperes.
inline constexpr std::string_view kMarkersHelp = "Ctrl+D : Dupliquer\xE2\x80\xA6 les remplace";

// Les lignes d'une famille (pas Elements : ce sont des objets), dans l'ordre de
// l'arbre ; Reperes : un par repere, puis la ligne d'aide (sans cible).
inline std::vector<Line> familyLines(const hmi::Project& p, const hmi::Object& o, Family f) {
    std::vector<Line> out;
    if (f == Family::Elements || f >= Family::Count) return out;
    if (f == Family::Params) {
        for (const auto& ip : hmi::pub::instanceParams(p, o)) out.push_back({paramText(ip), ui::Icon::Variable, -1, "params"});
        return out;
    }
    if (f == Family::Markers) {
        for (const auto& m : hmi::dup::ownMarkers(o)) out.push_back(markerTarget(m));
        if (!out.empty()) out.push_back({std::string(kMarkersHelp), ui::Icon::Info, -1, {}});
        return out;
    }
    const auto items = overridesOf(o);
    for (const auto k : familyRanks(items, f)) {
        Line l = items[k];
        l.text = lineText(l);
        out.push_back(std::move(l));
    }
    return out;
}
// Ou mene le clic sur le titre d'une famille : sa premiere ligne (Parametres :
// la case Arguments ; Elements : l'objet seul).
inline Line familyTarget(const hmi::Project& p, const hmi::Object& o, Family f) {
    const auto lines = familyLines(p, o, f);
    return lines.empty() ? Line{} : lines.front();
}
// ---- fin 1.10.3 ----

// Les proprietes animees de la vue : (objet, cle), dans l'ordre des objets.
inline std::vector<std::pair<const hmi::Object*, const hmi::Prop*>> animationsOf(const hmi::View& v) {
    std::vector<std::pair<const hmi::Object*, const hmi::Prop*>> out;
    for (const auto& o : v.objects)
        for (const auto& p : o.props)
            if (!p.expr.empty()) out.emplace_back(&o, &p);
    return out;
}

// ------------------------------------------------------------------ alarmes ---
inline std::string_view priorityText(int p) {
    switch (p) {
        case 1: return "Critique";
        case 2: return "Haute";
        case 3: return "Moyenne";
        default: return "Basse";
    }
}
// Les groupes d'alarmes, dans l'ordre alphabetique ; "" (sans groupe) a la fin.
inline std::vector<std::string> alarmGroups(const hmi::Project& p) {
    std::vector<std::string> out;
    bool none = false;
    for (const auto& a : p.alarms) {
        if (a.group.empty()) { none = true; continue; }
        if (std::find(out.begin(), out.end(), a.group) == out.end()) out.push_back(a.group);
    }
    std::sort(out.begin(), out.end(), [](const std::string& x, const std::string& y) { return hmikit::lower(x) < hmikit::lower(y); });
    if (none) out.emplace_back();
    return out;
}
inline std::vector<const hmi::AlarmDef*> alarmsOf(const hmi::Project& p, std::string_view group) {
    std::vector<const hmi::AlarmDef*> out;
    for (const auto& a : p.alarms) if (a.group == group) out.push_back(&a);
    return out;
}

// ------------------------------------------------------------- utilisateurs ---
inline std::vector<const hmi::User*> usersOf(const hmi::Project& p, hmi::Id group) {
    std::vector<const hmi::User*> out;
    for (const auto& u : p.security.users) if (u.group == group) out.push_back(&u);
    return out;
}
inline std::vector<const hmi::User*> usersWithoutGroup(const hmi::Project& p) {
    std::vector<const hmi::User*> out;
    for (const auto& u : p.security.users) {
        const bool known = std::any_of(p.security.groups.begin(), p.security.groups.end(),
                                       [&](const hmi::UserGroup& g) { return g.id == u.group; });
        if (!known) out.push_back(&u);
    }
    return out;
}

// ------------------------------------------------- les variables employees ---
struct Used { std::string path; bool hmi{false}; int uses{0}; };

// Le texte d'un script ST sans ses commentaires (les chaines restent : c'est
// variablePaths qui les saute).
inline std::string withoutComments(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool str = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char n = i + 1 < s.size() ? s[i + 1] : '\0';
        if (str) { out += c; if (c == '\'') str = false; continue; }
        if (c == '\'') { str = true; out += c; continue; }
        if (c == '(' && n == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? s.size() : end + 1;
            out += ' ';
            continue;
        }
        if (c == '/' && n == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            out += '\n';
            continue;
        }
        out += c;
    }
    return out;
}

// ---- lot 7 : les blocs de declaration d'un script ou d'une fonction --------
//  Le code sans ses blocs VAR / VAR_TEMP / VAR_INPUT ... END_VAR, et les noms
//  qu'ils declarent (en majuscules), avec pour les parametres leur type.
//  1.11.18 (refonte des scripts, lot 2) : lus par hmi::decl::extract, la lecture
//  partagee (une chaine "...", un membre x.VAR ou un repere $VAR$ n'ouvrent plus de
//  bloc ; VAR_IN_OUT, VAR_OUTPUT et les blocs des fonctions internes sont otes du
//  code, leurs noms sont des locales ; le code garde ses colonnes).
struct Declared {
    std::string                                      code;
    std::set<std::string>                            locals;   // en majuscules
    // (nom, TYPE), dans l'ordre ; 1.11.20 : TOUS les parametres - le nom d'une E/S ou d'une sortie
    // porte son mot ("VAR_IN_OUT Graine", "VAR_OUTPUT Tirage"), comme une signature l'ecrit.
    std::vector<std::pair<std::string, std::string>> inputs;
};
inline Declared withoutDeclarations(std::string_view s) {
    Declared out;
    const auto up = [](std::string_view w) {
        std::string u(w);
        for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return u;
    };
    const auto x = hmi::decl::extract(s);
    out.code = x.body;
    for (const auto& d : x.decls) {
        out.locals.insert(up(d.name));
        if (d.section == hmi::decl::Section::Input) out.inputs.emplace_back(d.name, up(d.type));
        else if (d.section == hmi::decl::Section::InOut) out.inputs.emplace_back("VAR_IN_OUT " + d.name, up(d.type));
        else if (d.section == hmi::decl::Section::Output) out.inputs.emplace_back("VAR_OUTPUT " + d.name, up(d.type));
    }
    // Une fonction interne : ses parametres et ses locales ne sont pas des variables de l'application.
    for (const auto& f : x.functions) {
        for (const auto& d : f.decls) out.locals.insert(up(d.name));
        for (const auto& b : f.blocks)
            for (std::size_t i = b.begin; i < b.end && i < out.code.size(); ++i)
                if (out.code[i] != '\n') out.code[i] = ' ';
    }
    return out;
}
// ---- 1.11.21 : LE CONTENU D'UN CODE (les explorateurs le deplient) ----
//  Ce qu'un script ou une fonction declare, lu comme le moteur le lit (decl::extract de son code
//  reconstruit : le modele de ses onglets, puis les blocs restes dans son texte) - ses groupes,
//  dans l'ordre : Parametres (une fonction ; leur mode), Constantes, Variables (les locales
//  d'une fonction), Fonctions internes (FUNCTION ... END_FUNCTION : leur signature, leurs
//  parametres et leurs locales en dessous). Un groupe vide n'est pas rendu.
struct OutlineItem {
    std::string              label;      // "Min : REAL", "RandomSeed : REAL  (E/S)", "Max : INT = 10", "Double(x : INT) : INT"
    std::string              name;       // le nom (aller a sa declaration)
    std::string              tip;        // son commentaire ; vide : aucun
    int                      line{0};    // sa ligne dans le code (une fonction interne : sa premiere ; une
                                         // declaration du modele : 1, ses onglets la montrent)
    std::vector<OutlineItem> children;   // une fonction interne : ses parametres, puis ses locales
};
enum class OutlineKind : std::uint8_t { Parameters = 0, Constants = 1, Variables = 2, Functions = 3 };
struct OutlineGroup {
    OutlineKind              kind{OutlineKind::Variables};
    std::string              label;      // "Param\xC3\xA8tres", "Constantes", "Variables", "Variables locales", "Fonctions internes"
    std::vector<OutlineItem> items;
};
// Une declaration lue, en une ligne : "Max : INT = 10" (une constante), "Compteur : INT := 0",
// "Graine : REAL  (E/S)", "Tirage : REAL  (sortie)", "b : REAL := 0.5" (une entree facultative).
inline std::string outlineLabel(const hmi::decl::Decl& d) {
    std::string s = d.name + " : " + d.type;
    if (!d.initial.empty()) s += (d.constant ? " = " : " := ") + d.initial;
    if (d.section == hmi::decl::Section::InOut) s += "  (E/S)";
    else if (d.section == hmi::decl::Section::Output) s += "  (sortie)";
    else if (d.retain) s += "  (conserv\xC3\xA9" "e)";
    return s;
}
inline std::vector<OutlineGroup> outlineOfCode(std::string_view code, bool function) {
    const auto x = hmi::decl::extract(code);
    const auto item = [](const hmi::decl::Decl& d) {
        OutlineItem it;
        it.label = outlineLabel(d);
        it.name = d.name;
        it.tip = d.comment;
        it.line = d.line;
        return it;
    };
    OutlineGroup params{OutlineKind::Parameters, "Param\xC3\xA8tres", {}};
    OutlineGroup consts{OutlineKind::Constants, "Constantes", {}};
    OutlineGroup vars{OutlineKind::Variables, function ? "Variables locales" : "Variables", {}};
    OutlineGroup inner{OutlineKind::Functions, "Fonctions internes", {}};
    for (const auto& d : x.decls) {
        if (hmi::decl::isParameter(d.section)) params.items.push_back(item(d));
        else if (d.constant) consts.items.push_back(item(d));
        else vars.items.push_back(item(d));
    }
    for (const auto& f : x.functions) {
        OutlineItem it;
        it.name = f.name;
        it.line = f.firstLine;
        std::string sig = f.name + "(";
        bool first = true;
        for (const auto& d : f.decls) {
            if (!hmi::decl::isParameter(d.section)) continue;
            sig += (first ? "" : ", ") + std::string(d.section == hmi::decl::Section::InOut ? "VAR_IN_OUT "
                                                     : d.section == hmi::decl::Section::Output ? "VAR_OUTPUT " : "")
                 + d.name + " : " + d.type;
            first = false;
        }
        it.label = sig + ")" + (f.returnType.empty() ? std::string{} : " : " + f.returnType);
        it.tip = "ligne " + std::to_string(f.firstLine);
        for (const auto& d : f.decls)                                   // ses parametres d'abord...
            if (hmi::decl::isParameter(d.section)) it.children.push_back(item(d));
        for (const auto& d : f.decls)                                   // ... puis ses locales
            if (!hmi::decl::isParameter(d.section)) it.children.push_back(item(d));
        inner.items.push_back(std::move(it));
    }
    std::vector<OutlineGroup> out;
    for (auto* g : {&params, &consts, &vars, &inner})
        if (!g->items.empty()) out.push_back(std::move(*g));
    return out;
}
// La documentation et le stockage des declarations du modele (le code reconstruit ne les dit pas).
inline void outlineDocs(std::vector<OutlineGroup>& groups, const std::vector<hmi::Declaration>& decls) {
    for (auto& g : groups) {
        if (g.kind == OutlineKind::Functions) continue;
        for (auto& it : g.items)
            for (const auto& d : decls) {
                if (d.name != it.name) continue;
                if (it.tip.empty() && !d.description.empty()) it.tip = d.description;
                const bool said = it.label.find("  (") != std::string::npos;
                if (!said && d.kind == hmi::DeclKind::Variable && d.storage == hmi::Storage::Kept) it.label += "  (conserv\xC3\xA9" "e)";
                if (!said && d.kind == hmi::DeclKind::Variable && d.storage == hmi::Storage::Persistent) it.label += "  (persistante)";
            }
    }
}
inline std::vector<OutlineGroup> outlineOf(const hmi::Script& s) {
    if (s.lang != hmi::ScriptLang::ST) return {};                       // C, C++ : rien a deplier
    auto out = outlineOfCode(hmi::decl::codeOf(s), false);
    outlineDocs(out, s.decls);
    return out;
}
inline std::vector<OutlineGroup> outlineOf(const hmi::HmiFunction& f) {
    auto out = outlineOfCode(hmi::decl::codeOf(f), true);
    outlineDocs(out, f.decls);
    return out;
}
// Le titre d'un groupe dans un arbre : "Param\xC3\xA8tres (4)".
inline std::string outlineTitle(const OutlineGroup& g) { return g.label + " (" + std::to_string(g.items.size()) + ")"; }

// "Moyenne(a : REAL, b : REAL) : REAL" ; sans retour : "Tracer(Message : STRING)".
inline std::string signatureOf(const hmi::HmiFunction& f) {
    const auto d = withoutDeclarations(hmi::decl::codeOf(f));          // 1.11.18 (lot 3) : ses parametres du modele aussi
    std::string sig = f.name + "(";
    for (std::size_t k = 0; k < d.inputs.size(); ++k) sig += (k ? ", " : "") + d.inputs[k].first + " : " + d.inputs[k].second;
    sig += ")";
    if (!f.returnType.empty()) sig += " : " + f.returnType;
    return sig;
}

inline bool stKeyword(std::string_view w) {
    static const char* k[] = {"IF", "THEN", "ELSIF", "ELSE", "END_IF", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE",
                              "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT", "CASE", "OF", "END_CASE", "EXIT", "RETURN",
                              "VAR", "VAR_TEMP", "END_VAR", "BOOL", "INT", "DINT", "UINT", "UDINT", "REAL", "LREAL",
                              "WORD", "DWORD", "BYTE", "TIME", "STRING", "EBOOL"};
    std::string u(w);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const char* x : k) if (u == x) return true;
    return false;
}

// Tout ce que l'application lit ou ecrit, par chemin (Armoires[0].ana.PT1.mes),
// avec le nombre d'endroits qui le citent : vues (proprietes, textes a trous,
// actions, securite), scripts, alarmes, recettes, historiques, utilisateurs.
inline std::vector<Used> usedVariables(const hmi::Project& p) {
    std::map<std::string, Used> found;
    // UN EMPLOI = UN ENDROIT (une propriete, une action, un script, une
    // alarme...) : un chemin cite deux fois au meme endroit compte une fois.
    std::vector<std::string> place;
    auto flush = [&] {
        std::sort(place.begin(), place.end());
        place.erase(std::unique(place.begin(), place.end()), place.end());
        for (const auto& key : place) ++found[key].uses;
        place.clear();
    };
    auto hmiVar = [&](std::string_view path) {
        std::size_t end = 0;
        while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
        const auto root = path.substr(0, end);
        return std::any_of(p.programs.variables.begin(), p.programs.variables.end(),
                           [&](const hmi::Variable& v) { return hmikit::same(v.name, root); });
    };
    // Lot 9 : SYS.X et Vue.Objet.Propriete ne sont pas des variables du programme :
    // elles ont leurs dossiers (Variables systeme, Variables d'instances).
    auto isPublic = [&](std::string_view path) {
        const auto dot = path.find('.');
        if (dot == std::string_view::npos || path.substr(0, dot).find('[') != std::string_view::npos) return false;
        const auto root = path.substr(0, dot);
        return hmi::pub::isSysRoot(root) || hmi::pub::viewNamed(p, root) != nullptr;
    };
    auto note = [&](std::string_view expr) {
        for (const auto& path : hmikit::variablePaths(expr)) {
            if (stKeyword(path) || isPublic(path)) continue;
            const auto key = hmikit::lower(path);
            auto& u = found[key];
            if (u.path.empty()) { u.path = path; u.hmi = hmiVar(path); }
            place.push_back(key);
        }
    };
    auto noteTemplate = [&](std::string_view text) {
        std::size_t at = 0;
        while ((at = text.find('{', at)) != std::string_view::npos) {
            const auto close = text.find('}', at);
            if (close == std::string_view::npos) break;
            auto inside = text.substr(at + 1, close - at - 1);
            if (const auto colon = inside.find(':'); colon != std::string_view::npos) inside = inside.substr(0, colon);
            note(inside);
            at = close + 1;
        }
    };
    auto noteList = [&](std::string_view list) {
        for (const auto& item : hmikit::splitList(std::string(list))) note(item);
    };
    auto noteAction = [&](const hmi::Action& a) {
        note(a.watch);
        note(a.guard);
        switch (a.operation) {
            case hmi::Operation::Toggle: case hmi::Operation::Set: case hmi::Operation::Reset:
            case hmi::Operation::Increment: case hmi::Operation::Decrement: case hmi::Operation::Assign:
            case hmi::Operation::RequestResource:
                note(a.target);
                break;
            default: break;
        }
        if (a.operation == hmi::Operation::Assign || a.operation == hmi::Operation::Increment
            || a.operation == hmi::Operation::Decrement)
            note(a.value);
        if (a.operation == hmi::Operation::Log) noteTemplate(a.value);
        if (a.operation == hmi::Operation::RunScript) note(withoutComments(a.value));
    };
    // Lot 7 : une variable locale (ou un parametre) n'est pas une variable
    // employee de l'application : elle ne vit que dans son script.
    std::set<std::string> localNames;
    const auto noteCode = [&](const std::string& code) {
        for (const auto& path : hmikit::variablePaths(code)) {
            std::size_t end = 0;
            while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
            std::string root = path.substr(0, end);
            for (auto& c : root) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (localNames.count(root)) continue;
            note(path);
        }
    };
    auto noteScript = [&](const hmi::Script& s) {
        if (s.lang == hmi::ScriptLang::ST) {
            const auto decl = withoutDeclarations(withoutComments(hmi::decl::codeOf(s)));   // 1.11.18 : ses declarations du modele aussi
            localNames = decl.locals;
            const auto& code = decl.code;
            noteCode(code);
            localNames.clear();
            // Les textes a trous des chaines : IHM_JOURNAL('Niveau {Cuve.niveau:0.0}').
            std::size_t at = 0;
            while ((at = code.find('\'', at)) != std::string::npos) {
                const auto close = code.find('\'', at + 1);
                if (close == std::string::npos) break;
                noteTemplate(std::string_view(code).substr(at + 1, close - at - 1));
                at = close + 1;
            }
        }
        note(s.watch);
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            for (const auto& pr : o.props) {
                if (!pr.expr.empty()) note(pr.expr);
                else if (pr.key == "text") noteTemplate(pr.value);
                else if (pr.key == "variable") note(pr.value);
                else if (pr.key == "variables") noteList(pr.value);
                else if (pr.key == "auth") note(pr.value);
                else if (pr.key == "cells")
                    for (const auto& d : drivenCells(pr.value)) {
                        if (!d.text.empty() && d.text.front() == '=') note(std::string_view(d.text).substr(1));
                        else noteTemplate(d.text);
                    }
                else if (pr.key == "states")
                    for (const auto& st : imageStatesOf(pr.value)) note(st.first);
                flush();
            }
            for (const auto& a : o.actions) { noteAction(a); flush(); }
        }
        for (const auto& a : v.actions) { noteAction(a); flush(); }
        for (const auto& s : v.scripts) { noteScript(s); flush(); }
    }
    for (const auto& s : p.programs.scripts) { noteScript(s); flush(); }
    // Lot 7 : le corps des fonctions IHM (leurs parametres et leur nom exclus).
    for (const auto& f : p.programs.functions) {
        const auto decl = withoutDeclarations(withoutComments(hmi::decl::codeOf(f)));   // 1.11.18 : ses declarations du modele aussi
        localNames = decl.locals;
        std::string fn = f.name;
        for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        localNames.insert(fn);
        noteCode(decl.code);
        localNames.clear();
        flush();
    }
    for (const auto& a : p.alarms) { note(a.condition); noteTemplate(a.message); flush(); }
    for (const auto& r : p.recipes) for (const auto& f : r.fields) { note(f.variable); flush(); }
    for (const auto& item : p.history.archived) { note(item); flush(); }
    for (const auto& u : p.security.users) if (u.protection == "expression") { note(u.expression); flush(); }

    std::vector<Used> out;
    out.reserve(found.size());
    for (auto& [key, u] : found) out.push_back(std::move(u));
    return out;                                        // deja dans l'ordre (cle en minuscules)
}

} // namespace app::hmitree
