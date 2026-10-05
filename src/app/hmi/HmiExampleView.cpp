#include "HmiExampleView.hpp"

#include "HmiIcons.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace {
constexpr double kTutorialBase = 1000.0;     // lot 16 : l'heure du moteur au debut du tour du tutoriel (comme freezeAt)
constexpr float kCaptionH = 28.f;
constexpr double kMove = 0.7;      // le pointeur part vers sa cible 0,7 s avant le geste
constexpr double kArrive = 0.12;   // et y arrive un peu avant
bool pointed(const std::string& op) {
    return op == "clic" || op == "partie" || op == "clavier" || op == "appui" || op == "relache" || op == "tirer" || op == "lacher"
        || op == "menu";
}
// Lot 9 : un glisser - le pointeur suit la poignee, sans elan.
bool dragged(const std::string& op) { return op == "tirer" || op == "lacher"; }
// L'onde d'un clic : pas au relacher, ni pendant un glisser.
bool rippled(const std::string& op) { return op == "clic" || op == "partie" || op == "clavier" || op == "appui" || op == "menu"; }
} // namespace

HmiExampleView::HmiExampleView(std::string id) : ui::Widget(std::move(id)) {
    auto canvas = std::make_unique<HmiLiveCanvas>(this->id() + ".canvas");
    canvas_ = &static_cast<HmiLiveCanvas&>(addChild(std::move(canvas)));
    // Lot 16 : "A toi" - les gestes vont au moteur de l'exemple, a son heure.
    const auto live = [this] { return interactive_ && runtime_ != nullptr; };
    links_ += canvas_->pressed->connect([this, live](hmi::Id o) { if (live()) { runtime_->press(o, clock_); showNow(); } });
    links_ += canvas_->released->connect([this, live](hmi::Id o, bool inside) { if (live()) { runtime_->release(o, clock_, inside); showNow(); } });
    links_ += canvas_->doubleClicked->connect([this, live](hmi::Id o) { if (live()) { runtime_->doubleClick(o, clock_); showNow(); } });
    links_ += canvas_->partClicked->connect([this, live](hmi::Id o, const std::string& part) {
        if (live()) { runtime_->objectPart(o, part, clock_); showNow(); }
    });
    links_ += canvas_->valueDragged->connect([this, live](hmi::Id o, double fraction, bool commit) {
        if (live()) { runtime_->dragValue(o, fraction, commit, clock_); showNow(); }
    });
    links_ += canvas_->textTyped->connect([this, live](const std::string& text) { if (live()) { runtime_->typeText(text, clock_); showNow(); } });
    links_ += canvas_->keyTyped->connect([this, live](int key) { if (live()) { runtime_->typeKey(static_cast<hmi::EditKey>(key), clock_); showNow(); } });
    links_ += canvas_->clickedAway->connect([this, live] { if (live()) { runtime_->unfocus(clock_); showNow(); } });
    links_ += canvas_->loginPartClicked->connect([this, live](const std::string& part) { if (live()) { runtime_->loginPart(part, clock_); showNow(); } });
    links_ += canvas_->systemPartClicked->connect([this, live](const std::string& part) { if (live()) { runtime_->systemPart(part, clock_); showNow(); } });
    links_ += canvas_->signaturePartClicked->connect([this, live](const std::string& part) {
        if (live()) { runtime_->signaturePart(part, clock_); showNow(); }
    });
    links_ += canvas_->popupCloseRequested->connect([this, live](int slot, bool instant) {
        if (!live()) return;
        hmi::Transition t;
        t.kind = instant ? hmi::TransitionKind::Instant : hmi::TransitionKind::Fade;
        t.durationMs = instant ? 0 : 200;
        (void)runtime_->closePopupAt(static_cast<std::size_t>(std::max(0, slot)), t, clock_);
        showNow();
    });
}

HmiExampleView::~HmiExampleView() = default;

