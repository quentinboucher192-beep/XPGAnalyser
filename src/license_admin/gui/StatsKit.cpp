// =============================================================================
//  license_admin/gui/StatsKit.cpp - 1.47.0 : CE QUE LES PAGES DES STATISTIQUES
//  PARTAGENT (StatsKit.hpp)
// =============================================================================
#include "StatsKit.hpp"

#include "../../ui/widgets/FilterBar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace license_admin::gui::statskit {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

// Les couleurs de la maquette (ds.css).
constexpr Color kInk = Color::rgb(0x0F172A), kInk2 = Color::rgb(0x334155), kMuted = Color::rgb(0x64748B), kFaint = Color::rgb(0x94A3B8);
constexpr Color kLine = Color::rgb(0xE3E8EF), kSunken = Color::rgb(0xEEF2F7), kViolet = Color::rgb(0x6D4AFF);

std::mutex& fetchMutex() {
    static std::mutex m;
    return m;
}
Fetchers& fetchers() {
    static Fetchers f;
    return f;
}
Fetchers current() {
    std::lock_guard<std::mutex> lock(fetchMutex());
    return fetchers();
}

ListFilter& opened() {
    static ListFilter f;
    return f;
}

void civilOf(ch::fr::Day d, int& y, int& m, int& dd) { ch::fr::civil(d, y, m, dd); }


// La premiere FilterBar sous un widget (la liste d'une page).
ui::FilterBar* findBar(ui::Widget& w) {
    if (auto* b = dynamic_cast<ui::FilterBar*>(&w)) return b;
    for (const auto& c : w.children())
        if (auto* b = findBar(*c)) return b;
    return nullptr;
}

} // namespace

// ============================================================================ les appels
void setFetchersForTests(Fetchers f) {
    std::lock_guard<std::mutex> lock(fetchMutex());
    fetchers() = std::move(f);
}

bool hooked() {
    const Fetchers f = current();
    return static_cast<bool>(f.series) || static_cast<bool>(f.updates);
}

OpResult fetchSeries(const Workspace& w, const stats::SeriesQuery& q, stats::SeriesReply& out, std::string* raw) {
    const Fetchers f = current();
    if (f.series) return f.series(q, out);
    return w.statsSeries(q, out, raw);
}
OpResult fetchUpdates(const Workspace& w, const stats::UpdatesQuery& q, stats::UpdatesReply& out) {
    const Fetchers f = current();
    if (f.updates) return f.updates(q, out);
    return w.statsUpdates(q, out);
}
OpResult fetchAdmins(const Workspace& w, const stats::Period& p, stats::AdminsReply& out) {
    const Fetchers f = current();
    if (f.admins) return f.admins(p, out);
    if (f.series) return OpResult::done({});   // les essais sans les admins : rien
    return w.statsAdmins(p, out);
}
OpResult fetchGlobe(const Workspace& w, int periodDays, stats::GlobeReply& out) {
    const Fetchers f = current();
    if (f.globe) return f.globe(periodDays, out);
    if (f.series) return OpResult::fail("pas de globe d'essai");
    return w.statsGlobe(periodDays, out);
}
OpResult fetchSnapshot(const Workspace& w, stats::SnapshotReply& out) {
    const Fetchers f = current();
    if (f.snapshot) return f.snapshot(out);
    return w.statsSnapshot(out);
}

std::string blocked(const Workspace& w, bool* tooOld) {
    if (tooOld) *tooOld = false;
    if (const std::string note = w.statsServiceNote(); !note.empty()) {
        if (tooOld) *tooOld = true;
        return note;
    }
    if (hooked()) return {};
    if (!w.service().configured) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 : les statistiques viennent de lui (Service en ligne).";
    if (!w.service().problem.empty()) return "Le r\xC3\xA9glage du service est illisible (Service en ligne).";
    if (w.role() != Role::Plus && w.role() != Role::Admin)
        return "Ce poste n'a pas de cl\xC3\xA9 qui signe (ni l'admin+, ni un admin de la liste) : le service ne lui donne pas ses chiffres.";
    return {};
}

