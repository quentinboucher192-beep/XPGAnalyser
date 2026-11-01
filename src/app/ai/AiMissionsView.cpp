// =============================================================================
//  app/ai/AiMissionsView.cpp - 1.22.0 : l'onglet Missions IA du panneau (voir le .hpp)
// =============================================================================
#include "AiMissionsView.hpp"

#include "AiNotifications.hpp"   // notifs::oneLine
#include "AiPanel.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numeric>

namespace app::ai {

namespace {

using missionui::Tone;

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
constexpr float kPad = 10.f;
constexpr float kBarH = 38.f;      // la recherche, le +
constexpr float kChipsH = 30.f;    // Toutes | En cours | Archivees
constexpr float kButtonH = 24.f;
constexpr float kLeft = 14.f;      // le texte d'une ligne commence apres le filet de l'etat
constexpr float kBtnH = 28.f;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

void boldText(gfx::IRenderer& r, gfx::Point at, std::string_view t, gfx::FontId f, gfx::Color c) {
    r.drawText(at, t, f, c);
    r.drawText({at.x + 0.6f, at.y}, t, f, c);
}

float lh(gfx::FontId f) { return ui::lineHeight(f) + 3.f; }

const char* kOpenLabel = "Tableau de bord \xE2\x80\xBA";

bool over(const mission::Mission& m) { return m.state == mission::State::Done || m.state == mission::State::Cancelled; }

} // namespace

// ============================================================================ la vue ==
AiMissionsView::AiMissionsView(std::string id, AiPanel& panel) : ui::Widget(std::move(id)), panel_(panel) {
    setFocusPolicy(true);
    const std::string base = this->id();
    search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".recherche")));
    search_->setPlaceholder("Rechercher une mission (nom, demande, \xC3\xA9tapes)\xE2\x80\xA6");
    search_->setEscapeClears(true);
    search_->setTooltip("Le nom, la demande initiale, les \xC3\xA9tapes, les \xC3\xA9l\xC3\xA9ments cr\xC3\xA9\xC3\xA9s : les plus pertinentes d'abord (les archiv\xC3\xA9" "es aussi).");
    new_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Nouvelle mission IA\xE2\x80\xA6", base + ".nouvelle")));
    new_->setIcon(ui::Icon::Plus);
    new_->setCompact(true);
    new_->setStyle(ui::Button::Style::Flat);
    new_->setTooltip("Nouvelle mission IA : une demande complexe d\xC3\xA9" "coup\xC3\xA9" "e en \xC3\xA9tapes, des propositions successives, reprise apr\xC3\xA8s une fermeture");
    composer_ = &static_cast<AiPromptEdit&>(addChild(std::make_unique<AiPromptEdit>(base + ".demande")));
    composer_->setPlaceholder("D\xC3\xA9" "cris la mission\xE2\x80\xA6 (ex. : Construis une station de pompage compl\xC3\xA8te avec deux pompes, une cuve, les "
                              "\xC3\xA9quipements Modbus, les variables, les alarmes, les scripts et les vues)");
    composer_->setTooltip("La demande de la mission. Entr\xC3\xA9" "e : cr\xC3\xA9" "er ; Maj+Entr\xC3\xA9" "e : \xC3\xA0 la ligne.");
    create_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er la mission", base + ".creer")));
    create_->setStyle(ui::Button::Style::Primary);
    create_->setTooltip("L'IA analyse d'abord le projet r\xC3\xA9" "el et propose un plan d'\xC3\xA9tapes : rien n'est modifi\xC3\xA9 avant ta validation.");
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    cancel_->setStyle(ui::Button::Style::Flat);
    rename_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".renommer")));
    rename_->setVisibility(ui::Visibility::Hidden);
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".menu")));
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{composer_, create_, cancel_}) w->setVisibility(ui::Visibility::Collapsed);
    links_ += search_->textChanged->connect([this](const std::string& t) { setSearch(t); });
    links_ += new_->clicked->connect([this] { openComposer(!composing_); });
    links_ += composer_->submitted->connect([this] { (void)create(); });
    links_ += create_->clicked->connect([this] { (void)create(); });
    links_ += cancel_->clicked->connect([this] { openComposer(false); });
    links_ += rename_->editingDone->connect([this](const std::string& t) { if (renaming()) (void)commitRename(t); });
    links_ += menu_->itemChosen->connect([this](int item) { menuChosen(item); });
    rebuild();
}

void AiMissionsView::say(const std::string& text) const {
    if (!text.empty() && panel_.uiHost().status) panel_.uiHost().status(text);
}

bool AiMissionsView::poll() {
    const auto& as = panel_.assistant();
    if (as.missionsRevision() == seen_ && as.generating() == generating_) return false;
    rebuild();
    return true;
}

