#pragma once
// 1.11 (T1, tranche 24, decision 53 : R111-12, les tutoriels de Demarrer) : ce que la scene lit
// de l'ECRAN, et non du projet du bac, sans ecran. TutorialApp.cpp en donne l'etat (la pile des
// menus, l'onglet courant) ; les essais (tests/tutorial_test.cpp) le rejouent.
//
//  - centre.ouvert : "oui" quand le centre d'aide est ouvert, c'est-a-dire qu'un de ses ecrans est
//    dans la pile des menus (MenuManager::path) : "help" (F1, Aide > Aide) ou un de ses chapitres
//    ("help.macros", "help.blocs", "help.hmi", App::buildMenuFactory) ; "non" sinon. Une remise a
//    neuf rouvre le bac (SwitchMenu("analysis") vide la pile) : avant les gestes d'un "A toi"
//    "Ouvre le centre d'aide (F1)", c'est "non".
//  - onglet.courant : le titre de l'onglet courant de l'espace de travail (le groupe actif de
//    "analysis.centre") ; "" quand aucun onglet n'est en vue (sous le centre d'aide, par exemple).
//  - la cible barre:<partie> : une partie de la barre du haut (TopBar::partRect), avec les noms de
//    la commande barre de ScriptRunner (projet, annuler, nouveau, aller, simuler, affichage,
//    aide...). barre:? est barre:aide, le "?" qui ouvre le menu Aide (Aide F1, Nouveautes...).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace help::screen {

// Un ecran du centre d'aide (HelpCenterScreen), par l'identifiant de son menu.
inline bool isHelpCenterMenu(std::string_view id) {
    return id == "help" || id == "help.macros" || id == "help.blocs" || id == "help.hmi";
}

// 1.11.2 (T1, decision 141 : les macros ; le clic sur API > Macros laisse l'onglet « API », onglet.courant ne
// le voit pas) : la ligne choisie de l'arbre du projet (TreeView::currentNode de "analysis.explorer").
//  - arbre.choisi : son chemin, les libelles de ses parents puis le sien, separes par "/", comme la cible
//    arbre: les ecrit (« API/Macros ») ; "" : aucune ligne choisie, ou elle est hors de vue (dossier replie).
//  - arbre.choisi(<chemin>) : "oui" quand la ligne choisie est celle que vise arbre:<chemin>, "non" sinon. Comme
//    la cible : chaque morceau est le debut d'un libelle, sans la casse, cherche apres le precedent ; le dernier
//    est la ligne choisie elle-meme (arbre:IHM/Programmation vise « Programmation generale »).
// Un libelle perd son compteur (« Macros  [31] », « Types derives (12) ») : ProjectTreeModel::text le met deja
// en pastille pour la plupart des dossiers.

// Une ligne visible de l'arbre, dans l'ordre de l'affichage : sa profondeur et son libelle.
struct TreeRow {
    int depth = 0;
    std::string text;
};

// « Macros  [31] » -> « Macros » ; « Types derives (12) » -> « Types derives » ; le reste ne change pas.
inline std::string treeLabel(std::string_view text) {
    std::string s(text);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.size() >= 3 && (s.back() == ']' || s.back() == ')')) {
        const char open = s.back() == ']' ? '[' : '(';
        const auto at = s.rfind(open);
        bool digits = at != std::string::npos && at + 2 < s.size();
        for (std::size_t k = at == std::string::npos ? s.size() : at + 1; digits && k + 1 < s.size(); ++k)
            digits = s[k] >= '0' && s[k] <= '9';
        if (digits && at > 0 && s[at - 1] == ' ') {
            s.erase(at);
            while (!s.empty() && s.back() == ' ') s.pop_back();
        }
    }
    return s;
}

// Le chemin de la ligne rows[at] : les libelles de ses parents (la derniere ligne au-dessus d'elle a chaque
// profondeur plus petite), puis le sien. Hors des lignes : vide.
inline std::vector<std::string> treePath(const std::vector<TreeRow>& rows, std::size_t at) {
    std::vector<std::string> out;
    if (at >= rows.size()) return out;
    int depth = rows[at].depth;
    out.push_back(treeLabel(rows[at].text));
    for (std::size_t k = at; k-- > 0 && depth > 0;)
        if (rows[k].depth < depth) {
            depth = rows[k].depth;
            out.insert(out.begin(), treeLabel(rows[k].text));
        }
    return out;
}

inline std::string lowerAscii(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// La ligne de chemin `chosen` est-elle celle que vise arbre:<wanted> ? (voir arbre.choisi(<chemin>) plus haut)
inline bool treePathMatches(const std::vector<std::string>& chosen, std::string_view wanted) {
    std::vector<std::string> parts;
    for (std::size_t from = 0;;) {
        const auto slash = wanted.find('/', from);
        parts.push_back(lowerAscii(wanted.substr(from, slash == std::string_view::npos ? std::string_view::npos : slash - from)));
        if (slash == std::string_view::npos) break;
        from = slash + 1;
    }
    if (chosen.empty() || parts.back().empty() || lowerAscii(chosen.back()).rfind(parts.back(), 0) != 0) return false;
    std::size_t part = 0;
    for (std::size_t k = 0; k + 1 < chosen.size() && part + 1 < parts.size(); ++k)
        if (lowerAscii(chosen[k]).rfind(parts[part], 0) == 0) ++part;
    return part + 1 == parts.size();
}

// Ce que la scene sait de l'ecran au moment de lire.
struct State {
    std::vector<std::string> menus;   // la pile des menus, de la racine au dessus
    std::optional<std::string> tab;   // le titre de l'onglet courant ; vide : aucun onglet en vue
    std::vector<std::string> tree;    // 1.11.2 : le chemin de la ligne choisie de l'arbre (treePath) ; vide : aucune
};

// La valeur d'un chemin de l'ecran ; nullopt : ce n'est pas un chemin de l'ecran (la scene
// cherche ailleurs).
inline std::optional<std::string> read(std::string_view path, const State& s) {
    if (path == "centre.ouvert") {
        for (const auto& id : s.menus)
            if (isHelpCenterMenu(id)) return std::string("oui");
        return std::string("non");
    }
    if (path == "onglet.courant") return s.tab.value_or(std::string());
    if (path == "arbre.choisi") {
        std::string out;
        for (const auto& part : s.tree) out += (out.empty() ? "" : "/") + part;
        return out;
    }
    if (path.rfind("arbre.choisi(", 0) == 0 && path.size() > 14 && path.back() == ')')
        return std::string(treePathMatches(s.tree, path.substr(13, path.size() - 14)) ? "oui" : "non");
    return std::nullopt;
}

// Le chemin se lit-il de l'ecran ? (la scene remplit alors State::tree seulement pour arbre.choisi)
inline bool isTreePath(std::string_view path) {
    return path == "arbre.choisi" || path.rfind("arbre.choisi(", 0) == 0;
}

// barre:<nom> -> la partie de la barre du haut (TopBar::partRect), en minuscules ; "?" : "aide".
inline std::string topBarPart(std::string_view name) {
    std::string out(name);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out == "?" ? std::string("aide") : out;
}

} // namespace help::screen
