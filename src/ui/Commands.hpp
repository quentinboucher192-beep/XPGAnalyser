// =============================================================================
//  ui/Commands.hpp - 1.12.18 : LA TABLE DES COMMANDES DU CLIC DROIT
// -----------------------------------------------------------------------------
//  UN MOT, UNE ICONE, UNE TOUCHE PAR ACTION. Avant, chaque menu ecrivait les
//  siens : « Epingler en haut » ici, « Epingler » la, « Favorite » ailleurs ;
//  Copier avec la feuille, Supprimer avec la croix ; F7 affiche dans un menu,
//  oublie dans l'autre. Un menu ecrit maintenant `cmd::item(cmd::Id::Copier, id)`
//  et recoit le libelle, l'icone et le raccourci de la table : ils ne peuvent
//  plus diverger d'un menu a l'autre.
//
//  LA COLONNE DE DROITE NE PORTE QUE LE RACCOURCI. Un compte va dans le
//  libelle (« Fermer tout (5) »), un lieu sous le libelle ; looksLikeShortcut()
//  dit si un texte est bien une touche - commands_test le verifie pour chaque
//  entree de la table, et les essais des ecrans pour leurs menus.
// =============================================================================
#pragma once

#include "widgets/Containers.hpp"   // 1.12.18 : wireSubTabMenu
#include "widgets/Controls.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ui::cmd {

enum class Id : std::uint16_t {
    // edition
    Couper, Copier, Coller, CollerIci, Dupliquer, DupliquerFenetre, ToutChoisir,
    Renommer, RenommerPartout, Supprimer, Retirer, Ajouter, Monter, Descendre,
    // ouvrir, garder
    Ouvrir, OuvrirFenetre, AfficherArbre, Epingler, Desepingler, Aide,
    // le code et les noms
    AllerDefinition, VoirUsages, ChoisirFonction, Corriger, CopierNom, CopierValeur,
    // la simulation
    Forcer, Relacher, ToutRelacher, SuivreCourbe, Simuler, Arreter, Compiler,
    // le projet
    CreerVersion, ImporterXhw, ReanalyserFichiers, ExporterZip,
    // le canevas
    Grouper, Degrouper, CopierStyle, CollerStyle, Verrouiller, Deverrouiller,
    // les arbres
    Deplier, Replier, ToutDeplier, ToutReplier,
    // les onglets, les fichiers
    Fermer, RouvrirOnglet, Actualiser, DossierParent,
    Count
};

struct Def {
    Id               id;
    std::string_view label;      // « Copier », « Renommer… »
    Icon             icon;
    std::string_view shortcut;   // « Ctrl+C » ; vide : aucune touche
};

[[nodiscard]] const Def& def(Id) noexcept;
[[nodiscard]] const std::vector<Def>& all();

// Une entree de menu tiree de la table. `label` : un libelle plus precis
// (« Copier la valeur », « Supprimer les 3… ») ; vide : celui de la table.
// `reason` : pourquoi elle est grisee (toujours dit, jamais « pas maintenant »).
[[nodiscard]] PopupMenu::Item item(Id, int itemId, bool enabled = true, std::string reason = {}, std::string label = {});

// Une regle et une ligne d'en-tete (ce qu'on vise : « Pompe_1 · instance de
// Sym_Pompe », « 3 objets »).
[[nodiscard]] PopupMenu::Item separator();
[[nodiscard]] PopupMenu::Item heading(std::string label, std::string right = {});

// Le texte ressemble-t-il a une touche : « Ctrl+Maj+D », « F2 », « Suppr »,
// « Entrée », « Alt+↑ », « Ctrl+K, Ctrl+C » ? Un compte (« 5 onglets »), une
// heure, « Double-clic » : non.
[[nodiscard]] bool looksLikeShortcut(std::string_view text);

// Les entrees d'un menu (et de ses sous-menus) dont la colonne de droite n'est
// pas une touche - vide : le menu est propre. Pour les essais des ecrans.
[[nodiscard]] std::vector<std::string> badShortcuts(const std::vector<PopupMenu::Item>& items);

// 1.12.18 (bug 7, planche 20) : LE MENU D'UNE BANDE DE SOUS-ONGLETS (inspecteur, panneau du bas,
// simulation...). Le clic droit choisit l'onglet, puis ouvre ses entrees (`entries`, peut-etre
// aucune), et toujours Disposition... (Ctrl+Maj+K). `run` recoit l'onglet et l'id choisi. Le menu
// est un enfant de `owner` ; ses liaisons vivent dans `links`.
using TabEntries = std::function<std::vector<PopupMenu::Item>(std::size_t tab)>;
using TabRun = std::function<void(std::size_t tab, int id)>;
inline constexpr int kDispositionItem = 9090;
void wireSubTabMenu(TabControl& tabs, Widget& owner, core::ConnectionScope& links, TabEntries entries = {}, TabRun run = {});
// Ce que fait Disposition... (l'ecran le branche une fois : la fenetre Disposition) ; vide : grise.
void setDispositionHook(std::function<void()> hook);
[[nodiscard]] PopupMenu::Item dispositionItem();   // l'entree (id kDispositionItem), grisee sans crochet
void openDisposition();

}   // namespace ui::cmd
