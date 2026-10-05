// app/hmi/HmiCyclicPageSets.cpp - Lecture cyclique (1.9) : les jeux de lecture
//  (dans le projet IHM, une commande annulable), l'export CSV, l'enregistrement
//  continu, les autres facons d'ajouter (variables, zones memoire, Excel) et les
//  menus / dialogues poses au-dessus de l'outil.
#include "HmiCyclicPage.hpp"

#include "HmiEquipmentHost.hpp"
#include "HmiModbusToolPane.hpp"
#include "HmiPaneKit.hpp"
#include "../ExportTarget.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiHistory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

using namespace hmikit;
namespace comm = hmi::comm;
namespace fs = std::filesystem;

namespace {

const char* const kPlcChoice = "automate du projet";
const char* const kNoName = "Sans nom";

std::string stemOf(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) && u < 0x80) || c == '-' || c == '_' ? c : '_';
    }
    while (out.find("__") != std::string::npos) out.replace(out.find("__"), 2, "_");
    return out.empty() ? std::string("jeu") : out;
}

std::vector<std::string> cellsOf(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') cur += '"', ++i;
            else quoted = !quoted;
        } else if (c == sep && !quoted) {
            out.push_back(trimmed(cur));
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(trimmed(cur));
    return out;
}

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// "Equipement", "Equipement" accentue, "Cible"... -> la cle d'une colonne ; vide : inconnue.
std::string columnKey(std::string title) {
    title = lower(trimmed(title));
    const auto has = [&](const char* w) { return title.find(w) != std::string::npos; };
    if (has("nom") || has("name")) return "nom";
    if (has("quipement") || has("cible") || has("appareil")) return "cible";
    if (has("fonction")) return "fonction";
    if (has("adresse") || has("registre")) return "adresse";
    if (has("nombre") || has("taille") || has("count")) return "nombre";
    if (has("format") || has("type")) return "format";
    if (has("riode")) return "periode";
    return {};
}

int functionOf(comm::Area a) {
    switch (a) {
        case comm::Area::Coils: return 1;
        case comm::Area::DiscreteInputs: return 2;
        case comm::Area::InputRegisters: return 4;
        default: return 3;
    }
}

std::string typeOfEncoding(comm::Encoding e) {
    switch (e) {
        case comm::Encoding::Bit: case comm::Encoding::BitOfWord: case comm::Encoding::BoolWord: return "BOOL";
        case comm::Encoding::UInt16: return "UINT";
        case comm::Encoding::Int32: return "DINT";
        case comm::Encoding::UInt32: return "UDINT";
        case comm::Encoding::Real32: return "REAL";
        case comm::Encoding::Text: return "STRING";
        default: return "INT";
    }
}

// Une place du plan -> une variable a lire (faux : un tableau, a lire par une plage).
bool itemOf(const comm::Point& pt, mbtool::ReadItem& it) {
    if (pt.array) return false;
    it.name = pt.name;
    it.type = typeOfEncoding(pt.encoding);
    it.function = functionOf(pt.area);
    it.offset = pt.offset;
    it.words = pt.bits() ? 1 : std::max<int>(1, pt.size);
    it.bit = pt.encoding == comm::Encoding::BitOfWord ? pt.bit : -1;
    return true;
}

HmiCyclicMenu::Item menuItem(int id, std::string label, std::string detail, std::string shortcut, int glyph) {
    HmiCyclicMenu::Item it;
    it.id = id;
    it.label = std::move(label);
    it.detail = std::move(detail);
    it.shortcut = std::move(shortcut);
    it.glyph = glyph;
    return it;
}

std::string placeOf(const mbtool::ReadItem& it) {
    return mbtool::schneiderOf(it.function, it.offset, it.bit) + " \xC2\xB7 " + mbtool::modiconOf(it.function, it.offset);
}

} // namespace

// ------------------------------------------------------------------- les jeux ---
hmi::ModbusReadSet HmiCyclicPage::currentSet() const {
    hmi::ModbusReadSet s = set_;
    s.reads.clear();
    for (const auto& r : rows_) s.reads.push_back(r.read);
    return s;
}

void HmiCyclicPage::loadSet(const hmi::ModbusReadSet& set) {
    const bool was = running();
    if (was) poller_.stop();
    poller_.clear();
    rows_.clear();
    set_ = set;
    set_.reads.clear();
    int id = 0;
    for (const auto& q : set.reads) {
        Row r;
        r.id = ++id;
        r.read = q;
        rows_.push_back(std::move(r));
    }
    selected_ = rows_.empty() ? 0 : rows_.front().id;
    undo_.clear();
    redo_.clear();
    changed(true);
    if (was) (void)start();
}

bool HmiCyclicPage::setModified() const {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    if (p)
        for (const auto& s : p->modbusSets)
            if (s.name == set_.name) return !(s == currentSet());
    return !rows_.empty();
}

