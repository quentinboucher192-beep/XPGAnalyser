// =============================================================================
//  app/ai/AiMissionStrip.cpp - 1.22.0 : la mission active dans le panneau, << Creer une mission IA ? >> (voir le .hpp)
// =============================================================================
#include "AiMissionStrip.hpp"
#include "../../core/Perf.hpp"   // 1.22.2 : ce qui consomme (l'onglet Performances)

#include "AiNotifications.hpp"   // notifs::oneLine
#include "AiPanel.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app::ai {

namespace {

using missionui::Op;
using missionui::Tone;

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
const gfx::FontId kBtn{16};
constexpr float kHeadH = 28.f;   // la ligne du haut : << Mission IA >>, le nom, l'etat
constexpr float kBarH = 18.f;    // la progression globale
constexpr float kRowH = 18.f;    // une ligne (la prochaine action, les propositions, une erreur)
constexpr float kBtnH = 28.f;
constexpr std::size_t kMaxProblems = 2;

void put(std::vector<Op>& ops, Op::Kind k, gfx::Rect r, std::string t, gfx::FontId f, Tone tone, int a = 255, float frac = 0.f,
         ui::Icon icon = ui::Icon::None) {
    Op op;
    op.kind = k;
    op.r = r;
    op.text = std::move(t);
    op.font = f;
    op.tone = tone;
    op.alpha = a;
    op.frac = frac;
    op.icon = icon;
    ops.push_back(std::move(op));
}

float buttonWidth(const std::string& label) { return ui::measureWidth(label, kBtn) + 28.f; }

} // namespace

// ============================================================================ la bande ==
AiMissionStrip::AiMissionStrip(std::string id, AiPanel& panel) : ui::Widget(std::move(id)), panel_(panel) {
    const std::string base = this->id();
    action_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Proposer la suite", base + ".action")));
    action_->setStyle(ui::Button::Style::Primary);
    board_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Tableau de bord", base + ".tableau")));
    board_->setStyle(ui::Button::Style::Flat);
    board_->setTooltip("Le tableau de bord de la mission, au centre : le plan, les propositions, l'historique.");
    links_ += action_->clicked->connect([this] { doAction(); });
    links_ += board_->clicked->connect([this] { (void)openBoard(); });
    action_->setVisibility(ui::Visibility::Collapsed);
    board_->setVisibility(ui::Visibility::Collapsed);
}

bool AiMissionStrip::buttonsOnTwoRows(float width) const {
    const float aw = next_.kind != missionui::Next::Kind::None ? buttonWidth(action_->text()) + 6.f : 0.f;
    return aw + buttonWidth(board_->text()) > width - 20.f;
}

int AiMissionStrip::momentRows(bool roomy) const {
    if (moment_.empty()) return 0;
    return moment_.step.empty() || !roomy ? 1 : 2;   // l'etape et son activite sur deux lignes s'il y a la place, sinon une
}

float AiMissionStrip::openHeight(float width, int rows) const {
    float h = kHeadH + kBarH + 4.f;
    h += static_cast<float>(rows) * kRowH;                                        // 1.22.1 : l'etape du moment, ce qu'elle fait
    h += kRowH;                                                                   // la prochaine action
    if (!proposals_.empty()) h += kRowH;                                          // les propositions
    h += static_cast<float>(std::min(problems_.size(), kMaxProblems)) * kRowH;   // les erreurs, les blocages
    h += 6.f + kBtnH + (buttonsOnTwoRows(width) ? kBtnH + 6.f : 0.f) + 8.f;
    return h;
}

float AiMissionStrip::heightFor(float width, float maxH) const {
    if (missionId_.empty()) return 0.f;
    const float closed = kHeadH + kBarH + 4.f;
    float h = closed;
    if (open_) {
        h = openHeight(width, momentRows(true));
        if (h > maxH) h = openHeight(width, momentRows(false));   // a l'etroit : l'etape et son activite sur une ligne
    }
    return std::min(h, std::max(closed, maxH));
}

