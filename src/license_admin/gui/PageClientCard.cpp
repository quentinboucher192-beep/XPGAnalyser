// =============================================================================
//  license_admin/gui/PageClientCard.cpp - 1.47.0 : LA FICHE D'UN CLIENT ET SES
//  COURBES (Clients > un client ; la maquette validee, planche 10)
// -----------------------------------------------------------------------------
//  On y arrive de la page Clients (<< Sa fiche et ses courbes >> ; un double clic
//  dans la fiche du client choisi) : requestPageFocus("client", "client:<nom>").
//    - en tete : son nom, sa ville (vue par le service : GET /admin/globe), << client
//      depuis le ... >>, une licence qui finit bientot ; ses licences actives, ses
//      places, ses PC, qui l'a servi ; << Nouvelle licence... >>, << Prolonger... >>
//      (celle qui finit le plus tot) ;
//    - les onglets : Vue d'ensemble, puis Licences, PC, Commandes, Demandes,
//      Journal - chacun ouvre sa liste filtree sur ce client (la barre de filtres
//      des listes) ; la periode (30 j, 12 mois, tout depuis son debut) ;
//    - a gauche : son identite (societe, ville, client depuis, facturation ; les
//      contacts de ses commandes), ses chiffres (licences, PC, achats, demandes),
//      ses PC (la barre de filtres : chercher, la version) ;
//    - ses courbes : ses licences et leurs echeances (la frise, de l'archive),
//      ses PC actifs (et les places achetees), ses mises a jour (la version de ses
//      PC, chaque semaine), ses achats et le montant cumule (DEUX graphiques,
//      jamais deux axes), ses demandes (par admin), qui l'a servi (le journal des
//      admins de ce poste).
//  Les courbes viennent du service (GET /admin/stats/serie avec client=<nom>,
//  par runQuiet) ; un service d'avant la 1.47.0 : un seul avertissement et
//  << Deployer le service >> ; ce qui vient de l'archive reste (la frise, ses PC,
//  qui l'a servi).
// =============================================================================
#include "ListKit.hpp"
#include "StatsKit.hpp"

#include "../../ui/widgets/FilterBar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace license_admin::gui {

namespace {

using namespace statskit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr Color kInk = Color::rgb(0x0F172A), kInk2 = Color::rgb(0x334155), kMuted = Color::rgb(0x64748B), kFaint = Color::rgb(0x94A3B8);
constexpr Color kLine = Color::rgb(0xE3E8EF), kSunken = Color::rgb(0xEEF2F7), kViolet = Color::rgb(0x6D4AFF), kAmber = Color::rgb(0xD97706);
constexpr Color kPageBg = Color::rgb(0xF4F6FA);
const std::string kDash = "\xE2\x80\x94";

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
bool sameClient(const std::string& a, const std::string& b) { return lowerAscii(trimmed(a)) == lowerAscii(trimmed(b)); }

// La version d'un logiciel dit par la demande (<< IHM Studio 1.30.0 >> -> << 1.30.0 >>).
std::string versionOf(const std::string& software) {
    // le dernier mot qui commence par un chiffre (<< IHM Studio 1.33.0 (essai) >> -> << 1.33.0 >>)
    std::string best, word;
    for (std::size_t i = 0; i <= software.size(); ++i) {
        const char c = i < software.size() ? software[i] : ' ';
        if (c == ' ') {
            if (!word.empty() && word[0] >= '0' && word[0] <= '9') best = word;
            word.clear();
        } else {
            word += c;
        }
    }
    return best;
}
// Comparer deux versions (1.45.2 < 1.46.0).
int versionCompare(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') x = x * 10 + (a[i++] - '0');
        while (j < b.size() && b[j] != '.') y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y ? -1 : 1;
        ++i;
        ++j;
    }
    return 0;
}
// << 1.47 >> d'une version << 1.47.0 >>.
std::string minorOf(const std::string& v) {
    const auto a = v.find('.');
    if (a == std::string::npos) return v;
    const auto b = v.find('.', a + 1);
    return b == std::string::npos ? v : v.substr(0, b);
}

// ---- ce que l'archive de ce poste sait du client ----
struct Local {
    std::string                      name;
    std::vector<const LicenceEntry*> licences;
    std::vector<KnownDevice>         devices;    // ses PC (actifs d'abord)
    std::vector<const shop::Order*>  orders;
    std::vector<licensing::Purchase> purchases;
    std::vector<listkit::Did>        journal;    // les lignes du journal sur ses licences
    Seconds                          since{0};
    std::string                      sinceBy;
    int                              active{0}, expired{0}, places{0}, pending{0}, requests{0};
    const LicenceEntry*              soonest{nullptr};   // la licence active qui finit le plus tot
    std::string                      latestVersion;     // la plus recente version vue chez tous les clients
};

Local readLocal(const Workspace& w, const std::string& name) {
    Local l;
    l.name = name;
    for (const auto& e : w.licences())
        if (sameClient(e.licence.customer, name)) {
            l.licences.push_back(&e);
            if (Workspace::inForce(e)) {
                ++l.active;
                l.places += e.licence.maxDevices;
                if (e.licence.expiresAt > 0 && (!l.soonest || e.licence.expiresAt < l.soonest->licence.expiresAt)) l.soonest = &e;
            } else if (e.expired) {
                ++l.expired;
            }
            if (e.licence.issuedAt > 0 && (l.since == 0 || e.licence.issuedAt < l.since)) l.since = e.licence.issuedAt;
            for (const auto& p : e.licence.purchases)
                if (p.at > 0 && (l.since == 0 || p.at < l.since)) l.since = p.at;
        }
    // la licence la plus ancienne d'abord : son premier achat (l'emission en place peut etre une prolongation)
    const auto firstOf = [](const LicenceEntry* e) {
        Seconds t = e->licence.issuedAt;
        for (const auto& p : e->licence.purchases)
            if (p.licenseId == e->licence.licenseId && p.at > 0) t = std::min(t, p.at);
        return t;
    };
    std::sort(l.licences.begin(), l.licences.end(), [&](const LicenceEntry* a, const LicenceEntry* b) { return firstOf(a) < firstOf(b); });
    for (const auto& d : w.devices()) {
        const std::string v = versionOf(d.software);
        if (!v.empty() && (l.latestVersion.empty() || versionCompare(v, l.latestVersion) > 0)) l.latestVersion = v;
        if (sameClient(d.customer, name) && d.state != "demande") l.devices.push_back(d);
    }
    std::stable_sort(l.devices.begin(), l.devices.end(), [](const KnownDevice& a, const KnownDevice& b) {
        if ((a.state == "active") != (b.state == "active")) return a.state == "active";
        return a.activatedAt > b.activatedAt;
    });
    for (const auto& o : w.orders().orders)
        if (sameClient(o.company, name)) l.orders.push_back(&o);
    l.purchases = w.purchaseHistory(name);
    for (const auto& c : w.clients())
        if (sameClient(c.name, name)) l.pending = c.pendingRequests;
    for (const auto& r : w.service().requests)
        if (sameClient(r.customer, name)) ++l.requests;
    const listkit::WhoDid who(w);
    for (const auto* e : l.licences)
        for (auto& d : who.history(e->licence.licenseId)) l.journal.push_back(d);
    std::sort(l.journal.begin(), l.journal.end(), [](const auto& a, const auto& b) { return a.when < b.when; });
    if (!l.licences.empty()) {
        const auto h = who.history(l.licences.front()->licence.licenseId);
        if (!h.empty()) l.sinceBy = h.front().who;
    }
    return l;
}

// ---- une carte simple : un titre, une precision, un lien a droite, un contenu ----
class Box final : public ui::Widget {
public:
    Box(std::string id, std::string title, std::string small = {}) : ui::Widget(std::move(id)), title_(std::move(title)), small_(std::move(small)) {}
    void setTitle(std::string t, std::string small) {
        title_ = std::move(t);
        small_ = std::move(small);
        invalidate();
    }
    std::function<void(const ui::PaintContext&, const Rect&)> paintBody;

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        paintCard(ctx.r, ctx.theme, b);
        if (!title_.empty()) {
            const FontId tf = gfx::semibold(FontId{14});
            ctx.r.drawText({b.x + 16.f, b.y + 14.f}, title_, tf, kInk);
            if (!small_.empty())
                ctx.r.drawText({b.x + 16.f + ctx.r.measure(title_, tf).width + 8.f, b.y + 16.f}, ch::detail::fit(ctx.r, small_, FontId{12}, b.w * 0.6f), FontId{12},
                               kFaint);
        }
        if (paintBody) paintBody(ctx, b);
    }