void AiMissionsView::refresh() { rebuild(); }

std::size_t AiMissionsView::count(Filter f) const noexcept { return counts_[static_cast<std::size_t>(f)]; }

void AiMissionsView::setFilter(Filter f) {
    filter_ = f;
    scroll_ = 0.f;
    rebuild();
}

void AiMissionsView::setSearch(std::string query) {
    if (query == query_) return;
    query_ = std::move(query);
    if (search_->text() != query_) search_->setText(query_);
    scroll_ = 0.f;
    rebuild();
}

void AiMissionsView::rebuild() {
    const auto& as = panel_.assistant();
    seen_ = as.missionsRevision();
    generating_ = as.generating();
    const auto& all = as.missions();
    counts_[0] = counts_[1] = counts_[2] = 0;
    attention_ = 0;
    alert_ = false;
    for (const auto& m : all) {
        if (m.archived) {
            ++counts_[2];
            continue;
        }
        ++counts_[0];
        if (over(m)) continue;
        ++counts_[1];
        bool error = m.state == mission::State::Blocked, waits = m.state == mission::State::Paused || m.pending.active;
        for (const auto& s : m.steps) error |= s.state == mission::StepState::Error;
        for (const auto& p : m.proposals) waits |= p.state == "generee" || p.state == "validee";
        if (error || waits) ++attention_;
        alert_ |= error;
    }
    // l'ordre : la recherche (les plus pertinentes d'abord), sinon la plus recente d'abord
    std::vector<std::size_t> order;
    if (!query_.empty()) {
        order = mission::search(all, query_, true);
    } else {
        order.resize(all.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return all[a].modified > all[b].modified; });
    }
    rows_.clear();
    for (const std::size_t i : order) {
        if (i >= all.size()) continue;
        const auto& m = all[i];
        // Toutes : les non archivees (une recherche trouve aussi les archivees) ; En cours : ni terminees ni annulees.
        if (filter_ == Filter::Archived && !m.archived) continue;
        if (filter_ == Filter::All && m.archived && query_.empty()) continue;
        if (filter_ == Filter::Open && (m.archived || over(m))) continue;
        Row r;
        r.id = m.id;
        r.name = missionui::title(m);
        r.state = std::string(mission::stateLabel(m.state));
        r.stateTone = missionui::stateTone(m.state);
        r.meta = "Projet " + (m.project.name.empty() ? std::string("non enregistr\xC3\xA9") : m.project.name) + " \xC2\xB7 "
                 + (m.modified.empty() ? std::string("\xE2\x80\x94") : shortDate(m.modified)) + (m.archived ? " \xC2\xB7 archiv\xC3\xA9" "e" : "");
        const mission::Progress p = mission::progressOf(m);   // la progression REELLE
        r.frac = p.global;
        r.count = std::to_string(p.done) + "/" + std::to_string(p.steps) + " \xC3\xA9tapes \xC2\xB7 " + std::to_string(p.percent()) + " %";
        r.next = mission::nextAction(m);
        r.archived = m.archived;
        r.over = over(m);
        r.paused = m.state == mission::State::Paused;
        r.canPause = m.state == mission::State::Active || m.state == mission::State::Planning || m.state == mission::State::Blocked;
        const bool busy = panel_.missionBusy(m);
        const missionui::Next n = missionui::nextOf(m, busy);
        r.canContinue = n.kind == missionui::Next::Kind::Continue && !generating_;
        r.continueWhy = n.kind == missionui::Next::Kind::Continue ? std::string("une r\xC3\xA9ponse est en cours")
                        : !n.why.empty()                                 ? n.why
                                                                         : "d'abord : " + n.label;
        if (!r.over && (r.paused || missionui::wantsResume(m, m.coherence, busy))) r.second = "Reprendre";
        else if (r.canPause) r.second = "Pause";
        // les erreurs et les blocages, en une ligne
        int errors = 0, blocked = 0;
        for (const auto& s : m.steps) {
            errors += s.state == mission::StepState::Error ? 1 : 0;
            blocked += s.state == mission::StepState::Blocked ? 1 : 0;
        }
        if (m.coherence == mission::Coherence::Missing) r.problems = "Projet introuvable";
        else if (m.coherence == mission::Coherence::Changed) r.problems = "Projet modifi\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde";
        if (errors > 0) r.problems += (r.problems.empty() ? "" : " \xC2\xB7 ") + missionui::count(errors, "\xC3\xA9tape en erreur", "\xC3\xA9tapes en erreur");
        if (blocked > 0) r.problems += (r.problems.empty() ? "" : " \xC2\xB7 ") + missionui::count(blocked, "bloqu\xC3\xA9" "e", "bloqu\xC3\xA9" "es");
        if (m.pending.active && !busy) r.problems += (r.problems.empty() ? "" : " \xC2\xB7 ") + std::string("appel \xC3\xA0 l'IA interrompu");
        r.problemsTone = errors > 0 || m.coherence == mission::Coherence::Missing ? Tone::Error : Tone::Warning;
        r.tip = r.name + " \xE2\x80\x94 " + r.state + "\n" + mission::summaryOf(m) + "\n\nDemande : " + notifs::oneLine(m.request, 240)
                + "\nEntr\xC3\xA9" "e ou un double-clic : le tableau de bord (au centre) ; clic droit : les autres gestes.";
        rows_.push_back(std::move(r));
    }
    selected_ = -1;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == selectedId_) selected_ = static_cast<int>(i);
    laidW_ = -1.f;
    layoutRows();
    invalidateLayout();
    invalidate();
}