void AiMissionStrip::setExpanded(bool open) {
    if (open_ == open) return;
    open_ = open;
    ++rev_;
    if (parent()) parent()->invalidateLayout();
    invalidateLayout();
    invalidate();
}

bool AiMissionStrip::poll() {
    const auto& as = panel_.assistant();
    const mission::Mission* m = as.activeMission();
    const std::string id = m ? m->id : std::string{};
    const bool busy = m && panel_.missionBusy(*m);
    const std::int64_t key = m ? missionui::liveKey(missionui::liveOf(as, *m)) : 0;   // 1.22.1 : le direct, chaque seconde au plus
    if (id == missionId_ && as.missionsRevision() == seenRev_ && as.generating() == generating_ && busy == busy_ && key == liveKey_) return false;
    refresh();
    return true;
}

void AiMissionStrip::refresh() {
    XPG_MESURE("ia.mission.bande");   // 1.22.2
    const auto& as = panel_.assistant();
    seenRev_ = as.missionsRevision();
    generating_ = as.generating();
    ++rev_;
    const mission::Mission* m = as.activeMission();
    problems_.clear();
    proposals_.clear();
    if (!m) {
        missionId_.clear();
        name_.clear();
        progress_ = {};
        next_ = {};
        moment_ = {};
        liveKey_ = 0;
        busy_ = resume_ = false;
        action_->setVisibility(ui::Visibility::Collapsed);
        board_->setVisibility(ui::Visibility::Collapsed);
        invalidateLayout();
        invalidate();
        return;
    }
    missionId_ = m->id;
    name_ = missionui::title(*m);
    state_ = mission::stateText(*m);   // 1.22.3 : << Terminee (fin forcee) >>
    stateTone_ = missionui::stateTone(m->state);
    busy_ = panel_.missionBusy(*m);
    progress_ = mission::progressOf(*m);   // la progression REELLE
    count_ = std::to_string(progress_.done) + "/" + std::to_string(progress_.steps) + " \xC3\xA9tapes \xC2\xB7 " + std::to_string(progress_.percent()) + " %";
    nextText_ = mission::nextAction(*m);
    next_ = missionui::nextOf(*m, busy_);
    {
        const mission::Live live = missionui::liveOf(as, *m);   // 1.22.1 : l'etape du moment et ce qu'elle fait
        liveKey_ = missionui::liveKey(live);
        moment_ = missionui::momentOf(*m, live);
    }
    resume_ = missionui::wantsResume(*m, m->coherence, busy_);
    // les propositions de la mission (les quatre dernieres) et leur etat
    {
        std::vector<const mission::ProposalLink*> list;
        for (const auto& p : m->proposals) list.push_back(&p);
        std::stable_sort(list.begin(), list.end(), [](const auto* a, const auto* b) { return a->order < b->order; });
        const std::size_t from = list.size() > 4 ? list.size() - 4 : 0;
        for (std::size_t i = from; i < list.size(); ++i)
            proposals_ += (proposals_.empty() ? std::string("Propositions : ") + (from > 0 ? "\xE2\x80\xA6 " : "") : std::string(" \xC2\xB7 ")) + "P"
                          + std::to_string(list[i]->order) + " " + missionui::proposalStateLabel(list[i]->state);
    }
    // les erreurs et les blocages (et ce qui empeche de continuer)
    if (m->coherence == mission::Coherence::Missing) problems_.emplace_back("Projet introuvable : ouvre celui de la mission pour la reprendre.", Tone::Error);
    else if (m->coherence == mission::Coherence::Changed) problems_.emplace_back("Projet modifi\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde : actualise le contexte.", Tone::Warning);
    if (m->pending.active && !busy_) problems_.emplace_back("Appel \xC3\xA0 l'IA interrompu : rien n'est rejou\xC3\xA9 sans v\xC3\xA9rification.", Tone::Warning);
    for (const auto& s : m->steps) {
        if (s.state == mission::StepState::Error)
            problems_.emplace_back(s.id + " en erreur" + (s.error.empty() ? std::string{} : " : " + s.error), Tone::Error);
        else if (s.state == mission::StepState::Blocked)
            problems_.emplace_back(s.id + " bloqu\xC3\xA9" "e" + (s.note.empty() ? std::string{} : " : " + s.note), Tone::Warning);
    }
    // le geste du moment
    using K = missionui::Next::Kind;
    if (next_.kind != K::None) {
        const bool needsAi = next_.kind == K::Refresh || next_.kind == K::Correct || next_.kind == K::Continue;
        action_->setText(next_.kind == K::Resume ? std::string("Reprendre\xE2\x80\xA6") : next_.label);
        std::string tip = next_.kind == K::Resume ? std::string("L'\xC3\xA9" "cran de reprise de la mission, au centre : le constat d'abord, puis Reprendre.")
                                                  : next_.tooltip;
        if (needsAi && generating_) tip += "\n(Une r\xC3\xA9ponse est en cours : attends sa fin.)";
        action_->setTooltip(tip);
        action_->setEnabled(!(needsAi && generating_));
    }
    invalidateLayout();
    invalidate();
}