private:
    std::string title_, small_;
};

// Une puce de l'en-tete (<< Lyon, FR >>, << client depuis le ... >>, << 1 licence finit dans 25 j >>).
float chip(gfx::IRenderer& r, float x, float cy, const std::string& text, int tone, ui::Icon icon = ui::Icon::None) {
    const FontId f = gfx::semibold(FontId{12});
    const float iw = icon != ui::Icon::None ? 18.f : 0.f;
    const float w = r.measure(text, f).width + 18.f + iw;
    const Color bg = tone == 1 ? Color::rgb(0xF1EDFF) : tone == -1 ? Color::rgb(0xFFF6E6) : kSunken;
    const Color fg = tone == 1 ? Color::rgb(0x4B2FD6) : tone == -1 ? Color::rgb(0xB45309) : kInk2;
    r.fillRoundedRect({x, cy - 12.f, w, 24.f}, bg, 7.f);
    if (icon != ui::Icon::None) ui::drawIcon(r, icon, {x + 8.f, cy - 7.f, 14.f, 14.f}, fg);
    r.drawText({x + 9.f + iw, cy - r.lineHeight(f) * 0.5f}, text, f, fg);
    return w;
}

// Le corps de la fiche (sous les onglets) : ses enfants y sont coupes ; plus haut que la place (une fenetre basse), il
// defile a la molette. La page pose ses enfants (onLayout), decales de `scroll`.
class Body final : public ui::Widget {
public:
    using ui::Widget::Widget;
    float                 scroll{0.f}, content{0.f};
    std::function<void()> moved;

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        if (content > b.h + 1.f) {
            const float th = std::max(30.f, b.h * b.h / content);
            const float ty = b.y + (b.h - th) * (scroll / std::max(1.f, content - b.h));
            ctx.r.fillRoundedRect({b.right() - 5.f, ty, 4.f, th}, kFaint.withAlpha(110), 2.f);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!bounds().contains(w->pos) || content <= bounds().h) return ui::EventResult::Ignored;
            scroll = std::clamp(scroll - w->dy * 48.f, 0.f, content - bounds().h);
            if (moved) moved();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
};

