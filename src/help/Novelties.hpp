// =============================================================================
//  help/Novelties.hpp - 1.10 (chantier P) : le registre des nouveautes
// -----------------------------------------------------------------------------
//  LES NOUVEAUTES SE VOIENT, A LA PREMIERE OUVERTURE D'UNE NOUVELLE VERSION.
//  Ce module ne dessine rien : il dit QUOI est nouveau et POUR QUI.
//
//   - LE REGISTRE (all()) : chaque nouveaute de chaque version, dans les
//     sources - un identifiant stable, la version, le titre, une phrase, une
//     image facultative (une ressource), ou aller (go), le widget a encadrer
//     en orange (son identifiant ui::Widget::id()), le sujet de l'aide.
//     1.11 : le registre est la table des notes de version (help/ReleaseNotes),
//     ses lignes marquees news, dans leur ordre. Une nouveaute de plus : sa
//     ligne card(...) dans ReleaseNotes.cpp, rien d'autre a changer.
//   - L'ETAT (State) : ce que l'utilisateur a vu, garde dans ses reglages
//     (XPGAnalyser.ini, cles "nouveautes.*") - la derniere version lancee, la
//     version d'avant (ce qui est plus recent est NOUVEAU pour lui), les
//     nouveautes vues (Me montrer, Tout vu), les elements deja utilises (un
//     clic dessus retire leur repere), les reperes masques, l'aide lue.
//
//  OU ALLER (Item::go), une commande que l'application interprete :
//    "arbre:IHM/Configuration/Equipements"   une ligne de l'arbre du projet
//                                            (comme la commande de script arbre)
//    "action:help.news"                     une action de l'application
//    "aide:parametres-popups"               l'aide de l'IHM, sur ce sujet
//    "tuto:objet-vanne"                     1.11 : le tutoriel du sujet (sinon sa page du centre)
//    ""                                     rien a ouvrir (la carte suffit)
//  Le widget (Item::widget) est cherche dans l'ecran du dessus une fois
//  l'endroit ouvert ; vide : pas encore connu (la bulle se pose au centre).
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace help::news {

struct Item {
    std::string id;        // "1.10.simulation-api-ihm" : stable, la cle des reglages
    std::string version;   // "1.10.0"
    std::string title;     // la carte, la bulle
    std::string text;      // une phrase : ce qui est nouveau
    std::string image;     // une image de la carte (resources/...) ; vide : aucune
    std::string go;        // ou aller (voir plus haut) ; vide : rien a ouvrir
    std::string widget;    // l'identifiant du widget a encadrer ; vide : inconnu
    std::string topic;     // le sujet de l'aide de l'IHM ; "aide:<ancre>" : l'aide generale
};

// Tout le registre, de la version la plus recente a la plus ancienne.
[[nodiscard]] const std::vector<Item>& all();
[[nodiscard]] const Item* find(std::string_view id);
// Les nouveautes d'une version ("1.9" ou "1.9.0" : la meme).
[[nodiscard]] std::vector<const Item*> ofVersion(std::string_view version);
// Les versions du registre, de la plus recente a la plus ancienne ("1.10.0", "1.9.0").
[[nodiscard]] std::vector<std::string> versions();

// Deux versions, champ par champ, en nombres ("1.10" > "1.9" ; "1.9" == "1.9.0") :
// -1, 0 ou 1. Un champ illisible vaut 0.
[[nodiscard]] int compareVersions(std::string_view a, std::string_view b);
// "1.10.0" -> "1.10", "1.9.2" -> "1.9.2" : ce que l'ecran ecrit.
[[nodiscard]] std::string shortVersion(std::string_view v);
// La version du registre juste avant `v` ("1.10.0" -> "1.9.0") ; "0" s'il n'y en a pas.
[[nodiscard]] std::string versionBefore(std::string_view v);

// ---- ce que l'utilisateur a vu ----------------------------------------------
struct State {
    std::string              lastVersion;   // nouveautes.version : la derniere version lancee
    std::string              baseline;      // nouveautes.depuis : NOUVEAU = plus recent que celle-ci
    std::string              helpRead;      // nouveautes.aideLue : "Tout marquer comme lu" (l'aide)
    std::vector<std::string> seen;          // nouveautes.vues : vues dans la fenetre (Me montrer, Tout vu)
    std::vector<std::string> used;          // nouveautes.utilisees : l'element a ete clique
    bool                     marksHidden{false};   // nouveautes.reperesMasques
    bool                     helpOnlyNew{false};   // nouveautes.aideFiltre : "Nouveautes seulement"
    // aide.notation : la notation choisie des exemples de l'aide F1 (ST, C ou
    // C++ ; vide : ST). Pas une nouveaute, mais l'aide garde ses choix ici.
    std::string              helpNotation;
};

// Avant la 1.10, rien n'etait note : un profil qui existait deja (des reglages
// d'une version precedente) vient au plus de la 1.9 - on lui montre la 1.9 et
// la 1.10 (kUnknownBefore). Un profil neuf ne voit que la version lancee.
inline constexpr const char* kUnknownBefore = "1.8.0";

// Au lancement de la version `current` : l'etat suit. Vrai : c'est le premier
// lancement d'une version plus recente que la derniere lancee - la fenetre
// "Nouveautes de la <version>" s'ouvre (s'il y a des nouveautes a montrer).
// `existingProfile` : des reglages existaient deja avant ce lancement.
[[nodiscard]] bool onLaunch(State& s, std::string_view current, bool existingProfile);

// Est-ce nouveau pour lui (plus recent que la version d'avant) ?
[[nodiscard]] bool isNew(const State& s, std::string_view version);
// Les nouveautes a montrer dans la fenetre, de la plus recente a la plus
// ancienne (les versions manquees, groupees par version), jusqu'a `current`.
[[nodiscard]] std::vector<const Item*> pending(const State& s, std::string_view current);
// Le repere orange de cet element est-il a l'ecran ? (nouveau, pas utilise, pas masque)
[[nodiscard]] bool markShown(const State& s, const Item& item);
// Les nouveautes dont le widget porte cet identifiant et dont le repere est a l'ecran.
[[nodiscard]] std::vector<const Item*> marksFor(const State& s, std::string_view widgetId);
// L'aide : un sujet ou un paragraphe "depuis `since`" est-il encadre en orange ?
[[nodiscard]] bool helpIsNew(const State& s, std::string_view since);

void markSeen(State& s, std::string_view id);
void markUsed(State& s, std::string_view id);
[[nodiscard]] bool wasSeen(const State& s, std::string_view id);
[[nodiscard]] bool wasUsed(const State& s, std::string_view id);
// "Tout vu" : toutes les nouveautes de pending() vues.
void markAllSeen(State& s, std::string_view current);
// L'aide : "Tout marquer comme lu" (plus rien d'orange dans l'aide).
void markHelpRead(State& s, std::string_view current);

// L'ETAT DE CETTE SESSION, pour tous les ecrans (l'aide, les reperes, la
// fenetre) : l'application le charge de ses reglages au lancement et l'ecrit a
// chaque changement ; les tests le posent a la main.
[[nodiscard]] State& session();
// La version lancee ("1.10.0" : XPG_ANALYZER_VERSION, posee par l'application ;
// une session de captures ou un test peut en essayer une autre).
[[nodiscard]] std::string& sessionVersion();
// "NOUVEAU \xC2\xB7 1.10" : l'etiquette d'un sujet ou d'un paragraphe nouveau.
[[nodiscard]] std::string label(std::string_view version);

// Les reglages : des chaines par cle ("nouveautes.version"...). Les listes sont
// separees par des virgules (les identifiants n'en ont pas).
void load(State& s, const std::function<std::string(const std::string&)>& get);
void save(const State& s, const std::function<void(const std::string&, const std::string&)>& set);

} // namespace help::news
