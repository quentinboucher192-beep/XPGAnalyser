// =============================================================================
//  app/hmi/HmiPanels.hpp - les panneaux de l'editeur de vues
// -----------------------------------------------------------------------------
//  HmiToolStrip    une barre d'outils a pictogrammes (etats actif / coche)
//  HmiObjectList   l'explorateur d'objets : l'arbre de la vue, recherche,
//                  filtre par type, oeil et cadenas cliquables sur chaque ligne
//  HmiLayerList    les calques : actif, visible, verrouille, nombre d'objets
//  HmiPalette      la bibliotheque de composants : categories, favoris,
//                  recherche, apercu dessine par le vrai HmiPainter
//
//  Ecrits pour l'editeur plutot que pris dans ui/ : ils affichent des objets
//  IHM (pictogrammes, bascules par ligne) que les vues generiques de ui/ ne
//  savent pas porter, et ui/ ne doit pas connaitre le modele.
// =============================================================================
#pragma once

#include "HmiIcons.hpp"
#include "HmiObjectAlarmTree.hpp"   // 1.10 (chantier O) : le noeud Alarmes d'un objet
#include "HmiPainter.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiAssets.hpp"     // 1.14.1 (lot N6) : la bibliotheque d'images (l'onglet Images)
#include "../../hmi/HmiDesign.hpp"
#include "../../hmi/HmiEditHistory.hpp"   // 1.12.26 : les dates des objets
#include "../../hmi/HmiExprCheck.hpp"   // ---- Lot API 8 : les expressions impossibles ----
#include "../../hmi/HmiModel.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <optional>
#include <map>      // ---- Lot API 8 : les expressions impossibles ----
#include <memory>   // ---- Lot API 8 : les expressions impossibles ----
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace domain { class Project; }
namespace hmi::apivars { class Model; }   // 1.11.1 (R1111-6) : l'arbre de la bibliotheque

namespace ui { class PopupMenu; }   // 1.12.30 : le menu ">>" de la barre