bool saysTooOld(const std::string& message) {
    return message.find("pour les statistiques et le globe") != std::string::npos;
}

bool runQuietly(const ShellHost& h, const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done) {
    if (h.runQuiet) return h.runQuiet(label, std::move(work), std::move(done));
    return h.runSlow && h.runSlow(label, std::move(work), std::move(done));
}

// ============================================================================ StatsJob
namespace {
std::vector<StatsJob*>& jobs() {
    static std::vector<StatsJob*> j;
    return j;
}
} // namespace

StatsJob::StatsJob() { jobs().push_back(this); }
StatsJob::~StatsJob() {
    auto& j = jobs();
    j.erase(std::remove(j.begin(), j.end(), this), j.end());
    bg_.wait();
}

bool StatsJob::start(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done) {
    if (running()) return false;
    if (hooked()) {   // les reponses d'essai : sur place
        const OpResult r = work();
        if (done) done(r);
        return true;
    }
    if (!bg_.start(label, std::move(work))) return false;
    done_ = std::move(done);
    pending_ = true;
    return true;
}

bool StatsJob::poll() {
    if (!pending_) return false;
    auto r = bg_.take();
    if (!r) return false;
    pending_ = false;
    auto done = std::move(done_);
    done_ = nullptr;
    if (done) done(*r);
    return true;
}

void StatsJob::wait() {
    bg_.wait();
    (void)poll();
}

void finishJobsForTests() {
    for (int round = 0; round < 8; ++round) {
        bool any = false;
        const auto copy = jobs();
        for (StatsJob* j : copy)
            if (j->running()) {
                j->wait();
                any = true;
            }
        if (!any) return;
    }
}

// ============================================================================ la periode
stats::Period periodOf(const Choice& c, Seconds now, const std::string& allFrom) {
    switch (c.range) {
        case Range::Days7: return stats::lastDays(now, 7);
        case Range::Days30: return stats::lastDays(now, 30);
        case Range::Days90: return stats::lastDays(now, 90);
        case Range::Months12: return stats::lastMonths(now, 12);
        case Range::All: {
            stats::Period p{allFrom, stats::dayOf(now)};
            if (p.from.empty() || p.from > p.to) p = stats::lastMonths(now, 24);
            return p;
        }
        case Range::Dates: {
            stats::Period p{c.from, c.to};
            if (p.from.empty() || p.to.empty() || stats::dayStart(p.from) < 0 || stats::dayStart(p.to) < 0) return stats::lastDays(now, 90);
            if (p.from > p.to) std::swap(p.from, p.to);
            return p;
        }
    }
    return stats::lastDays(now, 90);
}

std::string allFromOf(const Workspace& w) {
    Seconds first = 0;
    const auto take = [&first](Seconds t) {
        if (t > 0 && (first == 0 || t < first)) first = t;
    };
    for (const auto& e : w.licences()) {
        take(e.licence.issuedAt);
        for (const auto& p : e.licence.purchases) take(p.at);
    }
    for (const auto& o : w.orders().orders) take(o.paid ? o.paid : o.created);
    if (first == 0) return {};
    return stats::dayOf(first);
}

int daysOf(const stats::Period& p) {
    const Seconds a = stats::dayStart(p.from), b = stats::dayStart(p.to);
    if (a < 0 || b < 0 || b < a) return 0;
    return static_cast<int>((b - a) / 86400) + 1;
}

stats::Step fitStep(const stats::Period& p, stats::Step want) {
    const int days = daysOf(p);
    if (want == stats::Step::Day && days > 400) want = stats::Step::Week;
    if (want == stats::Step::Week && days / 7 + 2 > 400) want = stats::Step::Month;
    return want;
}

