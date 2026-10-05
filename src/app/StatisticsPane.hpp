// =============================================================================
//  app/StatisticsPane.hpp - lot API 7 : l'onglet API > Statistiques
// -----------------------------------------------------------------------------
//  L'ancien ecran plein « Statistiques » (Affichage, Ctrl+5) devient un onglet
//  de l'API, fait comme les autres (ApiFrame : la barre, la ligne d'aide), et
//  refait de fond en comble : les chiffres sont ceux du projet OUVERT
//  (project::stats::compute), recalcules apres chaque modification, en
//  francais, et chaque chiffre mene a ce qu'il compte.
//
//   * quatre cartes : les lignes de code, les variables, la memoire declaree,
//     la part commentee ;
//   * LE PROGRAMME, EN LIGNES : des barres par entree de MAST, par section ou
//     par langage (un choix a trois) - un clic ouvre la section ou l'unite ;
//   * LES VARIABLES : globales, locales, parametres par genre, et six
//     compteurs (pas utilisees, lues par l'IHM, ecrites jamais lues, jamais
//     ecrites, adresses en double, E/S sans commentaire) - chacun ouvre
//     l'onglet Variables ;
//   * LA MEMOIRE, PAR TYPE : les six plus lourds, et qui les porte ;
//   * A REGARDER : ce que les chiffres suggerent, avec le bouton qui y mene ;
//   * tous les types en bas, dans une table triable (un clic sur un titre).
//
//  Tout est peint ici (cartes, barres, survol, defilement a la molette) sauf
//  la table, un ui::TableView : on la trie, on la copie (Ctrl+C) comme les
//  autres. La molette sur la table fait d'abord defiler l'onglet (la table
//  arrive entiere), puis la table. Chaque zone cliquable a une cle (partRect)
//  pour les scripts.
// =============================================================================
#pragma once

#include "TaskPanes.hpp"               // ApiPaneHosts
#include "../project/ProjectStats.hpp"
#include "../ui/TextSearch.hpp"        // lot API 8 : la recherche

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TableView; }

namespace app {

class ApiFrame;

class StatisticsPane final : public ui::Widget {
public:
    enum Action : int { ARecompute = 1, AExport, AVariables, AOrder, AHelp };
    // Les barres du programme : par entree de MAST, par section, par langage.
    enum class Breakdown : std::uint8_t { Entries, Sections, Languages };

    explicit StatisticsPane(std::string id);
    ~StatisticsPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);   // remplit frame.tools() et frame.setHint(...)
    void refresh();                 // recalcule : apres chaque modification, ou un autre projet
    void runAction(int action);

