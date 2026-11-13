// =============================================================================
//  license_admin/gui/PageAdminsWho.cpp - 1.47.0 : ADMINISTRATEURS, QUI FAIT QUOI
//  (maquette 1.47.0, planche 05)
// -----------------------------------------------------------------------------
//  - LA PART DE CHACUN, PAR TYPE D'ACTION [J] : une barre a 100 % par type (les
//    13 droits, puis la boutique ; << Toutes (hors reservees) >> en tete), un
//    morceau par admin dans l'ordre fixe et sa couleur ; le total, l'ecart a la
//    periode d'avant (en % au-dessus de 10, sinon en nombre), qui est en tete
//    (<< seul(e) >> : le seul a l'avoir fait). La barre de filtres (chercher un
//    type ; En tete, Famille, Criticite). Un clic sur un morceau : le journal de
//    cet admin ;
//  - CETTE PERIODE ET LA PERIODE D'AVANT : quatre petits graphiques (Actions [J],
//    Licences signees, Demandes traitees, Montant encaisse [S]), un point gris
//    (avant) et un point de la couleur de l'admin (maintenant), chacun son
//    echelle ;
//  - TOI, L'ADMIN+ : ce que toi seul fais (les versions publiees, les
//    sauvegardes, les admins ajoutes et retires, les droits et les profils, les
//    criticites, Stripe) [J], et le certificat du service (l'etat lu).
// =============================================================================
#include "AdminsBoard.hpp"

#include "../../ui/widgets/FilterBar.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace license_admin::gui::adminsboard {

namespace {

namespace adm = licensing::admins;
constexpr FontId kF11{11}, kF12{12}, kF13{13}, kF14{14};
const FontId kB12 = gfx::semibold(kF12), kB13 = gfx::semibold(kF13), kB14 = gfx::semibold(kF14);

std::string firstName(const std::string& name) {
    const std::size_t sp = name.find(' ');
    return sp == std::string::npos ? name : name.substr(0, sp);
}

// La famille d'un type (les 13 droits : leur groupe ; la boutique).
std::string familyOf(int type) {
    if (type < 0) return {};
    if (type == js::kShop) return "Boutique";
    for (const auto& r : adm::rights())
        if (std::string(r.id) == js::types()[static_cast<std::size_t>(type)].id) return adm::groupLabel(r.group);
    return {};
}
// La criticite de depart la plus haute des actions d'un type.
adm::Level levelOfType(int type) {
    adm::Level best = adm::Level::Info;
    for (const auto& a : adm::actionTypes())
        if (js::typeOf(a.id) == type && static_cast<int>(a.level) > static_cast<int>(best)) best = a.level;
    return best;
}

class WhoView final : public ScrolledView {
public:
    WhoView(std::shared_ptr<Data> data, Actions actions, ShellHost host)
        : ScrolledView("admins.qui"), d_(std::move(data)), a_(std::move(actions)), host_(std::move(host)) {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> fields{
            {.title = "Type d'action", .kind = K::Text},
            {.title = "En t\xC3\xAAte", .kind = K::Values, .chip = true, .chipLabel = "Admin", .noun = "un admin"},
            {.title = "Criticit\xC3\xA9", .kind = K::Values, .chip = true, .noun = "une criticit\xC3\xA9", .order = {"Info", "Moyenne", "Haute", "Critique"}},
            {.title = "Famille", .kind = K::Values, .chip = true, .noun = "une famille"},
        };
        bar_ = &adoptIn(*this, std::make_unique<ui::FilterBar>("admins.qui.barre", std::move(fields), "admins.qui.recherche"));
        bar_->setNoun("types");
        bar_->searchField().setPlaceholder("Chercher un type\xE2\x80\xA6");
        ui::FilterBar::Source src;
        src.rows = [this] { return d_->ov.shares.size(); };
        src.text = [this](std::size_t row, std::size_t field) { return cellText(row, field); };
        bar_->setSource(std::move(src));
        toBefore_ = &adoptIn(*this, std::make_unique<LinkButton>("Cette p\xC3\xA9riode et celle d'avant, toi l'admin+", "admins.qui.dessous"));
        toBefore_->setVisibility(ui::Visibility::Collapsed);
        links_ += toBefore_->clicked->connect([this] { scrollTo(beforeTop_ - 8.f); });
        links_ += bar_->changed->connect([this] {
            invalidateLayout();
            invalidate();
        });
        for (int i = 0; i < 4; ++i) {
            static const char* const ids[4] = {"admins.qui.actions", "admins.qui.licences", "admins.qui.demandes", "admins.qui.montant"};
            dumbbells_[i] = &adoptIn(*this, std::make_unique<ch::Dumbbell>(ids[i]));
        }
        refreshData();
    }