std::string periodText(const stats::Period& p) {
    if (p.from.empty() || p.to.empty()) return {};
    const std::string a = p.from.substr(0, 4) == p.to.substr(0, 4) ? ddmm(p.from) : ddmmyyyy(p.from);
    return a + " \xE2\x86\x92 " + ddmmyyyy(p.to);
}

const char* rangeKey(Range r) noexcept {
    switch (r) {
        case Range::Days7: return "7j";
        case Range::Days30: return "30j";
        case Range::Days90: return "90j";
        case Range::Months12: return "12mois";
        case Range::All: return "tout";
        case Range::Dates: return "dates";
    }
    return "90j";
}
const char* rangeLabel(Range r) noexcept {
    switch (r) {
        case Range::Days7: return "7\xC2\xA0j";
        case Range::Days30: return "30\xC2\xA0j";
        case Range::Days90: return "90\xC2\xA0j";
        case Range::Months12: return "12 mois";
        case Range::All: return "Tout";
        case Range::Dates: return "Deux dates\xE2\x80\xA6";
    }
    return "";
}
const char* compareLabel(stats::Compare c) noexcept {
    switch (c) {
        case stats::Compare::Previous: return "la p\xC3\xA9riode d'avant";
        case stats::Compare::LastYear: return "l'an dernier";
        case stats::Compare::None: return "rien";
    }
    return "";
}
const char* stepLabel(stats::Step s) noexcept {
    switch (s) {
        case stats::Step::Day: return "jour";
        case stats::Step::Week: return "semaine";
        case stats::Step::Month: return "mois";
    }
    return "";
}
std::string stepSmall(stats::Step s) { return std::string("par ") + stepLabel(s); }

// ============================================================================ les jours, les axes, les valeurs
ch::fr::Day dayNumber(const std::string& day) {
    const Seconds t = stats::dayStart(day);
    return t < 0 ? -1 : static_cast<ch::fr::Day>(t / 86400);
}
std::string ddmm(const std::string& day) {
    const ch::fr::Day d = dayNumber(day);
    return d < 0 ? std::string() : ch::fr::ddmm(d);
}
std::string ddmmyyyy(const std::string& day) {
    const ch::fr::Day d = dayNumber(day);
    return d < 0 ? std::string() : ch::fr::ddmmyyyy(d);
}

