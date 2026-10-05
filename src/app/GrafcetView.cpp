#include "GrafcetView.hpp"

#include "../ui/Theme.hpp"
#include "Capture.hpp"
#include "Dossiers.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <ctime>

namespace app {

    using namespace ui;

    namespace {
        // Les tailles des textes du dessin, a zoom 1 (le dessin est calcule a 13 px).
        constexpr float kText = 13.f;
        constexpr float kNumber = 15.f;
        constexpr float kSmall = 11.f;
        constexpr float kSnap = 10.f;   // une etape glissee s'aimante a 10 px

        gfx::FontId sized(float px, float zoom) {
            return gfx::FontId{ static_cast<std::uint16_t>(std::clamp(std::round(px * zoom), 7.f, 64.f)) };
        }

        bool isTrue(const std::string& text) {
            return text == "TRUE" || text == "1" || text == "true";
        }

        // Une pointe de fleche en `tip`, vers le bas (down) ou vers le haut.
        void arrowHead(gfx::IRenderer& r, gfx::Point tip, bool down, float size, gfx::Color c) {
            const float dy = down ? -size : size;
            const gfx::Vertex v[3] = {
                { tip, c },
                { { tip.x - size * 0.6f, tip.y + dy }, c },
                { { tip.x + size * 0.6f, tip.y + dy }, c },
            };
            r.fillTriangles(v, 3);
        }

        // Un cadre en tirets (un controle en defaut) : il se distingue d'une selection.
        void dashedRect(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color c, float w) {
            const float dash = 5.f, gap = 3.f;
            auto run = [&](gfx::Point a, gfx::Point b) {
                const float len = std::hypot(b.x - a.x, b.y - a.y);
                if (len < 0.5f) return;
                const float ux = (b.x - a.x) / len, uy = (b.y - a.y) / len;
                for (float t = 0.f; t < len; t += dash + gap) {
                    const float e = std::min(len, t + dash);
                    r.line({ a.x + ux * t, a.y + uy * t }, { a.x + ux * e, a.y + uy * e }, c, w);
                }
            };
            run({ box.x, box.y }, { box.right(), box.y });
            run({ box.right(), box.y }, { box.right(), box.bottom() });
            run({ box.right(), box.bottom() }, { box.x, box.bottom() });
            run({ box.x, box.bottom() }, { box.x, box.y });
        }
    } // namespace

    GrafcetView::GrafcetView(std::string id) : Widget(std::move(id)) {
        setFocusPolicy(true);
    }

    void GrafcetView::setChart(grafcet::Chart chart) {
        const bool sameChart = chart.name == chart_.name;
        chart_ = std::move(chart);
        dirty_ = true;
        // Les boites de la peinture d'avant appartiennent au grafcet remplace : une
        // etape supprimee resterait cliquable jusqu'a la prochaine peinture.
        stepBoxes_.clear();
        bars_.clear();
        chips_.clear();
        if (!sameChart) {
            selectedStep_ = chart_.steps.empty() ? -1 : chart_.steps.front().id;
            scrollX_ = scrollY_ = 0.f;
            fitPending_ = true;
        }
        invalidate();
    }

    void GrafcetView::setChecks(std::vector<grafcet::Check> checks) {
        checks_ = std::move(checks);
        invalidate();
    }

    void GrafcetView::setTool(GrafcetTool tool) {
        if (tool_ == tool) return;
        tool_ = tool;
        // Une liaison commencee ne survit pas a un changement d'outil.
        wireTransition_ = wireStep_ = -1;
        invalidate();
    }

    void GrafcetView::setPlacement(const project::ChartLayout* placement) noexcept {
        placement_ = placement;
        pending_ = placement ? *placement : project::ChartLayout{};
        dirty_ = true;
        invalidate();
    }

    void GrafcetView::select(GrafcetPart part, int id) {
        selectedPart_ = part;
        selectedId_ = id;
        if (part == GrafcetPart::Step) selectedStep_ = id;
        invalidate();
    }

    gfx::Point GrafcetView::toScreen(gfx::Point p) const {
        return { bounds().x - scrollX_ + p.x * zoom_, bounds().y + topInset_ - scrollY_ + p.y * zoom_ };
    }

    gfx::Rect GrafcetView::toScreen(const gfx::Rect& r) const {
        const auto p = toScreen(gfx::Point{ r.x, r.y });
        return { p.x, p.y, r.w * zoom_, r.h * zoom_ };
    }

    gfx::Point GrafcetView::toChart(gfx::Point p) const {
        return { (p.x - bounds().x + scrollX_) / zoom_, (p.y - bounds().y - topInset_ + scrollY_) / zoom_ };
    }

    int GrafcetView::stepAt(gfx::Point at) const {
        for (const auto& box : stepBoxes_) if (box.rect.contains(at)) return box.id;
        return -1;
    }

    int GrafcetView::transitionAt(gfx::Point at) const {
        for (const auto& bar : bars_) if (bar.rect.contains(at)) return bar.id;
        return -1;
    }

    int GrafcetView::actionAt(gfx::Point at) const {
        for (const auto& chip : chips_) if (chip.rect.contains(at)) return chip.id;
        return -1;
    }

    void GrafcetView::clampScroll() {
        const float reachX = std::max(0.f, diagram_.size.w * zoom_ - bounds().w + 40.f);
        const float reachY = std::max(0.f, diagram_.size.h * zoom_ - bounds().h + 40.f);
        scrollX_ = std::clamp(scrollX_, -40.f, reachX);
        scrollY_ = std::clamp(scrollY_, -40.f, reachY);
    }

    void GrafcetView::setZoom(float zoom) {
        const float next = std::clamp(zoom, 0.3f, 3.f);
        if (std::abs(next - zoom_) < 0.001f) return;
        // Le milieu de la vue reste ou il est.
        const gfx::Point centre{ bounds().x + bounds().w / 2.f, bounds().y + bounds().h / 2.f };
        const gfx::Point at = toChart(centre);
        zoom_ = next;
        scrollX_ = at.x * zoom_ - bounds().w / 2.f;
        scrollY_ = at.y * zoom_ - bounds().h / 2.f;
        clampScroll();
        fitPending_ = false;
        zoomChanged->emit(zoom_);
        invalidate();
    }

    void GrafcetView::fit() {
        fitPending_ = false;
        if (diagram_.size.w <= 0.f || diagram_.size.h <= 0.f || bounds().w <= 0.f) {
            fitPending_ = true;
            invalidate();
            return;
        }
        const float z = std::min({ (bounds().w - 16.f) / diagram_.size.w,
                                   (bounds().h - 16.f) / diagram_.size.h, 1.25f });
        zoom_ = std::clamp(z, 0.3f, 3.f);
        scrollX_ = -std::max(0.f, (bounds().w - diagram_.size.w * zoom_) / 2.f);
        scrollY_ = 0.f;
        zoomChanged->emit(zoom_);
        invalidate();
    }