bool HmiCyclicPage::saveSet(const std::string& name, std::string* why) {
    const std::string n = trimmed(name.empty() ? set_.name : name);
    if (n.empty() || (name.empty() && n == kNoName)) {
        openSaveAs();
        return true;
    }
    if (n.find('|') != std::string::npos || n.find(';') != std::string::npos) {
        if (why) *why = "un nom sans | ni ;";
        say("Enregistrer le jeu : un nom sans | ni ;", true);
        return false;
    }
    hmi::DocumentPtr doc = tool_.hosts_.document ? tool_.hosts_.document() : nullptr;
    if (!doc || !tool_.hosts_.apply) {
        if (why) *why = "pas de projet IHM ouvert";
        say("Enregistrer le jeu : pas de projet IHM ouvert.", true);
        return false;
    }
    hmi::ModbusReadSet s = currentSet();
    s.name = n;
    auto cmd = hmi::changeProject(doc, "Enregistrer le jeu de lecture \xC2\xAB " + n + " \xC2\xBB", [s](hmi::Project& p) {
        bool found = false;
        for (auto& x : p.modbusSets)
            if (x.name == s.name) {
                x = s;
                found = true;
            }
        if (!found) p.modbusSets.push_back(s);
        p.modbusSetLast = s.name;
    });
    set_.name = n;
    if (cmd) tool_.hosts_.apply(std::move(cmd));
    say("Jeu de lecture \xC2\xAB " + n + " \xC2\xBB enregistr\xC3\xA9 dans le projet (Ctrl+Z le d\xC3\xA9" "fait).");
    refreshHeads();
    tool_.rebuildProperties();
    return true;
}

bool HmiCyclicPage::openSet(const std::string& name, std::string* why) {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    const hmi::ModbusReadSet* found = nullptr;
    if (p)
        for (const auto& s : p->modbusSets)
            if (same(s.name, name)) found = &s;
    if (!found) {
        if (why) *why = "pas de jeu \xC2\xAB " + name + " \xC2\xBB dans le projet";
        say("Pas de jeu de lecture \xC2\xAB " + name + " \xC2\xBB dans le projet.", true);
        return false;
    }
    const hmi::ModbusReadSet copy = *found;
    loadSet(copy);
    // Le dernier jeu ouvert revient a l'ouverture de l'outil : il se note dans le projet.
    if (p->modbusSetLast != copy.name && tool_.hosts_.document && tool_.hosts_.apply) {
        if (auto doc = tool_.hosts_.document()) {
            const std::string last = copy.name;
            if (auto cmd = hmi::changeProject(doc, "Jeu de lecture ouvert : " + last, [last](hmi::Project& pr) { pr.modbusSetLast = last; }, "modbus-set-last"))
                tool_.hosts_.apply(std::move(cmd));
        }
    }
    say("Jeu de lecture \xC2\xAB " + copy.name + " \xC2\xBB : " + badge() + ".");
    return true;
}

void HmiCyclicPage::newSet() {
    hmi::ModbusReadSet s;
    s.name = kNoName;
    loadSet(s);
    say("Nouveau jeu vide : ajoute des requ\xC3\xAAtes (+ Requ\xC3\xAAte), puis Enregistrer le jeu.");
}

bool HmiCyclicPage::restoreLastSet() {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    if (!p || p->modbusSetLast.empty() || !rows_.empty()) return false;
    for (const auto& s : p->modbusSets)
        if (s.name == p->modbusSetLast) {
            const hmi::ModbusReadSet copy = s;
            loadSet(copy);
            return true;
        }
    return false;
}

std::string HmiCyclicPage::setCsv() const {
    const auto s = currentSet();
    std::string out = "\xEF\xBB\xBF";
    out += "Jeu de lecture;" + mbtool::csvCell(s.name) + "\n";
    out += "P\xC3\xA9riode commune (ms);" + std::to_string(s.periodMs) + "\n";
    out += "Apr\xC3\xA8s N \xC3\xA9" "checs;" + std::to_string(s.maxFailures) + "\n";
    out += "Pause (ms);" + std::to_string(s.pauseMs) + "\n";
    out += std::string("Encha\xC3\xAEnement;") + (s.perTarget ? "equipement" : "serie") + "\n";
    out += "Fen\xC3\xAAtre (s);" + std::to_string(s.windowS) + "\n\n";
    out += "Nom;Cible;\xC3\x89quipement;H\xC3\xB4te;Port;Esclave;D\xC3\xA9lai;Fonction;Adresse;Nombre;Format;Ordre;P\xC3\xA9riode;Active;Valeurs;Trac\xC3\xA9" "es\n";
    for (const auto& q : s.reads) {
        std::string values, traced;
        for (const auto& v : q.values)
            values += (values.empty() ? "" : ",") + v.name + "|" + v.type + "|" + std::to_string(v.offset) + "|" + std::to_string(v.words) + "|" + std::to_string(v.bit);
        for (const int k : q.traced) traced += (traced.empty() ? "" : ",") + std::to_string(k);
        out += mbtool::csvCell(q.name) + ";" + q.target + ";" + mbtool::csvCell(q.equipment) + ";" + q.host + ";" + std::to_string(q.port) + ";"
             + std::to_string(q.unit) + ";" + std::to_string(q.timeoutMs) + ";" + std::to_string(q.function) + ";" + std::to_string(q.address) + ";"
             + std::to_string(q.count) + ";" + q.format + ";" + (q.lowFirst ? "faible" : "fort") + ";" + std::to_string(q.periodMs) + ";"
             + (q.active ? "1" : "0") + ";" + mbtool::csvCell(values) + ";" + traced + "\n";
    }
    return out;
}

