// =============================================================================
//  app/hmi/HmiDeployPane.cpp - 1.28.0 : IHM > Deploiement reseau (voir le .hpp)
// =============================================================================
#include "HmiDeployPane.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace app {

namespace {

using namespace hmikit;
using PG = ui::PropertyGrid;

enum : int { CPackage = 1, CDiscover, CAdd, CPair, CStatus, CLogs, CForget };

bool sameText(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

} // namespace

HmiDeployPane::HmiDeployPane(std::string id, Backend backend) : ui::Widget(std::move(id)), backend_(std::move(backend)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(CPackage, HmiGlyph::Export,
               "Cr\xC3\xA9" "er un package de d\xC3\xA9ploiement : le poste d'exploitation du projet (FINISH) et son lanceur, dans un "
               "fichier .xpgdeploy sign\xC3\xA9 par ta cl\xC3\xA9 d'\xC3\xA9" "diteur",
               "Cr\xC3\xA9" "er un package");
    tools->separator();
    tools->add(CDiscover, HmiGlyph::Search,
               "Rechercher les PC de maintenance du r\xC3\xA9seau local (leur agent r\xC3\xA9pond ; une r\xC3\xA9ponse ne prouve rien tant "
               "que la machine n'est pas appair\xC3\xA9" "e)",
               "Rechercher");
    tools->add(CAdd, HmiGlyph::Plus, "Ajouter une machine par son adresse IP (un autre r\xC3\xA9seau, une diffusion bloqu\xC3\xA9" "e)",
               "Ajouter par adresse");
    tools->add(CPair, HmiGlyph::Key,
               "Appairer la machine choisie : tape le code \xC3\xA0 6 chiffres que son agent affiche (XPG-Deployment-Agent --appairer) ; "
               "les deux machines gardent alors l'empreinte du certificat de l'autre",
               "Appairer");
    tools->add(CStatus, HmiGlyph::Refresh, "L'\xC3\xA9tat de la machine choisie (une machine appair\xC3\xA9" "e)", "\xC3\x89tat");
    tools->add(CLogs, HmiGlyph::History, "Le journal de l'agent de la machine choisie (une machine appair\xC3\xA9" "e)", "Journal");
    tools->add(CForget, HmiGlyph::Delete,
               "Oublier la machine choisie : ce PC ne la croit plus, et lui demande de l'oublier aussi si elle r\xC3\xA9pond",
               "Oublier");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto chosen = [this] { return selected() != nullptr; };
    const auto pairedChosen = [this] { return selected() && selected()->paired; };
    tools_->setEnabledWhen(CPackage, [this] { return static_cast<bool>(backend_.createPackage) && hosts_.projectFolder && !hosts_.projectFolder().empty(); });
    tools_->setEnabledWhen(CPair, [this, chosen] { return chosen() && !selected()->paired; });
    tools_->setEnabledWhen(CStatus, pairedChosen);
    tools_->setEnabledWhen(CLogs, pairedChosen);
    tools_->setEnabledWhen(CForget, chosen);

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto table = std::make_unique<ui::TableView>(base + ".machines");
    table->setColumns({{"Machine", 180.f}, {"Adresse", 150.f}, {"Identifiant", 100.f}, {"Agent", 80.f}, {"\xC3\x89tat", 150.f},
                       {"Empreinte du certificat", 230.f}, {"Vue", 160.f}});
    auto logs = std::make_unique<ui::TableView>(base + ".journal");
    logs->setColumns({{"Journal de l'agent", 1100.f}});
    table_ = table.get();
    logs_ = logs.get();
    tabs->addTab({"Machines", ui::Icon::Station}, std::move(table));
    tabs->addTab({"Journal de la machine", ui::Icon::Document}, std::move(logs));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<PG&>(addChild(std::make_unique<PG>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case CPackage:
                if (hosts_.confirmPackage) hosts_.confirmPackage();
                else (void)createPackage();
                break;
            case CDiscover: (void)discover(); break;
            case CAdd:
                if (hosts_.askAddress) hosts_.askAddress();
                break;
            case CPair:
                if (const auto* m = selected(); m && hosts_.askCode) hosts_.askCode(*m);
                break;
            case CStatus: (void)askStatus(); break;
            case CLogs: (void)askLogs(); break;
            case CForget: (void)forget(); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        const auto rows = table_->selectedModelRows();     // le tableau peut etre trie : les lignes du modele
        if (!rows.empty() && rows.front() < machines_.size()) selectedId_ = machines_[rows.front()].id;
        rebuildProperties();
    });

    thread_ = std::thread([this] { worker(); });
    rebuildTable();
    if (backend_.identity) identity_ = backend_.identity();
    rebuildProperties();
    (void)refreshMachines();
}

HmiDeployPane::~HmiDeployPane() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void HmiDeployPane::setHosts(Hosts h) { hosts_ = std::move(h); }

// ---------------------------------------------------------------- le fil ----

void HmiDeployPane::worker() {
    for (;;) {
        std::pair<std::string, Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        Result r;
        try {
            r = job.second();
        } catch (const std::exception& e) {
            r.error = true;
            r.message = job.first + " : " + e.what();
        } catch (...) {
            r.error = true;
            r.message = job.first + " : erreur inattendue";
        }
        std::lock_guard<std::mutex> lock(mutex_);
        done_.push_back(std::move(r));
        --pending_;
    }
}

bool HmiDeployPane::start(std::string what, Job job) {
    if (!job) {
        say(what + " : pas disponible ici", true);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.emplace_back(what, std::move(job));
        ++pending_;
    }
    running_ = what;
    status_->setTransientMessage(what + "\xE2\x80\xA6", 30.0);
    wake_.notify_one();
    invalidate();
    return true;
}

bool HmiDeployPane::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ > 0 || !done_.empty();
}

