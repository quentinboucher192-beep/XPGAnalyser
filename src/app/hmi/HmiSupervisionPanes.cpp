#include "HmiSupervisionPanes.hpp"
#include "../../core/Edition.hpp"   // 1.12.2 : XPGAnalyser IHM n'a pas d'automate
#include "HmiAssist.hpp"

#include "HmiPaneKit.hpp"
#include "HmiObjectAlarmPanes.hpp"    // 1.9 : les alarmes generees par les objets

#include "../../hmi/HmiCrypto.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiRecipes.hpp"
#include "../../hmi/HmiRenameRefs.hpp"    // lot API 8 : finitions (renommer une alarme)
#include "../../hmi/HmiAlarmGroupCommands.hpp"   // 1.10.2 (AL) : les groupes d'alarmes et leurs liens
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;
using namespace hmikit;

namespace {

ui::Tone priorityTone(int p) {
    switch (p) {
        case 1: return ui::Tone::Error;
        case 2: return ui::Tone::Warning;
        case 3: return ui::Tone::Accent;
        default: return ui::Tone::Muted;
    }
}

std::string priorityChoice(int p) { return std::to_string(p) + " - " + std::string(hmi::alarmPriorityLabel(p)); }

std::vector<std::string> priorityChoices() {
    std::vector<std::string> out;
    for (int p = 1; p <= hmi::kAlarmPriorities; ++p) out.push_back(priorityChoice(p));
    return out;
}

} // namespace

// =================================================================== alarmes ==
namespace {
enum AlarmAction : int { ANew = 1, ADup, ADel, AUp, ADown };
}

HmiAlarmsPane::HmiAlarmsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ANew, HmiGlyph::Plus, "Nouvelle alarme", "Nouvelle");
    tools->add(ADup, HmiGlyph::Duplicate, "Dupliquer l'alarme", "Dupliquer");
    tools->add(ADel, HmiGlyph::Delete, "Supprimer l'alarme (Ctrl+Z la rend)", "Supprimer");
    tools->separator();
    tools->add(AUp, HmiGlyph::Up, "Monter l'alarme dans la liste");
    tools->add(ADown, HmiGlyph::Down, "Descendre l'alarme dans la liste");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    for (int a : {ADup, ADel, AUp, ADown}) tools_->setEnabledWhen(a, [this] { return selectedAlarm() != kNoId; });

    auto prio = std::make_unique<ui::DropDown>(base + ".priority");
    std::vector<ui::DropDown::Item> items{{"Toutes les priorit\xC3\xA9s", "", {}, true}};
    for (int p = 1; p <= hmi::kAlarmPriorities; ++p) items.push_back({priorityChoice(p), "", {}, true});
    prio->setItems(std::move(items));
    prio->setSelectedIndex(0);
    priority_ = &static_cast<ui::DropDown&>(addChild(std::move(prio)));
    auto search = std::make_unique<ui::InputText>(base + ".search");
    search->setPlaceholder("Rechercher (nom, groupe, condition, message)...");
    search_ = &static_cast<ui::InputText&>(addChild(std::move(search)));

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Alarme", 214.f}, {"Priorit\xC3\xA9", 110.f}, {"Cat\xC3\xA9gorie", 110.f}, {"Groupe", 110.f},
                       {"Condition", 230.f}, {"Message", 300.f}, {"Acquittement", 104.f}, {"D\xC3\xA9lai", 70.f, 50.f, true, true, true, ui::Align::End}});
    table->setSelectionMode(ui::SelectionMode::Extended);     // lot 20 : plusieurs lignes a copier
    {
        // 1.9 : sous les alarmes du projet, celles generees par les objets (Symboles,
        // puis Objets du synoptique) - HmiObjectAlarmPanes.hpp.
        auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".left");
        table_ = &static_cast<ui::TableView&>(left->addPane(std::move(table), 0.55f, 120.f));
        generated_ = &static_cast<HmiGeneratedAlarmsTable&>(
            left->addPane(std::make_unique<HmiGeneratedAlarmsTable>(base + ".generated", doc_, apply_), 0.45f, 120.f));
        split->addPane(std::move(left), 0.64f, 320.f);
    }
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
    grid_ = &static_cast<ui::PropertyGrid&>(split->addPane(std::move(grid), 0.36f, 260.f));
    // Lot 20 : coller des alarmes depuis Excel - le nom dit laquelle ; chaque
    // case passe par setField (et ses controles). Un seul Ctrl+Z.
    paste_.table = table_;
    paste_.keyColumn = 0;
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
    };
    paste_.target = [this](const ui::TableView::PasteRequest& rq) {
        paste::Target tg;
        tg.noun = "alarme";
        tg.nouns = "alarmes";
        const auto idOf = [this](const std::string& k) -> Id {
            const auto* a = doc_->project.alarmByName(k);
            return a ? a->id : kNoId;
        };
        const auto field = [this, idOf](std::string name) {
            return [this, idOf, name](const std::string& k, const std::string& v, std::string* why) {
                return setField(idOf(k), name, v, why);
            };
        };
        tg.columns.push_back(paste::column("Alarme", {"Nom", "Name", "Alarm", "Identifiant"}, 0, nullptr, true));
        tg.columns.push_back(paste::column("Priorit\xC3\xA9", {"Priority", "Prio"}, 1,
            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                // "2 - Haute", "2", ou le mot seul (Critique, Haute, Moyenne, Basse).
                std::string value = v;
                const std::string n = paste::normalizedTitle(v);
                for (int p = 1; p <= hmi::kAlarmPriorities; ++p)
                    if (n == paste::normalizedTitle(hmi::alarmPriorityLabel(p))) value = std::to_string(p);
                return setField(idOf(k), "priorite", value, why);
            }));
        tg.columns.push_back(paste::column("Cat\xC3\xA9gorie", {"Category", "Classe"}, 2, field("categorie")));
        tg.columns.push_back(paste::column("Groupe", {"Group", "Zone"}, 3, field("groupe")));
        tg.columns.push_back(paste::column("Condition", {"Expression", "Seuil"}, 4, field("condition")));
        tg.columns.push_back(paste::column("Message", {"Texte", "Libelle", "Text"}, 5, field("message")));
        tg.columns.push_back(paste::column("Acquittement", {"Acquit", "Ack", "A acquitter"}, 6,
            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                const std::string n = paste::normalizedTitle(v);
                const bool automatic = n == "automatique" || n == "auto" || n == "non" || n == "false" || n == "0" || n == "no";
                return setField(idOf(k), "acquittement", automatic ? "FALSE" : "TRUE", why);
            }));
        tg.columns.push_back(paste::column("D\xC3\xA9lai", {"Delai (ms)", "Temporisation", "Delay", "Tempo"}, 7,
            [this, idOf](const std::string& k, const std::string& v, std::string* why) {
                std::string value = v;
                if (value == "-" || value == "\xE2\x80\x94") value = "0";
                if (value.size() > 2 && value.compare(value.size() - 2, 2, "ms") == 0) value.resize(value.size() - 2);
                while (!value.empty() && value.back() == ' ') value.pop_back();
                return setField(idOf(k), "delai", value, why);
            }));
        tg.columns.push_back(paste::column("Description", {"Commentaire", "Comment"}, -1, field("description")));
        tg.columns.push_back(paste::column("Consigne", {"Instruction", "Action operateur"}, -1, field("consigne")));
        tg.exists = [this](const std::string& k) { return doc_->project.alarmByName(k) != nullptr; };
        tg.freeKey = [this](const std::string& k) { return hmi::isIdentifier(k) ? hmi::uniqueAlarmName(doc_->project, k) : k; };
        tg.create = [this](const std::string& k, const std::map<std::string, std::string>&, paste::Notes&, std::vector<std::string>&,
                           std::string* why) -> std::string {
            const Id made = addAlarm(k, why);
            if (made == kNoId) return {};
            const auto* a = doc_->project.alarm(made);
            return a ? a->name : k;
        };
        tg.keysFromAnchor = paste::keysFrom(*table_, rq.anchorViewRow, 0);
        return tg;
    };
    paste::bind(paste_);
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedAlarm();
        switch (a) {
            case ANew: (void)addAlarm(); break;
            case ADup: if (sel) (void)duplicateAlarm(sel); break;
            case ADel:
                if (!sel) break;
                if (hosts_.remove) hosts_.remove(sel);
                else (void)deleteAlarm(sel);
                break;
            case AUp: if (sel) (void)moveAlarm(sel, -1); break;
            case ADown: if (sel) (void)moveAlarm(sel, +1); break;
            default: break;
        }
    });
    links_ += priority_->selectionChanged->connect([this](int i) { priorityFilter_ = i; refresh(); });
    links_ += search_->textChanged->connect([this](const std::string& t) { searchText_ = t; refresh(); });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { rebuildProperties(); });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    // 1.11 (R111, recette T2-16) : un changement du document (Ctrl+Z, Ctrl+Y, une
    // autre page) rend caduc ce que la page vient de dire (« Lier V_101 a
    // Armoire A ») : le message durable (le compte des alarmes) revient tout de
    // suite. Ce que la page fait elle-meme, elle le dit apres (say suit apply_).
    links_ += doc_->changed->connect([this](Id) {
        message_.clear();
        status_->dismissTransient();
        refresh();
        paste::forget(paste_);
    });
    refresh();
}