namespace app {

class HmiApiVarsView;     // 1.11.1 (R1111-6) : HmiApiVarsPane.hpp
struct ApiVarNode;


// --------------------------------------------------------------- panneau ----
// Un titre, des rangees de hauteur fixe (filtres, boutons), et un corps qui
// prend le reste. Tous les panneaux de l'IHM ont cette forme.
class HmiTitledPanel final : public ui::Widget {
public:
    HmiTitledPanel(std::string id, std::string title);
    ui::Widget& addRow(ui::WidgetPtr w, float height);
    ui::Widget& setBody(ui::WidgetPtr w);
    void setTitle(std::string t) { title_ = std::move(t); invalidate(); }
    [[nodiscard]] ui::SizeHint sizeHint() const override;
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    static constexpr float kTitle = 26.f;
    struct Row { ui::Widget* w; float h; };
    std::string      title_;
    std::vector<Row> rows_;
    ui::Widget*      body_{nullptr};
};

// ---------------------------------------------------------------- barre -----
class HmiToolStrip final : public ui::Widget {
public:
    explicit HmiToolStrip(std::string id = {});
    void add(int action, HmiGlyph glyph, std::string tip, std::string label = {});
    void separator();
    void setEnabledWhen(int action, std::function<bool()> predicate);
    void setCheckedWhen(int action, std::function<bool()> predicate);
    // Lot 15 : un bouton montre seulement quand `predicate` le dit (une barre
    // qui suit l'onglet choisi) ; un separateur sans bouton de chaque cote se cache.
    void setVisibleWhen(int action, std::function<bool()> predicate);
    // Lot 8 : changer l'infobulle et le libelle d'un bouton (Nouvelle vue / Nouvelle popup).
    void setText(int action, std::string tip, std::string label);
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    const core::SignalPtr<int> triggered = core::Signal<int>::create();
    // Pour les tests : le rectangle ecran d'un bouton.
    [[nodiscard]] gfx::Rect rectOf(int action) const;
    // Pour les scripts : le bouton dont l'infobulle commence par `tip`
    // (sans distinction de casse), ou -1.
    [[nodiscard]] int actionByTip(std::string_view tip) const;
    // 1.11.17 : pour les essais et les scripts - le bouton est-il actif (setEnabledWhen) ?
    // Faux : grise, ou pas de bouton `action`.
    [[nodiscard]] bool isEnabled(int action) const;
    // 1.12.27 : LES TOUCHES D'UNE LISTE A BARRE. Quand `table` a la main (hors edition d'une case) :
    // Suppr retire, Ctrl+D duplique, Ctrl+Maj+D le bouton « Dupliquer... » s'il y en a un a part, Inser
    // ajoute, Alt+Haut / Alt+Bas montent et descendent - exactement comme le clic sur le bouton de la
    // barre, et seulement s'il est actif. Les touches que la liste prenait deja passent d'abord. Un clic
    // sur la barre rend aussi la main a la liste (avant l'action : une fenetre ouverte par le bouton
    // garde la sienne). Sans `keys`, les boutons se reconnaissent a leur dessin (corbeille, double page,
    // plus, fleches) ; avec, 0 : pas de touche.
    struct ListKeys { int remove{0}, duplicate{0}, add{0}, up{0}, down{0}, duplicateDialog{0}; };
    void bindListKeys(ui::TableView& table);
    void bindListKeys(ui::TableView& table, ListKeys keys);
    // 1.12.30 : TROP ETROITE, MEME SANS LES LIBELLES, les derniers boutons passent dans le menu
    // du bouton ">>" au bout de la barre (leur infobulle en libelle, grises et coches comme sur la
    // barre). Pour les scripts et les essais : un bouton y est-il, son cadre, ouvrir ce menu ;
    // trigger : comme un clic sur le bouton (faux : grise, ou pas de tel bouton).
    [[nodiscard]] bool inOverflow(int action) const;
    [[nodiscard]] gfx::Rect moreRect() const;
    void openMore();
    [[nodiscard]] ui::PopupMenu* moreMenu() const noexcept { return more_; }
    bool trigger(int action);
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    struct Item {
        int action{0};
        HmiGlyph glyph{HmiGlyph::None};
        std::string tip, label;
        bool separator{false};
        std::function<bool()> enabled, checked, visible;
        mutable gfx::Rect rect{};
        mutable bool compact{false};      // lot macros 1 : le libelle tombe (barre trop etroite)
    };
    void layoutItems(float measureScale) const;
    [[nodiscard]] int itemAt(gfx::Point) const;
    std::vector<Item> items_;
    int hover_{-1}, pressed_{-1};
    ui::TableView* focusTarget_{nullptr};   // 1.12.27 : la liste qui reprend la main a chaque clic
    // 1.12.30 : les boutons qui ne tiennent pas (leurs rangs), le bouton du menu, le menu.
    mutable std::vector<std::size_t> overflow_;
    mutable gfx::Rect moreRect_{};
    bool moreHot_{false}, morePressed_{false};
    ui::PopupMenu* more_{nullptr};
};

// ------------------------------------------------------------ explorateur ---
class HmiObjectList final : public ui::Widget {
public:
    explicit HmiObjectList(std::string id = {});
    void setView(const hmi::View* view);           // a rappeler apres chaque modification
    void setSelection(const std::vector<hmi::Id>& ids);
    [[nodiscard]] const std::vector<hmi::Id>& selection() const noexcept { return selection_; }
    void setFilter(std::string text, std::optional<hmi::Kind> kind);
    void revealSelection();                         // deplie et fait defiler jusqu'a la selection
    // ---- Lot API 8 : les expressions impossibles ----
    // Ce que le badge "fx" consulte : le projet IHM et l'automate (leurs noms).
    // A donner avant setView : l'etat de chaque objet (pilote, en erreur) est
    // calcule a chaque setView, pas a chaque image.
    void setExpressionContext(const hmi::Project* project, const domain::Project* plc);
    void reveal(hmi::Id id);                        // ... jusqu'a cet objet, sans le choisir
    [[nodiscard]] std::size_t visibleRowCount() const noexcept { return rows_.size(); }
    // Pour les scripts et les tests : ou est la ligne d'un objet (part 0 : son
    // nom, 1 : l'oeil, 2 : le cadenas). false si elle n'est pas montree.
    [[nodiscard]] bool rowRect(hmi::Id, gfx::Rect& out, int part = 0) const;
    // 1.11.4 : la place des lignes (sans la barre de defilement, qu'on tire, a droite).
    [[nodiscard]] gfx::Rect listArea() const noexcept;
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    // Renommer sur place : un champ de saisie pose sur la ligne. Entree
    // valide (signal `renamed`), Echap annule.
    void beginRename(hmi::Id id);
    const core::SignalPtr<hmi::Id, const std::string&> renamed =
        core::Signal<hmi::Id, const std::string&>::create();

