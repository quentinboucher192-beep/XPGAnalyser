#include "DialogFrame.hpp"

#include "Theme.hpp"
#include "Widget.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace ui::dialogframe {

namespace {

constexpr float kEdgeIn = 3.f;      // un bord, cote dedans
constexpr float kEdgeOut = 6.f;     // ... et cote dehors (sur le voile)
constexpr float kCorner = 16.f;     // un coin, le long des deux bords
constexpr float kGrip = 14.f;       // la poignee dessinee en bas a droite, dedans
constexpr float kKeepX = 80.f;      // ce qui reste a l'ecran d'un dialogue pousse de cote
constexpr float kInset = 8.f;       // la marge d'un dialogue mis en grand
constexpr float kThreshold = 3.f;   // un glisser commence apres 3 pixels

struct Frame {
    Widget*                body{nullptr};
    Widget*                root{nullptr};
    std::string            key;
    gfx::Rect              area{};      // ou vit le dialogue (la surface)
    gfx::Rect              wanted{};    // son cadre a lui
    gfx::Rect              rect{};      // celui employe
    bool                   custom{false};   // deplace ou agrandi
    bool                   sized{false};    // agrandi
    bool                   maximized{false};
    gfx::Point             at{};
    gfx::Size              size{};
    std::vector<gfx::Rect> keeps;
};

struct Saved {
    gfx::Size  wanted{};
    bool       custom{false}, sized{false}, maximized{false};
    gfx::Point at{};
    gfx::Size  size{};
};

struct Drag {
    Widget*    body{nullptr};
    Grip       grip{Grip::None};
    gfx::Point from{};
    gfx::Rect  start{};
    bool       moved{false};
};

// L'etat, jamais detruit : un widget detruit a la sortie du programme (apres les statiques de ce fichier) appelle
// encore forget().
struct State {
    std::vector<Frame>                        frames;
    std::map<std::string, Saved, std::less<>> saved;
    std::string                               key;
    Drag                                      drag;
    Grip                                      hover{Grip::None};
    const Widget*                             hoverBody{nullptr};
};
State& state() {
    static State* s = new State();
    return *s;
}

Frame* find(const Widget* body) {
    for (auto& f : state().frames)
        if (f.body == body) return &f;
    return nullptr;
}

// Le dernier mis en page de cet arbre (un dialogue n'en a qu'un).
Frame* findRoot(const Widget* root) {
    for (auto it = state().frames.rbegin(); it != state().frames.rend(); ++it)
        if (it->root == root) return &*it;
    return nullptr;
}

bool sameSize(gfx::Size a, gfx::Size b) { return std::fabs(a.w - b.w) < 0.5f && std::fabs(a.h - b.h) < 0.5f; }

gfx::Rect compute(const Frame& f) {
    const gfx::Rect a = f.area;
    if (a.w <= 0.f || a.h <= 0.f) return f.wanted;
    if (f.maximized) {
        const float w = std::max(a.w - 2.f * kInset, std::min(f.wanted.w, a.w));
        const float h = std::max(a.h - 2.f * kInset, std::min(f.wanted.h, a.h));
        return {std::floor(a.x + (a.w - w) * 0.5f), std::floor(a.y + (a.h - h) * 0.5f), w, h};
    }
    if (!f.custom) return f.wanted;
    // Jamais plus petit que son cadre a lui (son contenu y est place pour cette taille).
    float w = f.sized ? std::max(f.size.w, f.wanted.w) : f.wanted.w;
    float h = f.sized ? std::max(f.size.h, f.wanted.h) : f.wanted.h;
    w = std::min(w, std::max(a.w, f.wanted.w));
    h = std::min(h, std::max(a.h, f.wanted.h));
    // La barre de titre reste a portee : en haut dans l'ecran, un bout de chaque cote.
    const float x = std::clamp(f.at.x, a.x - w + kKeepX, std::max(a.x - w + kKeepX, a.right() - kKeepX));
    const float y = std::clamp(f.at.y, a.y, std::max(a.y, a.bottom() - kTitleBand));
    return {std::floor(x), std::floor(y), w, h};
}

void save(const Frame& f) {
    if (f.key.empty()) return;
    state().saved[f.key] = Saved{{f.wanted.w, f.wanted.h}, f.custom, f.sized, f.maximized, f.at, f.size};
}

void settle(Frame& f) {
    f.rect = compute(f);
    if (f.body) f.body->invalidateLayout();
}

bool sameRect(const gfx::Rect& a, const gfx::Rect& b) {
    return std::fabs(a.x - b.x) < 0.5f && std::fabs(a.y - b.y) < 0.5f && sameSize({a.w, a.h}, {b.w, b.h});
}

// Un controle du dialogue sous `p` (un bouton, un champ, une liste) : le plus
// profond qui le contient. Le vide d'un conteneur ne compte pas, ni le panneau
// lui-meme quand c'est un widget (la planche des nouveautes) : on regarde dedans.
bool controlAt(const Widget& w, gfx::Point p, const gfx::Rect& frame) {
    const auto& kids = w.children();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        const Widget& c = **it;
        if (!c.visible() || !c.eventBounds().contains(p)) continue;
        if (sameRect(c.bounds(), frame)) {
            if (controlAt(c, p, frame)) return true;
            continue;
        }
        if (c.children().empty() || controlAt(c, p, frame)) return true;
    }
    return false;
}

