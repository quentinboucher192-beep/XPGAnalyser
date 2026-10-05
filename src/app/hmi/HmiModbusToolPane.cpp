// app/hmi/HmiModbusToolPane.cpp - IHM > Outil Modbus (lot 15).
#include "HmiModbusToolPane.hpp"
#include "../ExportTarget.hpp"

#include "HmiCyclicPage.hpp"
#include "HmiEquipmentHost.hpp"
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiTwin.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <thread>
#include <utility>

namespace app {

using namespace hmikit;
using PG = ui::PropertyGrid;
namespace eq = hmi::equip;

namespace {

enum : int {
    ARead = 1, AWrite, AIdentify, AStart, AStop, ASend, ABuild, ASpyStart, ASpyStop, APing, APingStop, AClear, AExport,
    // 1.9 : la lecture cyclique a plusieurs requetes
    AFreeze, AAddMenu, ADuplicate, ARemove, ASets, ARecord,
};

const char* const kPlcTarget = "automate du projet";
const char* const kTyped = "adresse tap\xC3\xA9" "e";
const char* const kTwinSuffix = " \xC2\xB7 esclave simul\xC3\xA9";   // 1.9 : l'esclave simule lie (le jumeau du lot 17)
const char* const kOldTwinSuffix = " (jumeau)";                          // avant la 1.9 (sessions, scripts)

double wallNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string clockOf(double epoch, bool millis = false) {
    const auto t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    if (millis)
        std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(std::fmod(epoch, 1.0) * 1000));
    else
        std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string msText(double ms) {
    if (ms >= 0 && ms < 0.1) return "< 0.1 ms";
    char b[32];
    std::snprintf(b, sizeof b, ms < 10 ? "%.1f ms" : "%.0f ms", ms);
    return b;
}

const std::vector<std::string>& functionLabels() {
    static const std::vector<std::string> k{
        "1 - lire des bits (bobines)", "2 - lire des entr\xC3\xA9" "es TOR", "3 - lire des mots (maintien)", "4 - lire des mots d'entr\xC3\xA9" "e",
        "5 - \xC3\xA9" "crire un bit", "6 - \xC3\xA9" "crire un mot", "15 - \xC3\xA9" "crire des bits", "16 - \xC3\xA9" "crire des mots",
        "43 - identification"};
    return k;
}

std::string functionLabel(int f) {
    for (const auto& l : functionLabels())
        if (std::atoi(l.c_str()) == f) return l;
    return std::to_string(f);
}

bool bitFunction(int f) { return f == 1 || f == 2 || f == 5 || f == 15; }

// La colonne Modicon d'une adresse (a partir de 0) pour cette fonction.
std::string modicon(int function, int address) {
    const int n = address + 1;
    char b[32];
    switch (function) {
        case 1: case 5: case 15: std::snprintf(b, sizeof b, "%05d", n); break;
        case 2: std::snprintf(b, sizeof b, "1%04d", n); break;
        case 4: std::snprintf(b, sizeof b, "3%04d", n); break;
        default: std::snprintf(b, sizeof b, n > 9999 ? "4%05d" : "4%04d", n); break;
    }
    return b;
}

gfx::Color seriesColor(std::size_t i) {
    static const std::uint32_t k[] = {0x3B82F6, 0x22C55E, 0xF59E0B, 0xEC4899, 0x8B5CF6, 0x14B8A6, 0xEF4444, 0x64748B};
    return gfx::Color::rgb(k[i % std::size(k)]);
}

ui::Tone toneOf(int t) { return t == 1 ? ui::Tone::Ok : t == 2 ? ui::Tone::Warning : t == 3 ? ui::Tone::Error : t == 4 ? ui::Tone::Accent : ui::Tone::None; }

// Une page : un bandeau (le graphique, le champ de la trame) au-dessus d'un tableau.
class StackPage final : public ui::Widget {
public:
    StackPage(std::string id, ui::WidgetPtr top, float topShare, ui::WidgetPtr bottom)
        : ui::Widget(std::move(id)), share_(topShare) {
        top_ = &addChild(std::move(top));
        bottom_ = &addChild(std::move(bottom));
    }

protected:
    void onLayout() override {
        const auto b = bounds();
        const float h = share_ > 1.f ? share_ : std::floor(b.h * share_);
        top_->setBounds({b.x + (share_ > 1.f ? 8.f : 0.f), b.y + (share_ > 1.f ? 8.f : 0.f), b.w - (share_ > 1.f ? 16.f : 0.f), h - (share_ > 1.f ? 12.f : 0.f)});
        bottom_->setBounds({b.x, b.y + h, b.w, std::max(0.f, b.h - h)});
    }
    void onPaint(const ui::PaintContext& ctx) override { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

private:
    ui::Widget* top_{nullptr};
    ui::Widget* bottom_{nullptr};
    float       share_;
};

// 1.9 : a droite de la barre d'etat de l'outil, en violet, la fiole et "R4 lit un
// esclave simule" (maquette M6). Un indicateur a part : le message de gauche
// (l'etat de la lecture cyclique, ou un message passager comme "5 requetes en
// marche") ne le cache plus. Vide : il ne prend pas de place.
class SlaveReadNote final : public ui::Widget {
public:
    explicit SlaveReadNote(std::string id) : ui::Widget(std::move(id)) {}
    void setText(std::string text) {
        if (text == text_) return;
        text_ = std::move(text);
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {text_.empty() ? 0.f : ui::measureWidth(text_, kStatus) + 22.f, 20.f};
        h.minimum = h.preferred;
        return h;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        if (text_.empty()) return;
        const auto b = bounds();
        const gfx::Color violet = simViolet();
        drawCyclicGlyph(ctx.r, GFlask, {b.x, b.y + (b.h - 14.f) * 0.5f, 14.f, 14.f}, violet);
        ctx.r.drawText({b.x + 20.f, b.y + (b.h - ctx.r.lineHeight(kStatus)) * 0.5f}, text_, kStatus, violet);
    }
private:
    static constexpr gfx::FontId kStatus{13};      // Theme::font.smallUi, comme le message de la barre
    std::string text_;
};

} // namespace

// ================================================================ graphique ===
HmiSeriesChart::HmiSeriesChart(std::string id) : ui::Widget(std::move(id)) {}

void HmiSeriesChart::setData(std::vector<Series> series, double from, double to, std::string empty) {
    series_ = std::move(series);
    from_ = from;
    to_ = to;
    empty_ = std::move(empty);
    invalidate();
}

void HmiSeriesChart::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& t = ctx.theme;
    const auto b = bounds();
    const gfx::FontId small{12};
    r.fillRect(b, t.color.panelBg);
    const gfx::Rect plot{b.x + 64, b.y + 30, std::max(10.f, b.w - 84), std::max(10.f, b.h - 60)};
    r.fillRect(plot, t.color.inputBg);
    r.strokeRect(plot, t.color.border, 1.f);
    double lo = 0, hi = 0;
    bool any = false;
    for (const auto& s : series_)
        for (const auto& [x, y] : s.points) {
            if (!any) lo = hi = y;
            lo = std::min(lo, y);
            hi = std::max(hi, y);
            any = true;
        }
    if (!any) {
        const std::string msg = empty_.empty() ? std::string("D\xC3\xA9marrer : la lecture cyclique trace ici ses valeurs") : empty_;
        r.drawText({plot.x + (plot.w - r.measure(msg, gfx::FontId{14}).width) / 2, plot.y + plot.h / 2 - 8}, msg, gfx::FontId{14}, t.color.textMuted);
        return;
    }
    if (hi - lo < 1e-9) {
        hi += 1;
        lo -= 1;
    }
    const double pad = (hi - lo) * 0.08;
    lo -= pad;
    hi += pad;
    const double span = std::max(1e-6, to_ - from_);
    // La grille et les graduations.
    for (int i = 0; i <= 4; ++i) {
        const float y = plot.y + plot.h * static_cast<float>(i) / 4.f;
        r.line({plot.x, y}, {plot.x + plot.w, y}, t.color.gridLine, 1.f);
        char label[32];
        const double v = hi - (hi - lo) * i / 4.0;
        std::snprintf(label, sizeof label, std::fabs(v) >= 1000 ? "%.0f" : "%.4g", v);
        r.drawText({plot.x - r.measure(label, small).width - 6, y - 7}, label, small, t.color.textMuted);
        const float x = plot.x + plot.w * static_cast<float>(i) / 4.f;
        r.line({x, plot.y}, {x, plot.y + plot.h}, t.color.gridLine, 1.f);
        const std::string tl = clockOf(from_ + span * i / 4.0);
        r.drawText({x - r.measure(tl, small).width / 2, plot.y + plot.h + 6}, tl, small, t.color.textMuted);
    }
    r.pushClip(plot);
    // Lot 18 : la zone de mouvement d'une case animee du jumeau (en clair).
    for (std::size_t k = 0; k < series_.size(); ++k) {
        const auto& s = series_[k];
        if (!s.band) continue;
        const float ya = plot.y + static_cast<float>((hi - s.band->second) / (hi - lo)) * plot.h;
        const float yb = plot.y + static_cast<float>((hi - s.band->first) / (hi - lo)) * plot.h;
        gfx::Color c = seriesColor(k);
        c.a = 40;
        r.fillRect({plot.x, ya, plot.w, std::max(1.f, yb - ya)}, c);
    }
    for (std::size_t k = 0; k < series_.size(); ++k) {
        const auto& s = series_[k];
        const gfx::Color c = s.forced ? gfx::Color{236, 132, 38, 255} : seriesColor(k);
        gfx::Point prev{};
        bool has = false;
        for (const auto& [x, y] : s.points) {
            const gfx::Point p{plot.x + static_cast<float>((x - from_) / span) * plot.w, plot.y + static_cast<float>((hi - y) / (hi - lo)) * plot.h};
            if (has) r.line(prev, p, c, 2.f);
            prev = p;
            has = true;
        }
    }
    r.popClip();
    // La legende : le nom et la derniere valeur.
    float lx = plot.x;
    for (std::size_t k = 0; k < series_.size(); ++k) {
        const auto& s = series_[k];
        char last[48] = "";
        if (!s.points.empty()) std::snprintf(last, sizeof last, " = %.6g", s.points.back().second);
        const std::string text = (s.forced ? "F " : s.animated ? "~ " : "") + s.name + last;
        r.fillRoundedRect({lx, b.y + 10, 12, 4}, s.forced ? gfx::Color{236, 132, 38, 255} : seriesColor(k), 2);
        r.drawText({lx + 16, b.y + 4}, text, small, t.color.text);
        lx += 16 + r.measure(text, small).width + 18;
        if (lx > b.x + b.w - 120) break;
    }
}

// ================================================================== le volet ===
HmiModbusToolPane::HmiModbusToolPane(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    session_ = std::make_unique<mbtool::Session>();
    journal_ = std::make_shared<spy::Journal>();
    pingStop_ = std::make_shared<std::atomic<bool>>(false);
    pingMutex_ = std::make_shared<std::mutex>();
    pingInbox_ = std::make_shared<std::vector<PingRow>>();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ARead, HmiGlyph::Import, "Lire : la requete, une fois, vers la cible (\xC3\xA0 droite)", "Lire");
    tools->add(AWrite, HmiGlyph::Export, "\xC3\x89" "crire : la valeur (\xC3\xA0 droite) \xC3\xA0 l'adresse choisie (fonction 5, 6, 15 ou 16)", "\xC3\x89" "crire");
    tools->add(AIdentify, HmiGlyph::Search, "Identification : fabricant, r\xC3\xA9" "f\xC3\xA9rence, version de l'\xC3\xA9quipement (fonction 43)", "Identification");
    tools->add(AStart, HmiGlyph::Play, "D\xC3\xA9marrer la lecture cyclique (toutes les requ\xC3\xAAtes actives)", "D\xC3\xA9marrer");
    tools->add(AStop, HmiGlyph::Stop, "Arr\xC3\xAAter la lecture cyclique", "Arr\xC3\xAAter");
    // 1.9 : la lecture cyclique a plusieurs requetes.
    tools->add(AFreeze, HmiGlyph::Pause, "Figer l'affichage : la lecture continue", "Figer");
    tools->add(AAddMenu, HmiGlyph::Plus, "Ajouter des requ\xC3\xAAtes : nouvelle, depuis des variables, depuis les zones m\xC3\xA9moire, coller depuis Excel\xE2\x80\xA6",
               "Requ\xC3\xAAte \xE2\x96\xBE");
    tools->add(ADuplicate, HmiGlyph::Duplicate, "Dupliquer la requ\xC3\xAAte choisie", "Dupliquer");
    tools->add(ARemove, HmiGlyph::Delete, "Retirer la requ\xC3\xAAte choisie (Suppr ; Ctrl+Z la remet)", "Retirer");
    tools->add(ASets, HmiGlyph::Template, "Les jeux de lecture du projet : ouvrir, enregistrer (Ctrl+S), exporter\xE2\x80\xA6", "Jeu : Sans nom \xE2\x96\xBE");
    tools->add(ASend, HmiGlyph::Export, "Envoyer la trame tap\xC3\xA9" "e (en hexad\xC3\xA9" "cimal, l'en-t\xC3\xAAte MBAP compris)", "Envoyer la trame");
    tools->add(ABuild, HmiGlyph::Code, "La trame de la requete de droite, pr\xC3\xAAte \xC3\xA0 envoyer (et \xC3\xA0 modifier)", "Trame de la requ\xC3\xAAte");
    tools->add(ASpyStart, HmiGlyph::Play, "D\xC3\xA9marrer l'espion (le mode choisi \xC3\xA0 droite : trace, relais, capture)", "D\xC3\xA9marrer l'espion");
    tools->add(ASpyStop, HmiGlyph::Stop, "Arr\xC3\xAAter l'espion", "Arr\xC3\xAAter l'espion");
    tools->add(APing, HmiGlyph::Play, "Ping : l'adresse de droite, N fois (0 : sans fin)", "Ping");
    tools->add(APingStop, HmiGlyph::Stop, "Arr\xC3\xAAter le ping", "Arr\xC3\xAAter le ping");
    tools->separator();
    tools->add(AClear, HmiGlyph::Delete, "Effacer ce que montre l'onglet", "Effacer");
    tools->add(AExport, HmiGlyph::Export, "Exporter en CSV (dossier exports/) ce que montre l'onglet", "Exporter CSV");
    // Le point de "* Enregistrer" (la maquette) : l'icone ; le texte ne le redit pas.
    tools->add(ARecord, HmiGlyph::Indicator, "Enregistrer en continu sur le disque, tant que la lecture tourne (exports/modbus/<date>_<jeu>.csv)",
               "Enregistrer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto on = [this](std::initializer_list<int> tabs) {
        const std::vector<int> list(tabs);
        return [this, list] { return std::find(list.begin(), list.end(), static_cast<int>(tabs_ ? tabs_->currentIndex() : 0)) != list.end(); };
    };
    tools_->setVisibleWhen(ARead, on({TRead}));
    tools_->setVisibleWhen(AWrite, on({TRead}));
    tools_->setVisibleWhen(AIdentify, on({TRead}));
    tools_->setVisibleWhen(AStart, on({TCyclic}));
    tools_->setVisibleWhen(AStop, on({TCyclic}));
    tools_->setVisibleWhen(ASend, on({TFrames}));
    tools_->setVisibleWhen(ABuild, on({TFrames}));
    tools_->setVisibleWhen(ASpyStart, on({TSpy}));
    tools_->setVisibleWhen(ASpyStop, on({TSpy}));
    tools_->setVisibleWhen(APing, on({TPing}));
    tools_->setVisibleWhen(APingStop, on({TPing}));
    tools_->setEnabledWhen(ARead, [this] { return !busy(); });
    tools_->setEnabledWhen(AWrite, [this] { return !busy(); });
    tools_->setEnabledWhen(AIdentify, [this] { return !busy(); });
    tools_->setEnabledWhen(ASend, [this] { return !busy(); });
    tools_->setEnabledWhen(AStart, [this] { return !cyclicTab_->running(); });
    tools_->setEnabledWhen(AStop, [this] { return cyclicTab_->running(); });
    for (const int a : {AFreeze, AAddMenu, ADuplicate, ARemove, ASets, ARecord}) tools_->setVisibleWhen(a, on({TCyclic}));
    tools_->setEnabledWhen(ADuplicate, [this] { return cyclicTab_->selected() != 0; });
    tools_->setEnabledWhen(ARemove, [this] { return cyclicTab_->selected() != 0; });
    tools_->setCheckedWhen(AFreeze, [this] { return cyclicTab_->frozen(); });
    tools_->setCheckedWhen(ARecord, [this] { return cyclicTab_->recording(); });
    tools_->setEnabledWhen(ASpyStart, [this] { return !spyRunning_; });
    tools_->setEnabledWhen(ASpyStop, [this] { return spyRunning_; });
    tools_->setEnabledWhen(APing, [this] { return !(pingTask_.valid() && pingTask_.wait_for(std::chrono::seconds(0)) != std::future_status::ready); });
    tools_->setEnabledWhen(APingStop, [this] { return pingTask_.valid() && pingTask_.wait_for(std::chrono::seconds(0)) != std::future_status::ready; });

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto values = std::make_unique<ui::TableView>(base + ".values");
    values->setColumns({{"Adresse", 90.f}, {"Modicon", 90.f}, {"Valeur", 200.f}, {"Hexa", 110.f}, {"Binaire", 190.f}, {"Esclave simul\xC3\xA9", 150.f}});
    values_ = values.get();
    // 1.9 : la lecture cyclique a plusieurs requetes (HmiCyclicPage.hpp).
    auto cyclicPage = std::make_unique<HmiCyclicPage>(base + ".cyclic", *this);
    cyclicTab_ = cyclicPage.get();
    cyclicPage_ = cyclicPage.get();
    auto field = std::make_unique<ui::InputText>(base + ".frame");
    frameText_ = field.get();
    auto frames = std::make_unique<ui::TableView>(base + ".frames");
    frames->setColumns({{"Heure", 110.f}, {"Sens", 90.f}, {"Trame (hexa)", 330.f}, {"En clair", 520.f}, {"Temps", 80.f}});
    frames_ = frames.get();
    auto framesPage = std::make_unique<StackPage>(base + ".framesPage", std::move(field), 48.f, std::move(frames));
    framesPage_ = framesPage.get();
    auto spyTable = std::make_unique<ui::TableView>(base + ".spy");
    spyTable->setColumns({{"N\xC2\xB0", 60.f}, {"Heure", 110.f}, {"Source", 84.f}, {"De", 150.f}, {"Vers", 150.f}, {"En clair", 470.f},
                          {"R\xC3\xA9ponse", 80.f}, {"Hexa", 330.f}});
    spy_ = spyTable.get();
    auto pings = std::make_unique<ui::TableView>(base + ".pings");
    pings->setColumns({{"Heure", 100.f}, {"Adresse", 170.f}, {"R\xC3\xA9sultat", 420.f}, {"M\xC3\xA9thode", 110.f}});
    pings_ = pings.get();
    tabs->addTab({"Lecture / \xC3\xA9" "criture", ui::Icon::Variable}, std::move(values));
    tabs->addTab({"Lecture cyclique", ui::Icon::Chart}, std::move(cyclicPage));
    tabs->addTab({"Trames", ui::Icon::Code}, std::move(framesPage));
    tabs->addTab({"Espion", ui::Icon::Search}, std::move(spyTable));
    tabs->addTab({"Ping", ui::Icon::Network}, std::move(pings));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    side_ = &static_cast<HmiCyclicSide&>(addChild(std::make_unique<HmiCyclicSide>(base + ".side")));
    links_ += side_->resumeButton().clicked->connect([this] { (void)cyclicTab_->resume(cyclicTab_->selected()); });
    const auto openMap = [this] {
        const auto* r = cyclicTab_->row(cyclicTab_->selected());
        if (r && !r->equipment.empty() && hosts_.openMemoryMap) hosts_.openMemoryMap(r->equipment);
    };
    links_ += side_->mapButton().clicked->connect(openMap);
    links_ += side_->detectButton().clicked->connect(openMap);
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    status_->addIndicator(std::make_unique<SlaveReadNote>(base + ".status.esclave"), ui::StatusBar::Slot::Right);
    frameText_->setText("00 01 00 00 00 06 01 03 00 00 00 0A");

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case ARead: (void)read(); break;
            case AWrite: (void)write(); break;
            case AIdentify: (void)identify(); break;
            case AStart: (void)startCyclic(); break;
            case AStop: stopCyclic(); break;
            case ASend: (void)sendFrame(frameText_->text()); break;
            case ABuild: {
                const auto frame = mbtool::buildFrame(query_, target_.unit, 1);
                frameText_->setText(mbtool::hex(frame));
                say("La trame de la requ\xC3\xAAte : " + mbtool::describe(frame, true));
                break;
            }
            case ASpyStart: (void)startSpy(); break;
            case ASpyStop: stopSpy(); break;
            case APing: (void)ping(); break;
            case APingStop: stopPing(); break;
            case AClear: clear(); break;
            case AFreeze: cyclicTab_->setFrozen(!cyclicTab_->frozen()); break;
            case AAddMenu: {
                const auto r = tools_->rectOf(AAddMenu);
                cyclicTab_->openAddMenu({r.x, r.y + r.h + 2});
                break;
            }
            case ADuplicate: (void)cyclicTab_->duplicate(cyclicTab_->selected()); break;
            case ARemove: (void)cyclicTab_->remove(cyclicTab_->selected()); break;
            case ASets: {
                const auto r = tools_->rectOf(ASets);
                cyclicTab_->openSetsMenu({r.x, r.y + r.h + 2});
                break;
            }
            case ARecord: (void)cyclicTab_->setRecording(!cyclicTab_->recording()); break;
            // Lot 7 : ou exporter (ExportTarget.hpp) - exports/ par defaut, le bouton ... ailleurs.
            case AExport:
                if (static_cast<int>(tabs_->currentIndex()) == TCyclic) {       // 1.9 : la boite d'export de la lecture cyclique
                    cyclicTab_->openExportDialog();
                    break;
                }
                if (!askExportTarget("ce que montre l'outil Modbus (CSV)", "Fichiers CSV|*.csv", [this] { (void)exportCsv(); })) (void)exportCsv();
                break;
            default: break;
        }
    });
    links_ += tabs_->currentChanged->connect([this](std::size_t) { rebuildProperties(); });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    rebuildProperties();
    refreshValues();
    refreshFrames();
    refreshSpy();
    refreshPings();
    status_->setMessage("Outil Modbus : une cible \xC3\xA0 droite (un \xC3\xA9quipement du projet, ou une adresse), puis Lire.", ui::StatusBar::Severity::Info);
}

