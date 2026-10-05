// app/hmi/HmiCyclicPage.cpp - IHM > Outil Modbus > Lecture cyclique (1.9) : la page.
//  Les requetes (le jeu de travail), la lecture (mbtool::MultiPoller), les trois
//  blocs (requetes, courbes, valeurs / journal), la grille de droite. Les jeux,
//  l'export, le collage, les zones et les variables : HmiCyclicPageSets.cpp.
#include "HmiCyclicPage.hpp"

#include "HmiEquipmentHost.hpp"
#include "HmiModbusToolPane.hpp"
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiTwin.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace app {

using namespace hmikit;
using PG = ui::PropertyGrid;

namespace {

const char* const kPlcChoice = "automate du projet";
const char* const kTypedChoice = "adresse tap\xC3\xA9" "e";
const char* const kCommon = "commune";
const char* const kLowFirst = "poids faible d'abord (Schneider)";
const char* const kHighFirst = "poids fort d'abord";
const char* const kByVariables = "d'apr\xC3\xA8s les variables";
const char* const kPerTarget = "une connexion par \xC3\xA9quipement (en m\xC3\xAAme temps)";
const char* const kSerial = "toutes l'une apr\xC3\xA8s l'autre";
const char* const kDash = "\xE2\x80\x94";
const char* const kDot = " \xC2\xB7 ";

enum : int {
    HAddLink = 1,                       // la bande des requetes
    HLanes = 11, HOneScale, HFrozen,    // la bande des courbes
    HValues = 21, HJournal, HOnlySelected,
};

double wallNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string msShort(double ms) {
    if (ms <= 0) return kDash;
    if (ms < 0.1) return "< 0,1 ms";
    return mbtool::frenchNumber(ms, ms < 10 ? 1 : 0) + " ms";
}

std::string countText(std::uint64_t n) { return mbtool::frenchNumber(static_cast<double>(n), 0); }

// La colonne "Il y a" (depuis le dernier changement) : "0,4 s", "12 s", "2 min",
// "1 h" (comme la maquette ; le titre dit deja "il y a").
std::string agoText(double since, double now) {
    if (since <= 0) return kDash;
    const double s = std::max(0.0, now - since);
    if (s < 10) return mbtool::frenchNumber(s, 1) + " s";
    if (s < 60) return std::to_string(static_cast<int>(s)) + " s";
    if (s < 3600) return std::to_string(static_cast<int>(s / 60)) + " min";
    return std::to_string(static_cast<int>(s / 3600)) + " h";
}

// Un entier tape ("1 000", "500 ms") ; faux : illisible.
bool parseNumber(const std::string& text, long long& out) {
    std::string digits;
    bool neg = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') digits += c;
        else if (c == '-' && digits.empty()) neg = true;
        else if (c == ' ' || c == '\t' || static_cast<unsigned char>(c) >= 0x80) continue;    // espaces, espaces fines
        else if (!digits.empty()) break;
    }
    if (digits.empty() || digits.size() > 12) return false;
    out = std::atoll(digits.c_str()) * (neg ? -1 : 1);
    return true;
}

bool bitFunction(int f) { return f == 1 || f == 2; }

std::string functionChoice(int f) { return mbtool::functionLong(f); }

std::vector<std::string> functionChoices() { return {functionChoice(1), functionChoice(2), functionChoice(3), functionChoice(4)}; }

// Les colonnes du tableau des valeurs, a la largeur du tableau. Chaque titre
// s'ecrit en entier (la TableView garde 26 px a droite du titre pour la fleche
// de tri) ; le Nom et la Valeur se partagent le reste. Avant : 1110 px fixes,
// et a 1920 x 1080 (910 px pour le tableau) "Changements" et "Il y a" passaient
// hors du tableau. Les deux premieres colonnes (la case Tracer, la pastille de
// la courbe) n'ont pas de titre, comme la colonne Active des requetes. La
// colonne Schneider ne se montre que si une valeur en a une (une requete vers
// l'automate du projet) : sinon elle ne dirait que des tirets.
std::vector<ui::TableView::Column> valueColumns(float width, bool schneider) {
    const gfx::FontId bold{16};                       // Theme::font.uiBold : les titres des colonnes
    const auto fit = [&](const char* title, float least) {
        return std::max(least, std::ceil(ui::measureWidth(title, bold)) + 8.f + 26.f + 4.f);
    };
    std::vector<ui::TableView::Column> cols = {
        {"", 30.f}, {"", 22.f}, {"R", fit("R", 40.f)}, {"Adresse", fit("Adresse", 70.f)}, {"Schneider", fit("Schneider", 80.f)},
        {"Nom", 0.f}, {"Valeur", 0.f}, {"Min", fit("Min", 72.f)}, {"Max", fit("Max", 72.f)}, {"Moyenne", fit("Moyenne", 72.f)},
        {"Changements", fit("Changements", 72.f)}, {"Il y a", fit("Il y a", 64.f)}};
    cols[4].visible = schneider;
    float fixed = 0.f;
    for (const auto& c : cols)
        if (c.visible) fixed += c.width;
    const float rest = std::max(0.f, std::floor(width) - 14.f - fixed);     // 14 : la barre de defilement
    cols[6].width = std::clamp(std::floor(rest * 0.42f), 110.f, 170.f);
    cols[5].width = std::max(96.f, rest - cols[6].width);
    for (auto& c : cols) c.minWidth = std::min(c.minWidth, c.width);
    return cols;
}

// Les colonnes du tableau des requetes, a la largeur du tableau. Chaque titre
// s'ecrit en entier (comme la maquette : "Nb" pour le nombre) ; le Nom et la Cible
// se partagent le reste. La Fonction (l'adresse Modicon la dit deja : 4xxxx = 3,
// 3xxxx = 4 ; la grille la dit en entier) puis les Lectures (aussi dans la grille)
// ne se montrent que s'il reste la place. Avant : 1200 px fixes, puis des titres
// coupes ("Fo...", "...") a 1920 x 1080 (910 px pour le tableau).
std::vector<ui::TableView::Column> requestColumns(float width) {
    const gfx::FontId bold{16};                       // Theme::font.uiBold : les titres des colonnes
    const auto fit = [&](const char* title, float least) {
        return std::max(least, std::ceil(ui::measureWidth(title, bold)) + 8.f + 26.f + 4.f);
    };
    std::vector<ui::TableView::Column> cols = {
        {"", 30.f}, {"R", 40.f}, {"Nom", 0.f}, {"Cible", 0.f}, {"Fonction", fit("Fonction", 88.f)}, {"Adresse", 104.f},
        {"Nb", fit("Nb", 48.f)}, {"Format", fit("Format", 92.f)}, {"P\xC3\xA9riode", fit("P\xC3\xA9riode", 86.f)},
        {"\xC3\x89tat", 150.f}, {"Lectures", fit("Lectures", 84.f)}};
    float fixed = 0.f;
    for (std::size_t c = 0; c < cols.size(); ++c)
        if (c != 4 && c != 10) fixed += cols[c].width;
    float rest = std::max(0.f, std::floor(width) - 14.f - fixed);           // 14 : la barre de defilement
    const float names = 120.f + 160.f;                                       // le Nom et la Cible, au moins
    cols[4].visible = rest >= names + cols[4].width;
    if (cols[4].visible) rest -= cols[4].width;
    cols[10].visible = rest >= names + cols[10].width;
    if (cols[10].visible) rest -= cols[10].width;
    cols[2].width = std::max(110.f, std::floor(rest * 0.44f));
    cols[3].width = std::max(140.f, rest - cols[2].width);
    for (auto& c : cols) c.minWidth = std::min(c.minWidth, c.width);
    return cols;
}

// La periode dans le tableau des requetes : "500 ms", "1 s", "1,5 s" (comme la
// maquette ; "1 000 ms" ne tenait pas dans la colonne).
std::string periodShort(int ms) {
    if (ms >= 1000 && ms % 100 == 0) return mbtool::frenchNumber(ms / 1000.0, ms % 1000 ? 1 : 0) + " s";
    return mbtool::frenchNumber(ms, 0) + " ms";
}

// Le min et le max d'une valeur : comme la valeur, mais un nombre a virgule (REAL,
// variable mise a l'echelle) garde deux decimales, comme la moyenne ("233,4567"
// sortait de la colonne).
std::string statText(const mbtool::ValueSpec& spec, double x) {
    if (spec.kind == mbtool::ValueKind::Number && std::isfinite(x) && (spec.scaled || spec.format == mbtool::Format::Float32))
        return mbtool::frenchNumber(x, 2);
    return mbtool::valueText(spec, x, {}, false);
}

} // namespace