    void GrafcetView::reveal(GrafcetPart part, int id) {
        gfx::Rect r{};
        if (part == GrafcetPart::Step) { if (const auto* s = diagram_.step(id)) r = s->box; }
        else if (part == GrafcetPart::Transition) { if (const auto* t = diagram_.transition(id)) r = t->hit; }
        else if (part == GrafcetPart::Action) { if (const auto* a = diagram_.action(id)) r = a->box; }
        if (r.w <= 0.f) return;
        const gfx::Rect on = toScreen(r);
        if (!bounds().inset(20.f, 20.f).contains({ on.x, on.y }) || !bounds().contains({ on.right(), on.bottom() })) {
            scrollX_ = (r.x + r.w / 2.f) * zoom_ - bounds().w / 2.f;
            scrollY_ = (r.y + r.h / 2.f) * zoom_ - bounds().h / 2.f;
            clampScroll();
        }
        invalidate();
    }

    SizeHint GrafcetView::sizeHint() const {
        SizeHint h;
        h.preferred = { 600.f, 400.f };
        h.minimum = { 240.f, 160.f };
        h.stretchX = 1.f;
        h.stretchY = 1.f;
        return h;
    }

    // ---------------------------------------------------------------------------
    //  L'etat en simulation. Lu par son nom a chaque image, jamais garde.
    //  L'indice d'un tableau est la PLACE de l'element dans le grafcet (la ou
    //  Builder l'a ecrit), pas son numero : ils coincident dans ce projet, et s'y
    //  fier casserait au premier grafcet ou ils different.
    // ---------------------------------------------------------------------------
    std::string GrafcetView::arrayField(std::string_view array, int index,
        std::string_view field) const {
        if (!runtime_ || chart_.arrayPrefix.empty() || index < 0) return {};
        const std::string name = std::string(array) + "_" + chart_.arrayPrefix + "["
            + std::to_string(index) + "]." + std::string(field);
        // 1.10 : les tableaux d'une unite de programme sont nommes avec elle
        // (Logigrammes_A.Steps_DetoxalA[0].Active) ; sans cela, rien ne s'allumait.
        sim::Value v;
        if (!chart_.scope.empty() && runtime_->get(chart_.scope + name, v)) return v.display();
        if (!runtime_->get(name, v)) return {};
        return v.display();
    }

    namespace {
        template <typename T>
        int indexOf(const std::vector<T>& items, int id) {
            for (std::size_t i = 0; i < items.size(); ++i)
                if (items[i].id == id) return static_cast<int>(i);
            return -1;
        }
    } // namespace

    std::optional<bool> GrafcetView::stepActive(int id) const {
        if (!runtime_) return std::nullopt;
        const auto text = arrayField("Steps", indexOf(chart_.steps, id), "Active");
        if (text.empty()) return std::nullopt;
        return isTrue(text);
    }

    std::string GrafcetView::stepActiveTime(int id) const {
        const auto text = arrayField("Steps", indexOf(chart_.steps, id), "ActiveTime");
        if (text.empty()) return {};
        const auto readable = grafcet::durationText(text);
        return readable.empty() ? text : readable;
    }

    std::optional<bool> GrafcetView::transitionTrue(int id) const {
        if (!runtime_) return std::nullopt;
        const auto text = arrayField("Trans", indexOf(chart_.transitions, id), "Condition");
        if (text.empty()) return std::nullopt;
        return isTrue(text);
    }

    // Validee : toutes ses etapes d'amont sont actives (IEC 60848, regle 2).
    bool GrafcetView::transitionValidated(int id) const {
        if (!runtime_) return false;
        const auto* t = grafcet::transitionById(chart_, id);
        if (!t || t->sources.empty()) return false;
        for (const int s : t->sources) {
            const auto a = stepActive(s);
            if (!a || !*a) return false;
        }
        return true;
    }

    std::optional<bool> GrafcetView::actionRunning(int id) const {
        if (!runtime_) return std::nullopt;
        const auto text = arrayField("Acts", indexOf(chart_.actions, id), "Out");
        if (text.empty()) return std::nullopt;
        return isTrue(text);
    }

    const grafcet::Check* GrafcetView::checkOn(char part, int id) const {
        const grafcet::Check* found = nullptr;
        for (const auto& c : checks_) {
            if (c.part != part || c.id != id) continue;
            if (!found || (c.severe && !found->severe)) found = &c;
        }
        return found;
    }

    // ---------------------------------------------------------------------------
    //  1.10 (R) : les commandes du moteur (ST_GC_CTRL) et l'historique
    // ---------------------------------------------------------------------------
    namespace {
        std::string clockNow() {
            const std::time_t now = std::time(nullptr);
            char buf[16]{};
            if (const std::tm* tm = std::localtime(&now)) std::strftime(buf, sizeof buf, "%H:%M:%S", tm);
            return buf;
        }
        std::string stepList(const std::vector<int>& ids) {
            std::string out;
            for (const int id : ids) out += (out.empty() ? "" : ", ") + ("X" + std::to_string(id));
            return out.empty() ? std::string("-") : out;
        }
    } // namespace

    void GrafcetView::askEngine(EngineCommand cmd) {
        if (!runtime_) return;
        if (cmd == EngineCommand::ForceSituation) {
            if (selectedPart_ != GrafcetPart::Step || selectedId_ < 0) return;
            forceTarget_ = selectedId_;
        }
        pendingCmd_ = cmd;
        invalidate();
    }

    void GrafcetView::cancelEngine() {
        pendingCmd_ = EngineCommand::None;
        invalidate();
    }

    std::string GrafcetView::engineText(EngineCommand cmd) const {
        const std::string ctrl = chart_.scope + "Ctrl_" + chart_.name + ".Cmd.";
        switch (cmd) {
        case EngineCommand::Init:
            return "Initialiser " + chart_.name + " : le moteur d\xC3\xA9sactive toutes les \xC3\xA9tapes et active "
                   "l'\xC3\xA9tape initiale (" + ctrl + "InitReq).";
        case EngineCommand::ResetAll:
            return "Tout r\xC3\xA9initialiser " + chart_.name + " : \xC3\xA9tapes, actions, tempos et compteurs "
                   "reviennent \xC3\xA0 z\xC3\xA9ro (" + ctrl + "ResetAllReq).";
        case EngineCommand::ForceNext:
            return "Forcer l'\xC3\xA9tape suivante : le moteur franchit la transition de sortie de l'\xC3\xA9tape active "
                   "sans attendre sa r\xC3\xA9" "ceptivit\xC3\xA9 (" + ctrl + "ForceNextReq).";
        case EngineCommand::ForceBack:
            return "Forcer l'\xC3\xA9tape pr\xC3\xA9" "c\xC3\xA9" "dente : le moteur revient \xC3\xA0 l'\xC3\xA9tape d'avant "
                   "(" + ctrl + "ForceBackReq).";
        case EngineCommand::ForceSituation: {
            std::vector<int> active;
            for (const auto& st : chart_.steps) if (const auto a = stepActive(st.id); a && *a) active.push_back(st.id);
            return "Forcer la situation : le moteur d\xC3\xA9sactive " + stepList(active) + " et active X"
                   + std::to_string(forceTarget_) + " (" + ctrl + "ForceStepFromId / ForceStepToId).";
        }
        default: return {};
        }
    }