    void refreshData() override {
        bar_->refreshCounts();
        // les petits graphiques
        static const char* const titles[4] = {"Actions", "Licences sign\xC3\xA9" "es", "Demandes trait\xC3\xA9" "es", "Montant encaiss\xC3\xA9"};
        double totals[4] = {0, 0, 0, 0}, befores[4] = {0, 0, 0, 0};
        bool any[4] = {false, false, false, false};
        ch::DumbbellData dd[4];
        for (const auto& p : d_->people) {
            const js::AdminStats* s = d_->stats(p.key);
            const AdminFigures* f = d_->fig(p.key);
            double now[4] = {s ? static_cast<double>(s->actions) : kNaN, s ? s->licences : kNaN, s ? s->requests : kNaN, f ? f->amount / 100.0 : kNaN};
            double before[4] = {s ? static_cast<double>(s->actionsBefore) : kNaN, s ? s->licencesBefore : kNaN, s ? s->requestsBefore : kNaN,
                                f ? f->amountBefore / 100.0 : kNaN};
            for (int i = 0; i < 4; ++i) {
                if (!known(now[i])) continue;
                ch::DumbbellRow row;
                row.key = "admin:" + std::to_string(p.color);
                row.label = firstName(p.name);
                row.before = known(before[i]) ? before[i] : 0.0;
                row.after = now[i];
                row.color = personColor(p);
                const std::string v = i == 3 ? ch::fr::euros(now[i]) : ch::fr::integer(now[i]);
                const std::string dlt = known(before[i]) ? ch::fr::deltaPercent(now[i], before[i]) : std::string();
                row.text = dlt.empty() ? v : v + " " + dlt;
                dd[i].rows.push_back(std::move(row));
                totals[i] += now[i];
                if (known(before[i])) befores[i] += before[i];
                any[i] = true;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (i == 3) dd[i].format = [](double v) { return ch::fr::eurosCompact(v); };
            dd[i].beforeName = "la p\xC3\xA9riode d'avant";
            dd[i].afterName = "cette p\xC3\xA9riode";
            dumbbells_[i]->setData(std::move(dd[i]));
            std::string aside;
            if (any[i]) {
                aside = (i == 3 ? ch::fr::euros(totals[i]) : ch::fr::integer(totals[i]));
                const std::string dl = ch::fr::deltaPercent(totals[i], befores[i]);
                if (!dl.empty()) aside += std::string(" ") + ch::fr::kDot + " " + dl;
            } else {
                aside = i == 0 ? std::string("le journal n'est pas relev\xC3\xA9") : d_->serviceLoading ? std::string("lecture\xE2\x80\xA6") : std::string("le service : \xE2\x80\x94");
            }
            titles_[i] = titles[i];
            asides_[i] = aside;
        }
        invalidateLayout();
        invalidate();
    }

protected:
    float place(const Rect& area) override {
        const float x0 = area.x + 2.f, w = area.w - 4.f;
        float y = area.y + 4.f;
        const bool side = w >= 1180.f;   // a cote : assez de place pour les libelles des types
        const float leftW = side ? std::floor(w * 0.56f) : w;
        const float rightW = side ? w - leftW - 16.f : w;
        // ---- la part de chacun ----
        share_ = {x0, y, leftW, 0.f};
        float ly = y + 18.f;
        ly += 46.f;   // le titre, le sous-titre
        legendY_ = ly;
        legendW_ = leftW - 36.f - (w >= 1180.f ? 0.f : toBefore_->naturalWidth() + 20.f);
        ly += legendHeight(legendW_) + 8.f;
        const float bh = bar_->heightFor(leftW - 36.f);
        put(bar_, {x0 + 18.f, ly, leftW - 36.f, bh});
        ly += bh + 10.f;
        headY_ = ly;
        ly += 28.f;
        const float inner = leftW - 36.f;
        cols_[0] = x0 + 18.f;
        colW_[0] = inner * 0.31f;
        cols_[1] = cols_[0] + colW_[0];
        colW_[1] = inner * 0.33f;
        cols_[2] = cols_[1] + colW_[1] + 8.f;
        colW_[2] = inner * 0.09f;
        cols_[3] = cols_[2] + colW_[2];
        colW_[3] = inner * 0.10f;
        cols_[4] = cols_[3] + colW_[3] + 8.f;
        colW_[4] = x0 + 18.f + inner - cols_[4];
        rows_.clear();
        for (std::size_t i = 0; i < d_->ov.shares.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            const float h = i == 0 ? 46.f : 44.f;
            rows_.push_back({static_cast<int>(i), {x0 + 1.f, ly, leftW - 2.f, h}});
            ly += h;
        }
        if (rows_.empty()) ly += 50.f;
        noteY_ = ly + 10.f;
        ly += 60.f;
        share_.h = ly - share_.y;
        // ---- la colonne de droite ----
        float ry = side ? y : share_.bottom() + 16.f;
        beforeTop_ = ry - area.y;
        {   // empiles : un lien sur la ligne de la legende y mene
            const float lw = toBefore_->naturalWidth();
            put(toBefore_, {x0 + leftW - 18.f - lw, legendY_, lw, 20.f}, !side && lw < leftW * 0.45f);
        }
        const float rx = side ? x0 + leftW + 16.f : x0;
        before_ = {rx, ry, rightW, 0.f};
        float by = ry + 18.f + 46.f;
        beforeLegendY_ = by;
        by += 24.f;
        const int cols = rightW >= 520.f ? 2 : 1;
        const int people = static_cast<int>(d_->people.size());
        const float cellH = 58.f + 24.f * static_cast<float>(std::max(1, people));
        const float cellW = (rightW - 28.f - 16.f * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        for (int i = 0; i < 4; ++i) {
            const float cx = rx + 14.f + static_cast<float>(i % cols) * (cellW + 16.f);
            const float cy = by + static_cast<float>(i / cols) * (cellH + 10.f);
            cells_[i] = {cx, cy, cellW, cellH};
            put(dumbbells_[i], {cx, cy + 24.f, cellW, cellH - 24.f});
        }
        by += static_cast<float>(4 / cols) * (cellH + 10.f) + 6.f;
        before_.h = by - before_.y;
        // ---- toi, l'admin+ ----
        plus_ = {rx, before_.bottom() + 16.f, rightW, 0.f};
        float py = plus_.y + 18.f + 46.f;
        plusRows_ = plusLines();
        plusY_ = py;
        for (const auto& l : plusRows_) {
            const float th = l.sub.empty() ? 0.f : kit::wrappedHeight(l.sub, kF12, rightW - 36.f - 46.f - 40.f, 17.f, 2);
            py += 24.f + th + 14.f;
        }
        plus_.h = py + 8.f - plus_.y;
        return std::max(share_.bottom(), plus_.bottom()) + 16.f - area.y;
    }

    void draw(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        r.pushClip(viewport());
        paintShare(ctx);
        // ---- cette periode et celle d'avant ----
        card(r, ctx.theme, before_);
        {
            const float x = before_.x + 18.f, w = before_.w - 36.f;
            (void)cardTitle(r, x, before_.y + 18.f, w, "Cette p\xC3\xA9riode et la p\xC3\xA9riode d'avant",
                            "par admin ; quatre petits graphiques, chacun son \xC3\xA9" "chelle", "J S");
            float lx = x;
            r.fillRoundedRect({lx, beforeLegendY_ + 4.f, 9.f, 9.f}, kFaint, 4.5f);
            lx += 14.f + text(r, lx + 14.f, beforeLegendY_, "la p\xC3\xA9riode d'avant (" + ddmmOf(d_->previous.first) + " \xE2\x86\x92 " + ddmmOf(d_->previous.last) + ")", kF12,
                              kInk2, w * 0.5f);
            r.fillRoundedRect({lx + 14.f, beforeLegendY_ + 4.f, 9.f, 9.f}, kInk, 4.5f);
            (void)text(r, lx + 28.f, beforeLegendY_, "cette p\xC3\xA9riode, dans la couleur de l'admin", kF12, kInk2, x + w - lx - 28.f);
            for (int i = 0; i < 4; ++i) {
                const Rect& c = cells_[i];
                const float aw = ui::measureWidth(asides_[i], kF12);
                (void)text(r, c.x, c.y + 2.f, titles_[i], kB14, kInk, c.w - aw - 10.f);
                (void)text(r, c.right() - std::min(aw, c.w * 0.6f), c.y + 3.f, asides_[i], kF12, kMuted, c.w * 0.6f);
            }
        }
        // ---- toi, l'admin+ ----
        paintPlus(ctx);
        r.popClip();
    }

private:
    struct RowGeo {
        int  share{0};
        Rect r{};
    };
    struct PlusLine {
        ui::Icon    icon;
        std::string title, good, sub, count;
    };

    static std::string ddmmOf(std::int64_t day) { return ch::fr::ddmm(day); }

    [[nodiscard]] std::string cellText(std::size_t row, std::size_t field) const {
        if (row >= d_->ov.shares.size()) return {};
        const js::ShareRow& s = d_->ov.shares[row];
        switch (field) {
            case 0: return s.label;
            case 1:
                if (s.leader >= 0 && static_cast<std::size_t>(s.leader) < d_->ov.admins.size()) return d_->ov.admins[static_cast<std::size_t>(s.leader)].name;
                return "personne";
            case 2: return s.type < 0 ? std::string() : adm::levelLabel(levelOfType(s.type));
            case 3: return familyOf(s.type);
            default: return {};
        }
    }

    // La legende : chaque admin (retire : depuis quand), puis les demandes.
    [[nodiscard]] float legendHeight(float w) const {
        float x = 0.f, lines = 1.f;
        for (const auto& it : legendItems()) {
            const float iw = 24.f + ui::measureWidth(it.first, kF13) + ui::measureWidth(it.second, kF13) + 18.f;
            if (x + iw > w && x > 0.f) {
                x = 0.f;
                lines += 1.f;
            }
            x += iw;
        }
        return lines * 24.f;
    }
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> legendItems() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& p : d_->people) {
            if (p.retired) out.push_back({p.name, " (retir\xC3\xA9 le " + dayMonth(p.retired) + ")"});
            else out.push_back({p.name, p.me ? std::string(" (toi)") : std::string()});
        }
        return out;
    }

