// =============================================================================
//  app/SearchFocus.hpp - lot API 8 (finitions) : Ctrl+F dans les volets
// -----------------------------------------------------------------------------
//  Ctrl+F, dans l'ecran d'analyse (MainAnalysisScreen::handleShortcut, apres
//  les widgets : un editeur qui aurait son propre chercher le garde) et dans
//  une fenetre detachee (DetachedWindows::deliver), met le curseur dans le
//  champ de recherche MONTRE de l'onglet en cours, tout son texte choisi : le
//  premier dans l'ordre de l'arbre parmi
//    - la barre des listes de l'API (ApiFilterBar : Variables, Types, Taches,
//      Statistiques, Sous-routines, Simulation...),
//    - le champ des volets (ui::SearchField : Recettes, Utilisateurs, Styles),
//    - un champ dont l'id dit qu'il cherche (le dernier morceau contient
//      search, chercher, recherche, find, filter ou filtre : hmi.alarms.search,
//      hmi.editor.objectsSearch, ...).
//  Echap dans ce champ l'efface (ui::InputText::setEscapeClears) ; vide, il le
//  quitte.
// =============================================================================
#pragma once

#include <string_view>

namespace ui { class Widget; class InputText; }

namespace app {

// Le champ de recherche montre sous `root` (nul : aucun).
[[nodiscard]] ui::InputText* visibleSearchField(ui::Widget& root);
// Un id de champ de recherche (le dernier morceau, sans la casse).
[[nodiscard]] bool looksLikeSearchId(std::string_view id);
// Le curseur dans le champ de recherche montre sous `root`, son texte choisi ;
// faux : aucun champ (ou il ne prend pas le curseur).
bool focusSearchField(ui::Widget& root);

} // namespace app
