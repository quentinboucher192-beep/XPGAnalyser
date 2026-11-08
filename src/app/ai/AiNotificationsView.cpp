// =============================================================================
//  app/ai/AiNotificationsView.cpp - 1.21.2 : l'onglet Notifications du panneau (voir le .hpp)
// =============================================================================
#include "AiNotificationsView.hpp"
#include "../../core/Perf.hpp"   // 1.22.2 : ce qui consomme (l'onglet Performances)

#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app::ai {

namespace {

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
constexpr float kPad = 10.f;
constexpr float kBarH = 34.f;
constexpr float kDayH = 26.f;
constexpr float kButtonH = 24.f;
constexpr float kLeft = 34.f;        // le texte d'une ligne commence apres le filet et l'icone du ton

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

gfx::Color toneColor(const ui::Theme& t, const std::string& tone) {
    const auto& c = t.color;
    return tone == "error" ? c.error : tone == "warning" ? c.warning : tone == "ok" ? c.ok : c.info;
}

ui::Icon toneIcon(const std::string& tone) {
    return tone == "error" ? ui::Icon::Error : tone == "warning" ? ui::Icon::Warning : tone == "ok" ? ui::Icon::Ok : ui::Icon::Info;
}

std::string toneWord(const std::string& tone) {
    return tone == "error" ? "erreur" : tone == "warning" ? "attention" : tone == "ok" ? "r\xC3\xA9ussi" : "information";
}

bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Coupe `s` pour tenir dans `w` (finit par ...), jamais au milieu d'un caractere.
std::string elide(const std::string& s, gfx::FontId f, float w) {
    if (ui::measureWidth(s, f) <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    std::size_t lo = 0, hi = s.size();
    while (lo < hi) {   // le plus long debut qui tient avec les points
        std::size_t mid = (lo + hi + 1) / 2;
        while (mid > 0 && mid < s.size() && continuation(s[mid])) --mid;
        if (mid <= lo) break;
        if (ui::measureWidth(s.substr(0, mid) + dots, f) <= w) lo = mid;
        else hi = mid - 1;
    }
    while (lo > 0 && lo < s.size() && continuation(s[lo])) --lo;
    std::string out = s.substr(0, lo);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

// Le texte en lignes de `w` au plus (au mot ; un mot trop long est coupe), `maxLines` au plus - la derniere finit par
// ... quand il en reste.
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float w, std::size_t maxLines) {
    std::vector<std::string> out;
    std::string line;
    bool more = false;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') {
            out.push_back(std::move(line));
            line.clear();
            ++i;
            if (out.size() >= maxLines) {
                more = text.find_first_not_of(" \n", i) != std::string::npos;
                break;
            }
            continue;
        }
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
        std::string word = text.substr(i, j - i);
        if (ui::measureWidth(word, f) > w) word = elide(word, f, w);   // un mot plus large que la ligne : coupe
        std::string tryLine = line.empty() ? word : line + " " + word;
        if (line.empty() || ui::measureWidth(tryLine, f) <= w) {
            line = std::move(tryLine);
            i = j;
            continue;
        }
        out.push_back(std::move(line));   // le mot commence la ligne suivante
        line.clear();
        if (out.size() >= maxLines) {
            more = true;
            break;
        }
    }
    if (!line.empty()) {
        if (out.size() < maxLines) out.push_back(std::move(line));
        else more = true;
    }
    if (more && !out.empty()) out.back() = elide(out.back() + " \xE2\x80\xA6", f, w);
    return out;
}

std::string upperFirst(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

void boldText(gfx::IRenderer& r, gfx::Point at, std::string_view t, gfx::FontId f, gfx::Color c) {
    r.drawText(at, t, f, c);
    r.drawText({at.x + 0.6f, at.y}, t, f, c);
}

std::string countBadge(int n) { return "\xC3\x97" + std::to_string(n); }

} // namespace