    const core::SignalPtr<const std::vector<hmi::Id>&> selectionChanged =
        core::Signal<const std::vector<hmi::Id>&>::create();
    const core::SignalPtr<hmi::Id> toggleHidden = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id> toggleLocked = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id> activated    = core::Signal<hmi::Id>::create();   // double-clic : renommer
    // 1.12.18 (bug 2) : le clic droit sur un objet (gardee si deja choisi, sinon il devient la
    // selection), ou Maj+F10 / Menu : l'editeur ouvre le menu des objets a `at`. Avant, le clic
    // droit faisait un clic gauche : il defaisait la selection, basculait l'oeil ou le cadenas.
    const core::SignalPtr<hmi::Id, gfx::Point> contextRequested = core::Signal<hmi::Id, gfx::Point>::create();
    // Une touche, quand l'explorateur a le focus : Suppr, F2, Ctrl+Z... ce que
    // l'editeur fait de la selection (la liste, elle, ne modifie rien).
    const core::SignalPtr<const ui::KeyDown&> keyPressed = core::Signal<const ui::KeyDown&>::create();
    // ---- 1.10 (chantier O) : DEPLIER UN OBJET MONTRE SES ALARMES ----
    //  Un objet qui porte des alarmes (le projet : setExpressionContext) a une
    //  cloche et leur nombre ; il se deplie (replie au depart) sur un noeud
    //  "Alarmes . <groupe interne> (n)" (replie au depart) qui se deplie sur
    //  chacune : la pastille de sa priorite, decochee, surchargee ; une instance
    //  de symbole : aussi les groupes de ses objets. Un clic sur une alarme
    //  choisit l'objet et emet alarmActivated(objet, nom de l'alarme dans le
    //  symbole ou la bibliotheque, chemin dans le symbole).
    const core::SignalPtr<hmi::Id, const std::string&, const std::string&> alarmActivated =
        core::Signal<hmi::Id, const std::string&, const std::string&>::create();
    void setAlarmsOpen(hmi::Id object, bool open);             // l'objet et son noeud Alarmes (deplies ou replies)
    [[nodiscard]] bool alarmsOpen(hmi::Id object) const noexcept { return alarmsOpen_.count(object) > 0; }
    [[nodiscard]] const app::alarmtree::Group* alarmNode(hmi::Id object) const;   // nul : pas d'alarme
    // Pour les scripts et les essais : la ligne `line` sous l'objet (0 : le noeud
    // Alarmes ; 1... : ses lignes depliees, dans l'ordre de alarmtree::linesOf) ;
    // faux : pas montree.
    [[nodiscard]] bool alarmRowRect(hmi::Id object, int line, gfx::Rect& out) const;
    // ---- fin 1.10 ----
    // ---- 1.10.3 (Q1103) : L'OBJET DEPLIE PAR FAMILLES, COMME L'ARBRE ----
    //  Un objet deplie montre ses familles (hmitree::objectFamilies : Actions,
    //  Liens fx, Parametres, [Alarmes], Animations, Reperes, [ses elements],
    //  Securite), repliees au depart, avec les memes lignes que l'arbre de
    //  l'application (hmitree::familyLines). Un clic sur une famille ou une ligne
    //  choisit l'objet et emet lineActivated(objet, famille, ligne ; -1 : le
    //  titre) : l'editeur y va (HmiEditor::showLine, comme l'arbre). Pas pendant
    //  une recherche (les objets seuls).
    const core::SignalPtr<hmi::Id, int, int> lineActivated = core::Signal<hmi::Id, int, int>::create();
    void setObjectOpen(hmi::Id object, bool open);                // l'objet deplie (ses familles)
    void setFamilyOpen(hmi::Id object, int family, bool open);    // et une famille (l'objet aussi)
    // Pour les scripts et les essais : la ligne d'une famille de l'objet (line -1 :
    // son titre) ; faux : pas montree. Le texte de chaque ligne montree sous l'objet.
    [[nodiscard]] bool familyRowRect(hmi::Id object, int family, int line, gfx::Rect& out) const;
    [[nodiscard]] std::vector<std::string> familyRowTexts(hmi::Id object) const;
    // ---- 1.12.26 : LES DATES DES OBJETS (l'historique des modifications) ----
    //  A droite du nom : la derniere modification enregistree (« il y a 2 h », « hier 14:32 »),
    //  ou le point orange et « non enregistr\xC3\xA9 » (change depuis le dernier enregistrement) ;
    //  trop etroit : le point seul. L'infobulle de la ligne : la date entiere, le PC, le compte.
    //  `now` : maintenant, fige (essais) ; 0 : l'horloge du PC.
    void setEditMarks(std::map<hmi::Id, hmi::edits::Mark> marks, std::int64_t now = 0);
    [[nodiscard]] const std::map<hmi::Id, hmi::edits::Mark>& editMarks() const noexcept { return marks_; }
    [[nodiscard]] std::string modifiedText(hmi::Id) const;   // ce que montre la ligne
    [[nodiscard]] std::string modifiedTip(hmi::Id) const;    // son infobulle
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return !marks_.empty() || Widget::hasTooltip(); }
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    std::map<hmi::Id, hmi::edits::Mark> marks_;   // 1.12.26
    std::int64_t              marksNow_{0};
    // alarm : -1 la ligne de l'objet ; 0 son noeud Alarmes ; k > 0 la ligne k de
    // ce noeud deplie (1.10).
    // 1.10.3 : family >= 0, une famille de l'objet (hmitree::Family) ; line >= 0, une de ses lignes.
    struct Row { hmi::Id id; int depth; bool group; bool match; int alarm{-1}; int family{-1}; int line{-1};
                 [[nodiscard]] bool object() const noexcept { return alarm < 0 && family < 0; } };
    void rebuild();
    void endRename(bool commit);
    ui::Widget*               editor_{nullptr};
    hmi::Id                   renaming_{hmi::kNoId};
    [[nodiscard]] int rowAt(float y) const;
    const hmi::View*          view_{nullptr};
    std::vector<Row>          rows_;
    std::vector<hmi::Id>      selection_;
    std::set<hmi::Id>         collapsed_;
    std::string               filterText_;
    std::optional<hmi::Kind>  filterKind_;
    float                     scrollY_{0};
    ui::EdgeScrollBar         vbar_;                 // 1.11.4 : la barre de l'explorateur d'objets
    int                       hover_{-1};
    int                       anchor_{-1};
    hmi::Id                   anchorId_{hmi::kNoId};   // 1.12.3 : le dernier objet clique (ici ou dans la vue)
    float                     rowH_{24};
    // ---- Lot API 8 : les expressions impossibles ----
    void refreshExpressionBadges();
    const hmi::Project*       exprProject_{nullptr};
    const domain::Project*    exprPlc_{nullptr};
    std::shared_ptr<const std::set<std::string, std::less<>>> exprPlcNames_;   // les globales, en majuscules
    std::map<hmi::Id, int>    exprBadges_;      // 1 : pilote par une expression, 2 : l'une ne peut pas marcher
    hmi::exprcheck::PlcPaths  exprPaths_{};     // les chemins de l'automate (membres, indices)
    // ---- 1.10 (chantier O) : les alarmes des objets ----
    std::map<hmi::Id, app::alarmtree::Group> alarmNodes_;   // refait a chaque setView
    std::set<hmi::Id>         objectsOpen_;     // les objets sans enfants deplies (sur leurs alarmes)
    std::set<hmi::Id>         alarmsOpen_;      // les noeuds Alarmes deplies
    // ---- 1.10.3 (Q1103) ----
    const hmi::Project*       familyProject() const noexcept { return exprProject_; }
    std::set<std::pair<hmi::Id, int>> familiesOpen_;   // les familles depliees
    [[nodiscard]] std::string familyRowText(const Row&) const;
};

