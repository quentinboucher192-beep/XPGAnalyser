// =============================================================================
//  license_admin/gui/ListKit.hpp - 1.47.0 (planches 06, 07, 12) : CE QUE LES
//  LISTES DE L'ADMIN PARTAGENT
// -----------------------------------------------------------------------------
//  Les trois composants sont dans src/ui (sans l'Admin) :
//    ui::FilterBar   la barre << Filtrer et chercher >> (FilterBar.hpp)
//    ui::ListMenu    le clic droit, le meme ordre partout (ListMenu.hpp)
//    ui::UnfoldCard  ce qu'une ligne depliee montre ; TableView::setUnfold (UnfoldCard.hpp, DataViews.hpp)
//  Ici, ce que l'Admin y ajoute :
//    - LA MEMOIRE DES LISTES : ce qui est choisi et << Mes filtres >> de chaque
//      liste, sur ce PC (le fichier listes-filtres.txt du dossier de l'Admin) -
//      la coquille la branche (installMemory), les barres la lisent par leur
//      cle (FilterBar::setMemoryKey("commandes")) ;
//    - LES ADMINS : la couleur de chacun (l'admin+, puis les admins dans
//      l'ordre de la liste : planche 00, extra.css), sa pastille (son nom, >
//      vers sa fiche) ;
//    - QUI A FAIT QUOI : la derniere ligne du journal des admins d'une action
//      sur un objet (<< signee et livree par Lea Essai >>) ;
//    - EXPORTER DES LIGNES (le CSV pour Excel : UTF-8 avec BOM, point-virgule).
//
//  UNE PAGE DE PLUS (Offres, Options, Versions...), en quatre gestes :
//    1. bar_ = &adopt(*this, std::make_unique<ui::FilterBar>("<page>", champs)) ; setSource (toutes ses lignes :
//       le texte de chaque champ) ; linkTable(*table_) ; setMemoryKey("<page>") ; changed -> rebuild() ; dans
//       rebuild, `if (!bar_->accepts(i)) continue;` ; dans refresh, bar_->refreshCounts().
//    2. onLayout : bar_->setBounds({x, y, w, bar_->heightFor(w)}) au-dessus de la table.
//    3. le clic droit : links_ += ui::ListMenu::bind(*table_, [this] { ... }) (l'ordre : ListMenu.hpp).
//    4. le depliage : table_->setUnfold([this](ui::RowIndex r) { auto c = std::make_unique<ui::UnfoldCard>(...); ...
//       return c; }) ; table_->setEnterActivates(true) ; activated -> la fiche.
//  Une case a plusieurs valeurs (les offres d'une option) : Field::split ; un titre de colonne entier a cote de son
//  entonnoir (la table defile plutot que de le couper) : sa largeur = le titre + 46 px.
//  Les pages faites ainsi : Commandes (PageShop.cpp), Journal des admins (une liste peinte : PageJournal.cpp), Licences,
//  Clients, Demandes en ligne, Activations, Offres, Options, Fonctionnalites (PageEditions.cpp : la base RegistryPage
//  les partage), Licences flottantes (PageFloating.cpp). Aller d'une liste a l'autre : requestPageFocus(page,
//  "offre:<id>" | "option:<id ou nom>" | "fonction:<libelle>" | "client:<nom>" | "licence:<id>").
// =============================================================================
#pragma once

#include "Page.hpp"

#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/FilterBar.hpp"
#include "../../ui/widgets/ListMenu.hpp"
#include "../../ui/widgets/UnfoldCard.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace license_admin::gui::listkit {

// ---- la memoire des listes (ui::FilterMemory) ----
// Brancher (la coquille, a sa creation) : <dossier>/listes-filtres.txt, une cle par ligne. `owner` : qui l'a branchee
// (seul lui la debranche).
void installMemory(const std::string& folder, const void* owner);
void uninstallMemory(const void* owner);
[[nodiscard]] std::string memoryFile(const std::string& folder);

// ---- les admins ----
// La couleur d'un admin (sa cle) : 0 l'admin+, puis 1, 2... les admins dans l'ordre de la liste ; -1 : inconnu, retire.
[[nodiscard]] int adminColor(const Workspace& w, const std::string& key);
// Le nom d'un admin (sa cle) : << Lea Essai >>, l'admin+ (le nom de la cle de l'editeur) ; vide : inconnu.
[[nodiscard]] std::string adminName(const Workspace& w, const std::string& key);
// Sa pastille : son initiale sur sa couleur, son nom, > vers sa fiche (Administrateurs, lui choisi).
[[nodiscard]] ui::UnfoldCard::Person person(const ShellHost& host, const std::string& key, const std::string& name, std::string count = {});

// ---- qui a fait quoi (le journal des admins de ce poste) ----
struct Did {
    std::string   key, who;      // la cle de l'admin, son nom (<< Le service >>)
    Seconds       when{0};
    std::string   action, summary;
    std::uint64_t number{0};     // la ligne du journal
};
class WhoDid {
public:
    explicit WhoDid(const Workspace& w) : w_(w) {}
    void reset() { loaded_ = false; }   // le journal a peut-etre change (un refresh)
    // La derniere ligne d'une de ces actions sur cet objet (<< XPG-2026-00412 >>, << commande 212 >>, << demande 66 >>) ;
    // actions vides : n'importe laquelle.
    [[nodiscard]] std::optional<Did> last(const std::vector<std::string>& actions, const std::string& object) const;
    // Toutes les lignes sur cet objet, des plus anciennes aux plus recentes (son histoire).
    [[nodiscard]] std::vector<Did> history(const std::string& object) const;

private:
    void load() const;
    const Workspace&          w_;
    mutable bool              loaded_{false};
    mutable std::vector<Did>  lines_;
};

// ---- exporter des lignes ----
// Les lignes du modele `rows` d'une table, ses colonnes montrees (les titres en premiere ligne), en CSV pour Excel :
// l'explorateur (Enregistrer sous), puis le fichier ; le resultat au bandeau. `what` : << commandes >>.
void exportRows(const ShellHost& host, const ui::TableView& table, std::vector<ui::RowIndex> rows, const std::string& fileName, const std::string& what);

// Les jours (pour les periodes) : une date du Workspace telle quelle (secondes).
[[nodiscard]] inline std::int64_t seconds(Seconds t) { return static_cast<std::int64_t>(t); }

} // namespace license_admin::gui::listkit
