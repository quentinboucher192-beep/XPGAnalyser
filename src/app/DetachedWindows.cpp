// =============================================================================
//  app/DetachedWindows.cpp - les onglets detaches dans des fenetres a eux
// -----------------------------------------------------------------------------
//  CE QUI EST A CHAQUE FENETRE, ET POURQUOI :
//
//    son renderer    une texture appartient a son SDL_Renderer : l'atlas des
//                    polices (SdlRenderer en tient un par renderer) et les
//                    images du projet sont refaits pour la deuxieme fenetre,
//                    a la demande ;
//    son cache IHM   HmiImageCache::Scope, comme les ecrans secondaires du
//                    poste : le cache commun, promene d'un renderer a l'autre
//                    a chaque image, se viderait soixante fois par seconde ;
//    son hote        ui::WidgetHost : le survol, le focus (Tab), les listes
//                    ouvertes et les infobulles de la fenetre ;
//    sa surface      ui::SurfaceScope le temps de son tour : une liste ou un
//                    menu de la page s'ouvre dans SES bords.
//
//  LES DIALOGUES (lot API 8). La pile du MenuManager reste une, mais chaque
//  couche y a sa fenetre : le temps d'un evenement de la fenetre detachee,
//  elle est la fenetre "en cours" (MenuManager::WindowScope), et un dialogue
//  demande alors (un outil de la page, un bouton, une touche, un menu) est a
//  elle. Elle le dessine par-dessus sa page, a la taille de SA surface (il s'y
//  centre, avec son voile) ; son clavier et sa souris vont a lui. Il reste
//  modal pour toute l'application : tant qu'une question attend dans une
//  fenetre, les autres ne prennent rien, le disent (un voile, et ou elle
//  attend) et un clic ou une touche y ramene - la principale comprise, dont la
//  croix pose toujours la question habituelle. Fermee (ou l'onglet rendu)
//  pendant qu'un dialogue est a elle, la fenetre le rend a la principale,
//  intact. Un dialogue demande ailleurs (la fenetre principale, un script, un
//  minuteur) s'ouvre dans la principale, comme avant ; les ecrans pousses
//  aussi. Les menus et listes qui sont des widgets DE LA PAGE (les menus
//  contextuels des volets, les listes deroulantes) s'ouvrent dans la fenetre
//  detachee : ils passent par la passe du dessus de son hote.
//
//  LE RYTHME : chaque fenetre est redessinee a chaque image de l'appli (rien
//  ne dit, dans cette interface, qu'une valeur en direct a change), sauf
//  reduite ou cachee. Sa synchronisation verticale est coupee : deux fenetres
//  qui attendent chacune le balayage de l'ecran diviseraient la cadence par
//  deux ; la fenetre principale donne le rythme.
// =============================================================================
#include "DetachedWindows.hpp"
#include "SimDebugPane.hpp"            // Lot API 8 : F11 dans Simulation > Debogage
#include "hmi/HmiSimulation.hpp"       // 1.10 : F11 dans Simulation . IHM (le plein ecran de l'IHM)

#include "App.hpp"
#include "Brand.hpp"
#include "Capture.hpp"
#include "SearchFocus.hpp"            // lot API 8 : finitions (Ctrl+F)
#include "hmi/HmiImages.hpp"
#include "hmi/HmiImportDialog.hpp"     // 1.11.2 (decision 188) : un paquet lache va a la fenetre d import
#include "../menu/IMenu.hpp"
#include "../menu/MenuManager.hpp"
#include "../project/ProjectIcon.hpp"
#include "../ui/WidgetHost.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <utility>

// SDL, comme dans App.cpp (voir son commentaire) : l'include est inconditionnel
// quand la construction definit XPG_WITH_SDL.
#if !defined(XPG_WITH_SDL)
#  if __has_include(<SDL3/SDL.h>)
#    define XPG_WITH_SDL 1
#  else
#    define XPG_WITH_SDL 0
#  endif
#endif

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#  define XPG_HAVE_SDL3 1
#else
#  define XPG_HAVE_SDL3 0
#endif

namespace app {

namespace {

// La bande du haut de la fenetre, en pixels de la surface.
constexpr float kBand = 38.f;
// Le titre de l'application, apres celui de l'onglet quand la fenetre
// principale n'en dit pas plus (App : AppOptions::title).
constexpr const char* kAppTitle = "PLC Project Analyzer";

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Une page qui change de fenetre ferme ce qu'elle avait d'ouvert par-dessus
// (son menu contextuel, une liste deroulante) : ouvert dans une fenetre, il
// se dessinerait dans l'autre a des coordonnees qui n'y veulent rien dire, et
// une liste ouverte y mangerait le premier clic. Echap, comme a la main ; au
// plus quelques tours (un menu qui refuse Echap reste ouvert).
void closeOverlays(ui::Widget& page) {
    for (int guard = 0; guard < 4; ++guard) {
        ui::Widget* open = page.findOverlayOwner();
        if (!open || open->dispatch(ui::KeyDown{ui::Key::Escape, {}, false}) != ui::EventResult::Consumed) break;
        if (page.findOverlayOwner() == open) break;
    }
}

// La racine d'une fenetre detachee : la bande du haut (ce qu'on regarde, et le
// bouton qui le ramene) et la page, qui prend tout le reste.
class DetachedRoot final : public ui::Widget {
public:
    DetachedRoot(std::string title, ui::WidgetPtr page)
        : ui::Widget("detached.root"), title_(std::move(title)) {
        auto back = std::make_unique<ui::Button>("Ramener dans la fen\xC3\xAAtre principale", "detached.back");
        back->setTooltip("Remet cet onglet \xC3\xA0 sa place dans la fen\xC3\xAAtre principale.\n"
                         "Tu peux aussi fermer cette fen\xC3\xAAtre : l'onglet revient pareil.");
        back_ = static_cast<ui::Button*>(&addChild(std::move(back)));
        // Un onglet cache (replie) arrive tel quel : ici, il est la page.
        page->setVisibility(ui::Visibility::Visible);
        page_ = &addChild(std::move(page));
    }