// --------------------------------------------------------------- calques ----
class HmiLayerList final : public ui::Widget {
public:
    explicit HmiLayerList(std::string id = {});
    void setView(const hmi::View* view);
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    const core::SignalPtr<hmi::Id> activate      = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id> toggleVisible = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id> toggleLocked  = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id> rename        = core::Signal<hmi::Id>::create();
    // 1.12.18 (bug 3) : le clic droit ne rend plus le calque actif (ce qui entrait dans
    // l'historique) : l'editeur ouvre le menu de CE calque. Maj+F10 / Menu : celui du calque actif.
    const core::SignalPtr<hmi::Id, gfx::Point> contextRequested = core::Signal<hmi::Id, gfx::Point>::create();
    // 1.12.18 : une touche quand la liste a le focus (F2, Suppr, Ctrl+D, Alt+Haut/Bas) - pour le calque actif.
    const core::SignalPtr<const ui::KeyDown&> keyPressed = core::Signal<const ui::KeyDown&>::create();
    void beginRename(hmi::Id layer);
    const core::SignalPtr<hmi::Id, const std::string&> renamed =
        core::Signal<hmi::Id, const std::string&>::create();
    // Pour les scripts : la ligne d'un calque (part 0 : son nom, 1 : l'oeil,
    // 2 : le cadenas).
    [[nodiscard]] bool rowRect(hmi::Id layer, gfx::Rect& out, int part = 0) const;
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    void endRename();
    ui::Widget*      editor_{nullptr};
    hmi::Id          renaming_{hmi::kNoId};
    const hmi::View* view_{nullptr};
    int              hover_{-1};
    float            rowH_{26};
};