Grip gripAt(const Frame& f, gfx::Point p) {
    const gfx::Rect r = f.rect;
    if (r.w <= 0.f || r.h <= 0.f) return Grip::None;
    const gfx::Rect outer{r.x - kEdgeOut, r.y - kEdgeOut, r.w + 2.f * kEdgeOut, r.h + 2.f * kEdgeOut};
    if (!outer.contains(p)) return Grip::None;
    if (!f.maximized) {
        if (r.contains(p) && p.x >= r.right() - kGrip && p.y >= r.bottom() - kGrip) return Grip::SE;
        const bool l = p.x < r.x + kEdgeIn, rt = p.x >= r.right() - kEdgeIn;
        const bool t = p.y < r.y + kEdgeIn, b = p.y >= r.bottom() - kEdgeIn;
        const bool nl = p.x < r.x + kCorner, nr = p.x >= r.right() - kCorner;
        const bool nt = p.y < r.y + kCorner, nb = p.y >= r.bottom() - kCorner;
        if ((t && nl) || (l && nt)) return Grip::NW;
        if ((t && nr) || (rt && nt)) return Grip::NE;
        if ((b && nl) || (l && nb)) return Grip::SW;
        if ((b && nr) || (rt && nb)) return Grip::SE;
        if (l) return Grip::W;
        if (rt) return Grip::E;
        if (t) return Grip::N;
        if (b) return Grip::S;
    }
    if (!r.contains(p) || p.y >= r.y + kTitleBand || p.x >= r.right() - kTitleRightKeep) return Grip::None;
    for (const auto& k : f.keeps)
        if (k.contains(p)) return Grip::None;
    if (f.body && controlAt(*f.body, p, r)) return Grip::None;
    return Grip::Move;
}

bool takesLeft(Grip g) { return g == Grip::W || g == Grip::NW || g == Grip::SW; }
bool takesRight(Grip g) { return g == Grip::E || g == Grip::NE || g == Grip::SE; }
bool takesTop(Grip g) { return g == Grip::N || g == Grip::NW || g == Grip::NE; }
bool takesBottom(Grip g) { return g == Grip::S || g == Grip::SW || g == Grip::SE; }

void dragTo(Frame& f, gfx::Point p) {
    Drag& dr = state().drag;
    const float dx = p.x - dr.from.x, dy = p.y - dr.from.y;
    if (!dr.moved) {
        if (std::fabs(dx) < kThreshold && std::fabs(dy) < kThreshold) return;
        dr.moved = true;
        if (f.maximized) {
            // Mis en grand puis tire par le titre : il reprend sa taille sous la souris.
            const float fx = (dr.from.x - dr.start.x) / std::max(1.f, dr.start.w);
            f.maximized = false;
            gfx::Rect back = compute(f);
            back.x = std::floor(dr.from.x - fx * back.w);
            back.y = dr.start.y;
            f.custom = true;
            f.at = {back.x, back.y};
            dr.start = back;
        }
    }
    const gfx::Rect s = dr.start;
    const Grip g = dr.grip;
    if (g == Grip::Move) {
        f.custom = true;
        f.at = {s.x + dx, s.y + dy};
    } else {
        const float minW = std::min(f.wanted.w, f.area.w), minH = std::min(f.wanted.h, f.area.h);
        float l = s.x, t = s.y, r = s.right(), b = s.bottom();
        if (takesLeft(g)) l = std::max(f.area.x, std::min(s.x + dx, r - minW));
        if (takesRight(g)) r = std::min(f.area.right(), std::max(s.right() + dx, l + minW));
        if (takesTop(g)) t = std::max(f.area.y, std::min(s.y + dy, b - minH));
        if (takesBottom(g)) b = std::min(f.area.bottom(), std::max(s.bottom() + dy, t + minH));
        f.custom = f.sized = true;
        f.at = {l, t};
        f.size = {r - l, b - t};
    }
    settle(f);
}

} // namespace

gfx::Rect place(Widget& body, gfx::Rect wanted) {
    Frame* f = find(&body);
    if (!f) {
        Frame n;
        n.body = &body;
        n.key = state().key;
        if (!n.key.empty())
            if (auto it = state().saved.find(n.key); it != state().saved.end() && sameSize(it->second.wanted, {wanted.w, wanted.h})) {
                n.custom = it->second.custom;
                n.sized = it->second.sized;
                n.maximized = it->second.maximized;
                n.at = it->second.at;
                n.size = it->second.size;
            }
        state().frames.push_back(std::move(n));
        f = &state().frames.back();
    }
    f->root = &body.topWidget();
    f->area = body.bounds();
    f->wanted = wanted;
    f->keeps.clear();
    f->rect = compute(*f);
    return f->rect;
}

