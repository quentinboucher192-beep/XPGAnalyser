// =============================================================================
//  app/hmi/HmiCyclicPage.hpp - IHM > Outil Modbus > Lecture cyclique (1.9)
// -----------------------------------------------------------------------------
//  LA LECTURE CYCLIQUE A PLUSIEURS REQUETES (MB-1 a MB-10), l'onglet de
//  l'outil Modbus :
//
//    Requetes      la liste (une ligne par requete : sa case, R1, son nom, sa
//                  cible, sa fonction, son adresse, son nombre, son format, sa
//                  periode - un point : la sienne -, son etat et ses lectures) ;
//                  la ligne "+ Ajouter une requete..." ; Ctrl+V colle une liste
//                  venue d'Excel ;
//    Courbes       les valeurs tracees, en pistes (une echelle chacune, les
//                  valeurs de meme unite ensemble) ou sur une seule echelle ;
//                  Figer arrete l'affichage, pas la lecture ;
//    Valeurs       une ligne par valeur lue (un DINT est une valeur) : Tracer,
//                  sa couleur, Rn, l'adresse Modicon et Schneider, le nom de la
//                  variable qui porte cette adresse, la valeur dans son format,
//                  minimum, maximum, moyenne, changements, "il y a" ; ce qui
//                  vient de changer s'allume. Journal : chaque lecture.
//
//  La grille de droite regle la requete choisie et la lecture (toutes les
//  requetes) ; dessous, la boite d'aide d'une requete en pause et la note
//  violette d'une requete qui vise un esclave simule.
//
//  LA LISTE EST UN JEU DE TRAVAIL : Enregistrer le jeu (Ctrl+S dans l'outil)
//  le garde DANS LE PROJET (une commande : Ctrl+Z la defait) ; le dernier jeu
//  revient a l'ouverture de l'outil. Les gestes sur la liste (ajouter, retirer,
//  regler) s'annulent dans l'outil (Ctrl+Z, la liste ayant le clavier).
//
//  Les menus (+ Requete, Jeu) et les dialogues (Lire des variables, Exporter,
//  Enregistrer sous) se posent au-dessus de tout le volet (HmiCyclicOverlay).
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiModbusCyclic.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace mbtool = hmi::mbtool;

class HmiModbusToolPane;
class EquipmentHost;

// Le violet reserve au simule (les maquettes 1.9) : le texte, le fond sombre.
[[nodiscard]] gfx::Color simViolet();
[[nodiscard]] gfx::Color simVioletBg();

// ------------------------------------------------------------ le graphique ---
//  Une piste par valeur (les valeurs de meme unite ensemble), chacune son
//  echelle ; ou une seule echelle. La legende "nom = valeur" en tete de piste,
//  l'echelle a droite, le temps en bas ("-60 s" ... "maintenant").
class HmiLaneChart final : public ui::Widget {
public:
    struct Series {
        std::string                                 name;
        std::string                                 unit;
        std::string                                 lane;       // la cle de sa piste (vide : la sienne)
        gfx::Color                                  color{};
        std::vector<std::pair<double, double>>      points;     // (heure murale, valeur)
        std::optional<std::pair<double, double>>    band;       // un esclave simule : la zone de mouvement
        bool                                        forced{false};
        bool                                        animated{false};
        bool                                        simulated{false};
        std::string                                 last;       // la derniere valeur, dans son format
    };
    explicit HmiLaneChart(std::string id = {});
    void setData(std::vector<Series> series, double from, double to, bool lanes, std::string empty = {});
    [[nodiscard]] const std::vector<Series>& series() const noexcept { return series_; }
    [[nodiscard]] bool lanes() const noexcept { return lanes_; }
    // Les pistes dessinees (les tests) : leur nombre au dernier setData.
    [[nodiscard]] std::size_t laneCount() const;

protected:
    void onPaint(const ui::PaintContext&) override;

private:
    std::vector<Series> series_;
    double              from_{0}, to_{0};
    bool                lanes_{true};
    std::string         empty_;
};