std::vector<ch::Category> axisOf(const std::vector<stats::Bucket>& buckets, stats::Step step, bool level) {
    std::vector<ch::Category> out;
    out.reserve(buckets.size());
    int lastYear = 0;
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        const ch::fr::Day a = dayNumber(buckets[i].from), b = dayNumber(buckets[i].to);
        ch::Category c;
        if (a < 0) {
            out.push_back(c);
            continue;
        }
        int y = 0, m = 0, d = 0;
        civilOf(a, y, m, d);
        switch (step) {
            case stats::Step::Day:
                c.label = ch::fr::ddmm(a);
                c.tip = level ? "Fin du " + ch::fr::dayLong(a) : ch::fr::dayLong(a);
                c.shade = false;
                break;
            case stats::Step::Week: {
                c.label = ch::fr::ddmm(a);
                const std::string range = "emaine du " + ch::fr::ddmm(a) + " au " + ch::fr::ddmm(b < 0 ? a : b);
                c.tip = (level ? "Fin de la s" : "S") + range;
                break;
            }
            case stats::Step::Month: {
                c.label = (m == 1 || i == 0 || y != lastYear) ? ch::fr::monthShort(m) + " " + std::to_string(y) : ch::fr::monthShort(m);
                std::string t = ch::fr::monthLong(m) + " " + std::to_string(y);
                if (d != 1 || (b >= 0 && i + 1 == buckets.size())) {
                    int y2 = 0, m2 = 0, d2 = 0;
                    civilOf(b < 0 ? a : b, y2, m2, d2);
                    if (d != 1 || d2 < 28) t += " (du " + ch::fr::ddmm(a) + " au " + ch::fr::ddmm(b < 0 ? a : b) + ")";
                }
                c.tip = level ? "Fin de " + t : t;
                break;
            }
        }
        lastYear = y;
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<std::pair<int, int>> ticksOf(std::size_t n) {
    if (n == 0) return {};
    if (n == 1) return {{0, 0}};
    if (n <= 4) {
        std::vector<std::pair<int, int>> t;
        for (std::size_t i = 0; i < n; ++i) t.push_back({static_cast<int>(i), 0});
        return t;
    }
    const int last = static_cast<int>(n) - 1;
    const int a = static_cast<int>(std::lround(last / 3.0)), b = static_cast<int>(std::lround(2.0 * last / 3.0));
    return {{0, 0}, {a, 0}, {b, 0}, {last, 0}};
}

int partialFrom(const std::vector<stats::Bucket>& buckets, stats::Step step, const std::string& today) {
    if (buckets.empty()) return -1;
    const stats::Bucket& last = buckets.back();
    if (last.from > today || last.to < today) return -1;
    (void)step;   // la periode qui contient aujourd'hui n'est pas finie : en cours (pale), quel que soit le pas
    return static_cast<int>(buckets.size()) - 1;
}

std::vector<double> valuesOf(const std::vector<double>& points, double scale) {
    std::vector<double> out;
    out.reserve(points.size());
    for (const double v : points) out.push_back(stats::isNumber(v) ? v * scale : ch::kNone);
    return out;
}
double sumOf(const std::vector<double>& v) {
    double s = 0;
    for (const double x : v)
        if (stats::isNumber(x)) s += x;
    return s;
}
double lastOf(const std::vector<double>& v) {
    for (auto it = v.rbegin(); it != v.rend(); ++it)
        if (stats::isNumber(*it)) return *it;
    return ch::kNone;
}

std::string mainName(stats::Compare c, const stats::Period& p) {
    if (c == stats::Compare::LastYear && p.to.size() >= 4) {
        if (p.from.size() >= 4 && p.from.substr(0, 4) != p.to.substr(0, 4)) return p.from.substr(0, 4) + "-" + p.to.substr(2, 2);
        return p.to.substr(0, 4);
    }
    return "cette p\xC3\xA9riode";
}
std::string compareName(stats::Compare c, bool longForm, const stats::Period& p) {
    if (c == stats::Compare::LastYear) {
        if (longForm && p.to.size() >= 4) {
            const int y = std::atoi(p.to.substr(0, 4).c_str()) - 1;
            return "l'an dernier (les m\xC3\xAAmes dates de " + std::to_string(y) + ")";
        }
        return "l'an dernier";
    }
    if (c == stats::Compare::Previous) return "la p\xC3\xA9riode d'avant";
    return {};
}
std::string againstText(stats::Compare c, const std::string& value, bool withWhen) {
    if (c == stats::Compare::None || value.empty()) return {};
    return "contre " + value + (withWhen ? " " + compareName(c) : std::string());
}

std::string partLabel(const std::string& split, const std::string& key) {
    if (key.empty()) return split == "option" ? "les offres" : "sans";
    if (key == "\xE2\x80\xA6") return "les autres";
    if (key == "options") return "Options";
    return key;
}
std::string offerColorKey(const std::string& key) {
    if (key == "options") return "offre:OPTIONS";
    return "offre:" + key;
}

int localHour(int utcHour, Seconds day) {
    if (utcHour < 0) return -1;
    const std::time_t t = static_cast<std::time_t>((day / 86400) * 86400 + utcHour * 3600);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm.tm_hour;
}

std::string seenText(Seconds now, Seconds at) {
    if (at <= 0) return "jamais";
    const auto local = [](Seconds s, std::tm& tm) {
        const std::time_t t = static_cast<std::time_t>(s);
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
    };
    std::tm n{}, a{};
    local(now, n);
    local(at, a);
    const long dn = static_cast<long>(n.tm_year) * 400 + n.tm_yday, da = static_cast<long>(a.tm_year) * 400 + a.tm_yday;
    if (n.tm_year == a.tm_year && dn == da) {
        if (now - at < 3600) {
            const long m = std::max<long>(1, static_cast<long>((now - at) / 60));
            return "il y a " + std::to_string(m) + " min";
        }
        char b[24];
        std::snprintf(b, sizeof b, "auj. %02d:%02d", a.tm_hour, a.tm_min);
        return b;
    }
    const long days = static_cast<long>((now - at) / 86400);
    if (days <= 1) return "hier";
    return "il y a " + std::to_string(days) + "\xC2\xA0j";
}

// ============================================================================ ouvrir une liste filtree
std::string openList(const ShellHost& h, ui::Widget& from, const ListFilter& f, Seconds now) {
    opened() = f;
    if (h.navigate) h.navigate(f.page);
    ui::Widget& root = from.topWidget();
    ui::Widget* page = root.findById("page." + f.page);
    if (!page) page = root.findById(f.page);
    ui::FilterBar* bar = page ? findBar(*page) : nullptr;
    std::string said = f.what;
    if (!bar) return said;
    bar->clearAll();
    if (!f.search.empty()) bar->setSearch(f.search);
    for (const auto& [title, value] : f.values) {
        const int field = bar->fieldByTitle(title);
        if (field >= 0) bar->filterOn(static_cast<std::size_t>(field), value);
    }
    if (!f.periodField.empty() && !f.from.empty()) {
        const int field = bar->fieldByTitle(f.periodField);
        if (field >= 0) {
            const Seconds a = stats::dayStart(f.from);
            if (f.to.empty() || f.to == f.from) {
                bar->filterOnDay(static_cast<std::size_t>(field), a + 12 * 3600);   // le jour (midi : le meme jour a l'heure du PC)
            } else {
                // La barre sait << les N derniers jours >> : depuis le premier jour de la periode jusqu'a aujourd'hui.
                const long days = static_cast<long>((stats::dayStart(stats::dayOf(now)) - a) / 86400) + 1;
                bar->setPeriod(static_cast<std::size_t>(field), static_cast<int>(std::max(1L, days)));
                if (stats::dayOf(now) > f.to) said += " (la liste montre depuis le " + ddmm(f.from) + " jusqu'\xC3\xA0 aujourd'hui)";
            }
        }
    }
    return said;
}

const ListFilter& lastOpened() { return opened(); }

// ============================================================================ SegGroup
class SegGroup::Seg final : public ui::Button {
public:
    Seg(SegGroup& g, std::string key, std::string label, std::string id, ui::Icon icon)
        : ui::Button(label, std::move(id)), group_(g), key_(std::move(key)), label_(std::move(label)), icon_(icon) {}
    void setLabel(std::string label) {
        label_ = std::move(label);
        setText(label_);
        invalidate();
    }
    [[nodiscard]] float natural() const {
        return std::ceil(ui::measureWidth(label_, gfx::semibold(FontId{12}))) + 20.f + (icon_ != ui::Icon::None ? 19.f : 0.f);
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const bool on = group_.current() == key_;
        if (on) {
            ctx.r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h}, Color::rgb(0x0F172A).withAlpha(14), 8.f);
            ctx.r.fillRoundedRect(b, Color::rgb(0xFFFFFF), 8.f);
        } else if (hovered()) {
            ctx.r.fillRoundedRect(b, Color::rgb(0xFFFFFF).withAlpha(110), 8.f);
        }
        const FontId f = gfx::semibold(FontId{12});
        const float tw = ctx.r.measure(label_, f).width;
        float x = b.x + (b.w - tw - (icon_ != ui::Icon::None ? 19.f : 0.f)) * 0.5f;
        const Color c = on ? kInk : kMuted;
        if (icon_ != ui::Icon::None) {
            ui::drawIcon(ctx.r, icon_, {x, b.y + (b.h - 13.f) * 0.5f, 13.f, 13.f}, c);
            x += 19.f;
        }
        ctx.r.drawText({std::round(x), std::round(b.y + (b.h - ctx.r.lineHeight(f)) * 0.5f)}, label_, f, c);
    }