void HmiAlarmsPane::refresh() {
    const Id keep = selectedAlarm();
    const auto& p = doc_->project;
    std::vector<std::vector<std::string>> rows;
    std::vector<int> prios;
    order_.clear();
    // Lot recherche : la recherche de toutes les listes - chaque mot (ou "phrase")
    // dans le nom, le groupe, la condition, le message, la categorie, la
    // DESCRIPTION ou la consigne ; aucun -mot exclu ; sans casse ni accents.
    const ui::SearchQuery query(searchText_);
    int perPriority[hmi::kAlarmPriorities + 1] = {};
    std::set<std::string> groups;
    for (const auto& a : p.alarms) {
        if (a.priority >= 1 && a.priority <= hmi::kAlarmPriorities) ++perPriority[a.priority];
        if (!a.group.empty()) groups.insert(a.group);
        if (priorityFilter_ > 0 && a.priority != priorityFilter_) continue;
        if (!query.matches({a.name, a.group, a.condition, a.message, a.category, a.description, a.instruction})) continue;
        order_.push_back(a.id);
        prios.push_back(a.priority);
        rows.push_back({a.name, priorityChoice(a.priority), a.category, a.group, a.condition, a.message,
                        a.ackRequired ? "requis" : "automatique", a.delayMs > 0 ? std::to_string(a.delayMs) + " ms" : "-"});
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Alarme", "Priorit\xC3\xA9", "Cat\xC3\xA9gorie", "Groupe", "Condition",
                                                             "Message", "Acquittement", "D\xC3\xA9lai"},
                                    std::move(rows), [prios](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle s;
                                        if (r >= prios.size()) return s;
                                        if (c == 0) { s.icon = ui::Icon::Warning; s.iconTone = priorityTone(prios[r]); s.bold = prios[r] == 1; }
                                        if (c == 1) s.fgTone = priorityTone(prios[r]);
                                        if (c == 4) s.monospace = true;
                                        return s;
                                    });
    table_->setModel(model_);
    table_->setHighlight(searchText_);          // lot recherche : les mots trouves, surlignes
    if (keep) selectAlarm(keep);
    std::string msg = std::to_string(p.alarms.size()) + (p.alarms.size() == 1 ? " alarme" : " alarmes");   // 1.11 (R111, T3-11)
    for (int k = 1; k <= hmi::kAlarmPriorities; ++k)
        if (perPriority[k]) msg += " \xC2\xB7 " + std::to_string(perPriority[k]) + " " + lower(std::string(hmi::alarmPriorityLabel(k)))
                                   + (perPriority[k] > 1 ? "s" : "");   // 1.11 (R111, T3-11) : « 3 critiques », « 1 basse »
    if (!groups.empty()) msg += " \xC2\xB7 groupes : " + fewOf(std::vector<std::string>(groups.begin(), groups.end()), 5);
    if (order_.size() != p.alarms.size()) msg += " \xC2\xB7 " + std::to_string(order_.size()) + (order_.size() == 1 ? " montr\xC3\xA9" "e" : " montr\xC3\xA9" "es");
    status_->setMessage(msg);
    rebuildProperties();
    invalidate();
}

Id HmiAlarmsPane::selectedAlarm() const {
    const int r = selectedRow(*table_);
    return r >= 0 && static_cast<std::size_t>(r) < order_.size() ? order_[static_cast<std::size_t>(r)] : kNoId;
}

void HmiAlarmsPane::selectAlarm(Id id) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == id) { table_->selectModelRows({static_cast<ui::RowIndex>(i)}); return; }
}

void HmiAlarmsPane::setPriorityFilter(int index) {
    priority_->setSelectedIndex(index);
    priorityFilter_ = index;
    refresh();
}

void HmiAlarmsPane::setSearch(std::string text) {
    search_->setText(text);
    searchText_ = std::move(text);
    refresh();
}

void HmiAlarmsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