class ClientCardPage final : public Page {
public:
    explicit ClientCardPage(ShellHost host) : Page("page.client", std::move(host)) {
        installSamplesFromEnvironment([this] { return ws().now(); });
        newLicence_ = add(std::make_unique<ui::Button>("Nouvelle licence\xE2\x80\xA6", "client.nouvelle-licence"));
        newLicence_->setIcon(ui::Icon::Plus);
        extend_ = add(std::make_unique<ui::Button>("Prolonger\xE2\x80\xA6", "client.prolonger"));
        extend_->setIcon(ui::Icon::Refresh);
        back_ = add(std::make_unique<ui::Button>("Clients", "client.retour"));
        back_->setStyle(ui::Button::Style::Flat);
        back_->setIcon(ui::Icon::Undo);
        links_ += newLicence_->clicked->connect([this] {
            if (host_.openDialog && !name_.empty()) host_.openDialog(makeLicenceWizard(host_, {}, {}, name_));
        });
        links_ += extend_->clicked->connect([this] {
            if (host_.openDialog && local_.soonest) host_.openDialog(makeLicenceWizard(host_, {}, local_.soonest->licence.licenseId, name_));
        });
        links_ += back_->clicked->connect([this] {
            requestPageFocus("clients", "client:" + name_);
            if (host_.navigate) host_.navigate("clients");
        });
        deploy_ = add(std::make_unique<ui::Button>("D\xC3\xA9ployer le service", "client.deployer"));
        deploy_->setStyle(ui::Button::Style::Primary);
        deploy_->setIcon(ui::Icon::ArrowUp);
        links_ += deploy_->clicked->connect([this] {
            if (deployer::running() || (host_.busy && host_.busy())) return;
            std::weak_ptr<bool> alive = alive_;
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive] {
                if (!alive.expired()) refresh();
            }));
        });
        const char* tabs[6][2] = {{"vue", "Vue d'ensemble"}, {"licences", "Licences"}, {"pc", "PC"}, {"commandes", "Commandes"}, {"demandes", "Demandes"}, {"journal", "Journal"}};
        const ui::Icon icons[6] = {ui::Icon::Chart, ui::Icon::Document, ui::Icon::Station, ui::Icon::Mail, ui::Icon::Import, ui::Icon::Task};
        for (int i = 0; i < 6; ++i) {
            auto t = std::make_unique<TabButton>(tabs[i][1], std::string("client.onglet.") + tabs[i][0], icons[i]);
            TabButton* raw = &static_cast<TabButton&>(addChild(std::move(t)));
            const std::string key = tabs[i][0];
            links_ += raw->clicked->connect([this, key] { openTab(key); });
            tabs_.push_back(raw);
        }
        tabs_.front()->setSelected(true);
        period_ = &static_cast<SegGroup&>(addChild(std::make_unique<SegGroup>(
            "client.periode", std::vector<SegGroup::Item>{{"30j", "30\xC2\xA0j"}, {"12mois", "12 mois"}, {"tout", "Tout"}})));
        period_->setCurrent("tout");
        period_->onPick = [this](const std::string&) {
            sig_.clear();
            load();
        };
        body_ = add(std::make_unique<Body>("client.corps"));
        body_->moved = [this] { invalidateLayout(); };
        // a gauche
        identity_ = addBody(std::make_unique<Box>("client.identite", "Identit\xC3\xA9"));
        identity_->paintBody = [this](const ui::PaintContext& ctx, const Rect& b) { paintIdentity(ctx, b); };
        numbers_ = addBody(std::make_unique<Box>("client.chiffres", ""));
        numbers_->paintBody = [this](const ui::PaintContext& ctx, const Rect& b) { paintNumbers(ctx, b); };
        pcs_ = addBody(std::make_unique<Box>("client.pc", "Ses PC"));
        pcs_->paintBody = [this](const ui::PaintContext& ctx, const Rect& b) { paintPcs(ctx, b); };
        toActivations_ = addBody(std::make_unique<ui::Button>("Activations \xE2\x80\xBA", "client.pc.activations"));
        toActivations_->setStyle(ui::Button::Style::Flat);
        links_ += toActivations_->clicked->connect([this] { openTab("pc"); });
        bar_ = addBody(std::make_unique<ui::FilterBar>("client.pc.barre", std::vector<ui::FilterBar::Field>{
                                                                            {.title = "PC", .kind = ui::FilterBar::Kind::Text},
                                                                            {.title = "Version", .chip = true, .noun = "une version"},
                                                                            {.title = "Vu", .kind = ui::FilterBar::Kind::Text}}));
        bar_->setNoun("PC");
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return local_.devices.size(); };
            src.text = [this](std::size_t r, std::size_t f) {
                if (r >= local_.devices.size()) return std::string();
                const auto& d = local_.devices[r];
                if (f == 0) return d.name.empty() ? d.fingerprint : d.name;
                if (f == 1) return versionOf(d.software);
                return seenText(ws().now(), d.activatedAt);
            };
            bar_->setSource(std::move(src));
        }
        links_ += bar_->changed->connect([this] { invalidate(); });
        // les courbes
        timeline_ = chart<ch::Timeline>("client.licences", "Ses licences et leurs \xC3\xA9" "ch\xC3\xA9" "ances", "chaque p\xC3\xA9riode : sign\xC3\xA9" "e, prolong\xC3\xA9" "e", timelineCard_);
        pcsChart_ = chart<ch::TimeChart>("client.pc-actifs", "Ses PC actifs", "\xC3\xA0 la fin de chaque mois", pcsCard_);
        updates_ = chart<ch::TimeChart>("client.maj", "Ses mises \xC3\xA0 jour", "la version de ses PC, chaque semaine", updatesCard_);
        // ses achats et le montant cumule : DEUX graphiques dans la meme carte (jamais deux axes)
        purchasesCard_ = &static_cast<FitCard&>(body_->addChild(std::make_unique<FitCard>("client.achats.carte", "Ses achats", "par trimestre")));
        purchases_ = &static_cast<ch::TimeChart&>(body_->addChild(std::make_unique<ch::TimeChart>("client.achats")));
        cumul_ = &static_cast<ch::TimeChart&>(body_->addChild(std::make_unique<ch::TimeChart>("client.cumul")));
        requests_ = chart<ch::TimeChart>("client.demandes", "Ses demandes", "par trimestre", requestsCard_);
        served_ = chart<ch::BarList>("client.servi", "Qui l'a servi", "depuis le d\xC3\xA9" "but", servedCard_);
        const auto pick = [this](const std::string& what) {
            return [this, what](const ch::Pick& p) { picked(what, p); };
        };
        timeline_->setOnPick(pick("licences"));
        pcsChart_->setOnPick(pick("pc"));
        updates_->setOnPick(pick("pc"));
        purchases_->setOnPick(pick("commandes"));
        cumul_->setOnPick(pick("commandes"));
        requests_->setOnPick(pick("demandes"));
        served_->setOnPick(pick("journal"));
        refresh();
    }

    [[nodiscard]] std::string title() const override { return name_.empty() ? std::string("Un client") : name_; }

    void refresh() override {
        if (const std::string f = takePageFocus("client"); f.rfind("client:", 0) == 0) {
            if (f.substr(7) != name_) {
                name_ = f.substr(7);
                sig_.clear();
                data_.clear();
                errors_.clear();
                city_.clear();
                bar_->clearAll();
            }
        }
        if (name_.empty()) {   // arrive sans client (--page client) : le premier client de l'archive
            const auto all = ws().clients();
            if (!all.empty()) name_ = all.front().name;
        }
        local_ = readLocal(ws(), name_);
        bar_->refreshCounts();
        bool old = false;
        note_ = blocked(ws(), &old);
        tooOld_ = old;
        fillLocal();
        load();
        invalidateLayout();
        invalidate();
    }

    // ---- les essais ----
    [[nodiscard]] const std::string& client() const noexcept { return name_; }
    [[nodiscard]] std::size_t devicesShown() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < local_.devices.size(); ++i) n += bar_->accepts(i) ? 1 : 0;
        return n;
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 1000.f ? 20.f : 28.f;
        const float x0 = b.x + pad, w = std::max(300.f, b.w - 2.f * pad);
        float y = b.y + 16.f;
        headY_ = y;
        // les boutons de l'en-tete, a droite
        float rx = x0 + w;
        for (ui::Button* btn : {extend_, newLicence_}) {
            const float bw = btn->sizeHint().preferred.w + 10.f;
            rx -= bw;
            btn->setBounds({rx, y + 6.f, bw, 34.f});
            rx -= 8.f;
        }
        extend_->setEnabled(local_.soonest != nullptr);
        headRight_ = rx - 8.f;
        back_->setBounds({x0, y - 2.f, 0.f, 0.f});
        back_->setVisibility(ui::Visibility::Collapsed);
        y += 60.f;
        // les onglets et la periode
        float tx = x0;
        for (auto* t : tabs_) {
            const float tw = t->preferredWidth();
            t->setBounds({tx, y, tw, 32.f});
            tx += tw + 22.f;
        }
        const float pw = period_->preferredWidth();
        period_->setBounds({x0 + w - pw, y - 2.f, pw, 34.f});
        y += 36.f;
        tabsBottom_ = y;
        y += 12.f;
        bannerRect_ = {};
        deploy_->setVisibility(tooOld_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (!note_.empty()) {
            const float bw = tooOld_ ? deploy_->sizeHint().preferred.w + 12.f : 0.f;
            const float bh = bannerHeight(note_, w, bw + 16.f);
            bannerRect_ = {x0, y, w, bh};
            if (tooOld_) deploy_->setBounds({x0 + w - bw - 12.f, y + (bh - 32.f) * 0.5f, bw, 32.f});
            y += bh + 10.f;
        }
        const float gap = 12.f;
        const float bottom = b.bottom() - 10.f;
        // le corps : sous les onglets, coupe ; des hauteurs au moins (une fenetre basse : il defile)
        const Rect bodyRect{x0 - 4.f, y - 4.f, w + 8.f, std::max(60.f, bottom - y + 8.f)};
        body_->setBounds(bodyRect);
        const float lw = std::clamp(w * 0.28f, 280.f, 400.f);
        const float idH = local_.orders.empty() ? 168.f : std::min(250.f, 168.f + 50.f * static_cast<float>(std::min<std::size_t>(2, contacts().size())) + 22.f);
        const float minRight = 200.f + 200.f + 250.f + 2 * gap, minLeft = idH + gap + 132.f + gap + 220.f;
        const float avail = std::max({bottom - y, minRight, minLeft});
        body_->content = avail + 8.f;
        body_->scroll = std::clamp(body_->scroll, 0.f, std::max(0.f, body_->content - bodyRect.h));
        y -= body_->scroll;
        // a gauche : l'identite, les chiffres, ses PC
        identity_->setBounds({x0, y, lw, idH});
        numbers_->setBounds({x0, y + idH + gap, lw, 132.f});
        const float py = y + idH + gap + 132.f + gap;
        pcs_->setBounds({x0, py, lw, std::max(120.f, y + avail - py)});
        const float aw = toActivations_->sizeHint().preferred.w + 6.f;
        toActivations_->setBounds({x0 + lw - aw - 10.f, py + 8.f, aw, 26.f});
        const float bh = bar_->heightFor(lw - 24.f);
        bar_->setBounds({x0 + 12.f, py + 40.f, lw - 24.f, bh});
        pcsListY_ = py + 40.f + bh + 6.f;
        // les courbes
        const float cx = x0 + lw + gap, cw = x0 + w - cx;
        const float r1 = std::max(200.f, std::floor(avail * 0.34f)), r2 = std::max(200.f, std::floor(avail * 0.32f)), r3 = avail - r1 - r2 - 2 * gap;
        timelineCard_->setBounds({cx, y, cw, r1});
        const float half = std::floor((cw - gap) * 0.5f);
        pcsCard_->setBounds({cx, y + r1 + gap, half, r2});
        updatesCard_->setBounds({cx + half + gap, y + r1 + gap, cw - half - gap, r2});
        const float third = std::floor((cw - 2 * gap) / 3.f);
        const float y3 = y + r1 + r2 + 2 * gap;
        purchasesCard_->setBounds({cx, y3, third, r3});
        requestsCard_->setBounds({cx + third + gap, y3, third, r3});
        servedCard_->setBounds({cx + 2 * (third + gap), y3, cw - 2 * (third + gap), r3});
        // le cumul : la seconde moitie de la carte des achats (deux graphiques, jamais deux axes)
        const Rect pc = purchasesCard_->bounds();
        const Rect inner = purchasesCard_->chartRect();
        const float split = std::floor(inner.y + inner.h * 0.52f);
        cumulTitleY_ = split + 4.f;
        purchases_->setBounds({inner.x, inner.y, inner.w, split - inner.y - 4.f});
        cumul_->setBounds({inner.x, split + 22.f, inner.w, std::max(30.f, pc.bottom() - 10.f - split - 22.f)});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        (void)job_.poll();
        const Rect b = bounds();
        ctx.r.fillRect(b, kPageBg);
        const float pad = b.w < 1000.f ? 20.f : 28.f;
        const float x0 = b.x + pad, w = std::max(300.f, b.w - 2.f * pad);
        // l'en-tete
        const Rect icon{x0, headY_ + 2.f, 46.f, 46.f};
        ctx.r.fillRoundedRect(icon, kViolet, 12.f);
        ui::drawIcon(ctx.r, ui::Icon::User, icon.inset(12.f, 12.f), Color::rgb(0xFFFFFF));
        float x = icon.right() + 14.f;
        const FontId nf = gfx::semibold(FontId{24});
        const std::string name = ch::detail::fit(ctx.r, name_.empty() ? std::string("Aucun client") : name_, nf, std::max(80.f, headRight_ - x - 120.f));
        ctx.r.drawText({x, headY_}, name, nf, kInk);
        float cx = x + ctx.r.measure(name, nf).width + 14.f;
        const float cy = headY_ + 14.f;
        const auto room = [&](const std::string& t) { return cx + ctx.r.measure(t, gfx::semibold(FontId{12})).width + 40.f < headRight_; };
        if (!city_.empty() && room(city_)) cx += chip(ctx.r, cx, cy, city_, 0, ui::Icon::Pin) + 8.f;
        if (local_.since > 0) {
            const std::string t = "client depuis le " + dateFr(local_.since);
            if (room(t)) cx += chip(ctx.r, cx, cy, t, 1) + 8.f;
        }
        if (local_.soonest) {
            const long days = static_cast<long>((local_.soonest->licence.expiresAt - ws().now()) / 86400);
            if (days >= 0 && days <= 60) {
                const std::string t = "1 licence finit dans " + std::to_string(days) + "\xC2\xA0j";
                if (room(t)) chip(ctx.r, cx, cy, t, -1, ui::Icon::Warning);
            }
        }
        ctx.r.drawText({x, headY_ + 32.f}, ch::detail::fit(ctx.r, subline(), FontId{14}, headRight_ - x), FontId{14}, kMuted);
        ctx.r.fillRect({x0, tabsBottom_ - 1.f, w, 1.f}, kLine);
        if (!bannerRect_.empty()) paintBanner(ctx.r, ctx.theme, bannerRect_, note_, true, tooOld_ ? deploy_->bounds().w + 28.f : 0.f);
        if (wantLoad_ && !loading_) load();   // un travail tournait : le releve part des que possible
    }
    void onPaintOverlay(const ui::PaintContext& ctx) override {
        // le titre du cumul, dans la carte des achats (coupe comme le corps)
        const Rect pc = purchasesCard_->bounds();
        const Rect clip = body_->bounds();
        if (cumulTitleY_ >= clip.y && cumulTitleY_ + 16.f <= clip.bottom()) ctx.r.drawText({pc.x + 16.f, cumulTitleY_}, "Le montant cumul\xC3\xA9", gfx::semibold(FontId{13}), kInk);
    }