bool AiMissionStrip::openBoard() {
    if (missionId_.empty()) return false;
    return panel_.openMission(missionId_, resume_ ? 1 : 0);
}

void AiMissionStrip::doAction() {
    const missionui::Next n = next_;   // une copie : le geste refait la bande
    const std::string id = missionId_;
    using K = missionui::Next::Kind;
    switch (n.kind) {
        case K::Resume: (void)panel_.openMission(id, 1); break;   // le constat de reprise d'abord
        case K::Refresh: (void)panel_.missionAct(id, AiPanel::MissionAct::Refresh); break;
        case K::Review: (void)panel_.missionProposal(id, n.proposal, 1); break;
        case K::Correct: (void)panel_.missionAct(id, AiPanel::MissionAct::Correct, n.step); break;
        case K::Audit: (void)panel_.missionAct(id, AiPanel::MissionAct::Audit); break;
        case K::Continue: (void)panel_.missionAct(id, AiPanel::MissionAct::Continue); break;
        case K::None: break;
    }
}

gfx::Rect AiMissionStrip::headRect() const {
    const auto b = bounds();
    return {b.x, b.y, b.w, kHeadH};
}

gfx::Rect AiMissionStrip::toggleRect() const {
    const auto b = bounds();
    return {b.x + 4.f, b.y + 4.f, 22.f, kHeadH - 8.f};
}