void AiMissionsView::layoutRows() {
    const float w = list_.w > 0.f ? list_.w : bounds().w;
    if (w <= 0.f) return;
    float y = 0.f;
    for (auto& r : rows_) {
        r.y = y;
        float h = 8.f + lh(kBody) + 1.f + lh(kMeta) + 14.f + lh(kMeta);
        if (!r.problems.empty()) h += lh(kMeta);
        h += 4.f + kButtonH + 10.f;
        r.h = h;
        y += h;
    }
    content_ = y;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - viewH()));
    laidW_ = w;
}

void AiMissionsView::onLayout() {
    const auto b = bounds();
    menu_->setBounds(b);
    bar_ = {b.x, b.y, b.w, kBarH};
    const float bs = 28.f;
    new_->setBounds({b.right() - kPad - bs, b.y + (kBarH - bs) / 2.f, bs, bs});
    search_->setBounds({b.x + kPad, b.y + 6.f, std::max(60.f, new_->bounds().x - 6.f - b.x - kPad), kBarH - 12.f});
    chips_ = {b.x, bar_.bottom(), b.w, kChipsH};
    const auto chipW = [](const std::string& s) { return ui::measureWidth(s, kMeta) + 20.f; };
    const std::string a = "Toutes " + std::to_string(counts_[0]), o = "En cours " + std::to_string(counts_[1]),
                      r = "Archiv\xC3\xA9" "es " + std::to_string(counts_[2]);
    chipAll_ = {b.x + kPad, chips_.y + 3.f, chipW(a), kChipsH - 8.f};
    chipOpen_ = {chipAll_.right() + 4.f, chipAll_.y, chipW(o), chipAll_.h};
    chipArchived_ = {chipOpen_.right() + 4.f, chipAll_.y, chipW(r), chipAll_.h};
    // la creation (le +) : la demande, Creer la mission, Annuler
    float top = chips_.bottom();
    compose_ = {};
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{composer_, create_, cancel_})
        w->setVisibility(composing_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (composing_) {
        const float cw = b.w - 2.f * kPad;
        const float eh = composer_->heightFor(cw);
        const float labelH = lh(kBody) + 4.f;
        compose_ = {b.x, top, b.w, labelH + eh + 6.f + kBtnH + 2.f * lh(kMeta) + 12.f};
        composer_->setBounds({b.x + kPad, top + labelH, cw, eh});
        const float by = top + labelH + eh + 6.f;
        const float w1 = ui::measureWidth(create_->text(), gfx::FontId{16}) + 28.f, w2 = ui::measureWidth(cancel_->text(), gfx::FontId{16}) + 24.f;
        create_->setBounds({b.x + kPad, by, std::min(w1, cw), kBtnH});
        cancel_->setBounds({b.x + kPad + std::min(w1, cw) + 6.f, by, w2, kBtnH});
        top = compose_.bottom();
    }
    list_ = {b.x, top, b.w, std::max(0.f, b.bottom() - top)};
    if (std::fabs(list_.w - laidW_) > 0.5f) layoutRows();
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - viewH()));
    // le champ de renommage, sur le nom de la ligne
    if (renaming()) {
        const Row* row = nullptr;
        for (const auto& x : rows_)
            if (x.id == renameId_) row = &x;
        if (row) {
            const auto a2 = rowArea(*row);
            rename_->setBounds({a2.x + kLeft - 4.f, a2.y + 4.f, std::max(60.f, a2.w - kLeft - 110.f), lh(kBody) + 6.f});
        }
    }
}

gfx::Rect AiMissionsView::filterRect(Filter f) const noexcept {
    return f == Filter::All ? chipAll_ : f == Filter::Open ? chipOpen_ : chipArchived_;
}

gfx::Rect AiMissionsView::rowArea(const Row& r) const { return {list_.x, list_.y + r.y - scroll_, list_.w, r.h}; }