// ============================================================================ la vue ==
AiNotificationsView::Source AiNotificationsView::assistant() {
    return {&notifs::revision, &notifs::list, &notifs::markRead, &notifs::remove, &notifs::markAllRead, &notifs::clearRead};
}

AiNotificationsView::AiNotificationsView(std::string id, Callbacks cb) : ui::Widget(std::move(id)), cb_(std::move(cb)) {
    setFocusPolicy(true);
    const std::string base = this->id();
    const auto iconButton = [&](const char* label, ui::Icon icon, const char* suffix, const char* tip) {
        auto b = std::make_unique<ui::Button>(label, base + suffix);
        b->setIcon(icon);
        b->setCompact(true);
        b->setStyle(ui::Button::Style::Flat);
        b->setTooltip(tip);
        return &static_cast<ui::Button&>(addChild(std::move(b)));
    };
    markAll_ = iconButton("Tout marquer comme lu", ui::Icon::Ok, ".tout-lu", "Tout marquer comme lu");
    clearRead_ = iconButton("Effacer les lues", ui::Icon::Clear, ".effacer-lues",
                            "Effacer les lues : les notifications d\xC3\xA9j\xC3\xA0 lues quittent le journal (les non lues restent)");
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".menu")));
    links_ += markAll_->clicked->connect([this] { (void)markAllRead(); });
    links_ += clearRead_->clicked->connect([this] { (void)clearRead(); });
    links_ += menu_->itemChosen->connect([this](int item) { menuChosen(item); });
    refresh();
}

void AiNotificationsView::say(const std::string& text) const {
    if (cb_.status && !text.empty()) cb_.status(text);
}

bool AiNotificationsView::poll() {
    if (src_.revision() == seen_) return false;
    refresh();
    return true;
}

void AiNotificationsView::refresh() {
    seen_ = src_.revision();
    all_ = src_.list();
    rebuild();
}

void AiNotificationsView::setFilter(Filter f) {
    kept_.clear();   // un nouveau choix : les lues gardees partent
    filter_ = f;
    scroll_ = 0.f;
    rebuild();
}

void AiNotificationsView::rebuild() {
    unread_ = 0;
    read_ = 0;
    for (const auto& n : all_) (n.read ? read_ : unread_) += 1;
    shown_.clear();
    for (const auto& n : all_)
        if (filter_ == Filter::All || !n.read || kept_.count(n.id)) shown_.push_back(n);
    // la ligne choisie suit sa notification (une autre ligne au-dessus, une ligne partie)
    selected_ = -1;
    for (std::size_t i = 0; i < shown_.size(); ++i)
        if (shown_[i].id == selectedId_) selected_ = static_cast<int>(i);
    if (selected_ < 0) selectedId_ = 0;
    now_ = notifs::nowSeconds();
    laidW_ = -1.f;
    layoutLines();
    updateButtons();
    invalidateLayout();   // les nombres des filtres changent leur largeur
    invalidate();
}

void AiNotificationsView::updateButtons() {
    if (markAll_) markAll_->setEnabled(unread_ > 0);
    if (clearRead_) clearRead_->setEnabled(read_ > 0);
}

void AiNotificationsView::layoutLines() {
    const float w = list_.w > 0.f ? list_.w : bounds().w;
    if (w <= 0.f) return;
    lines_.clear();
    const float lhMeta = ui::lineHeight(kMeta) + 2.f;
    const float lhBody = ui::lineHeight(kBody) + 2.f;
    const float textW = std::max(40.f, w - kLeft - kPad - 10.f);   // 10 : la barre de defilement
    float y = 0.f;
    std::string day;
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        const auto& n = shown_[i];
        const std::string d = notifs::dayLabel(n.at, now_);
        if (d != day) {
            day = d;
            Line h;
            h.day = true;
            h.text = upperFirst(d);
            h.y = y;
            h.h = kDayH;
            lines_.push_back(std::move(h));
            y += kDayH;
        }
        Line l;
        l.row = static_cast<int>(i);
        l.y = y;
        l.meta = notifs::timeLabel(n.at) + (n.category.empty() ? std::string{} : " \xC2\xB7 " + n.category);
        l.detail = n.detail.empty() ? std::vector<std::string>{} : wrap(n.detail, kMeta, textW, 2);
        float h = 6.f + lhMeta + lhBody + static_cast<float>(l.detail.size()) * lhMeta;
        if (!n.button.empty() && !n.action.empty()) {
            l.buttonW = std::min(textW, ui::measureWidth(n.button + " \xE2\x80\xBA", kMeta) + 18.f);
            h += 4.f + kButtonH;
        }
        l.h = h + 8.f;
        y += l.h;
        lines_.push_back(std::move(l));
    }
    content_ = y;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - viewH()));
    laidW_ = w;
}