void keep(Widget& body, gfx::Rect r) {
    if (Frame* f = find(&body)) f->keeps.push_back(r);
}

bool handle(Widget& root, const InputEvent& ev) {
    if (state().drag.body) {
        Frame* f = find(state().drag.body);
        if (!f) {
            state().drag = {};
            return false;
        }
        if (f->root != &root) return false;
        if (const auto* m = std::get_if<MouseMove>(&ev)) {
            dragTo(*f, m->pos);
            return true;
        }
        if (const auto* u = std::get_if<MouseUp>(&ev)) {
            if (u->button == MouseButton::Left) {
                if (state().drag.moved) save(*f);
                state().drag = {};
            }
            return true;
        }
        return std::holds_alternative<MouseDown>(ev);   // un autre bouton pendant le glisser
    }
    Frame* f = findRoot(&root);
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        state().hover = f ? gripAt(*f, m->pos) : Grip::None;
        state().hoverBody = state().hover == Grip::None || !f ? nullptr : f->body;
        return false;
    }
    if (!f) return false;
    const auto* d = std::get_if<MouseDown>(&ev);
    if (!d || d->button != MouseButton::Left) return false;
    const Grip g = gripAt(*f, d->pos);
    if (g == Grip::None) return false;
    if (g == Grip::Move && d->clickCount >= 2) {
        // Double clic sur le titre : en grand, ou rendu a sa place.
        f->maximized = !f->maximized;
        settle(*f);
        save(*f);
        return true;
    }
    state().drag = Drag{f->body, g, d->pos, f->rect, false};
    return true;
}

Grip cursor() noexcept { return state().drag.body ? state().drag.grip : state().hover; }

// 1.14.1 (N2) : la forme du pointeur (ui::cursor) pour ce que la souris prend.
cursor::Shape shapeOf(Grip g) noexcept {
    switch (g) {
        case Grip::Move: return cursor::Shape::Move;
        case Grip::W: case Grip::E: return cursor::Shape::ResizeEW;
        case Grip::N: case Grip::S: return cursor::Shape::ResizeNS;
        case Grip::NW: case Grip::SE: return cursor::Shape::ResizeNWSE;
        case Grip::NE: case Grip::SW: return cursor::Shape::ResizeNESW;
        case Grip::None: break;
    }
    return cursor::Shape::Default;
}

void paint(Widget& root, gfx::IRenderer& r, const Theme& theme) {
    const Frame* f = findRoot(&root);
    if (!f || f->rect.w <= 0.f || f->rect.h <= 0.f) return;
    const gfx::Rect q = f->rect;
    // 1.14.1 (N2) : le pointeur passe par ui::cursor - le titre ou le bord tire (le geste, meme la
    // souris sortie du dialogue), sinon celui sous la souris (apres l'arbre du dialogue : il gagne).
    if (state().drag.body == f->body && state().drag.grip != Grip::None) cursor::hold(shapeOf(state().drag.grip));
    else if (state().hoverBody == f->body && state().hover != Grip::None) cursor::request(shapeOf(state().hover));
    // Un bord pris ou montre : le cadre en couleur, pour voir ce qui va bouger.
    const bool active = (state().drag.body == f->body && state().drag.grip != Grip::Move)
                        || (state().hoverBody == f->body && state().hover != Grip::None && state().hover != Grip::Move);
    if (active) r.strokeRect(q, theme.color.accent, 1.f);
    if (f->maximized) return;
    const gfx::Color c = theme.color.textMuted.withAlpha(170);
    for (int i = 0; i < 3; ++i) {
        const float d = 4.f + 4.f * static_cast<float>(i);
        r.line({q.right() - 3.f - d, q.bottom() - 3.f}, {q.right() - 3.f, q.bottom() - 3.f - d}, c, 1.f);
    }
}

KeyScope::KeyScope(std::string_view key) : previous_(state().key) { state().key = std::string(key); }
KeyScope::~KeyScope() { state().key = std::move(previous_); }

void forget(const Widget* body) noexcept {
    if (state().frames.empty()) return;
    for (auto it = state().frames.begin(); it != state().frames.end(); ++it) {
        if (it->body != body) continue;
        try {
            save(*it);
        } catch (...) {
        }
        if (state().drag.body == body) state().drag = {};
        if (state().hoverBody == body) {
            state().hoverBody = nullptr;
            state().hover = Grip::None;
        }
        state().frames.erase(it);
        return;
    }
}

gfx::Rect frameOf(Widget& root) {
    const Frame* f = findRoot(&root);
    return f ? f->rect : gfx::Rect{};
}

void reset() noexcept {
    state().frames.clear();
    state().saved.clear();
    state().drag = {};
    state().hover = Grip::None;
    state().hoverBody = nullptr;
}

} // namespace ui::dialogframe
