// app/hmi/HmiEquipmentPanes.cpp - Configuration > Equipements (lot 15) : les
// equipements, les variables liees, le reseau du PC, le scanner IP ; la mise
// en page du volet.
#include "HmiCommPanes.hpp"
#include "../../hmi/HmiTypes.hpp"

#include "HmiCommHost.hpp"
#include "HmiEquipmentHost.hpp"
#include "HmiMemoryMap.hpp"
#include "HmiTwinValues.hpp"
#include "HmiNetDiagram.hpp"
#include "HmiPaneKit.hpp"
#include "HmiSimMarks.hpp"                 // 1.9 : le violet du simule
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiZones.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using hmi::Id;
using PG = ui::PropertyGrid;
namespace eq = hmi::equip;
namespace zn = hmi::zones;

namespace {

ui::Tone toneOf(int t) {
    switch (t) {
        case 1: return ui::Tone::Ok;
        case 2: return ui::Tone::Warning;
        case 3: return ui::Tone::Error;
        case 4: return ui::Tone::Accent;
        default: return ui::Tone::None;
    }
}

std::string msText(double ms) {
    if (ms < 0) return "\xE2\x80\x94";
    if (ms < 0.1) return "< 0.1 ms";
    char b[32];
    std::snprintf(b, sizeof b, ms < 10 ? "%.1f ms" : "%.0f ms", ms);
    return b;
}

std::string numberText(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

bool loopback(const std::string& host) {
    std::uint32_t ip = 0;
    return host == "localhost" || (eq::parseIpv4(host, ip) && (ip >> 24) == 127);
}

// Le masque que l'on suppose a un equipement hors reseau (pour proposer une
// adresse au PC) : /16 dans 172.16.0.0/12, /24 ailleurs.
int guessPrefix(std::uint32_t ip) {
    if ((ip & 0xFFF00000u) == 0xAC100000u) return 16;
    return 24;
}

const std::vector<std::string>& modeLabels() {
    static const std::vector<std::string> k{"Adresse fixe", "Automatique (DHCP)"};
    return k;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// ================================================================ outils ===
EquipmentHost* HmiCommPane::host() const { return hosts_.equipments ? hosts_.equipments() : nullptr; }

ui::TableView& HmiCommPane::scanTable() noexcept { return scanPage_->table(); }

std::optional<hmi::netinfo::Adapter> HmiCommPane::adapter(const std::string& name) const {
    auto* h = host();
    if (!h || name.empty()) return std::nullopt;
    for (auto& a : h->adapters())
        if (a.name == name) return a;
    return std::nullopt;
}

HmiCommPane::Side HmiCommPane::side() const {
    switch (static_cast<int>(tabs_->currentIndex())) {
        case TNetwork:
            if (simView_) return portSide_ || equipment_.empty() ? Side::SimPort : equipment_ == kPlcKey ? Side::Plc : Side::Equipment;
            return portSide_ || equipment_.empty() ? Side::Port : equipment_ == kPlcKey ? Side::Plc : Side::Equipment;
        case TMap: return Side::Map;
        case TValues: return Side::Values;
        case TScanner: return Side::Scan;
        case TEquipments:
        case TState: return equipment_.empty() || equipment_ == kPlcKey ? Side::Plc : Side::Equipment;
        case TBound: return Side::Bound;
        case TPlan: {
            // Lot 16 : la ligne choisie du plan - une variable IHM, un equipement, ou l'automate.
            const auto* r = selectedPlanRow();
            if (r && (r->kind == PlanRow::Kind::Ihm || r->kind == PlanRow::Kind::Member)) return Side::Bound;
            if (r && r->group != kPlcKey) return Side::Equipment;
            return Side::Plc;
        }
        default: return Side::Plc;
    }
}

bool HmiCommPane::changeProject(const std::string& label, const std::function<void(hmi::Project&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { fn(p); });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

void HmiCommPane::showReport(const std::vector<std::pair<std::string, int>>& lines, bool ok) {
    reportLines_.clear();
    reportTones_.clear();
    for (const auto& [text, tone] : lines) {
        reportLines_.push_back(text);
        reportTones_.push_back(tone);
    }
    std::vector<std::vector<std::string>> rows;
    for (const auto& l : reportLines_) rows.push_back({l});
    const auto tones = reportTones_;
    reportModel_ = std::make_shared<Rows>(std::vector<std::string>{"Compte rendu de l'essai"}, std::move(rows), [tones](ui::RowIndex r, std::size_t) {
        ui::CellStyle st;
        if (r >= tones.size()) return st;
        st.fgTone = toneOf(tones[r]);
        if (tones[r] == 1) st.icon = ui::Icon::Ok;
        if (tones[r] == 3) st.icon = ui::Icon::Error;
        if (tones[r] == 2) st.icon = ui::Icon::Warning;
        return st;
    });
    report_->setModel(reportModel_);
    tabs_->setCurrentIndex(TTest);
    tabs_->setTabBadge(TTest, ok ? "r\xC3\xA9ussi" : "\xC3\xA9" "chec", ok ? ui::Tone::Ok : ui::Tone::Error);
}

// ============================================================ equipements ===
namespace {

// "14:02:31" : une heure murale, en heure locale.
std::string clockOf(double wall) {
    if (wall <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(wall);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

// "1 vrai appareil", "7 vrais appareils".
std::string counted(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// 1.9 : la note sous la fiche (Detacher..., Dans l'application...) - des mots a
// la ligne, mesures par la plateforme (la mise en page se fait hors du dessin).
constexpr gfx::FontId kNoteFont{13};
std::vector<std::string> noteLines(const std::string& text, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tried, kNoteFont) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush();
        else word.push_back(c);
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}
float noteHeightFor(const std::string& text, float width) {
    if (text.empty()) return 0.f;
    return 14.f + static_cast<float>(noteLines(text, width - 20.f).size()) * (ui::lineHeight(kNoteFont) + 3.f);
}

// 1.9 : UNE LIGNE DU TABLEAU DES EQUIPEMENTS - l'automate du projet, un vrai
// appareil, l'esclave simule lie d'un vrai (en retrait, au cadenas), un
// equipement seulement simule. Les tons : 0 rien, 1 bon, 2 attention, 3 erreur,
// 4 accent, 5 estompe, 6 le violet du simule.
struct EquipLine {
    enum class Kind : std::uint8_t { Plc, Real, Slave, Only };
    Kind                     kind{Kind::Real};
    std::vector<std::string> cells;
    int                      rowTone{0}, stateTone{0}, readTone{0};
    bool                     readAuto{false};      // la colonne L'IHM lit : l'etiquette AUTO
    bool                     readDot{false};       // ... le point violet (lu par l'IHM)
    std::size_t              parent{0};            // l'esclave : la ligne de son vrai (le tri les garde ensemble)
    std::string              tip;
};

class EquipModel final : public ui::ITableModel {
public:
    EquipModel(std::vector<EquipLine> lines, bool dark) : lines_(std::move(lines)), dark_(dark) {}
    [[nodiscard]] std::size_t rowCount() const override { return lines_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 8; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const h[] = {"\xC3\x89quipement", "Type", "Adresse", "\xC3\x89tat", "L'IHM lit", "Var.", "Zones m\xC3\xA9moire", "Description"};
        return c < 8 ? h[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < lines_.size() && c < lines_[r].cells.size() ? lines_[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= lines_.size()) return st;
        const auto& l = lines_[r];
        const gfx::Color sim = simmark::text(dark_);
        const auto tone = [&](int t, ui::CellStyle& s) {
            if (t == 6) s.fg = sim;
            else if (t == 5) s.fgTone = ui::Tone::Muted;
            else if (t) s.fgTone = toneOf(t);
        };
        if (l.kind == EquipLine::Kind::Slave) st.bg = simmark::rowTint(dark_);
        if (c == 0) {
            st.bold = true;
            switch (l.kind) {
                case EquipLine::Kind::Plc: st.icon = ui::Icon::Cpu; st.iconTone = toneOf(l.rowTone); break;
                case EquipLine::Kind::Real: st.icon = ui::Icon::Module; st.iconTone = toneOf(l.rowTone); break;
                case EquipLine::Kind::Slave:
                    st.customIcon = HmiCommPane::kIconSlave;
                    st.indent = HmiCommPane::kSlaveIndent;
                    st.fg = sim;
                    st.iconColor = sim;
                    break;
                case EquipLine::Kind::Only:
                    st.customIcon = HmiCommPane::kIconFlask;
                    st.fg = sim;
                    st.iconColor = sim;
                    break;
            }
        }
        if (c == 1 && (l.kind == EquipLine::Kind::Slave || l.kind == EquipLine::Kind::Only)) st.fg = sim;
        if (c == 3) tone(l.stateTone, st);
        if (c == 4) {
            tone(l.readTone, st);
            if (l.readAuto) {
                st.lead = "AUTO";
                st.leadTone = ui::Tone::Accent;
            }
            if (l.readDot) {
                st.customIcon = HmiCommPane::kIconDot;
                st.iconColor = sim;
            }
        }
        if (c == 6 && l.kind == EquipLine::Kind::Slave) st.fgTone = ui::Tone::Muted;
        return st;
    }
    // Trier garde l'esclave sous son vrai : chaque ligne se range comme son vrai, le vrai d'abord.
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (a >= lines_.size() || b >= lines_.size()) return a < b;
        const std::size_t pa = lines_[a].kind == EquipLine::Kind::Slave ? lines_[a].parent : a;
        const std::size_t pb = lines_[b].kind == EquipLine::Kind::Slave ? lines_[b].parent : b;
        if (pa == pb) return lines_[a].kind != EquipLine::Kind::Slave && lines_[b].kind == EquipLine::Kind::Slave;
        return cellText(static_cast<ui::RowIndex>(pa), c) < cellText(static_cast<ui::RowIndex>(pb), c);
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < lines_.size() ? lines_[r].tip : std::string{}; }

private:
    std::vector<EquipLine> lines_;
    bool                   dark_{true};
};

} // namespace

void HmiCommPane::refreshEquipments() {
    // 1.9 : une ligne par equipement - et sous chaque vrai appareil qui en a un,
    // la ligne de son esclave simule lie ; ce que lit l'IHM, colonne par colonne.
    const auto& p = doc_->project;
    auto* h = host();
    equipRows_.clear();
    std::vector<EquipLine> lines;
    std::size_t reals = 0, reachable = 0, linked = 0, only = 0, readSim = 0;
    const std::string dash = "\xE2\x80\x94";
    // L'automate du projet, en tete.
    if (equipFilter_ == "tous" || equipFilter_ == "vrais") {
        const auto& c = p.comm;
        auto* ch = hosts_.comm ? hosts_.comm() : nullptr;
        const hmi::comm::Link* lk = ch ? ch->link() : nullptr;
        const auto [bits, words] = plcMemory();
        EquipLine l;
        l.kind = EquipLine::Kind::Plc;
        std::string state = "le simulateur de l'automate";
        l.stateTone = 4;
        if (c.modbus()) {
            if (lk && lk->connected()) {
                const auto d = lk->diagnostics();
                state = "joignable \xC2\xB7 " + msText(d.avgMs);
                l.stateTone = 1;
            } else if (lk) {
                state = "injoignable";
                l.stateTone = 3;
            } else {
                state = "pas encore test\xC3\xA9";
                l.stateTone = 0;
            }
        }
        l.rowTone = !c.modbus() ? 4 : l.stateTone;
        l.cells = {"Automate du projet", c.modbus() ? "Modbus TCP" : "Simulateur",
                   c.modbus() ? c.host + ":" + std::to_string(c.port) + " \xC2\xB7 esclave " + std::to_string(c.unit) : std::string("dans l'application"),
                   state, c.modbus() ? "le vrai automate" : "le simulateur", std::to_string(currentPlan().points().size()),
                   words ? "%M 0-" + std::to_string(bits ? bits - 1 : 0) + ", %MW 0-" + std::to_string(words - 1) + " (configuration)"
                         : std::string("configuration non import\xC3\xA9" "e"),
                   "l'automate que lit l'IHM"};
        lines.push_back(std::move(l));
        equipRows_.push_back({kPlcKey, false});
    }
    for (const auto& e : p.equipments) {
        const auto st = h ? h->status(e.name) : std::nullopt;
        const auto pr = h ? h->probe(e.name) : std::nullopt;
        const bool via = e.enabled && st && st->viaTwin;
        const auto* lk = h && e.modbus() ? h->link(e.name) : nullptr;
        const bool linkToReal = lk && !(via && e.modbus());
        // Le vrai repond-il ? Sa liaison s'il est lu, sinon son ping (ou le dernier essai de la bascule).
        bool realOk = false;
        if (!e.simulated && st && e.enabled) {
            if (linkToReal) realOk = lk->connected();
            else if (st->fallback) realOk = st->realOnline;
            else realOk = st->realTone == 1 || (pr && pr->done && pr->host == e.host && (pr->ok || pr->portOk));
            if (!e.modbus()) realOk = st->reachable;
        }
        if (!e.simulated) {
            ++reals;
            reachable += realOk ? 1 : 0;
        }
        if (e.linkedSlave()) ++linked;
        if (e.simulated && e.modbus()) ++only;
        readSim += via ? 1 : 0;
        // Montrer : tous, les vrais appareils, les esclaves simules, lus en simule.
        bool showMain = true, showSlave = e.linkedSlave() && e.modbus();
        if (equipFilter_ == "vrais") {
            showMain = !e.simulated;
            showSlave = false;
        } else if (equipFilter_ == "esclaves") {
            showMain = e.simulated;
        } else if (equipFilter_ == "lus") {
            showMain = via;
            showSlave = showSlave && via;
        }
        const int port = h && e.hasTwin() && e.modbus() ? h->simulatedPort(e.name) : 0;
        const std::size_t mainRow = lines.size();
        if (showMain) {
            EquipLine l;
            l.kind = e.simulated && e.modbus() ? EquipLine::Kind::Only : EquipLine::Kind::Real;
            std::string type = e.simulated && e.modbus() ? std::string("Seulement simul\xC3\xA9") : e.modbus() ? std::string("Modbus TCP") : std::string("Ethernet");
            std::string address;
            if (e.simulated && e.modbus())
                address = (port ? "127.0.0.1:" + std::to_string(port) + " " : std::string{}) + "(pr\xC3\xA9vue " + e.host + ")";
            else
                address = e.host + ":" + std::to_string(e.port) + (e.modbus() ? " \xC2\xB7 esclave " + std::to_string(e.unit) : std::string{});
            // L'etat : le vrai appareil (ou l'esclave d'un seulement simule).
            std::string state = "pas encore test\xC3\xA9";
            l.stateTone = 0;
            if (!e.enabled) {
                state = "d\xC3\xA9sactiv\xC3\xA9";
                l.stateTone = 5;
            } else if (e.simulated && e.modbus()) {
                state = twinStateText(e);
                l.stateTone = !port ? 5 : !e.twinResponds || e.twinException ? 2 : 6;
            } else if (!e.modbus()) {
                if (pr && pr->done && pr->host == e.host) {
                    state = pr->ok ? "joignable, ping " + msText(pr->ms) : pr->portOk ? std::string("joignable (port)") : std::string("injoignable");
                    l.stateTone = pr->ok || pr->portOk ? 1 : 3;
                }
            } else if (st && st->fallback) {
                // La bascule : le vrai se tait (le dernier essai dit s'il repond de nouveau).
                state = st->realOnline ? std::string("r\xC3\xA9pond \xC3\xA0 nouveau") : "injoignable" + (st->realSilentSince > 0 ? " depuis " + clockOf(st->realSilentSince) : std::string{});
                l.stateTone = st->realOnline ? 1 : 3;
            } else if (linkToReal) {
                const auto d = lk->diagnostics();
                if (d.connected) {
                    state = "joignable \xC2\xB7 " + msText(d.avgMs);
                    l.stateTone = 1;
                } else if (st && st->tested) {
                    std::string since = st->realSilentSince > 0 ? clockOf(st->realSilentSince) : d.since.size() >= 19 ? d.since.substr(11, 8) : std::string{};
                    state = "injoignable" + (since.empty() ? std::string{} : " depuis " + since);
                    l.stateTone = 3;
                }
            } else if (pr && pr->done && pr->host == e.host) {
                state = pr->ok ? "joignable, ping " + msText(pr->ms) : std::string("injoignable (ping)");
                l.stateTone = pr->ok ? 1 : 3;
            }
            // Ce que lit l'IHM.
            std::string read = dash;
            l.readTone = 5;
            if (!e.enabled || !e.modbus()) {
                read = dash;
            } else if (e.simulated) {
                read = "son esclave (pas encore livr\xC3\xA9)";
                l.readTone = 6;
                l.readDot = via;
            } else if (!e.linkedSlave()) {
                read = "le vrai appareil";
                l.readTone = 0;
            } else {
                const std::string mode = st ? st->readMode : std::string(hmi::readSourceKey(e.appRead()));
                l.readAuto = mode == "auto";
                read = via ? std::string("l'esclave simul\xC3\xA9") : std::string("le vrai appareil");
                l.readTone = via ? 6 : 0;
                if (st && st->chosen) read += " (page Simulation)";
            }
            const auto vars = e.modbus() ? eq::boundVariables(p, e).size() : 0;
            l.rowTone = !e.enabled ? 0 : l.stateTone == 6 ? 4 : l.stateTone == 5 ? 0 : l.stateTone;
            l.cells = {e.name, type, address, state, read, e.modbus() ? std::to_string(vars) : dash,
                       e.modbus() ? (e.zones.declared ? zn::summary(e.zones) : std::string("non d\xC3\xA9" "clar\xC3\xA9" "es")) : dash, e.description};
            if (st && !st->readWhy.empty()) l.tip = e.name + " : " + st->readWhy;
            lines.push_back(std::move(l));
            equipRows_.push_back({e.name, false});
        }
        if (showSlave) {
            // La ligne de son esclave simule lie : en retrait, au cadenas.
            EquipLine l;
            l.kind = EquipLine::Kind::Slave;
            l.parent = showMain ? mainRow : lines.size();
            l.stateTone = !e.enabled || !port ? 5 : !e.twinResponds || e.twinException ? 2 : 6;
            std::string read;
            if (!e.enabled) read = dash;
            else if (via) {
                read = "lu par l'IHM" + (st->readSince > 0 ? " depuis " + clockOf(st->readSince) : std::string{});
                l.readTone = 6;
                l.readDot = true;
            } else if (port) {
                read = "pr\xC3\xAAt, pas lu";
                l.readTone = 5;
            } else {
                read = "arr\xC3\xAAt\xC3\xA9";
                l.readTone = 5;
            }
            l.cells = {e.twinLabel(), "Esclave simul\xC3\xA9",
                       (port ? "127.0.0.1:" + std::to_string(port) : std::string("arr\xC3\xAAt\xC3\xA9")) + " (comme " + e.twinAddress() + ")",
                       e.enabled ? twinStateText(e) : std::string("d\xC3\xA9sactiv\xC3\xA9"), read, std::to_string(eq::boundVariables(p, e).size()),
                       "comme le vrai", "li\xC3\xA9 \xC3\xA0 " + e.name};
            l.tip = e.twinLabel() + " : l'esclave simul\xC3\xA9 li\xC3\xA9 \xC3\xA0 " + e.name + " - sa configuration suit le vrai appareil (le cadenas).";
            lines.push_back(std::move(l));
            equipRows_.push_back({e.name, true});
        }
    }
    equipSummary_ = counted(reals, "vrai appareil", "vrais appareils") + " (" + counted(reachable, "joignable", "joignables") + ") \xC2\xB7 "
                    + counted(linked, "esclave simul\xC3\xA9 li\xC3\xA9", "esclaves simul\xC3\xA9s li\xC3\xA9s") + " \xC2\xB7 "
                    + counted(only, "seulement simul\xC3\xA9", "seulement simul\xC3\xA9s") + " \xC2\xB7 "
                    + (readSim ? "l'IHM lit " + counted(readSim, "\xC3\xA9quipement", "\xC3\xA9quipements") + " en simul\xC3\xA9" : std::string("l'IHM ne lit rien en simul\xC3\xA9"));
    equipModel_ = std::make_shared<EquipModel>(std::move(lines), darkTheme_);
    const bool was = refreshing_;
    refreshing_ = true;
    equipTable_->setModel(equipModel_);
    for (std::size_t i = 0; i < equipRows_.size(); ++i) {
        const auto& row = equipRows_[i];
        if (equipment_.empty() || row.slave != slaveRow_) continue;
        if (row.equipment == equipment_ || (equipment_ != kPlcKey && same(row.equipment, equipment_)))
            equipTable_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    }
    refreshing_ = was;
    tabs_->setTabBadge(TEquipments, std::to_string(p.equipments.size() + (p.comm.modbus() ? 1 : 0))
                                        + (linked + only ? " \xC2\xB7 " + std::to_string(linked + only) + " simul\xC3\xA9" + (linked + only > 1 ? "s" : "") : std::string{}));
    if (simReads_) simReads_->setText(simulatedReadsText());
    // La case "Reperer les lectures simulees" suit le projet (Ctrl+Z).
    if (simMarksBox_) {
        const bool was2 = refreshing_;
        refreshing_ = true;
        simMarksBox_->setState(p.station.simMarks ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        refreshing_ = was2;
    }
}

void HmiCommPane::setEquipmentFilter(const std::string& filter) {
    // Les cles d'avant (lot 17) : "jumeaux", "simules" - les esclaves simules.
    const std::string f = lower(trimmed(filter));
    if (f == "vrais" || f.rfind("les vrais", 0) == 0) equipFilter_ = "vrais";
    else if (f == "esclaves" || f == "jumeaux" || f == "simules" || f.rfind("les esclaves", 0) == 0) equipFilter_ = "esclaves";
    else if (f == "lus" || f.rfind("lus en", 0) == 0) equipFilter_ = "lus";
    else equipFilter_ = "tous";
    if (equipFilterBox_) {
        const bool was = refreshing_;
        refreshing_ = true;
        const auto& items = equipFilterBox_->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value == equipFilter_) equipFilterBox_->setSelectedIndex(static_cast<int>(i));
        refreshing_ = was;
    }
    refreshEquipments();
}

void HmiCommPane::selectEquipment(const std::string& name) {
    equipment_ = name;
    slaveRow_ = false;
    if (name != kPlcKey)
        if (const auto* e = doc_->project.equipmentByName(name)) equipment_ = e->name;
    portSide_ = false;
    refreshEquipments();
    refreshDiagram();
    rebuildProperties();
}

std::string HmiCommPane::addEquipment(const std::string& rawName, hmi::EquipmentType type, const std::string& rawHost, int port, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return std::string{};
    };
    const auto& p = doc_->project;
    std::string name = trimmed(rawName);
    // Lot 16 : "Equipement 1", "Equipement 2"... - un nom qui ne se confond pas avec le mot.
    if (name.empty()) {
        for (int i = 1; i < 10000 && name.empty(); ++i)
            if (const std::string candidate = "\xC3\x89quipement " + std::to_string(i); !p.equipmentByName(candidate)) name = candidate;
    } else if (p.equipmentByName(name)) {
        return fail(name + " : ce nom est d\xC3\xA9j\xC3\xA0 pris");
    }
    std::string hostText = trimmed(rawHost);
    if (hostText.empty()) {
        // Une adresse libre dans le reseau du port choisi (ou du premier port branche).
        std::vector<hmi::netinfo::Adapter> list = host() ? host()->adapters() : std::vector<hmi::netinfo::Adapter>{};
        const hmi::netinfo::Adapter* a = nullptr;
        for (const auto& x : list)
            if (x.name == port_ && x.ip()) a = &x;
        for (const auto& x : list)
            if (!a && x.up && x.ip() && !x.automaticAddress()) a = &x;
        if (a) {
            std::vector<std::uint32_t> taken;
            for (const auto& x : list)
                for (const auto& [ip, pre] : x.addresses) taken.push_back(ip);
            for (const auto& e : p.equipments) {
                std::uint32_t ip = 0;
                if (eq::parseIpv4(e.host, ip)) taken.push_back(ip);
            }
            const std::uint32_t ip = eq::suggestAddress(a->ip(), a->prefix(), taken);
            if (ip) hostText = eq::ipv4Text(ip);
        }
        if (hostText.empty()) hostText = "192.168.1.100";
    } else if (hostText.find(' ') != std::string::npos) {
        return fail("adresse \xC2\xAB " + hostText + " \xC2\xBB : une adresse IP (192.168.1.30) ou un nom");
    }
    hmi::Equipment e;
    e.name = name;
    e.type = type;
    e.host = hostText;
    e.port = port > 0 ? port : (type == hmi::EquipmentType::ModbusTcp ? 502 : 80);
    if (e.port < 1 || e.port > 65535) return fail("port " + std::to_string(e.port) + " : de 1 \xC3\xA0 65535");
    if (!changeProject("Ajouter l'\xC3\xA9quipement " + name, [&](hmi::Project& x) {
            e.id = x.allocate();
            x.equipments.push_back(e);
        }))
        return fail("impossible d'ajouter " + name);
    selectEquipment(name);
    if (static_cast<int>(tabs_->currentIndex()) != TNetwork) tabs_->setCurrentIndex(TEquipments);
    say(name + " ajout\xC3\xA9 (" + std::string(hmi::equipmentTypeLabel(type)) + ", " + hostText + ") : r\xC3\xA9glez-le \xC3\xA0 droite, puis Tester.");
    // Lot 17 : s'il repond en Modbus, ses zones se detectent toutes seules (en lecture seule).
    if (type == hmi::EquipmentType::ModbusTcp && !trimmed(rawHost).empty() && !loopback(hostText)) (void)detectZones(name, false, false);
    return name;
}

bool HmiCommPane::removeEquipment(const std::string& name, std::string* why) {
    const auto* e = doc_->project.equipmentByName(name);
    if (!e) {
        const std::string m = "pas d'\xC3\xA9quipement " + name;
        say(m, true);
        if (why) *why = m;
        return false;
    }
    const std::string real = e->name;
    std::size_t unbound = 0;
    if (!changeProject("Retirer l'\xC3\xA9quipement " + real, [&](hmi::Project& x) {
            std::erase_if(x.equipments, [&](const hmi::Equipment& q) { return q.name == real; });
            for (auto& v : x.programs.variables)
                if (same(v.equipment, real)) {
                    v.equipment.clear();
                    v.address.clear();
                    ++unbound;
                }
        }))
        return false;
    if (equipment_ == real) equipment_.clear();
    say(real + " retir\xC3\xA9" + (unbound ? " (" + std::to_string(unbound) + " variable(s) d\xC3\xA9li\xC3\xA9" "e(s))" : std::string{}) + " : Ctrl+Z le rend.");
    return true;
}

bool HmiCommPane::setEquipmentField(const std::string& name, const std::string& field, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* cur = doc_->project.equipmentByName(name);
    if (!cur) return fail("pas d'\xC3\xA9quipement " + name);
    hmi::Equipment next = *cur;
    const std::string v = trimmed(raw);
    // 1.9 : les reglages de l'esclave simule s'ecrivent aussi "esclave_..." (les
    // cles du lot 17, "jumeau_...", restent) ; "esclave" seul est l'Unit Id.
    std::string key = field;
    if (key.rfind("esclave_", 0) == 0 && key != "esclave_simule") key = "jumeau_" + key.substr(8);
    const auto number = [&](long long lo, long long hi, int& out, const char* label) {
        char* end = nullptr;
        const long long n = std::strtoll(v.c_str(), &end, 10);
        if (v.empty() || (end && *end) || n < lo || n > hi) return fail(std::string(label) + " : un nombre de " + std::to_string(lo) + " \xC3\xA0 " + std::to_string(hi));
        out = static_cast<int>(n);
        return true;
    };
    std::string label = name + " : " + key;
    if (key == "nom") {
        if (v.empty()) return fail("un \xC3\xA9quipement a un nom");
        if (!same(v, name) && doc_->project.equipmentByName(v)) return fail(v + " : ce nom est d\xC3\xA9j\xC3\xA0 pris");
        next.name = v;
        label = "Renommer " + name + " en " + v;
    } else if (key == "type") {
        const auto t = hmi::equipmentTypeFrom(v);
        if (!t) return fail("type \xC2\xAB " + v + " \xC2\xBB : Modbus TCP/IP ou Ethernet TCP/IP");
        next.type = *t;
        if (next.type == cur->type) return true;
        if (next.type == hmi::EquipmentType::EthernetTcp && next.port == 502) next.port = 80;
        if (next.type == hmi::EquipmentType::ModbusTcp && next.port == 80) next.port = 502;
    } else if (key == "hote") {
        if (v.empty() || v.find(' ') != std::string::npos) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : une adresse IP (192.168.1.30) ou un nom");
        next.host = v;
    } else if (key == "port") {
        if (!number(1, 65535, next.port, "Port")) return false;
    } else if (key == "esclave") {
        if (!number(0, 255, next.unit, "Esclave")) return false;
    } else if (key == "delai") {
        if (!number(50, 60000, next.timeoutMs, "D\xC3\xA9lai de r\xC3\xA9ponse")) return false;
    } else if (key == "periode") {
        if (!number(50, 600000, next.periodMs, "P\xC3\xA9riode de scrutation")) return false;
    } else if (key == "reessai") {
        if (!number(1, 3600, next.retryS, "Nouvel essai")) return false;
    } else if (key == "mots") {
        if (!number(1, 125, next.maxWords, "Mots par requ\xC3\xAAte")) return false;
    } else if (key == "bits") {
        if (!number(1, 2000, next.maxBits, "Bits par requ\xC3\xAAte")) return false;
    } else if (key == "ecart") {
        if (!number(0, 100, next.gap, "Regroupement")) return false;
    } else if (key == "mauvaise") {
        if (!number(1, 86400, next.badAfterS, "Mauvaise apr\xC3\xA8s")) return false;
    } else if (key == "ping") {
        if (!number(0, 3600, next.pingS, "Ping toutes les")) return false;
    } else if (key == "ordre") {
        const std::string l = lower(v);
        next.wordOrder = l.find("fort") != std::string::npos ? "fort" : l.find("faible") != std::string::npos ? "faible" : "";
        if (next.wordOrder.empty()) return fail("ordre des mots : poids faible d'abord (Schneider), ou poids fort d'abord");
    } else if (key == "ecritures") {
        next.writes = yes(v);
    } else if (key == "simule") {
        next.simulated = yes(v);
    } else if (key == "actif") {
        next.enabled = yes(v);
    } else if (key == "description") {
        next.description = v;
    // ---- lot 17 : le jumeau (1.9 : l'esclave simule lie), les zones
    } else if (key == "jumeau" || key == "esclave_simule") {
        if (!next.modbus() && yes(v)) return fail(next.name + " est un \xC3\xA9quipement Ethernet TCP/IP : pas d'esclave simul\xC3\xA9 Modbus");
        if (next.simulated && key == "esclave_simule")
            return fail(next.name + " est seulement simul\xC3\xA9 : il est d\xC3\xA9j\xC3\xA0 un esclave simul\xC3\xA9 (d\xC3\xA9" "cochez Seulement simul\xC3\xA9 quand le vrai arrive)");
        // Un clone tout neuf : automatique (le vrai ; l'esclave s'il ne repond pas).
        if (yes(v) && !cur->twin) next.twinAuto = true;
        next.twin = yes(v);
        if (next.twin) next.twinRunning = true;
        label = next.twin ? name + " : cloner en esclave simul\xC3\xA9" : name + " : retirer son esclave simul\xC3\xA9";
    } else if (key == "lecture_appli" || key == "en_simulation") {
        // Ce que l'IHM lit dans l'application ; "en_simulation" (lot 17) : "au jumeau", "au vrai appareil".
        std::optional<hmi::ReadSource> src;
        const std::string l = lower(v);
        if (key == "en_simulation" && (l.find("jumeau") != std::string::npos || l == "true" || l == "1")) src = hmi::ReadSource::Slave;
        else src = hmi::readSourceFrom(v);
        if (!src) return fail("L'IHM lit \xC2\xAB " + v + " \xC2\xBB : le vrai appareil, l'esclave simul\xC3\xA9, ou automatique (vrai, esclave, auto)");
        if (*src != hmi::ReadSource::Real && !next.hasTwin())
            return fail(next.name + " n'a pas d'esclave simul\xC3\xA9 : coche d'abord \xC2\xAB Cloner en esclave simul\xC3\xA9 \xC2\xBB");
        next.twinAuto = *src == hmi::ReadSource::Auto;
        if (*src != hmi::ReadSource::Auto) next.useTwin = *src == hmi::ReadSource::Slave;
        label = name + (*src == hmi::ReadSource::Auto ? " : l'IHM lit automatiquement" : *src == hmi::ReadSource::Slave ? " : l'IHM lit l'esclave simul\xC3\xA9"
                                                                                                                           : " : l'IHM lit le vrai appareil");
    } else if (key == "bascule_apres") {
        if (!number(1, 3600, next.fallbackAfterS, "Basculer apr\xC3\xA8s (s)")) return false;
        label = name + " : basculer apr\xC3\xA8s " + std::to_string(next.fallbackAfterS) + " s";
    } else if (key == "bascule_retour") {
        const std::string l = lower(v);
        if (l.rfind("non", 0) == 0 || l == "false" || l == "0" || l == "faux" || l.find("jusqu") != std::string::npos) next.fallbackReturn = false;
        else if (l.find("d\xC3\xA8s") != std::string::npos || l.rfind("des", 0) == 0 || l == "oui" || l == "true" || l == "1" || l == "vrai"
                 || l.find("r\xC3\xA9pond") != std::string::npos)
            next.fallbackReturn = true;
        else return fail("Revenir au vrai \xC2\xAB " + v + " \xC2\xBB : d\xC3\xA8s qu'il r\xC3\xA9pond, ou non (l'esclave jusqu'au red\xC3\xA9marrage)");
        label = name + (next.fallbackReturn ? " : revenir au vrai d\xC3\xA8s qu'il r\xC3\xA9pond" : " : rester sur l'esclave jusqu'au red\xC3\xA9marrage");
    } else if (key == "lecture_poste") {
        const auto sr = hmi::stationReadFrom(v);
        if (!sr) return fail("Sur le poste d'exploitation \xC2\xAB " + v + " \xC2\xBB : toujours le vrai appareil, automatique, ou au choix d'un administrateur");
        next.stationRead = *sr;
        label = name + " : sur le poste, " + std::string(hmi::stationReadLabel(*sr));
    } else if (key == "jumeau_nom") {
        // Le nom par defaut ("<nom> - esclave simule", "<nom> (virtuel)" du lot 17) : vide.
        next.twinName = v.empty() || v == next.name + " \xC2\xB7 esclave simul\xC3\xA9" || v == next.name + " (virtuel)" ? std::string{} : v;
    } else if (key == "jumeau_hote") {
        std::string h = v;
        if (const auto sp = h.find(" ("); sp != std::string::npos) h = h.substr(0, sp);
        std::uint32_t ip = 0;
        if (!h.empty() && !eq::parseIpv4(h, ip)) return fail("adresse simul\xC3\xA9" "e \xC2\xAB " + v + " \xC2\xBB : une adresse IPv4 (vide : la m\xC3\xAAme que le vrai)");
        next.twinHost = h.empty() || h == next.host ? std::string{} : eq::ipv4Text(ip);
    } else if (key == "jumeau_port") {
        if (!number(1, 65535, next.twinPort, "Port de l'esclave")) return false;
    } else if (key == "jumeau_marche") {
        next.twinRunning = yes(v);
    } else if (key == "jumeau_depart") {
        const std::string l = lower(v);
        next.twinStart = l.find("z") == 0 || l.find("zero") != std::string::npos || l.find("z\xC3\xA9ro") != std::string::npos ? hmi::TwinStart::Zeros
                         : l.find("gard") != std::string::npos ? hmi::TwinStart::Saved : hmi::TwinStart::Initial;
    } else if (key == "jumeau_delai") {
        if (!number(0, 10000, next.twinDelayMs, "Temps de r\xC3\xA9ponse")) return false;
    } else if (key == "jumeau_gigue") {
        if (!number(0, 5000, next.twinJitterMs, "Plus ou moins")) return false;
    } else if (key == "jumeau_repond") {
        next.twinResponds = yes(v);
    } else if (key == "jumeau_exception") {
        const std::string l = lower(v);
        int code = 0;
        if (l.empty() || l.rfind("aucune", 0) == 0) code = 0;
        else if (l.rfind("0b", 0) == 0) code = 11;
        else code = std::atoi(l.c_str());
        if (code != 0 && code != 1 && code != 2 && code != 3 && code != 4 && code != 6 && code != 11) return fail("exception : aucune, 01, 02, 03, 04, 06 ou 0B");
        next.twinException = code;
    } else if (key == "jumeau_ping") {
        next.twinPing = yes(v);
    } else if (key == "jumeau_visible") {
        next.twinExpose = yes(v);
    } else if (key == "jumeau_port_visible") {
        if (!number(1, 65535, next.twinExposePort, "Port visible")) return false;
    } else if (key == "zones_modele") {
        return applyZonePreset(name, v, why);
    } else if (key.rfind("zones_", 0) == 0) {
        const auto t = hmi::zones::tableFrom(key.substr(6));
        if (!t) return fail("table inconnue : " + key.substr(6));
        return setZones(name, *t, v, why);
    } else {
        return fail("champ inconnu : " + key);
    }
    const std::string oldName = cur->name;
    if (next == *cur) return true;                  // rien ne change (la meme valeur retapee)
    if (!changeProject(label, [&](hmi::Project& x) {
            for (auto& e : x.equipments)
                if (e.name == oldName) e = next;
            // Un nouveau nom : ses variables liees le suivent.
            if (next.name != oldName)
                for (auto& var : x.programs.variables)
                    if (same(var.equipment, oldName)) var.equipment = next.name;
        }))
        return false;
    if (equipment_ == oldName) equipment_ = next.name;
    if (key == "simule")
        say(next.simulated ? next.name + " est seulement simul\xC3\xA9 : son esclave tient sa place, partout (ses zones, ses comportements : sa fiche et la carte m\xC3\xA9moire)."
                           : next.name + " : l'IHM le lit \xC3\xA0 " + next.host + ":" + std::to_string(next.port) + ".");
    if ((key == "jumeau" || key == "esclave_simule") && next.twin != cur->twin) {
        if (next.twin)
            say(next.name + " a son esclave simul\xC3\xA9 : \xC2\xAB " + next.twinLabel() + " \xC2\xBB, sa ligne sous le vrai (au cadenas). L'IHM lit "
                + std::string(hmi::readSourceLabel(next.appRead())) + (next.twinAuto ? " (bascule apr\xC3\xA8s " + std::to_string(next.fallbackAfterS) + " s)" : std::string{}) + ".");
        else
            say(next.name + " n'a plus d'esclave simul\xC3\xA9 : Ctrl+Z le rend, avec ses valeurs anim\xC3\xA9" "es et forc\xC3\xA9" "es.");
    }
    if (key == "lecture_appli" || key == "en_simulation") {
        const auto src = next.appRead();
        say(src == hmi::ReadSource::Auto ? next.name + " : l'IHM lit automatiquement - le vrai ; l'esclave simul\xC3\xA9 s'il ne r\xC3\xA9pond pas pendant "
                                               + std::to_string(next.fallbackAfterS) + " s."
            : src == hmi::ReadSource::Slave ? next.name + " : l'IHM lit son esclave simul\xC3\xA9 (dans l'application ; sur le poste : "
                                                  + std::string(hmi::stationReadLabel(next.stationRead)) + ")."
                                            : next.name + " : l'IHM lit le vrai appareil (" + next.host + ").");
    }
    // Plus d'esclave : sa ligne disparait, la fiche revient au vrai.
    if (!next.linkedSlave() && slaveRow_ && same(equipment_, next.name)) slaveRow_ = false;
    rebuildProperties();
    return true;
}

bool HmiCommPane::testEquipment(const std::string& name) {
    if (name == kPlcKey) return test();
    const auto* e = doc_->project.equipmentByName(name);
    if (!e) {
        say("pas d'\xC3\xA9quipement " + name, true);
        return false;
    }
    std::vector<std::pair<std::string, int>> lines;
    const std::string label = e->name + " (" + std::string(hmi::equipmentTypeLabel(e->type)) + ")";
    lines.push_back({"Essai de " + label, 4});
    bool ok = false;
    if (!e->enabled) lines.push_back({"L'\xC3\xA9quipement est d\xC3\xA9sactiv\xC3\xA9 : l'IHM ne le lit pas (cochez Actif).", 2});
    const bool sim = e->simulated && e->modbus();
    std::string hostName = e->host;
    int port = e->port;
    if (sim) {
        hostName = "127.0.0.1";
        port = host() ? host()->simulatedPort(e->name) : 0;
        lines.push_back({"Simul\xC3\xA9 : le serveur Modbus de ce PC, " + hostName + ":" + std::to_string(port), 4});
        if (port <= 0) {
            lines.push_back({"Le serveur simul\xC3\xA9 ne tourne pas (l'\xC3\xA9quipement est-il actif ?).", 3});
            showReport(lines, false);
            say("Essai de " + e->name + " : le serveur simul\xC3\xA9 ne tourne pas.", true);
            return false;
        }
    } else {
        // Le reseau d'abord : un port du PC dans le sien ?
        std::uint32_t ip = 0;
        if (eq::parseIpv4(e->host, ip) && host()) {
            std::vector<eq::Network> nets;
            const auto list = host()->adapters();
            for (std::size_t i = 0; i < list.size(); ++i)
                for (const auto& [a, pre] : list[i].addresses) nets.push_back({a, pre, static_cast<int>(i)});
            const int k = eq::networkFor(ip, nets);
            if (k >= 0) {
                const auto& ad = list[static_cast<std::size_t>(nets[static_cast<std::size_t>(k)].adapter)];
                lines.push_back({"R\xC3\xA9seau : " + eq::networkText(nets[static_cast<std::size_t>(k)].ip, nets[static_cast<std::size_t>(k)].prefix)
                                     + ", par le port " + ad.name + " (" + eq::ipv4Text(ad.ip()) + ")" + (ad.up ? std::string{} : " - c\xC3\xA2" "ble d\xC3\xA9" "branch\xC3\xA9 !"),
                                 ad.up ? 1 : 3});
            } else if (!loopback(e->host)) {
                lines.push_back({"R\xC3\xA9seau : aucun port du PC n'est dans le r\xC3\xA9seau de " + e->host + " (onglet R\xC3\xA9seau du PC : donnez une adresse \xC3\xA0 un port libre).",
                                 3});
            }
        }
        const auto pg = hmi::netinfo::ping(e->host, e->timeoutMs);
        lines.push_back({"Ping " + e->host + " : " + (pg.ok ? msText(pg.ms) : pg.why), pg.ok ? 1 : 2});
        if (!e->modbus()) {
            const auto t = hmi::netinfo::probeTcp(e->host, e->port, e->timeoutMs);
            lines.push_back({"Port " + std::to_string(e->port) + " : " + (t.ok ? "ouvert (" + msText(t.ms) + ")" : t.why), t.ok ? 1 : 2});
            ok = pg.ok || t.ok;
            lines.push_back({ok ? e->name + " est joignable." : e->name + " ne r\xC3\xA9pond ni au ping ni au port " + std::to_string(e->port)
                                                                   + " : l'adresse, le c\xC3\xA2" "ble, le r\xC3\xA9seau ?",
                             ok ? 1 : 3});
            showReport(lines, ok);
            say(ok ? "Essai r\xC3\xA9ussi : " + e->name + " r\xC3\xA9pond." : "Essai en \xC3\xA9" "chec : " + e->name + " ne r\xC3\xA9pond pas.", !ok);
            if (host()) host()->test(e->name);
            return ok;
        }
    }
    // Modbus : connexion, identification, lecture de ses variables.
    const auto plan = eq::buildPlan(doc_->project, *e);
    const auto settings = eq::settingsOf(*e, sim ? port : 0);
    const auto& project = doc_->project;
    const auto modbusLines = CommHost::testLink(
        plan, settings, "Variables li\xC3\xA9" "es : " + std::to_string(plan.points().size()) + " ; " + std::to_string(plan.refused().size()) + " sans place",
        [&project](const hmi::comm::Point& pt, const sim::Value& raw) {
            const auto* v = project.variable(pt.name);
            if (!v || !v->scaled()) return hmi::formatValue(raw);
            return hmi::formatValue(eq::fromRegister(*v, raw)) + " (brut " + hmi::formatValue(raw) + ")";
        });
    for (const auto& l : modbusLines) {
        lines.push_back({l.text, l.tone});
        ok = ok || (l.tone == 1 && l.text.rfind("Connexion", 0) == 0);
    }
    showReport(lines, ok);
    say(ok ? "Essai r\xC3\xA9ussi : " + e->name + " r\xC3\xA9pond en Modbus TCP." : "Essai en \xC3\xA9" "chec : " + e->name + " (voir le compte rendu)", !ok);
    if (host()) {
        host()->test(e->name);
        host()->reconnect(e->name);
    }
    return ok;
}

void HmiCommPane::testAll() {
    auto* h = host();
    if (!h) {
        say("Tester : les \xC3\xA9quipements ne sont pas suivis ici.", true);
        return;
    }
    h->test();
    std::size_t n = 0;
    for (const auto& e : doc_->project.equipments) n += e.enabled && !(e.simulated && e.modbus()) ? 1 : 0;
    say("Tester tous les \xC3\xA9quipements : " + std::to_string(n) + " ping(s) lanc\xC3\xA9(s) ; les r\xC3\xA9sultats arrivent sur le sch\xC3\xA9ma et dans \xC3\x89tat des liaisons.");
}

void HmiCommPane::rebuildEquipmentProperties(std::vector<PG::Category>& cats) {
    const auto* e = doc_->project.equipmentByName(equipment_);
    if (!e) return;
    // 1.9 : la ligne de l'esclave simule lie - sa fiche a lui.
    if (slaveRow_ && e->linkedSlave()) {
        rebuildSlaveProperties(cats, *e);
        return;
    }
    const std::string name = e->name;
    const auto field = [this, name](const char* key) {
        return [this, name, key](std::string_view v) {
            message_.clear();
            return setEquipmentField(equipment_.empty() ? name : equipment_, key, std::string(v));
        };
    };
    // 1.9 : la fiche - l'equipement, le vrai appareil, son esclave simule, ce que
    // l'IHM lit, puis la liaison (repliee), les zones memoire, l'etat.
    PG::Category main;
    main.name = "\xC3\x89quipement";
    main.properties.push_back(prop("Nom", e->name, PG::ValueType::Text, field("nom"), "Le nom que citent les variables li\xC3\xA9" "es et IHM_EQUIPEMENT_OK('...')."));
    main.properties.push_back(prop("Type", std::string(hmi::equipmentTypeLabel(e->type)), PG::ValueType::Enum, field("type"),
                                   "Modbus TCP/IP : l'IHM lit et \xC3\xA9" "crit ses registres (une centrale de mesure, un variateur...). Ethernet TCP/IP : "
                                   "un appareil IP que l'IHM surveille (ping, port) - une cam\xC3\xA9ra, une imprimante, un switch. Modbus RTU viendra plus tard.",
                                   hmi::equipmentTypeLabels()));
    if (e->modbus()) {
        main.properties.push_back(prop("Esclave (Unit Id)", std::to_string(e->unit), PG::ValueType::Integer, field("esclave"),
                                       "1 le plus souvent ; 255 pour l'UC d'un automate Schneider ; derri\xC3\xA8re une passerelle Modbus, le num\xC3\xA9ro de l'esclave s\xC3\xA9rie."));
        main.properties.push_back(prop("Zones m\xC3\xA9moire", e->zones.declared ? hmi::zones::summary(e->zones) : std::string("non d\xC3\xA9" "clar\xC3\xA9" "es"), PG::ValueType::ReadOnly, {},
                                       "Ce qu'il a dans sa m\xC3\xA9moire (plus bas : Zones m\xC3\xA9moire) ; le vrai appareil et son esclave simul\xC3\xA9 ont les m\xC3\xAAmes."));
    }
    main.properties.push_back(prop("Description", e->description, PG::ValueType::Text, field("description")));
    cats.push_back(std::move(main));
    // ---- le vrai appareil
    auto* h = host();
    const auto st = h ? h->status(e->name) : std::nullopt;
    PG::Category real;
    real.name = "Le vrai appareil";
    const auto onlySimulated = [&] {
        return prop("Seulement simul\xC3\xA9", tf(e->simulated), PG::ValueType::Boolean, field("simule"),
                    "Coch\xC3\xA9 : pas encore d'appareil - seul son esclave simul\xC3\xA9 existe, partout (aussi sur le poste d'exploitation). "
                    "Sa vraie adresse viendra plus tard.");
    };
    if (e->simulated && e->modbus()) real.properties.push_back(onlySimulated());
    if (!e->simulated) {
        real.properties.push_back(prop("Adresse IP", e->host, PG::ValueType::Text, field("hote"),
                                       "Son adresse sur le r\xC3\xA9seau : 192.168.1.30. Le sch\xC3\xA9ma du r\xC3\xA9seau dit quel port du PC la joint (on peut l'y glisser)."));
        real.properties.push_back(prop("Port", std::to_string(e->port), PG::ValueType::Integer, field("port"),
                                       e->modbus() ? "502 : le port Modbus TCP." : "Le port qu'on essaie quand le ping ne r\xC3\xA9pond pas (80 web, 554 cam\xC3\xA9ra, 9100 imprimante)."));
        real.properties.push_back(prop("Actif", tf(e->enabled), PG::ValueType::Boolean, field("actif"),
                                       "D\xC3\xA9" "coch\xC3\xA9 : l'IHM ne le lit plus, ne le ping plus ; ses variables sont mauvaises."));
        real.properties.push_back(prop("Ping toutes les (s)", std::to_string(e->pingS), PG::ValueType::Integer, field("ping"), "0 : jamais (Tester le fait \xC3\xA0 la demande)."));
        if (st) {
            // 1.9 : pendant la bascule, le vrai se tait depuis... (ou repond de nouveau, au dernier essai).
            std::string state = !st->realState.empty() ? st->realState : st->state;
            int tone = st->realTone;
            if (st->fallback) {
                state = st->realOnline ? std::string("r\xC3\xA9pond \xC3\xA0 nouveau (essai)")
                                       : "injoignable" + (st->realSilentSince > 0 ? " depuis " + clockOf(st->realSilentSince) : std::string{});
                tone = st->realOnline ? 1 : 3;
            } else if (st->realSilentSince > 0 && !st->realOnline) {
                state = "injoignable depuis " + clockOf(st->realSilentSince);
                tone = 3;
            }
            auto row = prop("\xC3\x89tat", state, PG::ValueType::ReadOnly);
            row.valueTone = tone == 1 ? ui::Tone::Ok : tone == 3 ? ui::Tone::Error : ui::Tone::None;
            real.properties.push_back(std::move(row));
            // Par l'esclave, "pourquoi" parle de l'esclave (sa panne simulee) : sa fiche le dit.
            if (!st->why.empty() && !st->viaTwin) real.properties.push_back(prop("Pourquoi", st->why, PG::ValueType::ReadOnly));
        }
        if (const auto pr = h ? h->probe(e->name) : std::nullopt; pr && pr->done)
            real.properties.push_back(prop("Dernier ping", (pr->ok ? msText(pr->ms) : pr->why) + (pr->portTried ? std::string(" \xC2\xB7 port ") + (pr->portOk ? "ouvert" : "ferm\xC3\xA9") : std::string{}),
                                           PG::ValueType::ReadOnly));
        if (e->modbus()) real.properties.push_back(onlySimulated());
    } else {
        real.properties.push_back(prop("Adresse pr\xC3\xA9vue", e->host, PG::ValueType::Text, field("hote"),
                                       "L'adresse qu'aura le vrai appareil (d\xC3\xA9" "cochez Seulement simul\xC3\xA9 quand il arrive) ; son esclave r\xC3\xA9pond \xC3\xA0 la m\xC3\xAAme."));
    }
    cats.push_back(std::move(real));
    if (e->modbus()) {
        // ---- son esclave simule (1.9), ce que l'IHM lit
        if (!e->simulated) {
            rebuildCloneProperties(cats, *e);
            if (e->twin) rebuildReadProperties(cats, *e);
        } else {
            rebuildOwnSlaveProperties(cats, *e, "Son esclave simul\xC3\xA9");
            rebuildNowProperties(cats, *e);
        }
        PG::Category link;
        link.name = "Liaison";
        link.properties.push_back(prop("D\xC3\xA9lai de r\xC3\xA9ponse (ms)", std::to_string(e->timeoutMs), PG::ValueType::Integer, field("delai"),
                                       "Au-del\xC3\xA0, la requ\xC3\xAAte est perdue ; deux de suite : la liaison est coup\xC3\xA9" "e, puis refaite."));
        link.properties.push_back(prop("P\xC3\xA9riode de scrutation (ms)", std::to_string(e->periodMs), PG::ValueType::Integer, field("periode")));
        link.properties.push_back(prop("Nouvel essai (s)", std::to_string(e->retryS), PG::ValueType::Integer, field("reessai")));
        link.properties.push_back(prop("Mauvaise apr\xC3\xA8s (s)", std::to_string(e->badAfterS), PG::ValueType::Integer, field("mauvaise")));
        link.properties.push_back(prop("\xC3\x89" "crire dans l'\xC3\xA9quipement", tf(e->writes), PG::ValueType::Boolean, field("ecritures"),
                                       "D\xC3\xA9" "coch\xC3\xA9 : lecture seule - l'IHM ne change rien dans l'\xC3\xA9quipement."));
        link.properties.push_back(prop("Ordre des mots (32 bits)", e->wordOrder == "fort" ? "poids fort d'abord" : "poids faible d'abord (Schneider)",
                                       PG::ValueType::Enum, field("ordre"),
                                       "Un REAL, un DINT tiennent deux registres : Schneider met le poids faible d'abord, beaucoup d'autres le poids fort.",
                                       {"poids faible d'abord (Schneider)", "poids fort d'abord"}));
        link.properties.push_back(prop("Mots par requ\xC3\xAAte", std::to_string(e->maxWords), PG::ValueType::Integer, field("mots"), "125 au plus (la norme)."));
        link.properties.push_back(prop("Regrouper \xC3\xA0 moins de (mots)", std::to_string(e->gap), PG::ValueType::Integer, field("ecart")));
        link.expanded = false;
        cats.push_back(std::move(link));
        // Les zones : le vrai appareil et son esclave ont les memes.
        rebuildZoneProperties(cats, *e);
    }
    // L'etat, en marche : la liaison de l'IHM (au vrai, ou a l'esclave).
    PG::Category state;
    state.name = "\xC3\x89tat";
    if (st) {
        state.properties.push_back(prop("\xC3\x89tat", st->state, PG::ValueType::ReadOnly));
        state.properties.push_back(prop("Joint par", st->address, PG::ValueType::ReadOnly));
    }
    if (const auto* lk = h ? h->link(e->name) : nullptr) {
        const auto d = lk->diagnostics();
        state.properties.push_back(prop("Temps de r\xC3\xA9ponse", msText(d.avgMs) + " (max " + msText(d.maxMs) + ")", PG::ValueType::ReadOnly));
        state.properties.push_back(prop("Requ\xC3\xAAtes", std::to_string(d.requests) + ", " + std::to_string(d.errors) + " erreur(s)", PG::ValueType::ReadOnly));
        if (!d.device.empty()) state.properties.push_back(prop("Identification", d.device, PG::ValueType::ReadOnly));
    }
    if (e->modbus()) state.properties.push_back(prop("Variables li\xC3\xA9" "es", std::to_string(eq::boundVariables(doc_->project, *e).size()), PG::ValueType::ReadOnly));
    if (!state.properties.empty()) cats.push_back(std::move(state));
    sheetNote_ = e->linkedSlave() ? "\xC2\xAB Dans l'application \xC2\xBB : le vrai appareil, l'esclave simul\xC3\xA9, ou automatique (le vrai ; l'esclave simul\xC3\xA9 "
                                    "s'il ne r\xC3\xA9pond pas pendant " + std::to_string(e->fallbackAfterS) + " s, le vrai d\xC3\xA8s qu'il r\xC3\xA9pond de nouveau)."
                                  : std::string{};
}

void HmiCommPane::rebuildZoneProperties(std::vector<PG::Category>& cats, const hmi::Equipment& e) {
    const std::string name = e.name;
    PG::Category z;
    z.name = "Zones m\xC3\xA9moire";
    const auto [bits, words] = plcMemory();
    const auto presets = hmi::zones::presets(doc_->project, e, bits, words);
    // Les modeles (le dernier : non declarees) - une seule fois chacun.
    std::vector<std::string> labels;
    for (const auto& pr : presets) labels.push_back(pr.label);
    z.properties.push_back(prop("Mod\xC3\xA8le", "choisir\xE2\x80\xA6", PG::ValueType::Enum,
                                [this, name, presets](std::string_view v) {
                                    message_.clear();
                                    if (v.rfind("non d", 0) == 0) return applyZonePreset(name, "non");
                                    for (const auto& pr : presets)
                                        if (pr.label == v) return applyZonePreset(name, pr.key);
                                    return false;
                                },
                                "Pour aller vite : les plages de ses variables (\xC3\xA0 la centaine), la configuration de l'automate du projet, celles d'un autre "
                                "\xC3\xA9quipement, tout (0-65535), les registres seulement... Ou D\xC3\xA9tecter les zones (barre du haut).",
                                labels));
    for (const auto t : {hmi::MemTable::Holding, hmi::MemTable::InputRegisters, hmi::MemTable::Coils, hmi::MemTable::DiscreteInputs}) {
        const std::string label = std::string(hmi::zones::tableLabel(t)) + " (" + std::string(hmi::zones::tableModicon(t)) + ")";
        z.properties.push_back(prop(label, e.zones.declared ? hmi::zones::rangesText(e.zones.of(t)) : std::string{}, PG::ValueType::Text,
                                    [this, name, t](std::string_view v) {
                                        message_.clear();
                                        return setZones(name, t, std::string(v));
                                    },
                                    "0-99; 1000-1049 (\xC3\xA0 partir de 0), 40001-40100 (Modicon), %MW0-%MW99 (Schneider), aucune (la table n'existe pas)."));
    }
    z.properties.push_back(prop("Origine", e.zones.declared ? (e.zones.origin.empty() ? std::string("saisies") : e.zones.origin) : std::string("non d\xC3\xA9" "clar\xC3\xA9" "es : aucune v\xC3\xA9rification"),
                                PG::ValueType::ReadOnly));
    cats.push_back(std::move(z));
}

// ========================================================= variables liees ===
void HmiCommPane::refreshBound() {
    const auto& p = doc_->project;
    auto* h = host();
    boundOrder_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (const auto& v : p.programs.variables) {
        if (!v.bound()) continue;
        boundOrder_.push_back(v.name);
        hmi::comm::Point pt;
        std::string reason, place;
        int tone = 0;
        const bool composite = hmi::types::isComposite(v.type);          // lot 16 : une structure, un tableau
        if (composite) {
            place = hmi::types::spanText(p, v);
            pt.writable = !hmi::types::bitArea(v.address) || v.address.empty() || (v.address.front() != '1' && v.address.find("DI") == std::string::npos
                                                                                  && v.address.rfind("%I", 0) != 0);
            if (place.empty()) {
                place = "sans place (adresse de d\xC3\xA9part illisible)";
                tone = 3;
            }
        } else if (eq::placeEquipmentAddress(v.address, eq::registerType(v), pt, &reason)) place = pt.placeText() + " \xC2\xB7 " + eq::modiconText(pt);
        else {
            place = reason.empty() ? std::string("adresse illisible") : reason;
            tone = 3;
        }
        const auto* e = p.equipmentByName(v.equipment);
        if (!e) {
            place = "\xC3\xA9quipement inconnu";
            tone = 3;
        }
        std::string value = "\xE2\x80\x94", quality = "\xE2\x80\x94";
        if (auto* lk = h && e && composite ? h->link(e->name) : nullptr) {
            // Lot 16 : la pire des cases lues ; la valeur : le nombre de cases.
            const auto leaves = hmi::types::leafVariables(p, v);
            value = std::to_string(leaves.size()) + " cases";
            int worst = 0;
            bool read = false;
            for (const auto& lf : leaves) {
                std::string why;
                const auto q = lk->quality(lf.name, &why);
                const int t2 = q == hmi::comm::Quality::Good ? 1 : q == hmi::comm::Quality::Stale ? 2 : q == hmi::comm::Quality::Bad ? 3 : 0;
                if (t2) read = true;
                if (t2 > worst) {
                    worst = t2;
                    quality = t2 == 1 ? std::string("bonne") : std::string(hmi::comm::qualityName(q)) + " : " + lf.name + (why.empty() ? std::string{} : " : " + why);
                }
            }
            if (!read) quality = "pas encore lue";
            if (tone != 3) tone = worst;
        } else if (auto* single = h && e ? h->link(e->name) : nullptr) {
            std::string why;
            const auto q = single->quality(v.name, &why);
            quality = std::string(hmi::comm::qualityName(q)) + (why.empty() ? std::string{} : " : " + why);
            if (q == hmi::comm::Quality::Pending) quality = "pas encore lue";
            sim::Value raw;
            if (single->read(v.name, raw) && (q == hmi::comm::Quality::Good || q == hmi::comm::Quality::Stale)) value = hmi::formatValue(eq::fromRegister(v, raw));
            if (tone != 3) tone = q == hmi::comm::Quality::Good ? 1 : q == hmi::comm::Quality::Stale ? 2 : q == hmi::comm::Quality::Bad ? 3 : 0;
        } else if (e && !e->enabled) {
            quality = "\xC3\xA9quipement d\xC3\xA9sactiv\xC3\xA9";
        }
        const std::string scale = v.scaled() ? numberText(v.rawMin) + ".." + numberText(v.rawMax) + " \xE2\x86\x92 " + numberText(v.engMin) + ".." + numberText(v.engMax)
                                             : std::string("\xE2\x80\x94");
        const bool writable = !v.readOnly && e && e->writes && pt.writable;
        rows.push_back({v.name, v.equipment, v.address, place, v.type, scale, writable ? "lecture, \xC3\xA9" "criture" : "lecture seule", value, quality});
        tones.push_back(tone);
    }
    boundModel_ = std::make_shared<Rows>(std::vector<std::string>{"Variable", "\xC3\x89quipement", "Adresse", "Place Modbus", "Type", "Mise \xC3\xA0 l'\xC3\xA9" "chelle",
                                                                  "Acc\xC3\xA8s", "Valeur", "Qualit\xC3\xA9"},
                                         std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle st;
                                             if (r >= tones.size()) return st;
                                             if (c == 0) {
                                                 st.bold = true;
                                                 st.icon = ui::Icon::LocatedVariable;
                                                 st.iconTone = toneOf(tones[r]);
                                             }
                                             if (c == 8 || (c == 3 && tones[r] == 3)) st.fgTone = toneOf(tones[r]);
                                             return st;
                                         });
    const bool was = refreshing_;
    refreshing_ = true;
    boundTable_->setModel(boundModel_);
    for (std::size_t i = 0; i < boundOrder_.size(); ++i)
        if (!bound_.empty() && same(boundOrder_[i], bound_)) boundTable_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    if (bound_.empty() && !boundOrder_.empty()) {
        bound_ = boundOrder_.front();
        boundTable_->selectModelRows({0}, false);
    }
    refreshing_ = was;
    tabs_->setTabBadge(TBound, std::to_string(boundOrder_.size()));
}

void HmiCommPane::selectBound(const std::string& variable) {
    bound_ = variable;
    if (const auto* v = doc_->project.variable(variable)) bound_ = v->name;
    refreshBound();
    rebuildProperties();
}

bool HmiCommPane::bindVariable(const std::string& rawVariable, const std::string& equipment, const std::string& rawAddress, const std::string& rawType,
                               std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& p = doc_->project;
    const std::string name = trimmed(rawVariable);
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return fail("variable \xC2\xAB " + name + " \xC2\xBB : un nom (Tension_L1)");
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return fail("variable \xC2\xAB " + name + " \xC2\xBB : lettres, chiffres et _ seulement");
    const auto* e = p.equipmentByName(equipment);
    if (!e) return fail(equipment.empty() ? std::string("ajoutez d'abord un \xC3\xA9quipement Modbus TCP/IP") : "pas d'\xC3\xA9quipement " + equipment);
    if (!e->modbus()) return fail(e->name + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas de variables (il se surveille par ping)");
    const auto* existing = p.variable(name);
    std::string type = upper(trimmed(rawType));
    if (type.empty()) type = existing ? existing->type : "INT";
    if (std::find(std::begin(hmi::kVariableTypes), std::end(hmi::kVariableTypes), type) == std::end(hmi::kVariableTypes))
        return fail("type \xC2\xAB " + type + " \xC2\xBB : INT, REAL, BOOL, DINT...");
    std::string address = trimmed(rawAddress);
    if (address.empty()) address = eq::nextFreeAddress(p, *e, eq::typeOfName(type));
    hmi::Variable probeVar = existing ? *existing : hmi::Variable{};
    probeVar.name = name;
    probeVar.type = type;
    probeVar.equipment = e->name;
    probeVar.address = address;
    hmi::comm::Point pt;
    std::string reason;
    if (!eq::placeEquipmentAddress(address, eq::registerType(probeVar), pt, &reason))
        return fail("adresse \xC2\xAB " + address + " \xC2\xBB : " + (reason.empty() ? std::string("illisible") : reason));
    const std::string eqName = e->name;
    if (!changeProject((existing ? "Lier " : "Cr\xC3\xA9" "er et lier ") + name + " \xC3\xA0 " + eqName, [&](hmi::Project& x) {
            for (auto& v : x.programs.variables)
                if (same(v.name, name)) {
                    v.equipment = eqName;
                    v.address = address;
                    v.type = type;
                    return;
                }
            hmi::Variable v;
            v.id = x.allocate();
            v.name = name;
            v.type = type;
            v.initial = type == "BOOL" ? "FALSE" : "0";
            v.equipment = eqName;
            v.address = address;
            x.programs.variables.push_back(std::move(v));
        }))
        return false;
    bound_ = name;
    tabs_->setCurrentIndex(TBound);
    refreshBound();
    rebuildProperties();
    say(name + " li\xC3\xA9" "e \xC3\xA0 " + eqName + " : " + address + " (" + eq::modiconText(pt) + ")");
    return true;
}

bool HmiCommPane::unbindVariable(const std::string& variable, std::string* why) {
    const auto* v = doc_->project.variable(variable);
    if (!v || !v->bound()) {
        const std::string m = variable + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    const std::string name = v->name;
    if (!changeProject("D\xC3\xA9lier " + name, [&](hmi::Project& x) {
            for (auto& var : x.programs.variables)
                if (var.name == name) {
                    var.equipment.clear();
                    var.address.clear();
                    var.readOnly = false;
                    var.rawMin = var.rawMax = var.engMin = var.engMax = 0;
                    var.rawType.clear();
                }
        }))
        return false;
    if (bound_ == name) bound_.clear();
    say(name + " d\xC3\xA9li\xC3\xA9" "e : une variable IHM locale (Ctrl+Z la relie).");
    return true;
}

bool HmiCommPane::setBoundField(const std::string& variable, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto* cur = doc_->project.variable(variable);
    if (!cur) return fail("pas de variable " + variable);
    hmi::Variable next = *cur;
    const std::string v = trimmed(raw);
    const auto real = [&](double& out, const char* label) {
        char* end = nullptr;
        const double d = std::strtod(v.c_str(), &end);
        if (v.empty() || (end && *end) || !std::isfinite(d)) return fail(std::string(label) + " : un nombre");
        out = d;
        return true;
    };
    if (key == "equipement") {
        const auto* e = doc_->project.equipmentByName(v);
        if (!e) return fail("pas d'\xC3\xA9quipement " + v);
        if (!e->modbus()) return fail(e->name + " est un \xC3\xA9quipement Ethernet TCP/IP : pas de variables");
        next.equipment = e->name;
    } else if (key == "adresse") {
        const std::string canon = eq::canonicalAddress(v, nullptr);
        hmi::comm::Point pt;
        std::string reason;
        next.address = v;
        if (!eq::placeEquipmentAddress(v, eq::registerType(next), pt, &reason))
            return fail("adresse \xC2\xAB " + v + " \xC2\xBB : " + (reason.empty() ? std::string("illisible") : reason));
        (void)canon;
    } else if (key == "type") {
        const std::string t = upper(v);
        if (std::find(std::begin(hmi::kVariableTypes), std::end(hmi::kVariableTypes), t) == std::end(hmi::kVariableTypes)) return fail("type \xC2\xAB " + v + " \xC2\xBB inconnu");
        next.type = t;
        hmi::comm::Point pt;
        std::string reason;
        if (!next.address.empty() && !eq::placeEquipmentAddress(next.address, eq::registerType(next), pt, &reason))
            return fail("le type " + t + " ne tient pas \xC3\xA0 " + next.address + " : " + reason);
    } else if (key == "lecture_seule") {
        next.readOnly = yes(v);
    } else if (key == "brut_min") {
        if (!real(next.rawMin, "Brut min")) return false;
    } else if (key == "brut_max") {
        if (!real(next.rawMax, "Brut max")) return false;
    } else if (key == "echelle_min") {
        if (!real(next.engMin, "\xC3\x89" "chelle min")) return false;
    } else if (key == "echelle_max") {
        if (!real(next.engMax, "\xC3\x89" "chelle max")) return false;
    } else if (key == "type_brut") {
        const std::string t = upper(v);
        if (!t.empty() && t != "INT" && t != "UINT" && t != "WORD" && t != "DINT" && t != "UDINT" && t != "DWORD" && t != "REAL")
            return fail("type du registre : INT, UINT, WORD, DINT, UDINT, DWORD, REAL");
        next.rawType = t;
    } else if (key == "description") {
        next.description = v;
    } else {
        return fail("champ inconnu : " + key);
    }
    const std::string name = cur->name;
    if (!changeProject(name + " : " + key, [&](hmi::Project& x) {
            for (auto& var : x.programs.variables)
                if (var.name == name) var = next;
        }))
        return false;
    rebuildProperties();
    return true;
}

void HmiCommPane::rebuildBoundProperties(std::vector<PG::Category>& cats) {
    const auto* v = doc_->project.variable(bound_);
    // Lot 16 : choisie dans le plan d'adressage - ce qu'elle est, en toutes lettres.
    if (const auto* r = static_cast<int>(tabs_->currentIndex()) == TPlan ? selectedPlanRow() : nullptr; r && v) {
        PG::Category c;
        c.name = "Variable choisie (plan d'adressage)";
        c.properties.push_back(prop(r->kind == PlanRow::Kind::Member ? "Case" : "Variable", r->name, PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Nature", "variable IHM (pas l'automate du projet)", PG::ValueType::ReadOnly, {},
                                    "Elle vit dans l'IHM (Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM) ; sa valeur vient de l'\xC3\xA9quipement."));
        c.properties.push_back(prop("Dossier", v->folder.empty() ? std::string("(racine)") : v->folder, PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Type", r->kind == PlanRow::Kind::Member ? hmi::types::typeOfPath(doc_->project, r->name) : v->type, PG::ValueType::ReadOnly));
        if (const auto* e = doc_->project.equipmentByName(v->equipment))
            c.properties.push_back(prop("Joint par", (e->simulated ? std::string("simul\xC3\xA9") : e->host + ":" + std::to_string(e->port)) + " \xC2\xB7 esclave "
                                                         + std::to_string(e->unit), PG::ValueType::ReadOnly));
        if (r->kind == PlanRow::Kind::Member)
            for (const auto& leaf : hmi::types::leafVariables(doc_->project, *v))
                if (same(leaf.name, r->name)) c.properties.push_back(prop("Adresse de la case", leaf.address, PG::ValueType::ReadOnly));
        if (hmi::types::isComposite(v->type)) c.properties.push_back(prop("Place", hmi::types::spanText(doc_->project, *v), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Ouvrir", "double-clic, ou \xC2\xAB Ouvrir la variable IHM \xC2\xBB (barre du haut)", PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    if (!v || !v->bound()) {
        PG::Category none;
        none.name = "Variables li\xC3\xA9" "es";
        none.properties.push_back(prop("Aucune", "Lier une variable (barre du haut) : une variable IHM lue et \xC3\xA9" "crite dans un \xC3\xA9quipement", PG::ValueType::ReadOnly));
        cats.push_back(std::move(none));
        return;
    }
    const std::string name = v->name;
    const auto field = [this, name](const char* key) {
        return [this, name, key](std::string_view value) {
            message_.clear();
            return setBoundField(name, key, std::string(value));
        };
    };
    std::vector<std::string> equipments;
    for (const auto& e : doc_->project.equipments)
        if (e.modbus()) equipments.push_back(e.name);
    PG::Category main;
    main.name = "Variable li\xC3\xA9" "e";
    main.properties.push_back(prop("Variable", v->name, PG::ValueType::ReadOnly));
    main.properties.push_back(prop("\xC3\x89quipement", v->equipment, PG::ValueType::Enum, field("equipement"), "L'\xC3\xA9quipement Modbus TCP/IP o\xC3\xB9 elle se lit.",
                                   equipments));
    main.properties.push_back(prop("Adresse", v->address, PG::ValueType::Text, field("adresse"),
                                   "Schneider (\xC3\xA0 partir de 0) : %MW100, %MF20 (REAL), %MD20 (DINT), %M5 (bit), %MW10.3 (bit 3), %IW4, %I7. "
                                   "Modicon (\xC3\xA0 partir de 1) : 40101 ou 400101 ou 4x0101 (maintien), 30001 (entr\xC3\xA9" "e), 00017 (bobine), 10005 (entr\xC3\xA9" "e TOR), "
                                   "40101.3 (bit). Ou HR100, IR100, CO100, DI100."));
    const bool composite = hmi::types::isComposite(v->type);
    if (composite) {
        // Lot 16 : une structure ou un tableau - son type se change dans Variables IHM.
        main.properties.push_back(prop("Type", v->type, PG::ValueType::ReadOnly, {},
                                       "Une structure ou un tableau : son type se change dans Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM."));
    } else {
        std::vector<std::string> types;
        for (const auto t : hmi::kVariableTypes) types.emplace_back(t);
        main.properties.push_back(prop("Type", v->type, PG::ValueType::Enum, field("type"), "Le type de la variable IHM (REAL : deux registres).", types));
    }
    main.properties.push_back(prop("Lecture seule", tf(v->readOnly), PG::ValueType::Boolean, field("lecture_seule"), "L'IHM la lit, ne l'\xC3\xA9" "crit jamais."));
    main.properties.push_back(prop("Description", v->description, PG::ValueType::Text, field("description")));
    cats.push_back(std::move(main));
    if (composite) {
        // La place d'une structure : de sa premiere case a sa derniere, ses cases, son poids.
        PG::Category place;
        place.name = "Place (structure ou tableau)";
        std::vector<std::string> why;
        std::string error;
        const auto leaves = hmi::types::leafVariables(doc_->project, *v, nullptr, &why, &error);
        const auto w = hmi::types::weightOf(doc_->project, v->type, v->packBools);
        place.properties.push_back(prop("Place Modbus", hmi::types::spanText(doc_->project, *v), PG::ValueType::ReadOnly));
        place.properties.push_back(prop("Cases", std::to_string(leaves.size()) + " (" + std::to_string(w.words) + " mots, " + std::to_string(w.bytes) + " octets)",
                                        PG::ValueType::ReadOnly));
        std::size_t refused = 0;
        for (const auto& y : why) refused += y.empty() ? 0 : 1;
        if (!error.empty()) place.properties.push_back(prop("Refus\xC3\xA9" "e", error, PG::ValueType::ReadOnly));
        else if (refused > 0) place.properties.push_back(prop("Sans place", std::to_string(refused) + " case(s)", PG::ValueType::ReadOnly));
        std::size_t overrides = 0;
        for (const auto& pl : v->places) overrides += pl.address.empty() ? 0 : 1;
        place.properties.push_back(prop("Membres", overrides == 0 ? std::string("\xC3\xA0 la suite, depuis l'adresse")
                                                                  : std::to_string(overrides) + " adresse(s) corrig\xC3\xA9" "e(s) \xC3\xA0 la main",
                                        PG::ValueType::ReadOnly, {},
                                        "L'adresse de chaque case se voit et se corrige dans Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM (d\xC3\xA9plier la variable)."));
        if (auto* lk = host() ? host()->link(v->equipment) : nullptr) {
            // La pire qualite des cases : une seule mauvaise suffit a le dire.
            int rank = -1;
            std::string text;
            for (const auto& leaf : leaves) {
                std::string y;
                const auto q = lk->quality(leaf.name, &y);
                if (q == hmi::comm::Quality::None) continue;
                const int r = q == hmi::comm::Quality::Bad ? 4 : q == hmi::comm::Quality::Stale ? 3 : q == hmi::comm::Quality::Good ? 2 : 1;
                if (r > rank) {
                    rank = r;
                    text = std::string(hmi::comm::qualityName(q)) + (y.empty() ? std::string{} : " : " + y) + " (" + leaf.name + ")";
                }
            }
            if (rank >= 0) place.properties.push_back(prop("Qualit\xC3\xA9", text, PG::ValueType::ReadOnly));
        }
        cats.push_back(std::move(place));
        return;
    }
    PG::Category scale;
    scale.name = "Mise \xC3\xA0 l'\xC3\xA9" "chelle";
    const char* help = "Le registre brut [brut min, brut max] devient la valeur [\xC3\xA9" "chelle min, \xC3\xA9" "chelle max] : 0..27648 -> 0..10 bar. "
                       "Brut min = brut max : pas de mise \xC3\xA0 l'\xC3\xA9" "chelle.";
    scale.properties.push_back(prop("Brut min", numberText(v->rawMin), PG::ValueType::Real, field("brut_min"), help));
    scale.properties.push_back(prop("Brut max", numberText(v->rawMax), PG::ValueType::Real, field("brut_max"), help));
    scale.properties.push_back(prop("\xC3\x89" "chelle min", numberText(v->engMin), PG::ValueType::Real, field("echelle_min"), help));
    scale.properties.push_back(prop("\xC3\x89" "chelle max", numberText(v->engMax), PG::ValueType::Real, field("echelle_max"), help));
    scale.properties.push_back(prop("Type du registre", v->rawType, PG::ValueType::Enum, field("type_brut"),
                                    "Le type du registre brut (vide : INT, un mot sign\xC3\xA9).", {"", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL"}));
    cats.push_back(std::move(scale));
    PG::Category place;
    place.name = "Place";
    hmi::comm::Point pt;
    std::string reason;
    if (eq::placeEquipmentAddress(v->address, eq::registerType(*v), pt, &reason)) {
        place.properties.push_back(prop("Place Modbus", pt.modbusText(), PG::ValueType::ReadOnly));
        place.properties.push_back(prop("En Modicon", eq::modiconText(pt), PG::ValueType::ReadOnly));
        place.properties.push_back(prop("En Schneider", pt.address, PG::ValueType::ReadOnly));
    } else {
        place.properties.push_back(prop("Refus\xC3\xA9" "e", reason, PG::ValueType::ReadOnly));
    }
    if (auto* lk = host() ? host()->link(v->equipment) : nullptr) {
        std::string why;
        const auto q = lk->quality(v->name, &why);
        place.properties.push_back(prop("Qualit\xC3\xA9", std::string(hmi::comm::qualityName(q)) + (why.empty() ? std::string{} : " : " + why), PG::ValueType::ReadOnly));
    }
    cats.push_back(std::move(place));
}

// ============================================================ reseau du PC ===
void HmiCommPane::refreshNetwork() {
    if (auto* h = host()) {
        h->refreshAdapters();
        say("Actualiser : les ports du PC sont relus.");
    }
}

void HmiCommPane::refreshDiagram() {
    auto* h = host();
    NetDiagram d;
    d.toggleNote = "le vrai r\xC3\xA9seau du PC : ses ports et les vrais appareils";
    // Lot 17 : le reseau simule (les jumeaux, les ports du PC simule).
    if (simView_) {
        refreshSimDiagram(d);
        diagram_->setData(std::move(d));
        return;
    }
    if (!h) {
        d.empty = "Le r\xC3\xA9seau du PC n'est pas suivi ici";
        diagram_->setData(std::move(d));
        return;
    }
    if (!h->adaptersKnown()) {
        h->refreshAdapters();
        d.empty = "Lecture des ports du PC\xE2\x80\xA6";
        diagram_->setData(std::move(d));
        return;
    }
    const auto& p = doc_->project;
    const auto list = h->adapters();
    d.computer = h->computerName();
    d.system = h->systemName() + " \xC2\xB7 ce PC";
    std::vector<eq::Network> nets;
    for (std::size_t i = 0; i < list.size(); ++i)
        for (const auto& [ip, pre] : list[i].addresses) nets.push_back({ip, pre, static_cast<int>(i)});
    // Les ports.
    std::size_t eth = 0, ethUp = 0, wifi = 0, other = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& a = list[i];
        NetDiagram::Port port;
        port.key = a.name;
        port.number = static_cast<int>(i) + 1;
        port.title = a.name;
        port.card = a.description;
        port.up = a.up;
        port.wifi = a.kind == hmi::netinfo::Adapter::Kind::WiFi;
        port.link = a.up ? "branch\xC3\xA9" + (a.speedText().empty() ? std::string{} : ", " + a.speedText()) : std::string("c\xC3\xA2" "ble d\xC3\xA9" "branch\xC3\xA9");
        if (port.wifi && a.up) port.link = "connect\xC3\xA9";
        if (a.ip()) {
            port.ip = eq::ipv4Text(a.ip());
            port.prefix = std::to_string(a.prefix());
        }
        std::string detail;
        if (a.gateway) detail = "passerelle " + eq::ipv4Text(a.gateway);
        if (a.dhcpKnown) detail += (detail.empty() ? "" : " \xC2\xB7 ") + std::string(a.dhcp ? "DHCP" : "fixe");
        if (a.addresses.size() > 1) detail += (detail.empty() ? "" : " \xC2\xB7 ") + ("+" + std::to_string(a.addresses.size() - 1) + " adresse(s)");
        port.detail = detail;
        if (!a.mac.empty()) port.mac = "MAC " + a.mac;
        port.automatic = a.automaticAddress();
        port.selected = portSide_ && a.name == port_;
        port.pending = h->applyState().busy && h->applyState().adapter == a.name;
        d.ports.push_back(std::move(port));
        if (a.kind == hmi::netinfo::Adapter::Kind::WiFi) ++wifi;
        else if (a.kind == hmi::netinfo::Adapter::Kind::Ethernet) {
            ++eth;
            ethUp += a.up ? 1 : 0;
        } else {
            ++other;
        }
    }
    if (eth) d.portsSummary.push_back(std::to_string(eth) + " Ethernet (" + std::to_string(ethUp) + " branch\xC3\xA9" + (ethUp > 1 ? "s" : "") + ")");
    if (wifi) d.portsSummary.push_back(std::to_string(wifi) + " Wi-Fi");
    if (other) d.portsSummary.push_back(std::to_string(other) + " autre" + (other > 1 ? "s" : "") + " (virtuel" + (other > 1 ? "s" : "") + ")");
    // Les equipements (et l'automate du projet), chacun sur le reseau qui le joint.
    struct Placed {
        NetDiagram::Equip card;
        int               net{-1};      // l'indice dans `nets` ; -1 : hors reseau ; -2 : dans ce PC
        std::uint32_t     ip{0};
        std::string       host;
        std::string       type;
    };
    std::vector<Placed> placed;
    {
        const auto& c = p.comm;
        if (c.modbus()) {
            Placed pl;
            auto* ch = hosts_.comm ? hosts_.comm() : nullptr;
            const hmi::comm::Link* lk = ch ? ch->link() : nullptr;
            const auto diag = lk ? lk->diagnostics() : hmi::comm::Diagnostics{};
            pl.card.key = kPlcKey;
            pl.card.name = "Automate";
            pl.card.tag = "automate du projet";
            pl.card.line1 = c.host + " \xC2\xB7 Modbus TCP/IP \xC2\xB7 esclave " + std::to_string(c.unit);
            const auto pr = h->probe(EquipmentHost::kPlc);
            std::string l2 = pr && pr->done ? (pr->ok ? "ping " + msText(pr->ms) : "ping : " + pr->why) : std::string("ping : pas encore");
            if (lk && lk->connected()) l2 += " \xC2\xB7 Modbus " + msText(diag.avgMs);
            else if (lk && !diag.lastError.empty()) l2 += " \xC2\xB7 Modbus : " + diag.lastError;
            pl.card.line2 = l2;
            pl.card.tone = lk ? (lk->connected() ? 1 : (diag.requests || !diag.lastError.empty() ? 3 : 0)) : 0;
            pl.card.tip = "L'automate du projet : " + c.host + ":" + std::to_string(c.port) + (diag.device.empty() ? std::string{} : " - " + diag.device);
            pl.host = c.host;
            pl.type = "Modbus TCP/IP";
            if (loopback(c.host)) {
                pl.net = -2;
                pl.card.simulated = true;
                pl.card.line1 = c.host + ":" + std::to_string(c.port) + " \xC2\xB7 serveur de d\xC3\xA9monstration";
                pl.card.line2 = "le simulateur de l'application, en Modbus TCP";
                pl.card.tone = 4;
            } else if (eq::parseIpv4(c.host, pl.ip)) {
                pl.net = eq::networkFor(pl.ip, nets);
            }
            pl.card.selected = !portSide_ && equipment_ == kPlcKey;
            placed.push_back(std::move(pl));
        }
    }
    for (const auto& e : p.equipments) {
        Placed pl;
        pl.card.key = e.name;
        pl.card.name = e.name;
        pl.host = e.host;
        pl.type = std::string(hmi::equipmentTypeLabel(e.type));
        const auto st = h->status(e.name);
        const auto pr = h->probe(e.name);
        const hmi::comm::Link* lk = h->link(e.name);
        pl.card.selected = !portSide_ && same(equipment_, e.name);
        pl.card.disabled = !e.enabled;
        if (e.simulated && e.modbus()) {
            pl.net = -2;
            pl.card.simulated = true;
            pl.card.line1 = "Modbus TCP/IP \xC2\xB7 seulement simul\xC3\xA9";
            const int port = h->simulatedPort(e.name);
            pl.card.line2 = (port ? "127.0.0.1:" + std::to_string(port) + " \xC2\xB7 " : std::string{}) + "son esclave virtuel (sa fiche, la carte m\xC3\xA9moire)";
            pl.card.tone = 4;
            pl.card.tip = e.name + " : seulement simul\xC3\xA9, son esclave virtuel dans ce PC";
            placed.push_back(std::move(pl));
            continue;
        }
        pl.card.line1 = e.host + " \xC2\xB7 " + pl.type + (e.modbus() ? " \xC2\xB7 esclave " + std::to_string(e.unit) : " \xC2\xB7 port " + std::to_string(e.port));
        std::string l2;
        if (!e.enabled) {
            l2 = "d\xC3\xA9sactiv\xC3\xA9";
            pl.card.tone = 5;
        } else {
            if (pr && pr->done && pr->host == e.host) l2 = pr->ok ? "ping " + msText(pr->ms) : "ping : " + pr->why;
            else l2 = "ping : pas encore";
            const bool twin = e.modbus() && h->viaTwin(e.name);        // lot 17 : l'IHM parle a son jumeau
            if (twin) {
                l2 += " \xC2\xB7 l'IHM lit son esclave simul\xC3\xA9";
            } else if (e.modbus()) {
                if (lk && lk->connected()) l2 += " \xC2\xB7 Modbus " + msText(lk->diagnostics().avgMs);
                else if (lk && !lk->diagnostics().lastError.empty()) l2 += " \xC2\xB7 Modbus : " + lk->diagnostics().lastError;
            } else if (pr && pr->done && pr->portTried) {
                l2 += " \xC2\xB7 port " + std::to_string(e.port) + (pr->portOk ? " ouvert" : " ferm\xC3\xA9");
            }
            pl.card.tone = st && st->tested ? (st->reachable ? 1 : 3) : 0;
            // Le vrai appareil : son etat a lui (pas celui du jumeau).
            if (twin) pl.card.tone = st && st->realTone ? st->realTone : (pr && pr->done && pr->host == e.host ? (pr->ok ? 1 : 3) : 0);
            pl.card.tip = st ? e.name + " : " + st->state + (st->why.empty() ? std::string{} : " - " + st->why) : e.name;
        }
        pl.card.line2 = l2;
        if (loopback(e.host)) {
            pl.net = -2;
            pl.card.simulated = true;
        } else if (eq::parseIpv4(e.host, pl.ip)) {
            pl.net = eq::networkFor(pl.ip, nets);
        }
        placed.push_back(std::move(pl));
    }
    // Les reseaux montres : ceux ou il y a au moins un equipement, dans l'ordre des ports.
    std::map<int, int> netOf;       // indice dans `nets` -> indice dans d.nets
    for (std::size_t k = 0; k < nets.size(); ++k) {
        std::size_t count = 0;
        for (const auto& pl : placed) count += pl.net == static_cast<int>(k) ? 1 : 0;
        if (!count) continue;
        NetDiagram::Net n;
        n.port = nets[k].adapter;
        n.label = "R\xC3\xA9seau " + eq::networkText(nets[k].ip, nets[k].prefix) + " \xC2\xB7 " + std::to_string(count) + " \xC3\xA9quipement" + (count > 1 ? "s" : "");
        netOf[static_cast<int>(k)] = static_cast<int>(d.nets.size());
        if (d.ports[static_cast<std::size_t>(n.port)].net < 0) d.ports[static_cast<std::size_t>(n.port)].net = static_cast<int>(d.nets.size());
        d.nets.push_back(std::move(n));
    }
    // Les ports libres (sans equipement) : pour proposer une adresse aux equipements hors reseau.
    std::vector<std::size_t> freePorts;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (d.ports[i].net < 0 && list[i].kind != hmi::netinfo::Adapter::Kind::WiFi) freePorts.push_back(i);
    std::stable_sort(freePorts.begin(), freePorts.end(), [&](std::size_t a, std::size_t b) { return list[a].up && !list[b].up; });
    gives_.clear();
    std::size_t outside = 0;
    for (auto& pl : placed) {
        if (pl.net >= 0) pl.card.net = netOf[pl.net];
        else pl.card.net = -1;
        if (pl.net == -1 && !pl.card.disabled) {
            ++outside;
            NetDiagram::Outside o;
            o.equip = pl.card.key;
            o.title = pl.card.name;
            o.line = pl.host + " \xC2\xB7 " + pl.type;
            if (!pl.ip) {
                o.text = "Son adresse est un nom (" + pl.host + ") : le PC ne sait pas dans quel r\xC3\xA9seau il est. Donnez son adresse IP.";
            } else {
                const int pre = guessPrefix(pl.ip);
                o.text = "Aucun port de ce PC n'est dans le r\xC3\xA9seau " + eq::networkText(pl.ip, pre) + " : le PC ne peut pas le joindre.";
                if (!freePorts.empty()) {
                    const std::size_t port = freePorts.front();
                    std::vector<std::uint32_t> taken{pl.ip};
                    for (const auto& q : placed)
                        if (q.ip) taken.push_back(q.ip);
                    const std::uint32_t ip = eq::suggestAddress(pl.ip, pre, taken);
                    if (ip) {
                        o.give = "Donner au port " + std::to_string(port + 1) + " (" + list[port].name + ") l'adresse " + eq::ipv4Text(ip) + " / " + std::to_string(pre);
                        gives_.push_back({list[port].name, eq::ipv4Text(ip), pre});
                    }
                } else {
                    o.text += " Aucun port libre : ajoutez une adresse \xC3\xA0 un port, ou changez celle de l'\xC3\xA9quipement.";
                }
            }
            if (o.give.empty()) gives_.push_back({});
            o.edit = pl.card.key == kPlcKey ? "Modifier l'adresse de l'automate" : "Modifier l'adresse de " + pl.card.name;
            d.outside.push_back(std::move(o));
        }
        d.equips.push_back(std::move(pl.card));
    }
    if (outside) d.outsideTitle = "Hors r\xC3\xA9seau : " + std::to_string(outside) + " \xC3\xA9quipement" + (outside > 1 ? "s" : "");
    d.hint = "Un clic sur un port : son adresse, \xC3\xA0 droite. Sur un \xC3\xA9quipement : sa fiche ; le tirer sur un port : l'y d\xC3\xA9placer ; Suppr : le supprimer.";
    diagram_->setData(std::move(d));
    // Le badge : les equipements joignables.
    std::size_t reachable = 0, total = 0;
    for (const auto& st : h->statuses()) {
        if (!st.enabled || st.simulated) continue;
        ++total;
        reachable += st.reachable ? 1 : 0;
    }
    tabs_->setTabBadge(TNetwork, std::to_string(list.size()) + " port" + (list.size() > 1 ? "s" : ""), ui::Tone::None);
    (void)reachable;
    (void)total;
}

void HmiCommPane::selectPort(const std::string& name) {
    port_ = name;
    portSide_ = true;
    loadPortDraft();
    refreshDiagram();
    rebuildProperties();
}

void HmiCommPane::loadPortDraft() {
    draft_ = PortDraft{};
    const auto a = adapter(port_);
    if (!a) return;
    draft_.adapter = a->name;
    draft_.dhcp = a->dhcpKnown && a->dhcp;
    draft_.ip = a->ip() ? eq::ipv4Text(a->ip()) : std::string{};
    draft_.mask = eq::maskText(a->prefix() ? a->prefix() : 24);
    draft_.gateway = a->gateway ? eq::ipv4Text(a->gateway) : std::string{};
    draft_.dns = a->dns.empty() ? std::string{} : eq::ipv4Text(a->dns.front());
}

bool HmiCommPane::setPortField(const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (port_.empty() || !adapter(port_)) return fail("choisissez d'abord un port (un clic sur le sch\xC3\xA9ma)");
    if (draft_.adapter != port_) loadPortDraft();
    const std::string v = trimmed(raw);
    std::uint32_t ip = 0;
    if (key == "mode") {
        const std::string l = lower(v);
        draft_.dhcp = l.find("dhcp") != std::string::npos || l.find("auto") != std::string::npos;
    } else if (key == "ip") {
        if (!eq::parseIpv4(v, ip)) return fail("adresse \xC2\xAB " + v + " \xC2\xBB : quatre nombres de 0 \xC3\xA0 255 (192.168.1.25)");
        draft_.ip = eq::ipv4Text(ip);
        draft_.dhcp = false;
    } else if (key == "masque") {
        int prefix = 0;
        if (!eq::parseMask(v, prefix)) return fail("masque \xC2\xAB " + v + " \xC2\xBB : 255.255.255.0, ou /24");
        draft_.mask = eq::maskText(prefix);
        draft_.dhcp = false;
    } else if (key == "passerelle") {
        if (!v.empty() && !eq::parseIpv4(v, ip)) return fail("passerelle \xC2\xAB " + v + " \xC2\xBB : une adresse IPv4, ou vide (aucune)");
        draft_.gateway = v.empty() ? std::string{} : eq::ipv4Text(ip);
    } else if (key == "dns") {
        if (!v.empty() && !eq::parseIpv4(v, ip)) return fail("DNS \xC2\xAB " + v + " \xC2\xBB : une adresse IPv4, ou vide");
        draft_.dns = v.empty() ? std::string{} : eq::ipv4Text(ip);
    } else {
        return fail("champ inconnu : " + key);
    }
    draft_.edited = true;
    rebuildProperties();
    return true;
}

std::vector<std::pair<int, std::string>> HmiCommPane::portChecks() const {
    std::vector<std::pair<int, std::string>> out;
    auto* h = host();
    const auto a = adapter(port_);
    if (!h || !a) return out;
    const auto st = h->applyState();
    if (st.busy && st.adapter == a->name) out.push_back({0, "Changement en cours\xE2\x80\xA6"});
    if (!draft_.edited) {
        out.push_back({4, "Changez l'adresse, le masque ou la passerelle ci-dessus, puis Appliquer."});
    } else if (draft_.dhcp) {
        out.push_back({4, "Le port demandera son adresse au serveur DHCP du r\xC3\xA9seau (sans serveur : une adresse 169.254.x.x)."});
    } else {
        std::string why;
        if (!eq::validPortSettings(draft_.ip, draft_.mask, draft_.gateway, &why)) {
            out.push_back({3, why});
        } else {
            std::uint32_t ip = 0;
            int prefix = 24;
            (void)eq::parseIpv4(draft_.ip, ip);
            (void)eq::parseMask(draft_.mask, prefix);
            const bool unchanged = ip == a->ip() && prefix == a->prefix() && draft_.gateway == (a->gateway ? eq::ipv4Text(a->gateway) : std::string{});
            if (unchanged) out.push_back({4, "Rien ne change : c'est d\xC3\xA9j\xC3\xA0 l'adresse du port."});
            // L'adresse est-elle libre ?
            if (!unchanged && ip != a->ip()) {
                const auto check = h->addressCheck();
                if (check.ip != ip || !check.done)
                    out.push_back({0, "V\xC3\xA9rification : " + draft_.ip + " est-elle libre ? (ping, ARP)"});
                else if (check.inUse)
                    out.push_back({3, draft_.ip + " est d\xC3\xA9j\xC3\xA0 prise sur le r\xC3\xA9seau" + (check.mac.empty() ? std::string{} : " (MAC " + check.mac + ")")
                                          + " : deux appareils \xC3\xA0 la m\xC3\xAAme adresse ne se joignent plus."});
                else
                    out.push_back({1, draft_.ip + " est libre : pas de r\xC3\xA9ponse au ping, absente de la table ARP."});
            }
            // Un equipement a deja cette adresse ?
            for (const auto& e : doc_->project.equipments)
                if (e.host == draft_.ip) out.push_back({3, draft_.ip + " est l'adresse de l'\xC3\xA9quipement " + e.name + "."});
            // Un autre port du PC dans ce reseau ?
            for (const auto& other : h->adapters()) {
                if (other.name == a->name) continue;
                for (const auto& [oip, opre] : other.addresses)
                    if (eq::sameNetwork(oip, ip, std::min(opre, prefix)))
                        out.push_back({2, "Le port " + other.name + " est d\xC3\xA9j\xC3\xA0 dans ce r\xC3\xA9seau (" + eq::ipv4Text(oip) + ") : le PC choisira l'un ou l'autre."});
            }
            // Les equipements : ceux qui restent, ceux qui partent, ceux qui arrivent.
            std::vector<std::string> stay, leave, arrive;
            std::string oldNet = a->ip() ? eq::networkText(a->ip(), a->prefix()) : std::string{};
            const auto consider = [&](const std::string& name, const std::string& hostText) {
                std::uint32_t eip = 0;
                if (!eq::parseIpv4(hostText, eip) || loopback(hostText)) return;
                const bool before = a->ip() && eq::sameNetwork(eip, a->ip(), a->prefix());
                const bool after = eq::sameNetwork(eip, ip, prefix);
                if (before && after) stay.push_back(name);
                else if (before) leave.push_back(name);
                else if (after) arrive.push_back(name);
            };
            if (doc_->project.comm.modbus()) consider("l'automate du projet", doc_->project.comm.host);
            for (const auto& e : doc_->project.equipments)
                if (e.enabled && !(e.simulated && e.modbus())) consider(e.name, e.host);
            if (!stay.empty())
                out.push_back({1, stay.size() == 1 ? stay.front() + " reste joignable (r\xC3\xA9seau " + oldNet + ")."
                                                   : "Les " + std::to_string(stay.size()) + " \xC3\xA9quipements du r\xC3\xA9seau " + oldNet + " y restent."});
            if (!leave.empty())
                out.push_back({2, std::to_string(leave.size()) + " \xC3\xA9quipement(s) ne seront plus joignables par ce port : " + fewOf(leave, 4) + "."});
            for (const auto& n : arrive) out.push_back({1, n + " devient joignable (r\xC3\xA9seau " + eq::networkText(ip, prefix) + ")."});
            // Ce qui ecoute sur ce port.
            const auto& w = doc_->project.web;
            if (w.enabled && w.allInterfaces && ip != a->ip())
                out.push_back({2, "L'acc\xC3\xA8s web \xC3\xA9" "coute sur ce port : les tablettes devront viser " + draft_.ip + ":" + std::to_string(w.port) + "."});
            if (doc_->project.comm.demoServer && doc_->project.comm.demoAllInterfaces && ip != a->ip())
                out.push_back({2, "Le serveur de d\xC3\xA9monstration \xC3\xA9" "coute sur ce port : les supervisions devront viser " + draft_.ip + "."});
            if (draft_.gateway.empty() && !unchanged)
                out.push_back({4, "Sans passerelle : le PC ne sort pas de ce r\xC3\xA9seau par ce port (normal pour un r\xC3\xA9seau machine)."});
        }
    }
#if defined(_WIN32)
    out.push_back({4, "Windows demandera l'autorisation administrateur."});
#else
    out.push_back({4, hmi::netinfo::isAdministrator() ? std::string("Appliqu\xC3\xA9 par la commande ip (droits administrateur).")
                                                     : std::string("pkexec demandera le mot de passe administrateur (commande ip).")});
#endif
    if (st.done && !st.ok && st.adapter == a->name) out.push_back({3, "Le dernier changement a \xC3\xA9t\xC3\xA9 refus\xC3\xA9 : " + st.why});
    return out;
}

void HmiCommPane::refreshChecks() {
    if (side() != Side::Port || port_.empty()) {
        checks_->setItems({}, {});
        return;
    }
    // L'adresse tapee : demander si elle est libre (une fois par adresse).
    if (auto* h = host(); h && draft_.edited && !draft_.dhcp) {
        std::uint32_t ip = 0;
        const auto a = adapter(port_);
        if (a && eq::parseIpv4(draft_.ip, ip) && ip != a->ip() && h->addressCheck().ip != ip) h->checkAddress(ip);
    }
    std::vector<HmiCheckList::Item> items;
    for (const auto& [tone, text] : portChecks()) items.push_back({tone, text});
    std::string note;
    if (auto* h = host()) {
        const std::string last = h->lastChangedAdapter();
        if (const auto prev = h->previous(port_.empty() ? last : port_))
            note = "Si le PC ne rejoint plus ses \xC3\xA9quipements apr\xC3\xA8s le changement, Remettre l'adresse d'avant (barre du haut) revient \xC3\xA0 "
                   + (prev->dhcp ? std::string("l'adresse automatique (DHCP)") : prev->ip + " / " + prev->mask) + " en un clic.";
        else
            note = "Apr\xC3\xA8s un changement, Remettre l'adresse d'avant (barre du haut) revient au r\xC3\xA9glage d'avant en un clic.";
    }
    checks_->setItems("V\xC3\xA9rifications", std::move(items), note);
    bool blocking = !draft_.edited;
    for (const auto& it : checks_->items()) blocking = blocking || it.tone == 3;
    applyButton_->setEnabled(!blocking && !(host() && host()->applyState().busy));
    cancelButton_->setEnabled(draft_.edited);
}

void HmiCommPane::rebuildPortProperties(std::vector<PG::Category>& cats) {
    const auto a = adapter(port_);
    if (!a) {
        PG::Category none;
        none.name = "R\xC3\xA9seau du PC";
        none.properties.push_back(prop("Port", "un clic sur un port du sch\xC3\xA9ma", PG::ValueType::ReadOnly));
        if (auto* h = host()) {
            none.properties.push_back(prop("PC", h->computerName(), PG::ValueType::ReadOnly));
            none.properties.push_back(prop("Syst\xC3\xA8me", h->systemName(), PG::ValueType::ReadOnly));
            none.properties.push_back(prop("Ports", std::to_string(h->adapters().size()), PG::ValueType::ReadOnly));
        }
        cats.push_back(std::move(none));
        return;
    }
    if (draft_.adapter != a->name) loadPortDraft();
    const auto field = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setPortField(key, std::string(v));
        };
    };
    PG::Category port;
    port.name = "Port choisi";
    port.properties.push_back(prop("Nom", a->name, PG::ValueType::ReadOnly));
    port.properties.push_back(prop("Carte", a->description, PG::ValueType::ReadOnly));
    if (!a->mac.empty()) port.properties.push_back(prop("Adresse MAC", a->mac, PG::ValueType::ReadOnly));
    port.properties.push_back(prop("\xC3\x89tat", a->up ? "branch\xC3\xA9" + (a->speedText().empty() ? std::string{} : ", " + a->speedText())
                                                         : std::string("c\xC3\xA2" "ble d\xC3\xA9" "branch\xC3\xA9"),
                                   PG::ValueType::ReadOnly));
    cats.push_back(std::move(port));
    PG::Category ipv4;
    ipv4.name = "Adresse IPv4";
    ipv4.properties.push_back(prop("Mode", draft_.dhcp ? modeLabels()[1] : modeLabels()[0], PG::ValueType::Enum, field("mode"),
                                   "Adresse fixe : celle qu'on tape (le cas d'un r\xC3\xA9seau machine). Automatique : le serveur DHCP du r\xC3\xA9seau la donne.",
                                   modeLabels()));
    if (!draft_.dhcp) {
        ipv4.properties.push_back(prop("Adresse IP", draft_.ip, PG::ValueType::Text, field("ip"), "L'adresse du PC sur ce port : 192.168.1.20."));
        ipv4.properties.push_back(prop("Masque", draft_.mask + " (/" + std::to_string([&] {
                                                     int pre = 24;
                                                     (void)eq::parseMask(draft_.mask, pre);
                                                     return pre;
                                                 }()) + ")",
                                       PG::ValueType::Text, field("masque"), "255.255.255.0 (/24) : les adresses x.x.x.1 \xC3\xA0 x.x.x.254 sont dans le r\xC3\xA9seau."));
        ipv4.properties.push_back(prop("Passerelle", draft_.gateway, PG::ValueType::Text, field("passerelle"), "Vide : aucune (un r\xC3\xA9seau machine isol\xC3\xA9)."));
        ipv4.properties.push_back(prop("DNS", draft_.dns, PG::ValueType::Text, field("dns"), "Vide : aucun."));
    }
    ipv4.properties.push_back(prop("Avant", a->ip() ? eq::ipv4Text(a->ip()) + " / " + std::to_string(a->prefix()) + (a->dhcpKnown ? (a->dhcp ? " (DHCP)" : " (fixe)") : std::string{})
                                                    : std::string("sans adresse"),
                                   PG::ValueType::ReadOnly));
    cats.push_back(std::move(ipv4));
}

bool HmiCommPane::applyPort(std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    auto* h = host();
    const auto a = adapter(port_);
    if (!h || !a) return fail("choisissez d'abord un port (un clic sur le sch\xC3\xA9ma)");
    if (!draft_.edited) return fail("rien \xC3\xA0 appliquer : changez d'abord l'adresse");
    for (const auto& [tone, text] : portChecks())
        if (tone == 3) return fail("Appliquer : " + text);
    hmi::netinfo::PortConfig cfg;
    cfg.dhcp = draft_.dhcp;
    cfg.ip = draft_.ip;
    cfg.mask = draft_.mask;
    cfg.gateway = draft_.gateway;
    cfg.dns = draft_.dns;
    std::string reason;
    if (!h->applyPort(*a, cfg, &reason)) return fail("Appliquer : " + reason);
    say("Port " + a->name + " : " + (cfg.dhcp ? std::string("adresse automatique (DHCP)") : cfg.ip + " / " + cfg.mask) + " en cours d'application"
#if defined(_WIN32)
        + " (Windows demande l'autorisation)"
#endif
        + "\xE2\x80\xA6");
    refreshChecks();
    return true;
}

void HmiCommPane::cancelPort() {
    loadPortDraft();
    rebuildProperties();
    say("Annul\xC3\xA9 : le port garde son adresse.");
}

bool HmiCommPane::restorePort(std::string* why) {
    auto* h = host();
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    if (!h) return fail("le r\xC3\xA9seau du PC n'est pas suivi ici");
    const std::string name = port_.empty() || !h->previous(port_) ? h->lastChangedAdapter() : port_;
    const auto prev = h->previous(name);
    const auto a = adapter(name);
    if (!prev || !a) return fail("aucun changement d'adresse \xC3\xA0 d\xC3\xA9" "faire");
    std::string reason;
    if (!h->applyPort(*a, *prev, &reason)) return fail("Remettre l'adresse d'avant : " + reason);
    port_ = name;
    portSide_ = true;
    say("Port " + name + " : retour \xC3\xA0 " + (prev->dhcp ? std::string("l'adresse automatique (DHCP)") : prev->ip + " / " + prev->mask) + "\xE2\x80\xA6");
    return true;
}

bool HmiCommPane::giveOutside(int index, std::string* why) {
    if (index < 0 || static_cast<std::size_t>(index) >= gives_.size() || gives_[static_cast<std::size_t>(index)].port.empty()) {
        const std::string m = "aucun port libre \xC3\xA0 qui donner une adresse";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    const Give g = gives_[static_cast<std::size_t>(index)];
    tabs_->setCurrentIndex(TNetwork);
    port_ = g.port;
    portSide_ = true;
    loadPortDraft();
    draft_.dhcp = false;
    draft_.ip = g.ip;
    draft_.mask = eq::maskText(g.prefix);
    draft_.gateway.clear();
    draft_.edited = true;
    refreshDiagram();
    rebuildProperties();
    say("Le port " + g.port + " recevra " + g.ip + " / " + std::to_string(g.prefix) + " : v\xC3\xA9rifiez \xC3\xA0 droite, puis Appliquer.");
    return true;
}

// ================================================================= scanner ===
bool HmiCommPane::scanning() const { return scanner_ && scanner_->running(); }

std::vector<hmi::ipscan::Host> HmiCommPane::scanResults() const {
    return scanner_ ? scanner_->results() : std::vector<hmi::ipscan::Host>{};
}

std::string HmiCommPane::selectedScanned() const {
    const auto rows = scanPage_->table().selectedModelRows();
    return rows.empty() || rows.front() >= scanOrder_.size() ? std::string{} : scanOrder_[rows.front()];
}

bool HmiCommPane::setScanField(const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string v = trimmed(raw);
    if (key == "port") {
        // "eth1 (192.168.1.0 / 24)" : le nom avant la parenthese.
        const std::string name = trimmed(v.substr(0, v.find(" (")));
        if (!adapter(name)) return fail("port inconnu : " + name);
        scanPort_ = name;
        scanRange_.clear();
    } else if (key == "plage") {
        std::uint32_t a = 0, b = 0;
        std::string reason;
        if (!v.empty() && !hmi::ipscan::parseRange(v, a, b, &reason)) return fail("plage : " + reason);
        scanRange_ = v;
    } else if (key == "ports") {
        std::vector<int> ports;
        std::string reason;
        if (!hmi::ipscan::parsePorts(v, ports, &reason)) return fail("ports : " + reason);
        scanOptions_.ports = ports;
    } else if (key == "delai") {
        const int n = std::atoi(v.c_str());
        if (n < 50 || n > 5000) return fail("d\xC3\xA9lai : de 50 \xC3\xA0 5000 ms");
        scanOptions_.timeoutMs = n;
    } else if (key == "noms") {
        scanOptions_.names = yes(v);
    } else if (key == "modbus") {
        scanOptions_.modbusId = yes(v);
    } else {
        return fail("champ inconnu : " + key);
    }
    rebuildProperties();
    refreshScan();
    return true;
}

bool HmiCommPane::startScan(std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    auto* h = host();
    std::string text = scanRange_;
    const auto a = adapter(scanPort_);
    if (text.empty()) {
        if (!a || !a->ip()) return fail("choisissez un port avec une adresse (ou tapez une plage)");
        text = eq::ipv4Text(eq::networkOf(a->ip(), a->prefix())) + "/" + std::to_string(a->prefix());
    }
    hmi::ipscan::Options o = scanOptions_;
    std::string reason;
    if (!hmi::ipscan::parseRange(text, o.first, o.last, &reason)) return fail("plage : " + reason);
    // Sur le reseau d'un port du PC : l'ARP dit qui est la.
    o.onLink = false;
    if (h)
        for (const auto& ad : h->adapters())
            for (const auto& [ip, pre] : ad.addresses)
                if (eq::sameNetwork(ip, o.first, pre) && eq::sameNetwork(ip, o.last, pre)) o.onLink = true;
    if (!scanner_) scanner_ = std::make_unique<hmi::ipscan::Scanner>();
    if (!scanner_->start(o, &reason)) return fail("scan : " + reason);
    scanWasRunning_ = true;
    tabs_->setCurrentIndex(TScanner);
    say("Scan de " + hmi::ipscan::rangeText(o.first, o.last) + " lanc\xC3\xA9 (ping, ARP, ports " + hmi::ipscan::portsText(o.ports) + ").");
    refreshScan();
    return true;
}

void HmiCommPane::stopScan() {
    if (!scanner_ || !scanner_->running()) return;
    scanner_->stop();
    say("Scan arr\xC3\xAAt\xC3\xA9 : " + std::to_string(scanner_->results().size()) + " appareil(s) trouv\xC3\xA9(s).");
    refreshScan();
}

void HmiCommPane::refreshScan() {
    auto* h = host();
    // Le port par defaut : le premier branche, avec une adresse.
    if (scanPort_.empty() && h)
        for (const auto& a : h->adapters())
            if (scanPort_.empty() && a.up && a.ip() && !a.automaticAddress() && a.kind != hmi::netinfo::Adapter::Kind::WiFi) scanPort_ = a.name;
    const auto a = adapter(scanPort_);
    std::string title = scanRange_.empty() ? (a && a->ip() ? "R\xC3\xA9seau " + eq::networkText(a->ip(), a->prefix()) + " sur " + a->name : std::string("Choisissez un port \xC3\xA0 droite"))
                                           : "Plage " + scanRange_;
    std::string detail = "ports " + hmi::ipscan::portsText(scanOptions_.ports) + " \xC2\xB7 d\xC3\xA9lai " + std::to_string(scanOptions_.timeoutMs) + " ms";
    float fraction = -1;
    bool running = false;
    std::vector<hmi::ipscan::Host> results;
    if (scanner_) {
        const auto pr = scanner_->progress();
        running = scanner_->running();
        results = scanner_->results();
        if (pr.total) {
            fraction = static_cast<float>(pr.done) / static_cast<float>(pr.total);
            char secs[32];
            std::snprintf(secs, sizeof secs, "%.1f s", pr.seconds);
            detail = std::to_string(pr.done) + " / " + std::to_string(pr.total) + " adresses essay\xC3\xA9" "es \xC2\xB7 " + std::to_string(pr.found) + " appareil"
                     + (pr.found > 1 ? "s" : "") + " \xC2\xB7 " + secs + (pr.stopped ? " (arr\xC3\xAAt\xC3\xA9)" : running ? std::string{} : " \xC2\xB7 termin\xC3\xA9");
        }
        if (scanWasRunning_ && !running) {
            scanWasRunning_ = false;
            say("Scan termin\xC3\xA9 : " + std::to_string(pr.found) + " appareil(s) en " + std::to_string(static_cast<int>(std::lround(pr.seconds))) + " s.");
        }
    }
    scanPage_->setProgress(title, detail, fraction, running);
    if (results.size() == scanShown_ && !running && scanModel_) {
        tabs_->setTabBadge(TScanner, scanner_ ? std::to_string(results.size()) : std::string{}, ui::Tone::None);
        return;
    }
    scanShown_ = results.size();
    // Ce que le projet connait deja a chaque adresse.
    std::map<std::string, std::string> known;
    for (const auto& e : doc_->project.equipments) known[e.host] = e.name;
    if (doc_->project.comm.modbus()) known[doc_->project.comm.host] = "automate du projet";
    if (h)
        for (const auto& ad : h->adapters())
            for (const auto& [ip, pre] : ad.addresses) known[eq::ipv4Text(ip)] = "ce PC (" + ad.name + ")";
    const std::string keep = selectedScanned();
    scanOrder_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (const auto& x : results) {
        scanOrder_.push_back(x.address);
        const auto k = known.find(x.address);
        std::string mac = x.mac, vendor = x.vendor;
        // Une adresse du PC lui-meme : sa MAC est celle du port.
        if (mac.empty() && h)
            for (const auto& ad : h->adapters())
                for (const auto& [ip, pre] : ad.addresses)
                    if (ip == x.ip) mac = ad.mac;
        if (vendor.empty() && !mac.empty()) vendor = hmi::ipscan::vendorOf(mac);
        rows.push_back({x.address, k == known.end() ? std::string("nouveau") : k->second, mac.empty() ? "\xE2\x80\x94" : mac, vendor,
                        x.portsText().empty() ? "\xE2\x80\x94" : x.portsText(), x.modbus, x.ping ? msText(x.pingMs) : "\xE2\x80\x94", x.name, x.how});
        tones.push_back(k != known.end() ? 4 : x.hasPort(502) ? 1 : 0);
    }
    scanModel_ = std::make_shared<Rows>(std::vector<std::string>{"Adresse", "Dans le projet", "MAC", "Fabricant", "Ports ouverts", "Modbus (identification)",
                                                                 "Ping", "Nom", "Vu par"},
                                        std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                            ui::CellStyle st;
                                            if (r >= tones.size()) return st;
                                            if (c == 0) {
                                                st.bold = true;
                                                st.icon = tones[r] == 1 ? ui::Icon::Cpu : ui::Icon::Module;
                                                st.iconTone = toneOf(tones[r]);
                                            }
                                            if (c == 1) st.fgTone = tones[r] == 4 ? ui::Tone::Accent : ui::Tone::Ok;
                                            if (c == 5 && tones[r] == 1) st.fgTone = ui::Tone::Ok;
                                            return st;
                                        });
    const bool was = refreshing_;
    refreshing_ = true;
    scanPage_->table().setModel(scanModel_);
    for (std::size_t i = 0; i < scanOrder_.size(); ++i)
        if (scanOrder_[i] == keep) scanPage_->table().selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    refreshing_ = was;
    tabs_->setTabBadge(TScanner, scanner_ ? std::to_string(results.size()) : std::string{}, running ? ui::Tone::Accent : ui::Tone::None);
    tabs_->setTabLive(TScanner, running);
}

bool HmiCommPane::addScanned(const std::string& ip, std::string* why) {
    const auto list = scanResults();
    const auto it = std::find_if(list.begin(), list.end(), [&](const hmi::ipscan::Host& x) { return x.address == ip; });
    if (it == list.end()) {
        const std::string m = ip + " n'est pas dans le r\xC3\xA9sultat du scan";
        say(m, true);
        if (why) *why = m;
        return false;
    }
    for (const auto& e : doc_->project.equipments)
        if (e.host == ip) {
            const std::string m = ip + " est d\xC3\xA9j\xC3\xA0 l'\xC3\xA9quipement " + e.name;
            say(m, true);
            if (why) *why = m;
            return false;
        }
    const bool modbus = it->hasPort(502);
    std::string name;
    if (!it->name.empty()) name = it->name.substr(0, it->name.find('.'));
    else if (!it->vendor.empty() && it->vendor.rfind("adresse locale", 0) != 0) name = it->vendor.substr(0, it->vendor.find(" (")) + " " + ip.substr(ip.rfind('.') + 1);
    else name = (modbus ? "Modbus " : "Appareil ") + ip;
    name = hmi::uniqueEquipmentName(doc_->project, name);
    int port = modbus ? 502 : (it->open.empty() ? 80 : it->open.front());
    const std::string added = addEquipment(name, modbus ? hmi::EquipmentType::ModbusTcp : hmi::EquipmentType::EthernetTcp, ip, port, why);
    if (added.empty()) return false;
    std::string desc = "trouv\xC3\xA9 par le scanner";
    if (!it->modbus.empty()) desc += " : " + it->modbus;
    else if (!it->vendor.empty()) desc += " : " + it->vendor;
    (void)setEquipmentField(added, "description", desc);
    if (modbus && it->modbusUnit >= 0) (void)setEquipmentField(added, "esclave", std::to_string(it->modbusUnit));
    tabs_->setCurrentIndex(TEquipments);
    selectEquipment(added);
    say(added + " ajout\xC3\xA9 (" + std::string(modbus ? "Modbus TCP/IP" : "Ethernet TCP/IP") + ", " + ip + ") : liez-lui des variables, puis Tester.");
    return true;
}

bool HmiCommPane::exportScan(std::string* where) {
    if (!scanner_ || scanner_->results().empty()) {
        say("Rien \xC3\xA0 exporter : lancez d'abord un scan.", true);
        return false;
    }
    const std::string text = scanner_->csv();
    hmi::ExportRequest rq;
    rq.fileName = "scan_ip_" + hmi::wallStamp().substr(0, 10) + ".csv";
    rq.format = "CSV";
    rq.source = "scanner";
    rq.rows = scanner_->results().size();
    rq.data = std::make_shared<const hmi::Bytes>(text.begin(), text.end());
    rq.origin = "Configuration > \xC3\x89quipements > Scanner IP";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Scan export\xC3\xA9 : " + path : "Export impossible" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

void HmiCommPane::rebuildScanProperties(std::vector<PG::Category>& cats) {
    auto* h = host();
    const auto field = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setScanField(key, std::string(v));
        };
    };
    std::vector<std::string> ports;
    std::string current;
    if (h)
        for (const auto& a : h->adapters()) {
            const std::string label = a.name + (a.ip() ? " (" + eq::networkText(a.ip(), a.prefix()) + ")" : std::string(" (sans adresse)"));
            ports.push_back(label);
            if (a.name == scanPort_) current = label;
        }
    PG::Category scan;
    scan.name = "Scanner IP";
    scan.properties.push_back(prop("Port", current, PG::ValueType::Enum, field("port"), "Le port du PC dont le r\xC3\xA9seau est scann\xC3\xA9.", ports));
    scan.properties.push_back(prop("Plage", scanRange_, PG::ValueType::Text, field("plage"),
                                   "Vide : tout le r\xC3\xA9seau du port. Sinon 192.168.1.1-50, 10.10.0.0/24, 192.168.1.30 (4096 adresses au plus)."));
    scan.properties.push_back(prop("Ports TCP", hmi::ipscan::portsText(scanOptions_.ports), PG::ValueType::Text, field("ports"),
                                   "502 Modbus, 80/443 web, 102 S7 (Siemens), 44818 EtherNet/IP, 4840 OPC UA, 9100 imprimante, 554 cam\xC3\xA9ra."));
    scan.properties.push_back(prop("D\xC3\xA9lai (ms)", std::to_string(scanOptions_.timeoutMs), PG::ValueType::Integer, field("delai"),
                                   "L'attente du ping et de chaque port. 400 ms suffit sur un r\xC3\xA9seau local."));
    scan.properties.push_back(prop("Noms (DNS)", tf(scanOptions_.names), PG::ValueType::Boolean, field("noms"), "Le nom de chaque appareil (DNS inverse)."));
    scan.properties.push_back(prop("Identification Modbus", tf(scanOptions_.modbusId), PG::ValueType::Boolean, field("modbus"),
                                   "Le port 502 ouvert : lire l'identification de l'appareil (fonction 43 : fabricant, r\xC3\xA9" "f\xC3\xA9rence, version)."));
    cats.push_back(std::move(scan));
    const std::string sel = selectedScanned();
    if (sel.empty()) return;
    for (const auto& x : scanResults()) {
        if (x.address != sel) continue;
        PG::Category host;
        host.name = "Adresse choisie";
        host.properties.push_back(prop("Adresse", x.address, PG::ValueType::ReadOnly));
        host.properties.push_back(prop("MAC", x.mac.empty() ? "\xE2\x80\x94" : x.mac, PG::ValueType::ReadOnly));
        host.properties.push_back(prop("Fabricant", x.vendor.empty() ? "inconnu" : x.vendor, PG::ValueType::ReadOnly));
        if (!x.name.empty()) host.properties.push_back(prop("Nom", x.name, PG::ValueType::ReadOnly));
        host.properties.push_back(prop("Ping", x.ping ? msText(x.pingMs) : "pas de r\xC3\xA9ponse (vu par " + x.how + ")", PG::ValueType::ReadOnly));
        host.properties.push_back(prop("Ports ouverts", x.portsText().empty() ? "aucun des ports essay\xC3\xA9s" : x.portsText(), PG::ValueType::ReadOnly));
        if (!x.modbus.empty())
            host.properties.push_back(prop("Modbus", x.modbus + (x.modbusUnit >= 0 ? " (esclave " + std::to_string(x.modbusUnit) + ")" : std::string{}), PG::ValueType::ReadOnly));
        cats.push_back(std::move(host));
    }
}

// ================================================================ la fiche ===
void HmiCommPane::rebuildProperties() {
    std::vector<PG::Category> cats;
    const Side s = side();
    sheetNote_.clear();
    switch (s) {
        case Side::Plc: rebuildPlcProperties(cats); break;
        case Side::Equipment: rebuildEquipmentProperties(cats); break;
        case Side::Bound: rebuildBoundProperties(cats); break;
        case Side::Port: rebuildPortProperties(cats); break;
        case Side::Scan: rebuildScanProperties(cats); break;
        case Side::Map: rebuildMapProperties(cats); break;
        case Side::SimPort: rebuildSimPortProperties(cats); break;
        case Side::Values: valuesCtl_->properties(cats); break;
    }
    grid_->setShowDescriptionPane(s != Side::Port);
    grid_->setCategories(std::move(cats));
    refreshChecks();
    const bool port = s == Side::Port && !port_.empty();
    checks_->setVisibility(port ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    applyButton_->setVisibility(port ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    cancelButton_->setVisibility(port ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    // 1.9 : la carte violette, en tete de la fiche d'un esclave simule lie.
    const auto* e = s == Side::Equipment && slaveRow_ ? equipmentOf(equipment_) : nullptr;
    const bool card = e && e->linkedSlave();
    if (card) slaveCard_->setEquipment(e->name);
    slaveCard_->setVisibility(card ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    shownDemoState_ = demoState();
    shownSide_ = std::to_string(static_cast<int>(s)) + "|" + port_ + "|" + equipment_ + (slaveRow_ ? "|esclave" : "");
    if (tools_) tools_->invalidate();
    onLayout();
    invalidate();
}

// =========================================================== mise en page ===
void HmiCommPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(480.f, b.w * 0.3f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    const float gx = b.x + b.w - gridW;
    float gy = b.y + 38;
    // 1.9 : la carte de l'esclave au-dessus de la fiche ; la note dessous.
    if (slaveCard_ && slaveCard_->visible()) {
        const float ch = std::min(h * 0.45f, slaveCard_->heightFor(gridW));
        slaveCard_->setBounds({gx, gy, gridW, ch});
        gy += ch;
        h = std::max(0.f, h - ch);
    }
    if (!sheetNote_.empty()) h = std::max(0.f, h - noteHeightFor(sheetNote_, gridW));
    if (checks_->visible()) {
        std::size_t rows = 0;
        for (const auto& c : grid_->categories()) rows += 1 + c.properties.size();
        const float gridH = std::min(h * 0.55f, static_cast<float>(rows) * 24.f + 8.f);
        grid_->setBounds({gx, gy, gridW, gridH});
        const float checkH = std::min(std::max(0.f, h - gridH - 50.f), checks_->heightFor(gridW));
        checks_->setBounds({gx, gy + gridH, gridW, checkH});
        const float by = gy + gridH + checkH + 8.f;
        applyButton_->setBounds({gx + 14.f, by, 104.f, 30.f});
        cancelButton_->setBounds({gx + 128.f, by, 92.f, 30.f});
    } else {
        grid_->setBounds({gx, gy, gridW, h});
    }
}

void HmiCommPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    // 1.9 : le violet des lignes simulees suit le theme ; une bascule se dit.
    if (ctx.theme.isDark() != darkTheme_) {
        darkTheme_ = ctx.theme.isDark();
        refreshEquipments();
    }
    noteSwitches(ctx.time);
    // 1.9 : la note sous la fiche (Detacher..., Dans l'application...).
    if (!sheetNote_.empty()) {
        const auto g = grid_->bounds();
        const float nh = noteHeightFor(sheetNote_, g.w);
        const gfx::Rect n{g.x, g.bottom(), g.w, nh};
        ctx.r.fillRect(n, ctx.theme.color.panelBg);
        ctx.r.line({n.x, n.y}, {n.right(), n.y}, ctx.theme.color.border, 1.f);
        float y = n.y + 7.f;
        for (const auto& l : noteLines(sheetNote_, g.w - 20.f)) {
            ctx.r.drawText({n.x + 10.f, y}, l, kNoteFont, ctx.theme.color.textMuted);
            y += ui::lineHeight(kNoteFont) + 3.f;
        }
    }
    // Les liaisons en marche : l'etat, les qualites, chaque seconde.
    if (ctx.time - lastLive_ >= 1.0) {
        lastLive_ = ctx.time;
        refreshState();
        const int tab = static_cast<int>(tabs_->currentIndex());
        if (tab == TPlan && ((hosts_.comm && hosts_.comm() && hosts_.comm()->link()) || !doc_->project.equipments.empty())) refreshPlan();
        if (tab == TEquipments) refreshEquipments();
        if (tab == TBound) refreshBound();
        if (tab == TNetwork) {
            refreshDiagram();
            if (auto* h = host(); h && ctx.time - lastNet_ >= 5.0) {
                lastNet_ = ctx.time;
                h->refreshAdapters();
            }
        }
        // L'etat du serveur de demonstration (dans la grille) : quand il change.
        if (side() == Side::Plc && demoState() != shownDemoState_) rebuildProperties();
        if (side() == Side::Equipment && !grid_->editing()) rebuildProperties();
        // Un changement d'adresse vient de finir : le dire, relire les ports.
        if (auto* h = host()) {
            const auto st = h->applyState();
            if (st.done && st.at != shownApplyAt_) {
                shownApplyAt_ = st.at;
                if (st.ok) {
                    say("Port " + st.adapter + " : " + (st.config.dhcp ? std::string("adresse automatique (DHCP)") : st.config.ip + " / " + st.config.mask)
                        + " appliqu\xC3\xA9" "e. Remettre l'adresse d'avant revient \xC3\xA0 " + (st.before.dhcp ? std::string("DHCP") : st.before.ip + " / " + st.before.mask) + ".");
                    draft_.edited = false;
                } else {
                    say("Port " + st.adapter + " : " + st.why, true);
                }
                h->refreshAdapters();
                lastNet_ = ctx.time - 4.0;       // relire les ports dans une seconde
            }
            if (side() == Side::Port && !grid_->editing()) {
                if (!draft_.edited) loadPortDraft();
                rebuildProperties();
            }
        }
    }
    // Le scanner : dix fois par seconde tant qu'il tourne.
    if (scanner_ && (scanner_->running() || scanWasRunning_)) refreshScan();
    // Lot 17 : la carte en direct, la detection des zones.
    tickLot17(ctx.time);
}

// 1.9 : L'AVIS D'UNE BASCULE, en bas a droite du volet, quelques secondes : l'heure
// et l'equipement, ce qui se passe (le violet du simule, la fiole).
void HmiCommPane::onPaintOverlay(const ui::PaintContext& ctx) {
    if (toast_.text.empty() || ctx.time >= toast_.until) return;
    if (toast_.since < 0) toast_.since = ctx.time;
    const float fade = static_cast<float>(std::clamp(std::min((ctx.time - toast_.since) / 0.25, (toast_.until - ctx.time) / 0.5), 0.0, 1.0));
    auto& r = ctx.r;
    const auto b = bounds();
    const gfx::FontId font{13};
    const float w = std::min(460.f, b.w - 40.f);
    const auto lines = noteLines(toast_.text, w - 46.f);
    const float lh = ui::lineHeight(font) + 3.f;
    const float h = 16.f + lh * static_cast<float>(lines.size() + 1);
    const gfx::Rect t{b.right() - w - 14.f, b.bottom() - 24.f - h - 10.f, w, h};
    const auto a = [&](gfx::Color c) { return c.withAlpha(static_cast<std::uint8_t>(static_cast<float>(c.a) * fade)); };
    r.fillRoundedRect({t.x + 3.f, t.y + 5.f, t.w, t.h}, a(gfx::Color{0, 0, 0, 110}), 7.f);
    r.fillRoundedRect(t, a(ctx.theme.brand.cardBorder), 7.f);
    r.fillRoundedRect({t.x + 1.f, t.y + 1.f, t.w - 2.f, t.h - 2.f}, a(ctx.theme.brand.card), 6.f);
    r.fillRect({t.x + 1.f, t.y + 4.f, 3.f, t.h - 8.f}, a(simmark::kLine));
    const gfx::Color violet = simmark::text(ctx.theme);
    simmark::drawFlask(r, {t.x + 12.f, t.y + 9.f, 16.f, 16.f}, a(violet));
    r.drawText({t.x + 36.f, t.y + 8.f}, toast_.title, font, a(violet));
    r.drawText({t.x + 36.6f, t.y + 8.f}, toast_.title, font, a(violet));
    float y = t.y + 8.f + lh;
    for (const auto& l : lines) {
        r.drawText({t.x + 36.f, y}, l, font, a(ctx.theme.color.text));
        y += lh;
    }
    invalidate();
}

// Lot 17 : Suppr supprime l'equipement choisi (Equipements ; le schema a le sien) ;
// 1.9 : sur la ligne de l'esclave simule, il le retire (decocher Cloner en esclave
// simule : Ctrl+Z le rend). F2 : le nom, dans la fiche (le nom affiche de l'esclave).
ui::EventResult HmiCommPane::onEvent(const ui::InputEvent& ev) {
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (!k || !k->mods.none()) return ui::EventResult::Ignored;
    const int tab = static_cast<int>(tabs_->currentIndex());
    if (tab != TEquipments || equipment_.empty() || equipment_ == kPlcKey) return ui::EventResult::Ignored;
    if (k->key == ui::Key::Delete) {
        if (slaveRow_) {
            if (const auto* e = equipmentOf(equipment_); e && e->linkedSlave()) (void)setEquipmentField(e->name, "esclave_simule", "0");
            return ui::EventResult::Consumed;
        }
        if (!askDeleteEquipment(equipment_) && !hosts_.ask) (void)deleteEquipment(equipment_);
        return ui::EventResult::Consumed;
    }
    if (k->key == ui::Key::F2) {
        const std::string what = slaveRow_ ? "Nom affich\xC3\xA9" : "Nom";
        if (grid_->revealValue(what)) say("Tape le nouveau nom \xC3\xA0 droite (" + what + ").");
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