    [[nodiscard]] ui::Button& backButton() const noexcept { return *back_; }
    // Lot API 8 : la page (les scripts y cherchent un outil, une ligne).
    [[nodiscard]] ui::Widget* page() const noexcept { return page_; }

    void setTitle(std::string title) {
        if (title == title_) return;
        title_ = std::move(title);
        invalidate();
    }

    // La page, rendue a son proprietaire : sans le survol ni le focus de cette
    // fenetre (elle arriverait surlignee, ou avec un deuxieme widget qui croit
    // avoir le focus dans la fenetre principale).
    ui::WidgetPtr takePage() {
        if (!page_) return nullptr;
        closeOverlays(*page_);
        ui::WidgetHost::release(*page_);
        ui::WidgetPtr owned = removeChild(*page_);
        page_ = nullptr;
        return owned;
    }

protected:
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float band = std::min(kBand, b.h);
        const ui::SizeHint hint = back_->sizeHint();
        const float bw = std::max(0.f, std::min(hint.preferred.w, b.w - 24.f));
        const float bh = std::max(0.f, std::min(hint.preferred.h, band - 8.f));
        back_->setBounds({b.right() - bw - 8.f, b.y + std::round((band - bh) * 0.5f), bw, bh});
        if (page_) page_->setBounds({b.x, b.y + band, b.w, std::max(0.f, b.h - band)});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        ctx.r.fillRect(b, c.windowBg);
        const gfx::Rect band{b.x, b.y, b.w, std::min(kBand, b.h)};
        ctx.r.fillRect(band, c.headerBg);
        ctx.r.fillRect({band.x, band.bottom() - 1.f, band.w, 1.f}, c.border);
        // Le titre de l'onglet, a gauche ; coupe avant le bouton s'il est long.
        const gfx::FontId font = ctx.theme.font.uiBold;
        const float left = band.x + 12.f;
        const float right = back_->bounds().x - 12.f;
        if (title_.empty() || right - left < 24.f) return;
        std::string shown = title_;
        if (ctx.r.measure(shown, font).width > right - left) {
            constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
            const float room = std::max(0.f, right - left - ctx.r.measure(kEllipsis, font).width);
            shown = shown.substr(0, ctx.r.fitCharacters(shown, font, room));
            shown += kEllipsis;
        }
        ctx.r.drawText({left, band.y + std::round((band.h - ctx.r.lineHeight(font)) * 0.5f)}, shown, font, c.text);
    }

private:
    std::string  title_;
    ui::Button*  back_{nullptr};
    ui::Widget*  page_{nullptr};
};

#if XPG_HAVE_SDL3
// Ce que montre une fenetre tant qu'une question attend dans une autre : ce
// qu'elle montre, voile, et ou aller. Lot API 8 : `where` - "la fenetre
// principale", ou "la fenetre << API - Variables >>" (la principale aussi
// attend, quand la question est dans une fenetre detachee).
void paintWaiting(gfx::IRenderer& r, const ui::Theme& theme, gfx::Size size, const std::string& where) {
    const auto& c = theme.color;
    r.fillRect({0.f, 0.f, size.w, size.h}, gfx::Color{0, 0, 0, 110});
    std::string line1 = "Une question t'attend dans " + where + ".";
    const std::string_view line2 = "Clique ici pour y aller.";
    const gfx::FontId bold = theme.font.uiBold, plain = theme.font.ui;
    // Un titre d'onglet long : coupe, avec des points de suspension.
    if (const float room = size.w - 32.f - 48.f; room > 40.f && r.measure(line1, bold).width > room) {
        constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
        line1 = line1.substr(0, r.fitCharacters(line1, bold, std::max(0.f, room - r.measure(kEllipsis, bold).width)));
        line1 += kEllipsis;
    }
    const float w = std::min(size.w - 32.f, std::max(r.measure(line1, bold).width, r.measure(line2, plain).width) + 48.f);
    const float lh = r.lineHeight(bold);
    const float h = lh * 2.f + 40.f;
    if (w < 40.f || h > size.h) return;
    const gfx::Rect card{std::round((size.w - w) * 0.5f), std::round((size.h - h) * 0.5f), w, h};
    r.fillRect({card.x + 3.f, card.y + 3.f, card.w, card.h}, gfx::Color{0, 0, 0, 70});
    r.fillRoundedRect(card, c.panelBg, theme.metric.radius * 2.f);
    r.strokeRect(card, c.borderStrong, 1.f);
    r.drawText({card.x + 24.f, card.y + 16.f}, line1, bold, c.text);
    r.drawText({card.x + 24.f, card.y + 20.f + lh}, line2, plain, c.textMuted);
}
#endif

} // namespace