// ------------------------------------------------------------- palette ------
// 1.12.18 : la famille d'un genre dans la bibliotheque (« Affichage », « Synoptique ») ; vide : aucune.
[[nodiscard]] std::string hmiPaletteFamily(hmi::Kind kind);
// 1.12.26 : le libelle d'une propriete d'un objet, celui de l'inspecteur (la fenetre Historique).
[[nodiscard]] std::string hmiPropertyLabel(hmi::Kind kind, std::string_view key);
// 1.14.0 (captures finales) : les mots-cles d'un genre pour la recherche de la bibliotheque (ceux de son sujet d'aide,
// @mots, et les siens), et s'ils trouvent `text` (le debut d'un mot, sans casse ni accents).
[[nodiscard]] std::vector<std::string_view> hmiPaletteWords(hmi::Kind kind);
[[nodiscard]] bool hmiPaletteWordMatch(hmi::Kind kind, std::string_view text);
// 1.14.0 (captures finales) : ce que montre, en gris, le Mot de passe vide de l'Enregistreur (avant : "{variable}").
inline constexpr const char* kNoPasswordInvite = "aucun (en clair)";
class HmiPalette final : public ui::Widget {
public:
    explicit HmiPalette(std::string id = {});
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    void setFilter(std::string text);
    void setCurrent(std::optional<hmi::Kind> kind) { current_ = kind; invalidate(); }
    void setFavorites(std::set<hmi::Kind> favs) { favorites_ = std::move(favs); invalidate(); }
    [[nodiscard]] const std::set<hmi::Kind>& favorites() const noexcept { return favorites_; }
    const core::SignalPtr<hmi::Kind> chosen           = core::Signal<hmi::Kind>::create();
    const core::SignalPtr<>          favoritesChanged = core::Signal<>::create();
    // 1.12.18 (bug 3) : le clic droit sur une tuile n'arme plus la pose et ne bascule plus
    // l'etoile : l'editeur ouvre son menu (Placer sur la vue, Favori, Aide).
    const core::SignalPtr<hmi::Kind, gfx::Point> contextRequested = core::Signal<hmi::Kind, gfx::Point>::create();
    void toggleFavorite(hmi::Kind k);
    void setHoverForTest(std::optional<hmi::Kind> k) { hoverKind_ = k; }
    // La tuile survolee : celle dont l'apercu est dessine en bas.
    [[nodiscard]] std::optional<hmi::Kind> hoveredKind() const noexcept { return hoverKind_; }
    // Pour les scripts : la tuile d'un genre d'objet (apres un premier dessin),
    // et faire defiler jusqu'a elle.
    [[nodiscard]] bool tileRect(hmi::Kind, gfx::Rect& out) const;
    void revealKind(hmi::Kind);
    // Lot 10 : LES SYMBOLES DU PROJET, une section a eux (apres les favoris) :
    // une tuile par symbole, son dessin en miniature. Un clic : "poser une
    // instance" (chosenSymbol). Sans projet : pas de section.
    void setProject(const hmi::Project* project) { project_ = project; invalidate(); }
    void setCurrentSymbol(std::string name) { currentSymbol_ = std::move(name); invalidate(); }
    const core::SignalPtr<const std::string&> chosenSymbol = core::Signal<const std::string&>::create();
    [[nodiscard]] bool symbolTileRect(const std::string& name, gfx::Rect& out) const;
    void revealSymbol(const std::string& name);
    [[nodiscard]] const std::string& hoveredSymbol() const noexcept { return hoverSymbol_; }
    // Lot 12 : L'ONGLET VARIABLES. Les variables de l'automate (et de l'IHM),
    // leur type ; une variable glissee dans la vue (ou choisie, puis un clic
    // dans la vue) y pose l'objet qui lui va (hmi/HmiDesign.hpp) : un voyant,
    // un afficheur, un texte, le bouton d'une popup d'equipement.
    // 1.14.1 (lot N6) : Images, l'onglet de la bibliotheque d'images (en dernier : les rangs restent ; il se montre
    // entre Objets et Variables).
    enum class Mode : std::uint8_t { Objects, Variables, Images };
    void setMode(Mode m);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    void setVariables(std::vector<hmi::design::VarInfo> vars) { variables_ = std::move(vars); invalidate(); }
    [[nodiscard]] const std::vector<hmi::design::VarInfo>& variables() const noexcept { return variables_; }
    void setCurrentVariable(std::string name) { currentVariable_ = std::move(name); invalidate(); }
    const core::SignalPtr<const hmi::design::VarInfo&> chosenVariable = core::Signal<const hmi::design::VarInfo&>::create();
    const core::SignalPtr<> variablesWanted = core::Signal<>::create();   // l'onglet s'ouvre : les lire
    [[nodiscard]] bool modeRect(Mode, gfx::Rect& out) const;
    [[nodiscard]] bool variableRect(const std::string& name, gfx::Rect& out) const;
    void revealVariable(const std::string& name);
    // 1.11.1 (R1111-6, decision 119) : l'onglet Variables montre l'ARBRE des
    // variables de l'automate, comme IHM > Configuration (les Globales, les
    // unites et leurs portees, Accessible, les cadenas), puis « Variables IHM » ;
    // une variable choisie (un clic) part avec son nom API.… et se pose comme
    // avant (un clic dans la vue, ou glissee). Sans modele (pas de programme
    // d'automate) : la liste d'avant.
    void setApiModel(std::shared_ptr<const hmi::apivars::Model> model);
    [[nodiscard]] HmiApiVarsView* apiTree() noexcept { return apiTree_; }
    [[nodiscard]] bool treeShown() const noexcept { return apiTree_ && apiModel_ && mode_ == Mode::Variables; }