void HmiExampleView::setKind(std::optional<hmi::Kind> kind) {
    if (kind == kind_ && (example_.has_value() || !kind)) return;
    kind_ = kind;
    example_.reset();
    runtime_.reset();
    canvas_->setLive(nullptr, nullptr);
    canvas_->setProject(nullptr);
    canvas_->setAssets(nullptr);
    canvas_->showLayers({}, false);
    origin_ = -1;
    played_ = 0;
    interactive_ = false;
    if (kind_) example_ = hmi::examples::exampleFor(*kind_);   // l'exemple existe-t-il ?
    invalidate();
}

// ---- lot 16 : le tutoriel pilote l'horloge ------------------------------------------------
void HmiExampleView::setManual(bool on) {
    if (manual_ == on) return;
    manual_ = on;
    interactive_ = false;
    playing_ = on ? false : true;
    lastPaint_ = -1;
    frozen_ = false;
    origin_ = -1;
    if (on && kind_) {
        start(kTutorialBase);
        showNow();
    }
    invalidate();
}

void HmiExampleView::setPlaying(bool on) {
    playing_ = on;
    lastPaint_ = -1;
    invalidate();
}

void HmiExampleView::setSpeed(double factor) {
    speed_ = std::clamp(factor, 0.25, 4.0);
    invalidate();
}

void HmiExampleView::setInteractive(bool on) {
    if (!manual_) setManual(true);
    interactive_ = on;
    playing_ = on ? true : playing_;
    lastPaint_ = -1;
    showNow();
    invalidate();
}

void HmiExampleView::seek(double seconds) {
    if (!kind_) return;
    if (!manual_) setManual(true);
    if (!runtime_ || !example_) start(kTutorialBase);
    if (!example_) return;
    interactive_ = false;
    const double target = std::clamp(seconds, 0.0, example_->period);
    // Reculer : le tour rejoue depuis le debut, a l'identique.
    if (target < played_ - 1e-9) start(kTutorialBase);
    manualTo(target);
    showNow();
    invalidate();
}

void HmiExampleView::manualTo(double seconds) {
    if (!example_ || !runtime_) return;
    for (double t = played_ + 0.05; t < seconds - 1e-9; t += 0.05) {
        const double now = kTutorialBase + t;
        runtime_->tick(now);
        if (!interactive_) (void)hmi::examples::play(*example_, *runtime_, played_, t, now);
        runtime_->tick(now);
        played_ = t;
        clock_ = now;
    }
    const double now = kTutorialBase + seconds;
    runtime_->tick(now);
    if (!interactive_) (void)hmi::examples::play(*example_, *runtime_, played_, seconds, now);
    runtime_->tick(now);
    played_ = seconds;
    clock_ = now;
}

void HmiExampleView::showNow() {
    if (!example_ || !runtime_) return;
    canvas_->showLayers(layers_.build(*runtime_, example_->project, clock_, nullptr), interactive_);
}

void HmiExampleView::restart() {
    origin_ = -1;
    frozen_ = false;
    if (manual_ && kind_) {
        // Le tutoriel : au debut du tour, a l'arret.
        interactive_ = false;
        start(kTutorialBase);
        showNow();
    }
    invalidate();
}

