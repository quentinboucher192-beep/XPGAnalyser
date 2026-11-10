// =============================================================================
//  license_admin/gui/AdminsBoard.cpp - 1.47.0 : CE QUE LES PAGES DES ADMINS
//  PARTAGENT (les donnees, la vue qui defile, les morceaux dessines)
// -----------------------------------------------------------------------------
//  Voir AdminsBoard.hpp. Les chiffres du journal : JournalStats (sur le fil de
//  l'interface : rien que des fichiers deja la) ; ceux du service : fetchService
//  et fetchCard (LENTS : un travail sans bruit). Les heures : Paris.
// =============================================================================
#include "AdminsBoard.hpp"

#include "../../deploy/Crypto.hpp"
#include "../../licensing/Admins.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace license_admin::gui::adminsboard {

namespace adm = licensing::admins;

// ============================================================================ les donnees
Color personColor(const Person& p) {
    if (p.kind == Person::Kind::Pending) return Color::rgb(0xA3ADBF);
    return ch::colors::admin(p.color);
}

const Person* Data::person(const std::string& key) const {
    for (const auto& p : people)
        if (p.key == key) return &p;
    return nullptr;
}
const js::AdminStats* Data::stats(const std::string& key) const {
    for (const auto& a : ov.admins)
        if (a.id == key) return &a;
    return nullptr;
}
const AdminFigures* Data::fig(const std::string& key) const {
    const auto it = figures.find(key);
    return it == figures.end() ? nullptr : &it->second;
}
int Data::activeCount() const {
    int n = 0;
    for (const auto& p : people) n += p.active() ? 1 : 0;
    return n;
}
int Data::retiredCount() const {
    int n = 0;
    for (const auto& p : people) n += p.kind != Person::Kind::Pending && p.retired ? 1 : 0;
    return n;
}
int Data::pendingCount() const {
    int n = 0;
    for (const auto& p : people) n += p.kind == Person::Kind::Pending ? 1 : 0;
    return n;
}

void setPeriod(Data& d, const Choice& c, Seconds now, const std::string& allFrom) {
    d.choice = c;
    d.now = now;
    const js::TimeZone tz = js::TimeZone::paris();
    js::Period p;
    switch (c.range) {
        case statskit::Range::Days7: p = js::lastDays(now, 7, tz); break;
        case statskit::Range::Days30: p = js::lastDays(now, 30, tz); break;
        case statskit::Range::Days90: p = js::lastDays(now, 90, tz); break;
        case statskit::Range::Months12: p = js::lastMonths(now, 12, tz); break;
        case statskit::Range::All: {
            const js::Period today = js::lastDays(now, 1, tz);
            if (!js::between(allFrom, today.to(), p)) p = js::lastMonths(now, 24, tz);
            break;
        }
        case statskit::Range::Dates:
            if (!js::between(c.from, c.to, p)) p = js::lastDays(now, 30, tz);
            break;
    }
    d.period = p;
    d.previous = js::previousOf(p);
    d.statsPeriod = {p.from(), p.to()};
    d.statsBefore = {d.previous.from(), d.previous.to()};
}