// =============================================================================
//  L'etat
// =============================================================================
struct DetachedWindows::Window {
    std::string                         title;
    std::function<void(ui::WidgetPtr)>  giveBack;
    // L'ecran qui avait la page (son id) : quand il quitte la pile, elle rentre
    // avant qu'il soit detruit.
    std::string                         owner;
    const ui::Widget*                   page{nullptr};
    DetachedRoot*                       root{nullptr};       // la racine de `host`
    SDL_Window*                         window{nullptr};
    std::uint32_t                       id{0};
    std::string                         shownTitle;          // le titre pose sur la fenetre
    std::vector<std::string>            captures;            // F12 : au prochain dessin
    bool                                returning{false};    // a rendre des qu'on peut
    bool                                orphan{false};       // proprietaire parti : a fermer sans le rappeler
    bool                                asleep{false};       // lot API 8 : une question attend (la page ne survole plus rien)
    core::Connection                    backClicked;
    // Dans cet ordre : l'arbre (et la page) part avant le cache de ses images,
    // le cache avant le renderer qui porte les textures.
    std::unique_ptr<gfx::IRenderer>     renderer;
    HmiImageCache                       images;
    std::unique_ptr<ui::WidgetHost>     host;
};

struct DetachedWindows::Impl {
    explicit Impl(App& a) : app(a) {}

    App&                                  app;
    SDL_Window*                           mainWindow{nullptr};
    std::vector<std::unique_ptr<Window>>  windows;
    // Dans un evenement ou un dessin d'une fenetre detachee : ce qui detruirait
    // son arbre attend la sortie (settle).
    int                                   busy{0};
    double                                seconds{0.0};
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    domain::ProjectIcon                   icon;                // celle posee sur les fenetres
    bool                                  iconSet{false};
    std::vector<hmi::Rgba>                brandIcons;          // le logo, rasterise une fois
    core::Connection                      menuExited;
    core::Connection                      projectClosed;
    // Lot API 8 : le temps depuis l'image d'avant (la mise a jour des
    // dialogues d'une fenetre), la taille des fenetres donnee au MenuManager,
    // et la fin (~DetachedWindows : la pile n'est peut-etre plus la).
    double                                delta{0.0};
    bool                                  surfacesGiven{false};
    bool                                  shuttingDown{false};

    // Lot API 8 : UNE QUESTION ATTEND (un dialogue, un modal : il bloque tout),
    // et dans quelle fenetre - route d'un evenement de la fenetre `id` : a ce
    // qu'elle montre (rien n'attend), a la pile (la question est chez elle), a
    // personne (elle est ailleurs).
    [[nodiscard]] menu::MenuManager::Route route(std::uint32_t id) const { return app.menus().routeFor(id); }
    // Ou elle attend, pour le dire : "la fenetre principale", ou celle de l'onglet.
    [[nodiscard]] std::string questionPlace() const {
        const auto id = app.menus().questionWindow();
        for (const auto& w : windows)
            if (id != menu::MenuManager::kMainWindow && w->id == id) return "la fen\xC3\xAAtre \xC2\xAB " + w->title + " \xC2\xBB";
        return "la fen\xC3\xAAtre principale";
    }

    // L'ecran qui detache : celui du dessus, ou - un dialogue par-dessus -
    // celui d'en dessous.
    [[nodiscard]] std::string ownerId() const {
        const menu::IMenu* top = app.menus().top();
        if (!top) return {};
        if (top->traits().kind == menu::MenuKind::Screen) return top->id();
        const auto path = app.menus().path();
        return path.size() >= 2 ? path[path.size() - 2] : top->id();
    }

    [[nodiscard]] std::string windowTitle(const std::string& tab) const {
        std::string after = kAppTitle;
#if XPG_HAVE_SDL3
        // Celui de la fenetre principale : "<projet> * - PLC Project Analyzer".
        if (mainWindow)
            if (const char* t = SDL_GetWindowTitle(mainWindow); t && *t) after = t;
#endif
        return tab.empty() ? after : tab + " \xE2\x80\x94 " + after;
    }

    void applyIcon(SDL_Window* win) {
#if XPG_HAVE_SDL3
        if (!win) return;
        if (!icon.empty()) {
            // Celle du projet, comme la fenetre principale (App::refreshWindowIcon).
            auto px = project::icon::rgba(icon);
            if (px.size() == 32u * 32u * 4u)
                if (SDL_Surface* s = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32, px.data(), 32 * 4)) {
                    (void)SDL_SetWindowIcon(win, s);
                    SDL_DestroySurface(s);
                    return;
                }
        }
        // Le logo a plusieurs tailles : le systeme prend la plus proche de ce
        // qu'il affiche (App::initPlatform fait de meme).
        if (brandIcons.empty())
            for (const int side : {256, 16, 24, 32, 48, 64, 128}) {
                hmi::Rgba img;
                if (brand::logoPixels(side, img)) brandIcons.push_back(std::move(img));
            }
        if (brandIcons.empty()) return;
        auto& big = brandIcons.front();
        if (SDL_Surface* s = SDL_CreateSurfaceFrom(big.width, big.height, SDL_PIXELFORMAT_RGBA32, big.pixels.data(), big.width * 4)) {
            for (std::size_t i = 1; i < brandIcons.size(); ++i) {
                auto& img = brandIcons[i];
                if (SDL_Surface* alt = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32, img.pixels.data(), img.width * 4)) {
                    (void)SDL_AddSurfaceAlternateImage(s, alt);
                    SDL_DestroySurface(alt);
                }
            }
            (void)SDL_SetWindowIcon(win, s);
            SDL_DestroySurface(s);
        }
