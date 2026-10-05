// Configuration > Notifications (lot 14).
#include "HmiNotifyPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiNotifyHost.hpp"
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiNotify.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using hmi::Id;
using PG = ui::PropertyGrid;

namespace {

enum : int { CTestMail = 1, CTestSms, CAdd, CRemove, CPassword, CTestBox, CClearBox };

// Les priorites qu'un destinataire recoit : de la critique (1) a N.
const char* const kPriorityChoices[] = {"Critique seulement", "Critique et haute", "Jusqu'\xC3\xA0 moyenne", "Toutes"};

std::string priorityText(int n) { return kPriorityChoices[std::clamp(n, 1, 4) - 1]; }

bool parsePriority(const std::string& v, int& out) {
    for (int k = 0; k < 4; ++k)
        if (same(v, kPriorityChoices[k])) { out = k + 1; return true; }
    const std::string l = lower(v);
    if (l.size() == 1 && l[0] >= '1' && l[0] <= '4') { out = l[0] - '0'; return true; }
    for (int k = 1; k <= hmi::kAlarmPriorities; ++k)
        if (same(v, hmi::alarmPriorityLabel(k))) { out = k; return true; }
    return false;
}

std::string daysText(const std::string& days) {
    static const char* names[] = {"lun", "mar", "mer", "jeu", "ven", "sam", "dim"};
    if (days == "1234567") return "tous les jours";
    if (days == "12345") return "lun.-ven.";
    if (days == "67") return "sam.-dim.";
    std::string out;
    for (const char c : days)
        if (c >= '1' && c <= '7') out += (out.empty() ? "" : " ") + std::string(names[c - '1']);
    return out.empty() ? std::string("aucun jour") : out;
}

bool parseDays(const std::string& raw, std::string& out) {
    const std::string l = lower(trimmed(raw));
    if (l == "tous" || l == "tous les jours") { out = "1234567"; return true; }
    if (l == "semaine" || l == "lun-ven" || l == "lun.-ven.") { out = "12345"; return true; }
    if (l == "week-end" || l == "weekend" || l == "sam-dim" || l == "sam.-dim.") { out = "67"; return true; }
    std::string d;
    for (const char c : l) {
        if (c >= '1' && c <= '7') { if (d.find(c) == std::string::npos) d += c; }
        else if (c != ' ' && c != ',' && c != ';') return false;
    }
    if (d.empty()) return false;
    std::sort(d.begin(), d.end());
    out = d;
    return true;
}

bool parseClock(const std::string& v, std::string& out) {
    int h = -1, m = 0;
    const std::string t = trimmed(v);
    if (std::sscanf(t.c_str(), "%d:%d", &h, &m) < 1 && std::sscanf(t.c_str(), "%dh%d", &h, &m) < 1) return false;
    if (h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) return false;
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d", h, m);
    out = b;
    return true;
}

// Dans la grille, un retour a la ligne s'ecrit \n.
std::string escapeLines(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}
std::string unescapeLines(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
        else out += s[i];
    }
    return out;
}

bool validEmail(const std::string& e) {
    const auto at = e.find('@');
    return at != std::string::npos && at > 0 && e.find('.', at) != std::string::npos && e.back() != '.' && e.find(' ') == std::string::npos
           && e.find('@', at + 1) == std::string::npos;
}

std::string cleanPhone(const std::string& raw, bool& ok) {
    std::string out;
    ok = true;
    for (const char c : trimmed(raw)) {
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '+' && out.empty())) out += c;
        else if (c != ' ' && c != '.' && c != '-' && c != '(' && c != ')') ok = false;
    }
    if (out.size() < 3) ok = false;
    return out;
}

std::string firstLine(const std::string& s) {
    const auto nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl) + " ...";
}

} // namespace