gfx::Rect AiMissionsView::buttonArea(const Row& r, int which) const {
    const auto a = rowArea(r);
    const float y = a.bottom() - 10.f - kButtonH;
    const float w0 = ui::measureWidth(kOpenLabel, kMeta) + 18.f;
    if (which == 0) return {a.x + kLeft, y, w0, kButtonH};
    if (r.second.empty()) return {};
    const std::string s = r.second + (r.second == "Reprendre" ? " \xE2\x80\xBA" : "");
    return {a.x + kLeft + w0 + 6.f, y, ui::measureWidth(s, kMeta) + 18.f, kButtonH};
}

gfx::Rect AiMissionsView::rowRect(std::size_t row) const {
    if (row >= rows_.size()) return {};
    const auto r = rowArea(rows_[row]);
    if (r.bottom() <= list_.y || r.y >= list_.bottom()) return {};
    return r;
}

gfx::Rect AiMissionsView::buttonRect(std::size_t row, int which) const {
    if (row >= rows_.size()) return {};
    const auto r = buttonArea(rows_[row], which);
    if (r.w <= 0.f || r.bottom() <= list_.y || r.y >= list_.bottom()) return {};
    return r;
}

int AiMissionsView::rowAt(gfx::Point p) const {
    if (!list_.contains(p)) return -1;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rowArea(rows_[i]).contains(p)) return static_cast<int>(i);
    return -1;
}

void AiMissionsView::ensureVisible(int row) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const auto& r = rows_[static_cast<std::size_t>(row)];
    if (r.y < scroll_) scroll_ = r.y;
    else if (r.y + r.h > scroll_ + viewH()) scroll_ = r.y + r.h - viewH();
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - viewH()));
    invalidateLayout();
    invalidate();
}

// ============================================================================ les gestes ==
std::string AiMissionsView::rowId(std::size_t row) const { return row < rows_.size() ? rows_[row].id : std::string{}; }

std::string AiMissionsView::secondLabel(std::size_t row) const { return row < rows_.size() ? rows_[row].second : std::string{}; }

std::string AiMissionsView::rowText(std::size_t row) const {
    if (row >= rows_.size()) return {};
    const auto& r = rows_[row];
    return r.name + " | " + r.state + " | " + r.meta + " | " + r.count + " | Prochaine : " + r.next + " | " + r.problems + " | " + r.second;
}

std::vector<std::string> AiMissionsView::lines() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < rows_.size(); ++i) out.push_back(rowText(i));
    return out;
}

bool AiMissionsView::select(int row) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return false;
    selected_ = row;
    selectedId_ = rows_[static_cast<std::size_t>(row)].id;
    ensureVisible(row);
    return true;
}

bool AiMissionsView::selectMission(const std::string& id) {
    const auto* m = panel_.assistant().findMission(id);
    if (!m) return false;
    if (!query_.empty()) setSearch({});
    if (m->archived && filter_ != Filter::Archived) setFilter(Filter::Archived);
    else if (!m->archived && filter_ == Filter::Archived) setFilter(Filter::All);
    else if (filter_ == Filter::Open && over(*m)) setFilter(Filter::All);
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) return select(static_cast<int>(i));
    return false;
}

bool AiMissionsView::open(int row) {
    if (!select(row)) return false;
    return panel_.openMission(rows_[static_cast<std::size_t>(row)].id, 0);
}

bool AiMissionsView::second(int row) {
    if (!select(row)) return false;
    const std::string id = rows_[static_cast<std::size_t>(row)].id, what = rows_[static_cast<std::size_t>(row)].second;
    if (what == "Reprendre") return panel_.openMission(id, 1);   // le constat de reprise d'abord (au centre)
    if (what == "Pause") return panel_.missionAct(id, AiPanel::MissionAct::Pause);
    return false;
}

void AiMissionsView::openComposer(bool open) {
    composing_ = open;
    if (open) {
        cancelRename();
        composer_->takeFocus();
    } else {
        takeFocus();
    }
    invalidateLayout();
    invalidate();
}

bool AiMissionsView::create() {
    const std::string text = composer_->text();
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        say("D\xC3\xA9" "cris d'abord la mission (ce que l'IA doit construire ou modifier).");
        composer_->takeFocus();
        return false;
    }
    if (!panel_.createMission(text)) return false;
    composer_->setText({});
    composing_ = false;
    invalidateLayout();
    rebuild();
    return true;
}

void AiMissionsView::startRename(int row) {
    if (!select(row)) return;
    const auto& r = rows_[static_cast<std::size_t>(row)];
    renameId_ = r.id;
    const auto* m = panel_.assistant().findMission(r.id);
    rename_->setText(m && !m->name.empty() ? m->name : r.name);
    rename_->setVisibility(ui::Visibility::Visible);
    invalidateLayout();
    layout();
    (void)rename_->focusAndSelectAll();
}