void AiNotificationsView::onLayout() {
    const auto b = bounds();
    menu_->setBounds(b);
    bar_ = {b.x, b.y, b.w, kBarH};
    list_ = {b.x, bar_.bottom(), b.w, std::max(0.f, b.bottom() - bar_.bottom())};
    // Toutes | Non lues, a gauche ; les deux gestes, a droite (des icones : leurs libelles dans l'infobulle)
    const float bs = 28.f;
    clearRead_->setBounds({b.right() - kPad - bs, b.y + (kBarH - bs) / 2.f, bs, bs});
    markAll_->setBounds({clearRead_->bounds().x - bs - 2.f, clearRead_->bounds().y, bs, bs});
    const std::string all = "Toutes " + std::to_string(all_.size());
    const std::string unread = "Non lues " + std::to_string(unread_);
    const float aw = ui::measureWidth(all, kMeta) + 20.f, uw = ui::measureWidth(unread, kMeta) + 20.f;
    chipAll_ = {b.x + kPad, b.y + 5.f, aw, kBarH - 10.f};
    chipUnread_ = {chipAll_.right() + 4.f, chipAll_.y, uw, chipAll_.h};
    if (std::fabs(list_.w - laidW_) > 0.5f) layoutLines();
}

gfx::Rect AiNotificationsView::rowArea(const Line& l) const { return {list_.x, list_.y + l.y - scroll_, list_.w, l.h}; }

gfx::Rect AiNotificationsView::buttonArea(const Line& l) const {
    if (l.buttonW <= 0.f) return {};
    const auto r = rowArea(l);
    return {r.x + kLeft, r.bottom() - 8.f - kButtonH, l.buttonW, kButtonH};
}

gfx::Rect AiNotificationsView::rowRect(std::size_t row) const {
    for (const auto& l : lines_)
        if (!l.day && l.row == static_cast<int>(row)) {
            const auto r = rowArea(l);
            if (r.bottom() <= list_.y || r.y >= list_.bottom()) return {};
            return r;
        }
    return {};
}

gfx::Rect AiNotificationsView::buttonRect(std::size_t row) const {
    for (const auto& l : lines_)
        if (!l.day && l.row == static_cast<int>(row)) {
            const auto r = buttonArea(l);
            if (r.w <= 0.f || r.bottom() <= list_.y || r.y >= list_.bottom()) return {};
            return r;
        }
    return {};
}

int AiNotificationsView::rowAt(gfx::Point p) const {
    if (!list_.contains(p)) return -1;
    for (const auto& l : lines_)
        if (!l.day && rowArea(l).contains(p)) return l.row;
    return -1;
}

void AiNotificationsView::ensureVisible(int row) {
    for (const auto& l : lines_)
        if (!l.day && l.row == row) {
            float top = l.y;
            // la premiere ligne d'un jour : son titre aussi
            if (&l != &lines_.front() && (&l - 1)->day) top -= kDayH;
            if (top < scroll_) scroll_ = top;
            else if (l.y + l.h > scroll_ + viewH()) scroll_ = l.y + l.h - viewH();
            scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - viewH()));
            invalidate();
            return;
        }
}

// ============================================================================ les gestes ==
std::string AiNotificationsView::buttonLabel(const notifs::Item& n) { return n.button.empty() ? std::string("Ouvrir") : n.button; }

