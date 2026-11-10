// =============================================================================
//  license_admin/gui/PageAdminCard.cpp - 1.47.0 : LA FICHE D'UN ADMIN (maquette
//  1.47.0, planches 02, 03 et 04)
// -----------------------------------------------------------------------------
//  Une longue page a ancres, sous l'en-tete de la fiche (PageAdmins.cpp : qui,
//  ses boutons, les ancres, la periode) :
//    0 VUE D'ENSEMBLE (planche 02) : qui et ou [J S N], huit tuiles et leur
//      courbe sur 12 semaines, ses actions par jour face a la periode d'avant,
//      par type (l'ordre fixe : les 13 droits, puis la boutique), par
//      criticite, sa part parmi les admins ;
//    1 ACTIVITE (planche 03) : la carte jour x heure (l'heure de Paris, les
//      heures inhabituelles pour lui signalees), ses jours, sa serie, le debut
//      et la fin de ses journees ;
//    6 DROITS et 5 SECURITE (planche 03) : ses droits utilises ou non (<< Retirer
//      ce droit ? >>), les signatures refusees, un autre PC, ses heures
//      inhabituelles et ses pics, le detail, ses droits changes, la chaine ;
//    2 LICENCES ET CLIENTS, 3 DEMANDES, 4 BOUTIQUE (planche 04) : ce qu'il
//      produit, et ses courbes (lui en couleur, les autres admins en gris).
//  Les chiffres [J] : JournalStats ; [S]/[N] : le service (Data::figures, la
//  fiche : fetchCard, lus par la page en arriere-plan).
// =============================================================================
#include "AdminsBoard.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace license_admin::gui::adminsboard {

namespace {

namespace adm = licensing::admins;
constexpr FontId kF11{11}, kF12{12}, kF13{13}, kF14{14};
const FontId kB12 = gfx::semibold(kF12), kB13 = gfx::semibold(kF13), kB14 = gfx::semibold(kF14);
constexpr std::int64_t kDayS = 86400;

std::string groups4(const std::string& hex, int count) {
    std::string out;
    for (int i = 0; i < count && static_cast<std::size_t>(i * 4) < hex.size(); ++i) {
        if (i) out += ' ';
        out += hex.substr(static_cast<std::size_t>(i * 4), 4);
    }
    for (auto& ch : out)
        if (ch >= 'a' && ch <= 'f') ch = static_cast<char>(ch - 32);
    return out;
}
std::string firstName(const std::string& name) {
    const std::size_t sp = name.find(' ');
    return sp == std::string::npos ? name : name.substr(0, sp);
}
std::string plural(double n, const char* one, const char* many) { return num(n) + " " + (n > 1 ? many : one); }

struct Tile {
    std::string         label, value, extra;
    double              cur{kNaN}, prev{kNaN};
    bool                upIsGood{true}, count{false};
    std::string         unit;          // apres la valeur (<< sur 30 >>, << jours ouvres >>)
    std::vector<double> spark;
    std::string         deltaText;     // un ecart deja ecrit (<< -1 h 20 >>)
    bool                deltaGood{true};
};

class CardView final : public ScrolledView {
public:
    CardView(std::shared_ptr<Data> data, Actions actions, ShellHost host, std::string key, bool own)
        : ScrolledView("admins.fiche.vue"), d_(std::move(data)), a_(std::move(actions)), host_(std::move(host)), key_(std::move(key)), own_(own) {
        perDay_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.fiche.parjour"));
        byType_ = &adoptIn(*this, std::make_unique<ch::BarList>("admins.fiche.types"));
        heat_ = &adoptIn(*this, std::make_unique<ch::HeatMap>("admins.fiche.heures"));
        strip_ = &adoptIn(*this, std::make_unique<ch::DayStrip>("admins.fiche.jours"));
        span_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.fiche.journees"));
        offers_ = &adoptIn(*this, std::make_unique<ch::BarList>("admins.fiche.offres"));
        weeksLic_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.fiche.licences"));
        clients_ = &adoptIn(*this, std::make_unique<ch::BarList>("admins.fiche.clients"));
        delay_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.fiche.delai"));
        weeksReq_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.fiche.demandes"));
        weeksAmount_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.fiche.montant"));
        articles_ = &adoptIn(*this, std::make_unique<ch::BarList>("admins.fiche.articles"));
        removeUnused_ = &adoptIn(*this, std::make_unique<LinkButton>("Retirer ces droits ?", "admins.fiche.retirer-droits", false));
        removeUnused_->color = kAmber;
        dayJournal_ = &adoptIn(*this, std::make_unique<LinkButton>("Ce jour dans son journal", "admins.fiche.jour"));
        lines_ = &adoptIn(*this, std::make_unique<LinkButton>("Ses lignes dans le journal", "admins.fiche.lignes"));
        links_ += removeUnused_->clicked->connect([this] {
            if (const js::AdminStats* s = d_->stats(key_); s && a_.removeRights) a_.removeRights(key_, s->unused);
        });
        links_ += dayJournal_->clicked->connect([this] {
            const js::AdminStats* s = d_->stats(key_);
            if (s && !s->peaks.empty() && a_.journalLines) {
                // la derniere ligne de ce jour-la
                std::vector<std::uint64_t> ls;
                for (const auto& l : d_->lines)
                    if (js::localDay(l.at(), js::TimeZone::paris()) == s->peaks.back().day && mine(l)) ls.push_back(l.number);
                a_.journalLines(ls, key_);
            } else if (a_.journalOf) {
                a_.journalOf(key_);
            }
        });
        links_ += lines_->clicked->connect([this] {
            if (a_.journalOf) a_.journalOf(key_);
        });
        for (int i = 0; i < 13; ++i) {
            const std::string id = adm::rights()[static_cast<std::size_t>(i)].id;
            auto& b = adoptIn(*this, std::make_unique<LinkButton>("\xE2\x80\x94 Retirer ce droit ?", "admins.fiche.retirer." + id, false));
            b.color = kAmber;
            links_ += b.clicked->connect([this, id] {
                if (a_.removeRights) a_.removeRights(key_, {id});
            });
            removeOne_[i] = &b;
        }
        refreshData();
    }

    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] bool own() const noexcept { return own_; }

    void anchor(int i) {
        if (i < 0 || i >= 7) return;
        anchor_ = i;
        scrollTo(std::max(0.f, anchorRel_[i] - 4.f));
    }
    [[nodiscard]] int currentAnchor() const {
        int best = 0;
        float bestY = -1e9f;
        for (int i = 0; i < 7; ++i)
            if (anchorRel_[i] <= offset() + 60.f && anchorRel_[i] > bestY) {
                bestY = anchorRel_[i];
                best = i;
            }
        return best;
    }