bool HmiCyclicPage::importSet(const std::string& csv, std::string* why) {
    std::string text = csv;
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    hmi::ModbusReadSet s;
    s.name = kNoName;
    bool table = false;
    for (const auto& line : linesOf(text)) {
        if (trimmed(line).empty()) continue;
        const auto c = cellsOf(line, ';');
        if (!table) {
            if (c.size() >= 2 && c[0] == "Nom" && c[1] == "Cible") {
                table = true;
                continue;
            }
            const std::string k = lower(c[0]);
            const long long n = c.size() > 1 ? std::atoll(c[1].c_str()) : 0;
            if (k.rfind("jeu", 0) == 0 && c.size() > 1) s.name = c[1];
            else if (k.find("commune") != std::string::npos) s.periodMs = static_cast<int>(std::clamp<long long>(n, 20, 3600000));
            else if (k.find("checs") != std::string::npos) s.maxFailures = static_cast<int>(std::clamp<long long>(n, 0, 100000));
            else if (k.rfind("pause", 0) == 0) s.pauseMs = static_cast<int>(std::clamp<long long>(n, 0, 60000));
            else if (k.find("nement") != std::string::npos && c.size() > 1) s.perTarget = c[1] != "serie";
            else if (k.find("tre (s)") != std::string::npos) s.windowS = static_cast<int>(std::clamp<long long>(n, 10, 900));
            continue;
        }
        if (c.size() < 13) continue;
        hmi::ModbusRead q;
        q.name = c[0];
        q.target = c[1];
        q.equipment = c[2];
        q.host = c[3];
        q.port = static_cast<int>(std::clamp<long long>(std::atoll(c[4].c_str()), 1, 65535));
        q.unit = static_cast<int>(std::clamp<long long>(std::atoll(c[5].c_str()), 0, 255));
        q.timeoutMs = static_cast<int>(std::clamp<long long>(std::atoll(c[6].c_str()), 50, 60000));
        q.function = static_cast<int>(std::clamp<long long>(std::atoll(c[7].c_str()), 1, 4));
        q.address = static_cast<int>(std::clamp<long long>(std::atoll(c[8].c_str()), 0, 65535));
        q.count = static_cast<int>(std::clamp<long long>(std::atoll(c[9].c_str()), 1, 2000));
        q.format = c[10].empty() ? std::string("decimal") : c[10];
        q.lowFirst = c[11] != "fort";
        q.periodMs = static_cast<int>(std::clamp<long long>(std::atoll(c[12].c_str()), 0, 3600000));
        q.active = c.size() < 14 || c[13] != "0";
        if (c.size() > 14)
            for (const auto& item : cellsOf(c[14], ',')) {
                const auto part = cellsOf(item, '|');
                if (part.size() < 5 || part[0].empty()) continue;
                hmi::ModbusReadValue v;
                v.name = part[0];
                v.type = part[1];
                v.offset = std::atoi(part[2].c_str());
                v.words = std::max(1, std::atoi(part[3].c_str()));
                v.bit = std::atoi(part[4].c_str());
                q.values.push_back(std::move(v));
            }
        if (c.size() > 15)
            for (const auto& k : cellsOf(c[15], ','))
                if (!k.empty()) q.traced.push_back(std::atoi(k.c_str()));
        s.reads.push_back(std::move(q));
    }
    if (!table) {
        if (why) *why = "ce n'est pas un jeu de lecture (la ligne Nom;Cible;... manque)";
        say("Importer un jeu : ce n'est pas un jeu de lecture.", true);
        return false;
    }
    loadSet(s);
    say("Jeu \xC2\xAB " + s.name + " \xC2\xBB import\xC3\xA9 : " + badge() + " (Enregistrer le jeu pour le garder dans le projet).");
    return true;
}

bool HmiCyclicPage::exportSet(std::string* where) {
    const std::string text = setCsv();
    hmi::ExportRequest rq;
    rq.fileName = "jeu_modbus_" + stemOf(set_.name) + ".csv";
    rq.format = "CSV";
    rq.source = "outil Modbus (jeu de lecture)";
    rq.rows = rows_.size();
    rq.data = std::make_shared<const hmi::Bytes>(text.begin(), text.end());
    rq.origin = "IHM > Outil Modbus";
    std::string path;
    const bool ok = tool_.hosts_.exportFile && tool_.hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Jeu export\xC3\xA9 : " + path : std::string("Export du jeu impossible"), !ok);
    return ok;
}

// ------------------------------------------------- exporter, enregistrer ---
std::string HmiCyclicPage::exportPreview(bool oneFile, bool raw) const {
    if (oneFile) return poller_.csvAll(raw, set_.periodMs, 6);
    std::string out;
    for (const auto& [id, text] : poller_.csvEach(raw, 3)) {
        out += "R" + std::to_string(id) + ".csv\n" + text + "\n";
        if (out.size() > 4000) break;
    }
    return out;
}