namespace {
// Lot 11 : les reglages communs a toutes les alarmes (les sons, la mise de cote).
PG::Category alarmSettingsCategory(const hmi::Project& p, const std::function<std::function<bool(std::string_view)>(const char*)>& commit) {
    PG::Category c;
    c.name = "Sons et mise de c\xC3\xB4t\xC3\xA9 (toutes les alarmes)";
    std::vector<std::string> sounds{""};
    for (const auto& r : p.assets.resources)
        if (r.kind() == hmi::MediaKind::Sound) sounds.push_back(r.name);
    static const char* kKeys[] = {"son1", "son2", "son3", "son4"};
    for (int k = 0; k < hmi::kAlarmPriorities; ++k) {
        const std::string current = p.alarmSettings.sounds[static_cast<std::size_t>(k)];
        auto choices = sounds;
        if (std::find(choices.begin(), choices.end(), current) == choices.end()) choices.push_back(current);
        c.properties.push_back(prop("Son : priorit\xC3\xA9 " + std::to_string(k + 1) + " (" + std::string(hmi::alarmPriorityLabel(k + 1)) + ")", current,
                                    PG::ValueType::Enum, commit(kKeys[k]),
                                    "Jou\xC3\xA9 quand une alarme de cette priorit\xC3\xA9 appara\xC3\xAEt (un son des ressources, WAV). "
                                    "Vide : aucun. Au volume des Param\xC3\xA8tres syst\xC3\xA8me ; \xC2\xAB Faire taire \xC2\xBB le coupe.",
                                    choices));
    }
    c.properties.push_back(prop("R\xC3\xA9p\xC3\xA9ter le son toutes les (s)", std::to_string(p.alarmSettings.repeatS), PG::ValueType::Integer,
                                commit("repetition"),
                                "Tant qu'une alarme attend son acquittement, le son de la plus grave revient. 0 : une seule fois."));
    c.properties.push_back(prop("Mise de c\xC3\xB4t\xC3\xA9 : au plus (min)", std::to_string(p.alarmSettings.maxShelveMin), PG::ValueType::Integer,
                                commit("mise_de_cote_max"),
                                "Une alarme mise de c\xC3\xB4t\xC3\xA9 (action \xC2\xAB Mettre de c\xC3\xB4t\xC3\xA9 une alarme \xC2\xBB) revient "
                                "d'elle-m\xC3\xAAme au plus tard apr\xC3\xA8s ce temps."));
    return c;
}

// 1.10.2 (AL) : IHM > Alarmes > Groupes - les groupes d'alarmes (declares, ou nommes
// par les alarmes : reglages par defaut), leurs reglages, les groupes d'objets lies et
// "Lier..." (plusieurs d'un coup). Chaque changement : une commande (Ctrl+Z).
PG::Category alarmGroupsCategory(const hmi::DocumentPtr& doc, const std::function<void(core::CommandPtr)>& apply,
                                 const std::function<void()>& after, const std::function<void(const std::string&)>& link) {
    using VT = PG::ValueType;
    using Commit = std::function<bool(std::string_view)>;
    const auto run = [apply, after](core::CommandPtr cmd) {
        if (!cmd) return false;
        apply(std::move(cmd));
        if (after) after();
        return true;
    };
    const auto clean = [](std::string_view v) {
        std::size_t a = 0, b = v.size();
        while (a < b && std::isspace(static_cast<unsigned char>(v[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
        return std::string(v.substr(a, b - a));
    };
    PG::Category out;
    out.name = "Groupes d'alarmes";
    out.properties.push_back(prop("Nouveau groupe", {}, VT::Text,
                                  Commit([doc, run, clean](std::string_view v) {
                                      const std::string name = clean(v);
                                      return !name.empty() && run(hmi::addAlarmGroupCmd(doc, name));
                                  }),
                                  "Le nom d'un nouveau groupe d'alarmes (une zone, une armoire). Ses r\xC3\xA9glages : par d\xC3\xA9" "faut ; "
                                  "puis lie-lui des groupes d'alarmes des objets (\xC2\xAB Lier\xE2\x80\xA6 \xC2\xBB)."));
    static const std::vector<std::string> kPriorities{"0 - celle de l'alarme", "1 - Critique", "2 - Haute", "3 - Moyenne", "4 - Basse"};
    const std::vector<std::string> acks{std::string(hmi::alarmAckModeLabel(hmi::AlarmAckMode::Single)),
                                        std::string(hmi::alarmAckModeLabel(hmi::AlarmAckMode::Group)),
                                        std::string(hmi::alarmAckModeLabel(hmi::AlarmAckMode::Auto))};
    for (const auto& g : hmi::alarmGroupsOf(doc->project)) {
        PG::Category c;
        c.name = g.name;
        c.expanded = false;
        const std::string name = g.name;
        // Un reglage : les reglages du groupe, changes par `change`, en une commande.
        const auto set = [doc, run, name](std::function<bool(hmi::AlarmGroupDef&, const std::string&)> change) {
            return Commit([doc, run, name, change](std::string_view v) {
                const hmi::AlarmGroupDef before = hmi::alarmGroupSettings(doc->project, name);
                hmi::AlarmGroupDef d = before;
                if (!change(d, std::string(v))) return false;
                return d == before || run(hmi::setAlarmGroupCmd(doc, name, d));
            });
        };
        c.properties.push_back(prop("Nom", g.name, VT::Text,
                                    Commit([doc, run, name, clean](std::string_view v) {
                                        const std::string to = clean(v);
                                        if (to == name) return true;
                                        std::string why;
                                        return !to.empty() && run(hmi::renameAlarmGroupCmd(doc, name, to, &why));
                                    }),
                                    "Renommer le groupe : ses alarmes, les liens des groupes des objets et les surcharges \xC2\xAB groupe \xC2\xBB suivent."));
        c.properties.push_back(prop("Priorit\xC3\xA9 par d\xC3\xA9" "faut", kPriorities[static_cast<std::size_t>(std::clamp(g.priority, 0, 4))], VT::Enum,
                                    set([](hmi::AlarmGroupDef& d, const std::string& v) {
                                        if (v.empty() || v[0] < '0' || v[0] > '4') return false;
                                        d.priority = v[0] - '0';
                                        return true;
                                    }),
                                    "La priorit\xC3\xA9 que prennent les alarmes des objets li\xC3\xA9s, et une nouvelle alarme mise dans ce groupe "
                                    "(0 : chaque alarme garde la sienne).", kPriorities));
        const auto color = [&](const char* label, std::string hmi::AlarmGroupDef::*field, const char* help) {
            c.properties.push_back(prop(label, g.*field, VT::Color,
                                        set([field](hmi::AlarmGroupDef& d, const std::string& v) {
                                            d.*field = v;
                                            return true;
                                        }),
                                        help));
        };
        color("Couleur : active", &hmi::AlarmGroupDef::colorActive, "La couleur d'une alarme active du groupe (liste, bandeau). Vide : celle de sa priorit\xC3\xA9.");
        color("Couleur : acquitt\xC3\xA9" "e", &hmi::AlarmGroupDef::colorAcked, "La couleur d'une alarme acquitt\xC3\xA9" "e encore pr\xC3\xA9sente. Vide : celle de toujours.");
        color("Couleur : disparue", &hmi::AlarmGroupDef::colorCleared, "La couleur d'une alarme disparue pas encore acquitt\xC3\xA9" "e. Vide : celle de toujours.");
        c.properties.push_back(prop("Acquittement", std::string(hmi::alarmAckModeLabel(g.ack)), VT::Enum,
                                    set([acks](hmi::AlarmGroupDef& d, const std::string& v) {
                                        for (std::size_t k = 0; k < acks.size(); ++k)
                                            if (acks[k] == v) {
                                                d.ack = k == 0 ? hmi::AlarmAckMode::Single : k == 1 ? hmi::AlarmAckMode::Group : hmi::AlarmAckMode::Auto;
                                                return true;
                                            }
                                        return false;
                                    }),
                                    "Un par un ; par groupe (acquitter une alarme acquitte tout le groupe) ; automatique au retour.", acks));
        c.properties.push_back(prop("Son", g.sound, VT::Text, set([](hmi::AlarmGroupDef& d, const std::string& v) {
                                        d.sound = v;
                                        return true;
                                    }),
                                    "Un son des ressources, jou\xC3\xA9 quand une alarme du groupe appara\xC3\xAEt. Vide : celui de sa priorit\xC3\xA9."));
        c.properties.push_back(prop("Zone", g.zone, VT::Text, set([](hmi::AlarmGroupDef& d, const std::string& v) {
                                        d.zone = v;
                                        return true;
                                    }),
                                    "La zone du r\xC3\xA9sum\xC3\xA9 par zone o\xC3\xB9 comptent les alarmes du groupe. Vide : le nom du groupe."));
        c.properties.push_back(prop("Niveau pour acquitter", std::to_string(g.level), VT::Integer,
                                    set([](hmi::AlarmGroupDef& d, const std::string& v) {
                                        if (v.size() != 1 || v[0] < '0' || v[0] > '4') return false;
                                        d.level = v[0] - '0';
                                        return true;
                                    }),
                                    "Le niveau d'acc\xC3\xA8s qu'il faut pour acquitter (0 \xC3\xA0 4 ; 0 : tout le monde). Si la s\xC3\xA9" "curit\xC3\xA9 est active."));
        c.properties.push_back(prop("Archivage", tf(g.archive), VT::Boolean, set([](hmi::AlarmGroupDef& d, const std::string& v) {
                                        d.archive = v == "TRUE" || v == "true" || v == "1" || v == "oui";
                                        return true;
                                    }),
                                    "Non : les alarmes du groupe n'entrent pas dans l'historique."));
        const auto linked = hmi::objectGroupsLinkedTo(doc->project, name);
        std::string names;
        for (const auto& l : linked) names += (names.empty() ? "" : ", ") + l;
        c.properties.push_back(prop("Groupes d'objets li\xC3\xA9s", linked.empty() ? std::string("aucun") : std::to_string(linked.size()) + " : " + names,
                                    VT::ReadOnly, {},
                                    "Les groupes d'alarmes des objets (Vue.Objet, Vue.*, symbole:Sym) dont les alarmes prennent le comportement de ce groupe."));
        // 1.11 (R111) : « Lier... » ouvre une fenetre a cocher (les groupes d'objets, les
        // vues, les symboles ; ceux deja lies coches). Un clic sur la case deroule son
        // unique choix ; le choisir ouvre la fenetre.
        static const std::string kOpenLink = "Ouvrir la fen\xC3\xAAtre \xC3\xA0 cocher\xE2\x80\xA6";
        auto lier = prop("Lier\xE2\x80\xA6", "Cocher les groupes d'objets\xE2\x80\xA6", VT::Enum,
                         Commit([link, name](std::string_view) {
                             if (link) link(name);
                             return true;
                         }),
                         "Une fen\xC3\xAAtre \xC3\xA0 cocher : les groupes d'objets, les vues (Vue.*), les symboles (symbole:Sym) ; "
                         "ceux d\xC3\xA9j\xC3\xA0 li\xC3\xA9s \xC3\xA0 ce groupe sont coch\xC3\xA9s. Une seule commande (Ctrl+Z).");
        lier.enumValues = {kOpenLink};   // une seule entree (prop() y ajoute la valeur) : la choisir ouvre la fenetre
        c.properties.push_back(std::move(lier));
        out.children.push_back(std::move(c));
    }
    return out;
}
} // namespace

// 1.11 (R111) : « Lier... » dans une fenetre a cocher.
HmiAskDialog::Spec HmiAlarmsPane::linkDialog(const std::string& group, std::vector<std::string>* names) const {
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiAlarmGroupLink";
    spec.title = "Lier au groupe d'alarmes " + group;
    spec.text = "Coche les groupes d'alarmes des objets dont les alarmes prennent le comportement du groupe " + group
              + " (priorit\xC3\xA9, couleurs, acquittement, son, zone, niveau, archivage). Ceux d\xC3\xA9j\xC3\xA0 li\xC3\xA9s sont coch\xC3\xA9s ; "
                "un groupe li\xC3\xA9 ailleurs passe \xC3\xA0 " + group + ".";
    spec.listTitle = "Groupes d'objets, vues, symboles";
    if (names) names->clear();
    std::vector<bool> initial;
    for (const auto& g : hmi::linkableObjectGroups(doc_->project)) {
        HmiAskDialog::Item it;
        it.label = g.name;
        static const char* const kKinds[] = {"objet", "toute la vue", "chaque instance du symbole", "lien existant"};
        it.detail = kKinds[std::clamp(g.kind, 0, 3)];
        if (g.alarms > 0) it.detail += " \xC2\xB7 " + std::to_string(g.alarms) + (g.alarms == 1 ? " alarme" : " alarmes");
        if (!g.linkedTo.empty() && g.linkedTo != group) it.detail += " \xC2\xB7 li\xC3\xA9 \xC3\xA0 " + g.linkedTo;
        it.checked = g.linkedTo == group;
        initial.push_back(it.checked);
        spec.items.push_back(std::move(it));
        if (names) names->push_back(g.name);
    }
    if (spec.items.empty()) spec.text += "\nAucun objet ne porte d'alarme pour l'instant.";
    spec.note = "Le plus pr\xC3\xA9" "cis gagne : l'objet, le groupe qui l'englobe, la vue (Vue.*), puis le symbole. "
                "La surcharge \xC2\xAB groupe \xC2\xBB d'une alarme de l'objet passe avant. Une seule commande : Ctrl+Z.";
    spec.confirm = "Appliquer";
    spec.confirmLabel = [initial](const std::vector<bool>& items, const std::vector<bool>&, int) {
        std::size_t on = 0, off = 0;
        for (std::size_t i = 0; i < items.size() && i < initial.size(); ++i) {
            if (items[i] && !initial[i]) ++on;
            if (!items[i] && initial[i]) ++off;
        }
        if (on == 0 && off == 0) return std::string("Rien \xC3\xA0 changer");
        std::string s = "Appliquer (";
        if (on) s += std::to_string(on) + " li\xC3\xA9" + (on > 1 ? "s" : "");
        if (on && off) s += ", ";
        if (off) s += std::to_string(off) + " d\xC3\xA9li\xC3\xA9" + (off > 1 ? "s" : "");
        return s + ")";
    };
    spec.width = 620.f;
    return spec;
}

bool HmiAlarmsPane::applyLinkAnswer(const std::string& group, const std::vector<std::string>& names, const HmiAskDialog::Answer& a) {
    std::vector<std::string> checked;
    for (std::size_t i = 0; i < names.size() && i < a.items.size(); ++i)
        if (a.items[i]) checked.push_back(names[i]);
    auto cmd = hmi::setLinkedObjectGroupsCmd(doc_, group, checked);
    if (!cmd) return false;
    const std::string label = cmd->label();
    apply_(std::move(cmd));
    refresh();
    say(label);
    return true;
}

void HmiAlarmsPane::openLinkDialog(const std::string& group) {
    if (!hosts_.ask) return;
    std::vector<std::string> names;
    auto spec = linkDialog(group, &names);
    hosts_.ask(std::move(spec), [this, group, names](bool ok, const HmiAskDialog::Answer& a) {
        if (ok) (void)applyLinkAnswer(group, names, a);
    });
}

void HmiAlarmsPane::rebuildProperties() {
    const auto* a = doc_->project.alarm(selectedAlarm());
    const auto settingCommit = [this](const char* field) -> std::function<bool(std::string_view)> {
        return [this, field](std::string_view v) {
            message_.clear();
            return setSetting(field, std::string(v));
        };
    };
    if (!a) {
        PG::Category c;
        c.name = "Alarmes";
        c.properties.push_back(prop("Alarmes d\xC3\xA9" "finies", std::to_string(doc_->project.alarms.size()), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Pour commencer", "Nouvelle (barre d'outils)", PG::ValueType::ReadOnly, {},
                                    std::string("Une alarme = une condition (une expression bool\xC3\xA9" "enne sur les variables de ")
                                        + (core::hasApi() ? "l'automate ou de l'IHM" : "l'IHM")   // 1.12.2
                                        + "), un message, une priorit\xC3\xA9, une cat\xC3\xA9gorie et un groupe."));
        grid_->setCategories({std::move(c), alarmGroupsCategory(doc_, apply_, [this] { refresh(); }, [this](const std::string& g) { openLinkDialog(g); }),
                              alarmSettingsCategory(doc_->project, settingCommit)});
        return;
    }
    const Id id = a->id;
    auto commit = [this, id](const char* field) {
        return [this, id, field](std::string_view v) {
            message_.clear();
            return setField(id, field, std::string(v));
        };
    };
    PG::Category c;
    c.name = "Alarme";
    c.properties.push_back(prop("Nom", a->name, PG::ValueType::Text, commit("nom"),
                                "Lettres, chiffres, _ ; unique. Les actions \xC2\xAB Acquitter \xC2\xBB qui la citent suivent."));
    c.properties.push_back(prop("Condition", a->condition, PG::ValueType::Text, commit("condition"),
                                "Vraie : l'alarme appara\xC3\xAEt. Ex. : Armoires[0].ana.PT1.mes < 20 AND Armoires[0].prete"));
    c.properties.push_back(prop("Message", a->message, PG::ValueType::Text, commit("message"),
                                "Texte \xC3\xA0 trous, horodat\xC3\xA9 \xC3\xA0 l'apparition : "
                                "Bouteille A vide ({Armoires[0].ana.PT1.mes:0.0} bar)"));
    c.properties.push_back(prop("Priorit\xC3\xA9", priorityChoice(a->priority), PG::ValueType::Enum, commit("priorite"),
                                "1 = critique ... 4 = basse. Les alarmes actives sont class\xC3\xA9" "es par priorit\xC3\xA9.",
                                priorityChoices()));
    c.properties.push_back(prop("Cat\xC3\xA9gorie", a->category, PG::ValueType::Enum, commit("categorie"), {}, hmi::alarmCategories()));
    c.properties.push_back(prop("Groupe", a->group, PG::ValueType::Text, commit("groupe"),
                                "Un groupe s'acquitte d'un coup (action \xC2\xAB Acquitter \xC2\xBB, cible groupe:Armoire A)."));
    c.properties.push_back(prop("Acquittement requis", tf(a->ackRequired), PG::ValueType::Boolean, commit("acquittement"),
                                "Non : l'alarme dispara\xC3\xAEt seule quand sa condition redevient fausse."));
    c.properties.push_back(prop("Temporisation (ms)", std::to_string(a->delayMs), PG::ValueType::Integer, commit("delai"),
                                "La condition doit rester vraie ce temps-l\xC3\xA0 avant que l'alarme apparaisse (0 : aussit\xC3\xB4t)."));
    c.properties.push_back(prop("Description", a->description, PG::ValueType::Text, commit("description")));
    // Lot 11 : la consigne - ce que fait l'operateur (l'objet Consigne d'alarme la montre).
    c.properties.push_back(prop("Consigne", a->instruction, PG::ValueType::Text, commit("consigne"),
                                "Ce que fait l'op\xC3\xA9rateur quand elle appara\xC3\xAEt ; \\n : une nouvelle ligne ; des trous "
                                "{Variable:0.0} remplis \xC3\xA0 l'apparition. L'objet Consigne d'alarme la montre."));
    PG::Category u;
    u.name = "Utilisation";
    const auto uses = actionUses(doc_->project, hmi::Operation::AckAlarm, a->name);
    u.properties.push_back(prop("Acquitt\xC3\xA9" "e par", uses.empty() ? std::string("aucune action (Tout acquitter la prend)") : fewOf(uses),
                                PG::ValueType::ReadOnly));
    const auto paths = variablePaths(a->condition);
    u.properties.push_back(prop("Variables lues", paths.empty() ? std::string("-") : fewOf(paths, 4), PG::ValueType::ReadOnly));
    grid_->setCategories({std::move(c), std::move(u), alarmGroupsCategory(doc_, apply_, [this] { refresh(); }, [this](const std::string& g) { openLinkDialog(g); }),
                          alarmSettingsCategory(doc_->project, settingCommit)});
}

bool HmiAlarmsPane::setSetting(const std::string& field, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string reason) {
        if (why) *why = reason;
        say(reason, true);
        return false;
    };
    const std::string value = trimmed(raw);
    hmi::AlarmSettings next = doc_->project.alarmSettings;
    std::string label;
    if (field.size() == 4 && field.rfind("son", 0) == 0 && field[3] >= '1' && field[3] <= '4') {
        const auto k = static_cast<std::size_t>(field[3] - '1');
        if (!value.empty()) {
            const auto* r = doc_->project.resourceByName(value);
            if (!r) return fail("son introuvable dans les ressources : " + value);
            if (r->kind() != hmi::MediaKind::Sound) return fail(value + " n'est pas un son");
        }
        next.sounds[k] = value;
        label = "son de la priorit\xC3\xA9 " + std::to_string(k + 1);
    } else if (field == "repetition") {
        double s = 0;
        if (!hmi::parseNumber(value, s) || s < 0 || s > 3600) return fail("r\xC3\xA9p\xC3\xA9tition : de 0 \xC3\xA0 3600 secondes");
        next.repeatS = static_cast<int>(s);
        label = "r\xC3\xA9p\xC3\xA9tition du son";
    } else if (field == "mise_de_cote_max") {
        double m = 0;
        if (!hmi::parseNumber(value, m) || m < 1 || m > 100000) return fail("mise de c\xC3\xB4t\xC3\xA9 : de 1 \xC3\xA0 100000 minutes");
        next.maxShelveMin = static_cast<int>(m);
        label = "mise de c\xC3\xB4t\xC3\xA9 maximale";
    } else {
        return fail("r\xC3\xA9glage inconnu : " + field);
    }
    if (next == doc_->project.alarmSettings) return true;
    auto cmd = hmi::changeProject(doc_, "Alarmes : " + label, [&](hmi::Project& pr) { pr.alarmSettings = next; });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Alarmes : " + label + " r\xC3\xA9gl\xC3\xA9" "e");
    return true;
}

Id HmiAlarmsPane::addAlarm(std::string name, std::string* why) {
    auto& p = doc_->project;
    if (name.empty()) name = hmi::uniqueAlarmName(p, "Alarme_1");
    if (!hmi::isIdentifier(name)) {
        if (why) *why = "nom invalide : lettres, chiffres et _";
        say("Nom refus\xC3\xA9 : " + name, true);
        return kNoId;
    }
    if (p.alarmByName(name)) {
        if (why) *why = "'" + name + "' existe d\xC3\xA9j\xC3\xA0";
        say("'" + name + "' existe d\xC3\xA9j\xC3\xA0", true);
        return kNoId;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Nouvelle alarme " + name, [&](hmi::Project& pr) {
        hmi::AlarmDef a;
        a.id = pr.allocate();
        a.name = name;
        a.message = name;
        a.condition = "FALSE";
        made = a.id;
        // Apres l'alarme choisie, sinon a la fin.
        auto at = pr.alarms.end();
        for (auto it = pr.alarms.begin(); it != pr.alarms.end(); ++it)
            if (it->id == selectedAlarm()) at = std::next(it);
        pr.alarms.insert(at, std::move(a));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) fresh_.push_back(made);   // 1.11 (R111) : son groupe lui donnera sa priorite par defaut
    if (priorityFilter_ != 0 || !searchText_.empty()) { priorityFilter_ = 0; searchText_.clear(); priority_->setSelectedIndex(0); search_->setText(""); }
    refresh();
    selectAlarm(made);
    say("Alarme " + name + " cr\xC3\xA9\xC3\xA9" "e : \xC3\xA9" "cris sa condition \xC3\xA0 droite");
    return made;
}

Id HmiAlarmsPane::duplicateAlarm(Id id, std::string* why) {
    const auto* src = doc_->project.alarm(id);
    if (!src) { if (why) *why = "aucune alarme choisie"; return kNoId; }
    const hmi::AlarmDef copy = *src;
    const std::string name = hmi::uniqueAlarmName(doc_->project, copy.name);
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Dupliquer " + copy.name, [&](hmi::Project& pr) {
        hmi::AlarmDef a = copy;
        a.id = pr.allocate();
        a.name = name;
        made = a.id;
        auto at = pr.alarms.end();
        for (auto it = pr.alarms.begin(); it != pr.alarms.end(); ++it)
            if (it->id == id) at = std::next(it);
        pr.alarms.insert(at, std::move(a));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectAlarm(made);
    say(copy.name + " dupliqu\xC3\xA9" "e en " + name);
    return made;
}

bool HmiAlarmsPane::deleteAlarm(Id id) {
    const auto* a = doc_->project.alarm(id);
    if (!a) return false;
    const std::string name = a->name;
    const auto uses = actionUses(doc_->project, hmi::Operation::AckAlarm, name);
    auto cmd = hmi::changeProject(doc_, "Supprimer " + name, [&](hmi::Project& pr) {
        auto& v = pr.alarms;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::AlarmDef& x) { return x.id == id; }), v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Supprim\xC3\xA9" "e : " + name + (uses.empty() ? std::string{} : " - " + std::to_string(uses.size())
                                                                            + " action(s) la citaient (G\xC3\xA9n\xC3\xA9rer le dira)"),
        !uses.empty());
    return true;
}

bool HmiAlarmsPane::moveAlarm(Id id, int delta) {
    const auto& v = doc_->project.alarms;
    std::size_t at = v.size();
    for (std::size_t i = 0; i < v.size(); ++i) if (v[i].id == id) at = i;
    if (at == v.size()) return false;
    const auto to = static_cast<std::ptrdiff_t>(at) + delta;
    if (to < 0 || to >= static_cast<std::ptrdiff_t>(v.size())) return false;
    auto cmd = hmi::changeProject(doc_, "Ordonner les alarmes", [&](hmi::Project& pr) {
        std::swap(pr.alarms[at], pr.alarms[static_cast<std::size_t>(to)]);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectAlarm(id);
    return true;
}

bool HmiAlarmsPane::setField(Id id, const std::string& field, const std::string& raw, std::string* why) {
    const auto* cur = doc_->project.alarm(id);
    const auto fail = [&](std::string reason) {
        if (why) *why = reason;
        say(reason, true);
        return false;
    };
    if (!cur) return fail("aucune alarme choisie");
    const std::string value = field == "message" || field == "description" || field == "consigne" ? raw : trimmed(raw);
    hmi::AlarmDef next = *cur;
    std::size_t followed = 0;
    std::size_t compared = 0;     // lot API 8 : finitions - les chaines comparees qui suivent
    int groupPriority = 0;        // 1.11 (R111) : la priorite par defaut du groupe, prise
    if (field == "nom") {
        if (!hmi::isIdentifier(value)) return fail("nom invalide : lettres, chiffres et _ (" + value + ")");
        for (const auto& a : doc_->project.alarms)
            if (a.id != id && same(a.name, value)) return fail("'" + value + "' existe d\xC3\xA9j\xC3\xA0");
        next.name = value;
    } else if (field == "condition") {
        const auto e = hmi::Expression::compile(value.empty() ? std::string("FALSE") : value);
        if (!e.valid()) return fail("condition illisible : " + e.error());
        std::string ro;
        if (!hmi::isReadOnly(value, &ro)) return fail(ro);
        next.condition = value;
    } else if (field == "message") {
        const auto errors = hmi::TextTemplate::compile(value).errors();
        if (!errors.empty()) return fail("message : " + errors.front());
        next.message = value;
    } else if (field == "priorite") {
        const int prio = value.empty() ? 0 : value[0] - '0';
        if (prio < 1 || prio > hmi::kAlarmPriorities) return fail("priorit\xC3\xA9 de 1 (critique) \xC3\xA0 4 (basse)");
        next.priority = prio;
        fresh_.erase(std::remove(fresh_.begin(), fresh_.end(), id), fresh_.end());   // 1.11 (R111) : choisie, elle reste
    } else if (field == "categorie") {
        if (value.empty()) return fail("une cat\xC3\xA9gorie : D\xC3\xA9" "faut, Alarme, Avertissement ou Information");
        next.category = value;
    } else if (field == "groupe") {
        next.group = value;
        // 1.11 (R111) : une NOUVELLE alarme (Ajouter), dont la priorite n'a pas ete choisie,
        // prend la priorite par defaut de son groupe (0 : elle garde la sienne).
        if (std::find(fresh_.begin(), fresh_.end(), id) != fresh_.end() && !value.empty())
            if (const int d = hmi::alarmGroupSettings(doc_->project, value).priority; d >= 1 && d <= hmi::kAlarmPriorities) {
                next.priority = d;
                groupPriority = d;
            }
    } else if (field == "acquittement") {
        next.ackRequired = yes(value);
    } else if (field == "delai") {
        double ms = 0;
        if (!hmi::parseNumber(value, ms) || ms < 0) return fail("temporisation : un nombre de millisecondes (0 ou plus)");
        next.delayMs = static_cast<int>(ms);
    } else if (field == "description") {
        next.description = value;
    } else if (field == "consigne") {
        const std::string text = raw;
        const auto errors = hmi::TextTemplate::compile(text).errors();
        if (!errors.empty()) return fail("consigne : " + errors.front());
        next.instruction = text;
    } else {
        return fail("champ inconnu : " + field);
    }
    if (next == *cur) return true;
    const std::string oldName = cur->name;
    auto cmd = hmi::changeProject(doc_, "Alarme " + next.name + " : " + field, [&](hmi::Project& pr) {
        if (auto* a = pr.alarm(id)) *a = next;
        if (field == "nom") followed = retarget(pr, hmi::Operation::AckAlarm, oldName, next.name);
        // ---- Lot API 8 : renommer partout (IHM) : mettre de cote, remettre en service ----
        if (field == "nom")
            for (const auto op : {hmi::Operation::ShelveAlarm, hmi::Operation::UnshelveAlarm})
                followed += retarget(pr, op, oldName, next.name);
        // ---- Lot API 8 : finitions : 'Alarme_1' comparee a SYS.AlarmSelected / AlarmLastName suit ----
        if (field == "nom") compared = hmi::renameAlarmReferences(pr, oldName, next.name);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectAlarm(id);
    static const std::map<std::string, std::string> labels = {
        {"condition", "condition"}, {"message", "message"}, {"priorite", "priorit\xC3\xA9"}, {"categorie", "cat\xC3\xA9gorie"},
        {"groupe", "groupe"}, {"acquittement", "acquittement requis"}, {"delai", "temporisation (ms)"}, {"description", "description"},
        {"consigne", "consigne"}};
    if (field == "nom")
        say("Renomm\xC3\xA9" "e en " + next.name + (followed ? " : " + std::to_string(followed) + " action(s) suivent" : std::string{})
            + (compared ? " ; " + std::to_string(compared) + " endroit(s) qui la nomment (SYS.AlarmSelected = '...', IHM_METTRE_DE_COTE('...')) : suivi(s)"
                        : std::string{}));   // lot API 8 : finitions
    else if (groupPriority > 0) {   // 1.11 (R111)
        static const char* const kNames[] = {"", "Critique", "Haute", "Moyenne", "Basse"};
        say(next.name + " : groupe = " + value + " ; priorit\xC3\xA9 " + std::to_string(groupPriority) + " - "
            + kNames[std::clamp(groupPriority, 1, 4)] + ", celle du groupe " + value);
    } else
        say(next.name + " : " + (labels.count(field) ? labels.at(field) : field) + " = " + value);
    return true;
}

void HmiAlarmsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    priority_->setBounds({b.x + 8, b.y + 42, 190, 28});
    search_->setBounds({b.x + 206, b.y + 42, std::min(420.f, b.w - 214), 28});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 76, b.w, std::max(0.f, b.h - 100)});
}

void HmiAlarmsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

// ================================================================== recettes ==
namespace {
enum RecipeAction : int {
    RNew = 1, RDup, RDel, FNew, FDel, SNew, SDup, SDel, RImport, RExport, RCompare
};
}

HmiRecipesPane::HmiRecipesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(RNew, HmiGlyph::Plus, "Nouvelle recette", "Recette");
    tools->add(RDup, HmiGlyph::Duplicate, "Dupliquer la recette (\xC3\xA9l\xC3\xA9ments et jeux)");
    tools->add(RDel, HmiGlyph::Delete, "Supprimer la recette (Ctrl+Z la rend)");
    tools->separator();
    tools->add(FNew, HmiGlyph::Plus, "Nouvel \xC3\xA9l\xC3\xA9ment (une variable de la recette)", "\xC3\x89l\xC3\xA9ment");
    tools->add(FDel, HmiGlyph::Delete, "Supprimer l'\xC3\xA9l\xC3\xA9ment choisi (et sa valeur dans chaque jeu)");
    tools->separator();
    tools->add(SNew, HmiGlyph::Plus, "Nouveau jeu de valeurs", "Jeu");
    tools->add(SDup, HmiGlyph::Duplicate, "Dupliquer le jeu choisi");
    tools->add(SDel, HmiGlyph::Delete, "Supprimer le jeu choisi");
    tools->separator();
    tools->add(RImport, HmiGlyph::Import, "Importer des jeux depuis un fichier CSV", "Importer");
    tools->add(RExport, HmiGlyph::Export, "Exporter les jeux dans un fichier CSV", "Exporter");
    tools->add(RCompare, HmiGlyph::Compare, "Comparer deux jeux", "Comparer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    for (int a : {RDup, RDel, FNew, SNew, RImport, RExport})
        tools_->setEnabledWhen(a, [this] { return selectedRecipe() != kNoId; });
    tools_->setEnabledWhen(FDel, [this] { return selectedField() >= 0; });
    tools_->setEnabledWhen(SDup, [this] { return selectedRecord() != kNoId; });
    tools_->setEnabledWhen(SDel, [this] { return selectedRecord() != kNoId; });
    tools_->setEnabledWhen(RCompare, [this] {
        const auto* r = doc_->project.recipe(selectedRecipe());
        return r && r->records.size() >= 2;
    });
    // Lot API 8 : chercher une recette - la recherche de toute l'application.
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Rechercher : recette, description, \xC3\xA9l\xC3\xA9ment, variable, jeu\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 dans le nom de la recette, sa description, ses \xC3\xA9l\xC3\xA9ments (nom, variable, unit\xC3\xA9) "
        "et ses jeux (nom, description) - tous les mots (ET), sans casse ni accents ; \"une phrase\" entre guillemets ; -mot : l'exclure.")));

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".recipesPanel", "RECETTES");
        auto table = std::make_unique<ui::TableView>(base + ".recipes");
        table->setColumns({{"Recette", 186.f}, {"\xC3\x89l\xC3\xA9ments", 86.f, 40.f, true, true, true, ui::Align::End},
                           {"Jeux", 56.f, 40.f, true, true, true, ui::Align::End}, {"Description", 200.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        recipes_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        split->addPane(std::move(panel), 0.24f, 200.f);
    }
    {
        auto centre = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".centre");
        auto panel = std::make_unique<HmiTitledPanel>(base + ".fieldsPanel", "\xC3\x89L\xC3\x89MENTS");
        auto table = std::make_unique<ui::TableView>(base + ".fields");
        table->setColumns({{"\xC3\x89l\xC3\xA9ment", 170.f}, {"Variable", 230.f}, {"Unit\xC3\xA9", 70.f}, {"Min", 70.f, 40.f, true, true, true, ui::Align::End},
                           {"Max", 70.f, 40.f, true, true, true, ui::Align::End}});
        table->setSelectionMode(ui::SelectionMode::Single);
        fields_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        centre->addPane(std::move(panel), 0.38f, 120.f);
        auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
        {
            auto table2 = std::make_unique<ui::TableView>(base + ".records");
            table2->setSelectionMode(ui::SelectionMode::Single);
            records_ = table2.get();
            tabs->addTab(ui::TabControl::Tab{"Jeux de valeurs", ui::Icon::AnimationTable, false, false}, std::move(table2));
        }
        {
            auto table3 = std::make_unique<ui::TableView>(base + ".compare");
            table3->setSelectionMode(ui::SelectionMode::Single);
            compare_ = table3.get();
            tabs->addTab(ui::TabControl::Tab{"Comparaison", ui::Icon::Analyze, false, false}, std::move(table3));
        }
        tabs->setCurrentIndex(0);
        tabs_ = &static_cast<ui::TabControl&>(centre->addPane(std::move(tabs), 0.62f, 140.f));
        centre_ = &static_cast<ui::Splitter&>(split->addPane(std::move(centre), 0.48f, 320.f));
    }
    grid_ = &static_cast<ui::PropertyGrid&>(split->addPane(std::make_unique<ui::PropertyGrid>(base + ".grid"), 0.28f, 240.f));
    grid_->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id r = selectedRecipe();
        switch (a) {
            case RNew: (void)addRecipe(); break;
            case RDup: if (r) (void)duplicateRecipe(r); break;
            case RDel:
                if (!r) break;
                if (hosts_.remove) hosts_.remove(r);
                else (void)deleteRecipe(r);
                break;
            case FNew: if (r) (void)addField(r, hmi::RecipeField{}); break;
            case FDel: if (r && selectedField() >= 0) (void)removeField(r, selectedField()); break;
            case SNew: if (r) (void)addRecord(r); break;
            case SDup: if (r && selectedRecord()) (void)duplicateRecord(r, selectedRecord()); break;
            case SDel: if (r && selectedRecord()) (void)deleteRecord(r, selectedRecord()); break;
            case RImport: if (r && hosts_.importCsv) hosts_.importCsv(r); break;
            case RExport: if (r && hosts_.exportCsv) hosts_.exportCsv(r); break;
            case RCompare: {
                const auto* rec = doc_->project.recipe(r);
                if (!rec || rec->records.size() < 2) break;
                if (hosts_.compare) { hosts_.compare(r); break; }
                // Sans hote : le jeu choisi contre le suivant (ou le premier).
                Id left = selectedRecord() ? selectedRecord() : rec->records[0].id;
                Id right = rec->records[0].id == left ? rec->records[1].id : rec->records[0].id;
                (void)compare(r, left, right);
                break;
            }
            default: break;
        }
    });
    links_ += recipes_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (refreshing_) return;
        refreshDetail();
        rebuildProperties();
    });
    links_ += fields_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += records_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) rebuildProperties();
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
    // Lot API 8 : la recherche, branchee, puis relue (retenue d'une seance a l'autre).
    links_ += search_->changed->connect([this] { refresh(); });
    search_->recall();
}