    void refreshData() override {
        const Person* p = d_->person(key_);
        const js::AdminStats* s = d_->stats(key_);
        const CardFigures* c = cardFigs();
        const Color mineC = p ? personColor(*p) : kViolet;
        const int n = d_->period.days();
        // ---- ses actions sur 12 semaines (le journal) ----
        weeksActions_.assign(12, 0.0);
        {
            const js::TimeZone tz = js::TimeZone::paris();
            const std::int64_t today = js::localDay(d_->now, tz);
            const std::int64_t monday = today - js::weekday(today);
            weekStart_ = monday - 11 * 7;
            for (const auto& l : d_->lines) {
                if (!mine(l)) continue;
                if (js::typeOf(l.action) < 0 && !js::reserved(l.action)) continue;
                const std::int64_t day = js::localDay(l.at(), tz);
                if (day < weekStart_ || day > today) continue;
                weeksActions_[static_cast<std::size_t>((day - weekStart_) / 7)] += 1.0;
            }
        }
        // ---- ses actions par jour ----
        {
            ch::TimeChartData cd;
            cd.kind = ch::Kind::Bars;
            cd.x = ch::dailyAxis(d_->period.first, n, n <= 92);
            if (s) {
                ch::Series cmp{"cmp", "avant", std::vector<double>(s->perDayBefore.begin(), s->perDayBefore.end())};
                cmp.role = ch::Role::Compare;
                ch::Series main{"admin", "cette p\xC3\xA9riode", std::vector<double>(s->perDay.begin(), s->perDay.end())};
                main.color = mineC;
                main.legendValue = std::to_string(s->actions);
                cmp.legendValue = std::to_string(s->actionsBefore);
                cd.series.push_back(std::move(main));
                cd.series.push_back(std::move(cmp));
                for (const auto& pk : s->peaks)
                    if (d_->period.contains(pk.day))
                        cd.notes.push_back({static_cast<int>(pk.day - d_->period.first), static_cast<double>(pk.count),
                                            std::to_string(pk.count) + " actions " + ch::fr::kDot + " " + ch::fr::decimal(pk.factor) + "\xC3\x97 sa moyenne"});
            }
            cd.legend = ch::LegendMode::None;
            cd.yTickCount = 4;
            cd.valueFormat = [](double v) { return ch::fr::integer(v) + (v > 1 ? " actions" : " action"); };
            perDay_->setData(std::move(cd));
        }
        // ---- par type ----
        {
            ch::BarListData bd;
            bd.mode = ch::BarMode::Ranking;
            bd.color = mineC;
            for (int t = 0; t < js::kTypes; ++t) {
                ch::BarRow row;
                row.label = js::types()[static_cast<std::size_t>(t)].label;
                row.value = s ? s->byType[static_cast<std::size_t>(t)] : 0;
                if (s && t < 13 && t < static_cast<int>(s->rights.size())) {
                    const auto& u = s->rights[static_cast<std::size_t>(t)];
                    if (!u.held && u.count == 0) {
                        row.note = "pas ce droit";
                        row.off = true;
                    } else if (u.neverUsed) {
                        row.note = "jamais (" + std::to_string(d_->unusedDays) + " j)";
                        row.warn = true;
                    }
                }
                bd.rows.push_back(std::move(row));
            }
            bd.rowHeight = 25.f;
            byType_->setData(std::move(bd));
        }
        // ---- la carte jour x heure ----
        {
            ch::HeatMapData hd;
            hd.rowLabels = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
            for (int h = 0; h < 24; ++h) hd.colLabels.push_back(std::to_string(h) + " h");
            hd.cells.assign(7, std::vector<double>(24, 0.0));
            if (s)
                for (int dd = 0; dd < 7; ++dd)
                    for (int h = 0; h < 24; ++h) {
                        hd.cells[static_cast<std::size_t>(dd)][static_cast<std::size_t>(h)] = s->dayHour[static_cast<std::size_t>(dd)][static_cast<std::size_t>(h)];
                        if (s->dayHourUnusual[static_cast<std::size_t>(dd)][static_cast<std::size_t>(h)] > 0) hd.flagged.push_back({dd, h});
                    }
            if (s && s->habitsKnown && s->usualStart >= 0) {
                hd.bandFrom = s->usualStart / 60.0;
                hd.bandTo = s->usualEnd / 60.0;
                hd.bandLabel = "ses heures habituelles : " + ch::fr::clock(s->usualStart / 60.0) + " " + ch::fr::kArrow + " " + ch::fr::clock(s->usualEnd / 60.0);
            }
            hd.scaleLabel = "actions par heure :";
            hd.flagLabel = "heure inhabituelle pour " + (p ? firstName(p->name) : std::string("lui")) + " (signal\xC3\xA9" "e)";
            hd.valueUnit = "action";
            hd.labelEvery = 3;
            heat_->setData(std::move(hd));
        }
        // ---- ses jours ----
        {
            ch::DayStripData sd;
            sd.first = d_->period.first;
            if (s) {
                sd.values.assign(s->perDay.begin(), s->perDay.end());
                // la serie en cours : les derniers jours ouvres actifs (samedi et dimanche ne coupent pas)
                if (s->streak > 0) {
                    int k = static_cast<int>(sd.values.size()) - 1, worked = 0;
                    const int end = k;
                    while (k >= 0) {
                        const int wd = js::weekday(d_->period.first + k);
                        if (sd.values[static_cast<std::size_t>(k)] > 0) worked += wd < 5 ? 1 : 0;
                        else if (wd < 5 && k != end) break;
                        if (worked >= s->streak) break;
                        --k;
                    }
                    sd.streakFrom = std::max(0, k);
                    sd.streakTo = end;
                }
                for (const auto& u : s->unusual) {
                    const std::int64_t day = js::localDay(u.when, js::TimeZone::paris());
                    if (d_->period.contains(day)) sd.flagged.push_back(static_cast<int>(day - d_->period.first));
                }
            }
            sd.streakColor = mineC;
            strip_->setData(std::move(sd));
        }
        // ---- le debut et la fin de ses journees ----
        {
            ch::TimeChartData cd;
            cd.kind = ch::Kind::Range;
            cd.x = ch::dailyAxis(d_->period.first, n, false);
            std::vector<double> a(static_cast<std::size_t>(std::max(0, n)), ch::kNone), b = a;
            std::vector<Color> colors(a.size(), Color{0, 0, 0, 0});
            if (s) {
                std::set<std::int64_t> odd;
                for (const auto& u : s->unusual) odd.insert(js::localDay(u.when, js::TimeZone::paris()));
                for (std::size_t i = 0; i < a.size() && i < s->dayFirst.size(); ++i) {
                    if (s->dayFirst[i] < 0) continue;
                    a[i] = s->dayFirst[i] / 60.0;
                    b[i] = std::max(s->dayLast[i] / 60.0, a[i] + 0.15);
                    if (odd.count(d_->period.first + static_cast<std::int64_t>(i))) colors[i] = kRed;
                }
                if (s->habitsKnown && s->usualStart >= 0) cd.yBands.push_back({s->usualStart / 60.0, s->usualEnd / 60.0, "habituel", Color{0, 0, 0, 0}});
            }
            ch::Series s0{"admin", "premi\xC3\xA8re action", a};
            s0.color = mineC;
            s0.pointColors = colors;
            ch::Series s1{"admin", "derni\xC3\xA8re action", b};
            s1.color = mineC;
            s1.pointColors = colors;
            cd.series = {s0, s1};
            cd.yMin = 6;
            cd.yMax = 24;
            cd.yTicks = {6, 12, 18, 24};
            cd.yFormat = [](double v) { return ch::fr::integer(v) + " h"; };
            cd.valueFormat = [](double v) { return ch::fr::clock(v); };
            cd.legend = ch::LegendMode::None;
            cd.emptyText = "Aucune journ\xC3\xA9" "e sur la p\xC3\xA9riode";
            span_->setData(std::move(cd));
        }
        // ---- le service : par offre, ses courbes, ses clients, les delais, les articles ----
        {
            ch::BarListData bd;
            bd.mode = ch::BarMode::Ranking;
            bd.color = mineC;
            if (c)
                for (const auto& [k, v] : c->byOffer) bd.rows.push_back({.label = statskit::partLabel("offre", k), .value = v});
            bd.emptyText = c && c->loaded ? std::string("Aucune licence sign\xC3\xA9" "e sur la p\xC3\xA9riode") : serviceWord();
            offers_->setData(std::move(bd));
        }
        const auto weekly = [&](ch::TimeChart* chart, const std::vector<double>& mineV, const std::vector<double>& others, ch::Format fmt, const char* empty) {
            ch::TimeChartData cd;
            cd.kind = ch::Kind::Stacked;
            if (c && !c->weeks.empty()) {
                cd.x = statskit::axisOf(c->weeks, stats::Step::Week);
                cd.xTicks = statskit::ticksOf(c->weeks.size());
                ch::Series me{"admin", p ? p->name : std::string("lui"), mineV};
                me.color = mineC;
                ch::Series rest{"autres", "les autres admins", others};
                rest.color = ch::colors::kOthers;
                cd.series = {me, rest};
                // pas encore admin : les semaines d'avant son arrivee
                if (p && p->since > 0) {
                    int before = -1;
                    for (std::size_t i = 0; i < c->weeks.size(); ++i)
                        if (stats::dayStart(c->weeks[i].to) + kDayS <= p->since) before = static_cast<int>(i);
                    if (before >= 0) cd.shades.push_back({0, static_cast<double>(before), "pas encore admin", Color{0, 0, 0, 0}});
                }
                cd.partialFrom = statskit::partialFrom(c->weeks, stats::Step::Week, stats::dayOf(d_->now));
            }
            cd.legend = ch::LegendMode::Top;
            cd.yTickCount = 3;
            if (fmt) {
                cd.yFormat = fmt;
                cd.valueFormat = fmt;
            }
            cd.emptyText = c && c->loaded ? std::string(empty) : serviceWord();
            chart->setData(std::move(cd));
        };
        weekly(weeksLic_, c ? c->weekLicences : std::vector<double>{}, c ? c->weekLicencesOthers : std::vector<double>{}, {}, "Aucune licence sur 12 semaines");
        weekly(weeksReq_, c ? c->weekRequests : std::vector<double>{}, c ? c->weekRequestsOthers : std::vector<double>{}, {}, "Aucune demande sur 12 semaines");
        {
            std::vector<double> me, others;
            if (c) {
                for (const double v : c->weekAmount) me.push_back(v / 100.0);
                for (const double v : c->weekAmountOthers) others.push_back(v / 100.0);
            }
            weekly(weeksAmount_, me, others, [](double v) { return ch::fr::eurosCompact(v); }, "Aucune commande sur 12 semaines");
        }
        {
            ch::BarListData bd;
            bd.mode = ch::BarMode::Ranking;
            bd.color = mineC;
            if (c)
                for (std::size_t i = 0; i < c->topClients.size(); ++i) {
                    ch::BarRow row;
                    row.label = c->topClients[i].first;
                    row.value = c->topClients[i].second / 100.0;
                    row.valueText = ch::fr::euros(row.value);
                    if (i < c->topClientLicences.size()) row.extra = ch::fr::integer(c->topClientLicences[i].second) + " lic.";
                    bd.rows.push_back(std::move(row));
                }
            bd.emptyText = c && c->loaded ? std::string("Aucun client sur la p\xC3\xA9riode") : serviceWord();
            clients_->setData(std::move(bd));
        }
        {
            ch::TimeChartData cd;
            cd.kind = ch::Kind::Bars;
            if (c && !c->weeks.empty() && !c->weekDelay.empty()) {
                cd.x = statskit::axisOf(c->weeks, stats::Step::Week);
                cd.xTicks = statskit::ticksOf(c->weeks.size());
                std::vector<double> h;
                for (const double v : c->weekDelay) h.push_back(known(v) ? v / 3600.0 : ch::kNone);
                ch::Series me{"admin", "son d\xC3\xA9lai moyen", h};
                me.color = mineC;
                cd.series = {me};
                const AdminFigures* f = d_->fig(key_);
                if (f && known(f->medianDelay)) cd.refs.push_back({f->medianDelay / 3600.0, "m\xC3\xA9" "diane " + hoursText(f->medianDelay), false, Color{0, 0, 0, 0}});
            }
            cd.yFormat = [](double v) { return ch::fr::hours(v); };
            cd.valueFormat = [](double v) { return ch::fr::hours(v); };
            cd.legend = ch::LegendMode::None;
            cd.emptyText = c && c->loaded ? std::string("Aucune demande trait\xC3\xA9" "e sur 12 semaines") : serviceWord();
            delay_->setData(std::move(cd));
        }
        {
            ch::BarListData bd;
            bd.mode = ch::BarMode::Ranking;
            bd.color = mineC;
            if (c)
                for (const auto& [k, v] : c->byArticle) bd.rows.push_back({.label = statskit::partLabel("article", k), .value = v});
            bd.emptyText = c && c->loaded ? std::string("Aucune commande livr\xC3\xA9" "e") : serviceWord();
            articles_->setData(std::move(bd));
        }
        invalidateLayout();
        invalidate();
    }

protected:
    // ======================================================================== la mise en page
    float place(const Rect& area) override {
        const float x0 = area.x + 2.f, w = area.w - 4.f;
        float y = area.y + 4.f;
        const float gap = 14.f;
        const js::AdminStats* s = d_->stats(key_);
        const Person* p = d_->person(key_);
        // ======== 0 VUE D'ENSEMBLE ========
        anchorRel_[0] = y - area.y;
        const bool wide = w >= 1000.f;
        const float leftW = wide ? std::clamp(w * 0.29f, 300.f, 380.f) : w;
        const float rightX = wide ? x0 + leftW + gap : x0;
        const float rightW = wide ? w - leftW - gap : w;
        // qui et ou
        who_ = {x0, y, leftW, whoHeight(leftW)};
        // les tuiles (deux rangees de quatre ; etroit : deux par rangee)
        const int per = rightW >= 640.f ? 4 : 2;
        const float tw = (rightW - gap * static_cast<float>(per - 1)) / static_cast<float>(per);
        float ty = wide ? y : who_.bottom() + gap;
        for (int i = 0; i < 8; ++i) tiles_[i] = {rightX + static_cast<float>(i % per) * (tw + gap), ty + static_cast<float>(i / per) * (104.f + gap), tw, 104.f};
        ty += static_cast<float>(8 / per) * (104.f + gap);
        // par jour, par type
        const bool split = rightW >= 760.f;
        const float typeW = split ? std::clamp(rightW * 0.36f, 300.f, 440.f) : rightW;
        perDayCard_ = {rightX, ty, split ? rightW - typeW - gap : rightW, 400.f};
        typeCard_ = {split ? perDayCard_.right() + gap : rightX, split ? ty : perDayCard_.bottom() + gap, typeW, 0.f};
        put(perDay_, {perDayCard_.x + 16.f, perDayCard_.y + 70.f, perDayCard_.w - 32.f, perDayCard_.h - 70.f - 44.f});
        const float dw = dayJournal_->naturalWidth();
        put(dayJournal_, {perDayCard_.right() - 16.f - dw, perDayCard_.bottom() - 34.f, dw, 22.f}, s && !s->peaks.empty());
        const float barsH = byType_->preferredHeight();
        put(byType_, {typeCard_.x + 16.f, typeCard_.y + 64.f, typeCard_.w - 32.f, barsH});
        float tcy = typeCard_.y + 64.f + barsH + 10.f;
        unusedBox_ = {};
        const bool unused = s && !s->unused.empty() && p && p->kind == Person::Kind::Admin && p->active() && !own_;
        if (s && !s->unused.empty()) {
            const float bh = 64.f;
            unusedBox_ = {typeCard_.x + 16.f, tcy, typeCard_.w - 32.f, bh};
            tcy += bh + 8.f;
        }
        const float rw = removeUnused_->naturalWidth();
        put(removeUnused_, {unusedBox_.x + 40.f, unusedBox_.y + 38.f, rw, 20.f}, unused);
        typeNoteY_ = tcy;
        tcy += 40.f;
        typeCard_.h = tcy - typeCard_.y;
        if (split) perDayCard_.h = std::max(perDayCard_.h, std::min(typeCard_.h, 520.f));
        put(perDay_, {perDayCard_.x + 16.f, perDayCard_.y + 70.f, perDayCard_.w - 32.f, perDayCard_.h - 70.f - 44.f});
        put(dayJournal_, {perDayCard_.right() - 16.f - dw, perDayCard_.bottom() - 34.f, dw, 22.f}, s && !s->peaks.empty());
        // parmi les admins (sous la courbe) ; par criticite (a gauche, sous qui et ou)
        const float underType = split ? typeCard_.bottom() : typeCard_.bottom();
        amongCard_ = {rightX, split ? std::max(perDayCard_.bottom(), 0.f) + gap : underType + gap, split ? perDayCard_.w : rightW, 250.f};
        if (wide) {
            critCard_ = {x0, who_.bottom() + gap, leftW, critHeight(leftW)};
        } else {
            critCard_ = {x0, std::max(amongCard_.bottom(), typeCard_.bottom()) + gap, w, critHeight(w)};
        }
        y = std::max({critCard_.bottom(), amongCard_.bottom(), typeCard_.bottom(), who_.bottom()}) + 24.f;
        // ======== 1 ACTIVITE ========
        anchorRel_[1] = y - area.y;
        const bool two = w >= 980.f;
        const float hw = two ? std::floor((w - gap) * 0.54f) : w;
        heatCard_ = {x0, y, hw, 380.f};
        put(heat_, {heatCard_.x + 16.f, heatCard_.y + 62.f, heatCard_.w - 32.f, heatCard_.h - 74.f});
        daysCard_ = {two ? x0 + hw + gap : x0, two ? y : heatCard_.bottom() + gap, two ? w - hw - gap : w, 380.f};
        put(strip_, {daysCard_.x + 16.f, daysCard_.y + 116.f, daysCard_.w - 32.f, 40.f});
        put(span_, {daysCard_.x + 16.f, daysCard_.y + 190.f, daysCard_.w - 32.f, daysCard_.h - 200.f});
        y = std::max(heatCard_.bottom(), daysCard_.bottom()) + gap;
        // ======== 6 DROITS, 5 SECURITE ========
        const float rightsW = two ? std::floor((w - gap) * 0.40f) : w;
        anchorRel_[6] = y - area.y;
        rightsCard_ = {x0, y, rightsW, 0.f};
        float ry = y + 78.f;
        for (int i = 0; i < 13; ++i) {
            rightRow_[i] = ry;
            const bool show = s && i < static_cast<int>(s->rights.size()) && s->rights[static_cast<std::size_t>(i)].neverUsed && p && p->kind == Person::Kind::Admin &&
                              p->active() && !own_;
            const float bw = removeOne_[i]->naturalWidth();
            put(removeOne_[i], {rightsCard_.right() - 16.f - bw, ry + 4.f, bw, 22.f}, show);
            ry += 32.f;
        }
        ry += 38.f;
        rightsCard_.h = ry - rightsCard_.y;
        anchorRel_[5] = (two ? y : rightsCard_.bottom() + gap) - area.y;
        secCard_ = {two ? x0 + rightsW + gap : x0, two ? y : rightsCard_.bottom() + gap, two ? w - rightsW - gap : w, 0.f};
        secCard_.h = secHeight(secCard_.w);
        if (two) {
            const float h = std::max(rightsCard_.h, secCard_.h);
            rightsCard_.h = secCard_.h = h;
        }
        const float lw = lines_->naturalWidth();
        put(lines_, {secCard_.right() - 18.f - lw, secCard_.bottom() - 36.f, lw, 22.f}, p && p->kind != Person::Kind::Pending && d_->journal && !own_);
        y = std::max(rightsCard_.bottom(), secCard_.bottom()) + 24.f;
        // ======== 2 LICENCES ET CLIENTS ========
        anchorRel_[2] = y - area.y;
        const bool three = w >= 1100.f;
        const float c1 = three ? std::floor((w - 2.f * gap) * 0.32f) : (two ? std::floor((w - gap) * 0.5f) : w);
        const float c2 = three ? std::floor((w - 2.f * gap) * 0.36f) : c1;
        licCard_ = {x0, y, c1, 360.f};
        const float ofH = std::max(60.f, offers_->preferredHeight());
        put(offers_, {licCard_.x + 16.f, licCard_.y + 158.f, licCard_.w - 32.f, std::min(ofH, 110.f)});
        weeksCard_ = {three || two ? x0 + c1 + gap : x0, three || two ? y : licCard_.bottom() + gap, three ? c2 : (two ? w - c1 - gap : w), 360.f};
        put(weeksLic_, {weeksCard_.x + 16.f, weeksCard_.y + 64.f, weeksCard_.w - 32.f, weeksCard_.h - 76.f});
        if (three) {
            renewCard_ = {weeksCard_.right() + gap, y, w - c1 - c2 - 2.f * gap, 360.f};
            clientsCard_ = {x0, y + 360.f + gap, c1, 0.f};
        } else {
            renewCard_ = {x0, std::max(licCard_.bottom(), weeksCard_.bottom()) + gap, two ? c1 : w, 300.f};
            clientsCard_ = {two ? x0 + c1 + gap : x0, two ? renewCard_.y : renewCard_.bottom() + gap, two ? w - c1 - gap : w, 0.f};
        }
        const float clH = std::max(80.f, clients_->preferredHeight());
        put(clients_, {clientsCard_.x + 16.f, clientsCard_.y + 124.f, clientsCard_.w - 32.f, clH});
        clientsCard_.h = 124.f + clH + 16.f;
        if (!three) renewCard_.h = two ? std::max(renewCard_.h, clientsCard_.h) : renewCard_.h;
        // ======== 3 DEMANDES ========
        float qy = three ? clientsCard_.y : std::max(renewCard_.bottom(), clientsCard_.bottom()) + 24.f;
        anchorRel_[3] = (three ? clientsCard_.y : qy) - area.y;
        reqCard_ = {three ? x0 + c1 + gap : x0, qy, three ? w - c1 - gap : w, 330.f};
        if (three) {
            const float h = std::max(clientsCard_.h, reqCard_.h);
            clientsCard_.h = reqCard_.h = h;
        }
        const float rq0 = reqCard_.x + 16.f, rqW = reqCard_.w - 32.f;
        const float figsW = std::min(260.f, rqW * 0.32f);
        const float chartW = (rqW - figsW - 2.f * gap) * 0.55f;
        put(delay_, {rq0 + figsW + gap, reqCard_.y + 64.f, chartW, reqCard_.h - 76.f});
        put(weeksReq_, {rq0 + figsW + gap + chartW + gap, reqCard_.y + 64.f, rqW - figsW - chartW - 2.f * gap, reqCard_.h - 76.f});
        y = reqCard_.bottom() + 24.f;
        // ======== 4 BOUTIQUE ========
        anchorRel_[4] = y - area.y;
        const float shopW = three ? std::floor((w - gap) * 0.70f) : w;
        shopCard_ = {x0, y, shopW, 320.f};
        const float sx0 = shopCard_.x + 16.f, sw = shopCard_.w - 32.f;
        const float sFigs = std::min(220.f, sw * 0.24f);
        const float sChart = (sw - sFigs - 2.f * gap) * 0.56f;
        put(weeksAmount_, {sx0 + sFigs + gap, shopCard_.y + 64.f, sChart, shopCard_.h - 76.f});
        put(articles_, {sx0 + sFigs + gap + sChart + gap, shopCard_.y + 90.f, sw - sFigs - sChart - 2.f * gap, shopCard_.h - 100.f});
        restCard_ = {three ? x0 + shopW + gap : x0, three ? y : shopCard_.bottom() + gap, three ? w - shopW - gap : w, 320.f};
        y = std::max(shopCard_.bottom(), restCard_.bottom()) + 20.f;
        return y - area.y;
    }