bool AiNotificationsView::select(int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return false;
    selected_ = row;
    selectedId_ = shown_[static_cast<std::size_t>(row)].id;
    if (!shown_[static_cast<std::size_t>(row)].read) {
        if (filter_ == Filter::Unread) kept_.insert(selectedId_);   // elle reste a sa place, grisee
        (void)src_.markRead(selectedId_, true);
        refresh();
    }
    ensureVisible(selected_);
    invalidate();
    return true;
}

bool AiNotificationsView::follow(int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return false;
    (void)select(row);
    if (selected_ < 0) return false;
    const notifs::Item n = shown_[static_cast<std::size_t>(selected_)];
    if (n.action.empty()) {
        say("Cette notification n'a pas de lien : elle dit seulement ce qui s'est pass\xC3\xA9.");
        return false;
    }
    return cb_.follow ? cb_.follow(n.action) : false;
}

bool AiNotificationsView::setRead(int row, bool read) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return false;
    const auto id = shown_[static_cast<std::size_t>(row)].id;
    if (read && filter_ == Filter::Unread) kept_.insert(id);
    if (!src_.markRead(id, read)) return false;
    refresh();
    return true;
}

bool AiNotificationsView::removeRow(int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return false;
    const auto id = shown_[static_cast<std::size_t>(row)].id;
    const std::string title = shown_[static_cast<std::size_t>(row)].title;
    if (!src_.remove(id)) return false;
    // la ligne choisie passe a la suivante (ou a la precedente, la derniere partie)
    if (id == selectedId_) {
        const std::size_t next = static_cast<std::size_t>(row) + 1 < shown_.size() ? static_cast<std::size_t>(row) + 1
                                 : row > 0                                          ? static_cast<std::size_t>(row) - 1
                                                                                    : shown_.size();
        selectedId_ = next < shown_.size() ? shown_[next].id : 0;
    }
    kept_.erase(id);
    refresh();
    say("Notification supprim\xC3\xA9" "e : " + title);
    return true;
}

bool AiNotificationsView::copyRow(int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return false;
    const auto& n = shown_[static_cast<std::size_t>(row)];
    std::string text = notifs::dayLabel(n.at, notifs::nowSeconds()) + " " + notifs::timeLabel(n.at) + " - "
                       + (n.category.empty() ? std::string{} : n.category + " : ") + n.title;
    if (n.count > 1) text += " (" + countBadge(n.count) + ")";
    if (!n.detail.empty()) text += "\n" + n.detail;
    ui::setClipboardText(text);
    say("Notification copi\xC3\xA9" "e dans le presse-papiers.");
    return true;
}

std::size_t AiNotificationsView::markAllRead() {
    if (filter_ == Filter::Unread)
        for (const auto& n : shown_) kept_.insert(n.id);
    const std::size_t n = src_.markAllRead();
    refresh();
    say(n == 0 ? std::string("Tout est d\xC3\xA9j\xC3\xA0 lu.")
               : std::to_string(n) + (n > 1 ? " notifications marqu\xC3\xA9" "es comme lues." : " notification marqu\xC3\xA9" "e comme lue."));
    return n;
}

std::size_t AiNotificationsView::clearRead() {
    const std::size_t n = src_.clearRead();
    kept_.clear();
    refresh();
    say(n == 0 ? std::string("Aucune notification lue \xC3\xA0 effacer.")
               : std::to_string(n) + (n > 1 ? " notifications lues effac\xC3\xA9" "es." : " notification lue effac\xC3\xA9" "e."));
    return n;
}