HmiNotifyPane::HmiNotifyPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(CTestMail, HmiGlyph::Check, "Tester le courriel : un message d'essai au destinataire choisi, par le relais", "Tester le courriel");
    tools->add(CTestSms, HmiGlyph::Check, "Tester le SMS : un message d'essai au destinataire choisi, par la passerelle", "Tester le SMS");
    tools->separator();
    tools->add(CAdd, HmiGlyph::Plus, "Ajouter un destinataire", "Ajouter");
    tools->add(CRemove, HmiGlyph::Delete, "Retirer le destinataire choisi", "Retirer");
    tools->separator();
    tools->add(CPassword, HmiGlyph::Password, "Le mot de passe du relais SMTP (s'il demande un utilisateur) : gard\xC3\xA9 masqu\xC3\xA9 dans le projet",
               "Mot de passe SMTP");
    tools->add(CTestBox, HmiGlyph::Bell,
               "Essayer avec la bo\xC3\xAEte d'essai : le relais et la passerelle sur ce poste (127.0.0.1), la bo\xC3\xAEte ouverte - "
               "sans relais, sans passerelle, sans r\xC3\xA9veiller l'astreinte",
               "Bo\xC3\xAEte d'essai");
    tools->add(CClearBox, HmiGlyph::Delete, "Vider la liste des messages re\xC3\xA7us par la bo\xC3\xAEte d'essai", "Vider la bo\xC3\xAEte");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(CRemove, [this] { return !selectedRecipient().empty(); });
    tools_->setEnabledWhen(CTestMail, [this] { return !selectedRecipient().empty() && hosts_.notify; });
    tools_->setEnabledWhen(CTestSms, [this] { return !selectedRecipient().empty() && hosts_.notify; });
    tools_->setEnabledWhen(CClearBox, [this] { return hosts_.notify && hosts_.notify() && hosts_.notify()->testBox(); });

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto table = std::make_unique<ui::TableView>(base + ".recipients");
    table->setColumns({{"Destinataire", 190.f}, {"Courriel", 220.f}, {"T\xC3\xA9l\xC3\xA9phone", 130.f}, {"Priorit\xC3\xA9s", 140.f},
                       {"Groupes", 150.f}, {"Astreinte", 190.f}, {"Disparition", 90.f}, {"Actif", 60.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    auto sent = std::make_unique<ui::TableView>(base + ".sent");
    sent->setColumns({{"Heure", 76.f}, {"Canal", 76.f}, {"Destinataire", 120.f}, {"Adresse", 200.f}, {"Alarme", 130.f}, {"\xC3\x89v\xC3\xA9nement", 118.f},
                      {"\xC3\x89tat", 108.f}, {"R\xC3\xA9sultat", 420.f}});
    auto box = std::make_unique<ui::TableView>(base + ".box");
    box->setColumns({{"Heure", 80.f}, {"Canal", 80.f}, {"De", 195.f}, {"\xC3\x80", 190.f}, {"Sujet", 285.f}, {"Texte", 420.f}, {"Pi\xC3\xA8" "ce jointe", 240.f}});
    table_ = static_cast<ui::TableView*>(table.get());
    sent_ = static_cast<ui::TableView*>(sent.get());
    box_ = static_cast<ui::TableView*>(box.get());
    tabs->addTab({"Destinataires", ui::Icon::User}, std::move(table));
    tabs->addTab({"Envois", ui::Icon::Mail}, std::move(sent));
    tabs->addTab({"Bo\xC3\xAEte d'essai", ui::Icon::Mail}, std::move(box));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case CTestMail: case CTestSms: (void)test(a == CTestSms); break;
            case CAdd: (void)addRecipient("Nouveau_destinataire"); break;
            case CRemove: if (!selectedRecipient().empty()) (void)removeRecipient(selectedRecipient()); break;
            case CPassword: if (hosts_.askPassword) hosts_.askPassword(); break;
            case CTestBox: (void)useTestBox(); break;
            case CClearBox:
                if (auto* host = hosts_.notify ? hosts_.notify() : nullptr; host && host->testBox()) {
                    const_cast<hmi::notify::TestServer*>(host->testBox())->clear();
                    refreshLive();
                }
                break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    // Lot 20 : coller des destinataires depuis Excel (le nom dit lequel).
    table_->setSelectionMode(ui::SelectionMode::Extended);
    paste_.table = table_;
    paste_.keyColumn = 0;
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
    };
    paste_.target = [this](const ui::TableView::PasteRequest& rq) {
        paste::Target tg;
        tg.noun = "destinataire";
        tg.nouns = "destinataires";
        tg.feminine = false;
        const auto field = [this](std::string key) {
            return [this, key](const std::string& k, const std::string& v, std::string* why) { return setRecipientField(k, key, v, why); };
        };
        // "oui, de 18:00 a 08:00" : le premier mot dit oui ou non.
        const auto flag = [this](std::string key) {
            return [this, key](const std::string& k, const std::string& v, std::string* why) {
                const std::string first = v.substr(0, v.find_first_of(" ,;"));
                const std::string n = paste::normalizedTitle(first);
                const bool on = n == "oui" || n == "true" || n == "1" || n == "yes" || n == "x" || n == "actif" || n == "active";
                return setRecipientField(k, key, on ? "TRUE" : "FALSE", why);
            };
        };
        tg.columns.push_back(paste::column("Destinataire", {"Nom", "Name", "Contact"}, 0, nullptr, true));
        tg.columns.push_back(paste::column("Courriel", {"Email", "E-mail", "Mail", "Adresse mail"}, 1, field("courriel")));
        tg.columns.push_back(paste::column("T\xC3\xA9l\xC3\xA9phone", {"Telephone", "Tel", "Portable", "Mobile", "SMS", "Phone"}, 2, field("telephone")));
        tg.columns.push_back(paste::column("Priorit\xC3\xA9s", {"Priorites", "Priorite"}, 3, field("priorite")));
        tg.columns.push_back(paste::column("Groupes", {"Groupe", "Zones"}, 4, field("groupes")));
        tg.columns.push_back(paste::column("Astreinte", {"Duty"}, 5, flag("astreinte")));
        tg.columns.push_back(paste::column("Disparition", {"Fin d'alarme"}, 6, flag("disparition")));
        tg.columns.push_back(paste::column("Actif", {"Active", "Enabled"}, 7, flag("actif")));
        tg.exists = [this](const std::string& k) { return doc_->project.recipientByName(k) != nullptr; };
        tg.create = [this](const std::string& k, const std::map<std::string, std::string>&, paste::Notes&, std::vector<std::string>&,
                           std::string* why) -> std::string {
            if (!addRecipient(k, {}, {}, why)) return {};
            const auto* r = doc_->project.recipientByName(k);
            return r ? r->name : k;
        };
        tg.keysFromAnchor = paste::keysFrom(*table_, rq.anchorViewRow, 0);
        return tg;
    };
    paste::bind(paste_);
    links_ += doc_->changed->connect([this](Id) { refresh(); paste::forget(paste_); });
    refresh();
}

void HmiNotifyPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiNotifyPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiNotifyPane::selectedRecipient() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= order_.size() ? std::string{} : order_[rows.front()];
}