bool AiMissionsView::commitRename(const std::string& name) {
    if (!renaming()) return false;
    const std::string id = std::exchange(renameId_, std::string{});
    rename_->setVisibility(ui::Visibility::Hidden);
    invalidateLayout();
    takeFocus();
    return panel_.renameMission(id, name);
}

void AiMissionsView::cancelRename() {
    if (!renaming()) return;
    renameId_.clear();
    rename_->setVisibility(ui::Visibility::Hidden);
    invalidateLayout();
}

void AiMissionsView::openRowMenu(int row, gfx::Point at) {
    if (!select(row)) return;
    const auto& r = rows_[static_cast<std::size_t>(row)];
    menuRow_ = row;
    menuId_ = r.id;
    std::vector<ui::PopupMenu::Item> items;
    const auto item = [&](std::string label, ui::Icon icon, int id, std::string shortcut = {}, bool enabled = true, std::string why = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.icon = icon;
        it.id = id;
        it.shortcut = std::move(shortcut);
        it.enabled = enabled;
        it.disabledReason = std::move(why);
        items.push_back(std::move(it));
    };
    const auto separator = [&] {
        ui::PopupMenu::Item s;
        s.separator = true;
        items.push_back(s);
    };
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = r.name;
    head.shortcut = r.state;
    items.push_back(head);
    item("Ouvrir le tableau de bord", ui::Icon::GoTo, kMenuOpen, "Entr\xC3\xA9" "e");
    item("\xC3\x89" "cran de reprise", ui::Icon::History, kMenuResumeScreen);
    item("Ouvrir la conversation", ui::Icon::Assistant, kMenuConversation);
    separator();
    item("Proposer la suite", ui::Icon::Play, kMenuContinue, {}, r.canContinue, r.continueWhy);
    if (r.paused) item("Reprendre", ui::Icon::Play, kMenuResume);
    else item("Mettre en pause", ui::Icon::Pause, kMenuPause, {}, r.canPause, "elle n'est pas en cours");
    item("Renommer\xE2\x80\xA6", ui::Icon::Rename, kMenuRename, "F2");
    item("Historique", ui::Icon::History, kMenuHistory);
    separator();
    if (r.archived) item("D\xC3\xA9sarchiver", ui::Icon::Open, kMenuUnarchive);
    else item("Archiver", ui::Icon::Folder, kMenuArchive, "Suppr");
    item("Annuler la mission\xE2\x80\xA6", ui::Icon::Stop, kMenuCancel, {}, !r.over, "elle est d\xC3\xA9j\xC3\xA0 termin\xC3\xA9" "e ou annul\xC3\xA9" "e");
    menu_->setItems(std::move(items));
    menu_->openAtSurface(toSurface(at));
}

void AiMissionsView::menuChosen(int id) {
    const std::string mid = std::exchange(menuId_, std::string{});
    const int row = std::exchange(menuRow_, -1);
    if (mid.empty()) return;
    using A = AiPanel::MissionAct;
    switch (id) {
        case kMenuOpen: (void)panel_.openMission(mid, 0); break;
        case kMenuResumeScreen: (void)panel_.openMission(mid, 1); break;
        case kMenuConversation: (void)panel_.missionAct(mid, A::Conversation); break;
        case kMenuContinue: (void)panel_.missionAct(mid, A::Continue); break;
        case kMenuPause: (void)panel_.missionAct(mid, A::Pause); break;
        case kMenuResume: (void)panel_.missionAct(mid, A::Resume); break;
        case kMenuRename:
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].id == mid) startRename(static_cast<int>(i));
            break;
        case kMenuHistory: (void)panel_.openMission(mid, 2); break;
        case kMenuArchive: (void)panel_.missionAct(mid, A::Archive); break;
        case kMenuUnarchive: (void)panel_.missionAct(mid, A::Unarchive); break;
        case kMenuCancel: (void)panel_.missionAct(mid, A::Cancel); break;
        default: break;
    }
    (void)row;
}

// ========================================================================= le dessin ==
std::string AiMissionsView::liveTooltip(gfx::Point mouse) const {
    for (auto it = tips_.rbegin(); it != tips_.rend(); ++it)
        if (it->first.contains(mouse)) return it->second;
    const int row = rowAt(mouse);
    return row < 0 ? std::string{} : rows_[static_cast<std::size_t>(row)].tip;
}