    // ---- scripts / tests --------------------------------------------------------
    // "entr", "section", "langage" (le debut, sans la casse ; "par " permis).
    bool chooseBreakdown(std::string_view label);
    [[nodiscard]] Breakdown breakdown() const noexcept { return breakdown_; }
    [[nodiscard]] const project::stats::ProjectStats& stats() const noexcept;
    [[nodiscard]] std::string csv() const;
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    // Ou est une zone cliquable (apres un dessin), par sa cle : "entrees",
    // "sections", "langages" (le choix a trois) ; "carte:lignes", "carte:variables",
    // "carte:memoire", "carte:commentees" ; "barre:<libelle>" (une barre du
    // programme, telle qu'ecrite ; "barre:autres" : le cumul, "barre:ST" : un
    // langage) ; "portee:globales", "portee:locales", "portee:parametres" (les
    // barres des variables) ; "compteur:pas-utilisees", "compteur:lues-ihm",
    // "compteur:ecrites-jamais-lues", "compteur:jamais-ecrites",
    // "compteur:adresses-double", "compteur:es-sans-commentaire" ; "type:<nom>"
    // (une barre de la memoire) ; "regarder:<id>" (le bouton d'une ligne de
    // « A regarder » : doublons, unites-longues, lectures-avant,
    // sans-commentaire, pas-utilisees, bibliotheque). Vide : pas montree.
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;
    // Ce qu'un clic sur cette zone demanderait a l'ecran ("" : rien, ou un choix
    // interne) - la meme table que la souris.
    [[nodiscard]] std::string requestOf(std::string_view key) const;
    // Le defilement vertical du contenu (0 : en haut).
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }
    void scrollTo(float y);
    // ---- Lot API 8 : chercher ----
    //  Une barre au-dessus de tout (fixe) : la table des types (le nom, le
    //  genre, les variables qui le portent) et les listes - les barres du
    //  programme (entrees, sections : leur nom, leur unite) et la memoire par
    //  type. Surlignee, "12 sur 40" (les types), retenue d'une seance a l'autre.
    [[nodiscard]] ApiFilterBar& filters() noexcept { return *filters_; }
    [[nodiscard]] std::size_t shownTypes() const;
    // ---- fin Lot API 8 ----

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    void            onPaintOverlay(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class TypeModel;
    class TypeTable;     // la table : la molette passe a l'onglet quand il doit defiler d'abord
    friend class TypeModel;

    // Une zone cliquable, en coordonnees d'ecran, refaite a chaque dessin.
    struct Hit {
        gfx::Rect   rect;
        std::string key;       // pour partRect
        std::string action;    // "seg:0".."seg:2", ou une cle de onApiRequest
        std::string tip;       // l'infobulle au survol
        bool        button{false};
    };
    // Ou va chaque bloc, pour une largeur donnee (coordonnees du contenu, avant
    // le defilement).
    struct Plan {
        float       pad{16.f}, gap{12.f};
        int         kpiColumns{4};
        bool        twoColumns{true};
        gfx::Rect   kpi[4]{};
        gfx::Rect   program{}, variables{}, memory{}, todo{};
        gfx::Rect   table{};
        float       height{0.f};    // tout le contenu
    };
    [[nodiscard]] Plan plan(float width, float viewHeight) const;
    [[nodiscard]] std::size_t todoRows() const noexcept;
    void setTypeColumns(float tableWidth);
    void updateStatus();            // le texte a droite de la barre
    void act(const std::string& action);
    void openType(std::size_t row);
    [[nodiscard]] int hitAt(gfx::Point p) const;
    [[nodiscard]] gfx::Rect thumbRect() const;
    [[nodiscard]] float maxScroll() const;

    void paintCards(const ui::PaintContext&, const Plan&, float dy);
    void paintProgram(const ui::PaintContext&, const gfx::Rect&);
    void paintVariables(const ui::PaintContext&, const gfx::Rect&);
    void paintMemory(const ui::PaintContext&, const gfx::Rect&);
    void paintTodo(const ui::PaintContext&, const gfx::Rect&);

    // ---- Lot API 8 : chercher ----
    [[nodiscard]] gfx::Rect area() const;     // sous la barre de recherche : ce qui defile
    void applySearch();
    [[nodiscard]] bool typeKept(const project::stats::TypeRow& t) const;
    ApiFilterBar*                 filters_{nullptr};
    ui::SearchQuery               query_;
    // ---- fin Lot API 8 ----
    ApiPaneHosts                  hosts_;
    ApiFrame*                     frame_{nullptr};
    ui::TableView*                table_{nullptr};
    std::shared_ptr<TypeModel>    model_;
    project::stats::ProjectStats  stats_;
    Breakdown                     breakdown_{Breakdown::Entries};
    float                         scrollY_{0.f};
    float                         contentH_{0.f};
    float                         columnsFor_{-1.f};  // la largeur de table pour laquelle les colonnes sont posees
    mutable std::vector<Hit>      hits_;
    int                           hover_{-1};
    int                           pressed_{-1};
    bool                          dragThumb_{false};
    float                         dragGrab_{0.f};     // ou le pouce a ete saisi
    core::ConnectionScope         links_;
};

} // namespace app