void computeLocal(const Workspace& w, Data& d) {
    js::Inputs in;
    const JournalView view = w.journalLines();
    in.lines = js::linesOf(view);
    std::vector<std::string> editors;
    for (const auto& k : w.editorKeys()) editors.push_back(deploy::crypto::hex(k));
    for (const auto& p : d.people) {
        if (p.kind == Person::Kind::Pending) {
            in.pending.push_back({p.name, p.received});
            continue;
        }
        js::Admin a;
        a.id = p.key;
        a.name = p.name;
        a.keys = {p.key};
        if (p.kind == Person::Kind::Plus)
            for (const auto& e : editors)
                if (e != p.key) a.keys.push_back(e);
        a.pc = p.pc;
        a.machine = p.machine;
        a.rights = p.rights;
        a.since = p.since;
        a.retired = p.retired;
        a.profile = p.profile;
        a.plus = p.kind == Person::Kind::Plus;
        in.admins.push_back(std::move(a));
    }
    in.period = d.period;
    in.now = d.now;
    in.latestAdminVersion = d.latestAdmin;
    for (const auto& [key, f] : d.figures) {
        js::ServiceFigures s;
        s.licences = f.licences;
        s.licencesBefore = f.licencesBefore;
        s.requests = f.requests;
        s.requestsBefore = f.requestsBefore;
        s.meanDelay = f.meanDelay;
        s.meanDelayBefore = f.meanDelayBefore;
        s.medianDelay = f.medianDelay;
        s.amount = f.amount;
        s.amountBefore = f.amountBefore;
        s.orders = f.orders;
        if (f.hasUsage) {
            s.adminVersion = f.usage.softwareVersion;
            s.lastSeen = f.usage.lastSeen;
        }
        in.service[key] = s;
    }
    d.ov = js::overview(in);
    d.journalLines = static_cast<int>(in.lines.size());
    d.journal = !in.lines.empty();
    d.lines = std::move(in.lines);
}

namespace {
// La cle d'un admin du service (son identifiant, sa cle, son nom).
std::string keyOf(const ServiceAsk& ask, const std::string& id, const std::string& key, const std::string& name) {
    for (std::size_t i = 0; i < ask.keys.size(); ++i)
        if ((!id.empty() && i < ask.ids.size() && ask.ids[i] == id) || (!key.empty() && ask.keys[i] == key)) return ask.keys[i];
    for (std::size_t i = 0; i < ask.keys.size(); ++i)
        if (!name.empty() && i < ask.names.size() && ask.names[i] == name) return ask.keys[i];
    return {};
}
double partTotal(const stats::Series* s, const std::string& id) {
    if (!s || s->refused) return kNaN;
    if (const stats::Part* p = s->part(id)) return p->total;
    return s->parts.empty() ? kNaN : 0.0;
}
} // namespace