void AiNotificationsView::openRowMenu(int row, gfx::Point at) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    menuRow_ = row;
    const auto& n = shown_[static_cast<std::size_t>(row)];
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = n.title;
    items.push_back(head);
    ui::PopupMenu::Item open;
    open.label = buttonLabel(n);
    open.icon = ui::Icon::GoTo;
    open.shortcut = "Entr\xC3\xA9" "e";
    open.id = kMenuFollow;
    if (n.action.empty()) {
        open.enabled = false;
        open.disabledReason = "elle n'a pas de lien";
    }
    items.push_back(open);
    ui::PopupMenu::Item read;
    read.label = n.read ? "Marquer comme non lue" : "Marquer comme lue";
    read.icon = n.read ? ui::Icon::Eye : ui::Icon::Ok;
    read.shortcut = "Espace";
    read.id = n.read ? kMenuUnread : kMenuRead;
    items.push_back(read);
    ui::PopupMenu::Item copy;
    copy.label = "Copier";
    copy.icon = ui::Icon::Copy;
    copy.shortcut = "Ctrl+C";
    copy.id = kMenuCopy;
    items.push_back(copy);
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    ui::PopupMenu::Item del;
    del.label = "Supprimer";
    del.icon = ui::Icon::Delete;
    del.shortcut = "Suppr";
    del.id = kMenuDelete;
    items.push_back(del);
    menu_->setItems(std::move(items));
    menu_->openAtSurface(toSurface(at));
}

void AiNotificationsView::menuChosen(int id) {
    const int row = menuRow_;
    menuRow_ = -1;
    switch (id) {
        case kMenuFollow: (void)follow(row); break;
        case kMenuRead: (void)setRead(row, true); break;
        case kMenuUnread: (void)setRead(row, false); break;
        case kMenuCopy: (void)copyRow(row); break;
        case kMenuDelete: (void)removeRow(row); break;
        default: break;
    }
}

// ======================================================================== les textes ==
std::string AiNotificationsView::rowText(std::size_t row) const {
    if (row >= shown_.size()) return {};
    const auto& n = shown_[row];
    std::string s = notifs::dayLabel(n.at, now_ ? now_ : notifs::nowSeconds()) + " | " + notifs::timeLabel(n.at) + " | " + n.category + " | " + n.title
                    + " | " + n.detail + " | " + (n.count > 1 ? countBadge(n.count) : std::string{}) + " | " + n.button + " | "
                    + (n.read ? "lue" : "non lue");
    return s;
}

std::vector<std::string> AiNotificationsView::lines() const {
    std::vector<std::string> out;
    for (const auto& l : lines_) out.push_back(l.day ? "== " + l.text : rowText(static_cast<std::size_t>(l.row)));
    return out;
}

std::string AiNotificationsView::liveTooltip(gfx::Point mouse) const {
    for (const auto& [r, t] : tips_)
        if (r.contains(mouse)) return t;
    const int row = rowAt(mouse);
    if (row < 0) return {};
    const auto& n = shown_[static_cast<std::size_t>(row)];
    std::string tip = n.title;
    if (n.count > 1) tip += " (" + std::to_string(n.count) + " fois, la derni\xC3\xA8re \xC3\xA0 " + notifs::timeLabel(n.at) + ")";
    if (!n.detail.empty()) tip += "\n" + n.detail;
    tip += "\n" + std::string(n.read ? "Lue" : "Non lue") + " \xC2\xB7 " + toneWord(n.tone);
    if (!n.action.empty()) tip += " \xC2\xB7 " + buttonLabel(n) + " : Entr\xC3\xA9" "e ou un double-clic";
    return tip;
}

// ========================================================================= le dessin ==
void AiNotificationsView::drawBell(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const float s = std::min(b.w, b.h) / 18.f;
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    r.fillRoundedRect({cx - 5.5f * s, cy - 7.f * s, 11.f * s, 12.f * s}, c, 5.f * s);
    r.fillRect({cx - 5.5f * s, cy - 1.f * s, 11.f * s, 5.f * s}, c);
    r.fillRoundedRect({cx - 7.5f * s, cy + 3.f * s, 15.f * s, 2.5f * s}, c, 1.f * s);
    r.fillRoundedRect({cx - 1.8f * s, cy + 6.f * s, 3.6f * s, 3.2f * s}, c, 1.6f * s);
}