    bool GrafcetView::confirmEngine() {
        const EngineCommand cmd = pendingCmd_;
        pendingCmd_ = EngineCommand::None;
        invalidate();
        if (!runtime_ || cmd == EngineCommand::None || chart_.name.empty()) return false;
        std::string ctrl = chart_.scope + "Ctrl_" + chart_.name + ".Cmd.";
        if (sim::Value probe; !runtime_->get(ctrl + "InitReq", probe)) ctrl = "Ctrl_" + chart_.name + ".Cmd.";
        bool ok = false;
        std::string what;
        std::vector<int> active;
        for (const auto& st : chart_.steps) if (const auto a = stepActive(st.id); a && *a) active.push_back(st.id);
        switch (cmd) {
        case EngineCommand::Init:      ok = runtime_->write(ctrl + "InitReq", sim::Value::boolean(true)); what = "Initialiser"; break;
        case EngineCommand::ResetAll:  ok = runtime_->write(ctrl + "ResetAllReq", sim::Value::boolean(true)); what = "Tout r\xC3\xA9initialiser"; break;
        case EngineCommand::ForceNext: ok = runtime_->write(ctrl + "ForceNextReq", sim::Value::boolean(true)); what = "\xC3\xA9tape suivante"; break;
        case EngineCommand::ForceBack: ok = runtime_->write(ctrl + "ForceBackReq", sim::Value::boolean(true)); what = "\xC3\xA9tape pr\xC3\xA9" "c\xC3\xA9" "dente"; break;
        case EngineCommand::ForceSituation:
            ok = runtime_->write(ctrl + "ForceStepFromId",
                                 sim::Value::integer(sim::Type::Int, active.empty() ? -1 : active.front()))
              && runtime_->write(ctrl + "ForceStepToId", sim::Value::integer(sim::Type::Int, forceTarget_));
            what = "situation X" + std::to_string(forceTarget_);
            break;
        default: break;
        }
        Firing f;
        f.time = clockNow();
        f.from = stepList(active);
        f.to = cmd == EngineCommand::ForceSituation ? "X" + std::to_string(forceTarget_) : std::string("?");
        f.condition = (ok ? "forc\xC3\xA9 : " : "refus\xC3\xA9 : ") + what;
        f.forced = true;
        history_.push_back(std::move(f));
        if (history_.size() > 200) history_.erase(history_.begin());
        historyChanged->emit();
        return ok;
    }

    // ---- exporter en PNG --------------------------------------------------------
    void GrafcetView::exportPng() {
        if (chart_.steps.empty()) return;
        exportPending_ = true;
        invalidate();
    }

    // ---- chercher ------------------------------------------------------------
    namespace {
        std::string lowered(std::string text) {
            for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return text;
        }
    } // namespace

    void GrafcetView::startSearch() {
        searching_ = true;
        search_.clear();
        searchHits_.clear();
        searchIndex_ = 0;
        invalidate();
    }

    void GrafcetView::setSearch(std::string text) {
        search_ = std::move(text);
        searchHits_.clear();
        searchIndex_ = 0;
        const std::string q = lowered(search_);
        if (!q.empty()) {
            for (const auto& st : chart_.steps)
                if (lowered("X" + std::to_string(st.id)) == q || lowered(st.name).find(q) != std::string::npos)
                    searchHits_.push_back({ GrafcetPart::Step, st.id });
            for (const auto& t : chart_.transitions)
                if (lowered("T" + std::to_string(t.id)) == q
                    || lowered(grafcet::drawnCondition(chart_, t)).find(q) != std::string::npos
                    || lowered(t.conditionExpr).find(q) != std::string::npos)
                    searchHits_.push_back({ GrafcetPart::Transition, t.id });
            for (const auto& a : chart_.actions)
                if (lowered("A" + std::to_string(a.id)) == q || lowered(a.name).find(q) != std::string::npos)
                    searchHits_.push_back({ GrafcetPart::Action, a.id });
        }
        if (!searchHits_.empty()) {
            const auto [part, id] = searchHits_.front();
            select(part, id);
            reveal(part, id);
            if (part == GrafcetPart::Step) stepSelected->emit(id);
            else if (part == GrafcetPart::Transition) transitionSelected->emit(id);
            else actionSelected->emit(id);
        }
        invalidate();
    }

    void GrafcetView::nextSearchHit() {
        if (searchHits_.empty()) return;
        searchIndex_ = (searchIndex_ + 1) % searchHits_.size();
        const auto [part, id] = searchHits_[searchIndex_];
        select(part, id);
        reveal(part, id);
        if (part == GrafcetPart::Step) stepSelected->emit(id);
        else if (part == GrafcetPart::Transition) transitionSelected->emit(id);
        else actionSelected->emit(id);
        invalidate();
    }

    // Les franchissements, vus d'une image a l'autre : une transition dont
    // toutes les etapes d'amont etaient actives et dont une etape d'aval vient de
    // s'activer a ete franchie.
    void GrafcetView::trackHistory() {
        if (!runtime_) { haveLast_ = false; lastActive_.clear(); return; }
        std::vector<int> now;
        for (const auto& st : chart_.steps) if (const auto a = stepActive(st.id); a && *a) now.push_back(st.id);
        if (haveLast_ && now != lastActive_) {
            auto was = [&](int id) { return std::find(lastActive_.begin(), lastActive_.end(), id) != lastActive_.end(); };
            auto is = [&](int id) { return std::find(now.begin(), now.end(), id) != now.end(); };
            bool any = false;
            for (const auto& t : chart_.transitions) {
                if (t.sources.empty() || t.destinations.empty()) continue;
                bool fromAll = true, toNew = false;
                for (const int x : t.sources) if (!was(x)) fromAll = false;
                for (const int x : t.destinations) if (is(x) && !was(x)) toNew = true;
                if (!fromAll || !toNew) continue;
                Firing f;
                f.time = clockNow();
                f.transition = t.id;
                f.from = stepList(t.sources);
                f.to = stepList(t.destinations);
                f.condition = grafcet::drawnCondition(chart_, t);
                history_.push_back(std::move(f));
                any = true;
            }
            while (history_.size() > 200) history_.erase(history_.begin());
            if (any) historyChanged->emit();
        }
        lastActive_ = std::move(now);
        haveLast_ = true;
    }

    // ---------------------------------------------------------------------------
    void GrafcetView::rebuild(const PaintContext& ctx) {
        if (!dirty_) return;
        auto* r = &ctx.r;
        const auto font = gfx::FontId{ static_cast<std::uint16_t>(kText) };
        // Une receptivite plus longue est coupee (...) : l'expression entiere est
        // dans l'infobulle et les Proprietes ; le dessin reste lisible a l'ecran.
        grafcet::DiagramMetrics metrics;
        metrics.maxLabel = 240.f;
        diagram_ = grafcet::buildDiagram(chart_, &pending_,
            [r, font](std::string_view text) { return r->measure(text, font).width; }, metrics);
        dirty_ = false;
    }

