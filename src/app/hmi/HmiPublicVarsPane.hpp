// =============================================================================
//  app/hmi/HmiPublicVarsPane.hpp - IHM > Programmation generale > Variables
//                                   systeme et Variables d'instances (lot 9)
// -----------------------------------------------------------------------------
//  Deux onglets, deux tableaux : ce qui se lit (et s'ecrit) sans rien
//  declarer. Le chemin, a qui il appartient, son type, son acces (R ou R/W),
//  sa valeur - celle de l'IHM en marche quand la simulation tourne, sinon
//  celle de l'editeur - et ce que c'est. Un double-clic (ou Copier) met le
//  chemin dans le presse-papiers : il se colle dans un script, une
//  expression, un texte a trous.
//
//  Lot 7 : RANGEES EN DOSSIERS, comme dans l'arbre. Les variables systeme par
//  domaine (Utilisateur et securite, Date et heure, Vues et popups...), celles
//  d'instances par vue puis par objet ("Variables de la vue", puis chaque
//  objet). Une ligne par dossier : sa fleche, et combien il en montre (sur
//  combien quand la recherche en cache). Replies au depart ; la fleche, un
//  double-clic, Entree ou les fleches gauche / droite les deplient et les
//  replient ; Tout deplier / Tout replier dans la barre. La recherche passe
//  dans TOUS les dossiers et montre deplies ceux qui gardent une ligne.
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace app {

class HmiToolStrip;

class HmiPublicVarsPane final : public ui::Widget {
public:
    enum Tab : int { System = 0, Instances = 1 };
    // Les colonnes des deux tableaux.
    enum Column : std::size_t { Path = 0, Owner, Type, Access, Value, About, ColumnCount };

    HmiPublicVarsPane(std::string id, hmi::DocumentPtr doc);

    struct Hosts {
        // La valeur en marche d'un chemin (la simulation) ; faux : l'IHM ne tourne pas.
        std::function<bool(const std::string& path, std::string& out)> live;
        // L'aide : le sujet du tableau affiche.
        std::function<void(const std::string& topic)> help;
    };
    void setHosts(Hosts h);

    void showTab(int tab);
    [[nodiscard]] int currentTab() const;
    void setSearch(std::string text);
    [[nodiscard]] const std::string& search() const noexcept { return searchText_; }
    // La ligne d'un chemin (depuis l'arbre), choisie et amenee en vue - ses
    // dossiers se deplient ; faux : absente.
    bool selectPath(const std::string& path);
    [[nodiscard]] std::string selectedPath() const;
    void refresh();                    // les lignes (le projet a pu changer) et les valeurs
    // Les VARIABLES qui passent la recherche (les dossiers ne comptent pas, replies ou non).
    [[nodiscard]] std::size_t rowsIn(int tab) const;
    [[nodiscard]] std::string cellAt(int tab, std::size_t row, std::size_t column) const;

    // ---- lot 7 : les dossiers -----------------------------------------------
    //  Un dossier par son chemin : "Date et heure" (un domaine), "Vue_A" (une
    //  vue), "Vue_A/Curseur" (un objet), "Vue_A/Variables de la vue".
    [[nodiscard]] bool folderOpen(int tab, const std::string& folder) const;
    void setFolderOpen(int tab, const std::string& folder, bool open);
    void setAllFoldersOpen(int tab, bool open);
    // Les dossiers montres (ceux qui gardent une variable), et les lignes du
    // tableau (dossiers compris, sans ce que cachent les dossiers replies).
    [[nodiscard]] std::size_t foldersIn(int tab) const;
    [[nodiscard]] std::size_t tableRowsIn(int tab) const;
    [[nodiscard]] bool        liveValues() const noexcept { return live_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    [[nodiscard]] ui::TabControl& tabs() noexcept { return *tabs_; }
    [[nodiscard]] ui::TableView&  table(int tab) noexcept { return *tables_[tab == Instances ? 1 : 0]; }
    [[nodiscard]] HmiToolStrip&   tools() noexcept { return *tools_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    struct Row {
        std::string cells[ColumnCount];
        bool        writable{false}, live{false}, heading{false};
        bool        member{false};       // 1.9 : un membre d'une structure SYS.Slave.<nom>.
        // 1.11.1 (R1111-8) : sous « Groupe d'alarmes » ou sous une alarme, la colonne Chemin
        // montre le nom seul (".Acked") ; le chemin entier reste partout ailleurs.
        bool        tail{false};
        // Lot 7 : ses dossiers, du plus haut au plus bas (un domaine ; une vue
        // puis son objet) : (cle, libelle) - "Vue_A", "Vue_A (vue)".
        std::vector<std::pair<std::string, std::string>> folders;
    };
    // Lot 7 : une ligne du tableau - un dossier, ou une variable (rang dans all_).
    struct Line {
        bool        folder{false};
        std::size_t row{0};              // une variable : son rang dans all_
        std::string path;                // un dossier : son chemin ("Vue_A/Curseur")
        std::string label;               // un dossier : son libelle
        int         depth{0};
        std::size_t shown{0}, total{0};  // un dossier : ses variables montrees / toutes
        bool        open{false};
    };
    void rebuildRows();
    void updateValues(bool both);
    void applyFilter();
    void rebuildLines(int t);            // lot 7 : les lignes du tableau, d'apres shown_ et les dossiers ouverts
    void toggleLine(int t, ui::RowIndex line);
    [[nodiscard]] const Line* lineAt(int t, ui::RowIndex line) const;
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr    doc_;
    Hosts               hosts_;
    HmiToolStrip*       tools_{nullptr};
    ui::InputText*      searchBox_{nullptr};
    ui::TabControl*     tabs_{nullptr};
    ui::TableView*      tables_[2]{};
    ui::StatusBar*      status_{nullptr};
    std::vector<Row>    all_[2];         // toutes les lignes
    std::vector<std::size_t> shown_[2];  // celles qui passent le filtre (rang dans all_)
    std::vector<Line>   lines_[2];       // lot 7 : les lignes du tableau (dossiers et variables)
    std::set<std::string> open_[2];      // lot 7 : les dossiers deplies (chemins en minuscules)
    std::set<std::string> searchClosed_[2];   // ... replies pendant la recherche en cours
    std::shared_ptr<ui::ITableModel> models_[2];
    std::string         searchText_;
    bool                slaveNote_{false};   // 1.9 : la note des structures SYS.Slave.<nom>, sous le tableau
    bool                live_{false};
    double              lastValues_{-1};
    std::string         message_;
    core::ConnectionScope links_;
};

} // namespace app