void AiNotificationsView::onPaint(const ui::PaintContext& ctx) {
    XPG_MESURE("ia.peinture.notifications");   // 1.22.2
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    tips_.clear();
    ctx.r.fillRect(b, c.panelBg);
    // ---- la barre : les filtres
    ctx.r.fillRect({bar_.x + 1.f, bar_.y, bar_.w - 1.f, bar_.h}, c.rowAltBg);
    ctx.r.fillRect({bar_.x + 1.f, bar_.bottom() - 1.f, bar_.w - 1.f, 1.f}, c.border);
    const auto chip = [&](const gfx::Rect& r, const std::string& label, std::size_t n, bool on, gfx::Color badge) {
        ctx.r.fillRoundedRect(r, on ? alpha(c.accent, ctx.theme.isDark() ? 70 : 45) : alpha(c.border, 90), r.h / 2.f);
        if (on) ctx.r.strokeRect(r, alpha(c.accent, 160), 1.f);
        const std::string count = std::to_string(n);
        const float lw = ctx.r.measure(label + " ", kMeta).width, cw = ctx.r.measure(count, kMeta).width;
        const float x = r.x + (r.w - lw - cw) / 2.f, y = r.y + (r.h - ctx.r.lineHeight(kMeta)) / 2.f;
        ctx.r.drawText({x, y}, label, kMeta, on ? c.text : c.textMuted);
        boldText(ctx.r, {x + lw, y}, count, kMeta, n > 0 ? badge : c.textMuted);
    };
    chip(chipAll_, "Toutes", all_.size(), filter_ == Filter::All, c.text);
    const bool alert = std::any_of(all_.begin(), all_.end(), [](const notifs::Item& n) { return !n.read && n.tone == "error"; });
    chip(chipUnread_, "Non lues", unread_, filter_ == Filter::Unread, alert ? c.error : c.accent);
    tips_.emplace_back(chipAll_, "Toutes les notifications de l'Assistant, la plus r\xC3\xA9" "cente d'abord");
    tips_.emplace_back(chipUnread_, "Seulement celles que tu n'as pas encore vues (une ligne choisie devient lue)");
    // ---- la liste
    ctx.r.pushClip(list_);
    const float lhMeta = ui::lineHeight(kMeta) + 2.f;
    const float lhBody = ui::lineHeight(kBody) + 2.f;
    if (shown_.empty()) {
        const float y = list_.y + 24.f;
        drawBell(ctx.r, {list_.x + (list_.w - 28.f) / 2.f, y, 28.f, 28.f}, alpha(c.textMuted, 150));
        const std::string head = filter_ == Filter::Unread && !all_.empty() ? std::string("Tout est lu.") : std::string("Aucune notification.");
        ctx.r.drawText({list_.x + (list_.w - ctx.r.measure(head, kBody).width) / 2.f, y + 40.f}, head, kBody, c.text);
        const auto text = wrap("L'Assistant note ici ce qui s'est pass\xC3\xA9 pendant que tu regardais ailleurs : une r\xC3\xA9ponse ou une "
                               "proposition pr\xC3\xAAte, une proposition appliqu\xC3\xA9" "e ou en \xC3\xA9" "chec, une erreur du service, une "
                               "pi\xC3\xA8" "ce jointe illisible, des souvenirs \xC3\xA0 valider, un scan r\xC3\xA9seau termin\xC3\xA9, l'analyse IA "
                               "d'un incident.",
                               kMeta, list_.w - 2.f * kPad - 20.f, 6);
        float ty = y + 40.f + lhBody + 6.f;
        for (const auto& t : text) {
            ctx.r.drawText({list_.x + (list_.w - ctx.r.measure(t, kMeta).width) / 2.f, ty}, t, kMeta, c.textMuted);
            ty += lhMeta;
        }
    }
    const float space = listBar_.space(list_, content_, viewH());
    for (const auto& l : lines_) {
        gfx::Rect r = rowArea(l);
        r.w -= space;
        if (r.bottom() <= list_.y || r.y >= list_.bottom()) continue;
        if (l.day) {
            ctx.r.drawText({r.x + kPad, r.y + (r.h - ctx.r.lineHeight(kMeta)) / 2.f + 2.f}, l.text, kMeta, c.textMuted);
            ctx.r.fillRect({r.x + kPad, r.bottom() - 2.f, r.w - 2.f * kPad, 1.f}, alpha(c.border, 160));
            continue;
        }
        const auto& n = shown_[static_cast<std::size_t>(l.row)];
        const bool sel = l.row == selected_;
        const bool hot = l.row == hover_;
        if (sel) ctx.r.fillRect(r, alpha(c.selectionBg, ctx.theme.isDark() ? 150 : 110));
        else if (hot) ctx.r.fillRect(r, ctx.theme.brand.hover);
        const gfx::Color tone = toneColor(ctx.theme, n.tone);
        // le filet du ton : plein tant qu'elle n'est pas lue
        ctx.r.fillRect({r.x + 1.f, r.y + 4.f, 3.f, r.h - 8.f}, n.read ? alpha(tone, 70) : tone);
        if (sel) ctx.r.fillRect({r.x + 1.f, r.y, 3.f, r.h}, c.accent);
        const gfx::Color ink = n.read ? c.textMuted : c.text;
        float y = r.y + 6.f;
        ui::drawIcon(ctx.r, toneIcon(n.tone), {r.x + 12.f, y + 1.f, 14.f, 14.f}, n.read ? alpha(tone, 170) : tone);
        // l'heure, la categorie ; a droite : x3, et le point des non lues
        float right = r.right() - kPad;
        if (!n.read) {
            ctx.r.fillRoundedRect({right - 7.f, y + (lhMeta - 7.f) / 2.f, 7.f, 7.f}, c.accent, 3.5f);
            right -= 13.f;
        }
        if (n.count > 1) {
            const std::string x = countBadge(n.count);
            const float bw = ctx.r.measure(x, kMeta).width + 10.f;
            const gfx::Rect pill{right - bw, y, bw, lhMeta};
            ctx.r.fillRoundedRect(pill, alpha(tone, ctx.theme.isDark() ? 60 : 40), lhMeta / 2.f);
            ctx.r.drawText({pill.x + 5.f, pill.y + 1.f}, x, kMeta, c.text);
            tips_.emplace_back(pill, std::to_string(n.count) + " fois (la m\xC3\xAAme notification, rendue non lue \xC3\xA0 chaque fois)");
            right = pill.x - 6.f;
        }
        ctx.r.drawText({r.x + kLeft, y}, elide(l.meta, kMeta, std::max(10.f, right - r.x - kLeft)), kMeta, c.textMuted);
        y += lhMeta;
        const std::string title = elide(n.title, kBody, r.right() - kPad - r.x - kLeft);
        if (!n.read) boldText(ctx.r, {r.x + kLeft, y}, title, kBody, ink);
        else ctx.r.drawText({r.x + kLeft, y}, title, kBody, ink);
        y += lhBody;
        for (const auto& d : l.detail) {
            ctx.r.drawText({r.x + kLeft, y}, d, kMeta, c.textMuted);
            y += lhMeta;
        }
        if (l.buttonW > 0.f) {
            const auto br = buttonArea(l);
            const bool overButton = hot && hoverButton_;
            ctx.r.fillRoundedRect(br, overButton ? alpha(c.accent, 60) : alpha(c.accent, 22), 4.f);
            ctx.r.strokeRect(br, alpha(c.accent, overButton ? 200 : 120), 1.f);
            const std::string label = elide(n.button + " \xE2\x80\xBA", kMeta, br.w - 12.f);
            ctx.r.drawText({br.x + 8.f, br.y + (br.h - ctx.r.lineHeight(kMeta)) / 2.f}, label, kMeta, c.accent);
        }
        ctx.r.fillRect({r.x + kLeft, r.bottom() - 1.f, r.w - kLeft - kPad, 1.f}, alpha(c.border, 110));
    }
    ctx.r.popClip();
    listBar_.paint(ctx, list_, content_, viewH(), scroll_);
    if (focused() && selected_ < 0 && !shown_.empty()) {
        // le clavier est ici : un cadre discret le dit (Haut / Bas choisissent)
        ctx.r.strokeRect({list_.x + 1.f, list_.y + 1.f, list_.w - 2.f, list_.h - 2.f}, alpha(c.accent, 90), 1.f);
    }
}