HmiModbusToolPane::~HmiModbusToolPane() {
    poller_.stop();
    if (cyclicTab_) cyclicTab_->stop();
    if (pingStop_) pingStop_->store(true);
    if (pingTask_.valid()) pingTask_.wait();
    if (pending_.valid()) pending_.wait();
    if (spyRunning_ && spyMode_ == "trace") spy::traceTo(nullptr);
    if (relay_) relay_->stop();
    if (capture_) capture_->stop();
}

void HmiModbusToolPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    rebuildProperties();
}

void HmiModbusToolPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiModbusToolPane::busy() const { return pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready; }

bool HmiModbusToolPane::wait(int ms) {
    if (pending_.valid() && pending_.wait_for(std::chrono::milliseconds(ms)) != std::future_status::ready) return false;
    collect();
    return true;
}

void HmiModbusToolPane::setTarget(const std::string& host, int port, int unit) {
    target_.host = host;
    target_.port = port > 0 ? port : 502;
    target_.unit = unit;
    equipment_.clear();
    // Une adresse d'un equipement du projet : il est choisi par son nom.
    if (const auto* p = hosts_.project ? hosts_.project() : nullptr) {
        for (const auto& e : p->equipments)
            if (e.host == host && e.port == target_.port) equipment_ = e.name;
        if (p->comm.modbus() && p->comm.host == host && p->comm.port == target_.port) equipment_ = kPlcTarget;
        auto* h = hosts_.equipments ? hosts_.equipments() : nullptr;
        // Lot 17 : le jumeau d'un equipement (son serveur local) ; seulement simule : son nom.
        for (const auto& e : p->equipments)
            if (h && e.hasTwin() && host == "127.0.0.1" && h->simulatedPort(e.name) == target_.port)
                equipment_ = e.simulated ? e.name : e.name + kTwinSuffix;
    }
    if (pingHost_.empty() || pingHost_ != host) pingHost_ = host;
    rebuildProperties();
    say("Cible : " + target_.host + ":" + std::to_string(target_.port) + ", esclave " + std::to_string(target_.unit)
        + (equipment_.empty() ? std::string{} : " (" + equipment_ + ")"));
}