private:
    SegGroup&   group_;
    std::string key_, label_;
    ui::Icon    icon_;
};

SegGroup::SegGroup(std::string id, std::vector<Item> items) : ui::Widget(id), items_(std::move(items)) {
    for (const auto& it : items_) {
        auto s = std::make_unique<Seg>(*this, it.key, it.label, id + "." + it.key, it.icon);
        ui::Button* raw = &static_cast<ui::Button&>(addChild(std::move(s)));
        const std::string key = it.key;
        links_ += raw->clicked->connect([this, key] {
            setCurrent(key);
            if (onPick) onPick(key);
        });
        buttons_.push_back(raw);
    }
    if (!items_.empty()) current_ = items_.front().key;
}

void SegGroup::setCurrent(const std::string& key) {
    current_ = key;
    invalidate();
    for (auto* b : buttons_) b->invalidate();
}

void SegGroup::setLabel(const std::string& key, std::string label) {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].key == key) {
            if (items_[i].label == label) return;
            items_[i].label = label;
            static_cast<Seg*>(buttons_[i])->setLabel(std::move(label));
            invalidateLayout();
        }
}

float SegGroup::widthOf(const std::vector<std::string>& labels) {
    float w = 6.f;
    for (const auto& l : labels) w += std::ceil(ui::measureWidth(l, gfx::semibold(FontId{12}))) + 20.f + 2.f;
    return w;
}