    void GrafcetView::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto  area = bounds();
        ctx.r.fillRect(area, c.windowBg);

        if (chart_.steps.empty()) {
            stepBoxes_.clear();
            bars_.clear();
            chips_.clear();
            ctx.r.drawText({ area.x + 16.f, area.y + 16.f },
                "Ce grafcet n'a pas d'\xC3\xA9tape : outil \xC3\x89tape pour en ajouter une.",
                ctx.theme.font.ui, c.textMuted);
            return;
        }

        rebuild(ctx);
        // en simulation, la barre des commandes du moteur a sa bande en haut : le
        // dessin commence dessous (elle ne couvre plus les couloirs des remontees)
        topInset_ = live() && !exportPending_ ? ctx.r.lineHeight(ctx.theme.font.ui) + 20.f : 0.f;
        // l'export : tout le grafcet, le temps d'une image
        const float keepZoom = zoom_, keepX = scrollX_, keepY = scrollY_;
        if (exportPending_) {
            zoom_ = std::clamp(std::min({ (area.w - 16.f) / std::max(1.f, diagram_.size.w),
                                          (area.h - 16.f) / std::max(1.f, diagram_.size.h), 1.25f }), 0.2f, 3.f);
            scrollX_ = -std::max(0.f, (area.w - diagram_.size.w * zoom_) / 2.f);
            scrollY_ = -std::max(0.f, (area.h - diagram_.size.h * zoom_) / 2.f);
        }
        if (fitPending_ && area.w > 40.f && area.h > 40.f) {
            fitPending_ = false;
            // A l'ouverture, jamais en dessous de 85 % : les textes restent lisibles
            // (un grand grafcet se parcourt ; Ajuster montre tout).
            const float z = std::min({ (area.w - 16.f) / std::max(1.f, diagram_.size.w),
                                       (area.h - 16.f) / std::max(1.f, diagram_.size.h), 1.25f });
            zoom_ = std::clamp(z, 0.85f, 3.f);
            scrollX_ = -std::max(0.f, (area.w - diagram_.size.w * zoom_) / 2.f);
            scrollY_ = 0.f;
            zoomChanged->emit(zoom_);
        }
        if (revealStep_ >= 0) { const int s = revealStep_; revealStep_ = -1; reveal(GrafcetPart::Step, s); }

        ctx.r.pushClip(area);

        // un quadrillage discret : on voit qu'on est sur une feuille de dessin
        {
            const float pitch = 24.f * zoom_;
            const gfx::Color grid = c.gridLine.withAlpha(60);
            if (pitch >= 8.f) {
                for (float x = area.x - std::fmod(scrollX_, pitch) - (scrollX_ < 0 ? pitch : 0.f); x < area.right(); x += pitch)
                    ctx.r.line({ x, area.y }, { x, area.bottom() }, grid, 1.f);
                for (float y = area.y - std::fmod(scrollY_, pitch) - (scrollY_ < 0 ? pitch : 0.f); y < area.bottom(); y += pitch)
                    ctx.r.line({ area.x, y }, { area.right(), y }, grid, 1.f);
            }
        }

        const bool  running = live();
        const auto  fText = sized(kText, zoom_);
        const auto  fNumber = sized(kNumber, zoom_);
        const auto  fSmall = sized(kSmall, zoom_);
        const float lhText = ctx.r.lineHeight(fText);
        const float lhSmall = ctx.r.lineHeight(fSmall);
        const gfx::Color wire = c.textMuted;
        const gfx::Color green = c.ok, yellow = c.warning, red = c.error;
        const float w1 = std::max(1.f, 1.5f * zoom_);

        // l'etat de chaque transition en simulation : 0 rien, 1 validee, 2 validee et vraie
        auto transitionState = [&](int id) {
            if (!running || !transitionValidated(id)) return 0;
            const auto t = transitionTrue(id);
            return t && *t ? 2 : 1;
        };
        auto inkOfTransition = [&](int id) {
            switch (transitionState(id)) {
            case 2: return green;
            case 1: return yellow;
            default: return wire;
            }
        };

        // ---- les liaisons ---------------------------------------------------------
        for (const auto& s : diagram_.segments) {
            const gfx::Color ink = s.transition >= 0 ? inkOfTransition(s.transition) : wire;
            const auto a = toScreen(s.a), b = toScreen(s.b);
            ctx.r.line(a, b, ink, w1);
            if (s.arrowDown) arrowHead(ctx.r, b, true, 7.f * zoom_, ink);
            if (s.arrowUp) {
                // une liaison qui remonte : la fleche a mi-chemin, vers le haut
                const gfx::Point mid{ (a.x + b.x) / 2.f, (a.y + b.y) / 2.f };
                arrowHead(ctx.r, mid, false, 7.f * zoom_, ink);
            }
        }

        // ---- divergences et convergences : OU un trait, ET deux traits -----------
        for (const auto& j : diagram_.junctions) {
            const gfx::Color ink = j.isAnd ? inkOfTransition(j.at) : wire;
            const auto a = toScreen(gfx::Point{ j.x0, j.y }), b = toScreen(gfx::Point{ j.x1, j.y });
            if (j.isAnd) {
                const float pad = 8.f * zoom_;
                ctx.r.line({ a.x - pad, a.y }, { b.x + pad, b.y }, ink, w1);
                ctx.r.line({ a.x - pad, a.y + 3.f * zoom_ }, { b.x + pad, b.y + 3.f * zoom_ }, ink, w1);
            }
            else {
                ctx.r.line(a, b, ink, w1);
            }
        }

        // ---- les transitions --------------------------------------------------------
        bars_.clear();
        for (const auto& t : diagram_.transitions) {
            const int state = transitionState(t.id);
            const gfx::Color ink = state == 2 ? green : state == 1 ? yellow : c.text;
            const auto bar = toScreen(t.bar);
            const float thick = std::max(3.f, (state == 2 ? 6.f : 4.f) * zoom_);
            ctx.r.fillRect({ bar.x, bar.y + bar.h / 2.f - thick / 2.f, bar.w, thick }, ink);

            const auto hit = toScreen(t.hit);
            bars_.push_back(BarBox{ t.id, hit });

            if (t.idBox.w > 0.f) {
                const auto idBox = toScreen(t.idBox);
                const float tw = ctx.r.measure(t.idText, fSmall).width;
                ctx.r.drawText({ idBox.right() - tw, idBox.y + (idBox.h - lhSmall) / 2.f }, t.idText, fSmall, c.textMuted);
            }
            const auto* check = checkOn('T', t.id);
            const gfx::Color labelInk = check && check->severe ? red
                : state == 2 ? green : state == 1 ? yellow : c.text;
            if (t.label.w > 0.f) {
                const auto label = toScreen(t.label);
                float y = label.y;
                if (!t.tag.empty()) {
                    ctx.r.drawText({ label.x, y }, t.tag, fSmall, c.textMuted);
                    y += 16.f * zoom_;
                }
                const std::string text = t.labelText.empty() ? std::string("?") : t.labelText;
                ctx.r.drawText({ label.x, y + (16.f * zoom_ - lhText) / 2.f }, text, fText, labelInk);
            }
            const bool selected = selectedPart_ == GrafcetPart::Transition && selectedId_ == t.id;
            if (selected) ctx.r.strokeRect(hit.inset(-3.f, -2.f), c.accent, 1.5f);
            if (wireTransition_ == t.id) ctx.r.strokeRect(hit.inset(-3.f, -2.f), yellow, 2.f);
            if (hoveredTransition_ == t.id && !selected) ctx.r.strokeRect(hit.inset(-3.f, -2.f), c.border, 1.f);
            if (check) dashedRect(ctx.r, hit.inset(-4.f, -3.f), check->severe ? red : yellow, 1.f);
        }