void AiMissionStrip::layoutOps() {
    const float w = bounds().w;
    laidW_ = w;
    laidH_ = bounds().h;   // 1.22.1 : l'etape du moment tient sur une ou deux lignes selon la hauteur donnee
    laidRev_ = rev_;
    ops_.clear();
    tipsRel_.clear();
    if (missionId_.empty() || w <= 0.f) return;
    const float h = std::max(bounds().h, kHeadH + kBarH + 4.f);
    put(ops_, Op::Kind::Fill, {0.f, 0.f, w, h}, {}, kBody, Tone::Card);
    put(ops_, Op::Kind::Fill, {0.f, 0.f, w, kHeadH + kBarH}, {}, kBody, Tone::Accent, 20);
    put(ops_, Op::Kind::Fill, {0.f, 0.f, 3.f, h}, {}, kBody, stateTone_);   // le filet de l'etat
    put(ops_, Op::Kind::Fill, {0.f, h - 1.f, w, 1.f}, {}, kBody, Tone::Border);
    // la ligne du haut : replier, << Mission IA >>, le nom, l'etat
    put(ops_, Op::Kind::Icon, {9.f, 7.f, 14.f, 14.f}, {}, kBody, Tone::Muted, 255, 0.f, open_ ? ui::Icon::Collapse : ui::Icon::Expand);
    const std::string title = "Mission IA";
    const float ty = (kHeadH - ui::lineHeight(kBody)) / 2.f;
    put(ops_, Op::Kind::Bold, {28.f, ty, 0.f, 0.f}, title, kBody, Tone::Text);
    const float cw = ui::measureWidth(state_, kMeta) + 14.f;
    const float cx = w - 10.f - cw;
    put(ops_, Op::Kind::Chip, {cx, 5.f, cw, kHeadH - 10.f}, state_, kMeta, stateTone_);
    const float nx = 28.f + ui::measureWidth(title, kBody) + 10.f;
    const std::string name = missionui::elide(name_, kMeta, cx - 8.f - nx);
    put(ops_, Op::Kind::Text, {nx, (kHeadH - ui::lineHeight(kMeta)) / 2.f, 0.f, 0.f}, name, kMeta, Tone::Text);
    tipsRel_.emplace_back(gfx::Rect{28.f, 0.f, w - 28.f, kHeadH},
                          name_ + " \xE2\x80\x94 " + state_ + "\nUn clic : le tableau de bord de la mission (au centre)" + (resume_ ? ", l'\xC3\xA9" "cran de reprise d'abord." : "."));
    tipsRel_.emplace_back(gfx::Rect{4.f, 4.f, 22.f, kHeadH - 8.f}, open_ ? std::string("Replier la bande (le nom et la progression seulement)")
                                                                         : std::string("D\xC3\xA9plier la bande : la prochaine action, les propositions, les erreurs"));
    // la progression globale (mission::progressOf)
    const float pw = ui::measureWidth(count_, kMeta) + 10.f;
    const Tone barTone = stateTone_ == Tone::Ok ? Tone::Ok : stateTone_ == Tone::Error ? Tone::Error : Tone::Accent;
    put(ops_, Op::Kind::Bar, {10.f, kHeadH + 4.f, std::max(20.f, w - 20.f - pw), 8.f}, {}, kMeta, barTone, 255, static_cast<float>(progress_.global));
    put(ops_, Op::Kind::Text, {w - 10.f - pw + 8.f, kHeadH + (8.f + 8.f - ui::lineHeight(kMeta)) / 2.f, 0.f, 0.f}, count_, kMeta, Tone::Muted);
    tipsRel_.emplace_back(gfx::Rect{0.f, kHeadH, w, kBarH}, progress_.text() + "\n\n" + mission::progressRule());
    if (!open_) return;
    float y = kHeadH + kBarH + 4.f;
    // 1.22.1 : l'etape du moment (sa progression reelle) et ce qu'elle fait maintenant - sur deux lignes s'il y a la place,
    // sinon sur une (la petite barre, << S4 . 40 % - ce qu'elle fait >>)
    if (const int rows = momentRows(bounds().h + 0.5f >= openHeight(w, momentRows(true))); rows == 1 && !moment_.step.empty()) {
        const Tone actTone = missionui::activityTone(moment_.activity.kind);
        float tx = 10.f;
        if (moment_.progress.counted()) {
            const Tone barTone2 = moment_.activity.kind == mission::Activity::Error ? Tone::Error : moment_.activity.kind == mission::Activity::Done ? Tone::Ok : Tone::Info;
            put(ops_, Op::Kind::Bar, {tx, y + 5.f, 30.f, 6.f}, {}, kMeta, barTone2, 255, static_cast<float>(moment_.progress.frac()));
            tx += 36.f;
        }
        put(ops_, moment_.activity.live ? Op::Kind::Bold : Op::Kind::Text, {tx, y, 0.f, 0.f}, missionui::elide(moment_.line(), kMeta, w - 10.f - tx), kMeta, actTone);
        tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kRowH}, moment_.step + " " + moment_.title + " \xE2\x80\x94 " + moment_.progress.text() + " (" + moment_.progress.detail()
                                                               + ")\n" + moment_.activity.text + "\n\n" + mission::stepProgressRule());
        y += kRowH;
    } else if (rows > 0) {
        const Tone actTone = missionui::activityTone(moment_.activity.kind);
        if (!moment_.step.empty()) {
            const std::string pct = moment_.progress.text();
            const float vw = ui::measureWidth(pct, kMeta) + 4.f, bw = 46.f;
            const float right = w - 10.f;
            put(ops_, Op::Kind::Text, {right - vw + 4.f, y, 0.f, 0.f}, pct, kMeta, Tone::Text);
            if (moment_.progress.counted()) {
                const Tone barTone2 = moment_.activity.kind == mission::Activity::Error ? Tone::Error : moment_.activity.kind == mission::Activity::Done ? Tone::Ok : Tone::Info;
                put(ops_, Op::Kind::Bar, {right - vw - bw, y + 5.f, bw, 6.f}, {}, kMeta, barTone2, 255, static_cast<float>(moment_.progress.frac()));
            }
            const std::string head = "\xE2\x96\xB8 " + moment_.step + " " + moment_.title;
            put(ops_, Op::Kind::Bold, {10.f, y, 0.f, 0.f}, missionui::elide(head, kMeta, right - vw - bw - 18.f), kMeta, Tone::Text);
            tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kRowH}, moment_.step + " " + moment_.title + " \xE2\x80\x94 " + pct + " (" + moment_.progress.detail() + ")\n\n"
                                                                   + mission::stepProgressRule());
            y += kRowH;
        }
        put(ops_, moment_.activity.live ? Op::Kind::Bold : Op::Kind::Text, {10.f, y, 0.f, 0.f}, missionui::elide(moment_.activity.text, kMeta, w - 20.f), kMeta, actTone);
        tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kRowH}, moment_.activity.text + (moment_.step.empty() ? std::string{} : "\nSon journal : le tableau de bord (un clic sur sa ligne du plan)."));
        y += kRowH;
    }
    const std::string next = "Prochaine : " + nextText_;
    put(ops_, Op::Kind::Text, {10.f, y, 0.f, 0.f}, missionui::elide(next, kMeta, w - 20.f), kMeta, Tone::Text);
    tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kRowH}, next);
    y += kRowH;
    if (!proposals_.empty()) {
        put(ops_, Op::Kind::Text, {10.f, y, 0.f, 0.f}, missionui::elide(proposals_, kMeta, w - 20.f), kMeta, Tone::Muted);
        tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kRowH}, proposals_ + "\nLeurs cartes : le tableau de bord, ou la conversation de la mission.");
        y += kRowH;
    }
    for (std::size_t i = 0; i < problems_.size() && i < kMaxProblems; ++i) {
        std::string t = problems_[i].first;
        if (i + 1 == kMaxProblems && problems_.size() > kMaxProblems) t += " (+" + std::to_string(problems_.size() - kMaxProblems) + ")";
        put(ops_, Op::Kind::Text, {10.f, y, 0.f, 0.f}, missionui::elide(t, kMeta, w - 20.f), kMeta, problems_[i].second);
        tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kRowH}, problems_[i].first);
        y += kRowH;
    }
    buttonsY_ = y + 6.f;
    if (next_.kind == missionui::Next::Kind::None && !next_.why.empty()) tipsRel_.emplace_back(gfx::Rect{0.f, y, w, kBtnH + 6.f}, next_.why);
}