void HmiRecipesPane::refresh() {
    const Id keep = selectedRecipe();
    const auto& p = doc_->project;
    std::vector<std::vector<std::string>> rows;
    recipeOrder_.clear();
    std::size_t sets = 0;
    // Lot API 8 : la recherche - le nom, la description, les elements (nom,
    // variable, unite), les jeux (nom, description).
    const ui::SearchQuery& query = search_->query();
    for (const auto& r : p.recipes) {
        sets += r.records.size();
        if (!query.empty()) {
            std::vector<std::string> texts{r.name, r.description};
            for (const auto& f : r.fields) {
                texts.push_back(f.name);
                texts.push_back(f.variable);
                texts.push_back(f.unit);
            }
            for (const auto& rec : r.records) {
                texts.push_back(rec.name);
                texts.push_back(rec.description);
            }
            if (!query.matches(texts)) continue;
        }
        recipeOrder_.push_back(r.id);
        rows.push_back({r.name, std::to_string(r.fields.size()), std::to_string(r.records.size()), r.description});
    }
    refreshing_ = true;
    recipesModel_ = std::make_shared<Rows>(std::vector<std::string>{"Recette", "\xC3\x89l\xC3\xA9ments", "Jeux", "Description"},
                                           std::move(rows), [](ui::RowIndex, std::size_t c) {
                                               ui::CellStyle s;
                                               if (c == 0) { s.icon = ui::Icon::AnimationTable; s.bold = true; }
                                               return s;
                                           });
    recipes_->setModel(recipesModel_);
    refreshing_ = false;
    // Lot API 8 : les termes surlignes (la recette, ses elements, ses jeux), le compte.
    for (auto* t : {recipes_, fields_, records_}) t->setHighlight(search_->text());
    search_->setCount(recipeOrder_.size(), p.recipes.size());
    // La recette choisie, si la recherche la montre encore ; sinon la premiere montree.
    const bool keepShown = keep && std::find(recipeOrder_.begin(), recipeOrder_.end(), keep) != recipeOrder_.end();
    if (keepShown) selectRecipe(keep);
    else if (!recipeOrder_.empty()) selectRecipe(recipeOrder_.front());
    else { refreshDetail(); rebuildProperties(); }
    status_->setMessage(std::to_string(p.recipes.size()) + " recette(s), " + std::to_string(sets) + " jeu(x) de valeurs"
                        + " \xC2\xB7 appliquer un jeu : action \xC2\xAB Charger une recette \xC2\xBB, ou en simulation");
    invalidate();
}