// ========================================================================= les evenements ==
ui::EventResult AiNotificationsView::onEvent(const ui::InputEvent& ev) {
    if (listBar_.handle(*this, ev, list_, content_, viewH(), scroll_)) return ui::EventResult::Consumed;
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        bool onButton = false;
        if (h >= 0)
            if (const auto br = buttonRect(static_cast<std::size_t>(h)); br.w > 0.f) onButton = br.contains(m->pos);
        if (h != hover_ || onButton != hoverButton_) {
            hover_ = h;
            hoverButton_ = onButton;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos) && !w->mods.ctrl) {
        scroll_ = std::clamp(scroll_ - w->dy * 40.f, 0.f, std::max(0.f, content_ - viewH()));
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button == ui::MouseButton::Left && chipAll_.contains(d->pos)) {
            setFilter(Filter::All);
            return ui::EventResult::Consumed;
        }
        if (d->button == ui::MouseButton::Left && chipUnread_.contains(d->pos)) {
            setFilter(Filter::Unread);
            return ui::EventResult::Consumed;
        }
        const int row = rowAt(d->pos);
        if (row >= 0) {
            takeFocus();
            if (d->button == ui::MouseButton::Right) {
                (void)select(row);
                openRowMenu(selected_, d->pos);
                return ui::EventResult::Consumed;
            }
            if (d->button != ui::MouseButton::Left) return ui::EventResult::Consumed;
            const auto br = buttonRect(static_cast<std::size_t>(row));
            if ((br.w > 0.f && br.contains(d->pos)) || d->clickCount >= 2) (void)follow(row);
            else (void)select(row);
            return ui::EventResult::Consumed;
        }
        if (list_.contains(d->pos) || bar_.contains(d->pos)) {
            takeFocus();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
        const int n = static_cast<int>(shown_.size());
        const auto go = [&](int row) {
            if (n > 0) (void)select(std::clamp(row, 0, n - 1));
            return ui::EventResult::Consumed;
        };
        if (ui::isContextMenuKey(*k)) {
            if (selected_ >= 0)
                if (const auto r = rowRect(static_cast<std::size_t>(selected_)); r.w > 0.f) openRowMenu(selected_, {r.x + 40.f, r.y + 12.f});
            return ui::EventResult::Consumed;
        }
        if (k->mods.ctrl && k->key == ui::Key::C) {
            (void)copyRow(selected_);
            return ui::EventResult::Consumed;
        }
        if (k->mods.ctrl || k->mods.alt) return ui::EventResult::Ignored;
        switch (k->key) {
            case ui::Key::Down: return go(selected_ < 0 ? 0 : selected_ + 1);
            case ui::Key::Up: return go(selected_ < 0 ? 0 : selected_ - 1);
            case ui::Key::Home: return go(0);
            case ui::Key::End: return go(n - 1);
            case ui::Key::PageDown: return go(selected_ < 0 ? 0 : selected_ + 5);
            case ui::Key::PageUp: return go(selected_ < 0 ? 0 : selected_ - 5);
            case ui::Key::Return:
                if (selected_ >= 0) (void)follow(selected_);
                return ui::EventResult::Consumed;
            case ui::Key::Delete:
                if (selected_ >= 0) (void)removeRow(selected_);
                return ui::EventResult::Consumed;
            case ui::Key::Space:
                if (selected_ >= 0) (void)setRead(selected_, !shown_[static_cast<std::size_t>(selected_)].read);
                return ui::EventResult::Consumed;
            default: break;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