        // ---- les renvois ------------------------------------------------------------
        for (const auto& ref : diagram_.refs) {
            const auto box = toScreen(ref.box);
            ctx.r.drawText({ box.x, box.y + (box.h - lhSmall) / 2.f }, ref.text, fSmall,
                ref.outgoing ? c.text : c.textMuted);
        }

        // ---- les etapes -------------------------------------------------------------
        stepBoxes_.clear();
        for (const auto& s : diagram_.steps) {
            const auto box = toScreen(s.box);
            StepBox sb;
            sb.id = s.id;
            sb.rect = box;
            if (const auto a = stepActive(s.id)) sb.active = *a;
            stepBoxes_.push_back(sb);

            const bool selected = selectedPart_ == GrafcetPart::Step && selectedId_ == s.id;
            const auto* check = checkOn('X', s.id);
            const gfx::Color fill = sb.active ? green.withAlpha(150) : c.panelBg;
            const gfx::Color frame = sb.active ? green : (check && check->severe) ? red : c.text;
            ctx.r.fillRect(box, fill);
            ctx.r.strokeRect(box, frame, std::max(1.f, 1.5f * zoom_));
            // l'etape initiale : le double carre de toute la norme
            if (s.initial)
                ctx.r.strokeRect(box.inset(4.f * zoom_, 4.f * zoom_), frame, std::max(1.f, 1.2f * zoom_));
            const std::string number = "X" + s.number;
            const float nw = ctx.r.measure(number, fNumber).width;
            const float lhNumber = ctx.r.lineHeight(fNumber);
            ctx.r.drawText({ box.x + (box.w - nw) / 2.f, box.y + (box.h - lhNumber) / 2.f }, number, fNumber,
                sb.active ? c.selectionText : c.text);

            // sous l'etape : son nom s'il differe, "finale"
            float under = box.bottom() + 2.f * zoom_;
            if (!s.name.empty()) {
                const float tw = ctx.r.measure(s.name, fSmall).width;
                ctx.r.drawText({ box.x + (box.w - tw) / 2.f, under }, s.name, fSmall, c.textMuted);
                under += lhSmall;
            }
            if (s.isFinal) {
                const char* fin = "finale";
                const float tw = ctx.r.measure(fin, fSmall).width;
                ctx.r.drawText({ box.x + (box.w - tw) / 2.f, box.bottom() - lhSmall - 1.f }, fin, fSmall,
                    sb.active ? c.selectionText : c.textMuted);
            }
            // depuis combien de temps elle est active : le chiffre qu'on regarde
            if (running && sb.active) {
                const auto elapsed = stepActiveTime(s.id);
                if (!elapsed.empty())
                    ctx.r.drawText({ box.right() - ctx.r.measure(elapsed, fSmall).width, box.y - lhSmall - 1.f },
                        elapsed, fSmall, green);
            }
            if (check) {
                dashedRect(ctx.r, box.inset(-4.f * zoom_, -4.f * zoom_), check->severe ? red : yellow, 1.f);
                std::string why;
                for (const auto& k : checks_)
                    if (k.part == 'X' && k.id == s.id)
                        why += (why.empty() ? "" : " \xC2\xB7 ") + std::string(grafcet::checkTitle(k.kind));
                ctx.r.drawText({ box.x - 4.f * zoom_, under + 4.f * zoom_ }, why, fSmall, check->severe ? red : yellow);
            }
            if (selected) ctx.r.strokeRect(box.inset(-2.f, -2.f), c.accent, 2.f);
            else if (s.id == hoveredStep_) ctx.r.strokeRect(box.inset(-2.f, -2.f), c.border, 1.f);
            if (wireStep_ == s.id) ctx.r.strokeRect(box.inset(-3.f, -3.f), yellow, 2.f);
        }

        // ---- les actions : un rectangle a droite de l'etape -----------------------
        chips_.clear();
        for (const auto& a : diagram_.actions) {
            const auto box = toScreen(a.box);
            const auto qbox = toScreen(a.qualifierBox);
            chips_.push_back(BarBox{ a.id, box });
            const auto runningNow = actionRunning(a.id);
            const bool on = runningNow && *runningNow;
            const auto* check = checkOn('A', a.id);
            ctx.r.fillRect(box, c.panelBg);
            if (on) ctx.r.fillRect(qbox, green.withAlpha(170));
            ctx.r.strokeRect(box, on ? green : c.textMuted, std::max(1.f, 1.2f * zoom_));
            ctx.r.line({ qbox.right(), qbox.y }, { qbox.right(), qbox.bottom() }, on ? green : c.textMuted,
                std::max(1.f, 1.2f * zoom_));
            const float qw = ctx.r.measure(a.qualifier, fText).width;
            ctx.r.drawText({ qbox.x + (qbox.w - qw) / 2.f, qbox.y + (qbox.h - lhText) / 2.f }, a.qualifier, fText,
                on ? c.selectionText : c.text);
            const float tx = qbox.right() + 6.f * zoom_;
            ctx.r.drawText({ tx, box.y + 2.f * zoom_ }, a.text, fText, c.text);
            std::string kind = a.kindText;
            if (on) kind += " \xC2\xB7 en cours";
            ctx.r.drawText({ tx, box.y + 2.f * zoom_ + 16.f * zoom_ }, kind, fSmall, on ? green : c.textMuted);
            if (selectedPart_ == GrafcetPart::Action && selectedId_ == a.id)
                ctx.r.strokeRect(box.inset(-2.f, -2.f), c.accent, 2.f);
            if (check) dashedRect(ctx.r, box.inset(-4.f, -3.f), check->severe ? red : yellow, 1.f);
        }