void HmiDeployPane::poll() {
    std::deque<Result> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(done_);
        if (pending_ == 0) running_.clear();
    }
    for (auto& r : ready) apply(std::move(r));
}

bool HmiDeployPane::waitIdle(double seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_ == 0) break;
        }
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    poll();
    return true;
}

void HmiDeployPane::apply(Result r) {
    if (r.hasMachines) {
        machines_ = std::move(r.machines);
        rebuildTable();
    }
    if (r.hasDetails) {
        details_ = std::move(r.details);
        detailsOf_ = selectedId_;
    }
    if (r.hasLogs) {
        logLines_ = std::move(r.logs);
        logsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Journal de l'agent"}, [&] {
            std::vector<std::vector<std::string>> rows;
            for (const auto& l : logLines_) rows.push_back({l});
            return rows;
        }());
        logs_->setModel(logsModel_);
        tabs_->setCurrentIndex(1);
    }
    if (!r.package.empty()) lastPackage_ = r.package;
    if (backend_.identity) identity_ = backend_.identity();
    rebuildProperties();
    if (!r.message.empty()) say(r.message, r.error);
    invalidate();
}

// ------------------------------------------------------------ les gestes ----

bool HmiDeployPane::refreshMachines() { return start("Les machines appair\xC3\xA9" "es", backend_.machines); }

bool HmiDeployPane::discover() {
    if (!backend_.discover) return start("Rechercher", {});
    return start("Rechercher les PC de maintenance", [b = backend_.discover] { return b(true); });
}

bool HmiDeployPane::addAddress(const std::string& address0, int port) {
    const std::string address = trimmed(address0);
    if (address.empty()) {
        say("Ajouter : donne une adresse IP (192.168.1.20) ou un nom de machine", true);
        return false;
    }
    if (port <= 0 || port > 65535) {
        say("Ajouter : le port va de 1 \xC3\xA0 65535", true);
        return false;
    }
    if (!backend_.add) return start("Ajouter", {});
    return start("Ajouter " + address, [b = backend_.add, address, port] { return b(address, port); });
}

bool HmiDeployPane::pairWith(const std::string& machineId, const std::string& code0) {
    std::string code;
    for (char c : code0)
        if (c != ' ') code.push_back(c);
    const bool digits = code.size() == 6 && std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (!digits) {
        say("Appairer : le code a 6 chiffres (celui qu'affiche l'agent, valable 5 minutes)", true);
        return false;
    }
    if (!machineId.empty()) (void)select(machineId);
    const Machine* m = selected();
    if (!m) {
        say("Appairer : choisis d'abord une machine", true);
        return false;
    }
    if (!backend_.pair) return start("Appairer", {});
    return start("Appairer " + m->name, [b = backend_.pair, copy = *m, code] { return b(copy, code); });
}

bool HmiDeployPane::askStatus() {
    const Machine* m = selected();
    if (!m || !m->paired) {
        say("\xC3\x89tat : choisis une machine appair\xC3\xA9" "e", true);
        return false;
    }
    if (!backend_.status) return start("\xC3\x89tat", {});
    return start("\xC3\x89tat de " + m->name, [b = backend_.status, copy = *m] { return b(copy); });
}

bool HmiDeployPane::askLogs() {
    const Machine* m = selected();
    if (!m || !m->paired) {
        say("Journal : choisis une machine appair\xC3\xA9" "e", true);
        return false;
    }
    if (!backend_.logs) return start("Journal", {});
    return start("Journal de " + m->name, [b = backend_.logs, copy = *m] { return b(copy); });
}