void HmiExampleView::start(double now) {
    example_ = kind_ ? hmi::examples::exampleFor(*kind_) : std::nullopt;
    runtime_.reset();
    if (!example_) return;
    runtime_ = std::make_unique<hmi::Runtime>();
    runtime_->bind(&example_->project, nullptr);
    hmi::Runtime::Hooks hooks;
    // Ce que changent les objets des utilisateurs : le projet de l'exemple.
    hooks.userRequest = [this](const hmi::UserRequest& rq) {
        if (!example_) return;
        auto& sec = example_->project.security;
        // Lot 12 : le menu de connexion change aussi les roles d'un groupe et la
        // deconnexion automatique.
        if (rq.op == "role") {
            for (auto& g : sec.groups) {
                if (g.id != rq.group) continue;
                std::erase(g.roles, rq.role);
                if (rq.enabled) g.roles.push_back(rq.role);
            }
            return;
        }
        if (rq.op == "deconnexion") { sec.autoLogoutMin = rq.minutes; return; }
        auto* u = example_->project.user(rq.user);
        if (!u) return;
        if (rq.op == "activer") u->enabled = rq.enabled;
        else if (rq.op == "changer" || rq.op == "motdepasse") { u->salt = rq.salt; u->passwordHash = rq.hash; }
        else if (rq.op == "groupe") u->group = rq.group;
    };
    // Lot 11 : l'editeur de recette enregistre dans le projet de l'exemple ; un
    // export n'ecrit rien (l'exemple le fait comme si).
    hooks.recipeRequest = [this](const hmi::RecipeRequest& rq) {
        if (!example_) return;
        auto* r = const_cast<hmi::Recipe*>(example_->project.recipeByName(rq.recipe));
        if (!r) return;
        if (rq.op == "enregistrer") {
            if (auto* rec = r->record(rq.record)) rec->values = rq.values;
        } else if (rq.op == "ajouter") {
            hmi::RecipeRecord rec;
            rec.id = example_->project.allocate();
            rec.name = "Jeu_" + std::to_string(r->records.size() + 1);
            rec.values = rq.values;
            r->records.push_back(std::move(rec));
        }
    };
    hooks.exportFile = [](const hmi::ExportRequest& rq, std::string* where) {
        if (where) *where = "exports/" + rq.fileName + " (exemple : rien n'est \xC3\xA9" "crit)";
        return true;
    };
    runtime_->setHooks(std::move(hooks));
    runtime_->start(now);
    layers_.reset();
    origin_ = now;
    played_ = 0;
    clock_ = now;
    nextStep_ = 0;
    clickAt_ = -10;
    pointerShown_ = false;
    canvas_->setLive(runtime_.get(), nullptr);
    canvas_->setProject(&example_->project);
    canvas_->setAssets(&example_->project.assets);
}

void HmiExampleView::step(double now) {
    if (!kind_) return;
    if (origin_ < 0 || !runtime_) start(now);
    if (!example_ || !runtime_) return;
    double t = now - origin_;
    if (t > example_->period) {
        start(now);
        if (!example_ || !runtime_) return;
        t = 0;
    }
    runtime_->tick(now);
    (void)hmi::examples::play(*example_, *runtime_, played_, t, now);
    runtime_->tick(now);
    played_ = t;
    clock_ = now;
    canvas_->showLayers(layers_.build(*runtime_, example_->project, now, nullptr), false);
}

void HmiExampleView::advanceTo(double seconds) {
    if (!kind_) return;
    if (origin_ < 0 || !runtime_) start(clock_ > 0 ? clock_ : 1000.0);
    if (!example_) return;
    // Le repere du tour : jamais en arriere.
    if (origin_ + seconds < clock_) { start(clock_); }
    const double target = origin_ + std::clamp(seconds, 0.0, example_->period);
    for (double t = clock_ + 0.05; t < target; t += 0.05) step(t);
    step(target);
}

void HmiExampleView::freezeAt(double seconds) {
    if (seconds < 0) {
        frozen_ = false;
        origin_ = -1;
        invalidate();
        return;
    }
    frozen_ = true;
    origin_ = -1;
    start(1000.0);            // une horloge a elle : le tour rejoue a l'identique
    advanceTo(seconds);
    invalidate();
}