OpResult fetchService(const Workspace& w, const ServiceAsk& ask, ServiceReply& out) {
    // 1. leurs jours et leurs heures (GET /admin/stats/admins)
    stats::AdminsReply ar;
    OpResult r = statskit::fetchAdmins(w, ask.period, ar);
    if (!r.ok) {
        out.note = r.message;
        out.tooOld = statskit::saysTooOld(r.message);
        return r;
    }
    std::map<std::string, std::string> idToKey;   // xpg-... -> la cle
    for (const auto& u : ar.admins) {
        const std::string k = keyOf(ask, u.id, u.key, u.name);
        if (k.empty()) continue;
        AdminFigures& f = out.figures[k];
        f.hasUsage = true;
        f.usage = u;
        f.serviceId = u.id;
        idToKey[u.id] = k;
    }
    for (std::size_t i = 0; i < ask.keys.size() && i < ask.ids.size(); ++i)
        if (!ask.ids[i].empty() && !idToKey.count(ask.ids[i])) idToKey[ask.ids[i]] = ask.keys[i];
    // 2. ce que chacun a produit : la periode, puis celle d'avant (par admin)
    stats::SeriesQuery q;
    q.measures = {"licences_nouvelles", "demandes_traitees", "demandes_acceptees", "demandes_refusees", "ventes_montant", "ventes_commandes",
                  "demandes_delai_moyen", "demandes_attente", "demandes_plus_ancienne"};
    q.period = ask.period;
    q.step = statskit::fitStep(ask.period, stats::Step::Day);
    q.split = "admin";
    q.compare = stats::Compare::Previous;
    stats::SeriesReply now;
    r = statskit::fetchSeries(w, q, now);
    if (!r.ok) {
        out.note = r.message;
        out.tooOld = statskit::saysTooOld(r.message);
        return r;
    }
    stats::SeriesQuery qb = q;
    qb.period = ask.before;
    qb.compare = stats::Compare::None;
    qb.measures = {"licences_nouvelles", "demandes_traitees", "ventes_montant", "ventes_commandes"};
    stats::SeriesReply before;
    const OpResult rb = statskit::fetchSeries(w, qb, before);
    for (const auto& [id, key] : idToKey) {
        AdminFigures& f = out.figures[key];
        if (f.serviceId.empty()) f.serviceId = id;
        f.licences = partTotal(now.find("licences_nouvelles"), id);
        f.requests = partTotal(now.find("demandes_traitees"), id);
        f.accepted = partTotal(now.find("demandes_acceptees"), id);
        f.refused = partTotal(now.find("demandes_refusees"), id);
        f.amount = partTotal(now.find("ventes_montant"), id);
        f.orders = partTotal(now.find("ventes_commandes"), id);
        if (rb.ok) {
            f.licencesBefore = partTotal(before.find("licences_nouvelles"), id);
            f.requestsBefore = partTotal(before.find("demandes_traitees"), id);
            f.amountBefore = partTotal(before.find("ventes_montant"), id);
            f.ordersBefore = partTotal(before.find("ventes_commandes"), id);
        }
    }
    // l'en-tete : les totaux
    const auto total = [](const stats::Series* s) { return s && !s->refused ? s->total : kNaN; };
    const auto beforeTotal = [](const stats::Series* s) { return s && !s->refused && s->hasComparison() ? s->beforeTotal : kNaN; };
    out.totals.pending = total(now.find("demandes_attente"));
    out.totals.oldest = total(now.find("demandes_plus_ancienne"));
    out.totals.meanDelay = total(now.find("demandes_delai_moyen"));
    out.totals.meanDelayBefore = beforeTotal(now.find("demandes_delai_moyen"));
    out.totals.licences = total(now.find("licences_nouvelles"));
    out.totals.licencesBefore = beforeTotal(now.find("licences_nouvelles"));
    out.totals.amount = total(now.find("ventes_montant"));
    out.totals.amountBefore = beforeTotal(now.find("ventes_montant"));
    out.totals.orders = total(now.find("ventes_commandes"));
    if (const auto* s = now.find("ventes_montant")) out.totals.amountPoints = s->points;
    if (const auto* s = now.find("licences_nouvelles")) out.totals.licencePoints = s->points;
    // 3. les delais de chacun (un filtre admin : la mesure ne se divise pas par admin pour un ratio)
    for (const auto& [id, key] : idToKey) {
        stats::SeriesQuery qd;
        qd.measures = {"demandes_delai_moyen", "demandes_delai_median", "demandes_delai_max"};
        qd.period = ask.period;
        qd.step = q.step;
        qd.compare = stats::Compare::Previous;
        qd.admin = id;
        stats::SeriesReply dr;
        if (!statskit::fetchSeries(w, qd, dr).ok) continue;
        AdminFigures& f = out.figures[key];
        f.meanDelay = total(dr.find("demandes_delai_moyen"));
        f.meanDelayBefore = beforeTotal(dr.find("demandes_delai_moyen"));
        f.medianDelay = total(dr.find("demandes_delai_median"));
        f.maxDelay = total(dr.find("demandes_delai_max"));
    }
    return OpResult::done({});
}

void applyService(Data& d, ServiceReply r) {
    d.serviceLoading = false;
    d.serviceLoaded = r.note.empty();
    d.serviceTooOld = r.tooOld;
    d.serviceNote = r.note;
    d.figures = std::move(r.figures);
    d.totals = std::move(r.totals);
}