void HmiNotifyPane::selectRecipient(const std::string& name) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], name)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildProperties();
}

void HmiNotifyPane::refresh() {
    refreshing_ = true;
    const std::string keep = selectedRecipient();
    const auto& n = doc_->project.notify;
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    for (const auto& r : n.recipients) {
        order_.push_back(r.name);
        const std::string duty = r.duty ? daysText(r.dutyDays) + " " + r.dutyFrom + " \xE2\x86\x92 " + r.dutyTo : std::string("toujours");
        rows.push_back({r.name, r.email.empty() ? std::string("\xE2\x80\x94") : r.email, r.phone.empty() ? std::string("\xE2\x80\x94") : r.phone,
                        priorityText(r.maxPriority), r.groups.empty() ? std::string("tous") : r.groups, duty, r.onClear ? "oui" : "non",
                        r.enabled ? "oui" : "non"});
        tones.push_back(!r.enabled ? 0 : (r.email.empty() && r.phone.empty()) ? 2 : 1);
    }
    tableModel_ = std::make_shared<Rows>(std::vector<std::string>{"Destinataire", "Courriel", "T\xC3\xA9l\xC3\xA9phone", "Priorit\xC3\xA9s", "Groupes",
                                                                  "Astreinte", "Disparition", "Actif"},
                                         std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle st;
                                             if (r >= tones.size()) return st;
                                             if (c == 0) {
                                                 st.bold = true;
                                                 st.icon = ui::Icon::User;
                                             }
                                             if (tones[r] == 0) st.fgTone = ui::Tone::None;
                                             if (tones[r] == 2 && c == 0) st.fgTone = ui::Tone::Warning;
                                             return st;
                                         });
    table_->setModel(tableModel_);
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (same(order_[i], keep)) table_->selectModelRows({static_cast<ui::RowIndex>(i)});
    tabs_->setTabBadge(0, std::to_string(order_.size()), n.enabled ? ui::Tone::Accent : ui::Tone::None);
    refreshing_ = false;
    shownSent_ = shownBox_ = static_cast<std::size_t>(-1);
    refreshLive();
    rebuildProperties();
}

