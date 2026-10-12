// =============================================================================
//  app/hmi/HmiContentPanel.hpp - l'onglet Contenu de l'inspecteur (lot 6)
// -----------------------------------------------------------------------------
//  CE QU'UN OBJET CONTIENT, A COTE DE SES PROPRIETES. Une liste en haut, les
//  reglages de l'element choisi en dessous, comme l'onglet Actions :
//
//    Tableau           les LIGNES ; chaque case : un texte, un texte a trous
//                      ({Niveau:0.0} bar) ou =expression, avec l'aide a la
//                      saisie ; les colonnes : en-tete et largeur relative.
//                      + ligne, + colonne, retirer, monter, descendre.
//    Courbe            les PLUMES : nom (legende), expression, couleur.
//    Image animee      les ETATS : condition, images qui defilent, periode.
//    Gestion de        la recette montree, les boutons presents, et les jeux
//    recettes          (en lecture : ils se modifient dans Recettes, ou en
//                      marche avec les boutons de l'objet).
//    Plan a zones      (lot 12) les ZONES : nom, points (en % du plan), groupe
//                      d'alarmes, vue ouverte d'un clic, couleur.
//    Conteneur a       (lot 12) les PAGES : leur libelle ; retirer ou deplacer
//    onglets           une page emmene ses objets.
//    Barre de          (lot 12) les VUES : la vue, son libelle.
//    navigation
//    Groupe, conteneur (1.10.4) les OBJETS qu'il tient, en lecture.
//    cadre, panneaux
//    Voyant, texte     (1.12.3) les ETATS : une ligne par etat (quand, texte, couleur,
//    multi-etats       clignote), le « sinon » ; choisis SELON LA VALEUR (0, 1, 10..20,
//                      'Auto') ou SELON DES CONDITIONS (Defaut, Marche... : la premiere
//                      vraie gagne, la Valeur est ecrite pour elles).
//    Selecteur, liste  (1.12.7, lot D) les CHOIX : une ligne par choix (libelle, valeur
//    deroulante, boutons ecrite ; vide : le rang), au lieu de positions / items et values
//    radio, liste      tapes en parallele.
//    Chronogramme      (1.12.7) ses LIGNES, ou ses ETATS (le tableau du multi-etats, au
//                      lieu de la ligne stateList) : « Montrer » choisit.
//    Bargraphe, cadran (1.12.7) les ZONES de couleur : de, a, couleur ; une borne en % de
//                      l'echelle (60 %) suit min et max.
//    Programmateur     (1.12.7) les PLAGES : jours, de, a ; l'arrondi a la resolution montre.
//    Selecteur de      (1.12.7) des CASES A COCHER : les langues du projet, les boutons de la
//    langue, gestion   gestion des utilisateurs, les zones (groupes d'alarmes, ceux des objets
//    des utilisateurs, compris) ; ce qui a ete tape et n'est pas propose (un repere $Zone$)
//    resume par zone   reste, coche.
//    Ligne, polygone   (1.12.12, lot H) les SOMMETS : X et Y, une ligne par sommet (un
//                      repere garde case par case) ; le cadre suit les points. Le dessin
//                      et Contenu choisissent le meme sommet.
//    Courbe XY         (1.12.12, lot H) ses courbes, ou (Montrer) les points de sa courbe
//                      de reference, ranges par x croissant.
//    Pompe, moteur,    (1.12.11, lot G) le MODE ETAT : une ligne par valeur (quand la valeur
//    vanne...          vaut, l'aspect - arret, marche, defaut, maintenance -, sa couleur, anime) ;
//                      « Depuis une enumeration » le remplit (ETAT_MOTEUR, une du projet).
//    Tableau en        (1.12.13) les COLONNES DYNAMIQUES : une ligne par colonne (titre, contenu -
//    lignes            [*].Niveau, [#], Debits[*], une formule, un texte a trous -, le type recu et
//    dynamiques        sa faute corrigee d'un clic, format, unite, largeur, alignement, R/W, niveau,
//                      couleur) ; « Une colonne par membre ». Un tableau d'avant (Lignes depuis,
//                      Colonnes de la source) s'y montre repris en colonnes, et ne change qu'a la
//                      premiere retouche. En cases fixes : l'acces R/W de chaque colonne.
//  Un objet qui ne peut rien contenir (un bouton, une cuve) : pas d'onglet.
//
//  Tout passe par des commandes de vue : Ctrl+Z reprend une ligne ajoutee, une
//  case modifiee, une plume retiree.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiContentPanel final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    // Lot 12 : les zones d'un plan, les pages d'un conteneur a onglets, les vues
    // d'une barre de navigation.
    // 1.10.4 (K3) : Members - les objets d'un groupe, d'un conteneur, d'un cadre,
    // d'un panneau (en lecture : on les regle en les choisissant). None : l'objet ne
    // peut pas avoir de contenu, l'editeur cache l'onglet.
    // 1.12.3 : States - les etats d'un voyant multi-etats, d'un texte multi-etats.
    // 1.12.7 (lot D) : Choices - les choix d'un selecteur, d'une liste deroulante, de boutons radio,
    // d'une liste ; Bands - les zones de couleur d'un bargraphe, d'un cadran ; Schedule - les plages
    // d'un programmateur ; Checks - les cases a cocher (langues, boutons, zones d'un resume).
    // SourceColumns - un tableau dont les lignes viennent d'une source (Lignes depuis) : ses colonnes.
    // 1.12.12 (lot H) : Points - les sommets d'une ligne, d'un polygone ; les points de la courbe de
    // reference d'une courbe XY (Montrer : la reference).
    // 1.12.11 (lot G) : Aspects - le mode Etat d'un symbole du synoptique (valeur -> aspect).
    // 1.12.13 : DynColumns - les colonnes dynamiques d'un tableau en lignes dynamiques (SourceColumns ne sert plus au
    // Tableau : un tableau d'avant s'y montre repris en colonnes).
    enum class Mode : std::uint8_t { None, Table, Trend, AnimatedImage, RecipeManager, Zones, Tabs, NavItems, Members, States,
                                     Choices, Bands, Schedule, Checks, SourceColumns, Points, Aspects, DynColumns };

    HmiContentPanel(std::string id, hmi::DocumentPtr doc, hmi::Id view, Apply apply);

    // L'objet montre ; kNoId (ou plusieurs objets choisis) : rien.
    void setObject(hmi::Id object);
    [[nodiscard]] hmi::Id object() const noexcept { return object_; }
    [[nodiscard]] Mode    mode() const;
    void refresh();
    [[nodiscard]] int  selectedIndex() const;
    void selectIndex(int index);
    [[nodiscard]] std::size_t count() const;          // lignes, plumes, etats, jeux

    // Les modifications (boutons de la barre, tests). Faux : rien n'a change.
    bool addItem();
    bool removeItem(int index);
    bool moveItem(int index, int delta);
    bool addColumn();                                  // tableau
    bool removeColumn(int column);                     // tableau ; -1 : la derniere
    // Un reglage de l'element `index` : "cell:2" (tableau, colonne 2 a partir
    // de 0), "header:1", "width:1", "name", "expression", "color",
    // "condition", "images", "addImage", "period" ; gestionnaire : "recipe",
    // "button:Ajouter" (TRUE/FALSE).
    // 1.12.7 (lot D) : choix "label", "value" ; tableau de variables "unit", "format" ;
    // chronogramme "show" (lignes / etats, index -1) ; zones "from", "to", "color" ;
    // plages "days", "from", "to" ; cases "check:<element>" (TRUE/FALSE) ; colonnes d'une source
    // "shown" (TRUE/FALSE), "title", "format".
    // 1.12.7 (lot E) : multi-etats, chronogramme, image animee "fromEnum" (index -1, le nom
    // d'une enumeration du projet) : un etat par valeur.
    // 1.12.12 (lot H) : sommets "x", "y" (un nombre, ou un texte a repere $X$ ; la reference d'une
    // courbe XY : un nombre) ; courbe XY "show" (les courbes / la reference, index -1).
    // 1.12.11 (lot G) : mode Etat d'un symbole "match", "look" (Arret, Marche, Defaut, Maintenance),
    // "color", "animated" ; index -1 : "fromEnum" (ETAT_MOTEUR, une enumeration ou une variable de ce
    // type), "enabled" (TRUE : ETAT_MOTEUR ; FALSE : la Valeur se relit vrai / faux).
    // 1.12.13 : colonnes dynamiques "title", "content", "format", "unit", "width", "align", "access" (R, R/W),
    // "level", "color" ("", seuils, formule), "colorExpr" ; index -1 : "perMember" (une colonne par membre de
    // la source, a la place des colonnes). Cases fixes : "access:2" (R, R/W) pour la colonne 2 (a partir de 0).
    bool setField(int index, const std::string& field, const std::string& value);
    // 1.12.12 (lot H) : « Ajouter » d'un plan a zones trace la zone sur le dessin (l'editeur relaie au
    // canevas : traceRequested) ; sans editeur (les essais), une zone rectangulaire comme avant.
    void setZoneTracer(bool on) noexcept { zoneTracer_ = on; }

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& grid() noexcept { return *grid_; }
    // Le nombre d'elements a change (la pastille de l'onglet).
    const core::SignalPtr<std::size_t> countChanged = core::Signal<std::size_t>::create();
    // 1.12.12 (lot H) : une ligne choisie (le sommet, la zone que le dessin montre) ; « Ajouter » d'un plan.
    const core::SignalPtr<int>         rowChosen = core::Signal<int>::create();
    const core::SignalPtr<>            traceRequested = core::Signal<>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    [[nodiscard]] const hmi::Object* current() const;
    bool change(const std::string& label, const std::function<bool(hmi::Object&)>& fn);
    // Lot 12 : une modification qui touche aussi les enfants (les pages d'onglets).
    bool changeInView(const std::string& label, const std::function<bool(hmi::View&, hmi::Object&)>& fn);
    void rebuildGrid();
    hmi::DocumentPtr doc_;
    hmi::Id          view_;
    hmi::Id          object_{hmi::kNoId};
    Apply            apply_;
    HmiToolStrip*    tools_{nullptr};
    ui::TableView*   table_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    int              selected_{-1};
    int              lastColumn_{-1};      // la colonne de la derniere case editee (retirer la colonne)
    bool             chartStates_{false};  // 1.12.7 : un chronogramme montre ses etats (sinon ses lignes)
    bool             zoneTracer_{false};   // 1.12.12 (lot H) : « Ajouter » d'un plan trace la zone
    bool             xyReference_{false};  // 1.12.12 (lot H) : une courbe XY montre sa reference (sinon ses courbes)
    bool             refreshing_{false};   // 1.12.12 (lot H) : refait par le document - rowChosen se tait
    std::size_t      lastCount_{static_cast<std::size_t>(-1)};
    core::ConnectionScope links_;
};

} // namespace app