#else
        (void)win;
#endif
    }

    void raiseMain() const {
#if XPG_HAVE_SDL3
        if (!mainWindow) return;
        if ((SDL_GetWindowFlags(mainWindow) & SDL_WINDOW_MINIMIZED) != 0) (void)SDL_RestoreWindow(mainWindow);
        (void)SDL_RaiseWindow(mainWindow);
#endif
    }

    // La fenetre, son renderer : detruits dans l'ordre (le renderer d'abord).
    // Apres SDL_Quit (ne devrait pas arriver : ~App rend tout avant), SDL les a
    // deja emportes - on ne les touche plus.
    static void destroy(Window& w) {
        w.backClicked.reset();
        w.host.reset();
#if XPG_HAVE_SDL3
        if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
            (void)w.renderer.release();
            w.window = nullptr;
            return;
        }
        w.renderer.reset();
        if (w.window) {
            (void)SDL_StopTextInput(w.window);
            SDL_DestroyWindow(w.window);
        }
#else
        w.renderer.reset();
#endif
        w.window = nullptr;
    }
};

// =============================================================================
//  Construction, fin
// =============================================================================
DetachedWindows::DetachedWindows(App& app) : impl_(std::make_unique<Impl>(app)) {
    // Le projet se ferme : les pages rentrent pendant que l'ecran qui les attend
    // est encore la (il part a l'application suivante de la pile).
    impl_->projectClosed = app.events().subscribe<ProjectClosed>([this](const ProjectClosed&) { giveBackAll(); });
}

DetachedWindows::~DetachedWindows() {
    // ~App les a deja rendues (avant de detruire les ecrans) ; ce qui resterait
    // n'a plus de proprietaire a qui revenir.
    impl_->busy = 0;
    impl_->shuttingDown = true;     // lot API 8 : la pile est deja partie (~App)
    while (!impl_->windows.empty()) returnWindow(impl_->windows.size() - 1, false);
}

void DetachedWindows::setMainWindow(SDL_Window* window) noexcept { impl_->mainWindow = window; }

