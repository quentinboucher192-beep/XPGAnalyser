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
    // Pour les sessions rejouees : la prochaine reponse de l'explorateur (il
    // ne s'ouvre pas), et le contenu "fichiers" du presse-papiers.
    void        queueFilePick(std::string answer);
    void        setScriptedClipboardFiles(std::vector<std::string> files);

    // Lot API 7 : UNE AUTRE FENETRE (un onglet detache, ui/WidgetHost). Le temps
    // de lui passer un evenement, de la mettre en page et de la dessiner,
    // surfaceSize() rend la taille de SA surface et non celle de la fenetre
    // principale : une liste ou un menu s'y ouvre du bon cote et s'y referme au
    // clic a cote. Les portees s'imbriquent ; la sortie rend la precedente.
    class SurfaceScope {
    public:
        explicit SurfaceScope(gfx::Size size) noexcept;
        ~SurfaceScope();
        SurfaceScope(const SurfaceScope&) = delete;
        SurfaceScope& operator=(const SurfaceScope&) = delete;
    private:
        gfx::Size previous_;
        bool      hadPrevious_;
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

        // Deepest visible widget under `p` that has a tooltip. Deepest wins: a
        // tooltip on a cell should beat one on the panel that contains it.
        [[nodiscard]] const Widget* tooltipAt(gfx::Point p) const;

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

        std::string             id_;
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