void HmiRecipesPane::refreshDetail() {
    const auto* r = doc_->project.recipe(selectedRecipe());
    const int keepField = r && r->id == shownRecipe_ ? selectedField() : -1;
    const Id keepRecord = r && r->id == shownRecipe_ ? selectedRecord() : kNoId;
    refreshing_ = true;
    std::vector<std::vector<std::string>> frows;
    if (r)
        for (const auto& f : r->fields) frows.push_back({f.name, f.variable, f.unit, f.min, f.max});
    fieldsModel_ = std::make_shared<Rows>(std::vector<std::string>{"\xC3\x89l\xC3\xA9ment", "Variable", "Unit\xC3\xA9", "Min", "Max"},
                                          std::move(frows), [](ui::RowIndex, std::size_t c) {
                                              ui::CellStyle s;
                                              if (c == 1) s.monospace = true;
                                              return s;
                                          });
    fields_->setModel(fieldsModel_);

    std::vector<ui::TableView::Column> cols{{"Jeu", 130.f}};
    std::vector<std::string> headers{"Jeu"};
    if (r)
        for (const auto& f : r->fields) {
            const std::string h = f.unit.empty() ? f.name : f.name + " (" + f.unit + ")";
            // Un nombre borne s'aligne a droite ; un texte a gauche (un texte long
            // aligne a droite deborderait sur la colonne voisine).
            const bool numeric = !f.min.empty() || !f.max.empty();
            cols.push_back({h, std::max(110.f, 9.5f * static_cast<float>(h.size()) + 28.f), 50.f, true, true, true,
                            numeric ? ui::Align::End : ui::Align::Start});
            headers.push_back(h);
        }
    cols.push_back({"Modifi\xC3\xA9", 150.f});
    headers.push_back("Modifi\xC3\xA9");
    std::vector<std::vector<std::string>> srows;
    recordOrder_.clear();
    if (r)
        for (const auto& rec : r->records) {
            recordOrder_.push_back(rec.id);
            std::vector<std::string> row{rec.name};
            for (std::size_t i = 0; i < r->fields.size(); ++i) row.push_back(i < rec.values.size() ? rec.values[i] : "");
            row.push_back(rec.modified);
            srows.push_back(std::move(row));
        }
    records_->setColumns(std::move(cols));
    recordsModel_ = std::make_shared<Rows>(std::move(headers), std::move(srows), [](ui::RowIndex, std::size_t c) {
        ui::CellStyle s;
        if (c == 0) s.bold = true;
        return s;
    });
    records_->setModel(recordsModel_);
    if (!r || r->id != shownRecipe_) { diff_.clear(); diffTitle_.clear(); }
    // La comparaison, si elle est de cette recette.
    std::vector<std::vector<std::string>> crows;
    std::vector<bool> differs;
    for (const auto& d : diff_) {
        crows.push_back({d.field, d.unit, d.left, d.right, d.differs ? "\xE2\x89\xA0" : "="});
        differs.push_back(d.differs);
    }
    std::string left = "Jeu A", right = "Jeu B";
    if (const auto bar = diffTitle_.find('|'); bar != std::string::npos) {
        left = diffTitle_.substr(0, bar);
        right = diffTitle_.substr(bar + 1);
    }
    compare_->setColumns({{"\xC3\x89l\xC3\xA9ment", 170.f}, {"Unit\xC3\xA9", 70.f}, {left, 170.f}, {right, 170.f}, {"\xC3\x89" "cart", 64.f}});
    compareModel_ = std::make_shared<Rows>(std::vector<std::string>{"\xC3\x89l\xC3\xA9ment", "Unit\xC3\xA9", left, right, "\xC3\x89" "cart"},
                                           std::move(crows), [differs](ui::RowIndex row, std::size_t c) {
                                               ui::CellStyle s;
                                               if (row < differs.size() && differs[row]) {
                                                   if (c >= 2) { s.bold = true; s.fgTone = ui::Tone::Warning; }
                                                   if (c == 0) { s.icon = ui::Icon::Warning; s.iconTone = ui::Tone::Warning; }
                                               }
                                               return s;
                                           });
    compare_->setModel(compareModel_);
    std::size_t nd = 0;
    for (const auto& d : diff_) nd += d.differs;
    tabs_->setTabBadge(1, diff_.empty() ? std::string{} : std::to_string(nd), nd ? ui::Tone::Warning : ui::Tone::Ok);
    tabs_->setTabBadge(0, r ? std::to_string(r->records.size()) : std::string{}, ui::Tone::Accent);
    shownRecipe_ = r ? r->id : kNoId;
    refreshing_ = false;
    if (keepField >= 0 && r && static_cast<std::size_t>(keepField) < r->fields.size())
        fields_->selectModelRows({static_cast<ui::RowIndex>(keepField)}, false);
    if (keepRecord) {
        for (std::size_t i = 0; i < recordOrder_.size(); ++i)
            if (recordOrder_[i] == keepRecord) records_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    }
}