void AiMissionStrip::onLayout() {
    const auto b = bounds();
    if (std::fabs(b.w - laidW_) > 0.5f || std::fabs(b.h - laidH_) > 0.5f || laidRev_ != rev_) layoutOps();
    const bool shown = !missionId_.empty() && open_ && b.h > 0.f;
    const bool hasAction = shown && next_.kind != missionui::Next::Kind::None;
    float x = b.x + 10.f, y = b.y + buttonsY_;
    const auto place = [&](ui::Button& btn, bool on) {
        if (!on) {
            btn.setVisibility(ui::Visibility::Collapsed);
            return;
        }
        const float bw = std::min(b.w - 20.f, buttonWidth(btn.text()));
        if (x > b.x + 10.f && x + bw > b.right() - 10.f) {
            x = b.x + 10.f;
            y += kBtnH + 6.f;
        }
        btn.setBounds({x, y, bw, kBtnH});
        btn.setVisibility(y + kBtnH <= b.bottom() + 0.5f ? ui::Visibility::Visible : ui::Visibility::Hidden);
        x += bw + 6.f;
    };
    place(*action_, hasAction);
    place(*board_, shown);
}

std::vector<std::string> AiMissionStrip::lines() const {
    std::vector<std::string> out;
    if (missionId_.empty()) return out;
    out.push_back("Mission IA : " + name_ + " \xE2\x80\x94 " + state_);
    out.push_back(count_);
    if (open_) {
        if (!moment_.empty()) {
            if (!moment_.step.empty()) out.push_back(moment_.step + " " + moment_.title + " \xC2\xB7 " + moment_.progress.text());
            out.push_back(moment_.activity.text);
        }
        out.push_back("Prochaine : " + nextText_);
        if (!proposals_.empty()) out.push_back(proposals_);
        for (const auto& [t, tone] : problems_) out.push_back(t);
        if (next_.kind != missionui::Next::Kind::None) out.push_back("[" + action_->text() + "]");
    }
    return out;
}