    // ======================================================================== le dessin
    void draw(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        r.pushClip(viewport());
        const Person* p = d_->person(key_);
        if (!p) {
            (void)text(r, viewport().x + 20.f, viewport().y + 20.f, "Cet admin n'est plus dans la liste.", kF14, kMuted);
            r.popClip();
            return;
        }
        paintWho(ctx, *p);
        for (int i = 0; i < 8; ++i) paintTile(ctx, tiles_[i], tileOf(i));
        paintPerDay(ctx);
        paintTypes(ctx);
        paintCrit(ctx);
        paintAmong(ctx, *p);
        paintHeat(ctx, *p);
        paintDays(ctx);
        paintRights(ctx, *p);
        paintSecurity(ctx, *p);
        paintLicences(ctx, *p);
        paintRenew(ctx, *p);
        paintClients(ctx);
        paintRequests(ctx, *p);
        paintShop(ctx, *p);
        paintRest(ctx, *p);
        r.popClip();
    }

private:
    [[nodiscard]] const CardFigures* cardFigs() const {
        const auto it = d_->cards.find(key_);
        return it == d_->cards.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::string serviceWord() const {
        const CardFigures* c = cardFigs();
        if (d_->serviceTooOld) return "Le service est trop ancien : d\xC3\xA9ploie la 1.47.0";
        if (c && c->loading) return "Lecture du service\xE2\x80\xA6";
        if (c && !c->problem.empty()) return "Le service ne r\xC3\xA9pond pas : " + c->problem;
        if (!d_->serviceNote.empty()) return d_->serviceNote;
        return "Le service n'est pas encore lu";
    }
    [[nodiscard]] bool mine(const js::Line& l) const {
        if (l.key == key_) return true;
        const Person* p = d_->person(key_);
        if (p && p->kind == Person::Kind::Plus) return std::find(d_->editorKeys.begin(), d_->editorKeys.end(), l.key) != d_->editorKeys.end();
        return false;
    }
    [[nodiscard]] std::string she() const {
        const Person* p = d_->person(key_);
        return own_ ? std::string("toi") : p ? firstName(p->name) : std::string("lui");
    }

    // ---- 0. qui et ou ----
    struct WhoRow {
        WhoRow(std::string l, std::string v, std::string f, std::string t) : label(std::move(l)), value(std::move(v)), faint(std::move(f)), tag(std::move(t)) {}
        std::string label, value, faint, tag;
        Color       valueColor{kInk};
        std::vector<std::pair<std::string, Color>> more;   // des lignes sous la valeur
    };
    [[nodiscard]] std::vector<WhoRow> whoRows() const {
        std::vector<WhoRow> out;
        const Person* p = d_->person(key_);
        if (!p) return out;
        const AdminFigures* f = d_->fig(key_);
        const js::AdminStats* s = d_->stats(key_);
        out.push_back({"Courriel", "\xE2\x80\x94", "pas dans la liste sign\xC3\xA9" "e", ""});
        out.push_back({"Son PC", p->pc.empty() ? std::string("\xE2\x80\x94") : p->pc, "", ""});
        if (!p->machine.empty()) out.push_back({"Empreinte du PC", groups4(p->machine, 4), "", ""});
        {
            WhoRow r{"Sa cl\xC3\xA9", kit::shortFingerprint(p->fingerprint), "", ""};
            if (p->kind == Person::Kind::Plus) r.more.push_back({"la cl\xC3\xA9 de l'\xC3\xA9" "diteur", kVioletInk});
            else if (p->retired) r.more.push_back({"certificat r\xC3\xA9voqu\xC3\xA9 le " + dayMonth(p->retired), kRed});
            else r.more.push_back({"certifi\xC3\xA9" "e \xE2\x9C\x93", kGreen});
            out.push_back(std::move(r));
        }
        {
            WhoRow r{"Son profil", p->kind == Person::Kind::Plus ? std::string("Admin+ (tous les droits)") : (p->profile.empty() ? std::string("sans profil") : p->profile), "", ""};
            if (p->gaps > 0) r.value += ", " + std::to_string(p->gaps) + (p->gaps > 1 ? " \xC3\xA9" "carts" : " \xC3\xA9" "cart");
            if (s)
                for (const auto& c : s->rightsChanges) {
                    for (const auto& a : c.removed) r.more.push_back({std::string(ch::fr::kMinus) + " " + rightShort(a) + " (" + dayMonth(c.line.when) + ")", kRed});
                    for (const auto& a : c.added) r.more.push_back({"+ " + rightShort(a) + " (" + dayMonth(c.line.when) + ")", kGreen});
                    if (r.more.size() > 4) break;
                }
            out.push_back(std::move(r));
        }
        if (p->since > 0) out.push_back({p->kind == Person::Kind::Plus ? "Admin+" : "Ajout\xC3\xA9", "le " + kit::whenFr(0, p->since).substr(0, 10), p->addedBy.empty() ? "" : "par " + p->addedBy, ""});
        {
            WhoRow r{"\xC3\x89tat", p->retired ? std::string("Retir\xC3\xA9") : std::string("Actif"), "", ""};
            r.valueColor = p->retired ? kRed : kGreen;
            if (p->since > 0) r.faint = std::to_string(std::max<std::int64_t>(0, ((p->retired ? p->retired : d_->now) - p->since) / kDayS)) + " jours d'admin";
            out.push_back(std::move(r));
        }
        {
            const std::string ver = f && f->hasUsage && !f->usage.softwareVersion.empty() ? f->usage.softwareVersion : p->version;
            WhoRow r{"Son Admin", ver.empty() ? std::string("\xE2\x80\x94") : ver, "", "N"};
            if (!ver.empty() && !d_->latestAdmin.empty()) {
                if (olderVersion(ver, d_->latestAdmin)) r.more.push_back({"pas \xC3\xA0 jour (" + d_->latestAdmin + ")", kAmber});
                else r.faint = "\xC3\xA0 jour \xE2\x9C\x93";
            }
            out.push_back(std::move(r));
        }
        if (f && f->hasUsage) {
            const auto& u = f->usage;
            std::string where = u.city.empty() ? std::string() : u.city + (u.country.empty() ? std::string() : ", " + u.country);
            out.push_back({"Vue", ago(d_->now, u.lastSeen), where, "N"});
            out.push_back({"Connect\xC3\xA9" + std::string(p->kind == Person::Kind::Plus ? "" : ""), std::to_string(u.days.size()) + " jours sur " + std::to_string(d_->period.days()),
                           "", "N"});
            if (u.usualStart >= 0) {
                const Seconds day = d_->now;
                const int a = statskit::localHour(u.usualStart, day), b = statskit::localHour(u.usualEnd, day);
                out.push_back({"Ses heures", std::to_string(a) + " h " + ch::fr::kArrow + " " + std::to_string(b + 1) + " h", "d\xC3\xA9" "but et fin, d'habitude", "N"});
            }
        } else {
            out.push_back({"Vue", "\xE2\x80\x94", d_->serviceLoading ? "lecture du service\xE2\x80\xA6" : "le service : pas de chiffres", "N"});
        }
        return out;
    }
    [[nodiscard]] float whoHeight(float w) const {
        float h = 64.f;
        for (const auto& r : whoRows()) {
            h += 36.f + 20.f * static_cast<float>(r.more.size());
            (void)w;
        }
        return h + 8.f;
    }
    void paintWho(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, who_);
        const float x = who_.x + 18.f, w = who_.w - 36.f;
        ui::drawIcon(r, ui::Icon::User, {x, who_.y + 20.f, 15.f, 15.f}, kMuted);
        (void)cardTitle(r, x + 22.f, who_.y + 18.f, w - 22.f, "Qui et o\xC3\xB9", "", "J S");
        float y = who_.y + 56.f;
        const float lw = std::min(140.f, w * 0.40f);
        for (const auto& row : whoRows()) {
            r.fillRect({x, y - 6.f, w, 1.f}, kLine2);
            (void)text(r, x, y + 4.f, row.label, kF13, kMuted, lw - 6.f);
            float vx = x + lw;
            vx += text(r, vx, y + 4.f, row.value, kB13, row.valueColor, w - lw - (row.tag.empty() ? 0.f : 26.f));
            if (!row.faint.empty()) vx += 6.f + text(r, vx + 6.f, y + 4.f, row.faint, kF12, row.faint.find("\xE2\x9C\x93") != std::string::npos ? kGreen : kMuted, x + w - vx - 32.f);
            if (!row.tag.empty() && vx + 26.f < x + w + 4.f) (void)sourceTag(r, x + w - 18.f, y + 3.f, row.tag);
            float my = y + 24.f;
            for (const auto& [t, c] : row.more) {
                (void)text(r, x + lw, my, t, kF12, c, w - lw);
                my += 20.f;
            }
            y += 36.f + 20.f * static_cast<float>(row.more.size());
        }
        (void)p;
    }

