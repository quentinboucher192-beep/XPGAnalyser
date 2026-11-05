// =============================================================================
//  app/hmi/HmiFontEditorPane.hpp - 1.26.0 : l'editeur de polices
// -----------------------------------------------------------------------------
//  LE CLIENT (05/11, maquette validee) : << pouvoir editer des TTF en live sur le
//  logiciel, en creer, en dupliquer a partir de >>. Le moteur : hmi/HmiTrueType.hpp.
//
//    [+ Nouvelle police] [Dupliquer a partir de...] | [Enregistrer (Mes polices)] [Enregistrer dans le projet] [Exporter en .ttf...] | ...
//    +----------------+-------------------------------------------+----------------+
//    | GLYPHES        |  (outils)   Capitales 1314 - - - - - - -  | GLYPHE << a >> |
//    | [chercher]     |             Hauteur d'x 978 - - - - - - -  | Chasse   981   |
//    | Tout Modifies  |        n    [  le contour, ses points  ]  d| POINT CHOISI   |
//    | A B C D E R    |  Ligne de base 0 _________________________ | POLICE         |
//    | ...            |  Descendante -550 - - - - -  [] Chasse 981 | Famille ...    |
//    +----------------+-------------------------------------------+----------------+
//    | APERCU EN DIRECT  [texte]   12  20  36                 [x] les vues suivent |
//    +--------------------------------------------------------------------------------+
//
//  OUVRIR : une police de Mes polices (Enregistrer la remplace), une ressource du
//  projet (Enregistrer dans le projet la remplace : les objets suivent), une police
//  fournie (verrouillee : on en modifie une COPIE, sous un autre nom). NOUVELLE
//  POLICE : les glyphes de base, vides. DUPLIQUER A PARTIR DE : une copie de
//  n'importe quelle TrueType (les noms reserves par sa licence refuses).
//
//  LE DESSIN : les points SUR la courbe (carres) et HORS de la courbe (ronds).
//  Choisir (clic, Maj+clic, un cadre), glisser, les fleches (1 unite ; Maj : 10),
//  Suppr ; double-clic sur un point : sur / hors de la courbe ; double-clic sur le
//  trait (ou l'outil Ajouter un point) : un point de plus, sans changer la forme.
//  La plume : des clics, un contour (Alt : un point hors de la courbe) ; clic sur
//  le premier point, Entree ou Echap le ferme. Rectangle, ellipse : glisser. La
//  chasse se glisse aussi (le carre en bas de sa ligne). Molette : zoom ; bouton
//  du milieu : deplacer la vue.
//
//  UN COMPOSE (<< e accent aigu >> = << e >> + l'accent) se montre sans ses points :
//  modifier << e >> le change ; Decomposer en fait un glyphe a dessiner ici.
//
//  ANNULER : Ctrl+Z, Ctrl+Y, dans l'editeur (chaque geste ; un glissement = un
//  geste). EN DIRECT : l'apercu se refait a chaque geste ; une police du projet
//  ouverte ici se dessine telle quelle dans les vues (<< les vues suivent >>) tant
//  qu'elle n'est ni enregistree ni abandonnee.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiTrueType.hpp"
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

namespace ui {
class SearchField;
}