OpResult fetchCard(const Workspace& w, const CardAsk& ask, CardFigures& out) {
    out = CardFigures{};
    if (ask.serviceId.empty()) {
        out.problem = "son identifiant au service n'est pas connu";
        return OpResult::fail(out.problem);
    }
    const auto total = [](const stats::Series* s) { return s && !s->refused ? s->total : kNaN; };
    const auto beforeTotal = [](const stats::Series* s) { return s && !s->refused && s->hasComparison() ? s->beforeTotal : kNaN; };
    // 1. ce qu'il a produit sur la periode (admin=<id>)
    stats::SeriesQuery q;
    q.measures = {"licences_prolongees", "licences_options", "licences_actives", "licences_expirees", "licences_revoquees", "pc_actifs",
                  "ventes_panier", "ventes_remboursements", "ventes_remboursements_nombre"};
    q.period = ask.period;
    q.step = statskit::fitStep(ask.period, stats::Step::Day);
    q.compare = stats::Compare::Previous;
    q.admin = ask.serviceId;
    stats::SeriesReply r1;
    OpResult r = statskit::fetchSeries(w, q, r1);
    if (!r.ok) {
        out.problem = r.message;
        return r;
    }
    out.prolonged = total(r1.find("licences_prolongees"));
    out.options = total(r1.find("licences_options"));
    out.active = total(r1.find("licences_actives"));
    out.expired = total(r1.find("licences_expirees"));
    out.revoked = total(r1.find("licences_revoquees"));
    out.pcs = total(r1.find("pc_actifs"));
    out.basket = total(r1.find("ventes_panier"));
    out.basketBefore = beforeTotal(r1.find("ventes_panier"));
    out.refunds = total(r1.find("ventes_remboursements"));
    out.refundsCount = total(r1.find("ventes_remboursements_nombre"));
    // 2. par offre, par client
    stats::SeriesQuery qo = q;
    qo.measures = {"licences_nouvelles"};
    qo.split = "offre";
    qo.compare = stats::Compare::None;
    stats::SeriesReply ro;
    if (statskit::fetchSeries(w, qo, ro).ok)
        if (const auto* s = ro.find("licences_nouvelles"))
            for (const auto& p : s->parts)
                if (p.key != "\xE2\x80\xA6") out.byOffer.push_back({p.key, p.total});
    stats::SeriesQuery qc = q;
    qc.measures = {"ventes_montant", "licences_nouvelles"};
    qc.split = "client";
    qc.compare = stats::Compare::Previous;
    stats::SeriesReply rc;
    if (statskit::fetchSeries(w, qc, rc).ok) {
        std::set<std::string> served;
        double all = 0, top = 0;
        if (const auto* s = rc.find("ventes_montant")) {
            all = s->total;
            for (const auto& p : s->parts) {
                if (p.key == "\xE2\x80\xA6" || p.key.empty()) continue;
                if (p.total > 0) served.insert(p.key);
                if (out.topClients.size() < 5) {
                    out.topClients.push_back({p.key, p.total});
                    top += p.total;
                }
            }
        }
        if (const auto* s = rc.find("licences_nouvelles"))
            for (const auto& p : s->parts)
                if (p.key != "\xE2\x80\xA6" && !p.key.empty() && p.total > 0) served.insert(p.key);
        out.clients = static_cast<double>(served.size());
        out.topShare = all > 0 ? top / all : kNaN;
    }
    // 3. ses courbes sur 12 semaines (elle, et les autres : par admin)
    stats::SeriesQuery qw;
    qw.measures = {"licences_nouvelles", "demandes_traitees", "ventes_montant"};
    qw.period = stats::lastDays(ask.now, 84);
    qw.step = stats::Step::Week;
    qw.split = "admin";
    qw.compare = stats::Compare::None;
    stats::SeriesReply rw;
    if (statskit::fetchSeries(w, qw, rw).ok) {
        out.weeks = rw.buckets;
        const auto split = [&](const char* m, std::vector<double>& mine, std::vector<double>& others) {
            const auto* s = rw.find(m);
            if (!s) return;
            mine.assign(s->points.size(), 0.0);
            others.assign(s->points.size(), 0.0);
            const stats::Part* p = s->part(ask.serviceId);
            for (std::size_t i = 0; i < s->points.size(); ++i) {
                const double all = known(s->points[i]) ? s->points[i] : 0.0;
                const double me = p && i < p->points.size() && known(p->points[i]) ? p->points[i] : 0.0;
                mine[i] = me;
                others[i] = std::max(0.0, all - me);
            }
        };
        split("licences_nouvelles", out.weekLicences, out.weekLicencesOthers);
        split("demandes_traitees", out.weekRequests, out.weekRequestsOthers);
        split("ventes_montant", out.weekAmount, out.weekAmountOthers);
    }
    stats::SeriesQuery qd;
    qd.measures = {"demandes_delai_moyen"};
    qd.period = stats::lastDays(ask.now, 84);
    qd.step = stats::Step::Week;
    qd.compare = stats::Compare::None;
    qd.admin = ask.serviceId;
    stats::SeriesReply rd;
    if (statskit::fetchSeries(w, qd, rd).ok)
        if (const auto* s = rd.find("demandes_delai_moyen")) out.weekDelay = s->points;
    out.loaded = true;
    return OpResult::done({});
}