    // ---- 1.14.1 (lot N6) : L'ONGLET IMAGES ---------------------------------------------------------------------
    //  La bibliotheque d'images (hmi::imagelib) : Mes images en tete, puis les categories (Fleches et navigation,
    //  Interface, Securite et signalisation...), chacune une section repliable avec son nombre d'images ; le champ
    //  de recherche de la bibliotheque y cherche le nom, la categorie et les mots-cles (les sections qui trouvent
    //  s'ouvrent). En haut : la couleur des dessins monochromes (Auto : celle qui se lit sur le fond de la vue, ou
    //  une autre), la taille des vignettes (petites, moyennes, grandes), Importer (l'image choisie dans les
    //  ressources, sans la poser) et + (un fichier du disque dans Mes images). Un clic choisit une image : un clic
    //  dans la vue (ou la lacher sur la vue) la pose - une Image et sa ressource ; un double-clic la pose au centre
    //  de la vue ; le clic droit ouvre le menu de l'editeur. Un fichier image lache sur l'onglet rejoint Mes images.
    //  Les reglages de l'onglet (couleur, taille, sections ouvertes) sont communs aux editeurs ouverts.
    void setImageBackdrop(std::string background);              // le fond de la vue ouverte, "#20242B"
    [[nodiscard]] const std::string& imageBackdrop() const noexcept { return backdrop_; }
    void setImageSwatch(int index);                             // 0 : Auto ; k : hmi::imagelib::swatches()[k - 1]
    [[nodiscard]] int imageSwatch() const noexcept;
    [[nodiscard]] std::uint32_t imageInk() const;               // la couleur de l'encre, Auto resolu
    void setImageSize(int size);                                // 0 petites, 1 moyennes, 2 grandes (et leur nom)
    [[nodiscard]] int imageSize() const noexcept;
    void setImageCategoryOpen(const std::string& category, bool open);
    [[nodiscard]] bool imageCategoryOpen(const std::string& category) const;
    void reloadMyImages();                                      // relire le dossier de Mes images
    [[nodiscard]] const std::vector<hmi::imagelib::Image>& myImages() const noexcept { return mine_; }
    // Une image montree par l'onglet (embarquee, ou de Mes images : "mes/" + son fichier) ; nul : inconnue.
    [[nodiscard]] const hmi::imagelib::Image* imageById(std::string_view id) const;
    // Ce que montre l'onglet (la recherche appliquee) : ses sections, dans l'ordre, et leur nombre d'images.
    struct ImageSection { std::string category; std::size_t count{0}; bool open{false}; };
    [[nodiscard]] std::vector<ImageSection> imageSections() const;
    // Pour les scripts et les essais (apres un premier dessin) : la vignette d'une image, l'en-tete d'une
    // section, les boutons ; faire defiler jusqu'a une image (sa section s'ouvre).
    [[nodiscard]] bool imageTileRect(std::string_view id, gfx::Rect& out) const;
    [[nodiscard]] bool imageHeadingRect(std::string_view category, gfx::Rect& out) const;
    [[nodiscard]] bool imageSwatchRect(int index, gfx::Rect& out) const;
    [[nodiscard]] bool imageSizeRect(int size, gfx::Rect& out) const;
    [[nodiscard]] bool imageImportRect(gfx::Rect& out) const;
    [[nodiscard]] bool imageAddRect(gfx::Rect& out) const;
    void revealImage(std::string_view id);
    [[nodiscard]] std::string imageTooltip(std::string_view id) const;   // l'infobulle d'une vignette
    [[nodiscard]] const std::string& currentImage() const noexcept { return currentImage_; }
    void setCurrentImage(std::string id) { currentImage_ = std::move(id); invalidate(); }
    [[nodiscard]] const std::string& hoveredImage() const noexcept { return hoverImage_; }
    // Un fichier du disque dans Mes images (le bouton +, un fichier lache sur l'onglet) ; faux et `why` sinon.
    bool addImageFile(const std::string& path, std::string* why = nullptr);
    const core::SignalPtr<const hmi::imagelib::Image&> chosenImage = core::Signal<const hmi::imagelib::Image&>::create();
    const core::SignalPtr<const hmi::imagelib::Image&> imageActivated = core::Signal<const hmi::imagelib::Image&>::create();   // double-clic
    const core::SignalPtr<const hmi::imagelib::Image&> importImageRequested = core::Signal<const hmi::imagelib::Image&>::create();
    const core::SignalPtr<const hmi::imagelib::Image&, gfx::Point> imageContextRequested =
        core::Signal<const hmi::imagelib::Image&, gfx::Point>::create();
    const core::SignalPtr<>                   addImageFileRequested = core::Signal<>::create();   // le bouton +
    const core::SignalPtr<const std::string&> imageMessage = core::Signal<const std::string&>::create();   // Mes images : ce qui a ete fait
protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    HmiApiVarsView*                            apiTree_{nullptr};     // R1111-6
    std::shared_ptr<const hmi::apivars::Model> apiModel_;
    std::optional<hmi::design::VarInfo>        treeChoice_;           // la variable choisie dans l'arbre (l'aide en bas)
    core::ConnectionScope                      treeLinks_;
    [[nodiscard]] hmi::design::VarInfo infoOf(const ApiVarNode& n) const;
    static constexpr float kPreviewH = 150.f;   // l'apercu, en bas, au survol d'une tuile
    static constexpr float kModeH = 30.f;       // lot 12 : les onglets Objets | Variables
    Mode                                  mode_{Mode::Objects};
    std::vector<hmi::design::VarInfo>     variables_;
    std::string                           currentVariable_, hoverVariable_;
    struct VarRow { std::size_t index; gfx::Rect rect; };
    mutable std::vector<VarRow>           varRows_;
    float                                 varScrollY_{0};
    void layoutVariables() const;
    void paintVariables(const ui::PaintContext&);
    struct Tile { hmi::Kind kind; gfx::Rect rect; gfx::Rect star; std::string symbol; };   // symbol : lot 10
    void layoutTiles(const ui::Theme*) const;
    std::string               filter_;
    std::set<hmi::Kind>       wordHits_;              // 1.14.0 (captures finales) : trouves par leurs mots-cles
    const hmi::Project*       project_{nullptr};
    std::string               currentSymbol_, hoverSymbol_;
    mutable float             emptySymbolsY_{-1.f};   // la ligne "aucun symbole", sous son titre
    std::optional<hmi::Kind>  current_, hoverKind_;
    std::set<hmi::Kind>       favorites_{hmi::Kind::Button, hmi::Kind::Indicator, hmi::Kind::Text};
    mutable std::vector<Tile> tiles_;
    mutable std::vector<std::pair<std::string, float>> headings_;
    float                     scrollY_{0};
    // 1.14.1 (lot N6) : l'onglet Images.
    static constexpr float kImageBarH = 52.f;   // la couleur, puis la taille, Importer et +
    static constexpr float kImageHelpH = 40.f;  // en bas : l'image survolee (ou choisie), ou le mode d'emploi
    struct ImageTile { const hmi::imagelib::Image* image; gfx::Rect rect; };
    struct ImageHead { std::string category; std::size_t count; bool open; gfx::Rect rect; };
    std::vector<hmi::imagelib::Image> mine_;
    bool                              mineLoaded_{false};
    std::string                       backdrop_{"#20242B"};
    std::string                       currentImage_, hoverImage_;
    float                             imageScrollY_{0};
    mutable std::vector<ImageTile>    imageTiles_;
    mutable std::vector<ImageHead>    imageHeads_;
    mutable float                     mineInviteY_{-1.f};   // "Mes images" vide : une ligne d'aide
    mutable std::map<std::pair<std::string, std::uint32_t>, hmi::Resource> tileRes_;   // le contenu d'une vignette
    void layoutImages() const;
    void paintImages(const ui::PaintContext&);
    ui::EventResult imageEvent(const ui::InputEvent&);
    [[nodiscard]] float imageCell() const noexcept;
    [[nodiscard]] const hmi::Resource& tileResource(const hmi::imagelib::Image&, std::uint32_t ink) const;
    [[nodiscard]] std::vector<std::pair<std::string, std::vector<const hmi::imagelib::Image*>>> imageGroups() const;
    [[nodiscard]] std::uint32_t inkFor(const hmi::imagelib::Image&) const;
};