Id HmiRecipesPane::selectedRecipe() const {
    const int r = selectedRow(*recipes_);
    return r >= 0 && static_cast<std::size_t>(r) < recipeOrder_.size() ? recipeOrder_[static_cast<std::size_t>(r)] : kNoId;
}

int HmiRecipesPane::selectedField() const {
    const auto* r = doc_->project.recipe(selectedRecipe());
    const int f = selectedRow(*fields_);
    return r && f >= 0 && static_cast<std::size_t>(f) < r->fields.size() ? f : -1;
}

Id HmiRecipesPane::selectedRecord() const {
    const int r = selectedRow(*records_);
    return r >= 0 && static_cast<std::size_t>(r) < recordOrder_.size() ? recordOrder_[static_cast<std::size_t>(r)] : kNoId;
}

void HmiRecipesPane::selectRecipe(Id id) {
    // Lot API 8 : une recette demandee (creee, dupliquee, renommee) que la
    // recherche cache : la recherche s'efface (refresh), puis elle est choisie.
    if (id != kNoId && !search_->text().empty() && doc_->project.recipe(id)
        && std::find(recipeOrder_.begin(), recipeOrder_.end(), id) == recipeOrder_.end())
        search_->setText("");
    for (std::size_t i = 0; i < recipeOrder_.size(); ++i)
        if (recipeOrder_[i] == id) {
            refreshing_ = true;
            recipes_->selectModelRows({static_cast<ui::RowIndex>(i)});
            refreshing_ = false;
            break;
        }
    refreshDetail();
    rebuildProperties();
}

void HmiRecipesPane::selectField(int index) {
    if (index < 0) fields_->selectModelRows({});
    else fields_->selectModelRows({static_cast<ui::RowIndex>(index)});
}

void HmiRecipesPane::selectRecord(Id id) {
    for (std::size_t i = 0; i < recordOrder_.size(); ++i)
        if (recordOrder_[i] == id) { records_->selectModelRows({static_cast<ui::RowIndex>(i)}); return; }
}

void HmiRecipesPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiRecipesPane::rebuildProperties() {
    const auto* r = doc_->project.recipe(selectedRecipe());
    std::vector<PG::Category> cats;
    if (!r) {
        PG::Category c;
        c.name = "Recettes";
        c.properties.push_back(prop("Recettes", std::to_string(doc_->project.recipes.size()), PG::ValueType::ReadOnly, {},
                                    "Une recette = des \xC3\xA9l\xC3\xA9ments (une variable chacun) et des jeux de valeurs. "
                                    "Appliquer un jeu \xC3\xA9" "crit toutes ses valeurs, ou aucune."));
        grid_->setCategories({std::move(c)});
        return;
    }
    const Id rid = r->id;
    PG::Category rc;
    rc.name = "Recette";
    rc.properties.push_back(prop("Nom", r->name, PG::ValueType::Text, [this, rid](std::string_view v) {
        return setRecipeField(rid, "nom", std::string(v));
    }, "Les actions \xC2\xAB Charger une recette \xC2\xBB qui la citent suivent."));
    rc.properties.push_back(prop("Description", r->description, PG::ValueType::Text, [this, rid](std::string_view v) {
        return setRecipeField(rid, "description", std::string(v));
    }));
    rc.properties.push_back(prop("\xC3\x89l\xC3\xA9ments / jeux", std::to_string(r->fields.size()) + " / " + std::to_string(r->records.size()),
                                 PG::ValueType::ReadOnly));
    const auto uses = actionUses(doc_->project, hmi::Operation::LoadRecipe, r->name);
    rc.properties.push_back(prop("Charg\xC3\xA9" "e par", uses.empty() ? std::string("aucune action") : fewOf(uses), PG::ValueType::ReadOnly));
    cats.push_back(std::move(rc));

    if (const auto* rec = r->record(selectedRecord())) {
        const Id sid = rec->id;
        PG::Category sc;
        sc.name = "Jeu : " + rec->name;
        sc.properties.push_back(prop("Nom du jeu", rec->name, PG::ValueType::Text, [this, rid, sid](std::string_view v) {
            return setRecordProp(rid, sid, "nom", std::string(v));
        }));
        sc.properties.push_back(prop("Description du jeu", rec->description, PG::ValueType::Text, [this, rid, sid](std::string_view v) {
            return setRecordProp(rid, sid, "description", std::string(v));
        }));
        for (std::size_t i = 0; i < r->fields.size(); ++i) {
            const auto& f = r->fields[i];
            std::string help = f.variable.empty() ? std::string("aucune variable") : "\xC3\x89" "crit dans " + f.variable;
            if (!f.min.empty() || !f.max.empty())
                help += " ; bornes [" + (f.min.empty() ? std::string("-") : f.min) + " ; " + (f.max.empty() ? std::string("-") : f.max) + "]";
            const int fi = static_cast<int>(i);
            sc.properties.push_back(prop(f.unit.empty() ? f.name : f.name + " (" + f.unit + ")",
                                         i < rec->values.size() ? rec->values[i] : std::string{}, PG::ValueType::Text,
                                         [this, rid, sid, fi](std::string_view v) { return setRecordValue(rid, sid, fi, std::string(v)); },
                                         help));
        }
        sc.properties.push_back(prop("Modifi\xC3\xA9", rec->modified.empty() ? std::string("-") : rec->modified, PG::ValueType::ReadOnly));
        cats.push_back(std::move(sc));
    }
    if (const int fi = selectedField(); fi >= 0) {
        const auto& f = r->fields[static_cast<std::size_t>(fi)];
        PG::Category fc;
        fc.name = "\xC3\x89l\xC3\xA9ment : " + f.name;
        const auto fieldCommit = [this, rid, fi](const char* key) {
            return [this, rid, fi, key](std::string_view v) { return setFieldProp(rid, fi, key, std::string(v)); };
        };
        fc.properties.push_back(prop("Nom de l'\xC3\xA9l\xC3\xA9ment", f.name, PG::ValueType::Text, fieldCommit("nom")));
        fc.properties.push_back(prop("Variable", f.variable, PG::ValueType::Text, fieldCommit("variable"),
                                     core::hasApi() ? "La variable \xC3\xA9" "crite (automate ou IHM) : Armoires[0].seuil_poids_saisi"
                                                    : "La variable IHM \xC3\xA9" "crite : Armoires[0].seuil_poids_saisi"));   // 1.12.2
        fc.properties.push_back(prop("Unit\xC3\xA9", f.unit, PG::ValueType::Text, fieldCommit("unite")));
        fc.properties.push_back(prop("Minimum", f.min, PG::ValueType::Text, fieldCommit("min"), "Vide : sans borne basse."));
        fc.properties.push_back(prop("Maximum", f.max, PG::ValueType::Text, fieldCommit("max"), "Vide : sans borne haute."));
        cats.push_back(std::move(fc));
    }
    grid_->setCategories(std::move(cats));
}

Id HmiRecipesPane::addRecipe(std::string name, std::string* why) {
    auto& p = doc_->project;
    if (name.empty()) name = hmi::uniqueRecipeName(p, "Recette_1");
    if (p.recipeByName(name)) {
        if (why) *why = "'" + name + "' existe d\xC3\xA9j\xC3\xA0";
        say("'" + name + "' existe d\xC3\xA9j\xC3\xA0", true);
        return kNoId;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Nouvelle recette " + name, [&](hmi::Project& pr) {
        hmi::Recipe r;
        r.id = pr.allocate();
        r.name = name;
        made = r.id;
        pr.recipes.push_back(std::move(r));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(made);
    say("Recette " + name + " cr\xC3\xA9\xC3\xA9" "e : ajoute ses \xC3\xA9l\xC3\xA9ments, puis ses jeux");
    return made;
}

Id HmiRecipesPane::duplicateRecipe(Id id, std::string* why) {
    const auto* src = doc_->project.recipe(id);
    if (!src) { if (why) *why = "aucune recette choisie"; return kNoId; }
    const hmi::Recipe copy = *src;
    const std::string name = hmi::uniqueRecipeName(doc_->project, copy.name);
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Dupliquer " + copy.name, [&](hmi::Project& pr) {
        hmi::Recipe r = copy;
        r.id = pr.allocate();
        r.name = name;
        for (auto& rec : r.records) rec.id = pr.allocate();
        made = r.id;
        pr.recipes.push_back(std::move(r));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(made);
    say(copy.name + " dupliqu\xC3\xA9" "e en " + name + " (" + std::to_string(copy.fields.size()) + " \xC3\xA9l\xC3\xA9ment(s), "
        + std::to_string(copy.records.size()) + " jeu(x))");
    return made;
}

bool HmiRecipesPane::deleteRecipe(Id id) {
    const auto* r = doc_->project.recipe(id);
    if (!r) return false;
    const std::string name = r->name;
    const auto uses = actionUses(doc_->project, hmi::Operation::LoadRecipe, name);
    auto cmd = hmi::changeProject(doc_, "Supprimer " + name, [&](hmi::Project& pr) {
        auto& v = pr.recipes;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::Recipe& x) { return x.id == id; }), v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Supprim\xC3\xA9" "e : " + name + (uses.empty() ? std::string{} : " - " + std::to_string(uses.size()) + " action(s) la chargeaient"),
        !uses.empty());
    return true;
}