float SegGroup::preferredWidth() const {
    float w = 6.f;
    for (auto* b : buttons_) w += static_cast<const Seg*>(b)->natural() + 2.f;
    return w;
}

ui::Button* SegGroup::button(const std::string& key) const {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].key == key) return buttons_[i];
    return nullptr;
}

void SegGroup::onLayout() {
    const Rect b = bounds();
    const float want = preferredWidth();
    const float k = want > b.w && want > 0.f ? b.w / want : 1.f;
    float x = b.x + 3.f;
    for (auto* btn : buttons_) {
        const float w = static_cast<Seg*>(btn)->natural() * k;
        btn->setBounds({x, b.y + 3.f, w, b.h - 6.f});
        x += w + 2.f;
    }
}

void SegGroup::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRoundedRect(bounds(), kSunken, 10.f); }

// ============================================================================ TabButton
TabButton::TabButton(std::string label, std::string id, ui::Icon icon, std::string count)
    : ui::Button(label, std::move(id)), label_(std::move(label)), count_(std::move(count)), icon_(icon) {}

float TabButton::preferredWidth() const {
    float w = std::ceil(ui::measureWidth(label_, gfx::semibold(FontId{14}))) + (icon_ != ui::Icon::None ? 22.f : 0.f) + 4.f;
    if (!count_.empty()) w += std::ceil(ui::measureWidth(count_, gfx::semibold(FontId{11}))) + 20.f;
    return w;
}

void TabButton::onPaint(const ui::PaintContext& ctx) {
    const Rect b = bounds();
    const Color c = selected_ ? kInk : hovered() ? kInk2 : kMuted;
    float x = b.x + 2.f;
    const float cy = b.y + (b.h - 3.f) * 0.5f;
    if (icon_ != ui::Icon::None) {
        ui::drawIcon(ctx.r, icon_, {x, cy - 8.f, 16.f, 16.f}, c);
        x += 22.f;
    }
    const FontId f = gfx::semibold(FontId{14});
    ctx.r.drawText({x, std::round(cy - ctx.r.lineHeight(f) * 0.5f)}, label_, f, c);
    x += ctx.r.measure(label_, f).width + 8.f;
    if (!count_.empty()) {
        const FontId cf = gfx::semibold(FontId{11});
        const float w = ctx.r.measure(count_, cf).width + 12.f;
        ctx.r.fillRoundedRect({x, cy - 8.5f, w, 17.f}, kSunken, 8.5f);
        ctx.r.drawText({x + 6.f, std::round(cy - ctx.r.lineHeight(cf) * 0.5f)}, count_, cf, kFaint);
    }
    if (selected_) ctx.r.fillRect({b.x, b.bottom() - 2.f, b.w, 2.f}, kViolet);
}