// ------------------------------------------------------ une bande de titre ---
//  "REQUETES  5 . 4 en marche ..." et, a droite, des liens, des segments
//  ("En pistes | Une seule echelle"), une case, une etiquette (FIGE).
class HmiBlockHead final : public ui::Widget {
public:
    enum class Kind : std::uint8_t { Link, Segment, Check, Tag, Text };
    struct Part {
        int         id{0};
        Kind        kind{Kind::Text};
        std::string text;
        std::string count;          // un segment : son nombre, en gris
        bool        on{false};      // segment choisi, case cochee
        bool        right{true};    // a droite (sinon apres le titre)
        int         group{0};       // les segments d'un meme groupe se touchent
    };
    explicit HmiBlockHead(std::string id = {});
    void setCaption(std::string caption, std::string summary);
    void setParts(std::vector<Part> parts);
    [[nodiscard]] const std::vector<Part>& parts() const noexcept { return parts_; }
    [[nodiscard]] const std::string& summary() const noexcept { return summary_; }
    [[nodiscard]] gfx::Rect partRect(int id) const;
    const core::SignalPtr<int> clicked = core::Signal<int>::create();

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void place(const gfx::IRenderer* r) const;
    std::string            caption_, summary_;
    std::vector<Part>      parts_;
    mutable std::vector<gfx::Rect> rects_;
    int                    hover_{-1};
};

// ------------------------------------------- un tableau aux cases a cocher ---
//  Une TableView dont un clic sur une case (la colonne `boxColumn`) le dit :
//  cocher Active ou Tracer, sans toucher a la selection au clavier. Le modele
//  se met a jour sur place : le defilement et la ligne choisie restent.
class HmiCyclicTable final : public ui::TableView {
public:
    explicit HmiCyclicTable(std::string id = {});
    void setBoxColumn(int column) noexcept { boxColumn_ = column; }
    // Les cases (une ligne par ligne du tableau) et leurs styles.
    void setContent(std::vector<std::string> headers, std::vector<std::vector<std::string>> cells,
                    std::vector<std::vector<ui::CellStyle>> styles, std::vector<std::string> tips = {});
    [[nodiscard]] std::string cell(std::size_t row, std::size_t col) const;
    [[nodiscard]] std::size_t rows() const noexcept;
    // (ligne du modele, colonne) : un clic sur une case a cocher.
    const core::SignalPtr<ui::RowIndex, int> boxClicked = core::Signal<ui::RowIndex, int>::create();
    // Un clic simple sur une ligne (la souris, pas le clavier).
    const core::SignalPtr<ui::RowIndex> rowClicked = core::Signal<ui::RowIndex>::create();

protected:
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class Model;
    std::shared_ptr<Model> model_;
    int                    boxColumn_{-1};
};

// ---------------------------------- menus et dialogues, au-dessus du volet ---
class HmiCyclicOverlay : public ui::Widget {
public:
    explicit HmiCyclicOverlay(std::string id, bool dim);
    // Pose sur le volet entier ; le panneau se centre (ou s'accroche).
    virtual void layoutIn(const gfx::Rect& host);
    void close();
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] gfx::Rect panel() const noexcept { return panel_; }
    const core::SignalPtr<> closedSignal = core::Signal<>::create();

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
    void            onLayout() override;
    virtual void    placePanel(const gfx::Rect& host) = 0;     // panel_
    virtual void    paintPanel(const ui::PaintContext&) = 0;
    virtual ui::EventResult panelEvent(const ui::InputEvent&) { return ui::EventResult::Ignored; }
    // Le cadre d'une fenetre : titre, croix, pied ; rend le corps.
    gfx::Rect paintFrame(const ui::PaintContext&, const std::string& title, int glyph, float footer);
    gfx::Rect panel_{};
    gfx::Rect host_{};
    gfx::Rect closeBox_{};

private:
    bool dim_{false};
    bool closed_{false};
};