    void paintShare(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, share_);
        const float x = share_.x + 18.f, w = share_.w - 36.f;
        (void)cardTitle(r, x, share_.y + 18.f, w, "La part de chacun, par type d'action",
                        "une barre par type (100 %) \xC2\xB7 les admins dans le m\xC3\xAAme ordre, la m\xC3\xAAme couleur \xC2\xB7 du " + ch::fr::ddmm(d_->period.first) + " au " +
                            ch::fr::ddmm(d_->period.last),
                        "J");
        // la legende
        {
            float lx = x, ly = legendY_;
            std::size_t k = 0;
            for (const auto& it : legendItems()) {
                const Person* p = nullptr;
                std::size_t idx = 0;
                for (const auto& q : d_->people)
                    if (idx++ == k) p = &q;
                ++k;
                const float iw = 24.f + ui::measureWidth(it.first, kF13) + ui::measureWidth(it.second, kF13) + 18.f;
                if (lx + iw > x + legendW_ && lx > x) {
                    lx = x;
                    ly += 24.f;
                }
                if (p) avatarOf(r, {lx, ly, 18.f, 18.f}, *p);
                const float nx = lx + (p ? 24.f : 0.f);
                const float nw = text(r, nx, ly + 1.f, it.first, kF13, kInk, x + w - nx);
                (void)text(r, nx + nw, ly + 1.f, it.second, kF13, kMuted, x + w - nx - nw);
                lx += iw;
            }
        }
        // les titres
        r.fillRect({share_.x + 1.f, headY_ - 4.f, share_.w - 2.f, 1.f}, kLine);
        static const char* const heads[5] = {"TYPE D'ACTION", "PART DE CHACUN", "ACTIONS", "VS AVANT", "EN T\xC3\x8ATE"};
        for (int c = 0; c < 5; ++c) (void)text(r, cols_[c], headY_ + 4.f, heads[c], gfx::semibold(kF11), kFaint, colW_[c]);
        r.fillRect({share_.x + 1.f, headY_ + 24.f, share_.w - 2.f, 1.f}, kLine);
        if (!d_->journal) {
            (void)kit::drawWrapped(r, x, headY_ + 40.f, w, "Le journal des admins n'est pas relev\xC3\xA9 sur ce poste : ces parts viennent de lui (Journal des admins, Actualiser).",
                                   kF13, kMuted, 18.f, 3);
        }
        for (const auto& g : rows_) {
            const js::ShareRow& s = d_->ov.shares[static_cast<std::size_t>(g.share)];
            const bool all = s.type < 0;
            if (all) r.fillRect(g.r, Color::rgb(0xF8FAFC));
            r.fillRect({g.r.x + 17.f, g.r.bottom() - 1.f, g.r.w - 34.f, 1.f}, kLine2);
            const float cy = g.r.y + (g.r.h - 18.f) * 0.5f;
            (void)text(r, cols_[0], cy, s.label, all ? kB14 : kF14, kInk, colW_[0] - 10.f);
            // la barre a 100 %
            std::vector<double> v;
            std::vector<Color> colors;
            for (std::size_t i = 0; i < s.perAdmin.size() && i < d_->ov.admins.size(); ++i) {
                const Person* p = d_->personById(d_->ov.admins[i].id);
                v.push_back(s.perAdmin[i]);
                colors.push_back(p ? personColor(*p) : kFaint);
            }
            const Rect bar{cols_[1], g.r.y + (g.r.h - 12.f) * 0.5f, colW_[1], 12.f};
            if (s.total > 0) ch::paintSegments(r, ctx.theme, bar, v, colors);
            else r.fillRoundedRect(bar, kSunken, 4.f);
            // le total, l'ecart
            const std::string t = std::to_string(s.total);
            const float tw = ui::measureWidth(t, kB14);
            r.drawText({cols_[2] + colW_[2] - tw - 6.f, cy}, t, kB14, kInk);
            std::string dl;
            Color dc = kMuted;
            if (s.changeInPercent && known(s.changePct)) {
                dl = ch::fr::signedPercent(s.changePct);
                dc = s.changePct > 0.5 ? kGreen : s.changePct < -0.5 ? kRed : kMuted;
            } else {
                dl = ch::fr::signedInteger(s.changeCount);
                dc = s.changeCount > 0 ? kGreen : s.changeCount < 0 ? kRed : kMuted;
            }
            const float dw = ui::measureWidth(dl, kB13);
            r.drawText({cols_[3] + colW_[3] - dw - 4.f, cy}, dl, kB13, dc);
            // en tete
            if (s.leader >= 0 && static_cast<std::size_t>(s.leader) < d_->ov.admins.size()) {
                const Person* p = d_->personById(d_->ov.admins[static_cast<std::size_t>(s.leader)].id);
                if (p) avatarOf(r, {cols_[4], g.r.y + (g.r.h - 20.f) * 0.5f, 20.f, 20.f}, *p);
                const std::string who = firstName(d_->ov.admins[static_cast<std::size_t>(s.leader)].name) +
                                        (s.alone ? (p && p->kind == Person::Kind::Plus ? std::string(" seule") : std::string(" seul(e)"))
                                                 : " " + ch::fr::percent(s.leaderShare * 100.0));
                (void)text(r, cols_[4] + 26.f, cy, who, kF14, kInk, colW_[4] - 26.f);
            } else {
                (void)text(r, cols_[4], cy, "personne", kF13, kFaint, colW_[4]);
            }
        }
        if (rows_.empty() && d_->journal) (void)text(r, x, headY_ + 44.f, "Aucun type ne correspond \xC3\xA0 ces filtres.", kF13, kMuted, w);
        ui::drawIcon(r, ui::Icon::Info, {x, noteY_ + 2.f, 14.f, 14.f}, kFaint);
        (void)kit::drawWrapped(r, x + 22.f, noteY_, w - 22.f,
                               "Un clic sur un morceau : le journal de cet admin. \xC2\xAB seul(e) \xC2\xBB : le seul \xC3\xA0 l'avoir fait. L'\xC3\xA9" "cart : en % au-dessus de 10, sinon en nombre.",
                               kF12, kMuted, 17.f, 2);
    }