void AiMissionStrip::onPaint(const ui::PaintContext& ctx) {
    XPG_MESURE("ia.peinture.bande-mission");   // 1.22.2
    tips_.clear();
    const auto b = bounds();
    if (missionId_.empty() || b.h <= 0.f) return;
    ctx.r.pushClip(b);
    missionui::paintOps(ctx, ops_, {b.x, b.y});
    ctx.r.popClip();
    for (const auto& [r, t] : tipsRel_) tips_.emplace_back(gfx::Rect{b.x + r.x, b.y + r.y, r.w, r.h}, t);
}

std::string AiMissionStrip::liveTooltip(gfx::Point mouse) const {
    for (auto it = tips_.rbegin(); it != tips_.rend(); ++it)
        if (it->first.contains(mouse)) return it->second;
    return {};
}

ui::EventResult AiMissionStrip::onEvent(const ui::InputEvent& ev) {
    if (missionId_.empty()) return ui::EventResult::Ignored;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        if (toggleRect().contains(d->pos)) setExpanded(!open_);
        else (void)openBoard();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ======================================================================== la carte de choix ==
namespace {
const char* kExplain = "Cette demande touche plusieurs parties du projet. Une mission IA la d\xC3\xA9" "coupe en \xC3\xA9tapes (un plan), te propose les "
                       "modifications une \xC3\xA0 une, suit ce qui est r\xC3\xA9" "ellement fait dans le projet et se reprend m\xC3\xAAme apr\xC3\xA8s la fermeture "
                       "du logiciel. Rien n'est envoy\xC3\xA9 tant que tu n'as pas choisi.";
constexpr float kAskPad = 10.f;
constexpr float kAskTitleH = 24.f;
} // namespace

AiMissionAsk::AiMissionAsk(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    create_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er la mission", base + ".creer")));
    create_->setStyle(ui::Button::Style::Primary);
    create_->setTooltip("La demande devient une mission IA : l'IA analyse d'abord le projet et propose un plan d'\xC3\xA9tapes ; rien n'est modifi\xC3\xA9 "
                        "avant ta validation. Son tableau de bord : l'onglet Missions IA.");
    continue_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Continuer en conversation", base + ".continuer")));
    continue_->setStyle(ui::Button::Style::Default);
    continue_->setTooltip("Envoyer la demande telle quelle dans la conversation (une r\xC3\xA9ponse, une proposition), sans mission.");
    links_ += create_->clicked->connect([this] { chosen->emit(1); });
    links_ += continue_->clicked->connect([this] { chosen->emit(2); });
}

void AiMissionAsk::setRequest(std::string text) {
    request_ = std::move(text);
    invalidateLayout();
    invalidate();
}

std::vector<std::string> AiMissionAsk::textLines(float width) const { return missionui::wrap(kExplain, kMeta, width - 2.f * kAskPad, 4); }

bool AiMissionAsk::twoRows(float width) const { return buttonWidth(create_->text()) + 6.f + buttonWidth(continue_->text()) > width - 2.f * kAskPad; }

float AiMissionAsk::heightFor(float width) const {
    const float lh = ui::lineHeight(kMeta) + 3.f;
    const float text = static_cast<float>(textLines(width).size()) * lh + lh;   // l'explication, puis la demande citee
    return 8.f + kAskTitleH + text + 8.f + kBtnH + (twoRows(width) ? kBtnH + 6.f : 0.f) + 10.f;
}

gfx::Rect AiMissionAsk::closeRect() const {
    const auto b = bounds();
    return {b.right() - 26.f, b.y + 8.f, 18.f, 18.f};
}

std::vector<std::string> AiMissionAsk::lines() const {
    std::vector<std::string> out{"Cr\xC3\xA9" "er une mission IA ?"};
    for (auto& l : textLines(bounds().w > 0.f ? bounds().w : 400.f)) out.push_back(std::move(l));
    out.push_back("\xC2\xAB " + notifs::oneLine(request_, 200) + " \xC2\xBB");
    return out;
}

void AiMissionAsk::onLayout() {
    const auto b = bounds();
    const float lh = ui::lineHeight(kMeta) + 3.f;
    laid_ = textLines(b.w);
    quote_ = missionui::elide("\xC2\xAB " + notifs::oneLine(request_, 200) + " \xC2\xBB", kMeta, b.w - 2.f * kAskPad);
    float y = b.y + 8.f + kAskTitleH + static_cast<float>(laid_.size()) * lh + lh + 8.f;
    float x = b.x + kAskPad;
    for (ui::Button* btn : {create_, continue_}) {
        const float bw = std::min(b.w - 2.f * kAskPad, buttonWidth(btn->text()));
        if (x > b.x + kAskPad && x + bw > b.right() - kAskPad) {
            x = b.x + kAskPad;
            y += kBtnH + 6.f;
        }
        btn->setBounds({x, y, bw, kBtnH});
        x += bw + 6.f;
    }
}

void AiMissionAsk::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    if (b.h <= 0.f) return;
    ctx.r.fillRoundedRect(b, c.accent.withAlpha(ctx.theme.isDark() ? 34 : 22), 6.f);
    ctx.r.strokeRect(b, c.accent.withAlpha(140), 1.f);
    ui::drawIcon(ctx.r, ui::Icon::Task, {b.x + kAskPad, b.y + 10.f, 16.f, 16.f}, c.accent);
    const std::string title = "Cr\xC3\xA9" "er une mission IA ?";
    const float ty = b.y + 8.f + (kAskTitleH - ctx.r.lineHeight(kBody)) / 2.f;
    ctx.r.drawText({b.x + kAskPad + 24.f, ty}, title, kBody, c.text);
    ctx.r.drawText({b.x + kAskPad + 24.8f, ty}, title, kBody, c.text);   // le gras du logiciel
    const auto x = closeRect();
    ui::drawIcon(ctx.r, ui::Icon::Close, {x.x + 2.f, x.y + 2.f, 14.f, 14.f}, c.textMuted);
    const float lh = ui::lineHeight(kMeta) + 3.f;
    float y = b.y + 8.f + kAskTitleH;
    for (const auto& l : laid_) {
        ctx.r.drawText({b.x + kAskPad, y}, l, kMeta, c.text);
        y += lh;
    }
    ctx.r.drawText({b.x + kAskPad, y}, quote_, kMeta, c.textMuted);
}

ui::EventResult AiMissionAsk::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && bounds().contains(d->pos)) {
        if (d->button == ui::MouseButton::Left && closeRect().contains(d->pos)) chosen->emit(0);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