// =============================================================================
//  Detacher, rendre
// =============================================================================
bool DetachedWindows::detach(const std::string& title, ui::WidgetPtr& page, std::function<void(ui::WidgetPtr)> onReturn,
                             int w, int h) {
#if XPG_HAVE_SDL3
    // Une page encore dans un arbre (son parent la possede aussi) : refusee.
    if (!page || page->parent() || isDetached(page.get())) return false;
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return false;
    // Le poste d'exploitation montre l'IHM seule.
    if (impl_->app.stationActive()) return false;

    // LA TAILLE : celle que la page avait, plus la bande - en pixels de la
    // fenetre principale, ramenes aux unites de fenetre (un ecran haute densite
    // en compte deux par unite ailleurs que sous Windows).
    float density = 1.f;
    if (impl_->mainWindow)
        if (const float d = SDL_GetWindowPixelDensity(impl_->mainWindow); d > 0.f) density = d;
    const gfx::Rect had = page->bounds();
    int width = w > 0 ? w : had.w >= 1.f ? static_cast<int>(std::lround(had.w / density)) : 1000;
    int height = h > 0 ? h : had.h >= 1.f ? static_cast<int>(std::lround((had.h + kBand) / density)) : 720;

    // L'ECRAN : le deuxieme s'il y en a un (celui ou la fenetre principale n'est
    // pas), sinon le sien.
    SDL_DisplayID home = impl_->mainWindow ? SDL_GetDisplayForWindow(impl_->mainWindow) : 0;
    if (home == 0) home = SDL_GetPrimaryDisplay();
    SDL_DisplayID target = home;
    int displays = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&displays)) {
        for (int i = 0; i < displays; ++i)
            if (ids[i] != home) { target = ids[i]; break; }
        SDL_free(ids);
    }
    SDL_Rect area{0, 0, 1280, 800};
    if (target != 0 && !SDL_GetDisplayUsableBounds(target, &area)) (void)SDL_GetDisplayBounds(target, &area);
    // Une page qui vivait dans une petite tuile (une mosaique, un groupe) ne
    // part pas dans une fenetre aussi petite : 1000 x 700 au moins, si l'ecran
    // le permet (les trois quarts de l'ecran au plus pour ce minimum).
    if (w <= 0) width = std::max(width, std::min(1000, area.w * 3 / 4));
    if (h <= 0) height = std::max(height, std::min(700, area.h * 3 / 4));
    width = std::clamp(width, 360, std::max(360, area.w - 24));
    height = std::clamp(height, 240, std::max(240, area.h - 48));
    // En cascade quand il y en a deja : chacune se voit.
    const int shift = 32 * static_cast<int>(impl_->windows.size() % 8);
    int x = 0, y = 0;
    if (target != home) {
        x = area.x + (area.w - width) / 2 + shift;
        y = area.y + (area.h - height) / 2 + shift;
    } else {
        // Un seul ecran : decalee de la fenetre principale, pour qu'on voie
        // que c'en est une autre.
        int mx = area.x, my = area.y;
        if (impl_->mainWindow) (void)SDL_GetWindowPosition(impl_->mainWindow, &mx, &my);
        x = mx + 48 + shift;
        y = my + 48 + shift;
    }
    x = std::clamp(x, area.x, std::max(area.x, area.x + area.w - width));
    y = std::clamp(y, area.y, std::max(area.y, area.y + area.h - height));

    const std::string shown = impl_->windowTitle(title);
    SDL_Window* win = SDL_CreateWindow(shown.c_str(), width, height,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!win) {
        std::fprintf(stderr, "onglet d\xC3\xA9tach\xC3\xA9 : fen\xC3\xAAtre impossible (%s)\n", SDL_GetError());
        return false;
    }
    (void)SDL_SetWindowPosition(win, x, y);
    (void)SDL_SetWindowMinimumSize(win, 360, 240);
    // L'icone du moment (celle du projet, sinon le logo).
    if (!impl_->iconSet) {
        auto doc = impl_->app.document();
        impl_->icon = doc ? doc->icon : domain::ProjectIcon{};
        impl_->iconSet = true;
    }
    impl_->applyIcon(win);
    auto created = gfx::SdlRenderer::create(win);
    if (!created) {
        std::fprintf(stderr, "onglet d\xC3\xA9tach\xC3\xA9 : rendu impossible (%s)\n", created.error().message().c_str());
        SDL_DestroyWindow(win);
        return false;
    }
    // La fenetre principale donne le rythme (voir en tete).
    if (SDL_Renderer* sdl = SDL_GetRenderer(win)) (void)SDL_SetRenderVSync(sdl, 0);

    // A partir d'ici, plus d'echec : la page change de mains.
    auto slot = std::make_unique<Window>();
    slot->title = title;
    slot->giveBack = std::move(onReturn);
    slot->owner = impl_->ownerId();
    slot->window = win;
    slot->id = SDL_GetWindowID(win);
    slot->shownTitle = shown;
    slot->renderer = std::move(*created);
    const ui::Widget* raw = page.get();
    slot->page = raw;
    closeOverlays(*page);
    ui::WidgetHost::release(*page);         // sans le survol ni le focus de la fenetre principale
    auto root = std::make_unique<DetachedRoot>(title, std::move(page));
    slot->root = root.get();
    slot->backClicked = root->backButton().clicked->connect([this, raw] { (void)giveBack(raw); });
    slot->host = std::make_unique<ui::WidgetHost>(std::move(root));
    impl_->windows.push_back(std::move(slot));

    // Les pages rentrent avant que leur ecran soit detruit (il quitte la pile :
    // un autre projet, l'accueil, la sortie).
    if (!impl_->menuExited.connected())
        impl_->menuExited = impl_->app.menus().menuExited->connect([this](const menu::MenuId& id) { ownerLeaving(id); });
    // ---- Lot API 8 : la pile demande la taille de nos fenetres (un dialogue s'y
    // centre et s'y met en page) ; une fenetre qu'on ne connait plus lui rend
    // ses dialogues (MenuManager::adoptLostWindows).
    if (!impl_->surfacesGiven) {
        impl_->surfacesGiven = true;
        impl_->app.menus().setWindowSurfaces([this](menu::MenuManager::WindowId id) -> std::optional<gfx::Size> {
            const Window* found = byId(id);
            if (!found || !found->renderer) return std::nullopt;
            return found->renderer->surfaceSize();
        });
    }

    (void)SDL_ShowWindow(win);
    // SDL3 demande la saisie de texte fenetre par fenetre : sans elle, aucun
    // champ de la page ne recevrait de caracteres.
    (void)SDL_StartTextInput(win);
    (void)SDL_RaiseWindow(win);
    changed->emit();
    return true;
#else
    (void)title; (void)page; (void)onReturn; (void)w; (void)h;
    return false;
#endif
}

DetachedWindows::Window* DetachedWindows::byPage(const ui::Widget* page) const {
    if (!page) return nullptr;
    for (const auto& w : impl_->windows)
        if (w->page == page) return w.get();
    return nullptr;
}

DetachedWindows::Window* DetachedWindows::byId(std::uint32_t windowId) const {
    if (windowId == 0) return nullptr;
    for (const auto& w : impl_->windows)
        if (w->id == windowId) return w.get();
    return nullptr;
}

bool DetachedWindows::isDetached(const ui::Widget* page) const {
    const Window* w = byPage(page);
    return w && !w->orphan;
}

void DetachedWindows::raise(const ui::Widget* page) {
#if XPG_HAVE_SDL3
    Window* w = byPage(page);
    if (!w || !w->window) return;
    if ((SDL_GetWindowFlags(w->window) & SDL_WINDOW_MINIMIZED) != 0) (void)SDL_RestoreWindow(w->window);
    (void)SDL_RaiseWindow(w->window);
#else
    (void)page;
#endif
}

bool DetachedWindows::giveBack(const ui::Widget* page) {
    Window* w = byPage(page);
    if (!w || w->orphan) return false;
    w->returning = true;
    settle();
    return true;
}

void DetachedWindows::giveBackAll() {
    for (auto& w : impl_->windows)
        if (!w->orphan) w->returning = true;
    settle();
}

std::size_t DetachedWindows::count() const noexcept { return impl_->windows.size(); }

std::vector<const ui::Widget*> DetachedWindows::pages() const {
    std::vector<const ui::Widget*> out;
    out.reserve(impl_->windows.size());
    for (const auto& w : impl_->windows) out.push_back(w->page);
    return out;
}