    // ---- 0. les tuiles ----
    [[nodiscard]] Tile tileOf(int i) const {
        const js::AdminStats* s = d_->stats(key_);
        const AdminFigures* f = d_->fig(key_);
        const CardFigures* c = cardFigs();
        Tile t;
        switch (i) {
            case 0:
                t.label = "Actions";
                t.value = s ? std::to_string(s->actions) : num(kNaN);
                if (s) t.cur = s->actions, t.prev = s->actionsBefore, t.extra = std::to_string(s->actionsBefore) + " avant";
                t.spark = weeksActions_;
                break;
            case 1:
                t.label = "Licences sign\xC3\xA9" "es";
                t.value = s ? num(s->licences) : num(kNaN);
                if (s) t.cur = s->licences, t.prev = s->licencesBefore, t.count = true, t.extra = num(s->licencesBefore) + " avant";
                if (c) t.spark = c->weekLicences;
                break;
            case 2:
                t.label = "Demandes trait\xC3\xA9" "es";
                t.value = s ? num(s->requests) : num(kNaN);
                if (s) t.cur = s->requests, t.prev = s->requestsBefore, t.count = true;
                if (f && known(f->accepted) && known(f->requests) && f->requests > 0) t.extra = ch::fr::percent(100.0 * f->accepted / f->requests) + " accept\xC3\xA9" "es";
                if (c) t.spark = c->weekRequests;
                break;
            case 3:
                t.label = "Montant encaiss\xC3\xA9";
                t.value = f ? euros(f->amount) : num(kNaN);
                if (f) t.cur = f->amount, t.prev = f->amountBefore;
                if (f && known(f->orders)) t.extra = plural(f->orders, "commande", "commandes");
                if (c) t.spark = c->weekAmount;
                break;
            case 4:
                t.label = "D\xC3\xA9lai de r\xC3\xA9ponse";
                t.value = f ? hoursText(f->meanDelay) : num(kNaN);
                if (f && known(f->meanDelay) && known(f->meanDelayBefore)) {
                    const double dh = (f->meanDelay - f->meanDelayBefore) / 3600.0;
                    t.deltaText = (dh < 0 ? std::string(ch::fr::kMinus) : std::string("+")) + ch::fr::hours(std::abs(dh));
                    t.deltaGood = dh <= 0;
                }
                if (f && known(f->medianDelay)) t.extra = "m\xC3\xA9" "diane " + hoursText(f->medianDelay);
                break;
            case 5:
                t.label = "Jours actifs";
                t.value = s ? std::to_string(s->activeDays) : num(kNaN);
                t.unit = "sur " + std::to_string(d_->period.days());
                if (s) {
                    t.cur = s->activeDays, t.prev = s->activeDaysBefore, t.count = true;
                    int weekend = 0;
                    for (std::size_t k = 0; k < s->perDay.size(); ++k)
                        if (s->perDay[k] > 0 && js::weekday(d_->period.first + static_cast<std::int64_t>(k)) >= 5) ++weekend;
                    if (weekend) t.extra = "dont " + std::to_string(weekend) + (weekend > 1 ? " jours de fin de semaine" : " jour de fin de semaine");
                }
                break;
            case 6:
                t.label = "S\xC3\xA9rie en cours";
                t.value = s ? std::to_string(s->streak) : num(kNaN);
                t.unit = "jours ouvr\xC3\xA9s";
                if (s) t.extra = "son record : " + std::to_string(s->bestStreak) + " jours";
                break;
            default:
                t.label = "Derni\xC3\xA8re action";
                t.value = s && s->last.number ? ago(d_->now, s->last.when) : std::string("aucune");
                if (s && s->last.number) t.extra = shortSummary(s->last);
                break;
        }
        return t;
    }
    void paintTile(const ui::PaintContext& ctx, const Rect& b, const Tile& t) const {
        auto& r = ctx.r;
        card(r, ctx.theme, b, 12.f);
        const float x = b.x + 16.f, w = b.w - 32.f;
        (void)text(r, x, b.y + 14.f, t.label, kF13, kInk2, w);
        const FontId big = gfx::semibold(FontId{t.value.size() > 9 ? static_cast<std::uint16_t>(19) : static_cast<std::uint16_t>(24)});
        const float sparkW = t.spark.size() > 1 ? std::min(84.f, w * 0.38f) : 0.f;
        const float vw = text(r, x, b.y + 34.f, t.value, big, kInk, w - sparkW - 4.f);
        if (!t.unit.empty()) (void)text(r, x + vw + 6.f, b.y + 44.f, t.unit, kB13, kInk2, w - vw - sparkW - 10.f);
        if (sparkW > 0.f) {
            const Person* p = d_->person(key_);
            ch::SparkStyle st;
            st.color = p ? personColor(*p) : kViolet;
            ch::paintSparkline(r, ctx.theme, {x + w - sparkW, b.y + 34.f, sparkW, 30.f}, t.spark, nullptr, st);
        }
        float ex = x;
        if (!t.deltaText.empty()) ex += text(r, ex, b.y + 74.f, t.deltaText, kB12, t.deltaGood ? kGreen : kRed, w) + 6.f;
        else if (known(t.cur) && known(t.prev)) ex += delta(r, ex, b.y + 74.f, t.cur, t.prev, t.upIsGood, kB12, t.count) + 6.f;
        if (!t.extra.empty()) (void)text(r, ex, b.y + 74.f, (ex > x ? std::string("\xC2\xB7 ") : std::string()) + t.extra, kF12, kMuted, x + w - ex);
    }

