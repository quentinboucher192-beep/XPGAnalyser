// help/F1Table.cpp - 1.11 (chantier T2, tranche 16) : la table de F1 (voir l'en-tete).
#include "F1Table.hpp"

#include "../hmi/HmiGuide.hpp"

namespace help::f1 {

const std::vector<Place>& places() {
    static const std::vector<Place> k = {
        // ---- l'IHM : le volet au centre, ou le noeud choisi dans l'arbre ----
        {Kind::HmiPlace, "vue", "\xC3\x89" "diteur de vue", "editeur", "help.hmi"},
        {Kind::HmiPlace, "actions", "\xC3\x89" "diteur de vue \xE2\x80\xBA inspecteur \xE2\x80\xBA Actions", "actions", "help.hmi"},
        {Kind::HmiPlace, "contenu", "\xC3\x89" "diteur de vue \xE2\x80\xBA inspecteur \xE2\x80\xBA Contenu", "contenu", "help.hmi"},
        {Kind::HmiPlace, "raccourcis", "\xC3\x89" "diteur de vue \xE2\x80\xBA inspecteur \xE2\x80\xBA Raccourcis", "raccourcis-vue", "help.hmi"},   // 1.11.23
        {Kind::HmiPlace, "scripts-vue", "Script d'une vue", "scripts", "help.hmi"},
        {Kind::HmiPlace, "scripts", "Scripts g\xC3\xA9n\xC3\xA9raux", "scripts", "help.hmi"},
        {Kind::HmiPlace, "fonctions", "Fonctions", "fonctions", "help.hmi"},
        {Kind::HmiPlace, "ihm", "IHM (le dossier de l'arbre)", "ihm", "help.hmi"},
        {Kind::HmiPlace, "vues", "Vues", "vues", "help.hmi"},
        {Kind::HmiPlace, "config", "Configuration", "configuration", "help.hmi"},
        {Kind::HmiPlace, "fichiers", "Fichiers externes", "fichiers", "help.hmi"},
        {Kind::HmiPlace, "ressources", "Ressources", "ressources", "help.hmi"},
        {Kind::HmiPlace, "echange", "Exporter / Importer", "echange", "help.hmi"},
        {Kind::HmiPlace, "symboles", "Symboles", "symboles", "help.hmi"},
        {Kind::HmiPlace, "styles", "Styles", "styles-nommes", "help.hmi"},
        {Kind::HmiPlace, "rechercher", "Rechercher / remplacer", "rechercher-remplacer", "help.hmi"},
        {Kind::HmiPlace, "essais", "Essais de r\xC3\xA9" "ception", "essais", "help.hmi"},
        {Kind::HmiPlace, "langues", "Langues", "langues", "help.hmi"},
        {Kind::HmiPlace, "unites", "Unit\xC3\xA9s", "unites-formats", "help.hmi"},
        {Kind::HmiPlace, "variables", "Variables IHM", "variables-ihm", "help.hmi"},
        {Kind::HmiPlace, "types-ihm", "Types IHM", "types-ihm", "help.hmi"},
        {Kind::HmiPlace, "variables-systeme", "Variables syst\xC3\xA8me", "variables-systeme", "help.hmi"},
        {Kind::HmiPlace, "variables-instances", "Variables d'instances", "variables-instances", "help.hmi"},
        {Kind::HmiPlace, "alarmes", "Alarmes", "alarmes", "help.hmi"},
        {Kind::HmiPlace, "recettes", "Recettes", "recettes", "help.hmi"},
        {Kind::HmiPlace, "utilisateurs", "Utilisateurs", "securite", "help.hmi"},
        {Kind::HmiPlace, "historiques", "Historiques", "historiques", "help.hmi"},
        {Kind::HmiPlace, "communication", "Communication", "communication", "help.hmi"},
        {Kind::HmiPlace, "reseau-pc", "Communication \xE2\x80\xBA R\xC3\xA9seau du PC", "reseau-pc", "help.hmi"},
        {Kind::HmiPlace, "scanner-ip", "Communication \xE2\x80\xBA Scanner IP", "scanner-ip", "help.hmi"},
        {Kind::HmiPlace, "equipements", "Communication \xE2\x80\xBA \xC3\x89quipements", "equipements", "help.hmi"},
        {Kind::HmiPlace, "variables-liees", "Communication \xE2\x80\xBA Variables li\xC3\xA9" "es", "variables-liees", "help.hmi"},
        {Kind::HmiPlace, "poste", "Poste d'exploitation", "poste-exploitation", "help.hmi"},
        {Kind::HmiPlace, "notifications", "Notifications", "notifications", "help.hmi"},
        {Kind::HmiPlace, "rapports", "Rapports", "rapports", "help.hmi"},
        {Kind::HmiPlace, "web", "Acc\xC3\xA8s web", "acces-web", "help.hmi"},
        {Kind::HmiPlace, "simulation", "Simulation de l'IHM", "simulation", "help.hmi"},
        {Kind::HmiPlace, "outil", "Outil Modbus", "outil-modbus", "help.hmi"},
        {Kind::HmiPlace, "generer", "G\xC3\xA9n\xC3\xA9rer", "verifier", "help.hmi"},
        {Kind::HmiPlace, "compiler", "Compiler", "verifier", "help.hmi"},
        // ---- l'ecran du projet : un endroit qui nomme son sujet ----
        {Kind::HmiKey, "historique", "Historique du projet (le panneau)", "historique", "help.hmi"},
        {Kind::HmiKey, "grafcet", "\xC3\x89" "diteur de grafcet", "grafcet", "help.hmi"},
        // ---- les onglets de l'API (sans mot ni bloc plus precis) ----
        // Tranche 17 (decision 3 du chef, 03/10) : Types, DFB, Unites, Sous-routines et
        // Variables prennent le sujet le plus precis du centre s'il existe. Types : le seul
        // sujet sur les DDT (L'automate (API), "Les DDT dans les popups de l'IHM"). DFB,
        // Unites, Sous-routines : aucun sujet general (le chapitre Blocs DFB / DDT n'a qu'une
        // page par bloc de la bibliotheque ; "grafcet" ne parle que de DFB_GRAFCETENGINE) :
        // L'arbre du projet reste. Variables : aucun sujet sur les variables de l'automate
        // ("variables-ihm" parle du tableau des variables IHM) : la recherche et les filtres restent.
        {Kind::ApiTab, "api", "API \xE2\x80\xBA Tableau de bord", "api-arbre-lot8", "help"},
        {Kind::ApiTab, "taches", "API \xE2\x80\xBA T\xC3\xA2" "ches (MAST)", "api-ordre", "help"},
        {Kind::ApiTab, "ordre", "API \xE2\x80\xBA Ordre des sections", "api-ordre", "help"},
        {Kind::ApiTab, "variables", "API \xE2\x80\xBA Variables", "api-filtres", "help"},
        {Kind::ApiTab, "types", "API \xE2\x80\xBA Types", "api-ddt-popups", "help"},
        {Kind::ApiTab, "dfb", "API \xE2\x80\xBA DFB", "api-arbre-lot8", "help"},
        {Kind::ApiTab, "unites", "API \xE2\x80\xBA Unit\xC3\xA9s de programme", "api-arbre-lot8", "help"},
        {Kind::ApiTab, "sous-routines", "API \xE2\x80\xBA Sous-routines", "api-arbre-lot8", "help"},
        {Kind::ApiTab, "tables", "API \xE2\x80\xBA Tables d'animation", "api-sim-forcages", "help"},
        {Kind::ApiTab, "configuration", "API \xE2\x80\xBA Configuration", "api-importer", "help"},
        {Kind::ApiTab, "statistiques", "API \xE2\x80\xBA Statistiques", "api-filtres", "help"},
        {Kind::ApiTab, "comparer", "API \xE2\x80\xBA Comparer", "api-reimporter", "help"},
        {Kind::ApiTab, "variables-inutilisees", "API \xE2\x80\xBA Variables inutilis\xC3\xA9" "es", "api-filtres", "help"},
        {Kind::ApiTab, "rafraichir", "API \xE2\x80\xBA Rafra\xC3\xAE" "chir", "api-reimporter", "help"},
        {Kind::ApiTab, "import-xhw", "API \xE2\x80\xBA Importer le .XHW", "api-importer", "help"},
        {Kind::ApiTab, "didacticiel", "API \xE2\x80\xBA Didacticiel", "api-arbre-lot8", "help"},
        {Kind::ApiTab, "bibliotheque", "API \xE2\x80\xBA Biblioth\xC3\xA8que", "", "help.blocs"},
        {Kind::ApiTab, "macros", "API \xE2\x80\xBA Macros (aucune macro choisie)", "", "help.macros"},
        // ---- les onglets de la simulation (lot8::helpAnchorFor) ----
        {Kind::SimTab, "ensemble", "Simulation \xE2\x80\xBA Vue d'ensemble", "api-sim-ensemble", "help"},
        {Kind::SimTab, "automate", "Simulation \xE2\x80\xBA Automate", "api-sim-ensemble", "help"},
        {Kind::SimTab, "debogage", "Simulation \xE2\x80\xBA D\xC3\xA9" "bogage", "api-sim-debogage", "help"},
        {Kind::SimTab, "forcages", "Simulation \xE2\x80\xBA For\xC3\xA7" "ages", "api-sim-forcages", "help"},
        {Kind::SimTab, "courbes", "Simulation \xE2\x80\xBA Courbes", "api-sim-courbes", "help"},
        {Kind::SimTab, "journal", "Simulation \xE2\x80\xBA Journal", "api-sim-journal", "help"},
        // ---- les ecrans et les fenetres ----
        {Kind::Screen, "themes", "Les th\xC3\xA8mes (la galerie)", "api-themes", "help"},
        {Kind::Screen, "renommer", "Renommer partout (F2)", "api-renommer", "help"},
        {Kind::Screen, "glisser", "Glisser un fichier (la fen\xC3\xAAtre)", "api-glisser", "help"},
        {Kind::Screen, "poste", "Le poste d'exploitation (plein \xC3\xA9" "cran)", "poste-exploitation", "help.hmi"},
    };
    return k;
}

std::string keyForPlace(std::string_view place) {
    if (place.empty()) return {};
    for (const auto& p : places())
        if (p.kind == Kind::HmiPlace && p.id == place) return std::string(p.key);
    return hmi::guide::topicForPlace(place);
}

namespace {
const Place* rowOf(Kind kind, std::string_view id) {
    for (const auto& p : places())
        if (p.kind == kind && p.id == id) return &p;
    return nullptr;
}
} // namespace

const Place* forApiTab(std::string_view tab) { return rowOf(Kind::ApiTab, tab); }
const Place* forSimTab(std::string_view tab) { return rowOf(Kind::SimTab, tab); }

std::string_view placeOfProgrammingTab(std::size_t tab) noexcept {
    static constexpr std::string_view kTabs[] = {"scripts", "variables", "types-ihm", "methodes-symboles"};
    return tab < std::size(kTabs) ? kTabs[tab] : kTabs[0];
}

} // namespace help::f1