bool HmiExampleView::targetOf(const hmi::examples::Step& s, gfx::Point& out) const {
    gfx::Rect r{};
    bool ok = false;
    if (s.op == "clic" || s.op == "appui" || s.op == "relache") ok = canvas_->objectRect(s.object, r);
    else if (dragged(s.op)) {
        ok = canvas_->partRect(s.object, "fraction:" + s.arg, r);
        if (ok) {
            out = {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
            return true;
        }
    } else if (s.op == "partie") {
        ok = canvas_->partRect(s.object, s.arg, r);
        if (!ok) ok = canvas_->objectRect(s.object, r);
    } else if (s.op == "clavier") {
        ok = canvas_->keyRect(s.arg, r);
    } else if (s.op == "menu") {            // lot 10 : le menu Parametres systeme
        ok = canvas_->systemPartRect(s.arg, r);
    } else if (s.op == "connexion") {       // lot 12 : le menu de connexion
        ok = canvas_->loginPartRect(s.arg, r);
    }
    if (!ok) return false;
    out = {r.x + r.w * 0.5f, r.y + r.h * 0.55f};
    return true;
}

void HmiExampleView::onLayout() {
    const auto b = bounds();
    canvas_->setBounds({b.x, b.y + kCaptionH, b.w, std::max(0.f, b.h - kCaptionH)});
}

void HmiExampleView::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    if (!example_) return;
    if (manual_) {
        // Le tutoriel : son horloge avance quand il joue (a sa vitesse), s'arrete a
        // la fin du tour ; en "A toi", elle tourne sans fin.
        if (!runtime_) {
            start(kTutorialBase);
            showNow();
        }
        const double dt = lastPaint_ < 0 ? 0.0 : std::clamp(ctx.time - lastPaint_, 0.0, 0.25);
        lastPaint_ = ctx.time;
        if (playing_ && dt > 0 && runtime_) {
            double target = played_ + dt * speed_;
            if (!interactive_ && target >= example_->period) {
                target = example_->period;
                playing_ = false;
            }
            manualTo(target);
            showNow();
        }
    } else if (!frozen_) {
        step(ctx.time);
    }
    // L'en-tete : "Exemple anime", ce qu'il montre, et ou en est le tour.
    const auto& f = ctx.theme.font;
    const gfx::Rect head{b.x, b.y, b.w, kCaptionH};
    ctx.r.fillRect(head, ctx.theme.color.windowBg);
    drawHmiGlyph(ctx.r, HmiGlyph::Play, {head.x + 8.f, head.y + 6.f, 16.f, 16.f}, ctx.theme.color.accent);
    const std::string label = title_;
    ctx.r.drawText({head.x + 30.f, head.y + (kCaptionH - ctx.r.lineHeight(f.ui)) / 2.f}, label, f.ui, ctx.theme.color.accent);
    const float lw = ctx.r.measure(label, f.ui).width;
    if (!caption_.empty()) {
        const float room = std::max(0.f, head.w - lw - 110.f);
        const auto n = ctx.r.fitCharacters(caption_, f.smallUi, room);
        ctx.r.drawText({head.x + 42.f + lw, head.y + (kCaptionH - ctx.r.lineHeight(f.smallUi)) / 2.f},
                       std::string_view(caption_).substr(0, n), f.smallUi, ctx.theme.color.textMuted);
    }
    // La progression du tour : une fine barre sous l'en-tete.
    const float frac = example_->period > 0 ? static_cast<float>(std::clamp(played_ / example_->period, 0.0, 1.0)) : 0.f;
    ctx.r.fillRect({head.x, head.bottom() - 2.f, head.w * frac, 2.f}, ctx.theme.color.accent.withAlpha(140));
    // Le cadre de la vue, pour les captures : le meme ajustement que le canevas.
    {
        const auto cb = canvas_->bounds();
        const float z = std::max(0.05f, std::min((cb.w - 24.f) / static_cast<float>(hmi::examples::kWidth),
                                                 (cb.h - 24.f) / static_cast<float>(hmi::examples::kHeight)));
        const float w = static_cast<float>(hmi::examples::kWidth) * z, h = static_cast<float>(hmi::examples::kHeight) * z;
        frame_ = {cb.x + (cb.w - w) / 2.f, cb.y + (cb.h - h) / 2.f, w, h};
    }
    invalidate();   // chaque image : le moteur avance
}