    [[nodiscard]] std::vector<PlusLine> plusLines() const {
        const js::PlusReserved& p = d_->ov.plus;
        const auto list = [](const std::vector<js::Ref>& refs, std::size_t most) {
            std::string out;
            for (std::size_t i = refs.size(); i-- > 0 && refs.size() - i <= most;) {
                const auto& r = refs[i];
                out += (out.empty() ? "" : " \xC2\xB7 ") + (r.object.empty() ? r.summary : r.object) + " le " + dayMonth(r.when);
            }
            return out;
        };
        const auto count = [](std::size_t n) { return std::to_string(n); };
        // l'objet d'une ligne sur un admin : l'identifiant de sa cle -> son nom
        const auto who = [this](const js::Ref& r) {
            if (const Person* q = d_->personById(r.object)) return q->name;
            return r.object.empty() ? r.summary : r.object;
        };
        std::vector<PlusLine> out;
        out.push_back({ui::Icon::Module, "Versions publi\xC3\xA9" "es", "", list(p.versions, 3), count(p.versions.size())});
        {
            PlusLine l{ui::Icon::Save, "Sauvegardes de secours", "", "", count(p.backups.size())};
            if (p.lastBackup.number) l.good = "la derni\xC3\xA8re " + ago(d_->now, p.lastBackup.when) + " \xE2\x9C\x93";
            l.sub = "restaurations : " + std::to_string(p.restores.size());
            out.push_back(std::move(l));
        }
        {
            std::string sub;
            for (const auto& r : p.adminsAdded) sub += (sub.empty() ? "" : " \xC2\xB7 ") + std::string("+ ") + who(r) + " (" + dayMonth(r.when) + ")";
            for (const auto& r : p.adminsRemoved) sub += (sub.empty() ? "" : " \xC2\xB7 ") + std::string(ch::fr::kMinus) + " " + who(r) + " (" + dayMonth(r.when) + ")";
            out.push_back({ui::Icon::User, "Admins ajout\xC3\xA9s, retir\xC3\xA9s", "", sub, count(p.adminsAdded.size() + p.adminsRemoved.size())});
        }
        {
            std::map<std::string, int> by;
            for (const auto& r : p.rights) ++by[who(r)];
            std::string sub;
            for (const auto& [k, n] : by) sub += (sub.empty() ? "" : " \xC2\xB7 ") + k + " \xC3\x97" + std::to_string(n);
            out.push_back({ui::Icon::Lock, "Droits et profils chang\xC3\xA9s", "", sub, count(p.rights.size())});
        }
        out.push_back({ui::Icon::Settings, "Criticit\xC3\xA9s r\xC3\xA9gl\xC3\xA9" "es", "", list(p.criticities, 3), count(p.criticities.size())});
        out.push_back({ui::Icon::Link, "Stripe et la boutique", "", list(p.stripe, 3), count(p.stripe.size())});
        // le certificat du service (l'etat lu dans cette seance)
        {
            PlusLine l{ui::Icon::Lock, "Le certificat du service", "", "", "\xE2\x80\x94"};
            const Workspace& w = host_.workspace();
            const auto& sv = w.service();
            if (!sv.configured) {
                l.sub = "le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)";
            } else if (!sv.hasState) {
                l.sub = "pas encore lu dans cette s\xC3\xA9" "ance (Service en ligne, V\xC3\xA9rifier)";
            } else if (sv.state.certOk) {
                l.good = "valide \xE2\x9C\x93";
                l.sub = "jusqu'au " + kit::whenFr(0, sv.state.certExpires);
                if (!sv.state.serviceKey.empty()) l.sub += " \xC2\xB7 empreinte " + online::keyFingerprint(sv.state.serviceKey).substr(0, 9);
            } else {
                l.sub = sv.state.certProblem.empty() ? std::string("aucun certificat valable") : sv.state.certProblem;
            }
            out.push_back(std::move(l));
        }
        return out;
    }