// ============================================================================ la vue qui defile
void ScrolledView::scrollTo(float y) {
    offset_ = std::max(0.f, y);
    invalidateLayout();
    invalidate();
}

void ScrolledView::onLayout() {
    const Rect b = bounds();
    view_ = b;
    const Rect area{b.x, b.y, std::max(80.f, b.w - 14.f), b.h};
    content_ = place({area.x, area.y - offset_, area.w, area.h});
    scroll_.place(b, content_, b.h);
    const float c = scroll_.clamp(offset_);
    if (c != offset_) {
        offset_ = c;
        content_ = place({area.x, area.y - offset_, area.w, area.h});
    }
}

void ScrolledView::onPaint(const ui::PaintContext& ctx) { draw(ctx); }

void ScrolledView::onPaintOverlay(const ui::PaintContext& ctx) {
    if (scroll_.needed()) scroll_.paint(ctx, offset_);
}

ui::EventResult ScrolledView::onEvent(const ui::InputEvent& ev) {
    bool changed = false;
    if (scroll_.handle(*this, ev, offset_, &changed)) {
        if (changed) invalidateLayout();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void ScrolledView::put(ui::Widget* w, const Rect& r, bool shown) const {
    if (!w) return;
    if (!shown) {
        w->setVisibility(ui::Visibility::Collapsed);
        return;
    }
    w->setBounds(r);
    const bool inside = r.bottom() > view_.y + 1.f && r.y < view_.bottom() - 1.f;
    w->setVisibility(inside ? ui::Visibility::Visible : ui::Visibility::Hidden);
}

// ============================================================================ dessiner
void card(gfx::IRenderer& r, const ui::Theme& th, const Rect& b, float radius) { kit::cardBox(r, th, b, radius); }

void avatarOf(gfx::IRenderer& r, const Rect& box, const Person& p) {
    Color c = personColor(p);
    if (p.kind != Person::Kind::Pending && p.retired) c = ch::colors::mix(c, Color::rgb(0xFFFFFF), 0.25f);
    kit::avatar(r, box, p.name, c, box.w * 0.28f);
}

float text(gfx::IRenderer& r, float x, float y, const std::string& s, FontId f, Color c, float w) {
    if (s.empty()) return 0.f;
    const std::string t = w < 1e8f ? kit::fit(r, s, f, w) : s;
    if (t.empty()) return 0.f;
    r.drawText({x, y}, t, f, c);
    return r.measure(t, f).width;
}

float delta(gfx::IRenderer& r, float x, float y, double cur, double prev, bool upIsGood, FontId f, bool count) {
    if (!known(cur) || !known(prev)) return 0.f;
    ch::Delta d;
    if (count) {
        const double diff = cur - prev;
        d.text = ch::fr::signedInteger(diff);
        d.direction = diff > 0 ? 1 : diff < 0 ? -1 : 0;
        d.sense = diff == 0 ? 0 : ((diff > 0) == upIsGood ? 1 : -1);
    } else {
        d = ch::deltaOf(cur, prev, upIsGood);
    }
    if (d.text.empty()) return 0.f;
    static const ui::Theme theme = ui::Theme::byName("Light");
    return ch::paintDelta(r, theme, x, y, d, f);
}

float sourceTag(gfx::IRenderer& r, float x, float y, const std::string& tag) {
    const FontId f = gfx::semibold(FontId{10});
    const Color fg = tag == "J" ? kViolet : tag == "S" ? Color::rgb(0x2563EB) : kAmber;
    const Color bg = tag == "J" ? kVioletPale : tag == "S" ? Color::rgb(0xEFF6FF) : kAmberPale;
    const float w = std::max(18.f, r.measure(tag, f).width + 10.f);
    r.fillRoundedRect({x, y, w, 18.f}, bg, 5.f);
    r.drawText({x + (w - r.measure(tag, f).width) * 0.5f, y + (18.f - r.lineHeight(f)) * 0.5f}, tag, f, fg);
    return w;
}

float cardTitle(gfx::IRenderer& r, float x, float y, float w, const std::string& title, const std::string& sub, const std::string& tags) {
    float right = x + w;
    for (auto it = tags.rbegin(); it != tags.rend(); ++it) {
        if (*it == ' ') continue;
        const std::string t(1, *it);
        right -= 22.f;
        (void)sourceTag(r, right, y, t);
        right -= 4.f;
    }
    const FontId ft = gfx::semibold(FontId{15});
    (void)text(r, x, y, title, ft, kInk, right - x - 8.f);
    float h = r.lineHeight(ft) + 2.f;
    if (!sub.empty()) {
        const auto lines = kit::wrap(sub, FontId{12}, w);
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
            r.drawText({x, y + h}, lines[i], FontId{12}, kMuted);
            h += r.lineHeight(FontId{12}) + 1.f;
        }
    }
    return h + 6.f;
}


// ---- les liens, les boutons ----
float LinkButton::naturalWidth() const {
    const FontId f = gfx::semibold(FontId{13});
    return ui::measureWidth(text(), f) + (chevron_ ? 14.f : 0.f) + 4.f;
}
ui::SizeHint LinkButton::sizeHint() const {
    ui::SizeHint h = ui::Button::sizeHint();
    h.preferred = {naturalWidth(), 22.f};
    h.minimum = {std::min(naturalWidth(), 40.f), 18.f};
    return h;
}
void LinkButton::onPaint(const ui::PaintContext& ctx) {
    const Rect b = bounds();
    const FontId f = gfx::semibold(FontId{13});
    const Color c = enabled() ? color : kFaint;
    const float room = b.w - (chevron_ ? 14.f : 0.f);
    const std::string t = kit::fit(ctx.r, text(), f, room);
    const float y = b.y + (b.h - ctx.r.lineHeight(f)) * 0.5f;
    ctx.r.drawText({b.x, y}, t, f, c);
    const float tw = ctx.r.measure(t, f).width;
    if (chevron_) ctx.r.drawText({b.x + tw + 5.f, y}, "\xE2\x80\xBA", f, c);
    if (hovered() && enabled()) ctx.r.fillRect({b.x, y + ctx.r.lineHeight(f) - 1.f, tw, 1.f}, c);
    if (focused()) ctx.r.strokeRect(b.inset(-2.f, -1.f), kViolet.withAlpha(140), 1.f);
}

ui::Button& makeButton(ui::Widget& parent, std::string text, std::string id, ui::Button::Style style, ui::Icon icon) {
    return kit::button(parent, std::move(text), std::move(id), style, icon);
}
float buttonW(const ui::Widget& b, float minimum) { return std::max(minimum, b.sizeHint().preferred.w + 8.f); }

std::string rightShort(const std::string& id) {
    if (id == "licence_creer") return "Cr\xC3\xA9" "er et signer";
    if (id == "licence_prolonger") return "Prolonger";
    if (id == "licence_option") return "Ajouter une option";
    if (id == "licence_revoquer") return "R\xC3\xA9voquer";
    if (id == "poste_retirer") return "Retirer un PC";
    if (id == "offres") return "Offres";
    if (id == "options") return "Options";
    if (id == "annonces") return "Annonces";
    if (id == "demandes") return "Demandes";
    if (id == "codes") return "Revoir le code";
    if (id == "client_supprimer") return "Supprimer un client";
    if (id == "clients_exporter") return "Exporter les clients";
    if (id == "courriels") return "\xC3\x89" "crire aux clients";
    return adm::rightLabel(id);
}

bool olderVersion(const std::string& a, const std::string& b) {
    const auto parts = [](const std::string& v) {
        std::vector<int> out;
        int cur = 0;
        bool any = false;
        for (const char c : v) {
            if (c >= '0' && c <= '9') {
                cur = cur * 10 + (c - '0');
                any = true;
            } else if (c == '.') {
                out.push_back(cur);
                cur = 0;
            } else {
                break;
            }
        }
        if (any) out.push_back(cur);
        return out;
    };
    const auto x = parts(a), y = parts(b);
    if (x.empty() || y.empty()) return false;
    return x < y;
}

// ---- les textes ----
std::string num(double v) { return known(v) ? ch::fr::integer(v) : std::string("\xE2\x80\x94"); }
std::string euros(double cents) { return known(cents) ? ch::fr::euros(cents / 100.0) : std::string("\xE2\x80\x94"); }
std::string hoursText(double seconds) { return known(seconds) ? ch::fr::hours(seconds / 3600.0) : std::string("\xE2\x80\x94"); }
std::string signedCount(double cur, double prev) {
    if (!known(cur) || !known(prev)) return {};
    return ch::fr::signedInteger(cur - prev);
}
std::string pct(double cur, double prev) {
    if (!known(cur) || !known(prev)) return {};
    return ch::fr::deltaPercent(cur, prev);
}

namespace {
struct Local {
    std::int64_t day{0};
    int          minute{0};
};
Local localOf(Seconds t) {
    const js::TimeZone tz = js::TimeZone::paris();
    return {js::localDay(t, tz), js::localMinuteOfDay(t, tz)};
}
std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}
} // namespace