void AiMissionsView::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    tips_.clear();
    ctx.r.fillRect(b, c.panelBg);
    // ---- la barre (la recherche, le +) et les filtres
    ctx.r.fillRect({bar_.x + 1.f, bar_.y, bar_.w - 1.f, bar_.h + chips_.h}, c.rowAltBg);
    ctx.r.fillRect({chips_.x + 1.f, chips_.bottom() - 1.f, chips_.w - 1.f, 1.f}, c.border);
    const auto chip = [&](const gfx::Rect& r, const std::string& label, std::size_t n, bool on, gfx::Color badge, const char* tip) {
        ctx.r.fillRoundedRect(r, on ? alpha(c.accent, ctx.theme.isDark() ? 70 : 45) : alpha(c.border, 90), r.h / 2.f);
        if (on) ctx.r.strokeRect(r, alpha(c.accent, 160), 1.f);
        const std::string count = std::to_string(n);
        const float lw = ctx.r.measure(label + " ", kMeta).width, cw = ctx.r.measure(count, kMeta).width;
        const float x = r.x + (r.w - lw - cw) / 2.f, y = r.y + (r.h - ctx.r.lineHeight(kMeta)) / 2.f;
        ctx.r.drawText({x, y}, label, kMeta, on ? c.text : c.textMuted);
        boldText(ctx.r, {x + lw, y}, count, kMeta, n > 0 ? badge : c.textMuted);
        tips_.emplace_back(r, tip);
    };
    chip(chipAll_, "Toutes", counts_[0], filter_ == Filter::All, c.text, "Toutes les missions qui ne sont pas archiv\xC3\xA9" "es, la plus r\xC3\xA9" "cente d'abord");
    chip(chipOpen_, "En cours", counts_[1], filter_ == Filter::Open, alert_ ? c.error : c.accent,
         "Celles qui ne sont ni termin\xC3\xA9" "es ni annul\xC3\xA9" "es (en cours, en pause, bloqu\xC3\xA9" "es, en planification)");
    chip(chipArchived_, "Archiv\xC3\xA9" "es", counts_[2], filter_ == Filter::Archived, c.textMuted,
         "Les missions archiv\xC3\xA9" "es : rien n'est supprim\xC3\xA9, D\xC3\xA9sarchiver (clic droit) les remet");
    // ---- la creation
    if (composing_) {
        ctx.r.fillRect({compose_.x + 1.f, compose_.y, compose_.w - 1.f, compose_.h}, ctx.theme.brand.card.a ? ctx.theme.brand.card : c.inputBg);
        ctx.r.fillRect({compose_.x + 1.f, compose_.bottom() - 1.f, compose_.w - 1.f, 1.f}, c.border);
        ui::drawIcon(ctx.r, ui::Icon::Task, {compose_.x + kPad, compose_.y + 4.f, 15.f, 15.f}, c.accent);
        boldText(ctx.r, {compose_.x + kPad + 22.f, compose_.y + 3.f}, "Nouvelle mission IA", kBody, c.text);
        const float hy = create_->bounds().bottom() + 4.f;
        const std::string hint = "L'IA analyse d'abord le projet r\xC3\xA9" "el, puis propose un plan d'\xC3\xA9tapes ; chaque proposition se v\xC3\xA9rifie et s'applique ensuite, "
                                 "comme dans la conversation.";
        float y = hy;
        for (const auto& l : missionui::wrap(hint, kMeta, compose_.w - 2.f * kPad, 2)) {
            ctx.r.drawText({compose_.x + kPad, y}, l, kMeta, c.textMuted);
            y += lh(kMeta);
        }
    }
    // ---- la liste
    ctx.r.pushClip(list_);
    if (rows_.empty()) {
        const float y = list_.y + 24.f;
        ui::drawIcon(ctx.r, ui::Icon::Task, {list_.x + (list_.w - 28.f) / 2.f, y, 28.f, 28.f}, alpha(c.textMuted, 150));
        std::string head, text;
        if (!query_.empty()) {
            head = "Aucune mission ne correspond.";
            text = "Rien ne correspond \xC3\xA0 \xC2\xAB " + query_ + " \xC2\xBB dans les noms, les demandes ni les \xC3\xA9tapes (les archiv\xC3\xA9" "es comprises).";
        } else if (filter_ == Filter::Archived) {
            head = "Aucune mission archiv\xC3\xA9" "e.";
        } else if (filter_ == Filter::Open && counts_[0] > 0) {
            head = "Aucune mission en cours.";
            text = "Les autres sont termin\xC3\xA9" "es ou annul\xC3\xA9" "es (le filtre Toutes les montre).";
        } else {
            head = "Aucune mission IA.";
            text = "Une demande complexe (une station de pompage compl\xC3\xA8te, une ligne de production...) devient une mission : un plan d'\xC3\xA9tapes, "
                   "des propositions successives que tu valides, ce qui est r\xC3\xA9" "ellement fait dans le projet, une reprise apr\xC3\xA8s une fermeture. "
                   "Le + en cr\xC3\xA9" "e une ; une demande complexe envoy\xC3\xA9" "e dans la conversation la propose aussi.";
        }
        ctx.r.drawText({list_.x + (list_.w - ctx.r.measure(head, kBody).width) / 2.f, y + 40.f}, head, kBody, c.text);
        float ty = y + 40.f + lh(kBody) + 6.f;
        for (const auto& t : missionui::wrap(text, kMeta, list_.w - 2.f * kPad - 20.f, 7)) {
            ctx.r.drawText({list_.x + (list_.w - ctx.r.measure(t, kMeta).width) / 2.f, ty}, t, kMeta, c.textMuted);
            ty += lh(kMeta);
        }
    }
    const float space = listBar_.space(list_, content_, viewH());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        gfx::Rect r = rowArea(row);
        r.w -= space;
        if (r.bottom() <= list_.y || r.y >= list_.bottom()) continue;
        const bool sel = static_cast<int>(i) == selected_, hot = static_cast<int>(i) == hover_;
        if (sel) ctx.r.fillRect(r, alpha(c.selectionBg, ctx.theme.isDark() ? 150 : 110));
        else if (hot) ctx.r.fillRect(r, ctx.theme.brand.hover);
        const gfx::Color tone = missionui::color(ctx.theme, row.stateTone);
        ctx.r.fillRect({r.x + 1.f, r.y + 5.f, 3.f, r.h - 10.f}, sel ? c.accent : tone);   // le filet de l'etat
        const gfx::Color ink = row.over || row.archived ? c.textMuted : c.text;
        float y = r.y + 8.f;
        // le nom, l'etat
        const float cw = ctx.r.measure(row.state, kMeta).width + 14.f;
        const gfx::Rect pill{r.right() - kPad - cw, y - 2.f, cw, ctx.r.lineHeight(kMeta) + 6.f};
        ctx.r.fillRoundedRect(pill, alpha(tone, 46), pill.h / 2.f);
        ctx.r.drawText({pill.x + 7.f, pill.y + 3.f}, row.state, kMeta, tone);
        if (renameId_ != row.id) boldText(ctx.r, {r.x + kLeft, y}, missionui::elide(row.name, kBody, pill.x - 8.f - r.x - kLeft), kBody, ink);
        y += lh(kBody) + 1.f;
        ctx.r.drawText({r.x + kLeft, y}, missionui::elide(row.meta, kMeta, r.right() - kPad - r.x - kLeft), kMeta, c.textMuted);
        y += lh(kMeta);
        // la progression globale
        const float pw = ctx.r.measure(row.count, kMeta).width + 8.f;
        const gfx::Rect track{r.x + kLeft, y + 3.f, std::max(30.f, r.right() - kPad - pw - r.x - kLeft), 7.f};
        ctx.r.fillRoundedRect(track, alpha(c.accent, 40), 3.5f);
        if (row.frac > 0.0)
            ctx.r.fillRoundedRect({track.x, track.y, std::max(7.f, track.w * static_cast<float>(std::clamp(row.frac, 0.0, 1.0))), track.h},
                                  row.stateTone == Tone::Ok ? c.ok : c.accent, 3.5f);
        ctx.r.drawText({track.right() + 8.f, y}, row.count, kMeta, c.textMuted);
        tips_.emplace_back(gfx::Rect{track.x, y, r.right() - track.x, 14.f}, row.count + " (la progression globale : les \xC3\xA9tapes termin\xC3\xA9" "es)\n\n" + mission::progressRule());
        y += 14.f;
        // la prochaine action, les erreurs
        ctx.r.drawText({r.x + kLeft, y}, missionui::elide("Prochaine : " + row.next, kMeta, r.right() - kPad - r.x - kLeft), kMeta, ink);
        y += lh(kMeta);
        if (!row.problems.empty()) {
            ctx.r.drawText({r.x + kLeft, y}, missionui::elide(row.problems, kMeta, r.right() - kPad - r.x - kLeft), kMeta, missionui::color(ctx.theme, row.problemsTone));
            y += lh(kMeta);
        }
        // les boutons (dessines, comme ceux des notifications)
        for (int which = 0; which < 2; ++which) {
            gfx::Rect br = buttonArea(row, which);
            if (br.w <= 0.f) continue;
            const bool overButton = hot && hoverButton_ == which;
            ctx.r.fillRoundedRect(br, overButton ? alpha(c.accent, 60) : alpha(c.accent, 22), 4.f);
            ctx.r.strokeRect(br, alpha(c.accent, overButton ? 200 : 120), 1.f);
            const std::string label = which == 0 ? std::string(kOpenLabel) : row.second + (row.second == "Reprendre" ? " \xE2\x80\xBA" : "");
            ctx.r.drawText({br.x + 9.f, br.y + (br.h - ctx.r.lineHeight(kMeta)) / 2.f}, label, kMeta, c.accent);
            tips_.emplace_back(br, which == 0 ? std::string("Le tableau de bord de la mission, au centre : le plan, les propositions, l'historique.")
                                   : row.second == "Reprendre" ? std::string("L'\xC3\xA9" "cran de reprise, au centre : le dernier \xC3\xA9tat, le projet compar\xC3\xA9, puis Reprendre.")
                                                               : std::string("Mettre la mission en pause : plus rien ne s'applique jusqu'\xC3\xA0 Reprendre."));
        }
        ctx.r.fillRect({r.x + kLeft, r.bottom() - 1.f, r.w - kLeft - kPad, 1.f}, alpha(c.border, 110));
    }
    ctx.r.popClip();
    listBar_.paint(ctx, list_, content_, viewH(), scroll_);
}