    void paintPlus(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, plus_);
        const float x = plus_.x + 18.f, w = plus_.w - 36.f;
        (void)cardTitle(r, x, plus_.y + 18.f, w - 90.f, "Toi, l'admin+ : ce que toi seul fais", "tes actions r\xC3\xA9serv\xC3\xA9" "es, sur la p\xC3\xA9riode", "");
        (void)kit::pill(r, x + w - 80.f, plus_.y + 16.f, "Admin+", kB12, kVioletInk, kVioletPale, 22.f);
        float y = plusY_;
        for (std::size_t i = 0; i < plusRows_.size(); ++i) {
            const auto& l = plusRows_[i];
            r.fillRoundedRect({x, y, 32.f, 32.f}, kSunken, 8.f);
            ui::drawIcon(r, l.icon, {x + 8.f, y + 8.f, 16.f, 16.f}, kInk2);
            const float tx = x + 46.f, tw = w - 46.f - 40.f;
            const float t1 = text(r, tx, y, l.title, kB14, kInk, tw);
            if (!l.good.empty()) (void)text(r, tx + t1 + 6.f, y + 1.f, "\xC2\xB7 " + l.good, kB12, kGreen, tw - t1 - 6.f);
            const float cw = ui::measureWidth(l.count, gfx::semibold(FontId{16}));
            r.drawText({x + w - cw, y}, l.count, gfx::semibold(FontId{16}), l.count == "\xE2\x80\x94" ? kFaint : kInk);
            float th = 0.f;
            if (!l.sub.empty()) th = kit::drawWrapped(r, tx, y + 22.f, tw, l.sub, kF12, kMuted, 17.f, 2);
            y += 24.f + (l.sub.empty() ? 0.f : kit::wrappedHeight(l.sub, kF12, tw, 17.f, 2)) + 14.f;
            (void)th;
            if (i + 1 < plusRows_.size()) r.fillRect({x, y - 8.f, w, 1.f}, kLine2);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* md = std::get_if<ui::MouseDown>(&ev); md && md->button == ui::MouseButton::Left && viewport().contains(md->pos)) {
            for (const auto& g : rows_) {
                if (!g.r.contains(md->pos) || md->pos.x < cols_[1] || md->pos.x > cols_[1] + colW_[1]) continue;
                const js::ShareRow& s = d_->ov.shares[static_cast<std::size_t>(g.share)];
                double total = 0;
                for (const int n : s.perAdmin) total += n;
                if (total <= 0) return ui::EventResult::Consumed;
                double acc = 0;
                const double at = (md->pos.x - cols_[1]) / colW_[1] * total;
                for (std::size_t i = 0; i < s.perAdmin.size() && i < d_->ov.admins.size(); ++i) {
                    acc += s.perAdmin[i];
                    if (at <= acc && s.perAdmin[i] > 0) {
                        if (a_.journalOf) a_.journalOf(d_->keyOf(d_->ov.admins[i].id));
                        break;
                    }
                }
                return ui::EventResult::Consumed;
            }
        }
        return ScrolledView::onEvent(ev);
    }

    std::shared_ptr<Data>  d_;
    Actions                a_;
    ShellHost              host_;
    ui::FilterBar*         bar_{nullptr};
    ch::Dumbbell*          dumbbells_[4]{};
    LinkButton*            toBefore_{nullptr};
    float                  legendW_{0.f};
    float                  beforeTop_{0.f};
    Rect                   cells_[4]{};
    std::string            titles_[4], asides_[4];
    Rect                   share_{}, before_{}, plus_{};
    float                  legendY_{0}, headY_{0}, noteY_{0}, beforeLegendY_{0}, plusY_{0};
    float                  cols_[5]{}, colW_[5]{};
    std::vector<RowGeo>    rows_;
    std::vector<PlusLine>  plusRows_;
    core::ConnectionScope  links_;
};

} // namespace

std::unique_ptr<ScrolledView> makeWho(std::shared_ptr<Data> data, Actions actions, ShellHost host) {
    return std::make_unique<WhoView>(std::move(data), std::move(actions), std::move(host));
}

} // namespace license_admin::gui::adminsboard
