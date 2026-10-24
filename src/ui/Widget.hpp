// =============================================================================
//  ui/Widget.hpp - retained-mode widget base
// -----------------------------------------------------------------------------
//  Ownership: a Widget owns its children by unique_ptr and holds a raw
//  *non-owning* observer pointer to its parent. That back-pointer is the one
//  place raw pointers appear in the code base; a weak_ptr cycle-breaker here
//  would cost an atomic load on every hit test for no safety gain, because a
//  child can never outlive its parent by construction.
//
//  Rendering is retained + invalidation-driven: Render() is only called for
//  subtrees whose dirty flag is set, or when the window itself was damaged.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../platform/Geometry.hpp"
#include "../platform/InputEvent.hpp"
#include "../platform/Renderer.hpp"
#include "Theme.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {

    class Widget;
    using WidgetPtr = std::unique_ptr<Widget>;
    struct ZoomMap;     // 1.12.30 : ui/Zoom.hpp (la correspondance de deux reperes)

    // ---------------------------------------------------------------------------
    //  Platform services the widget layer needs but must not link against.
    //
    //  sizeHint() has no renderer to ask, yet a button that does not know how wide
    //  its own label is will draw text outside itself. App installs these once, as
    //  soon as the renderer exists; until then the fallbacks derive from the pixel
    //  size carried in FontId, which is approximate but never wildly wrong.
    // ---------------------------------------------------------------------------
    // Lot macros 1 : ce qu'on demande a l'explorateur de fichiers du systeme.
    struct FilePick {
        std::string title;
        // Des filtres "nom", "motif" ; le motif a la maniere de SDL : "xlsx;xlsm".
        std::vector<std::pair<std::string, std::string>> filters;
        std::string start;      // le dossier (ou le fichier) de depart ; vide : au choix du systeme
        bool        save{false};
        // Un dossier, pas un fichier (les filtres ne comptent pas) : le bouton
        // ... des champs de dossier (ui/widgets/PathBrowse.hpp).
        bool        folder{false};
    };

    struct PlatformServices {
        std::function<float(std::string_view, gfx::FontId)> measureWidth;
        std::function<float(gfx::FontId)>                   lineHeight;
        std::function<std::string()>                        clipboardGet;
        std::function<void(std::string_view)>               clipboardSet;
        // The window, in logical pixels. A popup needs it to know whether it fits
        // below its owner or has to flip above, and no widget can reach the renderer
        // outside a paint. Absent (the default) means "do not clamp", which is what
        // a test that never installed it wants.
        std::function<gfx::Size()>                          surfaceSize;
        // --- ajoutes a la FIN : App les pose apres l'initialisation agregee ---
        // LOT MACROS 1 : L'EXPLORATEUR DE FICHIERS (le bouton ...) et les
        // fichiers copies dans l'Explorateur (Ctrl+V sur un champ de fichier).
        // pickFile est asynchrone : `done` est appele plus tard, sur le fil de
        // l'interface, avec le chemin choisi ("" : annule). Faux : pas
        // d'explorateur ici (tests, console), `done` ne sera jamais appele.
        std::function<bool(const FilePick&, std::function<void(std::string)>)> pickFile;
        std::function<std::vector<std::string>()>                       clipboardFiles;
        // 1.12.27 : glisser du texte HORS de la fenetre (vers une autre fenetre,
        // une autre application), jusqu'au lacher : 1 depose, 0 annule. Absent :
        // pas de glisser hors de la fenetre ici (les essais, Linux).
        std::function<int(std::string_view)>                            dragText;
    };

    void        installPlatformServices(PlatformServices services);
    float       measureWidth(std::string_view utf8, gfx::FontId font);
    float       lineHeight(gfx::FontId font);
    gfx::Size   surfaceSize();   // {0,0} when the host has not provided one
    std::string clipboardText();
    void        setClipboardText(std::string_view utf8);
    // Lot macros 1. L'explorateur de fichiers : faux quand il n'y en a pas.
    bool        pickFile(const FilePick& request, std::function<void(std::string)> done);
    [[nodiscard]] bool canPickFile();
    // Les fichiers copies dans l'Explorateur (vide : aucun, ou du texte).
    [[nodiscard]] std::vector<std::string> clipboardFiles();
    // 1.12.27 : glisser du texte hors de la fenetre (-1 : pas ici ; 0 annule ; 1 depose).
    int                dragText(std::string_view utf8);
    [[nodiscard]] bool canDragText();
    // Pour les sessions rejouees : la prochaine reponse de l'explorateur (il
    // ne s'ouvre pas), et le contenu "fichiers" du presse-papiers.
    void        queueFilePick(std::string answer);
    void        setScriptedClipboardFiles(std::vector<std::string> files);

    // Lot API 7 : UNE AUTRE FENETRE (un onglet detache, ui/WidgetHost). Le temps
    // de lui passer un evenement, de la mettre en page et de la dessiner,
    // surfaceSize() rend la taille de SA surface et non celle de la fenetre
    // principale : une liste ou un menu s'y ouvre du bon cote et s'y referme au
    // clic a cote. Les portees s'imbriquent ; la sortie rend la precedente.
    //  1.12.31 : `zoom` vrai - la portee d'un widget zoome (son repere agrandi) : la
    //  surface y est vue de ce repere, mais la FENETRE ne change pas (windowSurfaceSize).
    class SurfaceScope {
    public:
        explicit SurfaceScope(gfx::Size size, bool zoom = false) noexcept;
        ~SurfaceScope();
        SurfaceScope(const SurfaceScope&) = delete;
        SurfaceScope& operator=(const SurfaceScope&) = delete;
    private:
        gfx::Size previous_;
        bool      hadPrevious_;
        gfx::Size previousWindow_{};
        bool      hadPreviousWindow_{false};
        bool      zoom_{false};
    };
    //  1.12.31 : la fenetre du moment (la principale, ou celle d'un onglet detache), en
    //  pixels de sa surface, quel que soit le zoom des widgets traverses ({0,0} : inconnue).
    [[nodiscard]] gfx::Size windowSurfaceSize();

    // 1.12.30 : LA MESURE DU TEXTE PENDANT UNE MISE EN PAGE ZOOMEE. Un widget
    // zoome (ZoomSupport::Scaled) se met en page dans son repere agrandi :
    // measureWidth() et lineHeight() y rendent la mesure de la police a la
    // taille dessinee, ramenee a ce repere - la meme que celle du dessin
    // (ui/Zoom.hpp, ZoomRenderer). Les portees s'imbriquent (les zooms se
    // multiplient) ; la sortie rend la precedente.
    //  1.12.31 : `absolute` vrai - le zoom de la mesure EST `factor` (un ilot, voir
    //  Widget : il se mesure a l'echelle de la surface, quel que soit le zoom de sa page).
    class MeasureZoomScope {
    public:
        explicit MeasureZoomScope(float factor, bool absolute = false) noexcept;
        ~MeasureZoomScope();
        MeasureZoomScope(const MeasureZoomScope&) = delete;
        MeasureZoomScope& operator=(const MeasureZoomScope&) = delete;
    private:
        float previous_;
    };
    [[nodiscard]] float measureZoom() noexcept;       // 1 : pas de zoom en cours
    // La police a la taille dessinee sous un zoom (6 px au moins).
    [[nodiscard]] gfx::FontId zoomedFont(gfx::FontId font, float factor) noexcept;

    // 1.12.30 : CE QU'UN WIDGET SAIT DU ZOOM (voir Widget, "le contrat du zoom").
    enum class ZoomSupport : std::uint8_t {
        None,     // pas de zoom (le defaut)
        Scaled,   // la base le zoome : mis en page dans un repere agrandi, dessine a l'echelle
        Native,   // il zoome lui-meme (un canevas, un grafcet, un code, le rack) : zoom(), setZoom()
    };

    // What a widget wants; the parent layout arbitrates.
    struct SizeHint {
        gfx::Size preferred{};
        gfx::Size minimum{};
        float     stretchX{ 0.f };   // 0 = fixed, >0 = share of leftover space
        float     stretchY{ 0.f };
    };

    enum class Visibility : std::uint8_t { Visible, Hidden /*keeps space*/, Collapsed /*no space*/ };

    struct PaintContext {
        gfx::IRenderer& r;
        const Theme& theme;
        gfx::Rect       clip;      // already intersected with the widget bounds
        double          time;      // seconds since start, for carets and spinners
        // Widgets that must paint above their siblings (a dropdown popup) push
        // themselves here during the normal pass; the host paints them afterwards.
        std::vector<Widget*>* overlays{ nullptr };
    };

    class Widget {
    public:
        explicit Widget(std::string id = {});
        virtual ~Widget();

        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;

        // --- identity ---------------------------------------------------------
        [[nodiscard]] const std::string& id() const noexcept { return id_; }

        // --- geometry ---------------------------------------------------------
        void                     setBounds(const gfx::Rect& r);
        [[nodiscard]] gfx::Rect  bounds() const noexcept { return bounds_; }
        [[nodiscard]] gfx::Rect  contentRect() const noexcept;   // bounds minus padding
        [[nodiscard]] gfx::Point toLocal(gfx::Point global) const noexcept;
        void                     setPadding(gfx::Edges e) { padding_ = e; invalidateLayout(); }
        [[nodiscard]] gfx::Edges padding() const noexcept { return padding_; }

        // --- state ------------------------------------------------------------
        void setVisibility(Visibility v);
        [[nodiscard]] Visibility visibility() const noexcept { return visibility_; }
        [[nodiscard]] bool visible() const noexcept { return visibility_ == Visibility::Visible; }

        void setEnabled(bool e);
        [[nodiscard]] bool enabled() const noexcept { return enabled_ && (!parent_ || parent_->enabled()); }

        [[nodiscard]] bool hovered() const noexcept { return hovered_; }
        [[nodiscard]] bool focused() const noexcept { return focused_; }
        void setFocusPolicy(bool acceptsFocus) noexcept { acceptsFocus_ = acceptsFocus; }
        [[nodiscard]] bool acceptsFocus() const noexcept { return acceptsFocus_ && enabled() && visible(); }
        // 1.12.18 : donner le clavier a ce widget depuis l'exterieur (un menu qui se
        // ferme le rend a celui qui l'avait : l'arbre, la barre d'onglets) ; le
        // widget qui l'a sous `root` ; vrai si `w` est encore sous `root`.
        void takeFocus() { grabFocus(); }
        [[nodiscard]] Widget& topWidget() noexcept { return *rootWidget(); }
        [[nodiscard]] static Widget* focusedIn(Widget& root) noexcept;
        [[nodiscard]] static bool    inTree(Widget& root, const Widget* w) noexcept;

        void setTooltip(std::string t) { tooltip_ = std::move(t); }
        [[nodiscard]] const std::string& tooltip() const noexcept { return tooltip_; }
        // Lot 7 : L'INFOBULLE QUI SUIT CE QU'ELLE MONTRE. L'hote qui la dessine
        // (WidgetMenu, WidgetHost) redemande son texte a chaque image tant
        // qu'elle est ouverte : une valeur en simulation, un numero de cycle,
        // "en marche" changent sous les yeux, sans bouger la souris. Par
        // defaut : le fournisseur s'il y en a un, sinon tooltip(). Un widget dont
        // l'infobulle depend de ce qui est sous la souris (une ligne, une case,
        // une partie) la recalcule ici, a partir de ses donnees du moment ;
        // `mouse` : la souris, en coordonnees de la surface.
        [[nodiscard]] virtual std::string liveTooltip(gfx::Point mouse) const;
        // Un texte calcule a la demande (relu tant que l'infobulle est ouverte) ;
        // nul : tooltip() seul. Un widget a une infobulle des qu'il a l'un ou
        // l'autre (un fournisseur qui rend "" n'en montre pas) ; un widget qui
        // calcule les siennes (liveTooltip) le dit aussi ici.
        void setTooltipProvider(std::function<std::string()> provider) { tooltipProvider_ = std::move(provider); }
        [[nodiscard]] virtual bool hasTooltip() const { return !tooltip_.empty() || static_cast<bool>(tooltipProvider_); }

        // --- tree -------------------------------------------------------------
        Widget& addChild(WidgetPtr child);
        WidgetPtr removeChild(Widget& child);              // returns ownership to caller
        [[nodiscard]] Widget* parent() const noexcept { return parent_; }
        [[nodiscard]] const std::vector<WidgetPtr>& children() const noexcept { return children_; }
        [[nodiscard]] Widget* findById(std::string_view id);

        // --- frame ------------------------------------------------------------
        void layout();                                     // recurses if dirty
        void render(const PaintContext& ctx);              // clips, then onPaint + children
        EventResult dispatch(const InputEvent& ev);        // capture -> target -> bubble

        void invalidate() noexcept;                        // repaint needed
        void invalidateLayout() noexcept;                  // re-measure needed
        [[nodiscard]] bool needsRepaint() const noexcept { return dirtyPaint_; }

        [[nodiscard]] virtual SizeHint sizeHint() const;

        // Opt in to a second, top-most paint pass (see PaintContext::overlays).
        [[nodiscard]] virtual bool requestsOverlayPass() const { return false; }
        void paintTopMost(const PaintContext& ctx);

        // The rectangle that accepts pointer events. Normally the widget's own
        // bounds, but a widget painting outside them - a dropdown popup - must
        // widen this or dispatch() will reject the click before onEvent() sees it.
        [[nodiscard]] virtual gfx::Rect eventBounds() const { return bounds_; }
        // Lot 21 : un widget de la passe du dessus qui laisse voir (et toucher)
        // ce qui est dessous hors de sa bulle - le didacticiel interactif. Faux :
        // les infobulles du dessous reviennent a cet endroit.
        [[nodiscard]] virtual bool overlayCovers(gfx::Point) const { return true; }

        // Depth-first search for a descendant that is currently painting on top of
        // everything. Such a widget gets the next event before its siblings do,
        // regardless of tree order: an open popup drawn over a table must not have
        // its clicks stolen by the table underneath.
        [[nodiscard]] Widget* findOverlayOwner();

        // 1.11.4 : LA SOURIS TENUE. Un widget qui commence un glisser (le pouce d'une
        // barre de defilement) la tient : jusqu'au relachement, les deplacements et le
        // relachement lui arrivent, meme hors de lui et de ses parents (dispatch, lui,
        // les laisse tomber hors de eventBounds()). routeCapturedMouse() est appele a
        // l'entree d'un arbre (WidgetMenu, WidgetHost).
        void captureMouse() noexcept;
        void releaseMouse() noexcept;                      // seulement s'il la tient
        [[nodiscard]] bool holdsMouse() const noexcept;
        // Un deplacement ou un relachement, au widget qui tient la souris s'il est dans
        // l'arbre de `root` (le relachement la rend). Vrai : l'evenement est pris.
        static bool routeCapturedMouse(Widget& root, const InputEvent& ev);

        // Deepest visible widget under `p` that has a tooltip. Deepest wins: a
        // tooltip on a cell should beat one on the panel that contains it.
        [[nodiscard]] const Widget* tooltipAt(gfx::Point p) const;

        // --- 1.12.30 : LE CONTRAT DU ZOOM ---------------------------------------
        //  zoomSupport() dit si le widget se zoome. Scaled : la BASE s'en charge -
        //  le widget et ses enfants sont mis en page dans un repere agrandi (ses
        //  bornes divisees par le zoom, le coin haut-gauche ne bouge pas), puis
        //  dessines a l'echelle : le texte, la hauteur des lignes, les marges, les
        //  icones et les barres de defilement suivent, sans rien changer a leur
        //  code ; les evenements leur arrivent dans ce repere. Native : le widget
        //  zoome lui-meme et redefinit zoom() / setZoom() (et ses bornes).
        //  A 100 %, rien ne change : ni repere, ni renderer.
        //  Ctrl + molette sur un widget Scaled (hors d'un widget Native) demande un
        //  cran (setZoomRequestHandler : l'appli zoome la page entiere).
        //  1.12.31 : LES ILOTS. Un widget Native sous un widget zoome (une page zoomee :
        //  le code, la vue, le grafcet, le rack qu'elle contient) n'en prend pas le
        //  zoom : la base le met en page et le dessine a l'echelle de la surface - son
        //  repere EST celui de la surface (ses bornes : son cadre vu de la surface,
        //  arrondi au pixel), il se dessine avec le renderer de la surface, s'y mesure,
        //  et ses evenements y arrivent. Seul son zoom a lui compte. Un signal qui donne
        //  une position la donne dans la surface (toSurface) ; un menu s'y ouvre par
        //  PopupMenu::openAtSurface.
        [[nodiscard]] ZoomSupport zoomSupport() const noexcept { return zoomSupport_; }
        [[nodiscard]] bool zoomable() const noexcept { return zoomSupport_ != ZoomSupport::None; }
        void setZoomSupport(ZoomSupport s);
        [[nodiscard]] virtual float zoom() const { return zoomFactor_; }
        virtual void setZoom(float zoom);
        [[nodiscard]] virtual float minZoom() const { return 0.5f; }
        [[nodiscard]] virtual float maxZoom() const { return 2.f; }
        //  Le cadre donne par le parent (dans SON repere) : bounds() a 100 %.
        [[nodiscard]] gfx::Rect zoomFrame() const noexcept { return frame_; }
        //  Un point (un cadre) du repere de ce widget vers celui de la surface - les
        //  widgets zoomes au-dessus de lui compris - et l'inverse. Un signal qui
        //  donne une position (un menu a ouvrir) la donne dans la surface.
        [[nodiscard]] gfx::Point toSurface(gfx::Point own) const noexcept;
        [[nodiscard]] gfx::Rect  toSurface(const gfx::Rect& own) const noexcept;
        [[nodiscard]] gfx::Point fromSurface(gfx::Point surface) const noexcept;
        [[nodiscard]] float      surfaceScale() const noexcept;        // le produit des zooms (1 : aucun)
        //  1.12.31 : la correspondance de ce repere vers celui du parent, et vers la
        //  surface (les ilots compris : celui d'un ilot vers la surface est l'identite).
        [[nodiscard]] ZoomMap    mapToParent() const noexcept;
        [[nodiscard]] ZoomMap    mapToSurface() const noexcept;
        //  1.12.31 : vrai - un ilot (un widget Native sous un widget zoome, voir plus haut).
        [[nodiscard]] bool       zoomIsland() const noexcept { return islandNow(); }
        //  Un evenement venu de la surface (l'hote : une liste ouverte, un menu)
        //  pour ce widget, ramene au repere de son parent.
        EventResult dispatchFromSurface(const InputEvent& ev);
        //  L'infobulle du moment, la souris donnee dans la surface.
        [[nodiscard]] std::string liveTooltipAt(gfx::Point surfaceMouse) const;
        //  Ce que fait Ctrl + molette sur un widget Scaled : `step` +1 / -1. Sans
        //  hote (les essais), le widget zoome seul, d'un cran.
        using ZoomRequest = std::function<void(Widget&, int step)>;
        static void setZoomRequestHandler(ZoomRequest handler);
        static void requestZoomStep(Widget& w, int step);

        // --- signals ----------------------------------------------------------
        const core::SignalPtr<>      focusGained = core::Signal<>::create();
        const core::SignalPtr<>      focusLost = core::Signal<>::create();
        const core::SignalPtr<bool>  hoverChanged = core::Signal<bool>::create();

    protected:
        // Overridables. Default implementations do nothing, so a subclass only
        // implements what it actually needs.
        virtual void        onLayout() {}
        virtual void        onPaint(const PaintContext&) {}
        virtual void        onPaintOverlay(const PaintContext&) {}  // drawn above children
        virtual EventResult onEvent(const InputEvent&) { return EventResult::Ignored; }
        virtual void        onEnabledChanged(bool) {}
        virtual void        onFocusChanged(bool) {}
        virtual bool        hitTest(gfx::Point local) const;

        void grabFocus();
        void releaseFocus();
        [[nodiscard]] Widget* rootWidget() noexcept;
        static void clearFocusRecursive(Widget& w, Widget* keep);

    private:
        friend class FocusChain;
        friend class WidgetHost;
        void setHovered(bool h);
        void setFocusedInternal(bool f);
        // 1.12.30 : le zoom. Vrai : un repere agrandi (Scaled, zoom different de 1).
        [[nodiscard]] bool scaledNow() const noexcept { return zoomSupport_ == ZoomSupport::Scaled && zoomFactor_ != 1.f; }
        // 1.12.31 : un ilot - un widget Native sous un widget zoome : son repere est celui
        // de la surface (islandS_, islandT_ : du repere du parent vers la surface).
        [[nodiscard]] bool islandNow() const noexcept {
            return zoomSupport_ == ZoomSupport::Native && (islandS_ != 1.f || islandT_.x != 0.f || islandT_.y != 0.f);
        }
        [[nodiscard]] gfx::Rect islandRect(const gfx::Rect& frame) const noexcept;   // un cadre du parent, dans la surface
        void refreshIsland();                                                        // a la mise en page
        [[nodiscard]] gfx::Point toParentSpace(gfx::Point own) const noexcept;    // le repere du parent
        [[nodiscard]] gfx::Point fromParentSpace(gfx::Point outer) const noexcept;
        [[nodiscard]] gfx::Rect  outerEventBounds() const;                        // eventBounds(), dans le repere du parent
        [[nodiscard]] bool       nativeZoomAt(gfx::Point own) const;              // un widget Native sous ce point ?
        EventResult dispatchOwn(const InputEvent& ev, const gfx::Point* p, bool inside);

        std::string             id_;
        gfx::Rect               frame_{};     // 1.12.30 : le cadre donne par le parent
        float                   zoomFactor_{ 1.f };
        float                   islandS_{ 1.f };      // 1.12.31 : un ilot (voir islandNow)
        gfx::Point              islandT_{};
        ZoomSupport             zoomSupport_{ ZoomSupport::None };
        gfx::Rect               bounds_{};
        gfx::Edges              padding_{};
        Visibility              visibility_{ Visibility::Visible };
        bool                    enabled_{ true };
        bool                    hovered_{ false };
        bool                    focused_{ false };
        bool                    acceptsFocus_{ false };
        bool                    dirtyPaint_{ true };
        bool                    dirtyLayout_{ true };
        std::string             tooltip_;
        std::function<std::string()> tooltipProvider_;   // lot 7 : l'infobulle calculee a la demande
        Widget* parent_{ nullptr };      // non-owning back reference
        std::vector<WidgetPtr>  children_;
    };

    // ---------------------------------------------------------------------------
    // Focus management is a tree-level concern, not a widget-level one: exactly one
    // widget per host holds focus, Tab walks the chain in layout order.
    // ---------------------------------------------------------------------------
    class FocusChain {
    public:
        void         setRoot(Widget* root) noexcept { root_ = root; }
        void         focus(Widget* w);
        [[nodiscard]] Widget* current() const noexcept { return current_; }
        void         focusNext(bool backwards = false);
        void         clear();
    private:
        Widget* root_{ nullptr };
        Widget* current_{ nullptr };
    };

} // namespace ui