void HmiNotifyPane::refreshLive() {
    auto* host = hosts_.notify ? hosts_.notify() : nullptr;
    // Les envois, le plus recent en haut.
    if (host && host->log().size() != shownSent_) {
        shownSent_ = host->log().size();
        std::vector<std::vector<std::string>> rows;
        std::vector<int> tones;
        const auto& log = host->log();
        for (auto it = log.rbegin(); it != log.rend(); ++it) {
            const auto& s = *it;
            rows.push_back({s.at.size() >= 19 ? s.at.substr(11, 8) : s.at, s.channel, s.recipient, s.address, s.alarm.empty() ? std::string("\xE2\x80\x94") : s.alarm,
                            s.kind, s.ok ? "envoy\xC3\xA9" : s.skipped ? "non envoy\xC3\xA9" : "\xC3\xA9" "chec", s.result});
            tones.push_back(s.ok ? 1 : s.skipped ? 2 : 3);
        }
        sentModel_ = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Canal", "Destinataire", "Adresse", "Alarme", "\xC3\x89v\xC3\xA9nement",
                                                                     "\xC3\x89tat", "R\xC3\xA9sultat"},
                                            std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                                ui::CellStyle st;
                                                if (r >= tones.size() || c != 6) return st;
                                                st.bold = true;
                                                st.fgTone = tones[r] == 1 ? ui::Tone::Ok : tones[r] == 2 ? ui::Tone::Warning : ui::Tone::Error;
                                                return st;
                                            });
        sent_->setModel(sentModel_);
        std::size_t failed = 0;
        for (const auto& s : log) failed += !s.ok && !s.skipped;
        tabs_->setTabBadge(1, std::to_string(log.size()), failed ? ui::Tone::Error : ui::Tone::Accent);
    }
    // La boite d'essai.
    const auto* testBox = host ? host->testBox() : nullptr;
    const std::size_t boxCount = testBox ? testBox->count() + 1 : 0;
    if (boxCount != shownBox_) {
        shownBox_ = boxCount;
        std::vector<std::vector<std::string>> rows;
        if (testBox) {
            const auto list = testBox->received();
            for (auto it = list.rbegin(); it != list.rend(); ++it)
                rows.push_back({it->at, it->channel, it->from, it->to, firstLine(it->subject), firstLine(it->body),
                                it->attachment.empty() ? std::string("\xE2\x80\x94") : it->attachment});
        }
        const std::size_t count = rows.size();
        boxModel_ = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Canal", "De", "\xC3\x80", "Sujet", "Texte", "Pi\xC3\xA8" "ce jointe"},
                                           std::move(rows), [](ui::RowIndex, std::size_t c) {
                                               ui::CellStyle st;
                                               if (c == 4) st.bold = true;
                                               return st;
                                           });
        box_->setModel(boxModel_);
        tabs_->setTabBadge(2, testBox ? std::to_string(count) : std::string("ferm\xC3\xA9" "e"), testBox ? ui::Tone::Accent : ui::Tone::None);
    }
}