bool HmiCyclicPage::exportCsv(bool oneFile, bool raw, std::string* where) {
    const std::string stem = "lecture_cyclique_" + stemOf(set_.name) + "_" + hmi::wallStamp().substr(0, 10);
    const auto send = [&](const std::string& fileName, const std::string& text, std::size_t rowsCount, std::string* path) {
        hmi::ExportRequest rq;
        rq.fileName = fileName;
        rq.format = "CSV";
        rq.source = "outil Modbus (lecture cyclique)";
        rq.rows = rowsCount;
        rq.data = std::make_shared<const hmi::Bytes>(text.begin(), text.end());
        rq.origin = "IHM > Outil Modbus";
        return tool_.hosts_.exportFile && tool_.hosts_.exportFile(rq, path);
    };
    std::string path;
    bool ok = false;
    if (oneFile) {
        std::size_t lines = 0;
        const std::string text = poller_.csvAll(raw, set_.periodMs, 0, &lines);
        if (lines == 0) {
            say("Rien \xC3\xA0 exporter : d\xC3\xA9marre la lecture d'abord.", true);
            return false;
        }
        ok = send(stem + ".csv", text, lines, &path);
    } else {
        const auto files = poller_.csvEach(raw);
        if (files.empty()) {
            say("Rien \xC3\xA0 exporter : d\xC3\xA9marre la lecture d'abord.", true);
            return false;
        }
        ok = true;
        for (const auto& [id, text] : files) {
            std::string one;
            ok = send(stem + "_R" + std::to_string(id) + ".csv", text, 1, &one) && ok;
            if (path.empty()) path = one;
        }
    }
    if (where) *where = path;
    say(ok ? "Export\xC3\xA9 : " + path : std::string("Export impossible") + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

std::string HmiCyclicPage::recordFolder() const { return tool_.hosts_.recordFolder ? tool_.hosts_.recordFolder() : std::string{}; }

bool HmiCyclicPage::recording() const { return poller_.recording(); }

bool HmiCyclicPage::setRecording(bool on, std::string* why) {
    wantRecord_ = on;
    if (!on) {
        const bool was = poller_.recording();
        poller_.stopRecording();
        if (was) say("Enregistrement arr\xC3\xAAt\xC3\xA9 : " + mbtool::frenchNumber(static_cast<double>(poller_.recordedLines()), 0) + " ligne(s) dans " + poller_.recordFile() + ".");
        refreshHeads();
        tool_.rebuildProperties();
        return true;
    }
    if (!running()) {
        say("L'enregistrement commencera avec la lecture (D\xC3\xA9marrer).");
        tool_.rebuildProperties();
        return true;
    }
    const std::string folder = recordFolder();
    std::string reason;
    if (folder.empty()) reason = "pas de dossier pour l'enregistrement";
    else if (!poller_.recording() && !poller_.startRecording(folder, set_.name, &reason)) {
        if (reason.empty()) reason = "le fichier ne s'ouvre pas";
    } else {
        reason.clear();
    }
    if (!reason.empty()) {
        wantRecord_ = false;
        if (why) *why = reason;
        say("Enregistrer : " + reason, true);
        tool_.rebuildProperties();
        return false;
    }
    say("Enregistrement : " + poller_.recordFile() + " (une ligne par tour, tant que la lecture tourne).");
    refreshHeads();
    tool_.rebuildProperties();
    return true;
}

// ------------------------------------------------------- d'autres facons ---
std::vector<CyclicVarTab> HmiCyclicPage::variableTabs() const {
    std::vector<CyclicVarTab> tabs(2);
    tabs[0].label = "Automate";
    tabs[1].label = "Variables IHM li\xC3\xA9" "es";
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    // Ce qui est deja lu : par nom de variable, et par place (cible, fonction, adresse).
    const auto already = [this](const std::string& target, const mbtool::ReadItem& it) -> std::string {
        for (const auto& r : rows_) {
            for (const auto& v : r.read.values)
                if (v.name == it.name) return "R" + std::to_string(r.id);
            const bool sameTarget = (target == "automate" && r.read.target == "automate") || (target != "automate" && r.read.equipment == target);
            if (sameTarget && r.read.function == it.function && it.offset >= r.read.address && it.offset + it.words <= r.read.address + r.read.count)
                return "R" + std::to_string(r.id);
        }
        return {};
    };
    // ---- l'automate : les variables localisees du programme, et les adresses de la liaison ----
    {
        CyclicVarGroup words, bits, refused;
        words.title = "Mots %MW, %MD, %MF (registres de maintien)";
        words.target = "automate";
        bits.title = "Bits %M (bobines)";
        bits.target = "automate";
        bits.bits = true;
        refused.title = "Non lisibles par Modbus";
        refused.disabled = "sans adresse, ou %I / %Q : l'automate ne les donne pas par Modbus";
        std::set<std::string> seen;
        const auto addOne = [&](const std::string& name, const std::string& type, const std::string& address) {
            if (name.empty() || !seen.insert(name).second) return;
            CyclicVarItem item;
            item.name = name;
            item.type = type;
            comm::Point pt;
            std::string whyNot;
            const sim::Type ty = type.empty() ? comm::typeOfAddress(address) : hmi::equip::typeOfName(type);
            if (address.empty() || !comm::placeAddress(address, ty, 0, pt, &whyNot) || !itemOf(pt, item.read)) {
                item.place = address.empty() ? std::string("sans adresse") : address + " : " + (whyNot.empty() ? std::string("un tableau") : whyNot);
                refused.items.push_back(std::move(item));
                return;
            }
            item.read.name = name;
            if (!type.empty()) item.read.type = type;
            item.place = placeOf(item.read);
            item.already = already("automate", item.read);
            (pt.bits() ? bits : words).items.push_back(std::move(item));
        };
        if (tool_.hosts_.plcVariables)
            for (const auto& v : tool_.hosts_.plcVariables()) addOne(v.name, v.type, v.address);
        if (p)
            for (const auto& a : p->comm.addresses) addOne(a.variable, a.type, a.address);
        for (auto* g : {&words, &bits})
            std::sort(g->items.begin(), g->items.end(), [](const CyclicVarItem& a, const CyclicVarItem& b) {
                return a.read.offset != b.read.offset ? a.read.offset < b.read.offset : a.read.bit < b.read.bit;
            });
        words.open = true;
        for (auto* g : {&words, &bits, &refused}) {
            if (g->items.empty()) continue;
            g->type = std::to_string(g->items.size()) + " variable(s)";
            tabs[0].groups.push_back(std::move(*g));
        }
        if (p && p->comm.modbus()) tabs[0].targets = {kPlcChoice};
        if (tabs[0].groups.empty()) tabs[0].empty = "Aucune variable localis\xC3\xA9" "e (%MW, %M, %MF, %MD) dans le projet de l'automate.";
        else if (!p || !p->comm.modbus())
            for (auto& g : tabs[0].groups)
                if (g.disabled.empty()) g.disabled = "la liaison de l'IHM vers l'automate n'est pas Modbus (Configuration \xE2\x80\xBA Communication)";
    }
    // ---- les variables IHM liees, par equipement ----
    if (p)
        for (const auto& e : p->equipments) {
            if (!e.modbus()) continue;
            const auto plan = hmi::equip::buildPlan(*p, e);
            CyclicVarGroup g;
            g.title = e.name;
            g.target = e.name;
            CyclicVarGroup refused;
            refused.title = e.name + " : non lisibles";
            refused.disabled = "sans adresse, ou un tableau (lis-le par une plage)";
            for (const auto& pt : plan.points()) {
                CyclicVarItem item;
                item.name = pt.name;
                if (!itemOf(pt, item.read)) {
                    item.type = pt.typeName;
                    item.place = pt.address + " : un tableau";
                    refused.items.push_back(std::move(item));
                    continue;
                }
                item.type = pt.typeName.empty() ? item.read.type : pt.typeName;
                // Une variable mise a l'echelle : sa valeur se montre mise a l'echelle.
                for (const auto& v : p->programs.variables)
                    if (v.equipment == e.name && same(v.name, pt.name) && v.scaled()) {
                        item.read.scaled = true;
                        item.read.rawMin = v.rawMin;
                        item.read.rawMax = v.rawMax;
                        item.read.scaleMin = v.engMin;
                        item.read.scaleMax = v.engMax;
                    }
                item.place = placeOf(item.read);
                item.already = already(e.name, item.read);
                g.items.push_back(std::move(item));
            }
            for (const auto& [name, reason] : plan.refused()) {
                CyclicVarItem item;
                item.name = name;
                item.place = reason;
                refused.items.push_back(std::move(item));
            }
            if (!g.items.empty()) {
                g.type = std::to_string(g.items.size()) + " variable(s)";
                g.range = e.host + ":" + std::to_string(e.port);
                tabs[1].groups.push_back(std::move(g));
            }
            if (!refused.items.empty()) tabs[1].groups.push_back(std::move(refused));
        }
    if (!tabs[1].groups.empty()) tabs[1].groups.front().open = true;
    if (tabs[1].groups.empty()) tabs[1].empty = "Aucune variable IHM li\xC3\xA9" "e \xC3\xA0 un \xC3\xA9quipement Modbus.";
    return tabs;
}

std::vector<int> HmiCyclicPage::addPlanned(const std::vector<HmiCyclicVarsDialog::Planned>& planned) {
    std::vector<int> ids;
    if (planned.empty()) return ids;
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    remember();
    for (const auto& pl : planned) {
        hmi::ModbusRead q;
        const std::string& t = pl.target;
        if (t == "automate" || t == kPlcChoice) {
            q.target = "automate";
            if (p) q.lowFirst = p->comm.wordOrder != "fort";
        } else {
            q.target = "equipement";
            q.equipment = t;
            if (p)
                for (const auto& e : p->equipments) {
                    if (same(e.name, t)) q.equipment = e.name, q.lowFirst = e.wordOrder != "fort";
                    else if (e.linkedSlave() && same(e.twinLabel(), t)) q.target = "esclave", q.equipment = e.name, q.lowFirst = e.wordOrder != "fort";
                }
        }
        q.function = pl.read.function;
        q.address = pl.read.address;
        q.count = pl.read.count;
        q.format = "variables";
        for (const auto& spec : pl.read.values(pl.items)) {
            hmi::ModbusReadValue v;
            v.name = spec.name;
            v.type = spec.type.empty() ? std::string("INT") : spec.type;
            v.offset = spec.offset;
            v.words = spec.words;
            v.bit = spec.bit;
            q.values.push_back(std::move(v));
        }
        if (!pl.read.items.empty()) {
            const auto& first = pl.items[pl.read.items.front()].name;
            const auto& last = pl.items[pl.read.items.back()].name;
            q.name = pl.read.items.size() == 1 ? first : first + " \xE2\x80\xA6 " + last;
        }
        q.traced = {0};
        Row r;
        r.id = nextId();
        r.read = std::move(q);
        ids.push_back(r.id);
        rows_.push_back(std::move(r));
    }
    selected_ = ids.front();
    changed(true);
    (void)select(selected_);
    say(std::to_string(ids.size()) + (ids.size() > 1 ? " requ\xC3\xAAtes ajout\xC3\xA9" "es" : " requ\xC3\xAAte ajout\xC3\xA9" "e") + " depuis les variables (Ctrl+Z les retire).");
    return ids;
}

std::vector<int> HmiCyclicPage::addFromVariables(const std::vector<std::string>& names, bool merge, int gap, std::string* why) {
    // Les variables nommees, cherchees dans les deux onglets ; regroupees par cible.
    std::map<std::string, std::vector<mbtool::ReadItem>> byTarget;
    std::vector<std::string> missing;
    const auto tabs = variableTabs();
    for (const auto& n : names) {
        bool found = false;
        for (const auto& tab : tabs)
            for (const auto& g : tab.groups) {
                if (!g.disabled.empty() || found) continue;
                for (const auto& it : g.items)
                    if (same(it.name, n) && !found) {
                        byTarget[g.target].push_back(it.read);
                        found = true;
                    }
            }
        if (!found) missing.push_back(n);
    }
    if (byTarget.empty()) {
        const std::string text = "aucune variable lisible : " + fewOf(missing);
        if (why) *why = text;
        say("Lire des variables : " + text, true);
        return {};
    }
    std::vector<HmiCyclicVarsDialog::Planned> planned;
    for (const auto& [target, items] : byTarget)
        for (auto& pr : mbtool::planReads(items, merge, gap)) planned.push_back({target, std::move(pr), items});
    auto ids = addPlanned(planned);
    if (!missing.empty()) say("Pas trouv\xC3\xA9" "es (ou pas lisibles) : " + fewOf(missing), true);
    return ids;
}

std::vector<int> HmiCyclicPage::addFromZones(const std::string& equipment, std::string* why) {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    const hmi::Equipment* e = p ? p->equipmentByName(equipment) : nullptr;
    const auto refuse = [&](std::string text) {
        if (why) *why = text;
        say("Depuis les zones m\xC3\xA9moire : " + text, true);
        return std::vector<int>{};
    };
    if (!e) return refuse("pas d'\xC3\xA9quipement \xC2\xAB " + equipment + " \xC2\xBB");
    if (!e->modbus()) return refuse(e->name + " n'est pas un \xC3\xA9quipement Modbus TCP");
    std::vector<hmi::ModbusRead> reads;
    for (const auto t : hmi::kMemTables) {
        const int f = t == hmi::MemTable::Coils ? 1 : t == hmi::MemTable::DiscreteInputs ? 2 : t == hmi::MemTable::InputRegisters ? 4 : 3;
        const std::uint32_t most = f <= 2 ? 2000u : 125u;
        for (const auto& range : e->zones.of(t)) {
            for (std::uint32_t first = range.first; first <= range.last && first <= 65535u; first += most) {
                const std::uint32_t count = std::min(most, range.last - first + 1);
                hmi::ModbusRead q;
                q.target = "equipement";
                q.equipment = e->name;
                q.function = f;
                q.address = static_cast<int>(first);
                q.count = static_cast<int>(count);
                q.lowFirst = e->wordOrder != "fort";
                q.name = e->name + " " + mbtool::modiconOf(f, q.address) + (count > 1 ? "-" + mbtool::modiconOf(f, q.address + q.count - 1) : std::string{});
                q.traced = {0};
                reads.push_back(std::move(q));
                if (first + most < first) break;      // pas de debordement
            }
        }
    }
    if (reads.empty())
        return refuse(e->name + " n'a pas de zone d\xC3\xA9" "clar\xC3\xA9" "e ou d\xC3\xA9tect\xC3\xA9" "e (Configuration \xE2\x80\xBA \xC3\x89quipements \xE2\x80\xBA Carte m\xC3\xA9moire)");
    remember();
    std::vector<int> ids;
    for (auto& q : reads) {
        Row r;
        r.id = nextId();
        r.read = std::move(q);
        ids.push_back(r.id);
        rows_.push_back(std::move(r));
    }
    selected_ = ids.front();
    changed(true);
    (void)select(selected_);
    say(std::to_string(ids.size()) + (ids.size() > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte") + " depuis les zones de " + e->name + " (Ctrl+Z les retire).");
    return ids;
}

HmiCyclicPage::PasteReport HmiCyclicPage::pasteRequests(const std::string& text) {
    PasteReport rep;
    const auto lines = linesOf(text);
    if (lines.empty()) return rep;
    const char sep = lines.front().find('\t') != std::string::npos ? '\t' : ';';
    std::vector<std::string> keys{"nom", "cible", "adresse", "nombre", "format", "periode"};    // sans titres : cet ordre
    std::size_t start = 0;
    {
        const auto head = cellsOf(lines.front(), sep);
        std::vector<std::string> found;
        std::size_t known = 0;
        for (const auto& h : head) {
            found.push_back(columnKey(h));
            known += found.back().empty() ? 0 : 1;
        }
        if (known >= 2) {
            keys = found;
            start = 1;
        }
    }
    std::vector<Row> added;
    int id = nextId();
    for (std::size_t i = start; i < lines.size(); ++i) {
        if (trimmed(lines[i]).empty()) continue;
        const auto cells = cellsOf(lines[i], sep);
        // Une ligne : un brouillon de requete, regle par setField (les memes controles qu'a la main).
        Row r;
        r.id = id;
        r.read.name = "Requ\xC3\xAAte " + std::to_string(id);
        r.read.traced = {0};
        rows_.push_back(r);
        std::string note;
        bool ok = true;
        for (std::size_t c = 0; c < cells.size() && c < keys.size() && ok; ++c) {
            if (keys[c].empty() || cells[c].empty()) continue;
            std::string why;
            if (!setFieldQuiet(id, keys[c], cells[c], &why)) {
                ok = false;
                note = "ligne " + std::to_string(i + 1) + " : " + keys[c] + " \xC2\xAB " + cells[c] + " \xC2\xBB : " + why;
            }
        }
        Row done = rows_.back();
        rows_.pop_back();
        if (!ok) {
            ++rep.refused;
            rep.notes.push_back(note);
            continue;
        }
        added.push_back(std::move(done));
        rep.ids.push_back(id);
        ++id;
    }
    if (!added.empty()) {
        remember();
        for (auto& r : added) rows_.push_back(std::move(r));
        rep.added = added.size();
        selected_ = rep.ids.front();
        changed(true);
        (void)select(selected_);
    }
    say(std::to_string(rep.added) + " requ\xC3\xAAte(s) coll\xC3\xA9" "e(s)" + (rep.refused ? ", " + std::to_string(rep.refused) + " refus\xC3\xA9" "e(s) : " + rep.notes.front() : std::string{}),
        rep.refused > 0);
    return rep;
}

// ------------------------------------------------------- menus, dialogues ---
HmiCyclicOverlay* HmiCyclicPage::overlay() const noexcept { return overlay_ && !overlay_->closed() ? overlay_ : nullptr; }

void HmiCyclicPage::dropOverlay() {
    if (!overlay_) return;
    // Jamais detruit pendant son propre evenement : il attend l'image suivante.
    graveyard_.push_back(tool_.removeChild(*overlay_));
    overlay_ = nullptr;
}

void HmiCyclicPage::showOverlay(std::unique_ptr<HmiCyclicOverlay> o) {
    dropOverlay();
    o->layoutIn(tool_.bounds());
    overlay_ = &static_cast<HmiCyclicOverlay&>(tool_.addChild(std::move(o)));
    links_ += overlay_->closedSignal->connect([this] { closing_ = true; });
}

void HmiCyclicPage::openAddMenu(gfx::Point at) {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    bool zones = false, vars = false;
    if (p) {
        vars = !p->comm.addresses.empty() || static_cast<bool>(tool_.hosts_.plcVariables);
        for (const auto& e : p->equipments) {
            if (!e.modbus()) continue;
            vars = true;
            for (const auto t : hmi::kMemTables) zones = zones || !e.zones.of(t).empty();
        }
    }
    std::vector<HmiCyclicMenu::Item> items;
    items.push_back(menuItem(MNew, "Nouvelle requ\xC3\xAAte", "une plage \xC3\xA0 lire, r\xC3\xA9gl\xC3\xA9" "e \xC3\xA0 droite", "Inser", GPlus));
    items.push_back(menuItem(MFromRead, "Reprendre la requ\xC3\xAAte de Lecture / \xC3\xA9" "criture", "la cible et la plage de l'onglet Lecture / \xC3\xA9" "criture", {}, GReadWrite));
    items.push_back(menuItem(MFromVars, "Depuis des variables\xE2\x80\xA6", "l'automate ou les variables IHM li\xC3\xA9" "es, regroup\xC3\xA9" "es", {}, GVariables));
    items.back().enabled = vars;
    items.back().why = "aucune variable localis\xC3\xA9" "e ni li\xC3\xA9" "e";
    items.push_back(menuItem(MFromZones, "Depuis les zones m\xC3\xA9moire d'un \xC3\xA9quipement\xE2\x80\xA6", "une requ\xC3\xAAte par zone d\xC3\xA9" "clar\xC3\xA9" "e ou d\xC3\xA9tect\xC3\xA9" "e", {}, GMemory));
    items.back().enabled = zones;
    items.back().why = "aucun \xC3\xA9quipement n'a de zones (Carte m\xC3\xA9moire)";
    items.push_back(menuItem(MPaste, "Coller depuis Excel", "colonnes Nom, \xC3\x89quipement, Adresse, Nombre, Format, P\xC3\xA9riode", "Ctrl+V", GTable));
    HmiCyclicMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    items.push_back(menuItem(MOpenSet, "Ouvrir un jeu de lecture\xE2\x80\xA6", "une liste enregistr\xC3\xA9" "e dans le projet", {}, GFolder));
    auto menu = std::make_unique<HmiCyclicMenu>(id() + ".addMenu", std::move(items), at, 420.f);
    menu->chosen = [this, at](int item) {
        switch (item) {
            case MNew: (void)addNew(); break;
            case MFromRead: (void)addFromReadTab(); break;
            case MFromVars: openVariablesDialog(); break;
            case MFromZones: openZonesMenu(at); break;
            case MPaste:
                if (tool_.hosts_.clipboardText) (void)pasteRequests(tool_.hosts_.clipboardText());
                break;
            case MOpenSet: openSetsMenu(at); break;
            default: break;
        }
    };
    showOverlay(std::move(menu));
}

void HmiCyclicPage::openZonesMenu(gfx::Point at) {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    std::vector<HmiCyclicMenu::Item> items;
    HmiCyclicMenu::Item head;
    head.heading = true;
    head.label = "Les zones m\xC3\xA9moire de\xE2\x80\xA6";
    items.push_back(head);
    std::vector<std::string> names;
    if (p)
        for (const auto& e : p->equipments) {
            if (!e.modbus()) continue;
            std::size_t n = 0;
            for (const auto t : hmi::kMemTables) n += e.zones.of(t).size();
            HmiCyclicMenu::Item it;
            it.id = MZoneBase + static_cast<int>(names.size());
            it.label = e.name;
            it.detail = n ? std::to_string(n) + " zone(s)" + (e.zones.origin.empty() ? std::string{} : ", " + e.zones.origin) : std::string("aucune zone");
            it.glyph = GMemory;
            it.enabled = n > 0;
            it.why = "aucune zone : Configuration \xE2\x80\xBA \xC3\x89quipements \xE2\x80\xBA Carte m\xC3\xA9moire";
            items.push_back(std::move(it));
            names.push_back(e.name);
        }
    auto menu = std::make_unique<HmiCyclicMenu>(id() + ".zonesMenu", std::move(items), at, 420.f);
    menu->chosen = [this, names](int item) {
        const int k = item - MZoneBase;
        if (k >= 0 && static_cast<std::size_t>(k) < names.size()) (void)addFromZones(names[static_cast<std::size_t>(k)]);
    };
    showOverlay(std::move(menu));
}

void HmiCyclicPage::openSetsMenu(gfx::Point at) {
    const hmi::Project* p = tool_.hosts_.project ? tool_.hosts_.project() : nullptr;
    std::vector<HmiCyclicMenu::Item> items;
    HmiCyclicMenu::Item head;
    head.heading = true;
    head.label = "Les jeux de ce projet";
    items.push_back(head);
    std::vector<std::string> names;
    if (p)
        for (const auto& s : p->modbusSets) {
            HmiCyclicMenu::Item it;
            it.id = MSetBase + static_cast<int>(names.size());
            it.label = s.name;
            it.detail = std::to_string(s.reads.size()) + (s.reads.size() > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte") + ", " + std::to_string(s.periodMs) + " ms";
            it.glyph = GFolder;
            it.check = s.name == set_.name;
            it.current = it.check;
            items.push_back(std::move(it));
            names.push_back(s.name);
        }
    if (names.empty()) {
        HmiCyclicMenu::Item none;
        none.id = -1;
        none.label = "Aucun jeu enregistr\xC3\xA9";
        none.enabled = false;
        none.why = "Enregistrer le jeu le garde dans le projet";
        items.push_back(none);
    }
    HmiCyclicMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    items.push_back(menuItem(MSave, "Enregistrer le jeu", "dans le projet IHM (il suit ses versions)", "Ctrl+S", GSave));
    items.push_back(menuItem(MSaveAs, "Enregistrer sous\xE2\x80\xA6", "un autre nom", {}, GSave));
    items.push_back(menuItem(MNewSet, "Nouveau jeu vide", {}, {}, GPlus));
    items.push_back(sep);
    items.push_back(menuItem(MExportSet, "Exporter le jeu (CSV)\xE2\x80\xA6", "pour un autre poste ou une autre affaire", {}, GExport));
    items.push_back(menuItem(MImportSet, "Importer un jeu (CSV)\xE2\x80\xA6",
                             tool_.hosts_.importSetFile ? "un fichier \xC3\xA9" "crit par Exporter le jeu" : "un jeu export\xC3\xA9 (copi\xC3\xA9 dans le presse-papiers)", {}, GFolder));
    auto menu = std::make_unique<HmiCyclicMenu>(id() + ".setsMenu", std::move(items), at, 400.f);
    menu->chosen = [this, names](int item) {
        if (item >= MSetBase && static_cast<std::size_t>(item - MSetBase) < names.size()) {
            (void)openSet(names[static_cast<std::size_t>(item - MSetBase)]);
            return;
        }
        switch (item) {
            case MSave: (void)saveSet(); break;
            case MSaveAs: openSaveAs(); break;
            case MNewSet: newSet(); break;
            case MExportSet:
                // Ou l'ecrire : le dialogue des exports de l'IHM (sans ecran : dans exports/).
                if (!askExportTarget("le jeu de lecture (CSV)", "Fichiers CSV|*.csv", [this] { (void)exportSet(); })) (void)exportSet();
                break;
            case MImportSet:
                if (tool_.hosts_.importSetFile) tool_.hosts_.importSetFile();
                else if (tool_.hosts_.clipboardText) (void)importSet(tool_.hosts_.clipboardText());
                break;
            default: break;
        }
    };
    showOverlay(std::move(menu));
}

void HmiCyclicPage::openSaveAs() {
    auto prompt = std::make_unique<HmiCyclicPrompt>(id() + ".saveAs", "Enregistrer le jeu sous\xE2\x80\xA6",
                                                    "Le jeu de lecture se garde dans le projet IHM : il suit ses versions et part avec l'affaire.",
                                                    set_.name == kNoName ? std::string{} : set_.name, "Enregistrer");
    prompt->done = [this](const std::string& name, std::string* why) { return saveSet(name.empty() ? std::string(" ") : name, why); };
    showOverlay(std::move(prompt));
}

void HmiCyclicPage::openVariablesDialog() {
    auto dlg = std::make_unique<HmiCyclicVarsDialog>(id() + ".vars", variableTabs(), nextId());
    dlg->added = [this](const std::vector<HmiCyclicVarsDialog::Planned>& planned) { (void)addPlanned(planned); };
    showOverlay(std::move(dlg));
}

void HmiCyclicPage::openExportDialog() {
    const std::string folder = recordFolder();
    // Le fichier du jour (heure murale ; avec 0, le nom montrait 1970-01-01).
    const double wall = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto dlg = std::make_unique<HmiCyclicExportDialog>(id() + ".export", set_.periodMs, poller_.recording() || wantRecord_,
                                                       (folder.empty() ? std::string("exports/modbus") : folder) + "/" + mbtool::dateText(wall, true) + "_" + stemOf(set_.name) + ".csv");
    auto* raw = dlg.get();
    dlg->changed = [this, raw] { raw->setPreview(exportPreview(raw->oneFile(), raw->raw())); };
    dlg->exported = [this](bool oneFile, bool rawValues, bool record) {
        (void)exportCsv(oneFile, rawValues);
        if (record != (poller_.recording() || wantRecord_)) (void)setRecording(record);
    };
    // Ailleurs... : ou exporter (le dialogue des exports de l'IHM et son bouton ..., comme
    // les autres exports) ; sans ecran pour le demander (les essais) : dans exports/.
    dlg->elsewhere = [this, raw] {
        const bool one = raw->oneFile(), rawValues = raw->raw(), record = raw->record();
        if (!askExportTarget("la lecture cyclique (CSV)", "Fichiers CSV|*.csv", [this, one, rawValues] { (void)exportCsv(one, rawValues); }))
            (void)exportCsv(one, rawValues);
        if (record != (poller_.recording() || wantRecord_)) (void)setRecording(record);
    };
    raw->setPreview(exportPreview(true, false));
    showOverlay(std::move(dlg));
}

} // namespace app