// Un menu : des entrees (icone, libelle, description, raccourci, coche).
class HmiCyclicMenu final : public HmiCyclicOverlay {
public:
    struct Item {
        int         id{0};
        std::string label, detail, shortcut;
        int         glyph{0};          // un pictogramme (drawCyclicGlyph)
        bool        check{false};      // coche (le jeu ouvert)
        bool        current{false};    // fond (le jeu ouvert)
        bool        separator{false};
        bool        heading{false};    // un titre de section
        bool        enabled{true};
        std::string why;               // grisee : pourquoi
    };
    HmiCyclicMenu(std::string id, std::vector<Item> items, gfx::Point anchor, float width);
    [[nodiscard]] const std::vector<Item>& items() const noexcept { return items_; }
    [[nodiscard]] gfx::Rect itemRect(int id) const;
    // Choisir une entree (les scripts, les tests) ; faux : absente ou grisee.
    bool choose(int id);
    std::function<void(int)> chosen;

protected:
    void            placePanel(const gfx::Rect& host) override;
    void            paintPanel(const ui::PaintContext&) override;
    ui::EventResult panelEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] float itemHeight(const Item&) const;
    std::vector<Item>  items_;
    gfx::Point         anchor_{};
    float              width_{360};
    int                hover_{-1};
    mutable std::vector<gfx::Rect> rects_;
};

// Un nom a taper (Enregistrer sous..., un nouveau jeu).
class HmiCyclicPrompt final : public HmiCyclicOverlay {
public:
    HmiCyclicPrompt(std::string id, std::string title, std::string text, std::string value, std::string confirm);
    [[nodiscard]] ui::InputText& field() noexcept { return *field_; }
    bool accept();
    std::function<bool(const std::string&, std::string*)> done;      // faux : refuse (why sous le champ)

protected:
    void            placePanel(const gfx::Rect& host) override;
    void            paintPanel(const ui::PaintContext&) override;
    ui::EventResult panelEvent(const ui::InputEvent&) override;

private:
    std::string     title_, text_, error_;
    ui::InputText*  field_{nullptr};
    ui::Button*     ok_{nullptr};
    ui::Button*     cancel_{nullptr};
    core::ConnectionScope links_;
};

// ---- Lire des variables (MB-3) ----
struct CyclicVarItem {
    std::string     name, type, place;      // "DINT_1058", "DINT", "%MW1058 \xC2\xB7 41059" (place, en clair)
    mbtool::ReadItem read;
    std::string     already;                // "R2" : deja lue par cette requete
};
struct CyclicVarGroup {
    std::string                title, type, range;
    std::string                target;      // la cible de ses requetes ("automate", un equipement)
    std::vector<CyclicVarItem> items;
    std::string                disabled;    // non vide : pas lisibles, la raison (le groupe est grise)
    bool                       open{false};
    bool                       bits{false};
};
struct CyclicVarTab {
    std::string                 label;
    std::vector<CyclicVarGroup> groups;
    std::vector<std::string>    targets;    // le choix de la cible (vide : celle de chaque groupe)
    std::string                 empty;      // rien a lire : pourquoi
};