// ----------------------------------------------------------- proprietes -----
//  Le panneau des proprietes (1.10.4) : les sections communes (Objet, Position et taille, Apparence,
//  Securite), puis celle du genre (voir hmiPropertyCategories) - ou, sans selection, celles de la vue.
//  `commit` recoit (cle, valeur, est_une_expression) et fait la commande.
struct HmiPropertyCommits {
    std::function<bool(const std::string& key, const std::string& value, bool expr)> prop;
    std::function<bool(const std::string& field, const std::string& value)>         meta;   // nom, calque, verrou...
    std::function<bool(const std::string& field, const std::string& value)>         view;   // proprietes de la vue
};
// `assets` : les ressources et fichiers externes proposes dans les listes
// (Image, Video, Police, source d'un Tableau).
// `project` (lot 4) : les groupes d'utilisateurs (profil requis) et les groupes
// d'alarmes (filtre d'un Historique).
[[nodiscard]] std::vector<ui::PropertyGrid::Category>
hmiPropertyCategories(const hmi::View&, const std::vector<hmi::Id>& selection, const domain::Project* plc,
                      const HmiPropertyCommits&, const hmi::Assets* assets = nullptr, const hmi::Project* project = nullptr);
// ---- Lot API 8 : les expressions impossibles ----
// Le controle immediat d'une expression de propriete : "" si elle peut marcher,
// sinon le premier probleme (la pastille fx rouge, l'infobulle).
[[nodiscard]] std::string hmiExpressionError(const hmi::View&, std::string_view key, const std::string& expr,
                                             const domain::Project* plc, const hmi::Project* project,
                                             const std::set<std::string, std::less<>>* plcUpperNames = nullptr,
                                             const hmi::exprcheck::PlcPaths* plcPaths = nullptr,
                                             const hmi::Object* object = nullptr);   // 1.12.6 : son genre (le role de la cle)
// Les chemins de l'automate (le type d'une globale, d'un membre ; les DDT) pour
// hmiExpressionError, Compiler et Generer ; vides sans automate.
[[nodiscard]] hmi::exprcheck::PlcPaths hmiPlcPaths(const domain::Project* plc);
// Les globales de l'automate, en majuscules (ce que hmiExpressionError consulte vite).
[[nodiscard]] std::shared_ptr<const std::set<std::string, std::less<>>> hmiPlcUpperNames(const domain::Project* plc);

// 1.11.3 : les globales de l'automate donnees au moteur IHM (hmi::setPlcNames) - un
// argument de symbole qui n'en est pas une (ni une variable IHM) est une constante.
void hmiPublishPlcNames(const domain::Project* plc);
// L'adresse automate d'une variable du projet ("%MW100"), ou "".
[[nodiscard]] std::string plcAddressOf(const domain::Project* plc, const std::string& variable);
// Lot 9 : le libelle d'une propriete dans l'inspecteur ("Couleur de fond") et
// son aide ; faux : une cle que l'inspecteur ne decrit pas.
bool hmiPropertyInfo(std::string_view key, std::string& label, std::string& help);

} // namespace app