bool HmiNotifyPane::change(const std::string& label, const std::function<void(hmi::Notifications&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { fn(p.notify); });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiNotifyPane::setSetting(const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string v = trimmed(raw);
    const auto number = [&](long long lo, long long hi, long long& out) {
        char* end = nullptr;
        const long long x = std::strtoll(v.c_str(), &end, 10);
        if (v.empty() || (end && *end) || x < lo || x > hi) return false;
        out = x;
        return true;
    };
    long long x = 0;
    if (key == "actives" || key == "boite") {
        const bool on = yes(v);
        return change(key == "actives" ? (on ? "Notifications actives" : "Notifications arr\xC3\xAAt\xC3\xA9" "es") : "Bo\xC3\xAEte d'essai",
                      [&](hmi::Notifications& n) { (key == "actives" ? n.enabled : n.testBox) = on; });
    }
    if (key == "smtp") {
        if (v.find(' ') != std::string::npos) return fail("relais \xC2\xAB " + v + " \xC2\xBB : une adresse IP ou un nom, sans espace");
        return change("Relais SMTP", [&](hmi::Notifications& n) { n.smtpHost = v; });
    }
    if (key == "expediteur") {
        if (!validEmail(v)) return fail("exp\xC3\xA9" "diteur \xC2\xAB " + v + " \xC2\xBB : une adresse (ihm@usine.local)");
        return change("Exp\xC3\xA9" "diteur", [&](hmi::Notifications& n) { n.smtpFrom = v; });
    }
    if (key == "utilisateur") return change("Utilisateur SMTP", [&](hmi::Notifications& n) { n.smtpUser = v; });
    if (key == "sms_url") {
        const std::string l = lower(v);
        if (l.rfind("https://", 0) == 0) return fail("https : pas pris en charge (une passerelle en http, sur le r\xC3\xA9seau local)");
        if (!v.empty() && l.rfind("http://", 0) != 0) return fail("l'URL de la passerelle commence par http://");
        return change("Passerelle SMS", [&](hmi::Notifications& n) { n.smsUrl = v; });
    }
    if (key == "sms_corps" || key == "sujet" || key == "corps" || key == "sms") {
        const std::string text = unescapeLines(raw);
        return change(key == "sujet" ? "Sujet des courriels" : key == "corps" ? "Corps des courriels" : key == "sms" ? "Texte des SMS" : "Corps de la requ\xC3\xAAte SMS",
                      [&](hmi::Notifications& n) {
                          if (key == "sms_corps") n.smsBody = text;
                          else if (key == "sujet") n.subject = text;
                          else if (key == "corps") n.body = text;
                          else n.sms = text;
                      });
    }
    struct Num {
        const char* key;
        long long lo, hi;
        int hmi::Notifications::*field;
        const char* label;
    };
    static const Num kNums[] = {
        {"smtp_port", 1, 65535, &hmi::Notifications::smtpPort, "Port du relais"},
        {"delai", 1, 120, &hmi::Notifications::timeoutS, "D\xC3\xA9lai de r\xC3\xA9ponse"},
        {"attente", 0, 86400, &hmi::Notifications::delayS, "Attendre avant d'envoyer"},
        {"repetition", 0, 86400, &hmi::Notifications::repeatS, "Une fois en"},
        {"max_heure", 1, 10000, &hmi::Notifications::maxPerHour, "Au plus par heure"},
        {"boite_port", 1, 65534, &hmi::Notifications::testPort, "Port de la bo\xC3\xAEte d'essai"},
    };
    for (const auto& k : kNums)
        if (key == k.key) {
            if (!number(k.lo, k.hi, x)) return fail(std::string(k.label) + " : un nombre de " + std::to_string(k.lo) + " \xC3\xA0 " + std::to_string(k.hi));
            return change(k.label, [&](hmi::Notifications& n) { n.*(k.field) = static_cast<int>(x); });
        }
    return fail("r\xC3\xA9glage inconnu : " + key);
}

bool HmiNotifyPane::addRecipient(const std::string& rawName, const std::string& email, const std::string& phone, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& n = doc_->project.notify;
    std::string name = trimmed(rawName);
    if (name.empty()) return fail("un destinataire a un nom");
    const auto taken = [&](const std::string& x) {
        return std::any_of(n.recipients.begin(), n.recipients.end(), [&](const hmi::NotifyRecipient& r) { return same(r.name, x); });
    };
    if (taken(name)) {
        if (rawName != "Nouveau_destinataire") return fail(name + " existe d\xC3\xA9j\xC3\xA0");
        for (int k = 2; taken(name); ++k) name = "Nouveau_destinataire_" + std::to_string(k);
    }
    hmi::NotifyRecipient r;
    r.name = name;
    if (!trimmed(email).empty()) {
        if (!validEmail(trimmed(email))) return fail("courriel \xC2\xAB " + email + " \xC2\xBB illisible");
        r.email = trimmed(email);
    }
    if (!trimmed(phone).empty()) {
        bool ok = false;
        r.phone = cleanPhone(phone, ok);
        if (!ok) return fail("t\xC3\xA9l\xC3\xA9phone \xC2\xAB " + phone + " \xC2\xBB illisible (+33612345678)");
    }
    if (!change("Ajouter le destinataire " + name, [&](hmi::Notifications& x) { x.recipients.push_back(r); })) return false;
    selectRecipient(name);
    say(name + " ajout\xC3\xA9 : ses priorit\xC3\xA9s, ses groupes, son astreinte \xC3\xA0 droite.");
    return true;
}

bool HmiNotifyPane::removeRecipient(const std::string& name, std::string* why) {
    const auto& n = doc_->project.notify;
    if (std::none_of(n.recipients.begin(), n.recipients.end(), [&](const hmi::NotifyRecipient& r) { return same(r.name, name); })) {
        const std::string m = "pas de destinataire " + name;
        say(m, true);
        if (why) *why = m;
        return false;
    }
    if (!change("Retirer le destinataire " + name,
                [&](hmi::Notifications& x) { std::erase_if(x.recipients, [&](const hmi::NotifyRecipient& r) { return same(r.name, name); }); }))
        return false;
    say(name + " retir\xC3\xA9 (Ctrl+Z le rend).");
    return true;
}

bool HmiNotifyPane::setRecipientField(const std::string& name, const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& n = doc_->project.notify;
    const auto it = std::find_if(n.recipients.begin(), n.recipients.end(), [&](const hmi::NotifyRecipient& r) { return same(r.name, name); });
    if (it == n.recipients.end()) return fail("pas de destinataire " + name);
    hmi::NotifyRecipient next = *it;
    const std::string v = trimmed(raw);
    if (key == "nom") {
        if (v.empty()) return fail("un destinataire a un nom");
        if (!same(v, it->name) && std::any_of(n.recipients.begin(), n.recipients.end(), [&](const hmi::NotifyRecipient& r) { return same(r.name, v); }))
            return fail(v + " existe d\xC3\xA9j\xC3\xA0");
        next.name = v;
    } else if (key == "courriel") {
        if (!v.empty() && !validEmail(v)) return fail("courriel \xC2\xAB " + v + " \xC2\xBB illisible");
        next.email = v;
    } else if (key == "telephone") {
        if (!v.empty()) {
            bool ok = false;
            next.phone = cleanPhone(v, ok);
            if (!ok) return fail("t\xC3\xA9l\xC3\xA9phone \xC2\xAB " + v + " \xC2\xBB illisible (+33612345678)");
        } else {
            next.phone.clear();
        }
    } else if (key == "priorite") {
        if (!parsePriority(v, next.maxPriority)) return fail("priorit\xC3\xA9s : Critique seulement, Critique et haute, Jusqu'\xC3\xA0 moyenne, Toutes");
    } else if (key == "groupes") {
        next.groups = joinList(splitList(v));
    } else if (key == "astreinte" || key == "disparition" || key == "actif") {
        const bool on = yes(v);
        (key == "astreinte" ? next.duty : key == "disparition" ? next.onClear : next.enabled) = on;
    } else if (key == "jours") {
        if (!parseDays(v, next.dutyDays)) return fail("jours : des chiffres de 1 (lundi) \xC3\xA0 7 (dimanche) : 12345, 67 ; ou tous, semaine, week-end");
    } else if (key == "de" || key == "a") {
        std::string clock;
        if (!parseClock(v, clock)) return fail("heure \xC2\xAB " + v + " \xC2\xBB : 18:00, 08:00");
        (key == "de" ? next.dutyFrom : next.dutyTo) = clock;
    } else {
        return fail("champ inconnu : " + key);
    }
    if (next == *it) return true;
    const std::string label = next.name;
    // ---- Lot API 8 : renommer partout (IHM) : un destinataire renomme - les rapports
    // qui l'envoient (Report::recipients, leurs noms) suivent, dans la meme commande ----
    std::size_t rapportsSuivis = 0;
    auto cmd = hmi::changeProject(doc_, "Destinataire " + it->name, [&](hmi::Project& p) {
        for (auto& r : p.notify.recipients)
            if (same(r.name, name)) r = next;
        if (key != "nom") return;
        for (auto& rp : p.reports) {
            std::string out;
            std::size_t from = 0;
            bool changed = false;
            while (from <= rp.recipients.size()) {
                const auto at = rp.recipients.find_first_of(";,", from);
                const std::string part = rp.recipients.substr(from, at == std::string::npos ? std::string::npos : at - from);
                if (!trimmed(part).empty() && same(trimmed(part), name)) {
                    const auto lead = part.find_first_not_of(' ');
                    out += part.substr(0, lead == std::string::npos ? 0 : lead) + next.name;
                    changed = true;
                } else {
                    out += part;
                }
                if (at == std::string::npos) break;
                out += rp.recipients[at];
                from = at + 1;
            }
            if (changed) {
                rp.recipients = out;
                ++rapportsSuivis;
            }
        }
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    if (key == "nom") selectRecipient(label);
    if (rapportsSuivis) say("Renomm\xC3\xA9 en " + label + " : " + std::to_string(rapportsSuivis) + " rapport(s) suivent");
    return true;
}

bool HmiNotifyPane::setSmtpPassword(const std::string& password) {
    const std::string masked = password.empty() ? std::string{} : hmi::notify::maskSecret(password);
    if (!change(password.empty() ? "Relais SMTP sans mot de passe" : "Mot de passe du relais SMTP", [&](hmi::Notifications& n) { n.smtpPassword = masked; }))
        return false;
    say(password.empty() ? std::string("Mot de passe du relais retir\xC3\xA9.") : std::string("Mot de passe du relais gard\xC3\xA9 masqu\xC3\xA9 dans le projet."));
    return true;
}

bool HmiNotifyPane::useTestBox() {
    const int port = doc_->project.notify.testPort;
    if (!change("Notifications : la bo\xC3\xAEte d'essai", [&](hmi::Notifications& n) {
            n.enabled = true;
            n.testBox = true;
            n.smtpHost = "127.0.0.1";
            n.smtpPort = port;
            n.smtpUser.clear();
            n.smtpPassword.clear();
            n.smsUrl = "http://127.0.0.1:" + std::to_string(port + 1) + "/sms?to={numero}&text={message}";
            n.smsBody.clear();
        }))
        return false;
    if (auto* host = hosts_.notify ? hosts_.notify() : nullptr) host->tick(&doc_->project);
    tabs_->setCurrentIndex(2);
    refreshLive();
    say("Bo\xC3\xAEte d'essai : relais 127.0.0.1:" + std::to_string(port) + ", passerelle http://127.0.0.1:" + std::to_string(port + 1)
        + " - les notifications arrivent dans l'onglet Bo\xC3\xAEte d'essai.");
    return true;
}

bool HmiNotifyPane::test(bool sms, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const auto& n = doc_->project.notify;
    const std::string name = selectedRecipient().empty() && !n.recipients.empty() ? n.recipients.front().name : selectedRecipient();
    const auto it = std::find_if(n.recipients.begin(), n.recipients.end(), [&](const hmi::NotifyRecipient& r) { return same(r.name, name); });
    if (it == n.recipients.end()) return fail("choisissez un destinataire");
    if (sms && it->phone.empty()) return fail(it->name + " n'a pas de t\xC3\xA9l\xC3\xA9phone");
    if (!sms && it->email.empty()) return fail(it->name + " n'a pas de courriel");
    if (sms && trimmed(n.smsUrl).empty()) return fail("aucune passerelle SMS (URL) : r\xC3\xA9glage \xC2\xAB Passerelle (URL) \xC2\xBB");
    if (!sms && trimmed(n.smtpHost).empty()) return fail("aucun relais SMTP : r\xC3\xA9glage \xC2\xAB Relais \xC2\xBB");
    auto* host = hosts_.notify ? hosts_.notify() : nullptr;
    if (!host) return fail("les notifications ne sont pas disponibles ici");
    host->test(&doc_->project, *it, sms);
    tabs_->setCurrentIndex(1);
    say(std::string(sms ? "SMS" : "Courriel") + " d'essai parti vers " + it->name + " (" + (sms ? it->phone : it->email) + ") : l'onglet Envois dit la suite.");
    return true;
}

void HmiNotifyPane::rebuildProperties() {
    const auto& n = doc_->project.notify;
    const auto commit = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setSetting(key, std::string(v));
        };
    };
    std::vector<PG::Category> cats;
    PG::Category gen;
    gen.name = "Notifications";
    gen.properties.push_back(prop("Actives", tf(n.enabled), PG::ValueType::Boolean, commit("actives"),
                                  "Les alarmes pr\xC3\xA9viennent les destinataires (en simulation, sur le poste d'exploitation)."));
    gen.properties.push_back(prop("Attendre (s)", std::to_string(n.delayS), PG::ValueType::Integer, commit("attente"),
                                  "Acquitt\xC3\xA9" "e ou disparue avant N secondes : rien ne part (un d\xC3\xA9" "faut fugitif ne r\xC3\xA9veille personne). 0 : tout de suite."));
    gen.properties.push_back(prop("Une fois en (s)", std::to_string(n.repeatS), PG::ValueType::Integer, commit("repetition"),
                                  "La m\xC3\xAAme alarme au m\xC3\xAAme destinataire : une fois en N secondes au plus."));
    gen.properties.push_back(prop("Au plus par heure", std::to_string(n.maxPerHour), PG::ValueType::Integer, commit("max_heure"),
                                  "Au-del\xC3\xA0 (une avalanche d'alarmes), plus rien ne part dans l'heure ; l'onglet Envois le dit."));
    cats.push_back(std::move(gen));
    PG::Category mail;
    mail.name = "Courriel (SMTP)";
    mail.properties.push_back(prop("Relais", n.smtpHost, PG::ValueType::Text, commit("smtp"),
                                   "Le relais de l'usine (smtp.usine.local, 192.168.1.20), sans TLS ; vide : pas de courriel."));
    mail.properties.push_back(prop("Port", std::to_string(n.smtpPort), PG::ValueType::Integer, commit("smtp_port"), "25 ; 587 si le relais le demande (sans TLS)."));
    mail.properties.push_back(prop("Exp\xC3\xA9" "diteur", n.smtpFrom, PG::ValueType::Text, commit("expediteur"), "L'adresse qui envoie : ihm@usine.local."));
    mail.properties.push_back(prop("Utilisateur", n.smtpUser, PG::ValueType::Text, commit("utilisateur"),
                                   "Vide : le relais accepte sans authentification (le cas d'un relais interne). Sinon AUTH LOGIN."));
    mail.properties.push_back(prop("Mot de passe", n.smtpPassword.empty() ? std::string("aucun") : std::string("d\xC3\xA9" "fini (masqu\xC3\xA9)"),
                                   PG::ValueType::ReadOnly));
    mail.properties.push_back(prop("D\xC3\xA9lai de r\xC3\xA9ponse (s)", std::to_string(n.timeoutS), PG::ValueType::Integer, commit("delai"),
                                   "Une connexion, une r\xC3\xA9ponse du relais ou de la passerelle."));
    cats.push_back(std::move(mail));
    PG::Category sms;
    sms.name = "SMS (passerelle HTTP)";
    sms.properties.push_back(prop("Passerelle (URL)", n.smsUrl, PG::ValueType::Text, commit("sms_url"),
                                  "http://192.168.1.50/sms?to={numero}&text={message} : {numero} et {message} remplac\xC3\xA9s (encod\xC3\xA9s). Vide : pas de SMS."));
    sms.properties.push_back(prop("Corps (POST)", escapeLines(n.smsBody), PG::ValueType::Text, commit("sms_corps"),
                                  "Vide : une requ\xC3\xAAte GET. Sinon un POST de ce corps (to={numero}&text={message})."));
    cats.push_back(std::move(sms));
    PG::Category msg;
    msg.name = "Messages";
    msg.properties.push_back(prop("Sujet", n.subject, PG::ValueType::Text, commit("sujet"),
                                  "{projet} {alarme} {message} {priorite} {groupe} {categorie} {etat} {heure} {date} {consigne} {utilisateur}"));
    msg.properties.push_back(prop("Corps", escapeLines(n.body), PG::ValueType::Text, commit("corps"),
                                  "Vide : le texte par d\xC3\xA9" "faut (l'alarme, sa priorit\xC3\xA9, son message, l'heure, la consigne). \\n : une ligne."));
    msg.properties.push_back(prop("SMS", n.sms, PG::ValueType::Text, commit("sms"), "Le texte d'un SMS (300 caract\xC3\xA8res au plus)."));
    cats.push_back(std::move(msg));
    PG::Category box;
    box.name = "Bo\xC3\xAEte d'essai";
    box.properties.push_back(prop("Ouvrir la bo\xC3\xAEte", tf(n.testBox), PG::ValueType::Boolean, commit("boite"),
                                  "Un serveur SMTP (et HTTP pour les SMS) sur ce poste, qui garde ce qu'il re\xC3\xA7oit."));
    box.properties.push_back(prop("Port SMTP", std::to_string(n.testPort), PG::ValueType::Integer, commit("boite_port"),
                                  "2525 ; les SMS : le suivant (2526)."));
    {
        auto* host = hosts_.notify ? hosts_.notify() : nullptr;
        const auto* tb = host ? host->testBox() : nullptr;
        shownBoxState_ = tb ? "ouverte : SMTP 127.0.0.1:" + std::to_string(tb->smtpPort()) + ", SMS http://127.0.0.1:" + std::to_string(tb->httpPort())
                            : host && !host->testBoxError().empty() ? "impossible : " + host->testBoxError() : std::string("ferm\xC3\xA9" "e");
        box.properties.push_back(prop("\xC3\x89tat", shownBoxState_, PG::ValueType::ReadOnly));
    }
    cats.push_back(std::move(box));

    const std::string chosen = selectedRecipient();
    const auto it = std::find_if(n.recipients.begin(), n.recipients.end(), [&](const hmi::NotifyRecipient& r) { return same(r.name, chosen); });
    if (it != n.recipients.end()) {
        const std::string key = it->name;
        const auto field = [this, key](const char* f) {
            return [this, key, f](std::string_view v) {
                message_.clear();
                return setRecipientField(key, f, std::string(v));
            };
        };
        PG::Category r;
        r.name = "Destinataire choisi";
        r.properties.push_back(prop("Nom", it->name, PG::ValueType::Text, field("nom")));
        r.properties.push_back(prop("Courriel", it->email, PG::ValueType::Text, field("courriel"), "Vide : pas de courriel."));
        r.properties.push_back(prop("T\xC3\xA9l\xC3\xA9phone", it->phone, PG::ValueType::Text, field("telephone"), "+33612345678 ; vide : pas de SMS."));
        r.properties.push_back(prop("Priorit\xC3\xA9s", priorityText(it->maxPriority), PG::ValueType::Enum, field("priorite"),
                                    "Les alarmes qu'il re\xC3\xA7oit, de la plus grave \xC3\xA0 celle-ci.",
                                    {kPriorityChoices[0], kPriorityChoices[1], kPriorityChoices[2], kPriorityChoices[3]}));
        r.properties.push_back(prop("Groupes", it->groups, PG::ValueType::Text, field("groupes"),
                                    "Armoire A; Armoire B : les alarmes de ces groupes seulement ; vide : tous."));
        r.properties.push_back(prop("Astreinte", tf(it->duty), PG::ValueType::Boolean, field("astreinte"),
                                    "Coch\xC3\xA9 : seulement les jours et heures dits (18:00 \xE2\x86\x92 08:00 passe minuit : le matin est \xC3\xA0 l'astreinte de la veille)."));
        if (it->duty) {
            r.properties.push_back(prop("Jours", it->dutyDays, PG::ValueType::Text, field("jours"), "1 lundi ... 7 dimanche : 12345 ; tous, semaine, week-end."));
            r.properties.push_back(prop("De", it->dutyFrom, PG::ValueType::Text, field("de"), "18:00"));
            r.properties.push_back(prop("\xC3\x80", it->dutyTo, PG::ValueType::Text, field("a"), "08:00"));
        }
        r.properties.push_back(prop("\xC3\x80 la disparition aussi", tf(it->onClear), PG::ValueType::Boolean, field("disparition"),
                                    "Un second message quand l'alarme dispara\xC3\xAEt."));
        r.properties.push_back(prop("Actif", tf(it->enabled), PG::ValueType::Boolean, field("actif"), "D\xC3\xA9" "coch\xC3\xA9 : il ne re\xC3\xA7oit plus rien (des cong\xC3\xA9s)."));
        cats.push_back(std::move(r));
    }
    grid_->setCategories(std::move(cats));
}

void HmiNotifyPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(480.f, b.w * 0.34f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiNotifyPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    if (ctx.time - lastLive_ >= 1.0) {
        lastLive_ = ctx.time;
        refreshLive();
        auto* host = hosts_.notify ? hosts_.notify() : nullptr;
        const auto* tb = host ? host->testBox() : nullptr;
        const std::string state = tb ? "ouverte : SMTP 127.0.0.1:" + std::to_string(tb->smtpPort()) + ", SMS http://127.0.0.1:" + std::to_string(tb->httpPort())
                                     : host && !host->testBoxError().empty() ? "impossible : " + host->testBoxError() : std::string("ferm\xC3\xA9" "e");
        if (state != shownBoxState_) rebuildProperties();
    }
}

} // namespace app