void DetachedWindows::settle() {
    if (impl_->busy > 0) return;
    // Une recherche a chaque tour : un rappel de proprietaire peut detacher
    // ou rendre une autre page, et changer la liste sous nos pieds.
    for (;;) {
        auto& list = impl_->windows;
        const auto it = std::find_if(list.begin(), list.end(),
                                     [](const std::unique_ptr<Window>& w) { return w->returning || w->orphan; });
        if (it == list.end()) break;
        const bool callOwner = !(*it)->orphan;
        returnWindow(static_cast<std::size_t>(it - list.begin()), callOwner);
    }
}

void DetachedWindows::returnWindow(std::size_t index, bool callOwner) {
    auto& list = impl_->windows;
    if (index >= list.size()) return;
    std::unique_ptr<Window> w = std::move(list[index]);
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
    // ---- Lot API 8 : un dialogue ouvert dans cette fenetre revient dans la
    // principale, intact (remis en page a sa taille), et elle se montre.
    if (!impl_->shuttingDown && w->id != 0
        && impl_->app.menus().moveWindow(w->id, menu::MenuManager::kMainWindow) > 0)
        impl_->raiseMain();
    ui::WidgetPtr page = w->root ? w->root->takePage() : nullptr;
    auto owner = std::move(w->giveBack);
    // La fenetre part AVANT le rappel : la page revient dans un onglet qui se
    // dessine des la prochaine image, avec le renderer principal.
    Impl::destroy(*w);
    w.reset();
    if (callOwner && owner && page) owner(std::move(page));
    // Sinon la page meurt ici : son proprietaire n'est plus, ou n'en voulait pas.
    page.reset();
    changed->emit();
}

void DetachedWindows::ownerLeaving(const std::string& ownerId) {
    bool any = false;
    for (auto& w : impl_->windows) {
        if (w->owner.empty() || w->owner != ownerId || w->orphan) continue;
        any = true;
        // Au milieu d'un evenement de la fenetre, la page ne peut pas etre
        // rendue (on parcourt son arbre) ; son proprietaire, lui, va etre
        // detruit juste apres ce signal : la rappeler plus tard serait appeler
        // un mort. Elle sera fermee, sans rappel, a la sortie de l'evenement.
        if (impl_->busy > 0) w->orphan = true;
        else w->returning = true;
    }
    if (any) settle();
}

// =============================================================================
//  Les evenements
// =============================================================================
bool DetachedWindows::ownsWindow(std::uint32_t windowId) const { return byId(windowId) != nullptr; }

bool DetachedWindows::routeEvent(std::uint32_t windowId, const ui::InputEvent& e) {
    Window* w = byId(windowId);
    if (!w) return false;
    deliver(*w, e);
    return true;
}

bool DetachedWindows::closeRequested(std::uint32_t windowId) {
    Window* w = byId(windowId);
    if (!w) return false;
    if (!w->orphan) w->returning = true;
    settle();
    return true;
}

void DetachedWindows::mouseLeft(std::uint32_t windowId) {
    if (Window* w = byId(windowId); w && w->host) w->host->pointerLeft();
}

bool DetachedWindows::sendEvent(const ui::Widget* page, const ui::InputEvent& e) {
    Window* w = byPage(page);
    if (!w) return false;
    deliver(*w, e);
    return true;
}

void DetachedWindows::deliver(Window& w, const ui::InputEvent& e) {
    if (w.returning || w.orphan || !w.host) return;
    ++impl_->busy;
    {
        const gfx::Size size = w.renderer ? w.renderer->surfaceSize() : gfx::Size{};
        const ui::SurfaceScope surface(size);
        // ---- Lot API 8 : le temps de cet evenement, cette fenetre est "en
        // cours" : un dialogue qu'il demande (un outil, un bouton, une touche,
        // un menu de la page) s'ouvre ICI.
        auto& menus = impl_->app.menus();
        const menu::MenuManager::WindowScope here(menus, w.id);
        const auto route = impl_->route(w.id);
        const auto* key = std::get_if<ui::KeyDown>(&e);
        if (route == menu::MenuManager::Route::Elsewhere) {
            // Une question attend ailleurs (la fenetre principale, une autre
            // fenetre detachee) : la page ne prend rien, un clic ou une touche
            // montre ou elle est (repondre a l'aveugle a un dialogue qu'on ne
            // voit pas serait pire).
            if (std::holds_alternative<ui::MouseDown>(e) || (key && !key->repeat)) raiseWindow(menus.questionWindow());
        } else if (key && key->mods.none() && !key->repeat && key->key == ui::Key::F12) {
            // F12 : une capture de CETTE fenetre, dans captures/ (au prochain dessin).
            w.captures.push_back(defaultCapturePath());
        } else if (key && key->mods.none() && !key->repeat && key->key == ui::Key::F11 && !SimDebugPane::claimsKey(ui::Key::F11)
                   && !HmiSimulationPane::claimsKey(ui::Key::F11, w.root)) {   // 1.10 : l'onglet IHM de cette fenetre prend F11
#if XPG_HAVE_SDL3
            // F11 : le plein ecran de cette fenetre (et retour), comme la principale.
            if (w.window) (void)SDL_SetWindowFullscreen(w.window, (SDL_GetWindowFlags(w.window) & SDL_WINDOW_FULLSCREEN) == 0);
#endif
        } else if (route == menu::MenuManager::Route::Layers) {
            // Lot API 8 : le dialogue de CETTE fenetre prend son clavier et sa
            // souris (Echap l'annule, Tab promene son focus) ; la page attend.
            (void)menus.HandleEvent(e);
        } else if (const auto* drop = std::get_if<ui::FileDropped>(&e)) {
            // ---- Lot API 8 : glisser n'importe quel fichier ----
            // Le volet sous la souris le garde (Ressources, Fichiers externes, le
            // champ d'une macro) ; sinon, la question du depot, dans la principale.
            // 1.11.2 (decision 188) : un paquet (.xpgvues, .xpgsymboles...) va a la fenetre d'import, pas au volet.
            if (HmiImportDialog::isPackageFile(drop->path) || w.host->dispatch(e) == ui::EventResult::Ignored)
                (void)impl_->app.queueDroppedFile(drop->path);
            // ---- fin Lot API 8 : glisser n'importe quel fichier ----
        } else if (w.host->dispatch(e) == ui::EventResult::Ignored && key && key->mods.ctrl && !key->mods.alt) {
            // Les raccourcis de l'appli que la page n'a pas pris - les memes que
            // l'ecran principal apres ses widgets (MainAnalysisScreen) : un champ
            // en saisie garde son Ctrl+Z.
            auto& app = impl_->app;
            if (!key->repeat || key->key == ui::Key::Z || key->key == ui::Key::Y) {
                switch (key->key) {
                    case ui::Key::Z:
                        if (key->mods.shift) app.redo();
                        else app.undo();
                        break;
                    case ui::Key::Y: app.redo(); break;
                    case ui::Key::S: app.saveFromKeyboard(); break;
                    case ui::Key::H: app.events().publish(HistoryToggle{}); break;
                    // ---- Lot API 8 : finitions (Ctrl+F dans les volets) ----
                    case ui::Key::F:
                        if (ui::Widget* page = w.root ? w.root->page() : nullptr; !key->mods.shift && page)
                            (void)focusSearchField(*page);
                        break;
                    // ---- fin Lot API 8 : finitions ----
                    default: break;
                }
            }
        }
    }
    --impl_->busy;
    settle();
}