std::string clockOf(Seconds t) {
    const Local l = localOf(t);
    return two(l.minute / 60) + ":" + two(l.minute % 60);
}
std::string dayMonth(Seconds t) {
    int y = 0, m = 0, d = 0;
    ch::fr::civil(localOf(t).day, y, m, d);
    return two(d) + "/" + two(m);
}
std::string ago(Seconds now, Seconds t) {
    if (t <= 0) return "jamais";
    const Seconds s = std::max<Seconds>(0, now - t);
    if (s < 60) return "\xC3\xA0 l'instant";
    if (s < 3600) return "il y a " + std::to_string(s / 60) + " min";
    const Local a = localOf(now), b = localOf(t);
    if (a.day == b.day) return s < 4 * 3600 ? "il y a " + std::to_string(s / 3600) + " h" : "auj. " + clockOf(t);
    if (a.day == b.day + 1) return "hier " + clockOf(t);
    return dayMonth(t) + " " + clockOf(t);
}
std::string minuteText(int minutes) {
    if (minutes < 0) return "\xE2\x80\x94";
    return std::to_string(minutes / 60) + " h " + two(minutes % 60);
}
std::string shortSummary(const js::Ref& r) {
    if (!r.summary.empty()) return r.summary;
    return r.action + (r.object.empty() ? std::string() : " " + r.object);
}
std::string levelName(js::Level l) { return adm::levelLabel(l); }
Color levelColor(js::Level l) { return ch::colors::criticality(static_cast<int>(l)); }

} // namespace license_admin::gui::adminsboard
