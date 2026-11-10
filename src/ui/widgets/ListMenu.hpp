// =============================================================================
//  ui/widgets/ListMenu.hpp - 1.47.0 (planche 06) : LE CLIC DROIT, LE MEME ORDRE
//  DANS TOUTES LES LISTES
// -----------------------------------------------------------------------------
//  Un constructeur de menu : la page dit ce qui existe pour la ligne (ou les
//  lignes) choisie, dans n'importe quel ordre ; le menu sort toujours ainsi :
//
//    1. Ouvrir (la fiche, Entree) / Deplier (->) ou Replier (<-)
//    2. les actions de l'element (les plus frequentes d'abord : l'ordre donne)
//    3. Aller a >   (l'admin, la licence, le client, le PC, la commande lies)
//       Filtrer sur > (cet admin, ce client, ce type, cette criticite, ce jour)
//    4. Copier >    (l'identifiant, l'empreinte, la ligne)
//       Exporter... (un CSV de la selection)
//    5. a part, en rouge, avec << ... >> : Retirer, Revoquer, Supprimer
//
//  Une section vide disparait (et ses traits). Un droit qui manque : l'entree
//  est grisee, sa raison dessous (Entry::why). PLUSIEURS LIGNES CHOISIES
//  (Selection::count > 1) : en tete << 3 LICENCES CHOISIES >> ; ni Ouvrir, ni
//  Aller a, ni Filtrer sur (ils visent une ligne) ; << Deplier les 3 >>,
//  << Exporter 3 lignes (CSV) >> ; seules les entrees qui ont un libelle pour
//  plusieurs (Entry::many : << Prolonger les 3... >>, << Revoquer 3
//  licences... >>) restent.
//
//  L'EMPLOI (une page de l'Admin) :
//
//      links_ += ui::ListMenu::bind(*table_, [this] {
//          ui::ListMenu m({table_->selection().size(), "licences", true});
//          m.open("Ouvrir la licence", [this] { ... });
//          m.unfold(table_);                                  // Deplier / Replier les lignes choisies
//          m.action({.label = "Prolonger\xE2\x80\xA6", .many = "Prolonger les %n\xE2\x80\xA6", .icon = ui::Icon::Refresh,
//                    .why = ws().whyNot("licence_prolonger"), .run = [this] { ... }});
//          m.goTo({.label = "Le client", .icon = ui::Icon::User, .run = ...});
//          m.filterOn({.label = "Ce client : Atelier Exemple", .run = [this] { bar_->filterOn(1, "Atelier Exemple"); }});
//          m.copy("L'identifiant", id, ui::Icon::Copy, "Copier les %n identifiants", ids);
//          m.exportRows([this] { ... });
//          m.danger({.label = "R\xC3\xA9voquer", .many = "R\xC3\xA9voquer %n licences", .run = ...});
//          return m;
//      });
//
//  `%n` dans un libelle : le nombre de lignes choisies. La fonction est appelee
//  a chaque clic droit (la ligne cliquee deja choisie) ; ses entrees remplacent
//  le menu commun de la table (Copier, Coller, Tout choisir). Une liste peinte
//  par sa page (le journal) : items() pour son PopupMenu, run(id) au choix.
//  Copier : le texte va au presse-papiers (ui::setClipboardText), puis
//  ListMenu::setCopiedHook (l'Admin le dit dans son bandeau).
// =============================================================================
#pragma once

#include "Controls.hpp"
#include "../../core/Signal.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ui {

class TableView;

class ListMenu {
public:
    struct Selection {
        std::size_t count{1};        // les lignes choisies (0 : le clic droit a cote des lignes)
        std::string noun{"lignes"};  // au pluriel : << licences >>, << clients >>
        bool        feminine{true};  // << choisies >> / << choisis >>
    };
    struct Entry {
        std::string        label;            // une ligne choisie
        std::string        many{};           // plusieurs : son libelle (%n : le nombre) ; vide : pas pour plusieurs
        Icon               icon{Icon::None};
        std::string        why{};            // non vide : grisee, la raison dessous
        std::function<void()> run{};
        std::string        shortcut{};       // a droite : une touche (<< Entree >>) ou un nombre (<< 41 >>)
        std::string        note{};           // une ligne grise dessous (quand elle n'est pas grisee)
        std::vector<Entry> children{};       // un sous-menu (<< Criticite de cette ligne >>)
        bool               radio{false}, checked{false};
    };