// ============================================================================ FitCard
FitCard::FitCard(std::string id, std::string title, std::string small) : ch::ChartCard(std::move(id), title, small), heading_(std::move(title)) {
    if (!small.empty()) smalls_ = {std::move(small)};
}

void FitCard::setText(std::string value, ch::Delta delta, std::vector<std::string> compares) {
    text_ = std::move(value);
    delta_ = std::move(delta);
    compares_.clear();
    for (auto& c : compares)
        if (!c.empty()) compares_.push_back(std::move(c));
    apply();
}

void FitCard::setSmalls(std::string title, std::vector<std::string> smalls) {
    heading_ = std::move(title);
    smalls_.clear();
    for (auto& c : smalls)
        if (!c.empty()) smalls_.push_back(std::move(c));
    apply();
}

float sideWidthOf(const std::vector<ch::SideRow>& rows) {
    float w = 0.f;
    for (const auto& r : rows)
        w = std::max(w, 17.f + ui::measureWidth(r.label, FontId{12}) + 8.f + ui::measureWidth(r.value, gfx::semibold(FontId{12})) +
                            (r.faint.empty() ? 0.f : 5.f + ui::measureWidth(r.faint, FontId{11})) + 6.f);
    return w;
}

void FitCard::setSideRows(std::vector<ch::SideRow> rows, std::vector<ch::SideRow> narrow) {
    side_ = std::move(rows);
    sideNarrow_ = std::move(narrow);
    sideShown_ = -1;
    apply();
}

void FitCard::onLayout() {
    apply();
    ch::ChartCard::onLayout();
}

void FitCard::apply() {
    // les mesures de ChartCard (ChartsMore.cpp) : 14 px de chaque cote ; le titre en 13 demi-gras, sa precision en 11 ; la
    // valeur en 19 demi-gras puis 7 px, l'ecart puis 6 px, la comparaison en 12.
    const float w = bounds().w - 28.f;
    std::string small;
    std::string compare = compares_.empty() ? std::string() : compares_.front();
    ch::Delta delta = delta_;
    if (w > 40.f) {
        const float tw = ui::measureWidth(heading_, gfx::semibold(FontId{13})) + 6.f;
        for (const auto& c : smalls_)
            if (tw + ui::measureWidth(c, FontId{11}) <= w) {
                small = c;
                break;
            }
        compare.clear();
        if (!text_.empty()) {
            const float used = ui::measureWidth(text_, gfx::semibold(FontId{19})) + 7.f;
            const float dw = delta.text.empty() ? 0.f : ch::deltaWidth(delta) + 6.f;
            bool found = false;
            for (const auto& c : compares_)   // avec l'ecart
                if (used + dw + ui::measureWidth(c, FontId{12}) <= w) {
                    compare = c;
                    found = true;
                    break;
                }
            if (!found && used + dw > w) delta = {};   // l'ecart ne tient pas : la valeur seule, puis une comparaison
            if (!found && delta.text.empty())
                for (const auto& c : compares_)
                    if (used + ui::measureWidth(c, FontId{12}) <= w) {
                        compare = c;
                        break;
                    }
        }
    } else if (!smalls_.empty()) {
        small = smalls_.front();
    }
    if (!side_.empty()) {
        const float half = bounds().w * 0.5f - 14.f;
        const int want = (bounds().w <= 0.f || sideWidthOf(side_) <= half || sideNarrow_.empty()) ? 0 : 1;
        if (want != sideShown_) {
            sideShown_ = want;
            const auto& rows = want == 0 ? side_ : sideNarrow_;
            setSide(rows, sideWidthOf(rows));
        }
    }
    const std::string sig = heading_ + "|" + small + "|" + text_ + "|" + delta.text + "|" + compare;
    shownCompare_ = compare;
    deltaShown_ = !delta.text.empty();
    if (sig == applied_) return;
    applied_ = sig;
    setTitle(heading_, small);
    setValue(text_, delta, compare);
}