// =============================================================================
//  Le dessin
// =============================================================================
void DetachedWindows::frame() {
    frame(std::chrono::duration<double>(std::chrono::steady_clock::now() - impl_->start).count());
}

void DetachedWindows::frame(double totalSeconds) {
    impl_->delta = std::max(0.0, totalSeconds - impl_->seconds);     // lot API 8 : pour ses dialogues
    impl_->seconds = totalSeconds;
    settle();
    if (impl_->windows.empty()) return;
    // Le poste d'exploitation montre l'IHM seule, en plein ecran : les onglets
    // de conception n'y restent pas ouverts a cote.
    if (impl_->app.stationActive()) {
        giveBackAll();
        return;
    }
    refreshDecorations();
    for (std::size_t i = 0; i < impl_->windows.size(); ++i) (void)renderWindow(*impl_->windows[i], nullptr, nullptr);
    settle();
}

void DetachedWindows::refreshDecorations() {
#if XPG_HAVE_SDL3
    for (auto& w : impl_->windows) {
        if (!w->window) continue;
        const std::string want = impl_->windowTitle(w->title);
        if (want == w->shownTitle) continue;
        w->shownTitle = want;
        (void)SDL_SetWindowTitle(w->window, want.c_str());
    }
    auto doc = impl_->app.document();
    const domain::ProjectIcon none;
    const auto& want = doc ? doc->icon : none;
    if (impl_->iconSet && want == impl_->icon) return;
    impl_->icon = want;
    impl_->iconSet = true;
    for (auto& w : impl_->windows) impl_->applyIcon(w->window);
#endif
}