// =================================================================== la page ===
HmiCyclicPage::HmiCyclicPage(std::string id, HmiModbusToolPane& tool) : ui::Widget(std::move(id)), tool_(tool) {
    const std::string base = this->id();
    headRequests_ = &static_cast<HmiBlockHead&>(addChild(std::make_unique<HmiBlockHead>(base + ".headRequests")));
    requests_ = &static_cast<HmiCyclicTable&>(addChild(std::make_unique<HmiCyclicTable>(base + ".requests")));
    headChart_ = &static_cast<HmiBlockHead&>(addChild(std::make_unique<HmiBlockHead>(base + ".headChart")));
    chart_ = &static_cast<HmiLaneChart&>(addChild(std::make_unique<HmiLaneChart>(base + ".chart")));
    headLower_ = &static_cast<HmiBlockHead&>(addChild(std::make_unique<HmiBlockHead>(base + ".headLower")));
    lower_ = &static_cast<HmiCyclicTable&>(addChild(std::make_unique<HmiCyclicTable>(base + ".lower")));
    requests_->setBoxColumn(0);
    lower_->setBoxColumn(0);
    requests_->setColumns(requestColumns(0.f));
    set_.name = "Sans nom";

    links_ += requests_->boxClicked->connect([this](ui::RowIndex row, int) {
        if (row < rows_.size()) (void)setActive(rows_[row].id, !rows_[row].read.active);
        else requests_->rowClicked->emit(row);       // la ligne "+ Ajouter..." commence dans la colonne des cases
    });
    links_ += requests_->rowClicked->connect([this](ui::RowIndex row) {
        if (row < rows_.size()) (void)select(rows_[row].id);
        else if (row == rows_.size()) {               // la ligne "+ Ajouter une requete..."
            gfx::Rect rr;
            const gfx::Point at = requests_->rowRect(row, rr) ? gfx::Point{rr.x + 40, rr.y + rr.h} : gfx::Point{bounds().x + 40, bounds().y + 80};
            openAddMenu(at);
        }
    });
    links_ += requests_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
        if (!sel.empty() && sel.front() < rows_.size() && rows_[sel.front()].id != selected_) (void)select(rows_[sel.front()].id);
    });
    links_ += lower_->boxClicked->connect([this](ui::RowIndex row, int) {
        if (journal_ || row >= valueRows_.size()) return;
        const auto& v = valueRows_[row];
        if (v.traceable && v.value >= 0) (void)setTraced(v.request, static_cast<std::size_t>(v.value), !v.traced);
    });
    links_ += lower_->rowClicked->connect([this](ui::RowIndex row) {
        if (!journal_ && row < valueRows_.size() && valueRows_[row].request != selected_ && !onlySelected_) (void)select(valueRows_[row].request);
    });
    links_ += headChart_->clicked->connect([this](int part) {
        if (part == HLanes) setLanes(true);
        else if (part == HOneScale) setLanes(false);
        else if (part == HFrozen) setFrozen(false);
    });
    links_ += headLower_->clicked->connect([this](int part) {
        if (part == HValues) showJournal(false);
        else if (part == HJournal) showJournal(true);
        else if (part == HOnlySelected) setOnlySelected(!onlySelected_);
    });
    links_ += headRequests_->clicked->connect([this](int part) {
        if (part == HAddLink) openAddMenu({headRequests_->partRect(HAddLink).x, headRequests_->partRect(HAddLink).y + 24});
    });
    refreshRequests();
    refreshLower();
    refreshHeads();
}

HmiCyclicPage::~HmiCyclicPage() {
    poller_.stopRecording();
    poller_.stop();
}

// ------------------------------------------------------------- les requetes ---
const HmiCyclicPage::Row* HmiCyclicPage::row(int id) const {
    for (const auto& r : rows_)
        if (r.id == id) return &r;
    return nullptr;
}

int HmiCyclicPage::nextId() const {
    int n = 0;
    for (const auto& r : rows_) n = std::max(n, r.id);
    return n + 1;
}

bool HmiCyclicPage::select(int id) {
    if (id != 0 && !row(id)) return false;
    selected_ = id;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) requests_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    refreshLower();
    tool_.rebuildProperties();
    return true;
}

std::string HmiCyclicPage::valueName(const Row& r, int offset, int bit) const {
    for (const auto& v : r.read.values)
        if (v.offset == offset && v.bit == bit) return v.name;
    // L'automate : la variable localisee qui porte ce mot (ou ce bit).
    if (r.read.target == "automate" && tool_.hosts_.plcVariables) {
        const std::string place = mbtool::schneiderOf(r.read.function, r.read.address + offset, bit);
        if (place.empty()) return {};
        if (plcNames_.empty())
            for (const auto& v : tool_.hosts_.plcVariables()) plcNames_.emplace(v.address, v.name);
        if (const auto it = plcNames_.find(place); it != plcNames_.end()) return it->second;
    }
    // Un equipement (ou son esclave simule) : la variable IHM liee a cette place.
    if (!r.equipment.empty() && tool_.hosts_.project) {
        if (equipNames_.empty()) {
            equipNames_.emplace("", "");            // calcule (meme sans variable liee)
            if (const hmi::Project* p = tool_.hosts_.project())
                for (const auto& e : p->equipments) {
                    if (!e.modbus()) continue;
                    const auto plan = hmi::equip::buildPlan(*p, e);
                    for (const auto& pt : plan.points()) {
                        if (pt.array) continue;
                        const int fn = pt.area == hmi::comm::Area::Coils ? 1 : pt.area == hmi::comm::Area::DiscreteInputs ? 2
                                     : pt.area == hmi::comm::Area::InputRegisters ? 4 : 3;
                        const int b = pt.encoding == hmi::comm::Encoding::BitOfWord ? pt.bit : -1;
                        equipNames_.emplace(e.name + "|" + std::to_string(fn) + "|" + std::to_string(pt.offset) + "|" + std::to_string(b), pt.name);
                    }
                }
        }
        const std::string key = r.equipment + "|" + std::to_string(r.read.function) + "|" + std::to_string(r.read.address + offset) + "|" + std::to_string(bit);
        if (const auto it = equipNames_.find(key); it != equipNames_.end()) return it->second;
    }
    return {};
}

void HmiCyclicPage::resolve(Row& r) const {
    r.request.reset();
    r.why.clear();
    r.slave = false;
    r.schneider = false;
    r.equipment.clear();
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    EquipmentHost* eh = tool_.hosts_.equipments ? tool_.hosts_.equipments() : nullptr;
    const auto& q = r.read;
    mbtool::Target t;
    if (q.target == "automate") {
        r.targetLabel = "API";
        if (!p || !p->comm.modbus()) {
            r.why = "le projet n'a pas de liaison Modbus vers l'automate";
        } else {
            t.host = p->comm.host;
            t.port = p->comm.port;
            t.unit = p->comm.unit;
            t.timeoutMs = p->comm.timeoutMs;
            r.schneider = true;
        }
    } else if (q.target == "equipement" || q.target == "esclave") {
        const hmi::Equipment* e = p ? p->equipmentByName(q.equipment) : nullptr;
        r.equipment = q.equipment;
        r.targetLabel = q.equipment;
        if (!e) {
            r.why = "pas d'\xC3\xA9quipement \xC2\xAB " + q.equipment + " \xC2\xBB";
        } else if (!e->modbus()) {
            r.why = e->name + " n'est pas un \xC3\xA9quipement Modbus TCP";
        } else {
            r.equipment = e->name;
            const bool slave = q.target == "esclave" || e->simulated;
            r.targetLabel = q.target == "esclave" && !e->simulated ? e->twinLabel() : e->name;
            if (slave) {
                r.slave = true;
                const int port = eh ? eh->simulatedPort(e->name) : 0;
                if (!e->hasTwin()) r.why = e->name + " n'a pas d'esclave simul\xC3\xA9";
                else if (port <= 0) r.why = "l'esclave simul\xC3\xA9 de " + e->name + " n'est pas en marche";
                else {
                    t.host = "127.0.0.1";
                    t.port = port;
                }
            } else {
                t.host = e->host;
                t.port = e->port;
            }
            t.unit = e->unit;
            t.timeoutMs = e->timeoutMs;
        }
    } else {
        r.targetLabel = q.host + ":" + std::to_string(q.port);
        t.host = q.host;
        t.port = q.port;
        t.unit = q.unit;
        t.timeoutMs = q.timeoutMs;
    }
    if (!r.why.empty()) return;
    mbtool::CyclicRequest c;
    c.id = r.id;
    c.name = q.name;
    c.active = q.active;
    c.target = t;
    c.targetLabel = r.targetLabel;
    c.function = q.function;
    c.address = q.address;
    c.count = q.count;
    c.lowFirst = q.lowFirst;
    c.periodMs = q.periodMs;
    if (q.format == "variables" && !q.values.empty()) {
        for (const auto& v : q.values) {
            auto spec = mbtool::specForType(v.name, v.type, v.offset, v.bit, v.words);
            // Une variable IHM liee mise a l'echelle : sa valeur se montre mise a l'echelle.
            if (p && !r.equipment.empty())
                for (const auto& var : p->programs.variables)
                    if (var.equipment == r.equipment && var.name == v.name && var.scaled()) {
                        spec.scaled = true;
                        spec.rawMin = var.rawMin;
                        spec.rawMax = var.rawMax;
                        spec.scaleMin = var.engMin;
                        spec.scaleMax = var.engMax;
                    }
            c.values.push_back(std::move(spec));
        }
    } else {
        const auto f = mbtool::formatFromKey(q.format).value_or(mbtool::Format::Unsigned);
        c.values = mbtool::valuesForFormat(q.function, q.count, f, [&](int offset) { return valueName(r, offset, -1); });
    }
    std::string why;
    if (!mbtool::validRequest(c, &why)) {
        r.why = why;
        return;
    }
    r.request = std::move(c);
}