    ListMenu() = default;
    explicit ListMenu(Selection selection) : sel_(std::move(selection)) {}

    // ---- les sections (dans n'importe quel ordre : items() les range) ----
    ListMenu& open(std::string label, std::function<void()> run, std::string why = {});
    ListMenu& unfold(bool unfolded, std::function<void()> run);   // << Deplier >> (->) / << Replier >> (<-) ; plusieurs : << Deplier les 3 >>
    ListMenu& unfold(TableView& table);                            // les lignes choisies de cette table, d'apres leur etat
    ListMenu& action(Entry e);
    ListMenu& goTo(Entry e);
    ListMenu& filterOn(Entry e);
    ListMenu& copy(std::string label, std::string text, Icon icon = Icon::None, std::string many = {}, std::string manyText = {});
    ListMenu& copyEntry(Entry e);
    ListMenu& exportRows(std::function<void()> run, std::string why = {});   // << Exporter la ligne (CSV) >> / << Exporter 3 lignes (CSV) >>
    ListMenu& exportEntry(Entry e);                                            // un autre export (<< Exporter le fichier de licence... >>)
    ListMenu& danger(Entry e);                                                 // << ... >> ajoutes s'ils manquent
    ListMenu& note(std::string text, std::string sub = {});                    // une ligne grise a la fin (<< Rien ne s'efface du journal >>)

    // ---- le menu ----
    // Les entrees, dans l'ordre de la regle ; les id a partir de `firstId` (une table : 1000 et plus).
    [[nodiscard]] std::vector<PopupMenu::Item> items(int firstId = 1000) const;
    bool run(int id) const;                                     // l'entree choisie (faux : pas a lui, ou grisee)
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const Selection& selection() const noexcept { return sel_; }
    // Les essais : les libelles montres, sous-menus a plat (<< Aller \xC3\xA0 > Son PC >>), sans les traits.
    [[nodiscard]] std::vector<std::string> labels() const;
    // Choisir une entree par son libelle (<< Aller \xC3\xA0 > Son PC >> pour un sous-menu) ; faux : absente ou grisee.
    bool runLabel(const std::string& label) const;

    // Le texte de la regle (les essais, les autres pages) : << Aller \xC3\xA0 >>, << Filtrer sur >>, << Copier >>.
    static constexpr const char* kGoTo = "Aller \xC3\xA0";
    static constexpr const char* kFilterOn = "Filtrer sur";
    static constexpr const char* kCopy = "Copier";

    // Le menu d'une table : `build` a chaque clic droit (et Maj+F10) ; ses entrees remplacent celles de la table.
    // Garder la connexion rendue (links_ += ...).
    [[nodiscard]] static core::Connection bind(TableView& table, std::function<ListMenu()> build);
    // Apres une copie (le texte copie, ce que c'etait : << L'identifiant >>) - l'Admin le dit dans son bandeau.
    static void setCopiedHook(std::function<void(const std::string& what, const std::string& text)> hook);

private:
    enum class Sec : int { Open = 0, Actions, GoTo, FilterOn, Copy, Export, Danger, Note };
    struct Added {
        Sec   sec;
        Entry e;
    };
    [[nodiscard]] bool many() const noexcept { return sel_.count > 1; }
    [[nodiscard]] std::string labelOf(const Entry& e) const;   // une ligne : label ; plusieurs : many (%n remplace)
    [[nodiscard]] bool shown(const Added& a) const;
    Selection          sel_{};
    std::vector<Added> added_;
    mutable std::vector<std::function<void()>> runs_;          // id - firstId -> l'action (rempli par items())
    mutable int        firstId_{1000};
};

} // namespace ui