        // ---- la consigne d'un outil a deux clics ----------------------------------
        std::string hint;
        if (tool_ == GrafcetTool::Wire)
            hint = wireTransition_ >= 0 ? "Relier : clique l'\xC3\xA9tape d'arriv\xC3\xA9" "e de T" + std::to_string(wireTransition_) + " \xC2\xB7 \xC3\x89" "chap pour annuler"
                 : wireStep_ >= 0 ? "Relier : clique la transition \xC3\xA0 relier \xC3\xA0 X" + std::to_string(wireStep_) + " \xC2\xB7 \xC3\x89" "chap pour annuler"
                 : "Relier : clique une transition puis une \xC3\xA9tape (ou l'inverse)";
        else if (tool_ == GrafcetTool::AddTransition)
            hint = "Transition : clique l'\xC3\xA9tape de d\xC3\xA9part (ou le vide pour une transition seule)";
        else if (tool_ == GrafcetTool::AddAction)
            hint = "Action : clique l'\xC3\xA9tape qui la porte";
        else if (tool_ == GrafcetTool::AddStep)
            hint = "\xC3\x89tape : clique une \xC3\xA9tape pour ins\xC3\xA9rer avant elle, le vide pour l'ajouter \xC3\xA0 la fin";
        else if (tool_ == GrafcetTool::Remove)
            hint = "Supprimer : clique ce qui doit partir (on te le demande d'abord)";
        if (exportPending_) {
            exportPending_ = false;
            std::string folder = dossiers::actif(dossiers::Cle::Captures);
            if (folder.empty()) folder = "captures";
            char stamp[32]{};
            const std::time_t now = std::time(nullptr);
            if (const std::tm* tm = std::localtime(&now)) std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", tm);
            const std::string path = folder + "/Grafcet_" + chart_.name + "_" + stamp + ".png";
            const auto status = captureRegionToPng(ctx.r, path, static_cast<int>(area.x), static_cast<int>(area.y),
                                                   static_cast<int>(area.w), static_cast<int>(area.h));
            zoom_ = keepZoom;
            scrollX_ = keepX;
            scrollY_ = keepY;
            ctx.r.popClip();
            exported->emit(path, status.has_value());
            invalidate();
            return;
        }

        // ---- le bouton Exporter PNG, en bas a gauche ------------------------------
        {
            const auto f = ctx.theme.font.ui;
            const std::string label = "Exporter PNG";
            const float lh = ctx.r.lineHeight(f);
            exportButton_ = { area.x + 8.f, area.bottom() - lh - 16.f, ctx.r.measure(label, f).width + 16.f, lh + 8.f };
            if (searching_) exportButton_.y -= lh + 20.f;
            ctx.r.fillRect(exportButton_, c.headerBg);
            ctx.r.strokeRect(exportButton_, c.border, 1.f);
            ctx.r.drawText({ exportButton_.x + 8.f, exportButton_.y + 4.f }, label, f, c.text);
        }

        // ---- la mini-carte, quand le grafcet depasse nettement la vue ------------
        miniMap_ = {};
        if (diagram_.size.w * zoom_ > area.w * 1.3f || diagram_.size.h * zoom_ > area.h * 1.3f) {
            const float maxW = 170.f, maxH = 130.f;
            miniScale_ = std::min(maxW / std::max(1.f, diagram_.size.w), maxH / std::max(1.f, diagram_.size.h));
            const float w = diagram_.size.w * miniScale_, h = diagram_.size.h * miniScale_;
            miniMap_ = { area.right() - w - 12.f, area.bottom() - h - 12.f, w, h };
            ctx.r.fillRect(miniMap_.inset(-4.f, -4.f), c.headerBg);
            ctx.r.strokeRect(miniMap_.inset(-4.f, -4.f), c.border, 1.f);
            for (const auto& st : diagram_.steps) {
                const gfx::Rect r{ miniMap_.x + st.box.x * miniScale_, miniMap_.y + st.box.y * miniScale_,
                                   std::max(2.f, st.box.w * miniScale_), std::max(2.f, st.box.h * miniScale_) };
                bool on = false;
                if (const auto a = stepActive(st.id)) on = *a;
                ctx.r.fillRect(r, on ? green : c.textMuted);
            }
            const gfx::Rect view{ miniMap_.x + (scrollX_ / zoom_) * miniScale_, miniMap_.y + (scrollY_ / zoom_) * miniScale_,
                                  (area.w / zoom_) * miniScale_, (area.h / zoom_) * miniScale_ };
            ctx.r.strokeRect(view.intersect(miniMap_.inset(-4.f, -4.f)), c.accent, 1.5f);
        }

        // ---- chercher (Ctrl+F) ----------------------------------------------------
        if (searching_) {
            const auto f = ctx.theme.font.ui;
            const float lh = ctx.r.lineHeight(f);
            std::string text = "Chercher : " + search_ + "_";
            if (!search_.empty())
                text += searchHits_.empty() ? std::string("   (rien)")
                      : "   (" + std::to_string(searchIndex_ + 1) + "/" + std::to_string(searchHits_.size())
                        + " \xC2\xB7 Entr\xC3\xA9" "e : suivant \xC2\xB7 \xC3\x89" "chap)";
            const float tw = ctx.r.measure(text, f).width;
            const gfx::Rect box{ area.x + 8.f, area.bottom() - lh - 22.f, tw + 20.f, lh + 12.f };
            ctx.r.fillRect(box, c.headerBg);
            ctx.r.strokeRect(box, c.accent, 1.5f);
            ctx.r.drawText({ box.x + 10.f, box.y + 6.f }, text, f, searchHits_.empty() && !search_.empty() ? red : c.text);
        }