bool HmiRecipesPane::setRecipeField(Id id, const std::string& field, const std::string& raw, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto fail = [&](std::string reason) { if (why) *why = reason; say(reason, true); return false; };
    if (!r) return fail("aucune recette choisie");
    const std::string value = field == "description" ? raw : trimmed(raw);
    if (field == "nom") {
        if (value.empty()) return fail("une recette a un nom");
        for (const auto& o : doc_->project.recipes)
            if (o.id != id && same(o.name, value)) return fail("'" + value + "' existe d\xC3\xA9j\xC3\xA0");
        if (value == r->name) return true;
    } else if (field != "description") {
        return fail("champ inconnu : " + field);
    }
    const std::string old = r->name;
    std::size_t followed = 0;
    auto cmd = hmi::changeProject(doc_, "Recette " + old + " : " + field, [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        if (!x) return;
        if (field == "nom") {
            x->name = value;
            followed = retarget(pr, hmi::Operation::LoadRecipe, old, value);
        } else {
            x->description = value;
        }
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    say(field == "nom" ? "Renomm\xC3\xA9" "e en " + value + (followed ? " : " + std::to_string(followed) + " action(s) suivent" : std::string{})
                       : old + " : description chang\xC3\xA9" "e");
    return true;
}

bool HmiRecipesPane::addField(Id id, hmi::RecipeField f, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto fail = [&](std::string reason) { if (why) *why = reason; say(reason, true); return false; };
    if (!r) return fail("aucune recette choisie");
    if (f.name.empty()) {
        for (int i = static_cast<int>(r->fields.size()) + 1;; ++i) {
            std::string n = "\xC3\x89l\xC3\xA9ment " + std::to_string(i);
            bool used = false;
            for (const auto& g : r->fields) used = used || same(g.name, n);
            if (!used) { f.name = n; break; }
        }
    }
    for (const auto& g : r->fields)
        if (same(g.name, f.name)) return fail("'" + f.name + "' existe d\xC3\xA9j\xC3\xA0 dans " + r->name);
    const std::string name = f.name;
    auto cmd = hmi::changeProject(doc_, "Recette " + r->name + " : nouvel \xC3\xA9l\xC3\xA9ment", [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        if (!x) return;
        x->fields.push_back(f);
        for (auto& rec : x->records) rec.values.resize(x->fields.size());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    if (const auto* x = doc_->project.recipe(id)) selectField(static_cast<int>(x->fields.size()) - 1);
    say("\xC3\x89l\xC3\xA9ment " + name + " ajout\xC3\xA9 : donne-lui sa variable \xC3\xA0 droite");
    return true;
}

bool HmiRecipesPane::removeField(Id id, int index) {
    const auto* r = doc_->project.recipe(id);
    if (!r || index < 0 || static_cast<std::size_t>(index) >= r->fields.size()) return false;
    const std::string name = r->fields[static_cast<std::size_t>(index)].name;
    auto cmd = hmi::changeProject(doc_, "Recette " + r->name + " : retirer " + name, [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        if (!x) return;
        x->fields.erase(x->fields.begin() + index);
        for (auto& rec : x->records)
            if (static_cast<std::size_t>(index) < rec.values.size()) rec.values.erase(rec.values.begin() + index);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    say("\xC3\x89l\xC3\xA9ment " + name + " retir\xC3\xA9 (et sa valeur dans chaque jeu)");
    return true;
}

bool HmiRecipesPane::setFieldProp(Id id, int index, const std::string& key, const std::string& raw, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto fail = [&](std::string reason) { if (why) *why = reason; say(reason, true); return false; };
    if (!r || index < 0 || static_cast<std::size_t>(index) >= r->fields.size()) return fail("aucun \xC3\xA9l\xC3\xA9ment choisi");
    const std::string value = trimmed(raw);
    hmi::RecipeField f = r->fields[static_cast<std::size_t>(index)];
    double n = 0;
    if (key == "nom") {
        if (value.empty()) return fail("un \xC3\xA9l\xC3\xA9ment a un nom");
        for (std::size_t i = 0; i < r->fields.size(); ++i)
            if (static_cast<int>(i) != index && same(r->fields[i].name, value)) return fail("'" + value + "' existe d\xC3\xA9j\xC3\xA0");
        f.name = value;
    } else if (key == "variable") {
        if (!value.empty()) {
            const auto e = hmi::Expression::compile(value);
            if (!e.valid() || e.roots().size() != 1) return fail("une variable seule : Armoires[0].seuil_poids_saisi");
        }
        f.variable = value;
    } else if (key == "unite") {
        f.unit = value;
    } else if (key == "min" || key == "max") {
        if (!value.empty() && !hmi::parseNumber(value, n)) return fail(key + " : un nombre, ou vide (sans borne)");
        (key == "min" ? f.min : f.max) = value;
        double lo = 0, hi = 0;
        if (!f.min.empty() && !f.max.empty() && hmi::parseNumber(f.min, lo) && hmi::parseNumber(f.max, hi) && lo > hi)
            return fail("le minimum doit \xC3\xAAtre sous le maximum");
    } else {
        return fail("champ inconnu : " + key);
    }
    if (f == r->fields[static_cast<std::size_t>(index)]) return true;
    auto cmd = hmi::changeProject(doc_, "Recette " + r->name + " : " + f.name, [&](hmi::Project& pr) {
        if (auto* x = pr.recipe(id); x && static_cast<std::size_t>(index) < x->fields.size()) x->fields[static_cast<std::size_t>(index)] = f;
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    selectField(index);
    say(f.name + " : " + key + " = " + (value.empty() ? std::string("(vide)") : value));
    return true;
}

Id HmiRecipesPane::addRecord(Id id, std::string name, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    if (!r) { if (why) *why = "aucune recette choisie"; return kNoId; }
    if (name.empty())
        for (int i = static_cast<int>(r->records.size()) + 1;; ++i) {
            name = "Jeu_" + std::to_string(i);
            if (!r->record(std::string_view(name))) break;
        }
    if (r->record(std::string_view(name))) {
        if (why) *why = "'" + name + "' existe d\xC3\xA9j\xC3\xA0";
        say("Le jeu '" + name + "' existe d\xC3\xA9j\xC3\xA0", true);
        return kNoId;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Recette " + r->name + " : nouveau jeu", [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        if (!x) return;
        hmi::RecipeRecord rec;
        rec.id = pr.allocate();
        rec.name = name;
        rec.values.resize(x->fields.size());
        rec.modified = hmi::wallStamp().substr(0, 19);
        made = rec.id;
        x->records.push_back(std::move(rec));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    selectRecord(made);
    say("Jeu " + name + " ajout\xC3\xA9 : saisis ses valeurs \xC3\xA0 droite");
    return made;
}

Id HmiRecipesPane::duplicateRecord(Id id, Id record, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto* src = r ? r->record(record) : nullptr;
    if (!src) { if (why) *why = "aucun jeu choisi"; return kNoId; }
    std::string name;
    for (int i = 2;; ++i) {
        name = src->name + "_" + std::to_string(i);
        if (!r->record(std::string_view(name))) break;
    }
    const hmi::RecipeRecord copy = *src;
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Dupliquer le jeu " + copy.name, [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        if (!x) return;
        hmi::RecipeRecord rec = copy;
        rec.id = pr.allocate();
        rec.name = name;
        rec.modified = hmi::wallStamp().substr(0, 19);
        made = rec.id;
        auto at = x->records.end();
        for (auto it = x->records.begin(); it != x->records.end(); ++it)
            if (it->id == record) at = std::next(it);
        x->records.insert(at, std::move(rec));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    selectRecord(made);
    say("Jeu " + copy.name + " dupliqu\xC3\xA9 en " + name);
    return made;
}

bool HmiRecipesPane::deleteRecord(Id id, Id record) {
    const auto* r = doc_->project.recipe(id);
    const auto* rec = r ? r->record(record) : nullptr;
    if (!rec) return false;
    const std::string name = rec->name;
    auto cmd = hmi::changeProject(doc_, "Supprimer le jeu " + name, [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        if (!x) return;
        auto& v = x->records;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::RecipeRecord& z) { return z.id == record; }), v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    say("Jeu " + name + " supprim\xC3\xA9");
    return true;
}

bool HmiRecipesPane::setRecordProp(Id id, Id record, const std::string& key, const std::string& raw, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto* rec = r ? r->record(record) : nullptr;
    const auto fail = [&](std::string reason) { if (why) *why = reason; say(reason, true); return false; };
    if (!rec) return fail("aucun jeu choisi");
    const std::string value = key == "description" ? raw : trimmed(raw);
    if (key == "nom") {
        if (value.empty()) return fail("un jeu a un nom");
        for (const auto& o : r->records)
            if (o.id != record && same(o.name, value)) return fail("le jeu '" + value + "' existe d\xC3\xA9j\xC3\xA0");
        if (value == rec->name) return true;
    } else if (key != "description") {
        return fail("champ inconnu : " + key);
    }
    std::size_t jeuxSuivis = 0;     // Lot API 8 : renommer partout (IHM)
    auto cmd = hmi::changeProject(doc_, "Jeu " + rec->name + " : " + key, [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        auto* z = x ? x->record(record) : nullptr;
        if (!z) return;
        // ---- Lot API 8 : renommer partout (IHM) : "charger une recette" qui nomme ce jeu suit ----
        if (key == "nom")
            for (auto& v : pr.views) {
                for (auto& a : v.actions)
                    if (a.operation == hmi::Operation::LoadRecipe && same(a.target, x->name) && same(a.value, z->name)) { a.value = value; ++jeuxSuivis; }
                for (auto& o : v.objects)
                    for (auto& a : o.actions)
                        if (a.operation == hmi::Operation::LoadRecipe && same(a.target, x->name) && same(a.value, z->name)) { a.value = value; ++jeuxSuivis; }
            }
        (key == "nom" ? z->name : z->description) = value;
        z->modified = hmi::wallStamp().substr(0, 19);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    selectRecord(record);
    say("Jeu : " + key + " = " + value + (jeuxSuivis ? " (" + std::to_string(jeuxSuivis) + " action(s) suivent)" : std::string{}));
    return true;
}

bool HmiRecipesPane::setRecordValue(Id id, Id record, int field, const std::string& raw, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto* rec = r ? r->record(record) : nullptr;
    const auto fail = [&](std::string reason) { if (why) *why = reason; say(reason, true); return false; };
    if (!rec || field < 0 || static_cast<std::size_t>(field) >= r->fields.size()) return fail("aucune valeur choisie");
    const auto& f = r->fields[static_cast<std::size_t>(field)];
    std::string value = trimmed(raw);
    if (!value.empty()) {
        const auto e = hmi::Expression::compile(value);
        if (!e.valid()) return fail(f.name + " : \xC2\xAB " + value + " \xC2\xBB illisible (" + e.error() + ")");
        double n = 0, lo = 0, hi = 0;
        if (hmi::parseNumber(value, n)) {
            if (!f.min.empty() && hmi::parseNumber(f.min, lo) && n < lo) return fail(f.name + " : " + value + " sous le minimum " + f.min);
            if (!f.max.empty() && hmi::parseNumber(f.max, hi) && n > hi) return fail(f.name + " : " + value + " au-dessus du maximum " + f.max);
        }
    }
    if (static_cast<std::size_t>(field) < rec->values.size() && rec->values[static_cast<std::size_t>(field)] == value) return true;
    const std::string recName = rec->name;
    auto cmd = hmi::changeProject(doc_, "Jeu " + recName + " : " + f.name, [&](hmi::Project& pr) {
        auto* x = pr.recipe(id);
        auto* z = x ? x->record(record) : nullptr;
        if (!z) return;
        z->values.resize(x->fields.size());
        z->values[static_cast<std::size_t>(field)] = value;
        z->modified = hmi::wallStamp().substr(0, 19);
    }, "recipe:" + std::to_string(record) + ":" + std::to_string(field));
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    selectRecord(record);
    say(recName + " / " + f.name + " = " + (value.empty() ? std::string("(vide)") : value + (f.unit.empty() ? "" : " " + f.unit)));
    return true;
}

bool HmiRecipesPane::importCsv(Id id, const std::string& path, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto fail = [&](std::string reason) { if (why) *why = reason; say("Import impossible : " + reason, true); return false; };
    if (!r) return fail("aucune recette choisie");
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("fichier illisible : " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    // Un essai sur une copie : un fichier refuse ne laisse pas de commande vide.
    hmi::Project trial = doc_->project;
    hmi::RecipeImport rep;
    std::string error;
    if (!hmi::importRecipeCsv(trial, id, text, &rep, &error)) return fail(error);
    bool ok = true;
    auto cmd = hmi::changeProject(doc_, "Importer des jeux dans " + r->name, [&](hmi::Project& pr) {
        ok = hmi::importRecipeCsv(pr, id, text, nullptr, nullptr);
    });
    if (!ok) return fail("import refus\xC3\xA9");
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectRecipe(id);
    std::string msg = "Import\xC3\xA9 : " + std::to_string(rep.added) + " jeu(x) ajout\xC3\xA9(s), " + std::to_string(rep.replaced)
                    + " remplac\xC3\xA9(s)";
    if (!rep.warnings.empty()) msg += " \xC2\xB7 " + rep.warnings.front() + (rep.warnings.size() > 1 ? " (+" + std::to_string(rep.warnings.size() - 1) + ")" : "");
    say(msg, !rep.warnings.empty());
    return true;
}

bool HmiRecipesPane::exportCsv(Id id, const std::string& path, std::string* why) {
    const auto* r = doc_->project.recipe(id);
    const auto fail = [&](std::string reason) { if (why) *why = reason; say("Export impossible : " + reason, true); return false; };
    if (!r) return fail("aucune recette choisie");
    const std::string csv = hmi::recipeCsv(*r);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return fail("impossible d'\xC3\xA9" "crire " + path);
    out << csv;
    out.close();
    if (!out) return fail("\xC3\xA9" "criture interrompue : " + path);
    say(r->name + " export\xC3\xA9" "e : " + std::to_string(r->records.size()) + " jeu(x) dans " + path);
    return true;
}

std::size_t HmiRecipesPane::compare(Id id, Id left, Id right) {
    const auto* r = doc_->project.recipe(id);
    const auto* a = r ? r->record(left) : nullptr;
    const auto* b = r ? r->record(right) : nullptr;
    if (!a || !b) { say("Comparer : choisis deux jeux de la m\xC3\xAAme recette", true); return 0; }
    diff_ = hmi::compareValues(*r, a->values, b->values);
    diffTitle_ = a->name + "|" + b->name;
    shownRecipe_ = id;
    std::size_t n = 0;
    for (const auto& d : diff_) n += d.differs;
    refreshDetail();
    tabs_->setCurrentIndex(1);
    say(a->name + " / " + b->name + " : " + (n ? std::to_string(n) + " valeur(s) diff\xC3\xA8rent" : std::string("identiques")), n > 0);
    return n;
}

void HmiRecipesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    // Lot API 8 : la recherche sous la barre, comme celle des alarmes.
    search_->setBounds({b.x + 8, b.y + 42, std::max(0.f, std::min(560.f, b.w - 16)), 28});
    split_->setBounds({b.x, b.y + 76, b.w, std::max(0.f, b.h - 100)});
}

void HmiRecipesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