private:
    template <class T>
    T* add(std::unique_ptr<T> w) {
        T* p = w.get();
        addChild(std::move(w));
        return p;
    }
    template <class T>
    T* addBody(std::unique_ptr<T> w) {
        T* p = w.get();
        body_->addChild(std::move(w));
        return p;
    }
    template <class T>
    T* chart(const std::string& id, const std::string& title, const std::string& small, FitCard*& card) {
        auto c = std::make_unique<FitCard>(id + ".carte", title, small);
        T* ref = &c->setChartAs(std::make_unique<T>(id));
        card = &static_cast<FitCard&>(body_->addChild(std::move(c)));
        return ref;
    }

    // ---- ce qui vient de l'archive ----
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> contacts() const {
        std::vector<std::pair<std::string, std::string>> out;
        std::set<std::string> seen;
        for (const auto* o : local_.orders) {
            if (o->email.empty() || seen.count(lowerAscii(o->email))) continue;
            seen.insert(lowerAscii(o->email));
            out.push_back({o->name, o->email});
        }
        return out;
    }
    [[nodiscard]] std::vector<std::string> servedBy() const {
        std::vector<std::string> names;
        for (const auto& d : local_.journal)
            if (!d.who.empty() && std::find(names.begin(), names.end(), d.who) == names.end()) names.push_back(d.who);
        if (names.empty() && served_)   // le journal de ce poste n'a rien : ce que le service compte (la carte << Qui l'a servi >>)
            for (const auto& r : served_->data().rows) names.push_back(r.label);
        return names;
    }
    [[nodiscard]] std::string subline() const {
        std::string s = std::to_string(local_.active) + (local_.active > 1 ? " licences actives, " : " licence active, ") + std::to_string(local_.places) +
                        (local_.places > 1 ? " places" : " place");
        int active = 0;
        for (const auto& d : local_.devices) active += d.state == "active" ? 1 : 0;
        s += " \xC2\xB7 " + std::to_string(active) + " PC";
        const auto who = servedBy();
        if (!who.empty()) {
            s += " \xC2\xB7 servi par ";
            for (std::size_t i = 0; i < who.size() && i < 3; ++i) {
                if (i) s += i + 1 == std::min<std::size_t>(who.size(), 3) ? " et " : ", ";
                const auto sp = who[i].find(' ');
                s += sp == std::string::npos ? who[i] : who[i].substr(0, sp);
            }
        }
        return s;
    }

    void paintIdentity(const ui::PaintContext& ctx, const Rect& b) {
        float y = b.y + 42.f;
        const auto row = [&](const std::string& k, const std::string& v, const std::string& faint) {
            ctx.r.drawText({b.x + 16.f, y}, k, FontId{13}, kMuted);
            const float vx = b.x + 16.f + b.w * 0.34f;
            const std::string vv = ch::detail::fit(ctx.r, v, FontId{13}, b.right() - 16.f - vx);
            ctx.r.drawText({vx, y}, vv, FontId{13}, kInk);
            if (!faint.empty()) {   // la precision : entiere ou pas du tout
                const float fx = vx + ctx.r.measure(vv, FontId{13}).width;
                if (fx + ctx.r.measure(faint, FontId{12}).width <= b.right() - 16.f) ctx.r.drawText({fx, y + 1.f}, faint, FontId{12}, kMuted);
            }
            ctx.r.fillRect({b.x + 16.f, y + 23.f, b.w - 32.f, 1.f}, Color::rgb(0xF1F4F8));
            y += 30.f;
        };
        row("Soci\xC3\xA9t\xC3\xA9", name_, "");
        row("Ville", city_.empty() ? kDash : city_, city_.empty() ? "" : " \xC2\xB7 vue par le service");
        row("Client depuis", local_.since ? dateFr(local_.since) : kDash, local_.sinceBy.empty() ? "" : " \xC2\xB7 par " + local_.sinceBy);
        const auto cs = contacts();
        row("Facturation", cs.empty() ? kDash : cs.front().second, "");
        if (!cs.empty()) {
            ctx.r.drawText({b.x + 16.f, y + 4.f}, "Contacts", gfx::semibold(FontId{14}), kInk);
            ctx.r.drawText({b.x + 16.f + ctx.r.measure("Contacts", gfx::semibold(FontId{14})).width + 8.f, y + 5.f}, std::to_string(cs.size()), FontId{12}, kMuted);
            y += 28.f;
            for (std::size_t i = 0; i < cs.size() && i < 2; ++i) {
                if (y + 40.f > b.bottom()) break;
                const std::string ini = cs[i].first.empty() ? std::string("?") : cs[i].first.substr(0, 1);
                ctx.r.fillRoundedRect({b.x + 16.f, y + 4.f, 30.f, 30.f}, kSunken, 15.f);
                ctx.r.drawText({b.x + 26.f, y + 11.f}, ini, gfx::semibold(FontId{12}), kInk2);
                ctx.r.drawText({b.x + 56.f, y}, ch::detail::fit(ctx.r, cs[i].first, gfx::semibold(FontId{13}), b.w - 72.f), gfx::semibold(FontId{13}), kInk);
                ctx.r.drawText({b.x + 56.f, y + 19.f}, ch::detail::fit(ctx.r, cs[i].second, FontId{12}, b.w - 72.f), FontId{12}, kMuted);
                y += 46.f;
            }
        }
    }

    void paintNumbers(const ui::PaintContext& ctx, const Rect& b) {
        const float hw = b.w * 0.5f, hh = b.h * 0.5f;
        ctx.r.fillRect({b.x + hw, b.y + 1.f, 1.f, b.h - 2.f}, kLine);
        ctx.r.fillRect({b.x + 1.f, b.y + hh, b.w - 2.f, 1.f}, kLine);
        int active = 0;
        for (const auto& d : local_.devices) active += d.state == "active" ? 1 : 0;
        double cents = 0;
        for (const auto* o : local_.orders)
            if (o->state == "payee" || o->state == "livree" || o->state == "a_revoir") cents += static_cast<double>(o->amount);
        if (const auto* m = series("achats", "ventes_montant"); m && !m->refused && stats::isNumber(m->total)) cents = std::max(cents, m->total);
        const std::string reqTotal = [&] {
            if (const auto* m = series("demandes", "demandes_recues"); m && !m->refused && stats::isNumber(m->total)) return ch::fr::integer(m->total);
            return std::to_string(local_.requests);
        }();
        const struct {
            const char* label;
            std::string value, rest;
        } cells[4] = {
            {"Licences", std::to_string(local_.active), (local_.active > 1 ? "actives" : "active") + std::string(local_.expired ? " \xC2\xB7 " + std::to_string(local_.expired) + (local_.expired > 1 ? " expir\xC3\xA9" "es" : " expir\xC3\xA9" "e") : "")},
            {"PC", std::to_string(active), "sur " + std::to_string(local_.places) + (local_.places > 1 ? " places" : " place")},
            {"Achats", stats::euros(cents), std::to_string(local_.purchases.size()) + (local_.purchases.size() > 1 ? " achats" : " achat")},
            {"Demandes", reqTotal, std::to_string(local_.pending) + " en attente"},
        };
        for (int i = 0; i < 4; ++i) {
            const float x = b.x + 16.f + (i % 2) * hw, y = b.y + 12.f + (i / 2) * hh;
            ctx.r.drawText({x, y}, cells[i].label, FontId{12}, kMuted);
            const FontId vf = gfx::semibold(FontId{20});
            ctx.r.drawText({x, y + 18.f}, cells[i].value, vf, kInk);
            const float vx = x + ctx.r.measure(cells[i].value, vf).width + 7.f;
            ctx.r.drawText({vx, y + 24.f}, ch::detail::fit(ctx.r, cells[i].rest, FontId{12}, hw - (vx - x) - 20.f), FontId{12}, kInk2);
        }
    }

    void paintPcs(const ui::PaintContext& ctx, const Rect& b) {
        int active = 0, upToDate = 0;
        for (const auto& d : local_.devices) {
            active += d.state == "active" ? 1 : 0;
            upToDate += d.state == "active" && !local_.latestVersion.empty() && versionOf(d.software) == local_.latestVersion ? 1 : 0;
        }
        const std::string head = std::to_string(active) + " \xC2\xB7 " + std::to_string(upToDate) + " \xC3\xA0 jour";
        ctx.r.drawText({b.x + 16.f + ctx.r.measure("Ses PC", gfx::semibold(FontId{14})).width + 8.f, b.y + 16.f}, head, FontId{12}, kMuted);
        float y = pcsListY_;
        const float rowH = 28.f;
        std::size_t shown = 0;
        for (std::size_t i = 0; i < local_.devices.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            if (y + rowH > b.bottom() - 4.f) break;
            const auto& d = local_.devices[i];
            const float cy = y + rowH * 0.5f;
            const std::string name = d.name.empty() ? d.fingerprint : d.name;
            ctx.r.drawText({b.x + 16.f, cy - 8.f}, ch::detail::fit(ctx.r, name, FontId{12}, b.w * 0.40f), FontId{12}, d.state == "active" ? kInk : kFaint);
            const std::string v = versionOf(d.software);
            float vx = b.x + 16.f + b.w * 0.42f;
            if (!v.empty()) {
                const bool latest = v == local_.latestVersion;
                const bool late = !latest && !local_.latestVersion.empty() && minorOf(v) != minorOf(local_.latestVersion) &&
                                  versionCompare(minorOf(v) + ".9", minorOf(local_.latestVersion)) < 0;
                const float w = ctx.r.measure(v, FontId{12}).width + 12.f;
                ctx.r.fillRoundedRect({vx, cy - 10.f, w, 20.f}, latest ? Color::rgb(0xE8F6EE) : late ? Color::rgb(0xFEF3E2) : kSunken, 5.f);
                ctx.r.drawText({vx + 6.f, cy - 7.f}, v, FontId{12}, latest ? Color::rgb(0x15803D) : late ? Color::rgb(0xB45309) : kInk2);
                vx += w + 6.f;
                if (late) ui::drawIcon(ctx.r, ui::Icon::Warning, {vx, cy - 6.f, 12.f, 12.f}, kAmber);
                else if (d.state == "active" && ws().now() - d.activatedAt < 7 * 86400) {
                    const float nw = ctx.r.measure("nouveau", FontId{11}).width + 10.f;
                    ctx.r.fillRoundedRect({vx, cy - 9.f, nw, 18.f}, Color::rgb(0xF1EDFF), 5.f);
                    ctx.r.drawText({vx + 5.f, cy - 6.f}, "nouveau", FontId{11}, Color::rgb(0x4B2FD6));
                }
            }
            const std::string seen = d.state == "active" ? seenText(ws().now(), d.activatedAt) : d.state == "revoque" ? std::string("r\xC3\xA9voqu\xC3\xA9") : std::string("expir\xC3\xA9");
            ctx.r.drawText({b.right() - 16.f - ctx.r.measure(seen, FontId{12}).width, cy - 8.f}, seen, FontId{12}, kMuted);
            ctx.r.fillRect({b.x + 16.f, y + rowH - 0.5f, b.w - 32.f, 1.f}, Color::rgb(0xF1F4F8));
            y += rowH;
            ++shown;
        }
        if (local_.devices.empty()) ctx.r.drawText({b.x + 16.f, pcsListY_ + 4.f}, "Aucun PC connu de ce poste.", FontId{12}, kMuted);
    }

    // La frise, ses PC, qui l'a servi : de l'archive (sans le service).
    void fillLocal() {
        // ---- la frise ----
        ch::TimelineData t;
        const Seconds now = ws().now();
        t.today = now / 86400;
        Seconds first = local_.since ? local_.since : now - 365 * 86400;
        int y0 = 0, m0 = 0, d0 = 0;
        ch::fr::civil(first / 86400, y0, m0, d0);
        t.from = ch::fr::day(y0, 1, 1);
        int y1 = 0, m1 = 0, d1 = 0;
        ch::fr::civil(t.today, y1, m1, d1);
        Seconds last = now;
        for (const auto* e : local_.licences) last = std::max(last, e->licence.expiresAt);
        int yl = 0, ml = 0, dl = 0;
        ch::fr::civil(last / 86400, yl, ml, dl);
        t.to = ch::fr::day(std::max(yl, y1) + (ml > 6 ? 1 : 0), ml > 6 ? 6 : 12, ml > 6 ? 30 : 31);
        std::set<std::string> offers;
        for (const auto* e : local_.licences) {
            const auto& l = e->licence;
            ch::TimelineRow row;
            row.label = l.licenseId;
            row.sub = std::to_string(l.maxDevices) + " PC";
            const std::string key = "offre:" + l.edition;
            offers.insert(l.edition);
            std::vector<licensing::Purchase> ps;
            for (const auto& p : l.purchases)   // l'historique est celui du client : les achats de CETTE licence seulement
                if ((p.kind == "offer" || p.kind == "extension") && p.from > 0 && p.licenseId == l.licenseId) ps.push_back(p);
            std::sort(ps.begin(), ps.end(), [](const auto& a, const auto& b) { return a.from < b.from; });
            if (ps.empty()) {
                row.segments.push_back({l.issuedAt / 86400, (l.expiresAt ? l.expiresAt : last) / 86400, key, "sign\xC3\xA9" "e le " + dateFr(l.issuedAt)});
            } else {
                for (std::size_t i = 0; i < ps.size(); ++i) {
                    const Seconds until = ps[i].until ? ps[i].until : (i + 1 < ps.size() ? ps[i + 1].from : l.expiresAt ? l.expiresAt : last);
                    row.segments.push_back({ps[i].from / 86400, until / 86400, key,
                                            (ps[i].kind == "offer" ? "sign\xC3\xA9" "e" : "prolong\xC3\xA9" "e") + std::string(" le ") + dateFr(ps[i].at)});
                }
            }
            const long days = l.expiresAt ? static_cast<long>((l.expiresAt - now) / 86400) : 99999;
            if (e->revoked) {
                row.end = "r\xC3\xA9voqu\xC3\xA9" "e";
                row.faded = true;
            } else if (e->expired) {
                row.end = "expir\xC3\xA9" "e le " + dateFr(l.expiresAt);
                row.faded = true;
            } else if (!l.expiresAt) {
                row.end = "sans fin";
            } else {
                row.end = dateFr(l.expiresAt);
                if (days <= 30) {
                    row.end += " \xC2\xB7 dans " + std::to_string(days) + "\xC2\xA0j";
                    row.warn = true;
                }
            }
            if (now - l.issuedAt < 30 * 86400 && ps.size() <= 1) row.lead = "nouvelle";
            t.rows.push_back(row);
            for (const auto& o : l.options) {
                ch::TimelineRow orow;
                orow.label = o.label.empty() ? o.id : o.label;
                orow.sub = "option \xC2\xB7 " + l.licenseId.substr(l.licenseId.size() >= 5 ? l.licenseId.size() - 5 : 0);
                orow.mono = false;
                const Seconds until = o.until ? o.until : (l.expiresAt ? l.expiresAt : last);
                orow.segments.push_back({o.from / 86400, until / 86400, "offre:OPTIONS", "ajout\xC3\xA9" "e le " + dateFr(o.from)});
                orow.end = dateFr(until);
                orow.faded = e->expired || e->revoked;
                t.rows.push_back(orow);
            }
        }
        // les lignes en cours d'abord, les finies a la fin
        std::stable_sort(t.rows.begin(), t.rows.end(), [](const auto& a, const auto& b) { return !a.faded && b.faded; });
        for (const auto& o : offers) t.legend.push_back({o, ch::ColorKeys::standard().colorOf("offre:" + o), ch::Swatch::Box, {}});
        t.legend.push_back({"aujourd'hui", kInk, ch::Swatch::Line, {}});
        t.clickHint = "Clic : la licence";
        timeline_->setData(t);
        // ---- qui l'a servi (le journal des admins de ce poste) ----
        std::map<std::string, std::pair<int, std::pair<int, int>>> by;   // qui -> (actions, (licences, demandes))
        std::map<std::string, std::string> keyOf;
        for (const auto& d : local_.journal) {
            auto& x = by[d.who];
            ++x.first;
            if (d.action.find("demande") != std::string::npos) ++x.second.second;
            else if (d.action.find("licence") != std::string::npos) ++x.second.first;
            keyOf[d.who] = d.key;
        }
        ch::BarListData bl;
        bl.mode = ch::BarMode::Ranking;
        const double total = static_cast<double>(local_.journal.size());
        for (const auto& [who, x] : by) {
            ch::BarRow r;
            r.label = who;
            const int color = listkit::adminColor(ws(), keyOf[who]);
            r.key = color >= 0 ? "admin:" + std::to_string(std::min(color, 3)) : std::string();
            std::string sub;
            if (x.second.first) sub += std::to_string(x.second.first) + (x.second.first > 1 ? " actions sur ses licences" : " action sur ses licences");
            if (x.second.second) sub += (sub.empty() ? "" : ", ") + std::to_string(x.second.second) + (x.second.second > 1 ? " demandes" : " demande");
            r.sub = sub;
            r.value = x.first;
            r.extra = total > 0 ? ch::fr::percent(x.first * 100.0 / total) : std::string();
            bl.rows.push_back(r);
        }
        std::sort(bl.rows.begin(), bl.rows.end(), [](const auto& a, const auto& b) { return a.value > b.value; });
        bl.labelsAbove = true;   // le nom entier, au-dessus de sa barre
        bl.clickHint = "Clic : ses lignes du journal";
        bl.emptyText = "Aucune action";
        served_->setData(bl);
        std::string last1;
        if (!local_.journal.empty()) {
            const auto& d = local_.journal.back();
            last1 = "derni\xC3\xA8re : " + d.who + ", " + seenText(now, d.when);
        }
        servedCard_->setText(std::to_string(local_.journal.size()) + (local_.journal.size() > 1 ? " actions" : " action"), {},
                              last1.empty() ? std::string("dans le journal des admins") : last1);
    }

    // ---- les courbes du service ----
    [[nodiscard]] const stats::Series* series(const std::string& group, const std::string& m) const {
        const auto it = data_.find(group);
        return it == data_.end() ? nullptr : it->second.find(m);
    }
    [[nodiscard]] stats::Period period() const {
        const Seconds now = ws().now();
        const std::string k = period_->current();
        if (k == "30j") return stats::lastDays(now, 30);
        if (k == "12mois") return stats::lastMonths(now, 12);
        stats::Period p{stats::dayOf(local_.since ? local_.since : now - 365 * 86400), stats::dayOf(now)};
        p.from = p.from.substr(0, 8) + "01";
        return p;
    }

    void load() {
        wantLoad_ = false;
        if (name_.empty() || !note_.empty()) {
            fillService();
            return;
        }
        const stats::Period p = period();
        const std::string sig = name_ + "|" + p.from + "|" + p.to;
        if (sig == sig_) {   // deja lus : les cartes du service redessinees (fillLocal vient de refaire celles de l'archive)
            fillService();
            return;
        }
        if (loading_) {
            wantLoad_ = true;
            return;
        }
        const int days = daysOf(p);
        const stats::Step step = days <= 45 ? stats::Step::Day : days <= 120 ? stats::Step::Week : stats::Step::Month;
        struct Q {
            std::string        key;
            stats::SeriesQuery q;
        };
        std::vector<Q> qs;
        const auto make = [&](std::string key, std::vector<std::string> m, std::string split, stats::Period per, stats::Step st) {
            stats::SeriesQuery q;
            q.measures = std::move(m);
            q.split = std::move(split);
            q.period = per;
            q.step = fitStep(per, st);
            q.compare = stats::Compare::None;
            q.client = name_;
            qs.push_back({std::move(key), q});
        };
        make("pc", {"pc_actifs", "licences_actives"}, "", p, step);
        // ses mises a jour : les 20 dernieres semaines (la planche : du 29/06 au 09/11)
        const Seconds now = ws().now();
        stats::Period weeks = stats::lastDays(now, 7 * 19 + 1 + ch::fr::weekday(now / 86400));
        make("maj", {"pc_vus"}, "version", weeks, stats::Step::Week);
        make("achats", {"ventes_montant", "ventes_commandes"}, "", p, step);
        make("demandes", {"demandes_recues", "demandes_traitees", "licences_nouvelles"}, "admin", p, step);
        auto out = std::make_shared<std::map<std::string, stats::SeriesReply>>();
        auto errs = std::make_shared<std::map<std::string, std::string>>();
        auto admins = std::make_shared<stats::AdminsReply>();
        auto city = std::make_shared<std::string>();
        const Workspace* w = &ws();
        const std::string name = name_;
        loading_ = true;
        std::weak_ptr<bool> alive = alive_;
        const bool started = job_.start("Client : ses courbes",
            [w, qs, out, errs, admins, city, name, p] {
                OpResult last = OpResult::done({});
                for (const auto& q : qs) {
                    stats::SeriesReply r;
                    const OpResult x = fetchSeries(*w, q.q, r);
                    if (x.ok) (*out)[q.key] = std::move(r);
                    else {
                        (*errs)[q.key] = x.message;
                        last = x;
                        if (saysTooOld(x.message)) return last;
                    }
                }
                (void)fetchAdmins(*w, p, *admins);
                stats::GlobeReply g;
                if (fetchGlobe(*w, 30, g).ok)
                    for (const auto& c : g.cities)
                        for (const auto& n : c.customers)
                            if (sameClient(n, name) && city->empty()) *city = c.city + (c.country.empty() ? "" : ", " + c.country);
                return last;
            },
            [this, alive, out, errs, admins, city, sig](const OpResult& r) {
                if (alive.expired()) return;
                loading_ = false;
                sig_ = sig;
                data_ = *out;
                errors_ = *errs;
                admins_ = *admins;
                if (!city->empty()) city_ = *city;
                if (!r.ok && saysTooOld(r.message)) {
                    note_ = r.message;
                    tooOld_ = true;
                }
                fillService();
                invalidateLayout();
                invalidate();
                if (wantLoad_) load();
            });
        if (!started) {
            loading_ = false;
            wantLoad_ = true;
        }
        fillService();
    }

    // Les mois en trimestres (une periode longue : << par trimestre >>).
    static void toQuarters(const stats::SeriesReply& r, const std::vector<double>& v, std::vector<ch::Category>& x, std::vector<double>& out,
                           std::vector<std::pair<int, int>>& ticks) {
        std::map<std::string, std::size_t> at;
        for (std::size_t i = 0; i < r.buckets.size() && i < v.size(); ++i) {
            const ch::fr::Day d = dayNumber(r.buckets[i].from);
            int y = 0, m = 0, dd = 0;
            ch::fr::civil(d, y, m, dd);
            const int q = (m - 1) / 3 + 1;
            const std::string key = std::to_string(y) + "T" + std::to_string(q);
            auto it = at.find(key);
            if (it == at.end()) {
                it = at.emplace(key, x.size()).first;
                x.push_back({q == 1 ? std::to_string(y) : "T" + std::to_string(q),
                             std::string(q == 1 ? "1er" : q == 2 ? "2e" : q == 3 ? "3e" : "4e") + " trimestre " + std::to_string(y), false});
                out.push_back(0);
                if (q == 1) ticks.push_back({static_cast<int>(x.size()) - 1, 0});
            }
            if (stats::isNumber(v[i])) out[it->second] += v[i];
        }
    }

    bool missingChart(FitCard* card, ch::TimeChart* chart, const std::string& group, const stats::Series* s) {
        std::string why;
        if (!note_.empty()) why = tooOld_ ? "D\xC3\xA9ploie la 1.47.0 du service pour ces courbes." : note_;
        else if (const auto it = errors_.find(group); it != errors_.end()) why = it->second;
        else if (data_.find(group) == data_.end()) why = "Lecture des chiffres\xE2\x80\xA6";
        else if (!s) why = "Le service n'a pas rendu cette mesure.";
        else if (s->refused) why = s->rights.empty() ? std::string("Cette mesure ne se filtre pas par client.") : "Il te manque le droit \xC2\xAB " + s->rights.front() + " \xC2\xBB.";
        if (why.empty()) return false;
        ch::TimeChartData t;
        t.emptyText = why;
        chart->setData(t);
        card->setText(std::string());
        return true;
    }

    void fillService() {
        const Seconds now = ws().now();
        const std::string today = stats::dayOf(now);
        // ---- ses PC actifs (et les places achetees : ses licences, de l'archive) ----
        if (const auto* m = series("pc", "pc_actifs"); !missingChart(pcsCard_, pcsChart_, "pc", m)) {
            const auto& r = data_.at("pc");
            ch::TimeChartData t;
            t.x = axisOf(r.buckets, r.step, true);
            t.xTicks = ticksOf(t.x.size());
            if (!t.xTicks.empty()) {
                t.xTicks.front().second = -1;
                t.xTicks.back().second = 1;
            }
            ch::Series s{"un", "PC actifs", valuesOf(m->points)};
            s.step = s.area = s.endDot = true;
            std::vector<double> places;
            for (const auto& b : r.buckets) {
                const Seconds end = stats::dayStart(b.to) + 86399;
                int n = 0;
                for (const auto* e : local_.licences) {
                    const auto& l = e->licence;
                    if (l.issuedAt <= end && (l.expiresAt == 0 || l.expiresAt > end) && !(e->revoked)) n += l.maxDevices;
                }
                places.push_back(n);
            }
            ch::Series pl{"places", "places achet\xC3\xA9" "es (ses licences)", places};
            pl.step = true;
            pl.width = 1.25f;
            pl.color = Color::rgb(0x64748B);
            t.series = {s, pl};
            t.tipForward = true;
            t.yMin = 0;
            t.clickHint = "Clic : ses PC";
            pcsChart_->setData(t);
            pcsCard_->setSmalls("Ses PC actifs", std::string("\xC3\xA0 la fin de chaque ") + stepLabel(r.step));
            const double last = lastOf(m->points);
            double prev = ch::kNone;
            if (m->points.size() >= 2) prev = m->points[m->points.size() - 2];
            ch::Delta d;
            if (stats::isNumber(last) && stats::isNumber(prev) && last != prev)
                d = {ch::fr::signedInteger(last - prev) + (r.step == stats::Step::Day ? " aujourd'hui" : " ce " + std::string(stepLabel(r.step))), last > prev ? 1 : -1,
                     last > prev ? 1 : -1};
            pcsCard_->setText(stats::isNumber(last) ? ch::fr::integer(last) : kDash, d, "sur " + std::to_string(local_.places) + (local_.places > 1 ? " places" : " place"));
        }
        // ---- ses mises a jour (la version de ses PC, chaque semaine) ----
        if (const auto* m = series("maj", "pc_vus"); !missingChart(updatesCard_, updates_, "maj", m)) {
            const auto& r = data_.at("maj");
            ch::TimeChartData t;
            t.kind = ch::Kind::Stacked;
            t.x = axisOf(r.buckets, r.step, true);
            t.xTicks = ticksOf(t.x.size());
            // les versions groupees par x.y ; au-dela des 4 plus recentes : << x.y et avant >> (gris)
            std::map<std::string, std::vector<double>, std::function<bool(const std::string&, const std::string&)>> by(
                [](const std::string& a, const std::string& b) { return versionCompare(a, b) < 0; });
            for (const auto& p : m->parts) {
                if (p.key.empty() || p.key == "\xE2\x80\xA6") continue;
                auto& v = by[minorOf(p.key)];
                v.resize(r.buckets.size(), 0.0);
                for (std::size_t i = 0; i < p.points.size() && i < v.size(); ++i)
                    if (stats::isNumber(p.points[i])) v[i] += p.points[i];
            }
            std::vector<std::pair<std::string, std::vector<double>>> groups(by.begin(), by.end());
            while (groups.size() > 5) {   // les plus anciennes ensemble
                for (std::size_t i = 0; i < groups[0].second.size() && i < groups[1].second.size(); ++i) groups[1].second[i] += groups[0].second[i];
                groups.erase(groups.begin());
            }
            const bool older = by.size() > 5 || groups.size() == 5;
            int up = 0, all = 0;
            for (std::size_t k = 0; k < groups.size(); ++k) {
                ch::Series s;
                s.key = "version:" + groups[k].first;
                s.name = (k == 0 && older) ? groups[k].first + " et avant" : groups[k].first;
                s.values = groups[k].second;
                const int shownIdx = static_cast<int>(k) - (older ? 1 : 0);
                s.color = (k == 0 && older) ? ch::colors::kCompare : ch::colors::ordinal(shownIdx, static_cast<int>(groups.size()) - (older ? 1 : 0));
                t.series.push_back(s);
                const double last = groups[k].second.empty() ? 0 : groups[k].second.back();
                all += static_cast<int>(last);
                if (k + 1 == groups.size()) up = static_cast<int>(last);
            }
            t.legend = ch::LegendMode::Top;
            t.clickHint = "Clic : ses PC sur cette version";
            t.emptyText = "Aucun PC vu";
            updates_->setData(t);
            std::string late;
            const KnownDevice* worst = nullptr;
            for (const auto& d : local_.devices)
                if (d.state == "active" && !versionOf(d.software).empty() && (!worst || versionCompare(versionOf(d.software), versionOf(worst->software)) < 0)) worst = &d;
            if (worst && !local_.latestVersion.empty() && versionOf(worst->software) != local_.latestVersion)
                late = "\xE2\x9A\xA0 " + (worst->name.empty() ? worst->fingerprint : worst->name) + " : " + versionOf(worst->software);
            updatesCard_->setText(std::to_string(up) + " sur " + std::to_string(all) + " \xC3\xA0 jour", {}, late);
        }
        // ---- ses achats, puis le montant cumule (deux graphiques) ----
        if (const auto* m = series("achats", "ventes_montant"); !missingChart(purchasesCard_, purchases_, "achats", m)) {
            const auto& r = data_.at("achats");
            ch::TimeChartData t;
            t.kind = ch::Kind::Bars;
            std::vector<double> v = valuesOf(m->points, 0.01);
            const bool quarters = r.step == stats::Step::Month && r.buckets.size() > 18;
            if (quarters) {
                std::vector<double> q;
                toQuarters(r, v, t.x, q, t.xTicks);
                v = q;
            } else {
                t.x = axisOf(r.buckets, r.step);
                t.xTicks = ticksOf(t.x.size());
            }
            ch::Series s{"un", "achats", v};
            s.zeroInTip = true;
            t.series = {s};
            t.valueFormat = [](double x) { return ch::fr::euros(x); };
            t.yTickCount = 2;
            t.clickHint = "Clic : ses commandes";
            t.emptyText = "Aucun achat en ligne";
            purchases_->setData(t);
            purchasesCard_->setSmalls("Ses achats", quarters ? "par trimestre" : stepSmall(r.step));
            const auto* n = series("achats", "ventes_commandes");
            purchasesCard_->setText(stats::euros(m->total), {},
                                     std::to_string(local_.purchases.size()) + (local_.purchases.size() > 1 ? " achats" : " achat") +
                                         (n && !n->refused ? " \xC2\xB7 " + ch::fr::integer(n->total) + " en ligne" : std::string()));
            ch::TimeChartData c;
            c.x = t.x;
            c.xTicks = t.xTicks;
            c.lineInBands = true;
            std::vector<double> cum;
            double a = 0;
            for (const double x : v) cum.push_back(a += stats::isNumber(x) ? x : 0.0);
            ch::Series cs{"un", "cumul\xC3\xA9", cum};
            cs.step = cs.area = cs.endDot = true;
            c.series = {cs};
            c.yFormat = [](double x) { return x ? ch::fr::compact(x) : std::string("0"); };
            c.valueFormat = [](double x) { return ch::fr::euros(x); };
            c.yTickCount = 2;
            c.clickHint = "Clic : ses commandes jusque-l\xC3\xA0";
            cumul_->setData(c);
        } else {
            ch::TimeChartData c;
            c.emptyText = " ";
            cumul_->setData(c);
        }
        // ---- ses demandes (par admin) ----
        if (const auto* m = series("demandes", "demandes_traitees"); !missingChart(requestsCard_, requests_, "demandes", m)) {
            const auto& r = data_.at("demandes");
            ch::TimeChartData t;
            t.kind = ch::Kind::Stacked;
            const bool quarters = r.step == stats::Step::Month && r.buckets.size() > 18;
            std::vector<const stats::Part*> parts;
            for (const auto& p : m->parts)
                if (p.total > 0) parts.push_back(&p);
            const auto order = [this](const std::string& id) {
                for (std::size_t i = 0; i < admins_.admins.size(); ++i)
                    if (admins_.admins[i].id == id) return static_cast<int>(i);
                return 99;
            };
            std::sort(parts.begin(), parts.end(), [&](const auto* a, const auto* b) { return order(a->key) < order(b->key); });
            bool axisDone = false;
            for (const auto* p : parts) {
                std::vector<double> v = valuesOf(p->points);
                if (quarters) {
                    std::vector<ch::Category> x;
                    std::vector<std::pair<int, int>> ticks;
                    std::vector<double> q;
                    toQuarters(r, v, x, q, ticks);
                    v = q;
                    if (!axisDone) {
                        t.x = x;
                        t.xTicks = ticks;
                    }
                }
                axisDone = true;
                const int o = order(p->key);
                const stats::AdminUsage* a = admins_.find(p->key);
                std::string name = a && !a->name.empty() ? a->name : p->key.empty() ? std::string("sans admin connu") : p->key;
                const auto sp = name.find(' ');
                ch::Series s{o < 99 ? "admin:" + std::to_string(std::min(o, 3)) : "cmp", sp == std::string::npos ? name : name.substr(0, sp), v};
                s.legendValue = ch::fr::integer(p->total);
                t.series.push_back(s);
            }
            if (!quarters) {
                t.x = axisOf(r.buckets, r.step);
                t.xTicks = ticksOf(t.x.size());
            }
            t.legend = ch::LegendMode::Top;
            t.tipForward = true;
            t.yTickCount = 2;
            t.clickHint = "Clic : ses demandes";
            t.emptyText = "Aucune demande";
            requests_->setData(t);
            requestsCard_->setSmalls("Ses demandes", quarters ? "par trimestre" : stepSmall(r.step));
            const auto* in = series("demandes", "demandes_recues");
            requestsCard_->setText(in && !in->refused ? ch::fr::integer(in->total) : kDash, {}, std::to_string(local_.pending) + " en attente");
        }
        // ---- qui l'a servi : sans ligne du journal de ce poste sur ses licences, ce que le service compte par admin ----
        if (local_.journal.empty()) {
            const auto* dem = series("demandes", "demandes_traitees");
            const auto* lic = series("demandes", "licences_nouvelles");
            std::map<std::string, std::pair<double, double>> by;   // admin -> (licences, demandes)
            if (dem && !dem->refused)
                for (const auto& p : dem->parts) by[p.key].second += p.total;
            if (lic && !lic->refused)
                for (const auto& p : lic->parts) by[p.key].first += p.total;
            ch::BarListData bl;
            bl.mode = ch::BarMode::Ranking;
            double total = 0;
            for (const auto& [k, v] : by) total += v.first + v.second;
            for (const auto& [k, v] : by) {
                if (v.first + v.second <= 0) continue;
                ch::BarRow r;
                int order = 99;
                for (std::size_t i = 0; i < admins_.admins.size(); ++i)
                    if (admins_.admins[i].id == k) order = static_cast<int>(i);
                const stats::AdminUsage* a = admins_.find(k);
                r.label = a && !a->name.empty() ? a->name : k.empty() ? std::string("sans admin connu") : k;
                r.key = order < 99 ? "admin:" + std::to_string(std::min(order, 3)) : std::string();
                std::string sub;
                if (v.first > 0) sub += ch::fr::integer(v.first) + (v.first > 1 ? " licences" : " licence");
                if (v.second > 0) sub += (sub.empty() ? "" : ", ") + ch::fr::integer(v.second) + (v.second > 1 ? " demandes" : " demande");
                r.sub = sub;
                r.value = v.first + v.second;
                r.extra = total > 0 ? ch::fr::percent(r.value * 100.0 / total) : std::string();
                bl.rows.push_back(r);
            }
            std::sort(bl.rows.begin(), bl.rows.end(), [](const auto& a, const auto& b) { return a.value > b.value; });
            bl.labelsAbove = true;
            bl.clickHint = "Clic : ses lignes du journal";
            bl.emptyText = data_.count("demandes") ? "Aucune action" : "Lecture des chiffres\xE2\x80\xA6";
            served_->setData(bl);
            if (total > 0) servedCard_->setText(ch::fr::integer(total) + (total > 1 ? " actions" : " action"), {}, "licences et demandes, d'apr\xC3\xA8s le service");
        }
        (void)today;
    }

    // ---- les onglets et les clics : la liste filtree sur ce client ----
    void openTab(const std::string& key) {
        if (key == "vue") return;
        ListFilter f;
        if (key == "licences") {
            f.page = "licences";
            f.values.push_back({"Client", name_});
            f.what = "Licences \xC2\xB7 " + name_;
        } else if (key == "pc") {
            f.page = "activations";
            f.values.push_back({"Client", name_});
            f.what = "Activations \xC2\xB7 " + name_;
        } else if (key == "commandes") {
            f.page = "commandes";
            f.values.push_back({"Soci\xC3\xA9t\xC3\xA9", name_});
            f.what = "Commandes \xC2\xB7 " + name_;
        } else if (key == "demandes") {
            f.page = "demandes";
            f.values.push_back({"Client", name_});
            f.what = "Demandes en ligne \xC2\xB7 " + name_;
        } else if (key == "journal") {
            f.page = "journal";
            f.search = name_;
            f.what = "Journal des admins \xC2\xB7 " + name_;
        }
        const std::string said = openList(host_, *this, f, ws().now());
        if (host_.flash && !said.empty()) host_.flash(said, Tone::Info);
    }
    void picked(const std::string& what, const ch::Pick& p) {
        if (what == "licences" && p.index >= 0 && static_cast<std::size_t>(p.index) < timeline_->data().rows.size()) {
            const std::string id = timeline_->data().rows[static_cast<std::size_t>(p.index)].label;
            if (ws().licence(id)) {
                requestPageFocus("licences", id);
                if (host_.navigate) host_.navigate("licences");
                return;
            }
        }
        if (what == "journal" && p.index >= 0 && static_cast<std::size_t>(p.index) < served_->data().rows.size()) {
            ListFilter f;
            f.page = "journal";
            f.search = name_;
            f.what = "Journal des admins \xC2\xB7 " + name_ + " \xC2\xB7 " + served_->data().rows[static_cast<std::size_t>(p.index)].label;
            const std::string said = openList(host_, *this, f, ws().now());
            if (host_.flash && !said.empty()) host_.flash(said, Tone::Info);
            return;
        }
        openTab(what);
    }

    std::string                 name_, city_, note_, sig_;
    bool                        tooOld_{false}, loading_{false}, wantLoad_{false};
    Local                       local_;
    std::map<std::string, stats::SeriesReply> data_;
    std::map<std::string, std::string>       errors_;
    stats::AdminsReply          admins_;
    ui::Button*                 newLicence_{nullptr};
    ui::Button*                 extend_{nullptr};
    ui::Button*                 back_{nullptr};
    ui::Button*                 deploy_{nullptr};
    ui::Button*                 toActivations_{nullptr};
    std::vector<TabButton*>     tabs_;
    SegGroup*                   period_{nullptr};
    Body*                       body_{nullptr};
    Box*                        identity_{nullptr};
    Box*                        numbers_{nullptr};
    Box*                        pcs_{nullptr};
    ui::FilterBar*              bar_{nullptr};
    FitCard*                    timelineCard_{nullptr};
    FitCard*                    pcsCard_{nullptr};
    FitCard*                    updatesCard_{nullptr};
    FitCard*                    purchasesCard_{nullptr};
    FitCard*                    requestsCard_{nullptr};
    FitCard*                    servedCard_{nullptr};
    ch::Timeline*               timeline_{nullptr};
    ch::TimeChart*              pcsChart_{nullptr};
    ch::TimeChart*              updates_{nullptr};
    ch::TimeChart*              purchases_{nullptr};
    ch::TimeChart*              cumul_{nullptr};
    ch::TimeChart*              requests_{nullptr};
    ch::BarList*                served_{nullptr};
    float                       headY_{0}, headRight_{0}, tabsBottom_{0}, pcsListY_{0}, cumulTitleY_{0};
    Rect                        bannerRect_{};
    std::shared_ptr<bool>       alive_{std::make_shared<bool>(true)};
    core::ConnectionScope       links_;
    StatsJob                    job_;   // le releve a part (StatsKit.hpp)
};

} // namespace

PagePtr makeClientCardPage(ShellHost host) { return std::make_unique<ClientCardPage>(std::move(host)); }

} // namespace license_admin::gui