void HmiCyclicPage::assignColors() {
    int next = 0;
    for (auto& r : rows_) {
        const std::size_t n = r.request ? r.request->values.size() : 0;
        r.colors.assign(n, -1);
        for (const int k : r.read.traced)
            if (k >= 0 && static_cast<std::size_t>(k) < n) r.colors[static_cast<std::size_t>(k)] = next++;
    }
}

mbtool::CyclicOptions HmiCyclicPage::options() const {
    mbtool::CyclicOptions o;
    o.periodMs = set_.periodMs;
    o.maxFailures = set_.maxFailures;
    o.pauseMs = set_.pauseMs;
    o.perTarget = set_.perTarget;
    return o;
}

void HmiCyclicPage::pushToPoller() {
    std::vector<mbtool::CyclicRequest> list;
    for (const auto& r : rows_)
        if (r.request) list.push_back(*r.request);
    poller_.configure(std::move(list), options());
}

void HmiCyclicPage::changed(bool structure) {
    if (structure) {
        plcNames_.clear();
        equipNames_.clear();
    }
    for (auto& r : rows_) resolve(r);
    assignColors();
    pushToPoller();
    states_ = poller_.states();
    refreshRequests();
    refreshLower();
    refreshHeads();
    refreshChart(wallNow());
    tool_.rebuildProperties();
    tool_.tabs_->setTabBadge(HmiModbusToolPane::TCyclic, badge(), ui::Tone::None);
}