namespace app {

class HmiToolStrip;
class HmiFontEditorPane;

// ---- la vue de travail : un glyphe, ses lignes, ses points ----
class HmiGlyphCanvas final : public ui::Widget {
public:
    HmiGlyphCanvas(std::string id, HmiFontEditorPane& pane);
    void fit();                                         // ajuste : le glyphe et ses hauteurs tiennent
    [[nodiscard]] double scale() const noexcept { return scale_; }
    // De l'ecran aux unites de la police, et retour.
    [[nodiscard]] std::pair<double, double> toFont(gfx::Point p) const;
    [[nodiscard]] gfx::Point toScreen(double x, double y) const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    enum class Drag : std::uint8_t { None, Points, Advance, Band, Shape, Pan };
    void ensureView();
    HmiFontEditorPane& pane_;
    double             scale_{0.2}, originX_{0}, baselineY_{0};
    bool               fitted_{false};
    gfx::Rect          fittedFor_{};
    Drag               drag_{Drag::None};
    gfx::Point         downAt_{}, lastAt_{};
    std::pair<double, double> downFont_{0, 0};
    gfx::TextureId     fill_{};
    std::uint64_t      fillKey_{0};
    gfx::IRenderer*    fillRenderer_{nullptr};
};

// ---- la grille des glyphes ----
class HmiGlyphGrid final : public ui::Widget {
public:
    HmiGlyphGrid(std::string id, HmiFontEditorPane& pane);
    void reveal(int glyph);                             // fait defiler jusqu'a lui
    [[nodiscard]] gfx::Rect cellOf(int glyph) const;    // a l'ecran ; vide : pas montre (les essais)

protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] int columns() const;
    HmiFontEditorPane& pane_;
    float              scroll_{0};
    struct Cell {
        gfx::TextureId tex{};
        std::uint64_t  revision{0};
        std::uint32_t  color{0};
    };
    std::map<int, Cell> cells_;
    gfx::IRenderer*     renderer_{nullptr};
};

// ---- l'apercu en direct ----
class HmiFontLivePreview final : public ui::Widget {
public:
    HmiFontLivePreview(std::string id, HmiFontEditorPane& pane);

protected:
    void onPaint(const ui::PaintContext&) override;

private:
    HmiFontEditorPane& pane_;
    struct Line {
        gfx::TextureId tex{};
        int            w{0}, h{0};
    };
    std::map<int, Line> lines_;          // par taille
    std::uint64_t       key_{0};
    gfx::IRenderer*     renderer_{nullptr};
};

class HmiFontEditorPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Action : int {
        ActNew = 1, ActDuplicate, ActSaveMine, ActSaveProject, ActExport, ActUndo, ActRedo, ActCopyFrom, ActRevert, ActDecompose,
        ActFit, ActLive, ActDiscard,
        ToolSelect = 50, ToolPen, ToolPoint, ToolRect, ToolEllipse,
        FilterAll = 70, FilterModified, FilterUpper, FilterLower, FilterDigits, FilterAccents, FilterSymbols
    };
    enum class Tool : std::uint8_t { Select, Pen, AddPoint, Rectangle, Ellipse };

    // Une police a ouvrir ou a copier.
    struct Source {
        std::string  label;              // "Carlito", "Ma police Station"
        std::string  group;              // "Sans empattement", "Mes polices", "Du projet" (les menus)
        hmi::BlobPtr data;
        std::string  userFile;           // une de Mes polices : son fichier (Enregistrer le remplace)
        hmi::Id      resource{hmi::kNoId};   // une ressource du projet (Enregistrer dans le projet la remplace)
        bool         builtIn{false};     // une fournie : on en modifie une copie
    };
    struct Hosts {
        std::function<std::vector<Source>()> sources;     // Dupliquer a partir de..., Copier le glyphe de...
        // Enregistrer dans Mes polices : `fileName` ("Ma police.ttf"), le contenu, le fichier a remplacer (vide : un
        // nouveau). Rend le chemin du fichier garde.
        std::function<bool(const std::string& fileName, const hmi::Bytes&, const std::string& replace, std::string* stored,
                           std::string* why)>
                              saveMine;
        std::function<void()> askExport;                  // Exporter en .ttf... : le chemin, puis exportTo
    };
    struct PointRef {
        int  contour{0}, point{0};
        bool operator<(const PointRef& o) const noexcept { return contour != o.contour ? contour < o.contour : point < o.point; }
        bool operator==(const PointRef&) const = default;
    };

    HmiFontEditorPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiFontEditorPane() override;
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // ---- ouvrir, creer, copier (faux et `why` : des modifications non enregistrees, sauf `force`) ----
    bool openFont(const Source&, std::string* why = nullptr, bool force = false);
    bool newFont(std::string* why = nullptr, bool force = false);
    bool duplicateFrom(const Source&, std::string family = {}, std::string* why = nullptr, bool force = false);
    void discard();                                     // Abandonner : revenir a l'enregistree (ou a l'ouverte)
    [[nodiscard]] bool hasFont() const noexcept { return !font_.glyphs.empty(); }
    [[nodiscard]] const hmi::ttf::Font& font() const noexcept { return font_; }
    [[nodiscard]] bool modified() const noexcept;
    [[nodiscard]] std::string title() const;            // "Police \xC2\xB7 Ma police Station"
    [[nodiscard]] std::string originText() const;
    [[nodiscard]] bool lockedSource() const noexcept { return lockedSource_; }

    // ---- les glyphes ----
    bool selectGlyph(int glyph);
    bool selectCode(std::uint32_t code);
    [[nodiscard]] int glyph() const noexcept { return glyph_; }
    [[nodiscard]] const std::vector<int>& shownGlyphs() const noexcept { return shown_; }
    void setSearch(const std::string& text);
    void setFilter(int filterAction);
    [[nodiscard]] int filter() const noexcept { return filter_; }
    [[nodiscard]] bool glyphModified(int glyph) const;
    [[nodiscard]] std::vector<hmi::ttf::Contour> outlineOf(int glyph) const { return hmi::ttf::outline(font_, glyph); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // ---- les points choisis ----
    [[nodiscard]] const std::set<PointRef>& chosen() const noexcept { return chosen_; }
    void choose(std::set<PointRef> points);
    void chooseAll();

    // ---- les gestes (un geste = un Ctrl+Z) ----
    bool moveChosen(int dx, int dy);
    bool removeChosen();
    bool toggleChosenKind();                            // sur / hors de la courbe
    bool insertPointNear(double x, double y, double radius);
    bool addShape(Tool shape, int x0, int y0, int x1, int y1);
    bool reverseContourOf(const PointRef&);
    // La plume : un point de plus au contour en cours (le premier : un contour neuf) ; ferme si (x, y) est son premier point.
    bool penClick(double x, double y, bool offCurve, double radius);
    void penEnd();
    [[nodiscard]] int penContour() const noexcept { return penContour_; }
    // Glisser (les points choisis, ou la chasse) : un seul geste du debut a la fin.
    void beginDrag(bool advance);
    void dragBy(int dx, int dy);
    void endDrag();
    // Un reglage : "chasse", "approche_gauche", "approche_droite", "x", "y", "sorte", "famille", "style", "version",
    // "ascendante", "descendante", "capitales", "hauteur_x", "interligne".
    bool setField(const std::string& key, const std::string& value, std::string* why = nullptr);
    bool decompose();
    bool copyGlyphFrom(const Source&, std::string* why = nullptr);
    bool revertGlyph();                                 // Retablir l'original (le glyphe tel qu'il etait a l'ouverture)
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return undo_.empty() ? std::string() : undo_.back().label; }

    // ---- enregistrer ----
    bool saveMine(std::string* why = nullptr);
    bool saveProject(std::string* why = nullptr);
    bool exportTo(const std::string& path, std::string* why = nullptr);
    [[nodiscard]] hmi::Id resource() const noexcept { return resource_; }
    [[nodiscard]] const std::string& userFile() const noexcept { return userFile_; }
    // Le fichier .ttf de la police telle qu'elle est (refait apres chaque geste ; pendant un glissement, au plus toutes
    // les 120 ms).
    [[nodiscard]] hmi::BlobPtr liveBytes(bool now = false);
    void setLive(bool on);                              // les vues suivent (une police du projet)
    [[nodiscard]] bool live() const noexcept { return live_; }

    // ---- l'apercu ----
    [[nodiscard]] const std::string& sample() const noexcept { return sample_; }
    void setSample(std::string text);

    // ---- l'outil ----
    void setTool(Tool t);
    [[nodiscard]] Tool tool() const noexcept { return tool_; }
    [[nodiscard]] std::vector<ui::PopupMenu::Item> sourceItems(int base) const;

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] HmiGlyphCanvas&    canvas() noexcept { return *canvas_; }
    [[nodiscard]] HmiGlyphGrid&      grid() noexcept { return *grid_; }
    [[nodiscard]] ui::PropertyGrid&  settings() noexcept { return *settings_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    // La police a change de nom, d'etat (modifiee, enregistree) : l'onglet refait son titre.
    const core::SignalPtr<> titleChanged = core::Signal<>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Meta {
        std::string family, version;
        bool        bold{false}, italic{false};
        int         weight{400}, ascent{0}, descent{0}, capHeight{0}, xHeight{0}, lineGap{0};
    };
    struct Step {
        std::string                                    label;
        std::vector<std::pair<int, hmi::ttf::Glyph>>   glyphs;     // avant
        std::uint64_t                                  id{0};      // l'etat qu'il laisse (l'enregistre : savedAt_)
        std::optional<Meta>                            meta;       // avant
        int                                            glyph{0};   // le glyphe montre
    };
    [[nodiscard]] Meta metaOf() const;
    void setMeta(const Meta&);
    bool edit(const std::string& label, std::vector<int> glyphs, bool meta, const std::function<bool()>& fn);
    Step capture(const std::string& label, const std::vector<int>& glyphs, bool meta) const;
    void restore(Step& s);                               // remet `s` et y garde l'etat d'avant (pour l'autre pile)
    bool load(hmi::ttf::Font f, const std::string& origin, bool locked, std::string userFile, hmi::Id resource);
    bool guard(std::string* why, bool force);
    void changed();
    void rebuildShown();
    void rebuildSettings();
    void say(std::string text, bool error = false);
    bool fail(std::string text, std::string* why);
    [[nodiscard]] hmi::ttf::Glyph* current();
    [[nodiscard]] bool familyAllowed(const std::string& family, std::string* why) const;
    [[nodiscard]] std::string freeFamily(const std::string& wanted) const;
    void openMenu(int action, std::vector<ui::PopupMenu::Item> items);
    void pushLive();

    hmi::DocumentPtr         doc_;
    Apply                    apply_;
    Hosts                    hosts_;
    hmi::ttf::Font           font_, original_;
    std::string              origin_;            // "Copie de Carlito", "Nouvelle police", "Mes polices", "Ressource du projet"
    bool                     lockedSource_{false};
    std::string              userFile_;
    hmi::Id                  resource_{hmi::kNoId};
    std::string              resourceName_;
    int                      glyph_{-1};
    std::vector<int>         shown_;
    std::string              search_;
    int                      filter_{FilterAll};
    std::set<PointRef>       chosen_;
    std::set<int>            touched_;           // les glyphes changes depuis l'ouverture (<< Modifies >>)
    std::vector<Step>        undo_, redo_;
    std::optional<Step>      dragStep_;
    hmi::ttf::Glyph          dragBase_;
    int                      dragAdvanceBase_{0};
    bool                     dragAdvance_{false};
    int                      penContour_{-1};
    Tool                     tool_{Tool::Select};
    std::uint64_t            revision_{1}, bytesRevision_{0}, nextStep_{1}, savedAt_{0};
    hmi::BlobPtr             bytes_;
    double                   bytesAt_{0};
    bool                     live_{true};
    std::string              sample_{"Pompe P1 en marche \xE2\x80\x94 12,4 bar \xE2\x80\x94 \xC3\xA0\xC3\xA9\xC3\xA8\xC3\xA7\xC3\xB9 \xE2\x82\xAC"};
    bool                     refreshing_{false};
    std::string              message_;

    HmiToolStrip*            tools_{nullptr};
    HmiToolStrip*            toolbox_{nullptr};
    ui::DropDown*            filterBox_{nullptr};
    HmiToolStrip*            liveTools_{nullptr};
    ui::SearchField*         searchField_{nullptr};
    HmiGlyphGrid*            grid_{nullptr};
    HmiGlyphCanvas*          canvas_{nullptr};
    ui::PropertyGrid*        settings_{nullptr};
    ui::InputText*           sampleField_{nullptr};
    HmiFontLivePreview*      preview_{nullptr};
    ui::PopupMenu*           menu_{nullptr};
    int                      menuAction_{0};
    std::vector<Source>      menuSources_;
    gfx::Rect                gridHead_{}, liveHead_{}, statusRect_{};
    core::ConnectionScope    links_;

    friend class HmiGlyphCanvas;
    friend class HmiGlyphGrid;
};

} // namespace app