bool DetachedWindows::renderWindow(Window& w, const std::string* capture, std::string* error) {
#if XPG_HAVE_SDL3
    const auto failed = [error](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (!w.renderer || !w.window || !w.host || w.orphan) return failed("fen\xC3\xAAtre ferm\xC3\xA9" "e");
    const SDL_WindowFlags flags = SDL_GetWindowFlags(w.window);
    const bool hidden = (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
    if (hidden) return capture ? failed("fen\xC3\xAAtre r\xC3\xA9" "duite") : true;
    // Cachee par d'autres : rien a montrer, sauf une capture demandee.
    if (!capture && w.captures.empty() && (flags & SDL_WINDOW_OCCLUDED) != 0) return true;
    auto& r = *w.renderer;
    const gfx::Size size = r.surfaceSize();
    if (size.w < 1.f || size.h < 1.f) return capture ? failed("fen\xC3\xAAtre vide") : true;
    const ui::Theme& theme = impl_->app.theme();
    // Lot API 8 : une question attend-elle, et ou (ici : ses dialogues par-dessus
    // la page ; ailleurs : le voile, et ou aller).
    const auto route = impl_->route(w.id);
    // Une question arrive (ici ou ailleurs) : ce que la souris survolait dans la
    // page ne reste pas surligne sous le voile ; elle reprend a son prochain mouvement.
    if (route != menu::MenuManager::Route::Content && !w.asleep) w.host->pointerLeft();
    w.asleep = route != menu::MenuManager::Route::Content;
    bool ok = true;
    ++impl_->busy;
    {
        const ui::SurfaceScope surface(size);
        const HmiImageCache::Scope images(w.images);
        w.host->layout(size);
        r.beginFrame(theme.color.windowBg);
        // Les infobulles de la page seulement quand rien n'attend : sous un
        // dialogue, la souris est a lui.
        w.host->paint(r, theme, size, impl_->seconds, route == menu::MenuManager::Route::Content);
        // ---- Lot API 8 : les dialogues de cette fenetre, par-dessus sa page,
        // chacun avec son voile, a la taille de SA surface.
        const menu::FrameContext fc{impl_->delta, impl_->seconds, &theme, size, r.dpiScale()};
        impl_->app.menus().RenderWindow(w.id, r, fc);
        if (route == menu::MenuManager::Route::Elsewhere) paintWaiting(r, theme, size, impl_->questionPlace());
        // Ce qui vient d'etre dessine, avant de le presenter : apres, le contenu
        // du tampon n'est plus garanti.
        for (const auto& path : w.captures) {
            if (auto res = captureToPng(r, path); res) std::fprintf(stderr, "capture : %s\n", path.c_str());
            else std::fprintf(stderr, "capture impossible : %s\n", res.error().message().c_str());
        }
        w.captures.clear();
        if (capture) {
            if (auto res = captureToPng(r, *capture); !res) ok = failed(res.error().message());
        }
        r.endFrame();
    }
    --impl_->busy;
    return ok;
#else
    (void)w; (void)capture;
    if (error) *error = "pas de fen\xC3\xAAtre (construit sans SDL)";
    return false;
#endif
}

bool DetachedWindows::capturePng(const ui::Widget* page, const std::string& path, std::string* error) {
    Window* w = byPage(page);
    if (!w) {
        if (error) *error = "cet onglet n'est pas d\xC3\xA9tach\xC3\xA9";
        return false;
    }
    const bool ok = renderWindow(*w, &path, error);
    settle();
    return ok;
}

// =============================================================================
//  Les titres
// =============================================================================
std::string DetachedWindows::titleOf(const ui::Widget* page) const {
    const Window* w = byPage(page);
    return w ? w->title : std::string{};
}

void DetachedWindows::setTitle(const ui::Widget* page, std::string title) {
    Window* w = byPage(page);
    if (!w || w->title == title) return;
    w->title = std::move(title);
    if (w->root) w->root->setTitle(w->title);
    refreshDecorations();
    changed->emit();
}

std::vector<std::string> DetachedWindows::titles() const {
    std::vector<std::string> out;
    out.reserve(impl_->windows.size());
    for (const auto& w : impl_->windows) out.push_back(w->title);
    return out;
}

const ui::Widget* DetachedWindows::findByTitle(std::string_view title) const {
    for (const auto& w : impl_->windows)
        if (!w->orphan && w->title == title) return w->page;
    const std::string wanted = lowerAscii(title);
    for (const auto& w : impl_->windows)
        if (!w->orphan && lowerAscii(w->title) == wanted) return w->page;
    if (wanted.empty()) return nullptr;
    for (const auto& w : impl_->windows)
        if (!w->orphan && lowerAscii(w->title).rfind(wanted, 0) == 0) return w->page;
    return nullptr;
}

// =============================================================================
//  Lot API 8 : les dialogues dans la fenetre detachee
// =============================================================================
std::uint32_t DetachedWindows::windowIdOf(const ui::Widget* page) const {
    const Window* w = byPage(page);
    return w ? w->id : 0;
}

std::string DetachedWindows::titleOfWindow(std::uint32_t windowId) const {
    const Window* w = byId(windowId);
    return w ? w->title : std::string{};
}

ui::Widget* DetachedWindows::pageByTitle(std::string_view title) const {
    const Window* w = byPage(findByTitle(title));
    return w && w->root ? w->root->page() : nullptr;
}

void DetachedWindows::raiseWindow(std::uint32_t windowId) {
    if (windowId == menu::MenuManager::kMainWindow) {
        impl_->raiseMain();
        return;
    }
#if XPG_HAVE_SDL3
    const Window* w = byId(windowId);
    if (!w || !w->window) return;
    if ((SDL_GetWindowFlags(w->window) & SDL_WINDOW_MINIMIZED) != 0) (void)SDL_RestoreWindow(w->window);
    (void)SDL_RaiseWindow(w->window);
#endif
}

bool DetachedWindows::holdMainEvent(const ui::InputEvent& e) {
    // La question attend dans une fenetre detachee : la principale est comme une
    // fenetre detachee quand elle attend chez la principale (deliver).
    if (impl_->route(menu::MenuManager::kMainWindow) != menu::MenuManager::Route::Elsewhere) return false;
    const auto* key = std::get_if<ui::KeyDown>(&e);
    if (std::holds_alternative<ui::MouseDown>(e) || (key && !key->repeat))
        raiseWindow(impl_->app.menus().questionWindow());
    return true;
}

void DetachedWindows::paintOverMain(gfx::IRenderer& r, gfx::Size surface) {
#if XPG_HAVE_SDL3
    if (surface.w < 1.f || surface.h < 1.f) return;
    if (impl_->route(menu::MenuManager::kMainWindow) != menu::MenuManager::Route::Elsewhere) return;
    paintWaiting(r, impl_->app.theme(), surface, impl_->questionPlace());
#else
    (void)r; (void)surface;
#endif
}

SDL_Window* DetachedWindows::systemWindow(std::uint32_t windowId) const {
    const Window* w = byId(windowId);
    return w ? w->window : nullptr;
}

} // namespace app