        // ---- en simulation : la barre des commandes du moteur, et la confirmation
        engineButtons_.clear();
        confirmRect_ = cancelRect_ = {};
        if (running) {
            trackHistory();
            const auto f = ctx.theme.font.ui;
            const float lh = ctx.r.lineHeight(f);
            std::vector<std::pair<EngineCommand, std::string>> items = {
                { EngineCommand::Init, "Initialiser" },
                { EngineCommand::ResetAll, "Tout r\xC3\xA9initialiser" },
                { EngineCommand::ForceBack, "\xE2\x86\x90 Pr\xC3\xA9" "c\xC3\xA9" "dente" },
                { EngineCommand::ForceNext, "Suivante \xE2\x86\x92" },
            };
            if (selectedPart_ == GrafcetPart::Step && selectedId_ >= 0)
                items.push_back({ EngineCommand::ForceSituation, "Forcer X" + std::to_string(selectedId_) });
            float x = area.right() - 8.f;
            const float y = area.y + 6.f;
            for (auto it = items.rbegin(); it != items.rend(); ++it) {
                const float w = ctx.r.measure(it->second, f).width + 16.f;
                x -= w;
                const gfx::Rect b{ x, y, w, lh + 8.f };
                ctx.r.fillRect(b, c.headerBg);
                ctx.r.strokeRect(b, it->first == pendingCmd_ ? yellow : c.border, 1.f);
                ctx.r.drawText({ b.x + 8.f, b.y + 4.f }, it->second, f, c.text);
                engineButtons_.push_back({ it->first, b });
                x -= 4.f;
            }
            if (pendingCmd_ != EngineCommand::None) {
                // deux lignes : ce que le moteur va faire, puis la commande et les boutons
                const std::string text = engineText(pendingCmd_);
                const auto cut = text.find(" (");
                const std::string first = text.substr(0, cut);
                const std::string second = cut == std::string::npos ? std::string{} : text.substr(cut + 1);
                const float bw = 84.f;
                const float w = std::min(area.w - 16.f, std::max(ctx.r.measure(first, f).width + 16.f,
                                                                 ctx.r.measure(second, f).width + 2.f * bw + 40.f));
                const gfx::Rect strip{ area.x + 8.f, y + lh + 14.f, w, 2.f * lh + 22.f };
                ctx.r.fillRect(strip, c.headerBg);
                ctx.r.strokeRect(strip, yellow, 1.5f);
                ctx.r.pushClip(strip);
                ctx.r.drawText({ strip.x + 8.f, strip.y + 6.f }, first, f, c.text);
                ctx.r.drawText({ strip.x + 8.f, strip.y + lh + 14.f }, second, f, c.textMuted);
                ctx.r.popClip();
                confirmRect_ = { strip.right() - 2.f * bw - 12.f, strip.y + lh + 9.f, bw, lh + 8.f };
                cancelRect_ = { strip.right() - bw - 6.f, strip.y + lh + 9.f, bw, lh + 8.f };
                ctx.r.fillRect({ confirmRect_.x - 6.f, confirmRect_.y, 2.f * bw + 18.f, confirmRect_.h }, c.headerBg);
                ctx.r.fillRect(confirmRect_, yellow.withAlpha(200));
                ctx.r.drawText({ confirmRect_.x + 8.f, confirmRect_.y + (confirmRect_.h - lh) / 2.f }, "Confirmer", f, c.windowBg);
                ctx.r.strokeRect(cancelRect_, c.border, 1.f);
                ctx.r.drawText({ cancelRect_.x + 8.f, cancelRect_.y + (cancelRect_.h - lh) / 2.f }, "Annuler", f, c.text);
            }
        } else if (pendingCmd_ != EngineCommand::None) {
            pendingCmd_ = EngineCommand::None;
        }

        if (!hint.empty()) {
            const float tw = ctx.r.measure(hint, ctx.theme.font.ui).width;
            const gfx::Rect pill{ area.x + (area.w - tw) / 2.f - 10.f, area.y + 8.f, tw + 20.f,
                                  ctx.r.lineHeight(ctx.theme.font.ui) + 8.f };
            ctx.r.fillRect(pill, c.headerBg);
            ctx.r.strokeRect(pill, c.accent, 1.f);
            ctx.r.drawText({ pill.x + 10.f, pill.y + 4.f }, hint, ctx.theme.font.ui, c.text);
        }