    // ---- 0. par jour ----
    void paintPerDay(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, perDayCard_);
        const js::AdminStats* s = d_->stats(key_);
        const float x = perDayCard_.x + 16.f, w = perDayCard_.w - 32.f;
        (void)cardTitle(r, x, perDayCard_.y + 16.f, w * 0.55f, "Ses actions par jour",
                        "du " + ch::fr::ddmm(d_->period.first) + " au " + ch::fr::ddmm(d_->period.last) + " \xC2\xB7 fins de semaine en clair", "");
        // la legende a droite
        if (s) {
            float lx = x + w - 22.f;
            (void)sourceTag(r, lx, perDayCard_.y + 16.f, "J");
            const std::string b = "avant " + std::to_string(s->actionsBefore);
            lx -= ui::measureWidth(b, kF12) + 12.f;
            r.drawText({lx, perDayCard_.y + 17.f}, b, kF12, kInk2);
            r.fillRect({lx - 18.f, perDayCard_.y + 25.f, 12.f, 2.f}, ch::colors::kCompare);
            const std::string a = "cette p\xC3\xA9riode " + std::to_string(s->actions);
            lx -= 30.f + ui::measureWidth(a, kF12);
            r.drawText({lx, perDayCard_.y + 17.f}, a, kF12, kInk2);
            const Person* p = d_->person(key_);
            r.fillRoundedRect({lx - 16.f, perDayCard_.y + 20.f, 10.f, 10.f}, p ? personColor(*p) : kViolet, 2.f);
        }
        // sous le graphique : ses absences, son pic
        if (s) {
            std::string note;
            std::vector<std::string> absent;
            for (std::size_t i = 0; i < s->perDay.size(); ++i)
                if (s->perDay[i] == 0 && js::weekday(d_->period.first + static_cast<std::int64_t>(i)) < 5) absent.push_back(ch::fr::ddmm(d_->period.first + static_cast<std::int64_t>(i)));
            if (!absent.empty() && absent.size() <= 3) {
                note = "absent(e) le" + std::string(absent.size() > 1 ? "s " : " ");
                for (std::size_t i = 0; i < absent.size(); ++i) note += (i ? (i + 1 == absent.size() ? " et " : ", ") : "") + absent[i];
            } else if (!absent.empty()) {
                note = std::to_string(absent.size()) + " jours ouvr\xC3\xA9s sans action";
            }
            if (!s->peaks.empty()) {
                const auto& pk = s->peaks.back();
                note += (note.empty() ? "" : "   ") + std::string("pic le ") + ch::fr::ddmm(pk.day) + " : " + std::to_string(pk.count) + " actions";
            }
            (void)text(r, x, perDayCard_.bottom() - 32.f, note, kF12, kMuted, w - (s->peaks.empty() ? 0.f : dayJournal_->naturalWidth() + 16.f));
        }
    }

    // ---- 0. par type ----
    void paintTypes(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, typeCard_);
        const float x = typeCard_.x + 16.f, w = typeCard_.w - 32.f;
        (void)cardTitle(r, x, typeCard_.y + 16.f, w, "Ses actions par type", "les 13 droits, puis la boutique \xC2\xB7 toujours dans cet ordre", "J");
        const js::AdminStats* s = d_->stats(key_);
        if (!unusedBox_.empty() && s) {
            r.fillRoundedRect(unusedBox_, kAmberPale, 10.f);
            ui::drawIcon(r, ui::Icon::Lock, {unusedBox_.x + 12.f, unusedBox_.y + 12.f, 16.f, 16.f}, kAmber);
            std::string names;
            for (const auto& id : s->unused) names += (names.empty() ? "" : ", ") + rightShort(id);
            (void)text(r, unusedBox_.x + 40.f, unusedBox_.y + 10.f,
                       std::to_string(s->unused.size()) + (s->unused.size() > 1 ? " droits jamais utilis\xC3\xA9s" : " droit jamais utilis\xC3\xA9") + " en " +
                           std::to_string(d_->unusedDays) + " jours : " + names + ".",
                       kB13, kAmber, unusedBox_.w - 52.f);
        }
        (void)kit::drawWrapped(r, x, typeNoteY_, w, "\xC2\xAB pas ce droit \xC2\xBB : ni dans son profil, ni ajout\xC3\xA9 pour " + she() + ".", kF12, kMuted, 17.f, 2);
    }

    // ---- 0. par criticite ----
    [[nodiscard]] float critHeight(float w) const {
        float h = 150.f;
        for (const auto& l : critNotes()) h += kit::wrappedHeight(l, kF12, w - 36.f, 17.f, 3) + 6.f;
        return h + 10.f;
    }
    [[nodiscard]] std::vector<std::string> critNotes() const {
        std::vector<std::string> out;
        const js::AdminStats* s = d_->stats(key_);
        if (!s) return out;
        const auto refs = [](const std::vector<js::Ref>& v) {
            std::string t;
            for (std::size_t i = 0; i < v.size() && i < 3; ++i) t += (t.empty() ? "" : ", ") + v[i].object;
            return t;
        };
        if (!s->revocations.empty())
            out.push_back("Critique : " + std::string(s->revocations.size() > 1 ? "ses " : "sa ") + std::to_string(s->revocations.size()) +
                          (s->revocations.size() > 1 ? " r\xC3\xA9vocations (" : " r\xC3\xA9vocation (") + refs(s->revocations) + ").");
        std::string high;
        if (!s->codeReviews.empty()) high += std::to_string(s->codeReviews.size()) + (s->codeReviews.size() > 1 ? " codes revus" : " code revu");
        if (!s->deletions.empty())
            high += (high.empty() ? "" : ", ") + std::to_string(s->deletions.size()) + (s->deletions.size() > 1 ? " clients supprim\xC3\xA9s" : " client supprim\xC3\xA9");
        if (!high.empty()) out.push_back("Haute : " + high + ".");
        return out;
    }
    void paintCrit(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, critCard_);
        const js::AdminStats* s = d_->stats(key_);
        const float x = critCard_.x + 18.f, w = critCard_.w - 36.f;
        (void)cardTitle(r, x, critCard_.y + 18.f, w, "Par criticit\xC3\xA9", "ses " + std::to_string(s ? s->actions : 0) + " actions de la p\xC3\xA9riode", "J");
        if (!s) return;
        double total = 0;
        std::vector<double> v;
        std::vector<Color> colors;
        for (int i = 0; i < 4; ++i) {
            v.push_back(s->byLevel[static_cast<std::size_t>(i)]);
            colors.push_back(ch::colors::criticality(i));
            total += s->byLevel[static_cast<std::size_t>(i)];
        }
        const float by = critCard_.y + 66.f;
        if (total > 0) ch::paintSegments(r, ctx.theme, {x, by, w, 12.f}, v, colors);
        else r.fillRoundedRect({x, by, w, 12.f}, kSunken, 4.f);
        const float cw = w / 4.f;
        for (int i = 0; i < 4; ++i) {
            const js::Level lv = kit::kLevels[i];
            const kit::Tint tint = kit::levelTint(lv);
            const float cx = x + static_cast<float>(i) * cw;
            (void)kit::pill(r, cx, by + 22.f, levelName(lv), gfx::semibold(kF12), tint.fg, tint.bg, 22.f);
            const std::string n = std::to_string(s->byLevel[static_cast<std::size_t>(i)]);
            const float nw = text(r, cx, by + 52.f, n, gfx::semibold(FontId{18}), kInk, cw);
            if (total > 0) (void)text(r, cx + nw + 4.f, by + 57.f, ch::fr::percent(100.0 * s->byLevel[static_cast<std::size_t>(i)] / total), kF12, kMuted, cw - nw - 6.f);
        }
        float ny = by + 88.f;
        for (const auto& l : critNotes()) ny += kit::drawWrapped(r, x, ny, w, l, kF12, kMuted, 17.f, 3) + 6.f;
    }

    // ---- 0. parmi les admins ----
    void paintAmong(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, amongCard_);
        const float x = amongCard_.x + 16.f, w = amongCard_.w - 32.f;
        (void)cardTitle(r, x, amongCard_.y + 16.f, w, own_ ? "Toi parmi les admins" : firstName(p.name) + " parmi les admins",
                        "sa part des admins ; le d\xC3\xA9lai : un point par admin, " + she() + " en couleur", "J S");
        const Color mineC = personColor(p);
        struct Row {
            std::string label;
            double      me{kNaN}, all{0};
            int         rank{0}, of{0};
            bool        smallIsBest{false};
        };
        std::vector<Row> rows(4);
        rows[0].label = "Actions";
        rows[1].label = "Licences sign\xC3\xA9" "es";
        rows[2].label = "Demandes trait\xC3\xA9" "es";
        rows[3].label = "Montant encaiss\xC3\xA9";
        std::vector<std::vector<double>> vals(4);
        struct Dot {
            double v;
            Color  c;
            bool   me;
        };
        std::vector<Dot> delays;
        double myDelay = kNaN;
        for (const auto& q : d_->people) {
            if (q.kind == Person::Kind::Pending) continue;
            const js::AdminStats* s = d_->stats(q.key);
            const AdminFigures* f = d_->fig(q.key);
            const double v[4] = {s ? static_cast<double>(s->actions) : kNaN, s ? s->licences : kNaN, s ? s->requests : kNaN, f ? f->amount : kNaN};
            for (int i = 0; i < 4; ++i) {
                if (!known(v[i])) continue;
                rows[static_cast<std::size_t>(i)].all += v[i];
                vals[static_cast<std::size_t>(i)].push_back(v[i]);
                if (q.key == key_) rows[static_cast<std::size_t>(i)].me = v[i];
            }
            if (f && known(f->meanDelay)) {
                delays.push_back({f->meanDelay, q.key == key_ ? mineC : ch::colors::mix(personColor(q), Color::rgb(0xFFFFFF), 0.55f), q.key == key_});
                if (q.key == key_) myDelay = f->meanDelay;
            }
        }
        float y = amongCard_.y + 70.f;
        const float lw = std::min(170.f, w * 0.30f), bw = w - lw - 150.f;
        for (std::size_t i = 0; i < 4; ++i) {
            const Row& row = rows[i];
            (void)text(r, x, y, row.label, kF13, kInk, lw);
            const double share = known(row.me) && row.all > 0 ? row.me / row.all : kNaN;
            ch::paintInlineBar(r, ctx.theme, {x + lw, y + 4.f, bw, 9.f}, known(share) ? share : 0.0, mineC);
            (void)text(r, x + lw + bw + 12.f, y, known(share) ? ch::fr::percent(share * 100.0) : std::string("\xE2\x80\x94"), kB13, kInk, 60.f);
            if (known(row.me)) {
                int rank = 1;
                for (const double v : vals[i]) rank += v > row.me ? 1 : 0;
                const std::string rk = rank == 1 ? "1re sur " + std::to_string(vals[i].size()) : std::to_string(rank) + "e sur " + std::to_string(vals[i].size());
                (void)text(r, x + lw + bw + 74.f, y, rk, kF12, kMuted, 76.f);
            }
            y += 34.f;
        }
        // le delai : un point par admin
        (void)text(r, x, y, "D\xC3\xA9lai de r\xC3\xA9ponse", kF13, kInk, lw);
        double maxD = 0;
        for (const auto& dt : delays) maxD = std::max(maxD, dt.v);
        r.fillRect({x + lw, y + 8.f, bw, 1.f}, kLine);
        for (const auto& dt : delays) {
            const float px = x + lw + static_cast<float>(maxD > 0 ? dt.v / maxD : 0.0) * bw;
            ch::fillDisc(r, {px, y + 8.f}, dt.me ? 5.f : 4.f, dt.c);
        }
        if (known(myDelay)) {
            int rank = 1;
            for (const auto& dt : delays) rank += dt.v < myDelay ? 1 : 0;
            (void)text(r, x + lw + bw + 12.f, y, hoursText(myDelay), kB13, kInk, 60.f);
            (void)text(r, x + lw + bw + 74.f, y, rank == 1 ? std::string("le plus rapide") : std::to_string(rank) + "e plus rapide", kF12, kMuted, 76.f);
        } else {
            (void)text(r, x + lw + bw + 12.f, y, "\xE2\x80\x94", kB13, kFaint, 60.f);
        }
    }

    // ---- 1. la carte jour x heure ----
    void paintHeat(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, heatCard_);
        const js::AdminStats* s = d_->stats(key_);
        (void)p;
        (void)cardTitle(r, heatCard_.x + 16.f, heatCard_.y + 16.f, heatCard_.w - 32.f, own_ ? "Quand tu travailles" : "Quand " + she() + " travaille",
                        (s ? "ses " + std::to_string(s->actions) + " actions, " : std::string()) + "par jour de la semaine et par heure (Paris) \xC2\xB7 du " +
                            ch::fr::ddmm(d_->period.first) + " au " + ch::fr::ddmm(d_->period.last),
                        "J");
    }
    // ---- 1. ses jours ----
    void paintDays(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, daysCard_);
        const js::AdminStats* s = d_->stats(key_);
        const AdminFigures* f = d_->fig(key_);
        const float x = daysCard_.x + 16.f, w = daysCard_.w - 32.f;
        (void)cardTitle(r, x, daysCard_.y + 16.f, w, "Ses jours", "un carr\xC3\xA9 par jour, plus fonc\xC3\xA9 = plus d'actions ; la s\xC3\xA9rie en cours soulign\xC3\xA9" "e",
                        "J N");
        const float cw = w / 4.f, fy = daysCard_.y + 62.f;
        const auto fig = [&](int i, const std::string& label, const std::string& big, const std::string& small) {
            const float cx = x + static_cast<float>(i) * cw;
            (void)text(r, cx, fy, label, kF12, kMuted, cw - 8.f);
            const float bw = text(r, cx, fy + 16.f, big, gfx::semibold(FontId{18}), kInk, cw - 8.f);
            (void)text(r, cx + bw + 5.f, fy + 21.f, small, kF12, kInk2, cw - bw - 12.f);
        };
        if (s) {
            fig(0, "Jours actifs", std::to_string(s->activeDays), "sur " + std::to_string(d_->period.days()) + " \xC2\xB7 " + ch::fr::signedInteger(s->activeDays - s->activeDaysBefore));
            fig(1, "S\xC3\xA9rie en cours", std::to_string(s->streak), "jours ouvr\xC3\xA9s \xC2\xB7 record " + std::to_string(s->bestStreak));
            if (f && f->hasUsage) {
                int idle = 0;
                for (const auto& day : f->usage.days) {
                    std::int64_t dd = 0;
                    if (js::parseDay(day, dd) && d_->period.contains(dd) && dd - d_->period.first < static_cast<std::int64_t>(s->perDay.size()) &&
                        s->perDay[static_cast<std::size_t>(dd - d_->period.first)] == 0)
                        ++idle;
                }
                fig(2, "Connect\xC3\xA9", std::to_string(f->usage.days.size()), "jours \xC2\xB7 " + std::to_string(idle) + " sans action");
            } else {
                fig(2, "Connect\xC3\xA9", "\xE2\x80\x94", "le service");
            }
            fig(3, "Derni\xC3\xA8re action", s->last.number ? clockOf(s->last.when) : std::string("\xE2\x80\x94"),
                s->last.number ? (js::localDay(s->last.when, js::TimeZone::paris()) == js::localDay(d_->now, js::TimeZone::paris()) ? std::string("auj.") : dayMonth(s->last.when))
                               : std::string());
        }
        const float ly = daysCard_.y + 166.f;
        (void)text(r, x, ly, "D\xC3\xA9" "but et fin de ses journ\xC3\xA9" "es", kB13, kInk, w * 0.5f);
        float lx = x + w;
        const auto legend = [&](const std::string& t, Color c, bool box) {
            const float tw = ui::measureWidth(t, kF12);
            lx -= tw;
            r.drawText({lx, ly + 1.f}, t, kF12, kInk2);
            lx -= 14.f;
            if (box) r.fillRoundedRect({lx, ly + 3.f, 10.f, 10.f}, c, 2.f);
            else r.fillRoundedRect({lx + 3.f, ly + 1.f, 4.f, 13.f}, c, 2.f);
            lx -= 12.f;
        };
        const Person* p = d_->person(key_);
        legend("inhabituel", kRed, false);
        legend("habituel", Color::rgb(0xEAF2FD), true);
        legend("premi\xC3\xA8re \xE2\x86\x92 derni\xC3\xA8re action", p ? personColor(*p) : kViolet, false);
    }

    // ---- 6. ses droits ----
    void paintRights(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, rightsCard_);
        const js::AdminStats* s = d_->stats(key_);
        const float x = rightsCard_.x + 16.f, w = rightsCard_.w - 32.f;
        const std::string since = p.since > 0 ? "depuis qu'" + std::string(own_ ? "tu es" : "il est") + " admin (" +
                                                    std::to_string(std::max<std::int64_t>(0, (d_->now - p.since) / kDayS)) + " jours, au plus " +
                                                    std::to_string(d_->unusedDays) + ")"
                                              : "sur " + std::to_string(d_->unusedDays) + " jours";
        (void)cardTitle(r, x, rightsCard_.y + 16.f, w, own_ ? "Tes droits : utilis\xC3\xA9s ou non" : "Ses droits : utilis\xC3\xA9s ou non", since, "J");
        const float c1 = x, c2 = x + w * 0.46f, c3 = x + w * 0.60f, c4 = x + w * 0.70f;
        const float hy = rightsCard_.y + 60.f;
        for (const auto& [cx, t] : std::vector<std::pair<float, std::string>>{{c1, "DROIT"}, {c2, "DONN\xC3\x89"}, {c3, "FOIS"}, {c4, "QUAND"}})
            r.drawText({cx, hy}, t, gfx::semibold(kF11), kFaint);
        r.fillRect({x, hy + 16.f, w, 1.f}, kLine);
        if (p.kind == Person::Kind::Plus) {
            (void)kit::drawWrapped(r, x, hy + 30.f, w, "L'admin+ a tous les droits, et ceux qu'il garde pour lui (les versions, la sauvegarde, les admins, les droits).", kF13,
                                   kMuted, 18.f, 3);
            return;
        }
        if (!s) return;
        for (int i = 0; i < 13 && i < static_cast<int>(s->rights.size()); ++i) {
            const auto& u = s->rights[static_cast<std::size_t>(i)];
            const float y = rightRow_[i];
            if (u.neverUsed) r.fillRect({rightsCard_.x + 1.f, y - 2.f, rightsCard_.w - 2.f, 30.f}, kAmberPale);
            r.fillRect({x, y + 28.f, w, 1.f}, kLine2);
            const bool off = !u.held;
            (void)text(r, c1, y + 6.f, u.label, kF13, off ? kFaint : kInk, c2 - c1 - 8.f);
            if (u.held) {
                kit::drawTick(r, c2, y + 9.f, kGreen);
                if (u.given > 0 && u.given > p.since + kDayS) (void)text(r, c2 + 16.f, y + 6.f, dayMonth(u.given), kF12, kGreen, c3 - c2 - 18.f);
            } else {
                (void)text(r, c2, y + 6.f, u.count > 0 ? std::string("retir\xC3\xA9") : std::string("\xE2\x80\x94"), kF12, kFaint, c3 - c2 - 6.f);
            }
            const std::string n = u.held || u.count ? std::to_string(u.countWindow) : std::string("\xE2\x80\x94");
            (void)text(r, c3, y + 6.f, n, kB13, off ? kFaint : kInk, c4 - c3 - 6.f);
            if (u.neverUsed) (void)text(r, c4, y + 6.f, "jamais", kB13, kAmber, x + w - c4);
            else if (u.last > 0) (void)text(r, c4, y + 6.f, ago(d_->now, u.last), kF13, off ? kFaint : kInk2, x + w - c4 - (u.neverUsed ? 120.f : 0.f));
        }
        (void)kit::drawWrapped(r, x, rightRow_[12] + 40.f, w, "Retirer un droit ne casse rien de ce qu'" + std::string(own_ ? "tu as" : "il a") + " sign\xC3\xA9 (ses licences restent valides).",
                               kF12, kMuted, 17.f, 2);
    }

    // ---- 5. sa securite ----
    struct SecBlock {
        std::string                                      title;
        ui::Icon                                         icon;
        std::string                                      count;
        std::vector<std::pair<std::string, std::string>> lines;   // (quand, quoi)
        bool                                             good{false};
    };
    [[nodiscard]] std::vector<SecBlock> secBlocks() const {
        std::vector<SecBlock> out;
        const js::AdminStats* s = d_->stats(key_);
        const Person* p = d_->person(key_);
        if (!s || !p) return out;
        {
            SecBlock b{"Signatures refus\xC3\xA9" "es", ui::Icon::Error, std::to_string(s->refused.size()), {}};
            for (std::size_t i = s->refused.size(); i-- > 0 && b.lines.size() < 3;) {
                const auto& x = s->refused[i];
                b.lines.push_back({dayMonth(x.line.when) + " " + clockOf(x.line.when),
                                   shortSummary(x.line) + " : " + (x.reason == js::RefusalReason::MissingRight ? "droit manquant" : "cl\xC3\xA9 retir\xC3\xA9" "e")});
            }
            if (s->refused.empty()) b.lines.push_back({"", "aucune : sa cl\xC3\xA9 est certifi\xC3\xA9" "e"});
            out.push_back(std::move(b));
        }
        {
            SecBlock b{"Depuis un autre PC que le sien", ui::Icon::Screen, std::to_string(s->otherPc.size()), {}};
            for (std::size_t i = s->otherPc.size(); i-- > 0 && b.lines.size() < 2;) {
                const auto& x = s->otherPc[i];
                b.lines.push_back({dayMonth(x.line.when) + " " + clockOf(x.line.when), (x.pc.empty() ? x.machine : x.pc) + " : " + shortSummary(x.line)});
            }
            b.lines.push_back({"son PC", (p->pc.empty() ? std::string("\xE2\x80\x94") : p->pc) + " : " + std::to_string(std::max(0, s->actions - static_cast<int>(s->otherPc.size()))) +
                                             " autres"});
            out.push_back(std::move(b));
        }
        {
            SecBlock b{"Heures inhabituelles, pics", ui::Icon::History, std::to_string(s->unusual.size() + s->peaks.size()), {}};
            for (std::size_t i = s->unusual.size(); i-- > 0 && b.lines.size() < 2;)
                b.lines.push_back({ch::fr::weekdayShort(js::weekday(js::localDay(s->unusual[i].when, js::TimeZone::paris()))) + " " + dayMonth(s->unusual[i].when),
                                   clockOf(s->unusual[i].when) + ", " + shortSummary(s->unusual[i])});
            for (std::size_t i = s->peaks.size(); i-- > 0 && b.lines.size() < 3;)
                b.lines.push_back({ch::fr::weekdayShort(js::weekday(s->peaks[i].day)) + " " + ch::fr::ddmm(s->peaks[i].day),
                                   "pic : " + std::to_string(s->peaks[i].count) + " actions, " + ch::fr::decimal(s->peaks[i].factor) + "\xC3\x97 sa moyenne"});
            if (!s->habitsKnown) b.lines.push_back({"", "pas encore assez d'actions pour savoir ses heures"});
            else if (b.lines.empty()) b.lines.push_back({"", "aucune : tout est dans ses habitudes"});
            out.push_back(std::move(b));
        }
        {
            SecBlock b{"Ce qui compte, le d\xC3\xA9tail", ui::Icon::Document,
                       std::to_string(s->deletions.size() + s->revocations.size() + s->codeReviews.size()), {}};
            const auto list = [](const std::vector<js::Ref>& v) {
                std::string t;
                for (std::size_t i = 0; i < v.size() && i < 3; ++i) t += (t.empty() ? "" : ", ") + v[i].object;
                return t;
            };
            if (!s->deletions.empty()) b.lines.push_back({"clients", std::to_string(s->deletions.size()) + (s->deletions.size() > 1 ? " supprim\xC3\xA9s \xC2\xB7 " : " supprim\xC3\xA9 \xC2\xB7 ") + list(s->deletions)});
            if (!s->revocations.empty()) b.lines.push_back({"r\xC3\xA9vocations", std::to_string(s->revocations.size()) + " \xC2\xB7 " + list(s->revocations)});
            if (!s->codeReviews.empty())
                b.lines.push_back({"codes revus", std::to_string(s->codeReviews.size()) + " \xC2\xB7 le dernier : " + s->codeReviews.back().object + ", " + ago(d_->now, s->codeReviews.back().when)});
            if (b.lines.empty()) b.lines.push_back({"", "ni suppression, ni r\xC3\xA9vocation, ni code revu"});
            out.push_back(std::move(b));
        }
        {
            SecBlock b{own_ ? "Tes droits chang\xC3\xA9s" : "Ses droits chang\xC3\xA9s", ui::Icon::Lock, std::to_string(s->rightsChanges.size()), {}};
            for (std::size_t i = s->rightsChanges.size(); i-- > 0 && b.lines.size() < 3;) {
                const auto& c = s->rightsChanges[i];
                std::string t;
                if (c.joined) t = "ajout\xC3\xA9" + std::string(c.profile.empty() ? "" : ", profil " + c.profile);
                else if (c.left) t = "retir\xC3\xA9";
                else if (!c.profile.empty() && c.added.empty() && c.removed.empty()) t = "profil " + c.profile + " copi\xC3\xA9";
                for (const auto& a : c.added) t += (t.empty() ? "" : ", ") + std::string("+ ") + rightShort(a);
                for (const auto& a : c.removed) t += (t.empty() ? "" : ", ") + std::string(ch::fr::kMinus) + " " + rightShort(a);
                t += ", par " + (c.by == "admin+" ? std::string("toi") : c.by);
                b.lines.push_back({dayMonth(c.line.when), t});
            }
            if (b.lines.empty()) b.lines.push_back({"", "aucun changement"});
            out.push_back(std::move(b));
        }
        {
            const js::Chain& c = s->chain;
            SecBlock b{"La cha\xC3\xAEne de ses lignes", ui::Icon::Link, c.ok() ? std::string("v\xC3\xA9rifi\xC3\xA9" "e \xE2\x9C\x93") : std::string("rompue"), {}};
            b.good = c.ok();
            b.lines.push_back({"", std::to_string(c.lines) + " lignes" + (c.first ? " depuis le " + dayMonth(c.first) : std::string()) + ", sign\xC3\xA9" "es par sa cl\xC3\xA9"});
            b.lines.push_back({"", c.ok() ? std::string("aucune manquante, aucune modifi\xC3\xA9" "e")
                                          : "la premi\xC3\xA8re rompue : n\xC2\xB0 " + std::to_string(c.firstBreak)});
            if (c.last) b.lines.push_back({"la derni\xC3\xA8re", "n\xC2\xB0 " + ch::fr::integer(static_cast<double>(c.last)) + (c.lastHash.empty() ? std::string() : " \xC2\xB7 " + groups4(c.lastHash, 3) + "\xE2\x80\xA6")});
            out.push_back(std::move(b));
        }
        return out;
    }
    [[nodiscard]] float secHeight(float w) const {
        const auto blocks = secBlocks();
        const int cols = w >= 640.f ? 2 : 1;
        float h = 64.f;
        for (std::size_t i = 0; i < blocks.size(); i += static_cast<std::size_t>(cols)) {
            std::size_t most = blocks[i].lines.size();
            if (cols == 2 && i + 1 < blocks.size()) most = std::max(most, blocks[i + 1].lines.size());
            h += 52.f + 22.f * static_cast<float>(most) + 10.f;
        }
        return h + 52.f;
    }
    void paintSecurity(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, secCard_);
        const float x = secCard_.x + 18.f, w = secCard_.w - 36.f;
        ui::drawIcon(r, ui::Icon::Lock, {x, secCard_.y + 19.f, 15.f, 15.f}, kMuted);
        const float tw = text(r, x + 22.f, secCard_.y + 17.f, own_ ? "Ta s\xC3\xA9" "curit\xC3\xA9" : "Sa s\xC3\xA9" "curit\xC3\xA9", gfx::semibold(FontId{15}), kInk, w * 0.4f);
        (void)text(r, x + 30.f + tw, secCard_.y + 19.f, "la p\xC3\xA9riode \xC2\xB7 tout vient du journal sign\xC3\xA9", kF12, kMuted, w - tw - 60.f);
        (void)sourceTag(r, x + w - 18.f, secCard_.y + 17.f, "J");
        if (!d_->journal) {
            (void)kit::drawWrapped(r, x, secCard_.y + 64.f, w, "Le journal des admins n'est pas sur ce poste : il est relev\xC3\xA9 par l'admin+ (Journal des admins).", kF13, kMuted, 18.f, 3);
            return;
        }
        const auto blocks = secBlocks();
        const int cols = w >= 600.f ? 2 : 1;
        const float bw = (secCard_.w - 2.f) / static_cast<float>(cols);
        float y = secCard_.y + 54.f;
        for (std::size_t i = 0; i < blocks.size(); i += static_cast<std::size_t>(cols)) {
            std::size_t most = blocks[i].lines.size();
            if (cols == 2 && i + 1 < blocks.size()) most = std::max(most, blocks[i + 1].lines.size());
            const float bh = 52.f + 22.f * static_cast<float>(most) + 10.f;
            r.fillRect({secCard_.x + 1.f, y, secCard_.w - 2.f, 1.f}, kLine);
            for (int k = 0; k < cols && i + static_cast<std::size_t>(k) < blocks.size(); ++k) {
                const SecBlock& b = blocks[i + static_cast<std::size_t>(k)];
                const float bx = secCard_.x + 1.f + static_cast<float>(k) * bw;
                if (b.good) r.fillRect({bx, y + 1.f, bw - (k + 1 < cols ? 1.f : 0.f), bh - 1.f}, kGreenPale);
                if (k) r.fillRect({bx, y, 1.f, bh}, kLine);
                const float ix = bx + 17.f, iw = bw - 34.f;
                r.fillRoundedRect({ix, y + 12.f, 26.f, 26.f}, kSunken, 7.f);
                ui::drawIcon(r, b.icon, {ix + 6.f, y + 18.f, 14.f, 14.f}, kInk2);
                const float cw = ui::measureWidth(b.count, b.good ? kB13 : gfx::semibold(FontId{16}));
                (void)text(r, ix + 36.f, y + 15.f, b.title, kB14, kInk, iw - 36.f - cw - 8.f);
                r.drawText({ix + iw - cw, y + (b.good ? 17.f : 14.f)}, b.count, b.good ? kB13 : gfx::semibold(FontId{16}), b.good ? kGreen : kInk);
                float ly = y + 48.f;
                for (const auto& [when, what] : b.lines) {
                    const float ww = when.empty() ? 0.f : std::min(96.f, iw * 0.28f);
                    if (!when.empty()) (void)text(r, ix, ly, when, kF12, kFaint, ww - 4.f);
                    (void)text(r, ix + ww, ly, what, kF13, kInk2, iw - ww);
                    ly += 22.f;
                }
            }
            y += bh;
        }
        r.fillRect({secCard_.x + 1.f, y, secCard_.w - 2.f, 1.f}, kLine);
        const AdminFigures* f = d_->fig(key_);
        std::string con = "Ses connexions : ";
        if (f && f->hasUsage && !f->usage.city.empty())
            con += f->usage.city + (f->usage.country.empty() ? "" : ", " + f->usage.country) + " (" + std::to_string(f->usage.days.size()) + " jours)";
        else con += "\xE2\x80\x94 (le service)";
        ui::drawIcon(r, ui::Icon::Globe, {x, y + 15.f, 14.f, 14.f}, kMuted);
        (void)text(r, x + 20.f, y + 14.f, con, kF12, kInk2, w - 20.f - lines_->naturalWidth() - 16.f);
        (void)p;
    }

    // ---- 2. licences et clients ----
    void paintLicences(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, licCard_);
        const js::AdminStats* s = d_->stats(key_);
        const CardFigures* c = cardFigs();
        const float x = licCard_.x + 16.f, w = licCard_.w - 32.f;
        (void)cardTitle(r, x, licCard_.y + 16.f, w, own_ ? "Les licences que tu as sign\xC3\xA9" "es" : "Les licences qu'" + std::string("il a sign\xC3\xA9" "es"),
                        "sur la p\xC3\xA9riode \xC2\xB7 leur \xC3\xA9tat aujourd'hui", "S");
        const float vw = text(r, x, licCard_.y + 62.f, s ? num(s->licences) : num(kNaN), gfx::semibold(FontId{30}), kInk, 110.f);
        if (s) (void)delta(r, x, licCard_.y + 102.f, s->licences, s->licencesBefore, true, kB12, true);
        (void)vw;
        const float bx = x + 120.f, bw = w - 120.f;
        if (c && c->loaded) {
            const double a = known(c->active) ? c->active : 0, e = known(c->expired) ? c->expired : 0, v = known(c->revoked) ? c->revoked : 0;
            if (a + e + v > 0) ch::paintSegments(r, ctx.theme, {bx, licCard_.y + 70.f, bw, 10.f}, {a, e, v}, {kGreen, kFaint, kRed});
            (void)text(r, bx, licCard_.y + 90.f, num(a) + " actives \xC2\xB7 " + num(e) + " expir\xC3\xA9" "es \xC2\xB7 " + num(v) + " r\xC3\xA9voqu\xC3\xA9" "es", kF12, kInk2, bw);
        } else {
            (void)text(r, bx, licCard_.y + 76.f, serviceWord(), kF12, kMuted, bw);
        }
        (void)text(r, x, licCard_.y + 134.f, "Par offre", kB13, kInk, w);
        const float fy = licCard_.bottom() - 62.f;
        r.fillRect({x, fy - 8.f, w, 1.f}, kLine2);
        const float cw = w / 3.f;
        const auto fig = [&](int i, const std::string& label, const std::string& v) {
            (void)text(r, x + static_cast<float>(i) * cw, fy, label, kF12, kMuted, cw - 6.f);
            (void)text(r, x + static_cast<float>(i) * cw, fy + 18.f, v, gfx::semibold(FontId{17}), kInk, cw - 6.f);
        };
        fig(0, "PC activ\xC3\xA9s dessus", c ? num(c->pcs) : num(kNaN));
        fig(1, "Prolongations", c ? num(c->prolonged) : num(kNaN));
        fig(2, "Options ajout\xC3\xA9" "es", c ? num(c->options) : num(kNaN));
        // ses licences semaine apres semaine
        card(r, ctx.theme, weeksCard_);
        const float wx = weeksCard_.x + 16.f, ww = weeksCard_.w - 32.f;
        (void)cardTitle(r, wx, weeksCard_.y + 16.f, ww, own_ ? "Tes licences, semaine apr\xC3\xA8s semaine" : "Ses licences, semaine apr\xC3\xA8s semaine",
                        "12 semaines \xC2\xB7 sa part parmi tous les admins", "S");
        if (c && !c->weekLicences.empty()) {
            std::size_t best = 0;
            for (std::size_t i = 0; i < c->weekLicences.size(); ++i)
                if (c->weekLicences[i] > c->weekLicences[best]) best = i;
            if (c->weekLicences[best] > 0 && best < c->weeks.size()) {
                const std::string t = "sa meilleure : " + num(c->weekLicences[best]) + " (" + statskit::ddmm(c->weeks[best].from) + ")";
                (void)text(r, wx + ww - ui::measureWidth(t, kF12), weeksCard_.y + 64.f, t, kF12, kMuted, ww * 0.5f);
            }
        }
        (void)p;
    }
    void paintRenew(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, renewCard_);
        const float x = renewCard_.x + 16.f, w = renewCard_.w - 32.f;
        const auto list = renewals();
        (void)cardTitle(r, x, renewCard_.y + 16.f, w - 30.f, "\xC3\x80 renouveler", own_ ? "tes licences qui finissent dans les 30 jours" : "ses licences qui finissent dans les 30 jours", "");
        const std::string n = std::to_string(list.size());
        r.fillRoundedRect({x + w - 24.f, renewCard_.y + 16.f, 24.f, 20.f}, kSunken, 6.f);
        r.drawText({x + w - 12.f - ui::measureWidth(n, kB12) * 0.5f, renewCard_.y + 18.f}, n, kB12, kInk2);
        float y = renewCard_.y + 66.f;
        if (!d_->journal) {
            (void)kit::drawWrapped(r, x, y, w, "Ses licences se lisent dans le journal des admins (licence_creer, licence_prolonger) : il n'est pas sur ce poste.", kF13, kMuted, 18.f, 3);
            return;
        }
        if (list.empty()) (void)text(r, x, y, "Aucune ne finit dans les 30 jours.", kF13, kMuted, w);
        for (const auto* e : list) {
            if (y > renewCard_.bottom() - 60.f) break;
            const auto& L = e->licence;
            (void)text(r, x, y + 8.f, L.licenseId, kB13, kInk, w * 0.36f);
            (void)text(r, x + w * 0.38f, y, L.customer, kF13, kInk, w * 0.62f);
            const std::int64_t left = (static_cast<std::int64_t>(L.expiresAt) - static_cast<std::int64_t>(d_->now)) / kDayS;
            const float ew = text(r, x + w * 0.38f, y + 18.f, L.edition + " \xC2\xB7 fin le ", kF12, kMuted, w * 0.4f);
            (void)text(r, x + w * 0.38f + ew, y + 18.f, dayMonth(L.expiresAt), left <= 10 ? kB12 : kF12, left <= 10 ? kAmber : kMuted, w * 0.2f);
            y += 50.f;
            r.fillRect({x, y - 8.f, w, 1.f}, kLine2);
        }
        if (p.kind == Person::Kind::Admin && !p.rights.empty() && std::find(p.rights.begin(), p.rights.end(), "licence_prolonger") == p.rights.end())
            (void)kit::drawWrapped(r, x, renewCard_.bottom() - 34.f, w, "Il n'a plus le droit de prolonger : toi, tu le peux (Licences).", kF12, kMuted, 17.f, 2);
    }
    [[nodiscard]] std::vector<const LicenceEntry*> renewals() const {
        std::set<std::string> ids;
        for (const auto& l : d_->lines)
            if (mine(l) && (l.action == "licence_creer" || l.action == "licence_prolonger" || l.action == "licence_option") && !l.object.empty()) ids.insert(l.object);
        std::vector<const LicenceEntry*> out;
        for (const auto& e : host_.workspace().licences()) {
            if (!ids.count(e.licence.licenseId) || e.revoked || e.licence.expiresAt == 0) continue;
            const Seconds end = e.licence.expiresAt;
            if (end >= d_->now && end <= d_->now + 30 * kDayS) out.push_back(&e);
        }
        std::sort(out.begin(), out.end(), [](const LicenceEntry* a, const LicenceEntry* b) { return a->licence.expiresAt < b->licence.expiresAt; });
        return out;
    }
    void paintClients(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        card(r, ctx.theme, clientsCard_);
        const CardFigures* c = cardFigs();
        const float x = clientsCard_.x + 16.f, w = clientsCard_.w - 32.f;
        (void)cardTitle(r, x, clientsCard_.y + 16.f, w, own_ ? "Tes clients" : "Ses clients", "ceux pour qui " + std::string(own_ ? "tu as" : "il a") + " sign\xC3\xA9 ou livr\xC3\xA9", "S");
        const float fy = clientsCard_.y + 64.f, cw = w / 2.f;
        (void)text(r, x, fy, "Clients servis", kF12, kMuted, cw);
        (void)text(r, x, fy + 16.f, c ? num(c->clients) : num(kNaN), gfx::semibold(FontId{18}), kInk, cw);
        (void)text(r, x + cw, fy, "Ses 5 premiers", kF12, kMuted, cw);
        const float vw = text(r, x + cw, fy + 16.f, c && known(c->topShare) ? ch::fr::percent(c->topShare * 100.0) : num(kNaN), gfx::semibold(FontId{18}), kInk, cw);
        (void)text(r, x + cw + vw + 6.f, fy + 21.f, "de son montant", kF12, kInk2, cw - vw - 6.f);
    }

    // ---- 3. demandes ----
    void paintRequests(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, reqCard_);
        const js::AdminStats* s = d_->stats(key_);
        const AdminFigures* f = d_->fig(key_);
        const float x = reqCard_.x + 16.f, w = reqCard_.w - 32.f;
        (void)cardTitle(r, x, reqCard_.y + 16.f, w, own_ ? "Les demandes que tu as trait\xC3\xA9" "es" : "Les demandes qu'il a trait\xC3\xA9" "es",
                        "en ligne, depuis les Studios \xC2\xB7 le d\xC3\xA9lai entre la demande et sa r\xC3\xA9ponse ; son d\xC3\xA9lai moyen par semaine, et sa part", "S");
        const float figsW = std::min(260.f, w * 0.32f);
        float y = reqCard_.y + 66.f;
        const auto fig = [&](const std::string& label, const std::string& big, const std::string& small, Color bc = kInk) {
            (void)text(r, x, y, label, kF12, kMuted, figsW);
            const float bw = text(r, x, y + 16.f, big, gfx::semibold(FontId{18}), bc, figsW);
            (void)text(r, x + bw + 6.f, y + 21.f, small, kF12, kInk2, figsW - bw - 6.f);
            y += 54.f;
        };
        fig("Trait\xC3\xA9" "es", s ? num(s->requests) : num(kNaN), s ? signedCount(s->requests, s->requestsBefore) : std::string());
        if (f && known(f->accepted)) {
            const double t = f->accepted + (known(f->refused) ? f->refused : 0.0);
            fig("Accept\xC3\xA9" "es \xC2\xB7 refus\xC3\xA9" "es", num(f->accepted) + " \xC2\xB7 " + num(f->refused),
                t > 0 ? ch::fr::percent(100.0 * f->accepted / t) + " accept\xC3\xA9" "es" : std::string());
        } else {
            fig("Accept\xC3\xA9" "es \xC2\xB7 refus\xC3\xA9" "es", num(kNaN), "");
        }
        fig("D\xC3\xA9lai moyen \xC2\xB7 m\xC3\xA9" "dian \xC2\xB7 le plus long", f ? hoursText(f->meanDelay) + " \xC2\xB7 " + hoursText(f->medianDelay) : num(kNaN),
            f ? hoursText(f->maxDelay) : std::string());
        fig("En attente (tous les admins)", num(d_->totals.pending), known(d_->totals.oldest) && d_->totals.oldest > 0 ? "la plus ancienne : " + hoursText(d_->totals.oldest) : std::string(),
            kAmber);
        (void)p;
    }

    // ---- 4. boutique ----
    void paintShop(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, shopCard_);
        const CardFigures* c = cardFigs();
        const AdminFigures* f = d_->fig(key_);
        const float x = shopCard_.x + 16.f, w = shopCard_.w - 32.f;
        (void)cardTitle(r, x, shopCard_.y + 16.f, w, own_ ? "La boutique : les commandes que tu as livr\xC3\xA9" "es" : "La boutique : les commandes qu'il a livr\xC3\xA9" "es",
                        "pay\xC3\xA9" "es en ligne (Stripe), livr\xC3\xA9" "es depuis son Admin \xC2\xB7 le montant par semaine ; par article", "S");
        const float sFigs = std::min(220.f, w * 0.24f);
        float y = shopCard_.y + 66.f;
        const auto fig = [&](const std::string& label, const std::string& big, const std::string& small, double cur = kNaN, double prev = kNaN, bool count = false) {
            (void)text(r, x, y, label, kF12, kMuted, sFigs);
            const float bw = text(r, x, y + 16.f, big, gfx::semibold(FontId{18}), kInk, sFigs);
            float sx = x + bw + 6.f;
            if (known(cur) && known(prev)) sx += delta(r, sx, y + 21.f, cur, prev, true, kB12, count) + 6.f;
            (void)text(r, sx, y + 21.f, small, kF12, kInk2, x + sFigs - sx);
            y += 54.f;
        };
        const double orders = c && known(c->orders) ? c->orders : f ? f->orders : kNaN;
        const double ordersB = c && known(c->ordersBefore) ? c->ordersBefore : f ? f->ordersBefore : kNaN;
        fig("Commandes livr\xC3\xA9" "es", num(orders), "", orders, ordersB, true);
        fig("Montant encaiss\xC3\xA9", f ? euros(f->amount) : num(kNaN), "", f ? f->amount : kNaN, f ? f->amountBefore : kNaN);
        fig("Panier moyen", c ? euros(c->basket) : num(kNaN), c && known(c->basketBefore) ? euros(c->basketBefore) + " avant" : std::string());
        fig("Remboursements", c ? num(c->refundsCount) : num(kNaN), c && known(c->refunds) ? euros(c->refunds) : std::string());
        const float ax = articles_->bounds().x;
        (void)text(r, ax, shopCard_.y + 66.f, "Par article (commandes)", kB13, kInk, articles_->bounds().w);
        (void)p;
    }
    void paintRest(const ui::PaintContext& ctx, const Person& p) const {
        auto& r = ctx.r;
        card(r, ctx.theme, restCard_);
        const js::AdminStats* s = d_->stats(key_);
        const float x = restCard_.x + 16.f, w = restCard_.w - 32.f;
        (void)cardTitle(r, x, restCard_.y + 16.f, w, own_ ? "Le reste de ce que tu fais" : "Le reste de ce qu'il fait", "sur la p\xC3\xA9riode, depuis le journal", "J");
        if (!s) return;
        const auto idx = [](const char* id) {
            for (int t = 0; t < js::kTypes; ++t)
                if (std::string(js::types()[static_cast<std::size_t>(t)].id) == id) return t;
            return -1;
        };
        const auto held = [&](const char* id) {
            if (p.kind == Person::Kind::Plus) return true;
            const int t = idx(id);
            return t >= 0 && t < static_cast<int>(s->rights.size()) && s->rights[static_cast<std::size_t>(t)].held;
        };
        struct Line {
            std::string label, note;
            int         n;
            bool        off;
        };
        const auto count = [&](const char* id) {
            const int t = idx(id);
            return t >= 0 ? s->byType[static_cast<std::size_t>(t)] : 0;
        };
        std::vector<Line> lines{
            {"Codes d'activation revus", s->codeReviews.empty() ? std::string() : "le dernier " + ago(d_->now, s->codeReviews.back().when), count("codes"), !held("codes")},
            {"Courriels envoy\xC3\xA9s aux clients", "", count("courriels"), !held("courriels")},
            {"Exports des clients", "", count("clients_exporter"), !held("clients_exporter")},
            {"Annonces publi\xC3\xA9" "es", "", count("annonces"), !held("annonces")},
            {"Offres et options modifi\xC3\xA9" "es", "", count("offres") + count("options"), !held("offres") && !held("options")},
            {"PC retir\xC3\xA9s d'une licence", "", count("poste_retirer"), !held("poste_retirer")},
        };
        float y = restCard_.y + 66.f;
        for (const auto& l : lines) {
            const float lw = text(r, x, y, l.label, kF13, l.off ? kFaint : kInk, w - 60.f);
            const std::string note = l.off && l.n == 0 ? std::string("pas ce droit") : l.note;
            if (!note.empty()) (void)text(r, x + lw + 6.f, y + 1.f, "\xC2\xB7 " + note, kF12, kMuted, w - lw - 60.f);
            const std::string n = l.off && l.n == 0 ? std::string("\xE2\x80\x94") : std::to_string(l.n);
            r.drawText({x + w - ui::measureWidth(n, kB14), y}, n, kB14, l.off || l.n == 0 ? kFaint : kInk);
            y += 38.f;
            r.fillRect({x, y - 12.f, w, 1.f}, kLine2);
        }
    }

    std::shared_ptr<Data>  d_;
    Actions                a_;
    ShellHost              host_;
    std::string            key_;
    bool                   own_{false};
    int                    anchor_{0};
    float                  anchorRel_[7]{};
    std::vector<double>    weeksActions_;
    std::int64_t           weekStart_{0};
    ch::TimeChart*         perDay_{nullptr};
    ch::BarList*           byType_{nullptr};
    ch::HeatMap*           heat_{nullptr};
    ch::DayStrip*          strip_{nullptr};
    ch::TimeChart*         span_{nullptr};
    ch::BarList*           offers_{nullptr};
    ch::TimeChart*         weeksLic_{nullptr};
    ch::BarList*           clients_{nullptr};
    ch::TimeChart*         delay_{nullptr};
    ch::TimeChart*         weeksReq_{nullptr};
    ch::TimeChart*         weeksAmount_{nullptr};
    ch::BarList*           articles_{nullptr};
    LinkButton*            removeUnused_{nullptr};
    LinkButton*            dayJournal_{nullptr};
    LinkButton*            lines_{nullptr};
    LinkButton*            removeOne_[13]{};
    Rect                   who_{}, tiles_[8]{}, perDayCard_{}, typeCard_{}, unusedBox_{}, critCard_{}, amongCard_{};
    Rect                   heatCard_{}, daysCard_{}, rightsCard_{}, secCard_{};
    Rect                   licCard_{}, weeksCard_{}, renewCard_{}, clientsCard_{}, reqCard_{}, shopCard_{}, restCard_{};
    float                  typeNoteY_{0}, rightRow_[13]{};
    core::ConnectionScope  links_;
};

} // namespace

std::unique_ptr<ScrolledView> makeCard(std::shared_ptr<Data> data, Actions actions, ShellHost host, std::string key, bool own) {
    return std::make_unique<CardView>(std::move(data), std::move(actions), std::move(host), std::move(key), own);
}
void cardAnchor(ScrolledView& v, int anchor) {
    if (auto* c = dynamic_cast<CardView*>(&v)) c->anchor(anchor);
}
int cardCurrentAnchor(const ScrolledView& v) {
    if (const auto* c = dynamic_cast<const CardView*>(&v)) return c->currentAnchor();
    return 0;
}

} // namespace license_admin::gui::adminsboard