bool HmiModbusToolPane::chooseEquipment(const std::string& name, std::string* why) {
    const auto* p = hosts_.project ? hosts_.project() : nullptr;
    if (!p) return false;
    if (name == kPlcTarget) {
        setTarget(p->comm.host, p->comm.port, p->comm.unit);
        return true;
    }
    if (name == kTyped) {
        equipment_.clear();
        rebuildProperties();
        return true;
    }
    // Lot 17 : "Nom - esclave simule" (avant la 1.9 : "Nom (jumeau)") - son esclave virtuel.
    std::string base = name;
    bool twin = false;
    for (const std::string& suffix : {std::string(kTwinSuffix), std::string(kOldTwinSuffix)}) {
        if (twin || base.size() <= suffix.size() || base.compare(base.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        base = base.substr(0, base.size() - suffix.size());
        twin = true;
    }
    const auto* e = p->equipmentByName(base);
    if (!e) {
        if (why) *why = "pas d'\xC3\xA9quipement " + name;
        say("pas d'\xC3\xA9quipement " + name, true);
        return false;
    }
    auto* h = hosts_.equipments ? hosts_.equipments() : nullptr;
    twin = twin || e->simulated;
    if (twin && e->modbus() && h && h->simulatedPort(e->name) > 0) setTarget("127.0.0.1", h->simulatedPort(e->name), e->unit);
    else if (twin) {
        const std::string m = "l'esclave simul\xC3\xA9 de " + e->name + " n'est pas en marche";
        if (why) *why = m;
        say(m, true);
        return false;
    } else {
        setTarget(e->host, e->port, e->unit);
    }
    target_.timeoutMs = e->timeoutMs;
    lowFirst_ = e->wordOrder != "fort";
    equipment_ = twin && !e->simulated ? e->name + kTwinSuffix : e->name;
    rebuildProperties();
    return true;
}

void HmiModbusToolPane::showTab(const std::string& tab) {
    static const std::vector<std::string> keys{"lecture", "cyclique", "trames", "espion", "ping"};
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == tab) tabs_->setCurrentIndex(i);
    if (tab == "ping" && !target_.host.empty()) {
        pingHost_ = target_.host;
        rebuildProperties();
    }
}

mbtool::Format HmiModbusToolPane::format() const { return mbtool::formatFrom(formatLabel_); }

std::vector<std::string> HmiModbusToolPane::targetChoices() const {
    std::vector<std::string> out{kTyped};
    if (const auto* p = hosts_.project ? hosts_.project() : nullptr) {
        if (p->comm.modbus()) out.push_back(kPlcTarget);
        for (const auto& e : p->equipments) {
            if (!e.modbus()) continue;
            out.push_back(e.name);
            if (e.hasTwin() && !e.simulated) out.push_back(e.name + kTwinSuffix);    // lot 17 : son jumeau
        }
    }
    return out;
}

bool HmiModbusToolPane::setField(const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string v = trimmed(raw);
    const auto number = [&](long long lo, long long hi, int& out, const char* label) {
        char* end = nullptr;
        const long long n = std::strtoll(v.c_str(), &end, 10);
        if (v.empty() || (end && *end) || n < lo || n > hi) return fail(std::string(label) + " : un nombre de " + std::to_string(lo) + " \xC3\xA0 " + std::to_string(hi));
        out = static_cast<int>(n);
        return true;
    };
    bool targetChanged = false;
    if (key == "equipement") {
        return chooseEquipment(v, why);
    } else if (key == "hote") {
        if (v.empty() || v.find(' ') != std::string::npos) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : une adresse IP ou un nom");
        target_.host = v;
        equipment_.clear();
        targetChanged = true;
    } else if (key == "port") {
        if (!number(1, 65535, target_.port, "Port")) return false;
        targetChanged = true;
    } else if (key == "esclave") {
        if (!number(0, 255, target_.unit, "Esclave")) return false;
        targetChanged = true;
    } else if (key == "delai") {
        if (!number(50, 60000, target_.timeoutMs, "D\xC3\xA9lai")) return false;
        targetChanged = true;
    } else if (key == "fonction") {
        const int f = std::atoi(v.c_str());
        if (f != 1 && f != 2 && f != 3 && f != 4 && f != 5 && f != 6 && f != 15 && f != 16 && f != 43) return fail("fonction : 1 \xC3\xA0 6, 15, 16 ou 43");
        query_.function = f;
    } else if (key == "adresse") {
        // 0-based ; ou une adresse Modicon / Schneider ("40101", "%MW100").
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (!v.empty() && end && !*end && n >= 0 && n <= 65535 && v.size() < 5) {
            query_.address = static_cast<int>(n);
        } else {
            hmi::comm::Point pt;
            std::string reason;
            if (!eq::placeEquipmentAddress(v, sim::Type::Int, pt, &reason)) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : " + reason);
            query_.address = pt.offset;
            if (pt.area == hmi::comm::Area::InputRegisters) query_.function = 4;
            else if (pt.area == hmi::comm::Area::Coils) query_.function = 1;
            else if (pt.area == hmi::comm::Area::DiscreteInputs) query_.function = 2;
            else if (!(query_.function == 3 || query_.function == 6 || query_.function == 16)) query_.function = 3;
        }
    } else if (key == "nombre") {
        if (!number(1, bitFunction(query_.function) ? 2000 : 125, query_.count, "Nombre")) return false;
    } else if (key == "format") {
        const auto& labels = mbtool::formatLabels();
        if (std::find(labels.begin(), labels.end(), v) == labels.end()) return fail("format inconnu : " + v);
        formatLabel_ = v;
        refreshValues();
    } else if (key == "ordre") {
        lowFirst_ = lower(v).find("fort") == std::string::npos;
        refreshValues();
    } else if (key == "valeur") {
        writeText_ = v;
    } else if (key == "periode") {
        if (!number(20, 600000, periodMs_, "P\xC3\xA9riode")) return false;
    } else if (key == "fenetre") {
        if (!number(10, 900, windowS_, "Fen\xC3\xAAtre")) return false;
    } else if (key == "mode") {
        const std::string l = lower(v);
        spyMode_ = l.find("relais") != std::string::npos ? "relais" : l.find("capture") != std::string::npos ? "capture" : "trace";
    } else if (key == "interface") {
        spyInterface_ = v.substr(0, v.find(" ("));
    } else if (key == "port_modbus") {
        if (!number(1, 65535, spyPort_, "Port Modbus")) return false;
    } else if (key == "promiscuite") {
        promiscuous_ = yes(v);
    } else if (key == "ecoute") {
        if (!number(1, 65535, relayPort_, "Port d'\xC3\xA9" "coute")) return false;
    } else if (key == "ping_hote") {
        if (v.empty() || v.find(' ') != std::string::npos) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : une adresse IP ou un nom");
        pingHost_ = v;
    } else if (key == "ping_port") {
        if (!number(0, 65535, pingPort_, "Port TCP")) return false;
    } else if (key == "ping_nombre") {
        if (!number(0, 100000, pingCount_, "Nombre")) return false;
    } else {
        return fail("r\xC3\xA9glage inconnu : " + key);
    }
    if (targetChanged && session_ && !busy()) session_->close();
    rebuildProperties();
    return true;
}

bool HmiModbusToolPane::launch(const mbtool::Query& q, const std::string& label, std::string* why) {
    if (busy()) {
        if (why) *why = "une requ\xC3\xAAte est en cours";
        return false;
    }
    std::string reason;
    if (q.function != 43 && !mbtool::validQuery(q, &reason)) {
        say(label + " : " + reason, true);
        if (why) *why = reason;
        return false;
    }
    if (!(session_->target() == target_)) session_->setTarget(target_);
    pendingQuery_ = q;
    pendingLabel_ = label;
    pendingRaw_ = false;
    auto* s = session_.get();
    pending_ = std::async(std::launch::async, [s, q] { return s->run(q); });
    say(label + " : " + target_.host + ":" + std::to_string(target_.port) + "\xE2\x80\xA6");
    return true;
}

bool HmiModbusToolPane::read(std::string* why) {
    mbtool::Query q = query_;
    if (q.function == 5 || q.function == 15) q.function = 1;
    if (q.function == 6 || q.function == 16) q.function = 3;
    if (q.function == 43) return identify(why);
    tabs_->setCurrentIndex(TRead);
    return launch(q, "Lire (" + functionLabel(q.function) + ")", why);
}

bool HmiModbusToolPane::write(std::string* why) {
    mbtool::Query q = query_;
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    // Les valeurs tapees : "12", "12; 13", "3.5" (flottant), "1 0 1" (bits).
    std::vector<std::string> items;
    std::string cur;
    for (const char c : writeText_ + ";") {
        if (c == ';' || c == ',' || (c == ' ' && bitFunction(q.function))) {
            if (!trimmed(cur).empty()) items.push_back(trimmed(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (items.empty()) return fail("\xC3\x89" "crire : tapez une valeur (\xC3\xA0 droite, Valeur \xC3\xA0 \xC3\xA9" "crire)");
    if (bitFunction(q.function) || q.function == 1 || q.function == 2) {
        q.bits.clear();
        for (const auto& it : items) q.bits.push_back(yes(it) || it == "1");
        q.function = q.bits.size() == 1 ? 5 : 15;
    } else {
        q.registers.clear();
        for (const auto& it : items) {
            std::vector<std::uint16_t> regs;
            std::string reason;
            if (!mbtool::parseValue(it, format(), lowFirst_, regs, &reason)) return fail("valeur \xC2\xAB " + it + " \xC2\xBB : " + reason);
            q.registers.insert(q.registers.end(), regs.begin(), regs.end());
        }
        q.function = q.registers.size() == 1 ? 6 : 16;
    }
    q.count = static_cast<int>(bitFunction(q.function) ? q.bits.size() : q.registers.size());
    tabs_->setCurrentIndex(TRead);
    return launch(q, "\xC3\x89" "crire (" + functionLabel(q.function) + ")", why);
}

bool HmiModbusToolPane::identify(std::string* why) {
    mbtool::Query q;
    q.function = 43;
    tabs_->setCurrentIndex(TRead);
    return launch(q, "Identification (43)", why);
}

bool HmiModbusToolPane::sendFrame(const std::string& hexText, std::string* why) {
    std::vector<std::uint8_t> frame;
    std::string reason;
    if (!mbtool::parseHex(hexText, frame, &reason)) {
        say("Trame : " + reason, true);
        if (why) *why = reason;
        return false;
    }
    if (busy()) {
        if (why) *why = "une requ\xC3\xAAte est en cours";
        return false;
    }
    if (frameText_->text() != hexText) frameText_->setText(hexText);
    if (!(session_->target() == target_)) session_->setTarget(target_);
    pendingRaw_ = true;
    pendingLabel_ = "Trame";
    auto* s = session_.get();
    pending_ = std::async(std::launch::async, [s, frame] { return s->raw(frame); });
    tabs_->setCurrentIndex(TFrames);
    say("Trame envoy\xC3\xA9" "e \xC3\xA0 " + target_.host + ":" + std::to_string(target_.port) + "\xE2\x80\xA6");
    return true;
}

void HmiModbusToolPane::collect() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    mbtool::Reply r = pending_.get();
    const std::string now = clockOf(wallNow(), true);
    // Le journal des trames : la requete, la reponse.
    const int asked = pendingRaw_ ? (r.request.size() > 7 ? r.request[7] : 0) : pendingQuery_.function;
    if (!r.request.empty()) frameRows_.push_back({now, "\xE2\x86\x92 envoy\xC3\xA9" "e", mbtool::hex(r.request), mbtool::describe(r.request, true), "", 0});
    if (!r.response.empty())
        frameRows_.push_back({now, "\xE2\x86\x90 re\xC3\xA7ue", mbtool::hex(r.response), mbtool::describe(r.response, false, asked), msText(r.ms), r.exception ? 2 : 1});
    else
        frameRows_.push_back({now, "\xE2\x86\x90 rien", "", r.why, r.timeout ? msText(r.ms) : "", 3});
    if (frameRows_.size() > 2000) frameRows_.erase(frameRows_.begin(), frameRows_.begin() + 500);
    const std::string label = pendingLabel_;
    if (!pendingRaw_) {
        last_ = r;
        lastQuery_ = pendingQuery_;
        refreshValues();
    }
    refreshFrames();
    if (r.ok) say(label + " : r\xC3\xA9ponse en " + msText(r.ms) + (r.registers.empty() && r.bits.empty() ? std::string{} : " (" + std::to_string(r.registers.size() + r.bits.size()) + " valeurs)"));
    else if (r.exception == 4 && !twinTarget().empty() && !pendingRaw_ && (pendingQuery_.function == 5 || pendingQuery_.function == 6 || pendingQuery_.function == 15 || pendingQuery_.function == 16))
        say(label + " : exception 04 \xE2\x80\x94 la case est forc\xC3\xA9" "e dans " + twinTarget() + " : l'\xC3\xA9" "criture est refus\xC3\xA9" "e (Valeurs simul\xC3\xA9" "es : d\xC3\xA9" "forcer)", true);
    else if (r.exception) say(label + " : exception " + hmi::modbus::exceptionText(r.exception), true);
    else say(label + " : " + (r.why.empty() ? std::string("pas de r\xC3\xA9ponse") : r.why), true);
}

// Lot 18 : la cible est-elle un jumeau ? (son adresse locale, ou visible sur le vrai reseau)
std::string HmiModbusToolPane::twinTarget() const {
    auto* h = hosts_.equipments ? hosts_.equipments() : nullptr;
    if (!h) return {};
    return h->twinOfEndpoint(target_.host + ":" + std::to_string(target_.port));
}

std::string HmiModbusToolPane::twinCell(int function, int address, bool* forced, std::optional<std::pair<double, double>>* band) const {
    const std::string name = twinTarget();
    if (name.empty()) return {};
    auto* h = hosts_.equipments ? hosts_.equipments() : nullptr;
    const hmi::Project* p = hosts_.project ? hosts_.project() : nullptr;
    // Le jumeau se nomme par son libelle ("Centrale PM5560 (virtuel)") : son equipement.
    const hmi::Equipment* e = p ? p->equipmentByName(name) : nullptr;
    if (p && !e)
        for (const auto& q : p->equipments)
            if (q.hasTwin() && q.twinLabel() == name) e = &q;
    const auto bank = h && e ? h->twinBank(e->name) : nullptr;
    if (!e || !bank || address < 0) return {};
    const hmi::MemTable t = function == 1 || function == 5 || function == 15 ? hmi::MemTable::Coils
                            : function == 2                                   ? hmi::MemTable::DiscreteInputs
                            : function == 4                                   ? hmi::MemTable::InputRegisters
                                                                              : hmi::MemTable::Holding;
    const auto o = static_cast<std::uint32_t>(address);
    std::string out;
    const bool isForced = bank->forced(t, o);
    if (forced) *forced = isForced;
    for (const auto& b : e->behaviors) {
        if (!b.enabled) continue;
        const auto fr = hmi::twin::freeRow(b.address, b.type);
        if (!fr || fr->table != t) continue;
        std::string ty = b.type;
        for (auto& ch : ty) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const std::uint32_t n = fr->boolean ? 1u : (ty == "REAL" || ty == "DINT" || ty == "UDINT" || ty == "DWORD") ? 2u : 1u;
        if (o < fr->offset || o >= fr->offset + n) continue;
        out = "~ " + std::string(hmi::behaviorKindLabel(b.kind));
        if (band && (b.kind == hmi::BehaviorKind::Sine || b.kind == hmi::BehaviorKind::Ramp || b.kind == hmi::BehaviorKind::Random) && n == 1)
            *band = std::make_pair(std::min(b.a, b.b), std::max(b.a, b.b));
        break;
    }
    if (isForced) {
        for (const auto& f : e->forcings) {
            const auto fr = hmi::twin::freeRow(f.address, f.type);
            if (fr && fr->table == t && (fr->offset == o || fr->offset + 1 == o)) {
                out = (out.empty() ? std::string{} : out + " \xC2\xB7 ") + "F " + hmi::twin::numberText(f.value) + " (forc\xC3\xA9" "e)";
                break;
            }
        }
        if (out.find('F') == std::string::npos) out += (out.empty() ? "" : " \xC2\xB7 ") + std::string("F (forc\xC3\xA9" "e)");
    }
    return out;
}

std::string HmiModbusToolPane::placeText(int offset) const { return modicon(lastQuery_.function, offset); }

void HmiModbusToolPane::refreshValues() {
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    if (last_) {
        const auto& r = *last_;
        const auto f = format();
        if (!r.ok && !r.exception) {
            rows.push_back({"\xE2\x80\x94", "", r.why.empty() ? std::string("pas de r\xC3\xA9ponse") : r.why, "", ""});
            tones.push_back(3);
        } else if (r.exception) {
            rows.push_back({"\xE2\x80\x94", "", "exception " + hmi::modbus::exceptionText(r.exception), "", ""});
            tones.push_back(2);
        } else if (lastQuery_.function == 43) {
            static const char* names[] = {"Fabricant", "R\xC3\xA9" "f\xC3\xA9rence", "Version", "Site", "Produit", "Mod\xC3\xA8le", "Application"};
            for (const auto& [object, text] : r.objects) {
                rows.push_back({std::to_string(object), object < 7 ? names[object] : "", text, "", ""});
                tones.push_back(1);
            }
        } else if (!r.bits.empty()) {
            for (std::size_t i = 0; i < r.bits.size(); ++i) {
                const int a = lastQuery_.address + static_cast<int>(i);
                bool forced = false;
                const std::string tw1 = twinCell(lastQuery_.function, a, &forced);
                rows.push_back({std::to_string(a), placeText(a), r.bits[i] ? "1 (vrai)" : "0 (faux)", "", "", tw1});
                tones.push_back(forced ? 2 : r.bits[i] ? 1 : 0);
            }
        } else if (lastQuery_.function == 5 || lastQuery_.function == 6 || lastQuery_.function == 15 || lastQuery_.function == 16) {
            rows.push_back({std::to_string(lastQuery_.address), placeText(lastQuery_.address), "\xC3\xA9" "crit : " + writeText_, "", ""});
            tones.push_back(1);
        } else {
            const std::size_t step = mbtool::wide(f) ? 2 : 1;
            for (std::size_t i = 0; i < r.registers.size(); i += step) {
                const int a = lastQuery_.address + static_cast<int>(i);
                char hexText[16];
                std::snprintf(hexText, sizeof hexText, "0x%04X", r.registers[i]);
                std::string bin;
                for (int b = 15; b >= 0; --b) {
                    bin += (r.registers[i] >> b) & 1 ? '1' : '0';
                    if (b % 4 == 0 && b) bin += ' ';
                }
                bool forced = false;
                std::string tw1 = twinCell(lastQuery_.function, a, &forced);
                if (step == 2 && tw1.empty()) tw1 = twinCell(lastQuery_.function, a + 1, &forced);
                rows.push_back({std::to_string(a) + (step == 2 ? "-" + std::to_string(a + 1) : std::string{}), placeText(a),
                                mbtool::formatValue(r.registers, i, f, lowFirst_), hexText, bin, tw1});
                tones.push_back(forced ? 2 : 0);
            }
        }
    }
    for (auto& row : rows) row.resize(6);
    valuesModel_ = std::make_shared<Rows>(std::vector<std::string>{"Adresse", "Modicon", "Valeur", "Hexa", "Binaire", "Esclave simul\xC3\xA9"}, std::move(rows),
                                          [tones](ui::RowIndex r, std::size_t c) {
                                              ui::CellStyle st;
                                              if (r >= tones.size()) return st;
                                              if (c == 0) st.bold = true;
                                              if (c == 2 || c == 5) st.fgTone = toneOf(tones[r]);
                                              return st;
                                          });
    values_->setModel(valuesModel_);
    tabs_->setTabBadge(TRead, last_ ? (last_->ok ? msText(last_->ms) : std::string("\xC3\xA9" "chec")) : std::string{},
                       last_ ? (last_->ok ? ui::Tone::Ok : ui::Tone::Error) : ui::Tone::None);
}

// 1.9 : la lecture cyclique a plusieurs requetes (la page) ; sans requete, celle
// de Lecture / ecriture est reprise (les sessions d'avant la 1.9 demarrent ainsi).
bool HmiModbusToolPane::startCyclic(std::string* why) {
    if (cyclicTab_->rows().empty() && !cyclicTab_->addFromReadTab(why)) return false;
    tabs_->setCurrentIndex(TCyclic);
    const bool ok = cyclicTab_->start(why);
    rebuildProperties();
    return ok;
}

void HmiModbusToolPane::stopCyclic() {
    cyclicTab_->stop();
    rebuildProperties();
}

void HmiModbusToolPane::refreshCyclic() {
    cyclicTab_->tick(lastLive_, true);
    cyclicTab_->refreshSide(*side_);
}

void HmiModbusToolPane::refreshFrames() {
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (auto it = frameRows_.rbegin(); it != frameRows_.rend() && rows.size() < 500; ++it) {
        rows.push_back({it->time, it->direction, it->hexText, it->text, it->ms});
        tones.push_back(it->tone);
    }
    framesModel_ = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Sens", "Trame (hexa)", "En clair", "Temps"}, std::move(rows),
                                          [tones](ui::RowIndex r, std::size_t c) {
                                              ui::CellStyle st;
                                              if (r < tones.size() && (c == 1 || c == 3)) st.fgTone = toneOf(tones[r]);
                                              return st;
                                          });
    frames_->setModel(framesModel_);
    tabs_->setTabBadge(TFrames, frameRows_.empty() ? std::string{} : std::to_string(frameRows_.size()));
}

bool HmiModbusToolPane::startSpy(std::string* why) {
    const auto fail = [&](std::string m) {
        say("Espion : " + m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (spyRunning_) stopSpy();
    std::string reason;
    if (spyMode_ == "trace") {
        spy::traceTo(journal_);
        say("Espion (trace) : les trames de ce PC - les liaisons de l'IHM, les \xC3\xA9quipements et l'outil.");
    } else if (spyMode_ == "relais") {
        relay_ = std::make_unique<spy::Relay>();
        if (!relay_->start("0.0.0.0", relayPort_, target_.host, target_.port, journal_, &reason)) {
            relay_.reset();
            return fail("relais impossible : " + reason);
        }
        say("Espion (relais) : le ma\xC3\xAEtre doit viser ce PC, port " + std::to_string(relay_->port()) + " ; les trames sont relay\xC3\xA9" "es vers "
            + target_.host + ":" + std::to_string(target_.port) + ".");
    } else {
        const auto sup = spy::captureSupport();
        if (!sup.available) return fail(sup.why);
        std::string ifname = spyInterface_;
        if (ifname.empty()) {
            const auto list = spy::captureInterfaces(&reason);
            if (list.empty()) return fail("aucune interface : " + reason);
            ifname = list.front().name;
        }
        capture_ = std::make_unique<spy::Capture>();
        if (!capture_->start(ifname, spyPort_, promiscuous_, journal_, &reason)) {
            capture_.reset();
            return fail(reason);
        }
        say("Espion (capture, " + sup.backend + ") : l'interface " + ifname + ", port " + std::to_string(spyPort_)
            + (promiscuous_ ? " (tout ce qui passe : port miroir)" : std::string{}) + ".");
    }
    spyRunning_ = true;
    tabs_->setCurrentIndex(TSpy);
    rebuildProperties();
    return true;
}

void HmiModbusToolPane::stopSpy() {
    if (!spyRunning_) return;
    if (spyMode_ == "trace") spy::traceTo(nullptr);
    if (relay_) relay_->stop();
    if (capture_) capture_->stop();
    relay_.reset();
    capture_.reset();
    spyRunning_ = false;
    const auto st = journal_->stats();
    say("Espion arr\xC3\xAAt\xC3\xA9 : " + std::to_string(st.frames) + " trame(s).");
    rebuildProperties();
}

void HmiModbusToolPane::refreshSpy() {
    const std::uint64_t last = journal_->lastSeq();
    if (last == spyShown_ && spyModel_) return;
    spyShown_ = last;
    const auto events = journal_->since(last > 1000 ? last - 1000 : 0, 1000);
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        rows.push_back({std::to_string(it->seq), clockOf(it->t, true), it->source, it->from, it->to, (it->request ? "\xE2\x86\x92 " : "\xE2\x86\x90 ") + it->text,
                        it->request ? std::string{} : (it->replyMs >= 0 ? msText(it->replyMs) : std::string("?")), mbtool::hex(it->adu)});
        tones.push_back(it->exception ? 2 : it->request ? 0 : 1);
    }
    spyModel_ = std::make_shared<Rows>(std::vector<std::string>{"N\xC2\xB0", "Heure", "Source", "De", "Vers", "En clair", "R\xC3\xA9ponse", "Hexa"}, std::move(rows),
                                       [tones](ui::RowIndex r, std::size_t c) {
                                           ui::CellStyle st;
                                           if (r < tones.size() && c == 5) st.fgTone = toneOf(tones[r]);
                                           return st;
                                       });
    spy_->setModel(spyModel_);
    tabs_->setTabLive(TSpy, spyRunning_);
    tabs_->setTabBadge(TSpy, last ? std::to_string(journal_->stats().frames) : std::string{});
}

bool HmiModbusToolPane::ping(std::string* why) {
    if (pingHost_.empty()) pingHost_ = target_.host;
    if (pingHost_.empty()) {
        if (why) *why = "aucune adresse";
        return false;
    }
    stopPing();
    pingStop_ = std::make_shared<std::atomic<bool>>(false);
    auto stop = pingStop_;
    auto inbox = pingInbox_;
    auto mutex = pingMutex_;
    const std::string host = pingHost_;
    const int port = pingPort_, count = pingCount_, timeout = target_.timeoutMs;
    pingTask_ = std::async(std::launch::async, [stop, inbox, mutex, host, port, count, timeout] {
        for (int i = 0; (count == 0 || i < count) && !stop->load(); ++i) {
            const auto r = hmi::netinfo::ping(host, timeout);
            PingRow row;
            row.time = clockOf(wallNow());
            row.host = host;
            row.result = r.ok ? "r\xC3\xA9ponse en " + msText(r.ms) : r.why;
            row.method = r.method;
            row.tone = r.ok ? 1 : 3;
            {
                std::lock_guard<std::mutex> lock(*mutex);
                inbox->push_back(row);
            }
            if (port > 0) {
                const auto t = hmi::netinfo::probeTcp(host, port, timeout);
                PingRow p;
                p.time = clockOf(wallNow());
                p.host = host + ":" + std::to_string(port);
                p.result = t.ok ? "port ouvert, connexion en " + msText(t.ms) : t.why;
                p.method = t.method;
                p.tone = t.ok ? 1 : 3;
                std::lock_guard<std::mutex> lock(*mutex);
                inbox->push_back(p);
            }
            for (int w = 0; w < 10 && !stop->load() && (count == 0 || i + 1 < count); ++w) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    tabs_->setCurrentIndex(TPing);
    say("Ping " + host + (port > 0 ? " (et le port " + std::to_string(port) + ")" : std::string{}) + (count ? ", " + std::to_string(count) + " fois" : std::string(", sans fin")));
    return true;
}

void HmiModbusToolPane::stopPing() {
    if (pingStop_) pingStop_->store(true);
    if (pingTask_.valid()) pingTask_.wait();
}

void HmiModbusToolPane::refreshPings() {
    {
        std::lock_guard<std::mutex> lock(*pingMutex_);
        if (!pingInbox_->empty() || !pingsModel_) {
            for (auto& r : *pingInbox_) pingRows_.push_back(std::move(r));
            pingInbox_->clear();
        } else {
            return;
        }
    }
    if (pingRows_.size() > 2000) pingRows_.erase(pingRows_.begin(), pingRows_.begin() + 500);
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    std::size_t ok = 0, total = 0;
    for (auto it = pingRows_.rbegin(); it != pingRows_.rend(); ++it) {
        rows.push_back({it->time, it->host, it->result, it->method});
        tones.push_back(it->tone);
        ++total;
        ok += it->tone == 1 ? 1 : 0;
    }
    pingsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Adresse", "R\xC3\xA9sultat", "M\xC3\xA9thode"}, std::move(rows),
                                         [tones](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle st;
                                             if (r < tones.size() && c == 2) st.fgTone = toneOf(tones[r]);
                                             return st;
                                         });
    pings_->setModel(pingsModel_);
    tabs_->setTabBadge(TPing, total ? std::to_string(ok) + " / " + std::to_string(total) : std::string{}, ok == total ? ui::Tone::Ok : ui::Tone::Warning);
}

void HmiModbusToolPane::clear() {
    switch (static_cast<int>(tabs_->currentIndex())) {
        case TRead: last_.reset(); refreshValues(); break;
        case TCyclic: cyclicTab_->clearData(); return;
        case TFrames: frameRows_.clear(); refreshFrames(); break;
        case TSpy: journal_->clear(); spyShown_ = static_cast<std::uint64_t>(-1); refreshSpy(); break;
        case TPing: {
            pingRows_.clear();
            pingsModel_.reset();
            refreshPings();
            break;
        }
        default: break;
    }
    say("Effac\xC3\xA9.");
}

bool HmiModbusToolPane::exportCsv(std::string* where) {
    std::string text, name;
    std::size_t rows = 0;
    switch (static_cast<int>(tabs_->currentIndex())) {
        case TCyclic: return cyclicTab_->exportCsv(true, false, where);      // 1.9 : toutes les requetes
        case TSpy:
            text = journal_->csv();
            name = "espion_modbus";
            rows = journal_->stats().frames;
            break;
        case TPing:
            text = "heure;adresse;resultat;methode\n";
            for (const auto& r : pingRows_) text += r.time + ";" + r.host + ";" + r.result + ";" + r.method + "\n";
            name = "ping";
            rows = pingRows_.size();
            break;
        case TFrames:
            text = "heure;sens;trame;en_clair;temps\n";
            for (const auto& r : frameRows_) text += r.time + ";" + r.direction + ";" + r.hexText + ";" + r.text + ";" + r.ms + "\n";
            name = "trames_modbus";
            rows = frameRows_.size();
            break;
        default:
            if (valuesModel_) {
                text = "adresse;modicon;valeur;hexa;binaire\n";
                for (std::size_t r = 0; r < valuesModel_->rowCount(); ++r) {
                    for (std::size_t c = 0; c < 5; ++c) text += (c ? ";" : "") + valuesModel_->cellText(static_cast<ui::RowIndex>(r), c);
                    text += "\n";
                }
                rows = valuesModel_->rowCount();
            }
            name = "lecture_modbus";
            break;
    }
    if (rows == 0) {
        say("Rien \xC3\xA0 exporter dans cet onglet.", true);
        return false;
    }
    hmi::ExportRequest rq;
    rq.fileName = name + "_" + hmi::wallStamp().substr(0, 10) + ".csv";
    rq.format = "CSV";
    rq.source = "outil Modbus";
    rq.rows = rows;
    rq.data = std::make_shared<const hmi::Bytes>(text.begin(), text.end());
    rq.origin = "IHM > Outil Modbus";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Export\xC3\xA9 : " + path : "Export impossible" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

void HmiModbusToolPane::rebuildProperties() {
    const auto field = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setField(key, std::string(v));
        };
    };
    std::vector<PG::Category> cats;
    const int tab = static_cast<int>(tabs_->currentIndex());
    if (tab == TCyclic) {                 // 1.9 : la requete choisie, la lecture (toutes les requetes)
        cyclicTab_->properties(cats);
        grid_->setCategories(std::move(cats));
        cyclicTab_->refreshSide(*side_);
        invalidateLayout();
        return;
    }
    PG::Category target;
    target.name = "Cible";
    target.properties.push_back(prop("\xC3\x89quipement", equipment_.empty() ? std::string(kTyped) : equipment_, PG::ValueType::Enum, field("equipement"),
                                     "Un \xC3\xA9quipement du projet (son adresse, son esclave, son ordre des mots), l'automate, ou une adresse tap\xC3\xA9" "e.",
                                     targetChoices()));
    target.properties.push_back(prop("Adresse IP", target_.host, PG::ValueType::Text, field("hote")));
    target.properties.push_back(prop("Port", std::to_string(target_.port), PG::ValueType::Integer, field("port"), "502 : Modbus TCP."));
    target.properties.push_back(prop("Esclave (Unit Id)", std::to_string(target_.unit), PG::ValueType::Integer, field("esclave")));
    target.properties.push_back(prop("D\xC3\xA9lai (ms)", std::to_string(target_.timeoutMs), PG::ValueType::Integer, field("delai")));
    cats.push_back(std::move(target));
    if (tab == TRead || tab == TCyclic || tab == TFrames) {
        PG::Category rq;
        rq.name = "Requ\xC3\xAAte";
        rq.properties.push_back(prop("Fonction", functionLabel(query_.function), PG::ValueType::Enum, field("fonction"), "Lire ou \xC3\xA9" "crire : bits ou mots.",
                                     functionLabels()));
        rq.properties.push_back(prop("Adresse", std::to_string(query_.address), PG::ValueType::Text, field("adresse"),
                                     "\xC3\x80 partir de 0 (le registre 40001 est l'adresse 0) ; ou tapez 40101, 30001, %MW100 : l'adresse et la fonction suivent."));
        rq.properties.push_back(prop("En Modicon", modicon(query_.function, query_.address), PG::ValueType::ReadOnly));
        rq.properties.push_back(prop("Nombre", std::to_string(query_.count), PG::ValueType::Integer, field("nombre"), "125 mots, 2000 bits au plus."));
        rq.properties.push_back(prop("Format", formatLabel_, PG::ValueType::Enum, field("format"),
                                     "Comment lire un registre ; 32 bits et flottant : deux registres.", mbtool::formatLabels()));
        rq.properties.push_back(prop("Ordre des mots", lowFirst_ ? "poids faible d'abord (Schneider)" : "poids fort d'abord", PG::ValueType::Enum, field("ordre"),
                                     {}, {"poids faible d'abord (Schneider)", "poids fort d'abord"}));
        if (tab == TRead)
            rq.properties.push_back(prop("Valeur \xC3\xA0 \xC3\xA9" "crire", writeText_, PG::ValueType::Text, field("valeur"),
                                         "Une valeur, ou plusieurs s\xC3\xA9par\xC3\xA9" "es par ; (\xC3\xA9" "crites \xC3\xA0 la suite) - dans le format choisi. Bits : 1 0 1."));
        cats.push_back(std::move(rq));
    }
    if (tab == TSpy) {
        PG::Category sp;
        sp.name = "Espion";
        static const std::vector<std::string> modes{"Trace (ce PC)", "Relais (un autre ma\xC3\xAEtre)", "Capture r\xC3\xA9seau (Npcap / libpcap)"};
        const std::string mode = spyMode_ == "relais" ? modes[1] : spyMode_ == "capture" ? modes[2] : modes[0];
        sp.properties.push_back(prop("Mode", mode, PG::ValueType::Enum, field("mode"),
                                     "Trace : les trames de ce PC (IHM, \xC3\xA9quipements, outil). Relais : un autre ma\xC3\xAEtre vise ce PC, qui relaie vers la cible. "
                                     "Capture : ce que voit la carte r\xC3\xA9seau, comme Wireshark (port miroir du switch pour voir les autres).",
                                     modes));
        if (spyMode_ == "relais") {
            sp.properties.push_back(prop("Port d'\xC3\xA9" "coute", std::to_string(relayPort_), PG::ValueType::Integer, field("ecoute"),
                                         "Le ma\xC3\xAEtre (une supervision) vise ce PC \xC3\xA0 ce port ; les trames vont \xC3\xA0 la cible (\xC3\xA0 droite, en haut)."));
        }
        if (spyMode_ == "capture") {
            const auto sup = spy::captureSupport();
            sp.properties.push_back(prop("Capture", sup.available ? sup.backend + " : disponible" : sup.why, PG::ValueType::ReadOnly));
            std::vector<std::string> names;
            std::string current;
            std::string reason;
            for (const auto& i : spy::captureInterfaces(&reason)) {
                std::string label = i.name + (i.addresses.empty() ? std::string{} : " (" + i.addresses.front() + ")");
                if (i.description != i.name) label = i.name + " (" + i.description + (i.addresses.empty() ? std::string{} : ", " + i.addresses.front()) + ")";
                names.push_back(label);
                if (i.name == spyInterface_) current = label;
            }
            sp.properties.push_back(prop("Interface", current, PG::ValueType::Enum, field("interface"), "La carte r\xC3\xA9seau \xC3\xA0 \xC3\xA9" "couter.", names));
            sp.properties.push_back(prop("Port Modbus", std::to_string(spyPort_), PG::ValueType::Integer, field("port_modbus")));
            sp.properties.push_back(prop("Tout ce qui passe", tf(promiscuous_), PG::ValueType::Boolean, field("promiscuite"),
                                         "Mode promiscuit\xC3\xA9 : aussi les trames qui ne sont pas pour ce PC (avec un port miroir sur le switch)."));
        }
        const auto st = journal_->stats();
        sp.properties.push_back(prop("\xC3\x89tat", spyRunning_ ? "en marche" : "arr\xC3\xAAt\xC3\xA9", PG::ValueType::ReadOnly));
        sp.properties.push_back(prop("Trames", std::to_string(st.frames) + " (" + std::to_string(st.requests) + " requ\xC3\xAAtes, " + std::to_string(st.responses) + " r\xC3\xA9ponses)",
                                     PG::ValueType::ReadOnly));
        sp.properties.push_back(prop("Exceptions, sans r\xC3\xA9ponse", std::to_string(st.exceptions) + ", " + std::to_string(st.unanswered), PG::ValueType::ReadOnly));
        if (st.responses) sp.properties.push_back(prop("Temps de r\xC3\xA9ponse", msText(st.avgReplyMs) + " en moyenne, max " + msText(st.maxReplyMs), PG::ValueType::ReadOnly));
        cats.push_back(std::move(sp));
    }
    if (tab == TPing) {
        PG::Category pg;
        pg.name = "Ping";
        pg.properties.push_back(prop("Adresse", pingHost_.empty() ? target_.host : pingHost_, PG::ValueType::Text, field("ping_hote")));
        pg.properties.push_back(prop("Port TCP", std::to_string(pingPort_), PG::ValueType::Integer, field("ping_port"),
                                     "0 : aucun. Sinon ce port est essay\xC3\xA9 aussi (un pare-feu d'usine bloque souvent le ping, rarement le 502)."));
        pg.properties.push_back(prop("Nombre", std::to_string(pingCount_), PG::ValueType::Integer, field("ping_nombre"), "0 : sans fin (Arr\xC3\xAAter le ping)."));
        cats.push_back(std::move(pg));
    }
    grid_->setCategories(std::move(cats));
}

void HmiModbusToolPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(440.f, b.w * 0.3f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    // 1.9 : sous la grille, la boite d'aide d'une requete en pause et la note violette.
    const bool side = static_cast<int>(tabs_->currentIndex()) == TCyclic && !side_->empty();
    const float sideH = side ? std::min(h * 0.5f, side_->heightFor(gridW)) : 0.f;
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h - sideH});
    side_->setBounds({b.x + b.w - gridW, b.y + 38 + h - sideH, gridW, sideH});
}

// 1.9 : sur l'onglet Lecture cyclique, les touches de la liste des requetes ; Ctrl+Z
// defait d'abord un geste sur la liste (s'il y en a), sinon il va au projet.
ui::EventResult HmiModbusToolPane::onEvent(const ui::InputEvent& ev) {
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (!k || static_cast<int>(tabs_->currentIndex()) != TCyclic || grid_->editing()) return ui::EventResult::Ignored;
    if (k->mods.ctrl && !k->mods.shift && k->key == ui::Key::V) {
        if (!hosts_.clipboardText) return ui::EventResult::Ignored;
        (void)cyclicTab_->pasteRequests(hosts_.clipboardText());
        return ui::EventResult::Consumed;
    }
    return cyclicTab_->key(*k) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
}

void HmiModbusToolPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    collect();
    if (ctx.time - lastLive_ >= 0.25) {
        const bool second = ctx.time - lastSecond_ >= 1.0;
        if (second) lastSecond_ = ctx.time;
        lastLive_ = ctx.time;
        const bool cyclic = static_cast<int>(tabs_->currentIndex()) == TCyclic;
        if (cyclicTab_->running() || cyclic) {
            cyclicTab_->tick(ctx.time);
            tools_->setText(ASets, "Les jeux de lecture du projet : ouvrir, enregistrer (Ctrl+S), exporter\xE2\x80\xA6",
                            "Jeu : " + cyclicTab_->setName() + (cyclicTab_->setModified() ? " *" : "") + " \xE2\x96\xBE");
            if (cyclic) {
                const std::string text = cyclicTab_->statusText();
                if (text != cyclicStatus_) {
                    cyclicStatus_ = text;
                    status_->setMessage(text, ui::StatusBar::Severity::Info);
                }
            }
        }
        if (!cyclic) cyclicStatus_.clear();      // de retour sur l'onglet : l'etat se redit
        // A droite : "R4 lit un esclave simule", seulement sur l'onglet Lecture cyclique.
        if (auto* note = dynamic_cast<SlaveReadNote*>(status_->findById(id() + ".status.esclave")))
            note->setText(cyclic ? cyclicTab_->slaveStatus() : std::string{});
        refreshSpy();
        refreshPings();
        if (second && !grid_->editing() && (cyclicTab_->running() || spyRunning_)) rebuildProperties();
    }
}

} // namespace app