        ctx.r.popClip();
    }

    // L'infobulle : ce qu'il y a sous la souris, et en simulation la valeur des
    // variables d'une receptivite.
    std::string GrafcetView::liveTooltip(gfx::Point mouse) const {
        const int t = transitionAt(mouse);
        if (t >= 0) {
            const auto* tr = grafcet::transitionById(chart_, t);
            if (!tr) return {};
            std::string out = "T" + std::to_string(t) + " : " + grafcet::drawnCondition(chart_, *tr);
            if (runtime_) {
                const auto state = transitionTrue(t);
                out += "\n" + std::string(transitionValidated(t) ? "valid\xC3\xA9" "e" : "non valid\xC3\xA9" "e")
                     + (state ? (*state ? " \xC2\xB7 vraie" : " \xC2\xB7 fausse") : "");
                int shown = 0;
                for (const auto& path : grafcet::variablePaths(tr->conditionExpr)) {
                    sim::Value v;
                    if (!(!chart_.scope.empty() && runtime_->get(chart_.scope + path, v)) && !runtime_->get(path, v)) continue;
                    out += "\n" + path + " = " + v.display();
                    if (++shown >= 8) break;
                }
            }
            return out;
        }
        const int s = stepAt(mouse);
        if (s >= 0) {
            const auto* step = chart_.stepById(s);
            if (!step) return {};
            std::string out = "X" + std::to_string(s);
            if (!step->name.empty() && step->name != out) out += " \xC2\xB7 " + step->name;
            if (step->initial) out += " \xC2\xB7 initiale";
            if (step->isFinal) out += " \xC2\xB7 finale";
            if (const auto a = stepActive(s)) out += *a ? "\nactive depuis " + stepActiveTime(s) : "\ninactive";
            for (const auto& k : checks_)
                if (k.part == 'X' && k.id == s) out += "\n" + k.message;
            return out;
        }
        const int a = actionAt(mouse);
        if (a >= 0) {
            const auto* act = grafcet::actionById(chart_, a);
            if (!act) return {};
            return "A" + std::to_string(a) + " " + act->name + " \xC2\xB7 " + grafcet::kindLabel(act->kind, act->delay)
                 + " (" + grafcet::kindCode(act->kind) + ")\n" + std::string(grafcet::kindMeaning(act->kind));
        }
        return {};
    }

    // ---------------------------------------------------------------------------
    EventResult GrafcetView::onEvent(const InputEvent& ev) {
        if (const auto* w = std::get_if<MouseWheel>(&ev)) {
            if (!bounds().contains(w->pos)) return EventResult::Ignored;
            if (w->mods.ctrl) {
                // le zoom autour de la souris
                const gfx::Point at = toChart(w->pos);
                const float next = std::clamp(zoom_ * (w->dy > 0.f ? 1.1f : 1.f / 1.1f), 0.3f, 3.f);
                zoom_ = next;
                scrollX_ = at.x * zoom_ - (w->pos.x - bounds().x);
                scrollY_ = at.y * zoom_ - (w->pos.y - bounds().y);
                clampScroll();
                fitPending_ = false;
                zoomChanged->emit(zoom_);
                invalidate();
                return EventResult::Consumed;
            }
            if (w->mods.shift) scrollX_ -= w->dy * 40.f;
            else { scrollY_ -= w->dy * 40.f; scrollX_ -= w->dx * 40.f; }
            clampScroll();
            invalidate();
            return EventResult::Consumed;
        }

        if (const auto* t = std::get_if<TextInput>(&ev)) {
            if (!searching_) return EventResult::Ignored;
            setSearch(search_ + t->utf8);
            return EventResult::Consumed;
        }
        if (const auto* k = std::get_if<KeyDown>(&ev)) {
            if (k->key == Key::F && k->mods.ctrl) { startSearch(); return EventResult::Consumed; }
            if (searching_) {
                if (k->key == Key::Escape) { searching_ = false; invalidate(); return EventResult::Consumed; }
                if (k->key == Key::Return || k->key == Key::F3) { nextSearchHit(); return EventResult::Consumed; }
                if (k->key == Key::Backspace) {
                    std::string text = search_;
                    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
                    if (!text.empty()) text.pop_back();
                    setSearch(std::move(text));
                    return EventResult::Consumed;
                }
            }
            if (k->key == Key::F3) { nextSearchHit(); return EventResult::Consumed; }
            if (k->key == Key::Escape) {
                if (pendingCmd_ != EngineCommand::None) { cancelEngine(); return EventResult::Consumed; }
                if (wireTransition_ >= 0 || wireStep_ >= 0) { wireTransition_ = wireStep_ = -1; invalidate(); return EventResult::Consumed; }
                return EventResult::Ignored;
            }
            if (k->key == Key::Delete && selectedPart_ != GrafcetPart::None && selectedId_ >= 0) {
                removeRequested->emit(selectedPart_, selectedId_);
                return EventResult::Consumed;
            }
            if (k->key == Key::F2 && selectedPart_ != GrafcetPart::None && selectedId_ >= 0) {
                editRequested->emit(selectedPart_, selectedId_);
                return EventResult::Consumed;
            }
            return EventResult::Ignored;
        }

        if (const auto* m = std::get_if<MouseMove>(&ev)) {
            if (draggingStep_ >= 0) {
                // en coordonnees du dessin, aimantees : l'etape reste ou on l'a mise
                // quand on zoome ou qu'on se deplace ensuite
                gfx::Point p = toChart({ m->pos.x - dragOffset_.x, m->pos.y - dragOffset_.y });
                p.x = std::round(p.x / kSnap) * kSnap;
                p.y = std::round(p.y / kSnap) * kSnap;
                pending_.steps[draggingStep_] = p;
                dragged_ = true;
                dirty_ = true;
                invalidate();
                return EventResult::Consumed;
            }
            if (panning_) {
                scrollX_ -= m->pos.x - panFrom_.x;
                scrollY_ -= m->pos.y - panFrom_.y;
                panFrom_ = m->pos;
                clampScroll();
                invalidate();
                return EventResult::Consumed;
            }
            const int overStep = stepAt(m->pos);
            const int overBar = overStep >= 0 ? -1 : transitionAt(m->pos);
            if (overStep != hoveredStep_ || overBar != hoveredTransition_) {
                hoveredStep_ = overStep;
                hoveredTransition_ = overBar;
                invalidate();
            }
            return EventResult::Ignored;   // le survol ne garde pas le pointeur
        }

        if (std::get_if<MouseUp>(&ev)) {
            if (panning_) { panning_ = false; return EventResult::Consumed; }
            if (draggingStep_ >= 0) {
                const bool moved = dragged_;
                draggingStep_ = -1;
                dragged_ = false;
                // annonce seulement si quelque chose a bouge : un clic n'est pas une
                // modification, et le fichier ne doit pas etre reecrit a chaque clic
                if (moved) placementChanged->emit();
                return EventResult::Consumed;
            }
            return EventResult::Ignored;
        }

        if (const auto* d = std::get_if<MouseDown>(&ev)) {
            if (!bounds().contains(d->pos)) return EventResult::Ignored;
            grabFocus();

            if (d->button == MouseButton::Middle) {
                panning_ = true;
                panFrom_ = d->pos;
                return EventResult::Consumed;
            }
            if (exportButton_.w > 0.f && exportButton_.contains(d->pos)) { exportPng(); return EventResult::Consumed; }
            // la mini-carte : un clic y amene la vue
            if (miniMap_.w > 0.f && miniMap_.inset(-4.f, -4.f).contains(d->pos)) {
                const float cx = (d->pos.x - miniMap_.x) / miniScale_, cy = (d->pos.y - miniMap_.y) / miniScale_;
                scrollX_ = cx * zoom_ - bounds().w / 2.f;
                scrollY_ = cy * zoom_ - bounds().h / 2.f;
                clampScroll();
                invalidate();
                return EventResult::Consumed;
            }
            // en simulation : la confirmation, puis la barre des commandes du moteur
            if (pendingCmd_ != EngineCommand::None) {
                if (confirmRect_.contains(d->pos)) { (void)confirmEngine(); return EventResult::Consumed; }
                if (cancelRect_.contains(d->pos)) { cancelEngine(); return EventResult::Consumed; }
            }
            for (const auto& [cmd, rect] : engineButtons_)
                if (rect.contains(d->pos)) { askEngine(cmd); return EventResult::Consumed; }

            const int step = stepAt(d->pos);
            const int bar = step >= 0 ? -1 : transitionAt(d->pos);
            const int action = (step >= 0 || bar >= 0) ? -1 : actionAt(d->pos);

            switch (tool_) {
            case GrafcetTool::Wire: {
                // toujours transition -> etape, dans un ordre ou dans l'autre : deux
                // etapes ne se relient jamais directement en GRAFCET
                if (bar >= 0)  wireTransition_ = bar;
                else if (step >= 0) wireStep_ = step;
                if (wireTransition_ >= 0 && wireStep_ >= 0) {
                    const int t = wireTransition_, x = wireStep_;
                    wireTransition_ = wireStep_ = -1;
                    linkRequested->emit(t, x);
                }
                invalidate();
                return EventResult::Consumed;
            }
            case GrafcetTool::Remove:
                if (step >= 0) removeRequested->emit(GrafcetPart::Step, step);
                else if (bar >= 0) removeRequested->emit(GrafcetPart::Transition, bar);
                else if (action >= 0) removeRequested->emit(GrafcetPart::Action, action);
                return EventResult::Consumed;

            case GrafcetTool::AddStep:
            case GrafcetTool::AddTransition:
            case GrafcetTool::AddAction:
                insertRequested->emit(tool_, step >= 0 ? step : bar);
                return EventResult::Consumed;

            case GrafcetTool::Select:
                break;
            }

            if (step >= 0) {
                select(GrafcetPart::Step, step);
                stepSelected->emit(step);
                if (d->clickCount >= 2) { editRequested->emit(GrafcetPart::Step, step); }
                else if (d->button == MouseButton::Left) {
                    draggingStep_ = step;
                    dragged_ = false;
                    const auto* s = diagram_.step(step);
                    const auto rect = s ? toScreen(s->box) : gfx::Rect{ d->pos.x, d->pos.y, 0.f, 0.f };
                    dragOffset_ = { d->pos.x - rect.x, d->pos.y - rect.y };
                }
                return EventResult::Consumed;
            }
            if (bar >= 0) {
                select(GrafcetPart::Transition, bar);
                transitionSelected->emit(bar);
                if (d->clickCount >= 2) editRequested->emit(GrafcetPart::Transition, bar);
                return EventResult::Consumed;
            }
            if (action >= 0) {
                select(GrafcetPart::Action, action);
                actionSelected->emit(action);
                if (d->clickCount >= 2) editRequested->emit(GrafcetPart::Action, action);
                return EventResult::Consumed;
            }
            select(GrafcetPart::None, -1);
            nothingSelected->emit();
            // glisser le fond : se deplacer dans le dessin
            panning_ = true;
            panFrom_ = d->pos;
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }

} // namespace app
