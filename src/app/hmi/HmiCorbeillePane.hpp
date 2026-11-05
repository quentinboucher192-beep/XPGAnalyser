// =============================================================================
//  app/hmi/HmiCorbeillePane.hpp - 1.25.1 : Projet > Corbeille (le volet)
// -----------------------------------------------------------------------------
//  Le client (05/11) : << fait une corbeille bien rangee avec recherche possible >>.
//  Ce que les corbeilles inscrites gardent (app/Corbeille.hpp), en un volet :
//
//    [Restaurer] [Supprimer definitivement] [Vider la corbeille] | [Ouvrir dans l'explorateur] [Actualiser]  [Chercher...]
//    +--------------------------------------------------------------------------------------------------+
//    | v Enregistrements (3)                                                                            |
//    |     Nuit                05/11/2026 17:30   C:\Projets\Station\ihm\ressources\enregistrements  12 Ko |
//    | v Conversations IA (2)                                                                           |
//    |     Bilan de mise en service  ...                                                                 |
//    +--------------------------------------------------------------------------------------------------+
//    Les elements supprimes il y a plus de 7 jours partent a l'ouverture suivante (les macros restent).
//    12 elements, 3,4 Mo
//
//  Un dossier par genre, replie ou deplie (son nombre), comme la bibliotheque de sons ; la recherche (chaque mot
//  commence un mot du nom, du genre ou de l'origine, sans casse ni accents) deplie ce qui trouve. Plusieurs d'un coup
//  (Ctrl+clic, Maj+clic, Ctrl+A) : Restaurer (Entree ; un seul Ctrl+Z les remet dans la corbeille), Supprimer
//  definitivement (Suppr : une question, sans retour), Vider la corbeille (une question). La colonne Origine est un
//  chemin (le dossier ou il reviendra : Ouvrir dans l'explorateur, Aller a, Copier le chemin) ; Ouvrir dans
//  l'explorateur montre l'element dans la corbeille.
// =============================================================================
#pragma once

#include "../Corbeille.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ui {
class StatusBar;
class SearchField;
}

namespace app {

class HmiToolStrip;

class HmiCorbeillePane final : public ui::Widget {
public:
    enum Action : int { ARestore = 1, APurge, AEmpty, AReveal, ARefresh };

    explicit HmiCorbeillePane(std::string id);
    ~HmiCorbeillePane() override;

    // Relit les corbeilles (le volet le fait seul quand la corbeille change : corbeille::revision).
    void refresh();
    void setSearch(const std::string& text);
    void setKindOpen(corbeille::Kind kind, bool open);
    [[nodiscard]] bool kindOpen(corbeille::Kind kind) const;

    // Ce que montre la liste (la recherche appliquee) : les dossiers et leur nombre ; les elements dans l'ordre.
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> shownGroups() const;
    [[nodiscard]] std::vector<std::string> shownNames() const;
    [[nodiscard]] const std::vector<corbeille::Entry>& all() const noexcept { return entries_; }

    // Les elements choisis (les dossiers ne comptent pas) ; les choisir par leur nom (les essais, les scripts).
    [[nodiscard]] std::vector<corbeille::Entry> chosen() const;
    bool choose(const std::vector<std::string>& names);

    // Les gestes. Restaurer : sans question (un seul Ctrl+Z les remet) ; Supprimer definitivement et Vider : une
    // question (several::confirm), sans retour. Rendent combien.
    std::size_t restoreChosen();
    void        askPurgeChosen();
    std::size_t purgeChosen();
    void        askEmpty();
    std::size_t emptyAll();

    [[nodiscard]] std::vector<ui::PopupMenu::Item> contextItems() const;
    void runAction(int action);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     table() noexcept { return *table_; }
    [[nodiscard]] ui::StatusBar&     status() noexcept { return *status_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    // "12 elements, 3,4 Mo" (ce que dit la ligne d'etat, sans le dernier geste).
    [[nodiscard]] std::string summary() const;
    // La ligne de la purge (sous la liste).
    [[nodiscard]] static std::string purgeNote();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    struct Line {
        bool            header{false};
        corbeille::Kind kind{corbeille::Kind::Recording};
        std::size_t     count{0};
        int             entry{-1};   // rang dans entries_
    };
    void rebuild();
    void rebuild(const std::vector<std::string>& keep);   // ces elements (leur cle) restent choisis
    void say(std::string text, bool warning = false);

    HmiToolStrip*                    tools_{nullptr};
    ui::SearchField*                 search_{nullptr};
    ui::TableView*                   table_{nullptr};
    ui::StatusBar*                   status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<corbeille::Entry>    entries_;
    std::vector<Line>                lines_;
    std::set<int>                    closed_;   // les genres replies
    std::string                      query_, message_;
    std::uint64_t                    seen_{0};
    core::ConnectionScope            links_;
};

} // namespace app