class HmiCyclicVarsDialog final : public HmiCyclicOverlay {
public:
    HmiCyclicVarsDialog(std::string id, std::vector<CyclicVarTab> tabs, int nextId);
    // Le resultat : par cible, les requetes prevues.
    struct Planned {
        std::string                  target;
        mbtool::PlannedRead          read;
        std::vector<mbtool::ReadItem> items;   // toutes les variables de cette cible (read.items y renvoie)
    };
    [[nodiscard]] std::vector<Planned> plan() const;
    [[nodiscard]] std::size_t checkedCount() const;
    // Les gestes (les scripts, les tests) : cocher par nom (ou un groupe par son titre), l'onglet, le regroupement.
    bool check(const std::string& name, bool on = true);
    void checkAll(bool on);
    void showTab(std::size_t index);
    [[nodiscard]] std::size_t tab() const noexcept { return tab_; }
    void setMerge(bool on);
    void setGap(int words);
    [[nodiscard]] bool merge() const noexcept { return merge_; }
    [[nodiscard]] int gap() const noexcept { return gap_; }
    void setSearch(const std::string& text);
    bool setTarget(const std::string& label);
    [[nodiscard]] std::string target() const;
    void openGroup(const std::string& title, bool open);
    [[nodiscard]] const std::vector<CyclicVarTab>& tabs() const noexcept { return tabs_; }
    bool accept();
    std::function<void(const std::vector<Planned>&)> added;

protected:
    void            placePanel(const gfx::Rect& host) override;
    void            paintPanel(const ui::PaintContext&) override;
    ui::EventResult panelEvent(const ui::InputEvent&) override;
    void            onLayout() override;

private:
    struct Line { int group{-1}; int item{-1}; };          // une ligne de la liste (item -1 : le groupe)
    void rebuildLines();
    void refreshPlan();
    [[nodiscard]] bool matches(const CyclicVarItem&) const;
    [[nodiscard]] std::string targetOf(const CyclicVarGroup&) const;
    std::vector<CyclicVarTab>   tabs_;
    std::vector<std::vector<std::vector<bool>>> checked_;  // [onglet][groupe][variable]
    std::size_t                 tab_{0};
    int                         nextId_{1};
    bool                        merge_{true};
    int                         gap_{10};
    std::string                 search_;
    int                         target_{0};
    std::vector<Line>           lines_;
    float                       scroll_{0}, planScroll_{0};
    std::vector<Planned>        planned_;
    std::size_t                 count_{0};
    gfx::Rect                   list_{}, planBox_{}, tabRects_[2]{}, mergeBox_{}, minus_{}, plus_{}, targetBox_{};
    ui::InputText*              search_field_{nullptr};
    ui::Button*                 ok_{nullptr};
    ui::Button*                 cancel_{nullptr};
    core::ConnectionScope       links_;
    int                         hoverLine_{-1};
};

// ---- Exporter la lecture cyclique (MB-8) ----
class HmiCyclicExportDialog final : public HmiCyclicOverlay {
public:
    HmiCyclicExportDialog(std::string id, int commonMs, bool recording, std::string recordNote);
    void setOneFile(bool on);
    void setRaw(bool on);
    void setRecord(bool on);
    [[nodiscard]] bool oneFile() const noexcept { return oneFile_; }
    [[nodiscard]] bool raw() const noexcept { return raw_; }
    [[nodiscard]] bool record() const noexcept { return record_; }
    void setPreview(std::string text);
    [[nodiscard]] const std::string& preview() const noexcept { return preview_; }
    bool accept();
    std::function<void()> changed;                 // l'apercu a refaire
    std::function<void(bool oneFile, bool raw, bool record)> exported;
    std::function<void()> elsewhere;               // le bouton ... (un autre dossier)

protected:
    void            placePanel(const gfx::Rect& host) override;
    void            paintPanel(const ui::PaintContext&) override;
    ui::EventResult panelEvent(const ui::InputEvent&) override;
    void            onLayout() override;

private:
    int          commonMs_{500};
    bool         oneFile_{true}, raw_{false}, record_{false};
    std::string  recordNote_, preview_;
    gfx::Rect    radios_[4]{}, recordBox_{};
    ui::Button*  ok_{nullptr};
    ui::Button*  cancel_{nullptr};
    ui::Button*  other_{nullptr};
    core::ConnectionScope links_;
};

// ------------------------------------------------------- sous la grille ---
//  La boite d'aide rouge (une requete en pause ou en echec : l'erreur en clair,
//  Reprendre, Carte memoire, Detecter les zones) et la note violette (la
//  requete choisie vise un esclave simule).
class HmiCyclicSide final : public ui::Widget {
public:
    explicit HmiCyclicSide(std::string id = {});
    void setHelp(std::string title, std::string text, bool resume, bool map, bool detect);
    void setNote(std::string title, std::string text);
    [[nodiscard]] float heightFor(float width) const;
    [[nodiscard]] bool  empty() const noexcept { return helpTitle_.empty() && noteTitle_.empty(); }
    [[nodiscard]] const std::string& helpTitle() const noexcept { return helpTitle_; }
    [[nodiscard]] const std::string& helpText() const noexcept { return helpText_; }
    [[nodiscard]] const std::string& noteText() const noexcept { return noteText_; }
    [[nodiscard]] ui::Button& resumeButton() noexcept { return *resume_; }
    [[nodiscard]] ui::Button& mapButton() noexcept { return *map_; }
    [[nodiscard]] ui::Button& detectButton() noexcept { return *detect_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    std::string  helpTitle_, helpText_, noteTitle_, noteText_;
    ui::Button*  resume_{nullptr};
    ui::Button*  map_{nullptr};
    ui::Button*  detect_{nullptr};
};

// ------------------------------------------------------------- la page ---
class HmiCyclicPage final : public ui::Widget {
public:
    HmiCyclicPage(std::string id, HmiModbusToolPane& tool);
    ~HmiCyclicPage() override;