void HmiCyclicPage::remember() {
    undo_.push_back({rows_, selected_});
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

bool HmiCyclicPage::undoList() {
    if (undo_.empty()) return false;
    redo_.push_back({rows_, selected_});
    rows_ = std::move(undo_.back().rows);
    selected_ = undo_.back().selected;
    undo_.pop_back();
    changed(true);
    say("Annul\xC3\xA9 (la liste des requ\xC3\xAAtes).");
    return true;
}

bool HmiCyclicPage::redoList() {
    if (redo_.empty()) return false;
    undo_.push_back({rows_, selected_});
    rows_ = std::move(redo_.back().rows);
    selected_ = redo_.back().selected;
    redo_.pop_back();
    changed(true);
    say("R\xC3\xA9tabli (la liste des requ\xC3\xAAtes).");
    return true;
}

int HmiCyclicPage::addRequest(hmi::ModbusRead read, std::string* why) {
    if (read.function < 1 || read.function > 4) {
        if (why) *why = "fonction " + std::to_string(read.function) + " : une lecture se fait par les fonctions 1 \xC3\xA0 4";
        return 0;
    }
    remember();
    Row r;
    r.id = nextId();
    if (read.name.empty()) read.name = "Requ\xC3\xAAte " + std::to_string(r.id);
    if (read.traced.empty()) read.traced.push_back(0);
    r.read = std::move(read);
    rows_.push_back(std::move(r));
    selected_ = rows_.back().id;
    changed(true);
    select(selected_);
    return rows_.back().id;
}

int HmiCyclicPage::addNew(std::string* why) {
    hmi::ModbusRead q;
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    // La cible de la requete choisie, sinon l'automate, sinon le premier equipement Modbus.
    if (const Row* cur = row(selected_)) {
        q.target = cur->read.target;
        q.equipment = cur->read.equipment;
        q.host = cur->read.host;
        q.port = cur->read.port;
        q.unit = cur->read.unit;
        q.timeoutMs = cur->read.timeoutMs;
        q.lowFirst = cur->read.lowFirst;
        q.address = cur->read.address + cur->read.count;
    } else if (p && p->comm.modbus()) {
        q.target = "automate";
    } else {
        q.target = "adresse";
        if (p)
            for (const auto& e : p->equipments)
                if (e.modbus()) {
                    q.target = "equipement";
                    q.equipment = e.name;
                    q.lowFirst = e.wordOrder != "fort";
                    break;
                }
    }
    return addRequest(std::move(q), why);
}

int HmiCyclicPage::addFromReadTab(std::string* why) {
    hmi::ModbusRead q;
    const auto& query = tool_.query_;
    q.function = query.function == 5 || query.function == 15 ? 1 : query.function == 6 || query.function == 16 || query.function == 43 ? 3 : query.function;
    q.address = query.address;
    q.count = query.count;
    q.format = std::string(mbtool::formatKey(tool_.format()));
    q.lowFirst = tool_.lowFirst_;
    const std::string& eq = tool_.equipment_;
    // La cible de Lecture / ecriture : "<Nom> - esclave simule" (avant la 1.9 : "<Nom> (jumeau)").
    std::string slaveOf;
    for (const std::string& suffix : {std::string(" \xC2\xB7 esclave simul\xC3\xA9"), std::string(" (jumeau)")})
        if (slaveOf.empty() && eq.size() > suffix.size() && eq.compare(eq.size() - suffix.size(), suffix.size(), suffix) == 0)
            slaveOf = eq.substr(0, eq.size() - suffix.size());
    if (eq == kPlcChoice) {
        q.target = "automate";
    } else if (!slaveOf.empty()) {
        q.target = "esclave";
        q.equipment = slaveOf;
    } else if (!eq.empty()) {
        q.target = "equipement";
        q.equipment = eq;
    } else {
        q.target = "adresse";
        q.host = tool_.target_.host;
        q.port = tool_.target_.port;
        q.unit = tool_.target_.unit;
        q.timeoutMs = tool_.target_.timeoutMs;
    }
    q.name = "Lecture " + mbtool::modiconOf(q.function, q.address);
    return addRequest(std::move(q), why);
}

bool HmiCyclicPage::duplicate(int id) {
    const Row* r = row(id);
    if (!r) return false;
    hmi::ModbusRead q = r->read;
    q.name += " (copie)";
    return addRequest(std::move(q)) != 0;
}

bool HmiCyclicPage::remove(int id) {
    const auto it = std::find_if(rows_.begin(), rows_.end(), [id](const Row& r) { return r.id == id; });
    if (it == rows_.end()) return false;
    remember();
    const std::size_t at = static_cast<std::size_t>(it - rows_.begin());
    const std::string name = "R" + std::to_string(id);
    rows_.erase(it);
    selected_ = rows_.empty() ? 0 : rows_[std::min(at, rows_.size() - 1)].id;
    changed(true);
    select(selected_);
    say(name + " retir\xC3\xA9" "e (Ctrl+Z la remet).");
    return true;
}

bool HmiCyclicPage::setActive(int id, bool on) {
    for (auto& r : rows_)
        if (r.id == id) {
            if (r.read.active == on) return true;
            remember();
            r.read.active = on;
            changed(false);
            say("R" + std::to_string(id) + (on ? " remise dans la lecture." : " coup\xC3\xA9" "e (elle reste dans la liste)."));
            return true;
        }
    return false;
}

bool HmiCyclicPage::setTraced(int id, std::size_t value, bool on) {
    for (auto& r : rows_)
        if (r.id == id) {
            auto& t = r.read.traced;
            const int k = static_cast<int>(value);
            const bool has = std::find(t.begin(), t.end(), k) != t.end();
            if (has == on) return true;
            if (on) {
                t.push_back(k);
                std::sort(t.begin(), t.end());
            } else {
                t.erase(std::remove(t.begin(), t.end(), k), t.end());
            }
            assignColors();
            refreshLower();
            refreshHeads();
            refreshChart(wallNow());
            return true;
        }
    return false;
}

std::vector<std::string> HmiCyclicPage::targetChoices() const {
    std::vector<std::string> out;
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    if (p && p->comm.modbus()) out.push_back(kPlcChoice);
    if (p)
        for (const auto& e : p->equipments) {
            if (!e.modbus()) continue;
            out.push_back(e.name);
            if (e.linkedSlave()) out.push_back(e.twinLabel());
        }
    out.push_back(kTypedChoice);
    return out;
}

bool HmiCyclicPage::setField(int id, const std::string& key, const std::string& value, std::string* why) {
    Row* r = nullptr;
    for (auto& x : rows_)
        if (x.id == id) r = &x;
    const auto refuse = [&](std::string text) {
        if (why) *why = text;
        if (!quiet_) say(key + " : " + text, true);
        return false;
    };
    if (!r) return refuse("pas de requ\xC3\xAA" "te R" + std::to_string(id));
    hmi::ModbusRead q = r->read;
    long long n = 0;
    const std::string v = trimmed(value);
    if (key == "nom") {
        if (v.empty()) return refuse("un nom, s'il te pla\xC3\xAEt");
        q.name = v;
    } else if (key == "active") {
        q.active = yes(v) || v == "oui";
    } else if (key == "cible") {
        const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
        if (v == kPlcChoice || same(v, "automate") || same(v, "API")) {
            q.target = "automate";
        } else if (v == kTypedChoice || same(v, "adresse")) {
            q.target = "adresse";
        } else {
            const hmi::Equipment* found = nullptr;
            bool slave = false;
            if (p)
                for (const auto& e : p->equipments) {
                    if (same(e.name, v)) found = &e;
                    else if (e.linkedSlave() && same(e.twinLabel(), v)) {
                        found = &e;
                        slave = true;
                    }
                    if (found) break;
                }
            if (!found) return refuse("pas de cible \xC2\xAB " + v + " \xC2\xBB");
            q.target = slave ? "esclave" : "equipement";
            q.equipment = found->name;
            q.lowFirst = found->wordOrder != "fort";
        }
    } else if (key == "hote") {
        if (v.empty()) return refuse("une adresse IP ou un nom");
        q.target = "adresse";
        q.host = v;
    } else if (key == "port" || key == "esclave" || key == "delai") {
        if (!parseNumber(v, n)) return refuse("\xC2\xAB " + v + " \xC2\xBB n'est pas un nombre");
        if (key == "port" && (n < 1 || n > 65535)) return refuse("un port de 1 \xC3\xA0 65535");
        if (key == "esclave" && (n < 0 || n > 255)) return refuse("un esclave de 0 \xC3\xA0 255");
        if (key == "delai" && (n < 50 || n > 60000)) return refuse("un d\xC3\xA9lai de 50 \xC3\xA0 60000 ms");
        if (q.target != "adresse") return refuse("la cible suit son \xC3\xA9quipement : choisis \xC2\xAB adresse tap\xC3\xA9" "e \xC2\xBB pour la r\xC3\xA9gler");
        (key == "port" ? q.port : key == "esclave" ? q.unit : q.timeoutMs) = static_cast<int>(n);
    } else if (key == "fonction") {
        if (!parseNumber(v, n) || n < 1 || n > 4) return refuse("une fonction de lecture : 1 \xC3\xA0 4");
        if (q.format == "variables") q.values.clear(), q.format = "decimal";
        q.function = static_cast<int>(n);
    } else if (key == "adresse") {
        // 0..65535 ; 40101, 30001, 10001, 00001 (Modicon : la fonction suit) ; %MW100, %M10.
        std::string a = v;
        if (!a.empty() && a[0] == '%') {
            std::string up;
            for (const char c : a) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            long long num = 0;
            if (up.rfind("%MW", 0) == 0 && parseNumber(up.substr(3), num)) q.function = 3, n = num;
            else if (up.rfind("%IW", 0) == 0 && parseNumber(up.substr(3), num)) q.function = 4, n = num;
            else if (up.rfind("%M", 0) == 0 && parseNumber(up.substr(2), num)) q.function = 1, n = num;
            else if (up.rfind("%I", 0) == 0 && parseNumber(up.substr(2), num)) q.function = 2, n = num;
            else return refuse("\xC2\xAB " + v + " \xC2\xBB : %MW, %M, %IW ou %I suivi d'un num\xC3\xA9ro");
        } else {
            if (!parseNumber(a, n) || n < 0) return refuse("\xC2\xAB " + v + " \xC2\xBB n'est pas une adresse");
            if ((a.size() == 5 || a.size() == 6) && n >= 1) {
                // Une adresse Modicon : 4xxxx(x) maintien, 3xxxx entrees, 1xxxx entrees TOR, 0xxxx bobines.
                const char head = a[0];
                const long long rest = std::atoll(a.substr(1).c_str());
                if (head == '4' && rest >= 1) q.function = 3, n = rest - 1;
                else if (head == '3' && rest >= 1) q.function = 4, n = rest - 1;
                else if (head == '1' && rest >= 1) q.function = 2, n = rest - 1;
                else if (head == '0' && rest >= 1) q.function = 1, n = rest - 1;
            }
        }
        if (n > 65535) return refuse("une adresse de 0 \xC3\xA0 65535");
        q.address = static_cast<int>(n);
    } else if (key == "nombre") {
        if (!parseNumber(v, n) || n < 1) return refuse("un nombre de 1 \xC3\xA0 125 mots (2000 bits)");
        if (!bitFunction(q.function) && n > 125) return refuse("125 mots au plus par requ\xC3\xAAte");
        if (bitFunction(q.function) && n > 2000) return refuse("2000 bits au plus par requ\xC3\xAAte");
        q.count = static_cast<int>(n);
        if (q.format == "variables") q.values.clear(), q.format = "decimal";
    } else if (key == "format") {
        if (v == kByVariables || v == "variables") {
            if (q.values.empty()) return refuse("cette requ\xC3\xAAte ne vient pas de variables");
            q.format = "variables";
        } else {
            std::optional<mbtool::Format> f = mbtool::formatFromKey(v);
            if (!f) {
                const auto& labels = mbtool::formatLabels();
                for (std::size_t i = 0; i < labels.size(); ++i)
                    if (same(labels[i], v)) f = static_cast<mbtool::Format>(i);
            }
            if (!f) return refuse("format inconnu : " + v);
            q.format = std::string(mbtool::formatKey(*f));
        }
    } else if (key == "ordre") {
        q.lowFirst = !(v == kHighFirst || same(v, "fort"));
    } else if (key == "periode") {
        if (v.empty() || same(v, kCommon) || v.rfind(kCommon, 0) == 0 || v == "0") {
            q.periodMs = 0;
        } else {
            if (!parseNumber(v, n) || n < 20 || n > 3600000) return refuse("une p\xC3\xA9riode de 20 ms \xC3\xA0 1 h, ou 0 (la p\xC3\xA9riode commune)");
            q.periodMs = static_cast<int>(n);
        }
    } else if (key == "tracer") {
        // "aucune", "toutes", "la premiere", ou "rang,oui|non".
        const std::size_t values = r->request ? r->request->values.size() : 0;
        if (same(v, "aucune") || v.rfind("aucune", 0) == 0) {
            q.traced.clear();
        } else if (same(v, "toutes") || v.rfind("toutes", 0) == 0) {
            q.traced.clear();
            for (std::size_t k = 0; k < values; ++k) q.traced.push_back(static_cast<int>(k));
        } else if (v.rfind("la premi", 0) == 0) {
            q.traced = {0};
        } else {
            const auto comma = v.find(',');
            long long k = 0;
            if (comma == std::string::npos || !parseNumber(v.substr(0, comma), k) || k < 0) return refuse("rang,oui ou rang,non");
            return setTraced(id, static_cast<std::size_t>(k), yes(trimmed(v.substr(comma + 1))) || trimmed(v.substr(comma + 1)) == "oui");
        }
    } else {
        return refuse("r\xC3\xA9glage inconnu");
    }
    if (q == r->read) return true;
    if (quiet_) {                       // un brouillon (coller) : ni annuler, ni rafraichir
        r->read = std::move(q);
        resolve(*r);
        return true;
    }
    remember();
    r->read = std::move(q);
    changed(false);
    return true;
}

bool HmiCyclicPage::setFieldQuiet(int id, const std::string& key, const std::string& value, std::string* why) {
    quiet_ = true;
    const bool ok = setField(id, key, value, why);
    quiet_ = false;
    return ok;
}

bool HmiCyclicPage::setOption(const std::string& key, const std::string& value, std::string* why) {
    const auto refuse = [&](std::string text) {
        if (why) *why = text;
        say(key + " : " + text, true);
        return false;
    };
    long long n = 0;
    const std::string v = trimmed(value);
    auto s = set_;
    if (key == "periode_commune") {
        if (!parseNumber(v, n) || n < 20 || n > 3600000) return refuse("une p\xC3\xA9riode de 20 ms \xC3\xA0 1 h");
        s.periodMs = static_cast<int>(n);
    } else if (key == "enchainement") {
        s.perTarget = !(v == kSerial || same(v, "serie"));
    } else if (key == "pause") {
        if (!parseNumber(v, n) || n < 0 || n > 60000) return refuse("une pause de 0 \xC3\xA0 60000 ms");
        s.pauseMs = static_cast<int>(n);
    } else if (key == "echecs") {
        if (!parseNumber(v, n) || n < 0 || n > 100000) return refuse("un nombre d'\xC3\xA9" "checs (0 : jamais de pause)");
        s.maxFailures = static_cast<int>(n);
    } else if (key == "fenetre") {
        if (!parseNumber(v, n) || n < 10 || n > 900) return refuse("une fen\xC3\xAAtre de 10 \xC3\xA0 900 s");
        s.windowS = static_cast<int>(n);
    } else if (key == "pistes") {
        s.lanes = yes(v) || v == "oui";
    } else if (key == "enregistrer") {
        return setRecording(yes(v) || v == "oui", why);
    } else {
        return refuse("r\xC3\xA9glage inconnu");
    }
    if (s == set_) return true;
    set_ = s;
    pushToPoller();
    refreshRequests();
    refreshHeads();
    refreshChart(wallNow());
    tool_.rebuildProperties();
    return true;
}

// ---------------------------------------------------------------- la lecture ---
bool HmiCyclicPage::start(std::string* why) {
    changed(false);        // les esclaves simules ont pu demarrer depuis : les cibles se refont
    std::size_t ready = 0;
    std::string firstWhy;
    for (const auto& r : rows_) {
        if (r.request && r.read.active) ++ready;
        if (!r.request && firstWhy.empty()) firstWhy = "R" + std::to_string(r.id) + " : " + r.why;
    }
    if (ready == 0) {
        const std::string text = rows_.empty() ? std::string("aucune requ\xC3\xAAte : ajoute-en une (+ Requ\xC3\xAAte)")
                                               : firstWhy.empty() ? std::string("toutes les requ\xC3\xAAtes sont coup\xC3\xA9" "es") : firstWhy;
        if (why) *why = text;
        say("Lecture cyclique : " + text, true);
        return false;
    }
    poller_.start();
    if (wantRecord_ && !poller_.recording()) (void)setRecording(true);
    frozen_ = false;
    tool_.tabs_->setTabLive(HmiModbusToolPane::TCyclic, true);
    say("Lecture cyclique : " + std::to_string(ready) + (ready > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte") + " en marche"
        + (firstWhy.empty() ? std::string{} : " (" + firstWhy + ")") + ".", !firstWhy.empty());
    refreshHeads();
    tool_.rebuildProperties();
    return true;
}

void HmiCyclicPage::stop() {
    const bool was = poller_.running();
    poller_.stopRecording();
    poller_.stop();
    tool_.tabs_->setTabLive(HmiModbusToolPane::TCyclic, false);
    if (was) {
        std::uint64_t reads = 0, errors = 0;
        for (const auto& s : poller_.states()) reads += s.reads, errors += s.errors;
        say("Lecture cyclique arr\xC3\xAAt\xC3\xA9" "e : " + countText(reads) + " lecture(s), " + countText(errors) + " erreur(s).");
    }
    states_ = poller_.states();
    refreshRequests();
    refreshHeads();
    tool_.rebuildProperties();
}

bool HmiCyclicPage::running() const { return poller_.running(); }

void HmiCyclicPage::setFrozen(bool on) {
    if (frozen_ == on) return;
    frozen_ = on;
    frozenAt_ = wallNow();
    refreshHeads();
    if (!on) {
        refreshLower();
        refreshChart(wallNow());
    }
    say(on ? "Affichage fig\xC3\xA9 : la lecture continue (Figer \xC3\xA0 nouveau pour reprendre)." : "Affichage repris.");
}

bool HmiCyclicPage::resume(int id) {
    if (!poller_.resume(id)) return false;
    states_ = poller_.states();
    refreshRequests();
    tool_.rebuildProperties();
    say("R" + std::to_string(id) + " reprend la lecture.");
    return true;
}

void HmiCyclicPage::clearData() {
    poller_.clear();
    states_ = poller_.states();
    refreshRequests();
    refreshLower();
    refreshChart(wallNow());
    say("Effac\xC3\xA9 : les chiffres, les courbes et le journal repartent de z\xC3\xA9ro.");
}

void HmiCyclicPage::setLanes(bool on) {
    if (set_.lanes == on) return;
    set_.lanes = on;
    refreshHeads();
    refreshChart(wallNow());
}

void HmiCyclicPage::showJournal(bool on) {
    if (journal_ == on) return;
    journal_ = on;
    refreshLower();
    refreshHeads();
}

void HmiCyclicPage::setOnlySelected(bool on) {
    if (onlySelected_ == on) return;
    onlySelected_ = on;
    refreshLower();
    refreshHeads();
}

const mbtool::MultiPoller::RequestState* HmiCyclicPage::stateOf(int id) const {
    for (const auto& s : states_)
        if (s.id == id) return &s;
    return nullptr;
}

// ------------------------------------------------------------ les textes ---
std::string HmiCyclicPage::stateText(int id) const {
    const Row* r = row(id);
    if (!r) return {};
    if (!r->request) return "impossible : " + r->why;
    if (!r->read.active) return "coup\xC3\xA9" "e";
    const auto* s = stateOf(id);
    if (!s || (!s->reads && !s->errors)) return running() ? std::string("en attente") : std::string(kDash);
    if (s->paused) return "en pause : " + s->lastErrorShort;
    if (!s->lastOk) return "\xC3\xA9" "chec : " + s->lastErrorShort + " (" + std::to_string(s->failuresInRow) + ")";
    return std::string(r->slave ? "simul\xC3\xA9" "e" : "bonne") + kDot + msShort(s->lastMs);
}

std::vector<std::string> HmiCyclicPage::requestCells(int id) const {
    const Row* r = row(id);
    if (!r) return {};
    const auto& q = r->read;
    std::string format;
    if (q.format == "variables") format = "variables";
    else if (const auto f = mbtool::formatFromKey(q.format)) format = mbtool::formatShort(*f);
    else format = q.format;
    const std::string period = q.periodMs > 0 ? "\xE2\x80\xA2 " + periodShort(q.periodMs) : periodShort(set_.periodMs);
    const auto* s = stateOf(id);
    return {"", "R" + std::to_string(id), q.name, r->targetLabel, mbtool::functionShort(q.function),
            std::to_string(q.address) + kDot + mbtool::modiconOf(q.function, q.address), std::to_string(q.count), format, period, stateText(id),
            s ? countText(s->reads) : std::string(kDash)};
}

std::string HmiCyclicPage::badge() const {
    return std::to_string(rows_.size()) + (rows_.size() > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte");
}

std::string HmiCyclicPage::statusText() const {
    std::set<std::string> targets;
    std::string paused;
    for (const auto& r : rows_) {
        if (r.request && r.read.active) targets.insert(mbtool::targetKey(r.request->target));
        if (const auto* s = stateOf(r.id); s && s->paused && paused.empty()) paused = "R" + std::to_string(r.id) + " en pause (" + s->lastErrorShort + ")";
    }
    std::string out = "Lecture cyclique : " + badge() + ", " + std::to_string(targets.size()) + (targets.size() > 1 ? " \xC3\xA9quipements" : " \xC3\xA9quipement");
    if (!paused.empty()) out += kDot + paused;
    if (frozen_) out += kDot + std::string("affichage fig\xC3\xA9");
    if (poller_.recording()) out += kDot + std::string("enregistrement : ") + poller_.recordFile();
    return out;
}

std::string HmiCyclicPage::slaveStatus() const {
    std::vector<std::string> ids;
    for (const auto& r : rows_)
        if (r.slave && r.read.active) ids.push_back("R" + std::to_string(r.id));
    if (ids.empty()) return {};
    if (ids.size() == 1) return ids.front() + " lit un esclave simul\xC3\xA9";
    return joinList(ids, ", ") + " lisent un esclave simul\xC3\xA9";
}

// --------------------------------------------------- les blocs de la page ---
void HmiCyclicPage::refreshRequests() {
    std::vector<std::vector<std::string>> cells;
    std::vector<std::vector<ui::CellStyle>> styles;
    std::vector<std::string> tips;
    const gfx::Color violet = simViolet();
    for (const auto& r : rows_) {
        cells.push_back(requestCells(r.id));
        std::vector<ui::CellStyle> st(cells.back().size());
        st[0].customIcon = r.read.active ? GBoxOn : GBoxOff;
        st[0].iconTone = r.read.active ? ui::Tone::Accent : ui::Tone::Muted;
        st[1].bold = true;
        const auto* s = stateOf(r.id);
        for (std::size_t c = 1; c < st.size(); ++c) {
            if (!r.read.active) st[c].fgTone = ui::Tone::Muted;
            else if (r.slave && c != 9) st[c].fg = violet;
        }
        if (r.slave) st[3].bold = true;
        if (!r.request) st[9].fgTone = ui::Tone::Error;
        else if (s && s->paused) st[9].fgTone = ui::Tone::Error;
        else if (s && (s->reads || s->errors) && !s->lastOk) st[9].fgTone = ui::Tone::Warning;
        else if (s && s->lastOk) {
            if (r.slave) st[9].fg = violet;
            else st[9].fgTone = ui::Tone::Ok;
        }
        if (r.read.periodMs > 0) st[8].bold = true;
        styles.push_back(std::move(st));
        tips.push_back(r.request ? r.targetLabel + " (" + mbtool::targetKey(r.request->target) + ")" : r.why);
    }
    // La derniere ligne : "+ Ajouter une requete...", sur toute la largeur (comme la
    // maquette ; dans la colonne Nom, elle se coupait en "+ Ajouter un..."), le texte
    // sous les noms : 30 + 40 px des deux premieres colonnes (6 de marge et 16 pour
    // la fleche d'une table en arbre sont deja comptes par la table).
    std::vector<std::string> add(11);
    // "+" ordinaire : le "+" pleine chasse (U+FF0B) n'est pas dans la police de l'ecran.
    add[0] = "+ Ajouter une requ\xC3\xAAte\xE2\x80\xA6  (ou Ctrl+V : une liste venue d'Excel)";
    cells.push_back(add);
    std::vector<ui::CellStyle> addStyle(11);
    addStyle[0].fgTone = ui::Tone::Accent;
    addStyle[0].spanRow = true;
    addStyle[0].indent = 30.f + 40.f - 16.f;
    styles.push_back(std::move(addStyle));
    tips.push_back("Nouvelle requ\xC3\xAAte, depuis des variables, depuis les zones m\xC3\xA9moire, coller depuis Excel\xE2\x80\xA6");
    // Les colonnes suivent la largeur du tableau (reposees quand elle change).
    static std::map<const void*, float> laidFor;      // le tableau -> la largeur de ses colonnes
    const float width = std::floor(requests_->bounds().w);
    if (float& was = laidFor[requests_]; was != width) {
        requests_->setColumns(requestColumns(width));
        was = width;
    }
    requests_->setContent({"", "R", "Nom", "Cible", "Fonction", "Adresse", "Nb", "Format", "P\xC3\xA9riode", "\xC3\x89tat", "Lectures"},
                          std::move(cells), std::move(styles), std::move(tips));
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == selected_) requests_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    // La hauteur du tableau suit le nombre de requetes (sinon : celle de la liste vide, 4 lignes).
    if (laidRows_ != rows_.size() && bounds().h > 0) {
        laidRows_ = rows_.size();
        onLayout();
    }
}

std::string HmiCyclicPage::twinCellOf(const Row& r, int offset, bool* forced, std::optional<std::pair<double, double>>* band) const {
    if (!r.slave || r.equipment.empty()) return {};
    EquipmentHost* h = tool_.hosts_.equipments ? tool_.hosts_.equipments() : nullptr;
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    const hmi::Equipment* e = p ? p->equipmentByName(r.equipment) : nullptr;
    const auto bank = h && e ? h->twinBank(e->name) : nullptr;
    const int address = r.read.address + offset;
    if (!e || !bank || address < 0) return {};
    const int f = r.read.function;
    const hmi::MemTable t = f == 1 ? hmi::MemTable::Coils : f == 2 ? hmi::MemTable::DiscreteInputs : f == 4 ? hmi::MemTable::InputRegisters : hmi::MemTable::Holding;
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
        if (band && (b.kind == hmi::BehaviorKind::Sine || b.kind == hmi::BehaviorKind::Ramp || b.kind == hmi::BehaviorKind::Random))
            *band = std::make_pair(std::min(b.a, b.b), std::max(b.a, b.b));
        break;
    }
    if (isForced) out += (out.empty() ? "" : " \xC2\xB7 ") + std::string("F");
    return out;
}

void HmiCyclicPage::refreshLower() {
    const double now = wallNow();
    valueRows_.clear();
    if (journal_) {
        std::vector<std::vector<std::string>> cells;
        std::vector<std::vector<ui::CellStyle>> styles;
        for (const auto& j : poller_.journal(400, onlySelected_ ? selected_ : 0)) {
            const Row* r = row(j.id);
            cells.push_back({mbtool::clockText(j.t), "R" + std::to_string(j.id) + (r ? " " + r->read.name : std::string{}),
                             j.ok ? (r && r->slave ? "simul\xC3\xA9" "e" : "bonne") : "\xC3\xA9" "chec", msShort(j.ms), j.text});
            std::vector<ui::CellStyle> st(5);
            st[2].fgTone = j.ok ? ui::Tone::Ok : ui::Tone::Error;
            if (j.ok && r && r->slave) st[2].fg = simViolet(), st[2].fgTone = ui::Tone::None;
            st[0].monospace = true;
            styles.push_back(std::move(st));
        }
        if (lower_->columns().size() != 5)
            lower_->setColumns({{"Heure", 110.f}, {"Requ\xC3\xAAte", 200.f}, {"R\xC3\xA9sultat", 90.f}, {"Temps", 80.f}, {"Valeurs lues", 700.f}});
        lower_->setBoxColumn(-1);
        lower_->setContent({"Heure", "Requ\xC3\xAAte", "R\xC3\xA9sultat", "Temps", "Valeurs lues"}, std::move(cells), std::move(styles));
        return;
    }
    for (const auto& r : rows_) {
        if (onlySelected_ && r.id != selected_) continue;
        const auto* s = stateOf(r.id);
        const auto* rq = r.request ? &*r.request : nullptr;
        if (!rq || rq->values.empty()) {
            ValueRow v;
            v.request = r.id;
            v.name = r.read.name;
            v.value_ = rq ? std::string(kDash) : "impossible : " + r.why;
            v.error = !rq;
            v.slave = r.slave;
            valueRows_.push_back(std::move(v));
            continue;
        }
        for (std::size_t k = 0; k < rq->values.size(); ++k) {
            const auto& spec = rq->values[k];
            ValueRow v;
            v.request = r.id;
            v.value = static_cast<int>(k);
            v.traceable = spec.kind == mbtool::ValueKind::Number || spec.kind == mbtool::ValueKind::Bit;
            v.traced = std::find(r.read.traced.begin(), r.read.traced.end(), static_cast<int>(k)) != r.read.traced.end();
            v.color = k < r.colors.size() ? r.colors[k] : -1;
            const int place = bitFunction(rq->function) ? rq->address + spec.offset : rq->address + spec.offset;
            v.address = mbtool::modiconOf(rq->function, place);
            v.schneider = r.schneider ? mbtool::schneiderOf(rq->function, place, spec.bit) : std::string(kDash);
            v.name = spec.name.empty() ? std::string(kDash) : spec.name;
            v.slave = r.slave;
            if (r.slave) {
                bool forced = false;
                const std::string tw = twinCellOf(r, spec.offset, &forced, nullptr);
                v.forced = forced;
                v.animated = tw.find('~') != std::string::npos;
            }
            const bool have = s && k < s->values.size() && s->values[k].has;
            if (have) {
                const auto& vs = s->values[k];
                v.value_ = (v.animated ? "~ " : "") + mbtool::valueText(spec, vs.number, vs.text);
                const bool numeric = vs.count > 0;
                v.min = numeric ? statText(spec, vs.min) : std::string(kDash);
                v.max = numeric ? statText(spec, vs.max) : std::string(kDash);
                v.mean = numeric && spec.kind == mbtool::ValueKind::Number ? mbtool::frenchNumber(vs.mean(), 2) : std::string(kDash);
                v.changes = countText(vs.changes);
                v.ago = agoText(vs.changedAt, now);
                v.changedAt = vs.changedAt;
            } else {
                v.value_ = v.min = v.max = v.mean = v.changes = v.ago = kDash;
            }
            v.error = s && s->paused;
            valueRows_.push_back(std::move(v));
        }
    }
    std::vector<std::vector<std::string>> cells;
    std::vector<std::vector<ui::CellStyle>> styles;
    const gfx::Color violet = simViolet();
    for (const auto& v : valueRows_) {
        cells.push_back({"", "", "R" + std::to_string(v.request), v.address, v.schneider, v.name, v.value_, v.min, v.max, v.mean, v.changes, v.ago});
        std::vector<ui::CellStyle> st(12);
        if (v.traceable) {
            st[0].customIcon = v.traced ? GBoxOn : GBoxOff;
            st[0].iconTone = v.traced ? ui::Tone::Accent : ui::Tone::Muted;
        }
        if (v.color >= 0) st[1].customIcon = GChipBase + v.color;
        st[2].bold = true;
        st[6].bold = true;
        st[6].monospace = true;
        // Ce qui vient de changer s'allume (une seconde et demie).
        if (v.changedAt > 0 && now - v.changedAt < 1.5) st[6].bg = gfx::Color::rgb(0x2B4A2F);
        if (v.slave)
            for (std::size_t c = 2; c < 12; ++c) st[c].fg = violet;
        if (v.forced) {
            st[6].badge = "F";
            st[6].badgeTone = ui::Tone::Warning;
        }
        if (v.error) st[6].fgTone = ui::Tone::Error, st[6].fg.reset();
        styles.push_back(std::move(st));
    }
    // Les colonnes suivent la largeur du tableau : reposees quand elle change (ou en
    // revenant du journal) ; une colonne elargie a la main reste telle quelle sinon.
    static std::map<const void*, float> laidFor;      // le tableau -> la largeur de ses colonnes
    const float width = std::floor(lower_->bounds().w);
    bool schneider = false;
    for (const auto& v : valueRows_) schneider = schneider || (!v.schneider.empty() && v.schneider != kDash);
    float& was = laidFor[lower_];
    if (lower_->columns().size() != 12 || lower_->columns()[11].title != "Il y a" || lower_->columns()[4].visible != schneider
        || was != width) {
        lower_->setColumns(valueColumns(width, schneider));
        was = width;
    }
    lower_->setBoxColumn(0);
    lower_->setContent({"Tracer", "", "R", "Adresse", "Schneider", "Nom", "Valeur", "Min", "Max", "Moyenne", "Changements", "Il y a"},
                       std::move(cells), std::move(styles));
}

void HmiCyclicPage::refreshChart(double now) {
    if (frozen_) return;
    std::vector<HmiLaneChart::Series> series;
    const double window = std::max(10, set_.windowS);
    for (const auto& r : rows_) {
        if (!r.request) continue;
        const auto* s = stateOf(r.id);
        for (std::size_t k = 0; k < r.request->values.size(); ++k) {
            if (k >= r.colors.size() || r.colors[k] < 0) continue;
            const auto& spec = r.request->values[k];
            HmiLaneChart::Series ser;
            ser.name = "R" + std::to_string(r.id) + " " + (spec.name.empty() ? mbtool::modiconOf(r.request->function, r.request->address + spec.offset) : spec.name);
            ser.unit = spec.unit;
            ser.lane = spec.unit;
            ser.color = cyclicSeriesColor(r.colors[k]);
            ser.points = poller_.history(r.id, k, window, now);
            ser.simulated = r.slave;
            if (r.slave) {
                bool forced = false;
                std::optional<std::pair<double, double>> band;
                const std::string tw = twinCellOf(r, spec.offset, &forced, &band);
                ser.forced = forced;
                ser.animated = tw.find('~') != std::string::npos;
                ser.band = band;
            }
            if (s && k < s->values.size() && s->values[k].has) ser.last = mbtool::valueText(spec, s->values[k].number, s->values[k].text);
            series.push_back(std::move(ser));
        }
    }
    chart_->setData(std::move(series), now - window, now, set_.lanes,
                    rows_.empty() ? "Ajoute une requ\xC3\xAAte, puis D\xC3\xA9marrer." : "Coche Tracer sur une valeur (en bas) pour la voir ici.");
    lastChart_ = now;
}

void HmiCyclicPage::refreshHeads() {
    std::size_t active = 0, paused = 0;
    for (const auto& r : rows_) {
        if (r.read.active && r.request) ++active;
        if (const auto* s = stateOf(r.id); s && s->paused) ++paused;
    }
    std::string summary = std::to_string(rows_.size());
    if (running()) summary += kDot + std::to_string(active - std::min(active, paused)) + " en marche";
    if (paused) summary += kDot + std::to_string(paused) + " en pause";
    headRequests_->setCaption("REQU\xC3\x8ATES", summary);
    headRequests_->setParts({{HAddLink, HmiBlockHead::Kind::Link, "+ Requ\xC3\xAAte", {}, false, true, 0}});

    std::size_t traced = 0;
    for (const auto& r : rows_)
        for (const int c : r.colors) traced += c >= 0 ? 1 : 0;
    headChart_->setCaption("COURBES", std::to_string(traced) + (traced > 1 ? " trac\xC3\xA9" "es" : " trac\xC3\xA9" "e") + kDot + "fen\xC3\xAAtre "
                                          + std::to_string(set_.windowS) + " s");
    std::vector<HmiBlockHead::Part> chartParts;
    if (frozen_) chartParts.push_back({HFrozen, HmiBlockHead::Kind::Tag, "FIG\xC3\x89", {}, true, true, 0});
    chartParts.push_back({HLanes, HmiBlockHead::Kind::Segment, "En pistes", {}, set_.lanes, true, 1});
    chartParts.push_back({HOneScale, HmiBlockHead::Kind::Segment, "Une seule \xC3\xA9" "chelle", {}, !set_.lanes, true, 1});
    headChart_->setParts(std::move(chartParts));

    std::size_t values = 0;
    for (const auto& r : rows_) values += r.request ? r.request->values.size() : 0;
    headLower_->setCaption({}, {});
    headLower_->setParts({{HValues, HmiBlockHead::Kind::Segment, "Valeurs", std::to_string(values), !journal_, false, 1},
                          {HJournal, HmiBlockHead::Kind::Segment, "Journal", countText(poller_.journalSize()), journal_, false, 1},
                          {HOnlySelected, HmiBlockHead::Kind::Check, "Seulement la requ\xC3\xAAte choisie", {}, onlySelected_, true, 0}});
}

// ------------------------------------------------------- la grille de droite ---
void HmiCyclicPage::properties(std::vector<PG::Category>& cats) {
    const auto field = [this](const char* key) {
        return [this, key](std::string_view v) {
            tool_.message_.clear();
            return setField(selected_, key, std::string(v));
        };
    };
    const auto option = [this](const char* key) {
        return [this, key](std::string_view v) {
            tool_.message_.clear();
            return setOption(key, std::string(v));
        };
    };
    if (const Row* r = row(selected_)) {
        const auto& q = r->read;
        const std::string rn = "R" + std::to_string(r->id);
        PG::Category head;
        head.name = "Requ\xC3\xAAte " + rn;
        head.properties.push_back(prop("Nom", q.name, PG::ValueType::Text, field("nom")));
        head.properties.push_back(prop("Active", tf(q.active), PG::ValueType::Boolean, field("active"), "D\xC3\xA9" "coch\xC3\xA9" "e : coup\xC3\xA9" "e, sans \xC3\xAAtre retir\xC3\xA9" "e."));
        cats.push_back(std::move(head));

        PG::Category target;
        target.name = "Cible";
        const std::string choice = q.target == "automate" ? std::string(kPlcChoice) : q.target == "adresse" ? std::string(kTypedChoice) : r->targetLabel;
        target.properties.push_back(prop("\xC3\x89quipement", choice, PG::ValueType::Enum, field("cible"),
                                         "Un \xC3\xA9quipement du projet (on suit ses r\xC3\xA9glages), son esclave simul\xC3\xA9, l'automate, ou une adresse tap\xC3\xA9" "e.",
                                         targetChoices()));
        const bool typed = q.target == "adresse";
        // La cible suit son equipement : ses reglages se lisent ici, ils se changent dans sa fiche.
        const auto editable = [&](const char* key) { return typed ? std::function<bool(std::string_view)>(field(key)) : std::function<bool(std::string_view)>{}; };
        const auto t = r->request ? r->request->target : mbtool::Target{q.host, q.port, q.unit, q.timeoutMs};
        if (r->slave && r->request)
            target.properties.push_back(prop("Adresse", t.host + ":" + std::to_string(t.port) + " (simul\xC3\xA9)", PG::ValueType::ReadOnly));
        target.properties.push_back(prop("Adresse IP", t.host, PG::ValueType::Text, editable("hote")));
        target.properties.push_back(prop("Port", std::to_string(t.port), PG::ValueType::Integer, editable("port")));
        target.properties.push_back(prop("Esclave", std::to_string(t.unit), PG::ValueType::Integer, editable("esclave")));
        target.properties.push_back(prop("D\xC3\xA9lai (ms)", std::to_string(t.timeoutMs), PG::ValueType::Integer, editable("delai")));
        cats.push_back(std::move(target));

        PG::Category rd;
        rd.name = "Lecture";
        rd.properties.push_back(prop("Fonction", functionChoice(q.function), PG::ValueType::Enum, field("fonction"), {}, functionChoices()));
        rd.properties.push_back(prop("Adresse", std::to_string(q.address), PG::ValueType::Text, field("adresse"),
                                     "\xC3\x80 partir de 0 (40001 est l'adresse 0) ; ou tape 40101, 30001, %MW100 : l'adresse et la fonction suivent."));
        rd.properties.push_back(prop("En Modicon", mbtool::modiconOf(q.function, q.address), PG::ValueType::ReadOnly));
        rd.properties.push_back(prop("En Schneider", r->schneider ? mbtool::schneiderOf(q.function, q.address) : std::string(kDash), PG::ValueType::ReadOnly));
        rd.properties.push_back(prop("Nombre", std::to_string(q.count), PG::ValueType::Integer, field("nombre"), "125 mots, 2000 bits au plus."));
        std::vector<std::string> formats = mbtool::formatLabels();
        std::string format;
        if (q.format == "variables") format = kByVariables;
        else if (const auto f = mbtool::formatFromKey(q.format)) format = mbtool::formatLabels()[static_cast<std::size_t>(*f)];
        if (!q.values.empty()) formats.push_back(kByVariables);
        rd.properties.push_back(prop("Format", format, PG::ValueType::Enum, field("format"), "Comment lire ; d'apr\xC3\xA8s les variables : chacune son type.", formats));
        rd.properties.push_back(prop("Ordre des mots", q.lowFirst ? kLowFirst : kHighFirst, PG::ValueType::Enum, field("ordre"), {}, {kLowFirst, kHighFirst}));
        rd.properties.push_back(prop("P\xC3\xA9riode", q.periodMs > 0 ? std::to_string(q.periodMs) : std::string(kCommon) + " (" + std::to_string(set_.periodMs) + " ms)",
                                     PG::ValueType::Text, field("periode"), "En ms ; 0 ou \xC2\xAB commune \xC2\xBB : la p\xC3\xA9riode commune (en bas)."));
        const std::size_t n = r->request ? r->request->values.size() : 0;
        const std::string traced = q.traced.empty() ? std::string("aucune") : q.traced.size() >= n && n ? std::string("toutes")
                                   : std::to_string(q.traced.size()) + " valeur(s)";
        rd.properties.push_back(prop("Tracer", traced, PG::ValueType::Enum, field("tracer"), "Les valeurs de la requ\xC3\xAAte sur le graphique (ou la case Tracer, en bas).",
                                     {"aucune", "la premi\xC3\xA8re", "toutes"}));
        cats.push_back(std::move(rd));

        PG::Category st;
        st.name = "Cette requ\xC3\xAAte";
        const auto* s = stateOf(r->id);
        st.properties.push_back(prop("\xC3\x89tat", stateText(r->id), PG::ValueType::ReadOnly));
        st.properties.push_back(prop("Lectures", s ? countText(s->reads) + ", " + countText(s->errors) + " erreur(s)" : std::string(kDash), PG::ValueType::ReadOnly));
        st.properties.push_back(prop("Temps : dernier", s && s->reads ? msShort(s->lastMs) : std::string(kDash), PG::ValueType::ReadOnly));
        st.properties.push_back(prop("Temps : moyenne", s && s->reads ? msShort(s->avgMs) : std::string(kDash), PG::ValueType::ReadOnly));
        st.properties.push_back(prop("Temps : maximum", s && s->reads ? msShort(s->maxMs) : std::string(kDash), PG::ValueType::ReadOnly));
        if (s && !s->lastError.empty()) st.properties.push_back(prop("Derni\xC3\xA8re erreur", s->lastError, PG::ValueType::ReadOnly));
        cats.push_back(std::move(st));
    }
    PG::Category all;
    all.name = "Lecture cyclique (toutes les requ\xC3\xAAtes)";
    all.properties.push_back(prop("P\xC3\xA9riode commune (ms)", std::to_string(set_.periodMs), PG::ValueType::Integer, option("periode_commune")));
    all.properties.push_back(prop("Encha\xC3\xAEnement", set_.perTarget ? kPerTarget : kSerial, PG::ValueType::Enum, option("enchainement"),
                                  "Une connexion par \xC3\xA9quipement : deux \xC3\xA9quipements sont lus en m\xC3\xAAme temps.", {kPerTarget, kSerial}));
    all.properties.push_back(prop("Pause entre deux requ\xC3\xAAtes (ms)", std::to_string(set_.pauseMs), PG::ValueType::Integer, option("pause")));
    all.properties.push_back(prop("Apr\xC3\xA8s N \xC3\xA9" "checs de suite", std::to_string(set_.maxFailures), PG::ValueType::Integer, option("echecs"),
                                  "La requ\xC3\xAAte se met en pause (les autres continuent) ; 0 : jamais."));
    all.properties.push_back(prop("Fen\xC3\xAAtre du graphique (s)", std::to_string(set_.windowS), PG::ValueType::Integer, option("fenetre")));
    all.properties.push_back(prop("Enregistrer sur le disque", tf(poller_.recording() || wantRecord_), PG::ValueType::Boolean, option("enregistrer"),
                                  "Tant que la lecture tourne : une ligne par tour dans exports/modbus/<date>_<jeu>.csv (un fichier par jour)."));
    cats.push_back(std::move(all));
}

void HmiCyclicPage::refreshSide(HmiCyclicSide& side) {
    side.setHelp({}, {}, false, false, false);
    side.setNote({}, {});
    const Row* r = row(selected_);
    if (!r) return;
    const auto* s = stateOf(r->id);
    const std::string rn = "R" + std::to_string(r->id);
    if (!r->request) {
        side.setHelp(rn + " ne peut pas \xC3\xAAtre lue.", r->why + ".", false, !r->equipment.empty(), false);
    } else if (s && (s->paused || (!s->lastOk && s->errors))) {
        std::string advice;
        if (s->exception == 2) advice = " V\xC3\xA9rifie l'adresse et le nombre dans la carte m\xC3\xA9moire de l'\xC3\xA9quipement.";
        else if (s->timeout) advice = " V\xC3\xA9rifie l'adresse IP, le c\xC3\xA2" "ble et que l'appareil est sous tension.";
        side.setHelp(s->paused ? rn + " en pause apr\xC3\xA8s " + std::to_string(s->failuresInRow) + " \xC3\xA9" "checs de suite." : rn + " \xC3\xA9" "choue.",
                     s->lastError + "." + advice + (s->paused ? " Les autres requ\xC3\xAAtes continuent." : std::string{}), s->paused, !r->equipment.empty(),
                     s->exception == 2 && !r->equipment.empty());
    }
    if (r->slave && r->request) {
        side.setNote("Esclave simul\xC3\xA9.", rn + " lit \xC2\xAB " + r->targetLabel + " \xC2\xBB (" + r->request->target.host + ":" + std::to_string(r->request->target.port)
                                                   + ") : ses valeurs viennent de l'application, pas du vrai appareil. ~ : une valeur anim\xC3\xA9" "e ; F : une case forc\xC3\xA9" "e.");
    }
}

// -------------------------------------------------------- chaque image ---
void HmiCyclicPage::tick(double time, bool force) {
    graveyard_.clear();
    if (closing_ || (overlay_ && overlay_->closed())) {
        closing_ = false;
        if (overlay_ && overlay_->closed()) dropOverlay();
    }
    if (!restored_ && tool_.hosts_.project && tool_.hosts_.project()) {
        restored_ = true;
        (void)restoreLastSet();
    }
    if (!force && time - lastTick_ < 0.25) return;
    lastTick_ = time;
    states_ = poller_.states();
    refreshRequests();
    // Fige : les courbes et le tableau des valeurs (ou le journal) restent tels quels,
    // comme la maquette ; les requetes et la barre d'etat suivent la lecture.
    if (!frozen_) refreshLower();
    refreshHeads();
    const double now = wallNow();
    if (force || now - lastChart_ >= 0.25) refreshChart(now);
    tool_.tabs_->setTabBadge(HmiModbusToolPane::TCyclic, badge(), running() ? ui::Tone::Ok : ui::Tone::None);
    tool_.tabs_->setTabLive(HmiModbusToolPane::TCyclic, running());
}

bool HmiCyclicPage::key(const ui::KeyDown& k) {
    if (overlay_) return false;
    if (k.key == ui::Key::Insert && k.mods.none()) return addNew() != 0;
    if (k.key == ui::Key::Delete && k.mods.none() && selected_) return remove(selected_);
    if (k.mods.ctrl && !k.mods.shift && k.key == ui::Key::S) return saveSet();
    if (k.mods.ctrl && !k.mods.shift && k.key == ui::Key::Z) return undoList();
    if (k.mods.ctrl && (k.key == ui::Key::Y || (k.mods.shift && k.key == ui::Key::Z))) return redoList();
    return false;
}

void HmiCyclicPage::say(std::string text, bool error) { tool_.say(std::move(text), error); }

void HmiCyclicPage::onLayout() {
    const auto b = bounds();
    const float head = 30.f;
    const float rowH = 26.f;
    laidRows_ = rows_.size();
    // De 4 a 8 lignes (la ligne "+ Ajouter..." comprise), au plus 40 % de la hauteur.
    const float lo = rowH * 4 + 28.f;
    const float tableH = std::clamp(rowH * static_cast<float>(std::min<std::size_t>(rows_.size() + 1, 8) + 1) + 28.f, lo, std::max(lo, std::floor(b.h * 0.40f)));
    float y = b.y;
    headRequests_->setBounds({b.x, y, b.w, head});
    y += head;
    requests_->setBounds({b.x, y, b.w, tableH});
    y += tableH;
    const float lowerH = std::floor(std::max(120.f, (b.y + b.h - y) * 0.42f));
    const float chartH = std::max(60.f, b.y + b.h - y - 2 * head - lowerH);
    headChart_->setBounds({b.x, y, b.w, head});
    y += head;
    chart_->setBounds({b.x + 8, y + 4, b.w - 16, chartH - 8});
    y += chartH;
    headLower_->setBounds({b.x, y, b.w, head});
    y += head;
    lower_->setBounds({b.x, y, b.w, std::max(0.f, b.y + b.h - y)});
    if (overlay_) overlay_->layoutIn(b);
}

void HmiCyclicPage::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