void HmiExampleView::onPaintOverlay(const ui::PaintContext& ctx) {
    if (!example_ || !runtime_ || interactive_) return;       // "A toi" : la vraie souris
    const double t = played_;
    // Les gestes pointes, dans l'ordre : ou et quand.
    struct Mark { double at; gfx::Point p; bool drag; bool ripple; };
    std::vector<Mark> marks;
    gfx::Point last{frame_.right() - 30.f, frame_.bottom() - 24.f};   // le repos : en bas a droite
    const gfx::Point rest = last;
    for (const auto& s : example_->steps) {
        if (!pointed(s.op)) continue;
        gfx::Point p{};
        if (!targetOf(s, p)) p = last;
        marks.push_back({s.at, p, dragged(s.op), rippled(s.op)});
        last = p;
    }
    if (marks.empty()) return;
    // La place du pointeur a l'instant t : au repos, puis de cible en cible. Un
    // glisser (lot 9) : de poignee en poignee, sans elan.
    gfx::Point pos = rest;
    double prevAt = -1e9;
    gfx::Point prev = rest;
    bool prevDrag = false;
    for (const auto& m : marks) {
        const bool follow = m.drag && prevDrag;
        const double go = follow ? prevAt : std::max(prevAt + 0.15, m.at - kMove);
        const double arrive = follow ? m.at : m.at - kArrive;
        if (t < go) { pos = prev; break; }
        if (t < arrive) {
            const double u = std::clamp((t - go) / std::max(0.05, arrive - go), 0.0, 1.0);
            const double e = follow ? u : u * u * (3 - 2 * u);   // doux au depart et a l'arrivee
            pos = {prev.x + static_cast<float>((m.p.x - prev.x) * e), prev.y + static_cast<float>((m.p.y - prev.y) * e)};
            break;
        }
        pos = m.p;
        prev = m.p;
        prevAt = m.at;
        prevDrag = m.drag;
    }
    // Lot 9 : le bouton de la souris enfonce (un appui tenu, un glisser) : un point sous la pointe.
    bool held = false;
    for (const auto& s : example_->steps) {
        if (s.at > t) break;
        if (s.op == "appui" || s.op == "tirer") held = true;
        else if (s.op == "relache" || s.op == "lacher" || s.op == "clic" || s.op == "partie") held = false;
    }
    if (held) ui::shapes::fillPolygon(ctx.r, ui::shapes::ellipse({pos.x, pos.y}, 7.f, 7.f), gfx::Color{255, 255, 255, 90});
    // L'onde du dernier clic.
    for (const auto& m : marks) {
        if (!m.ripple) continue;
        const double since = t - m.at;
        if (since < 0 || since > 0.45) continue;
        const float rr = 6.f + static_cast<float>(since / 0.45) * 22.f;
        const auto a = static_cast<std::uint8_t>(200.0 * (1.0 - since / 0.45));
        ui::shapes::strokePolyline(ctx.r, ui::shapes::ellipse({m.p.x, m.p.y}, rr, rr), true, gfx::Color{255, 255, 255, a}, 2.f);
    }
    // Un texte tape : une bulle pres du pointeur.
    for (const auto& s : example_->steps) {
        if (s.op != "texte" || t < s.at || t > s.at + 0.9) continue;
        const std::string what = "\xC2\xAB " + s.arg + " \xC2\xBB";
        const float tw = ctx.r.measure(what, ctx.theme.font.ui).width + 16.f;
        const gfx::Rect bubble{pos.x + 18.f, pos.y - 30.f, tw, 24.f};   // au-dessus, a droite du pointeur
        ctx.r.fillRoundedRect(bubble, gfx::Color{20, 24, 30, 230}, 5.f);
        ctx.r.strokeRect(bubble, ctx.theme.color.accent, 1.f);
        ctx.r.drawText({bubble.x + 8.f, bubble.y + (24.f - ctx.r.lineHeight(ctx.theme.font.ui)) / 2.f}, what, ctx.theme.font.ui,
                       gfx::Color::rgb(0xE6EAF0));
    }
    // Le pointeur : une fleche blanche au contour sombre.
    const float x = pos.x, y = pos.y;
    const std::vector<gfx::Point> arrow = {{x, y},           {x, y + 20.f},       {x + 5.f, y + 15.5f}, {x + 9.f, y + 24.f},
                                           {x + 12.5f, y + 22.5f}, {x + 8.5f, y + 14.5f}, {x + 15.f, y + 14.5f}};
    ui::shapes::fillPolygon(ctx.r, arrow, gfx::Color{255, 255, 255, 240});
    ui::shapes::strokePolyline(ctx.r, arrow, true, gfx::Color{15, 18, 24, 255}, 1.2f);
}

} // namespace app