// ========================================================================= les evenements ==
ui::EventResult AiMissionsView::onEvent(const ui::InputEvent& ev) {
    if (listBar_.handle(*this, ev, list_, content_, viewH(), scroll_)) {
        invalidateLayout();
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        int onButton = -1;
        if (h >= 0)
            for (int which = 0; which < 2; ++which)
                if (const auto br = buttonRect(static_cast<std::size_t>(h), which); br.w > 0.f && br.contains(m->pos)) onButton = which;
        if (h != hover_ || onButton != hoverButton_) {
            hover_ = h;
            hoverButton_ = onButton;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos) && !w->mods.ctrl) {
        scroll_ = std::clamp(scroll_ - w->dy * 40.f, 0.f, std::max(0.f, content_ - viewH()));
        invalidateLayout();
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button == ui::MouseButton::Left) {
            if (chipAll_.contains(d->pos)) { setFilter(Filter::All); return ui::EventResult::Consumed; }
            if (chipOpen_.contains(d->pos)) { setFilter(Filter::Open); return ui::EventResult::Consumed; }
            if (chipArchived_.contains(d->pos)) { setFilter(Filter::Archived); return ui::EventResult::Consumed; }
        }
        const int row = rowAt(d->pos);
        if (row >= 0) {
            takeFocus();
            if (d->button == ui::MouseButton::Right) {
                openRowMenu(row, d->pos);
                return ui::EventResult::Consumed;
            }
            if (d->button != ui::MouseButton::Left) return ui::EventResult::Consumed;
            const auto b0 = buttonRect(static_cast<std::size_t>(row), 0), b1 = buttonRect(static_cast<std::size_t>(row), 1);
            if ((b0.w > 0.f && b0.contains(d->pos)) || d->clickCount >= 2) (void)open(row);
            else if (b1.w > 0.f && b1.contains(d->pos)) (void)second(row);
            else (void)select(row);
            return ui::EventResult::Consumed;
        }
        if (list_.contains(d->pos) || bar_.contains(d->pos) || chips_.contains(d->pos) || compose_.contains(d->pos)) {
            if (list_.contains(d->pos)) takeFocus();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape && renaming()) {
            cancelRename();
            takeFocus();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && composing_) {
            openComposer(false);
            return ui::EventResult::Consumed;
        }
        if (!focused()) return ui::EventResult::Ignored;
        const int n = static_cast<int>(rows_.size());
        const auto go = [&](int row) {
            if (n > 0) (void)select(std::clamp(row, 0, n - 1));
            return ui::EventResult::Consumed;
        };
        if (ui::isContextMenuKey(*k)) {
            if (selected_ >= 0)
                if (const auto r = rowRect(static_cast<std::size_t>(selected_)); r.w > 0.f) openRowMenu(selected_, {r.x + 40.f, r.y + 12.f});
            return ui::EventResult::Consumed;
        }
        if (k->mods.ctrl || k->mods.alt) return ui::EventResult::Ignored;
        switch (k->key) {
            case ui::Key::Down: return go(selected_ < 0 ? 0 : selected_ + 1);
            case ui::Key::Up: return go(selected_ < 0 ? 0 : selected_ - 1);
            case ui::Key::Home: return go(0);
            case ui::Key::End: return go(n - 1);
            case ui::Key::PageDown: return go(selected_ < 0 ? 0 : selected_ + 4);
            case ui::Key::PageUp: return go(selected_ < 0 ? 0 : selected_ - 4);
            case ui::Key::Return:
                if (selected_ >= 0) (void)open(selected_);
                return ui::EventResult::Consumed;
            case ui::Key::F2:
                if (selected_ >= 0) startRename(selected_);
                return ui::EventResult::Consumed;
            case ui::Key::Delete:
                if (selected_ >= 0 && !rows_[static_cast<std::size_t>(selected_)].archived)
                    (void)panel_.missionAct(rows_[static_cast<std::size_t>(selected_)].id, AiPanel::MissionAct::Archive);
                return ui::EventResult::Consumed;
            default: break;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