    // ---- les requetes (le jeu de travail) ----
    struct Row {
        int                                  id{0};
        hmi::ModbusRead                      read;
        std::optional<mbtool::CyclicRequest> request;      // vide : pas lisible (why)
        std::string                          why;
        std::string                          targetLabel;  // "API", "Centrale PM5560", "Variateur ATV320 \xC2\xB7 esclave simul\xC3\xA9"
        bool                                 slave{false}; // vise un esclave simule (violet)
        bool                                 schneider{false};   // les adresses Schneider ont un sens ici
        std::string                          equipment;    // l'equipement vise (vide : l'API, une adresse)
        std::vector<int>                     colors;       // par valeur : la couleur de sa courbe (-1 : pas tracee)
    };
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] const Row* row(int id) const;
    [[nodiscard]] int selected() const noexcept { return selected_; }
    bool select(int id);

    // Ajouter (rend le numero de la requete ; 0 : refusee, why).
    int addRequest(hmi::ModbusRead read, std::string* why = nullptr);
    int addNew(std::string* why = nullptr);                  // Nouvelle requete (Inser)
    int addFromReadTab(std::string* why = nullptr);          // Reprendre la requete de Lecture / ecriture
    // Depuis des variables : leurs noms (de l'API, ou des variables IHM liees), regroupees.
    std::vector<int> addFromVariables(const std::vector<std::string>& names, bool merge = true, int gap = 10, std::string* why = nullptr);
    std::vector<int> addPlanned(const std::vector<HmiCyclicVarsDialog::Planned>& planned);
    // Depuis les zones memoire d'un equipement : une requete par zone (125 mots, 2000 bits au plus).
    std::vector<int> addFromZones(const std::string& equipment, std::string* why = nullptr);
    // Coller depuis Excel : colonnes Nom, Equipement, Fonction, Adresse, Nombre, Format, Periode.
    struct PasteReport {
        std::size_t added{0}, refused{0};
        std::vector<std::string> notes;       // "ligne 3 : adresse illisible"
        std::vector<int> ids;
    };
    PasteReport pasteRequests(const std::string& text);
    bool duplicate(int id);
    bool remove(int id);
    bool setActive(int id, bool on);
    // Un reglage de la requete : "nom", "active", "cible" (un libelle du choix de la cible),
    // "hote", "port", "esclave", "delai", "fonction", "adresse" (0..65535, 40101, %MW100),
    // "nombre", "format", "ordre", "periode" (0 / commune, des ms), "tracer" (rang, oui/non).
    bool setField(int id, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Le meme, sans annuler ni rafraichir ni message (un brouillon : coller depuis Excel).
    bool setFieldQuiet(int id, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Un reglage de la lecture : "periode_commune", "enchainement", "pause", "echecs",
    // "fenetre", "enregistrer", "pistes".
    bool setOption(const std::string& key, const std::string& value, std::string* why = nullptr);
    bool setTraced(int id, std::size_t value, bool on);
    [[nodiscard]] std::vector<std::string> targetChoices() const;
    // Annuler / retablir un geste sur la liste.
    bool undoList();
    bool redoList();
    [[nodiscard]] bool canUndoList() const noexcept { return !undo_.empty(); }

    // ---- la lecture ----
    bool start(std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const;
    void setFrozen(bool on);
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    bool resume(int id);
    void clearData();
    [[nodiscard]] mbtool::MultiPoller& poller() noexcept { return poller_; }
    [[nodiscard]] const hmi::ModbusReadSet& settings() const noexcept { return set_; }

    // ---- les jeux (MB-6) ----
    [[nodiscard]] const std::string& setName() const noexcept { return set_.name; }
    [[nodiscard]] bool setModified() const;
    // Enregistrer (nom vide : le nom du jeu) - une commande sur le projet.
    bool saveSet(const std::string& name = {}, std::string* why = nullptr);
    bool openSet(const std::string& name, std::string* why = nullptr);
    void newSet();
    // Le jeu en CSV (le meme pour tous les postes d'une affaire), et le retour.
    [[nodiscard]] std::string setCsv() const;
    bool exportSet(std::string* where = nullptr);
    bool importSet(const std::string& csv, std::string* why = nullptr);
    // Le dernier jeu du projet revient (a l'ouverture de l'outil) ; faux : aucun.
    bool restoreLastSet();

    // ---- exporter, enregistrer (MB-7, MB-8) ----
    bool exportCsv(bool oneFile, bool raw, std::string* where = nullptr);
    [[nodiscard]] std::string exportPreview(bool oneFile, bool raw) const;
    bool setRecording(bool on, std::string* why = nullptr);
    [[nodiscard]] bool recording() const;
    [[nodiscard]] std::string recordFolder() const;

    // ---- l'affichage ----
    void setLanes(bool on);
    [[nodiscard]] bool lanes() const noexcept { return set_.lanes; }
    void showJournal(bool on);
    [[nodiscard]] bool journalShown() const noexcept { return journal_; }
    void setOnlySelected(bool on);
    [[nodiscard]] bool onlySelected() const noexcept { return onlySelected_; }

    // Une ligne du tableau des valeurs (les tests ; le tableau).
    struct ValueRow {
        int         request{0};
        int         value{-1};          // rang dans la requete (-1 : la ligne d'une requete pas encore lue)
        bool        traceable{false}, traced{false};
        int         color{-1};
        std::string address, schneider, name, value_, min, max, mean, changes, ago;
        bool        slave{false}, animated{false}, forced{false}, error{false};
        double      changedAt{0};
    };
    [[nodiscard]] const std::vector<ValueRow>& valueRows() const noexcept { return valueRows_; }
    // Les textes d'une requete dans le tableau (colonnes du tableau des requetes).
    [[nodiscard]] std::vector<std::string> requestCells(int id) const;
    [[nodiscard]] std::string stateText(int id) const;          // "bonne \xC2\xB7 61 ms", "en pause : exception 02"
    // La barre d'etat : "Lecture cyclique : 5 requetes, 3 equipements . R5 en pause (exception 02)".
    [[nodiscard]] std::string statusText() const;
    // A droite de la barre d'etat : "R4 lit un esclave simule" (vide : aucune).
    [[nodiscard]] std::string slaveStatus() const;
    [[nodiscard]] std::string badge() const;                     // "5 requetes"

    // ---- la grille et ce qui est dessous ----
    void properties(std::vector<ui::PropertyGrid::Category>& cats);
    void refreshSide(HmiCyclicSide& side);

    // ---- menus et dialogues ----
    enum MenuId : int {
        MNew = 1, MFromRead, MFromVars, MFromZones, MPaste, MOpenSet,
        MSave = 20, MSaveAs, MNewSet, MExportSet, MImportSet, MSetBase = 100, MZoneBase = 500,
    };
    void openAddMenu(gfx::Point at);
    void openSetsMenu(gfx::Point at);
    void openZonesMenu(gfx::Point at);
    void openVariablesDialog();
    void openExportDialog();
    void openSaveAs();
    [[nodiscard]] HmiCyclicOverlay* overlay() const noexcept;
    void dropOverlay();

    // ---- les morceaux (les tests, les scripts) ----
    [[nodiscard]] HmiCyclicTable& requestsTable() noexcept { return *requests_; }
    [[nodiscard]] HmiCyclicTable& lowerTable() noexcept { return *lower_; }
    [[nodiscard]] HmiLaneChart&   chart() noexcept { return *chart_; }
    [[nodiscard]] HmiBlockHead&   requestsHead() noexcept { return *headRequests_; }
    [[nodiscard]] HmiBlockHead&   chartHead() noexcept { return *headChart_; }
    [[nodiscard]] HmiBlockHead&   lowerHead() noexcept { return *headLower_; }

    // Chaque image (le volet) ; `force` : tout refaire maintenant.
    void tick(double time, bool force = false);
    // Les touches de l'outil (Inser, Ctrl+S, Ctrl+Z, Ctrl+Y, Suppr) ; vrai : prise.
    bool key(const ui::KeyDown& k);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    friend class HmiModbusToolPane;
    void changed(bool structure);                 // la liste a change : le moteur, les tableaux, la grille
    void remember();                              // avant un geste sur la liste (annuler)
    void resolve(Row& r) const;                   // la cible, les valeurs, la requete du moteur
    [[nodiscard]] int nextId() const;
    void pushToPoller();
    void refreshRequests();
    void refreshLower();
    void refreshChart(double now);
    void refreshHeads();
    void assignColors();                          // les couleurs des valeurs tracees (une palette qui tourne)
    [[nodiscard]] mbtool::CyclicOptions options() const;
    void showOverlay(std::unique_ptr<HmiCyclicOverlay> o);
    [[nodiscard]] std::string valueName(const Row& r, int offset, int bit) const;
    [[nodiscard]] const mbtool::MultiPoller::RequestState* stateOf(int id) const;
    [[nodiscard]] hmi::ModbusReadSet currentSet() const;
    void loadSet(const hmi::ModbusReadSet& set);
    [[nodiscard]] std::vector<CyclicVarTab> variableTabs() const;
    [[nodiscard]] std::string twinCellOf(const Row& r, int offset, bool* forced,
                                         std::optional<std::pair<double, double>>* band) const;
    void say(std::string text, bool error = false);

    HmiModbusToolPane&           tool_;
    HmiBlockHead*                headRequests_{nullptr};
    HmiCyclicTable*              requests_{nullptr};
    HmiBlockHead*                headChart_{nullptr};
    HmiLaneChart*                chart_{nullptr};
    HmiBlockHead*                headLower_{nullptr};
    HmiCyclicTable*              lower_{nullptr};
    std::vector<Row>             rows_;
    hmi::ModbusReadSet           set_;                 // le nom et les reglages du jeu de travail (les requetes : rows_)
    int                          selected_{0};
    std::size_t                  laidRows_{0};         // le nombre de requetes de la derniere mise en page
    bool                         frozen_{false};
    double                       frozenAt_{0};
    bool                         journal_{false};
    bool                         onlySelected_{false};
    bool                         wantRecord_{false};
    mbtool::MultiPoller          poller_;
    std::vector<mbtool::MultiPoller::RequestState> states_;
    std::vector<ValueRow>        valueRows_;
    std::vector<int>             lowerRequest_;        // la requete de chaque ligne du tableau du bas
    std::vector<int>             lowerValue_;          // et sa valeur (-1 : aucune)
    struct Snapshot { std::vector<Row> rows; int selected{0}; };
    std::vector<Snapshot>        undo_, redo_;
    double                       lastTick_{-10};
    double                       lastChart_{-10};
    bool                         restored_{false};
    HmiCyclicOverlay*            overlay_{nullptr};
    std::vector<ui::WidgetPtr>   graveyard_;          // les menus fermes, detruits a l'image suivante
    bool                         closing_{false};
    bool                         quiet_{false};       // setFieldQuiet
    mutable std::map<std::string, std::string> plcNames_;   // "%MW1058" -> la variable de l'API (cache)
    mutable std::map<std::string, std::string> equipNames_; // "equipement|fonction|place|bit" -> la variable IHM liee (cache)
    core::ConnectionScope        links_;
};

// Les pictogrammes des menus et des cases (dessines ici, pas des icones du theme).
enum CyclicGlyph : int {
    GNone = 0, GPlus, GReadWrite, GVariables, GMemory, GTable, GFolder, GSave, GExport, GFlask,
    GBoxOn = 50, GBoxOff, GBoxDisabled, GDotOk, GDotWarn, GDotErr, GDotOff, GDotSim, GForced, GChipBase = 100,
};
void drawCyclicGlyph(gfx::IRenderer& r, int glyph, const gfx::Rect& box, gfx::Color c);
// La couleur de la courbe n (une palette qui tourne : 16 teintes).
[[nodiscard]] gfx::Color cyclicSeriesColor(int index);

} // namespace app