bool HmiDeployPane::forget() {
    const Machine* m = selected();
    if (!m) {
        say("Oublier : choisis une machine", true);
        return false;
    }
    if (!backend_.forget) return start("Oublier", {});
    return start("Oublier " + m->name, [b = backend_.forget, copy = *m] { return b(copy); });
}

bool HmiDeployPane::createPackage() {
    const std::string folder = hosts_.projectFolder ? hosts_.projectFolder() : std::string{};
    if (folder.empty()) {
        say("Cr\xC3\xA9" "er un package : enregistre d'abord le projet", true);
        return false;
    }
    std::string why;
    if (hosts_.finished && !hosts_.finished(&why)) {
        say("Cr\xC3\xA9" "er un package : le projet doit \xC3\xAAtre FINISH (" + why + ")", true);
        return false;
    }
    if (!backend_.createPackage) return start("Cr\xC3\xA9" "er un package", {});
    return start("Cr\xC3\xA9" "er le package de d\xC3\xA9ploiement", [b = backend_.createPackage, folder] { return b(folder); });
}

bool HmiDeployPane::select(const std::string& key) {
    for (std::size_t i = 0; i < machines_.size(); ++i) {
        const auto& m = machines_[i];
        if (sameText(m.id, key) || sameText(m.name, key) || m.address == key) {
            selectedId_ = m.id;
            table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
            rebuildProperties();
            return true;
        }
    }
    return false;
}

const HmiDeployPane::Machine* HmiDeployPane::selected() const {
    for (const auto& m : machines_)
        if (!selectedId_.empty() && m.id == selectedId_) return &m;
    return nullptr;
}

// -------------------------------------------------------------- l'ecran -----

void HmiDeployPane::rebuildTable() {
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (const auto& m : machines_) {
        rows.push_back({m.name, m.address + (m.port != 47861 ? ":" + std::to_string(m.port) : std::string{}), m.id, m.version, m.state,
                        m.fingerprint, m.seen});
        tones.push_back(m.paired ? 1 : 0);
    }
    tableModel_ = std::make_shared<Rows>(
        std::vector<std::string>{"Machine", "Adresse", "Identifiant", "Agent", "\xC3\x89tat", "Empreinte du certificat", "Vue"}, std::move(rows),
        [tones](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r < tones.size() && c == 0) s.bold = tones[r] == 1;
            return s;
        });
    table_->setModel(tableModel_);
    for (std::size_t i = 0; i < machines_.size(); ++i)
        if (machines_[i].id == selectedId_) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
}

void HmiDeployPane::rebuildProperties() {
    std::vector<PG::Category> cats;
    if (const Machine* m = selected()) {
        PG::Category c;
        c.name = "Machine choisie";
        c.properties.push_back(prop("Nom", m->name, PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Adresse", m->address + ":" + std::to_string(m->port), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Identifiant", m->id, PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Agent", m->version, PG::ValueType::ReadOnly));
        c.properties.push_back(prop("\xC3\x89tat", m->state, PG::ValueType::ReadOnly, {},
                                    "Appair\xC3\xA9" "e : son certificat est connu, ses r\xC3\xA9ponses sont crues. D\xC3\xA9" "couverte : une "
                                    "adresse qui a r\xC3\xA9pondu, rien de plus ; il faut l'appairer."));
        c.properties.push_back(prop("Empreinte du certificat", m->fingerprint, PG::ValueType::ReadOnly, {},
                                    "SHA-256 du certificat de son agent. \xC3\x80 comparer avec celle qu'affiche "
                                    "XPG-Deployment-Agent --etat sur la machine."));
        if (detailsOf_ == m->id)
            for (const auto& [k, v] : details_) c.properties.push_back(prop(k, v, PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    if (!identity_.empty()) {
        PG::Category me;
        me.name = "Ce PC";
        for (const auto& [k, v] : identity_) me.properties.push_back(prop(k, v, PG::ValueType::ReadOnly));
        cats.push_back(std::move(me));
    }
    if (!lastPackage_.empty()) {
        PG::Category pk;
        pk.name = "Dernier package";
        pk.properties.push_back(prop("Fichier", lastPackage_, PG::ValueType::ReadOnly));
        cats.push_back(std::move(pk));
    }
    grid_->setCategories(std::move(cats));
}

void HmiDeployPane::say(std::string text, bool error) {
    message_ = std::move(text);
    lastError_ = error;
    status_->setTransientMessage(message_, 10.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiDeployPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(460.f, b.w * 0.34f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiDeployPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    poll();
    if (busy()) invalidate();
}

} // namespace app