// ============================================================================ FitBarList
void FitBarList::setData(ch::BarListData data) {
    full_ = std::move(data);
    extras_ = -1;
    apply();
}

void FitBarList::onLayout() { apply(); }

void FitBarList::apply() {
    const float w = bounds().w;
    int want = 1;
    if (w > 20.f) {
        for (const auto& r : full_.rows) {
            if (r.extra.empty()) continue;
            const std::string v = !r.valueText.empty() ? r.valueText : full_.format ? full_.format(r.value) : ch::fr::integer(r.value);
            const float need = ui::measureWidth(r.label, FontId{12}) + ui::measureWidth(v, gfx::semibold(FontId{12})) +
                               ui::measureWidth(r.extra, FontId{11}) + 22.f;
            if (need > w) want = 0;
        }
    }
    if (want == extras_) return;
    extras_ = want;
    ch::BarListData d = full_;
    if (!want)
        for (auto& r : d.rows) {
            if (r.tip.empty() && !r.extra.empty()) r.tip = r.label + " : " + (!r.valueText.empty() ? r.valueText : d.format ? d.format(r.value) : ch::fr::integer(r.value)) + ", " + r.extra;
            r.extra.clear();
        }
    ch::BarList::setData(std::move(d));
}

// ============================================================================ les cartes, les bandeaux
void paintCard(gfx::IRenderer& r, const ui::Theme& th, const Rect& b) {
    r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h + 1.f}, th.brand.cardShadow, 12.f);
    r.fillRoundedRect(b, kLine, 12.f);
    r.fillRoundedRect(b.inset(1.f, 1.f), Color::rgb(0xFFFFFF), 11.f);
}

namespace {
std::vector<std::string> wrapText(const std::string& text, FontId f, float w) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > w) {
            lines.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}
} // namespace

float bannerHeight(const std::string& text, float width, float reserveRight) {
    const auto lines = wrapText(text, FontId{14}, std::max(80.f, width - 56.f - reserveRight));
    return std::max(44.f, static_cast<float>(lines.size()) * 20.f + 24.f);
}

float paintBanner(gfx::IRenderer& r, const ui::Theme& th, const Rect& b, const std::string& text, bool warning, float reserveRight) {
    const Color tone = warning ? Color::rgb(0xD97706) : Color::rgb(0x2A78D6);
    r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h + 1.f}, th.brand.cardShadow, 12.f);
    r.fillRoundedRect(b, tone.withAlpha(110), 12.f);
    r.fillRoundedRect(b.inset(1.f, 1.f), warning ? Color::rgb(0xFFF8EB) : Color::rgb(0xF2F7FE), 11.f);
    ui::drawIcon(r, warning ? ui::Icon::Warning : ui::Icon::Info, {b.x + 16.f, b.y + 13.f, 18.f, 18.f}, tone);
    const auto lines = wrapText(text, FontId{14}, std::max(80.f, b.w - 56.f - reserveRight));
    float y = b.y + 12.f;
    for (const auto& l : lines) {
        r.drawText({b.x + 44.f, y}, l, FontId{14}, kInk);
        y += 20.f;
    }
    return b.h;
}

void reloadView(ui::Widget& view) {
    if (auto* s = dynamic_cast<Reloadable*>(&view)) s->reload();
}

} // namespace license_admin::gui::statskit
