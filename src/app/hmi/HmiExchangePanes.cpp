// IHM > Configuration > Historiques, et IHM > Exporter / Importer.
#include "HmiSupervisionPanes.hpp"
#include "../ExportTarget.hpp"
#include "HmiAssist.hpp"
#include "HmiImages.hpp"                   // lot 13 : hmiTextMeasure
#include "HmiPainter.hpp"                  // lot 13 : les vignettes du dossier
#include "../Capture.hpp"                  // lot 13 : le JPEG des vignettes
#include "../../hmi/HmiDossier.hpp"        // lot 13 : le dossier de l'IHM

#include "HmiPaneKit.hpp"

#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;
using namespace hmikit;

namespace {

// Lot 7 : l'endroit choisi pour le dossier Word / PDF, du clic au dessin qui l'ecrit.
std::string& pendingDossierTarget() {
    static std::string target;
    return target;
}

std::string bytesText(std::uint64_t n) {
    if (n < 1024) return std::to_string(n) + " o";
    if (n < 1024 * 1024) return std::to_string((n + 512) / 1024) + " Ko";
    return hmi::formatNumber(static_cast<double>(n * 10 / (1024 * 1024)) / 10.0) + " Mo";
}

// "2026-09-22 07:40:12.350" -> "22/09 07:40:12" : la colonne tient, la date
// reste (un historique couvre des jours).
std::string shortStamp(const std::string& s) {
    if (s.size() < 19 || s[4] != '-') return s.empty() ? std::string("-") : s;
    return s.substr(8, 2) + "/" + s.substr(5, 2) + " " + s.substr(11, 8);
}

ui::Tone priorityTone(int p) {
    return p == 1 ? ui::Tone::Error : p == 2 ? ui::Tone::Warning : p == 3 ? ui::Tone::Accent : ui::Tone::Muted;
}

// Les evenements : couleur par genre (une alarme qui apparait, un acces refuse...).
ui::Tone eventTone(const std::string& kind) {
    if (kind.find("Appar") != std::string::npos || kind.find("refus") != std::string::npos || kind == "Erreur") return ui::Tone::Error;
    if (kind.find("Acquitt") != std::string::npos) return ui::Tone::Warning;
    if (kind.find("Dispar") != std::string::npos) return ui::Tone::Ok;
    if (kind.find("Connexion") != std::string::npos || kind.find("connexion") != std::string::npos) return ui::Tone::Accent;
    return ui::Tone::Muted;
}

enum HistoryAction : int { HRefresh = 1, HExport, HClear, HVerify };

// Lot 13 : le journal d'audit - un refus, un verrou en rouge ; une signature en violet.
ui::Tone auditTone(const hmi::AuditEntry& e) {
    if (e.kind.find("refus") != std::string::npos || e.kind == "Verrouillage") return ui::Tone::Error;
    if (!e.signature.empty() || e.kind.rfind("Signature", 0) == 0) return ui::Tone::Accent;
    if (e.kind == "Connexion" || e.kind.rfind("D\xC3\xA9" "connexion", 0) == 0) return ui::Tone::Ok;
    return ui::Tone::Muted;
}
enum ExchangeAction : int { XExport = 1, XImport, XCheck, XWord, XPdf };

// Lot 13 : la vignette de chaque vue et popup, dessinee hors de l'ecran (comme
// l'editeur la montre) puis gardee en JPEG. Vide : le renderer ne sait pas.
std::map<Id, hmi::DossierImage> viewThumbnails(gfx::IRenderer& r, const hmi::Project& p, const ui::Theme& theme) {
    std::map<Id, hmi::DossierImage> out;
    for (const auto& v : p.views) {
        if ((v.role != "vue" && v.role != "popup") || v.width <= 0 || v.height <= 0) continue;
        const int w = 960;
        const int h = std::clamp(static_cast<int>(std::lround(960.0 * v.height / v.width)), 40, 1200);
        if (!r.beginOffscreen(w, h, gfx::Color::rgb(0xFFFFFF))) break;
        paintHmiViewPreview(r, p, v, {0.f, 0.f, static_cast<float>(w), static_cast<float>(h)}, theme);
        std::vector<std::uint8_t> rgba;
        int rw = 0, rh = 0;
        if (!r.endOffscreen(rgba, rw, rh)) continue;
        hmi::DossierImage img;
        img.name = v.name;
        img.width = rw;
        img.height = rh;
        img.caption = v.name + " \xE2\x80\x94 " + std::to_string(v.width) + " \xC3\x97 " + std::to_string(v.height) + " px";
        if (encodeJpeg(rgba, rw, rh, 85, img.jpeg)) out[v.id] = std::move(img);
    }
    return out;
}

} // namespace

// ================================================================ historiques ==
HmiHistoryPane::HmiHistoryPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(HRefresh, HmiGlyph::Refresh, "Relire l'historique", "Relire");
    tools->add(HExport, HmiGlyph::Export, "Exporter l'onglet affich\xC3\xA9 en CSV", "Exporter CSV");
    tools->add(HClear, HmiGlyph::Delete, "Vider l'historique (ne s'annule pas ; le journal d'audit reste)", "Vider");
    tools->separator();
    tools->add(HVerify, HmiGlyph::Audit, "V\xC3\xA9rifier la cha\xC3\xAEne d'empreintes du journal d'audit (une ligne retouch\xC3\xA9" "e se voit)",
               "V\xC3\xA9rifier l'int\xC3\xA9grit\xC3\xA9");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(HClear, [this] {
        const auto& h = doc_->history;
        return !h.alarms.empty() || !h.events.empty() || !h.system.empty() || !h.samples.empty();
    });
    tools_->setEnabledWhen(HExport, [this] { return rowsIn(currentTab()) > 0; });
    tools_->setEnabledWhen(HVerify, [this] { return !doc_->history.audit.empty(); });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    grid_ = &static_cast<ui::PropertyGrid&>(split->addPane(std::make_unique<ui::PropertyGrid>(base + ".grid"), 0.30f, 260.f));
    grid_->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    const char* titles[] = {"Alarmes", "\xC3\x89v\xC3\xA9nements", "Syst\xC3\xA8me", "Mesures", "Audit"};
    const ui::Icon icons[] = {ui::Icon::Warning, ui::Icon::Document, ui::Icon::Settings, ui::Icon::Chart, ui::Icon::Lock};
    std::vector<std::vector<ui::TableView::Column>> cols = {
        {{"Apparue", 132.f}, {"Acquitt\xC3\xA9" "e", 132.f}, {"Disparue", 132.f}, {"Priorit\xC3\xA9", 100.f}, {"Groupe", 100.f},
         {"Alarme", 190.f}, {"Message", 300.f}, {"Par", 90.f}},
        {{"Heure", 132.f}, {"Type", 130.f}, {"Source", 200.f}, {"Message", 420.f}, {"Utilisateur", 100.f}},
        {{"Heure", 132.f}, {"Type", 130.f}, {"Source", 200.f}, {"Message", 420.f}, {"Utilisateur", 100.f}},
        {{"Heure", 132.f}, {"Variable", 260.f}, {"Valeur", 120.f, 50.f, true, true, true, ui::Align::End}},
        // lot 13 : le journal d'audit
        {{"Heure", 132.f}, {"Qui", 90.f}, {"Type", 120.f}, {"O\xC3\xB9", 190.f}, {"Cible", 190.f}, {"Avant", 110.f}, {"Apr\xC3\xA8s", 110.f},
         {"Motif", 150.f}, {"Signature", 150.f}, {"Empreinte", 110.f}},
    };
    for (int t = 0; t < 5; ++t) {
        auto table = std::make_unique<ui::TableView>(base + ".table" + std::to_string(t));
        table->setColumns(cols[static_cast<std::size_t>(t)]);
        table->setSelectionMode(ui::SelectionMode::Single);
        tables_[t] = table.get();
        tabs->addTab(ui::TabControl::Tab{titles[t], icons[t], false, false}, std::move(table));
    }
    tabs->setCurrentIndex(0);
    tabs_ = &static_cast<ui::TabControl&>(split->addPane(std::move(tabs), 0.70f, 360.f));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case HRefresh: for (auto& s : shown_) s = static_cast<std::size_t>(-1); refresh(); break;
            case HExport: if (hosts_.exportCsv) hosts_.exportCsv(currentTab()); break;
            case HClear:
                if (hosts_.clear) hosts_.clear();
                else (void)clearHistory();
                break;
            case HVerify:
                showTab(Audit);
                (void)verifyAudit();
                break;
            default: break;
        }
    });
    links_ += doc_->changed->connect([this](Id) { rebuildProperties(); });
    refresh();
}

void HmiHistoryPane::refresh() {
    rebuildProperties();
    rebuildTables();
    invalidate();
}

void HmiHistoryPane::showTab(int tab) { tabs_->setCurrentIndex(static_cast<std::size_t>(std::clamp(tab, 0, 4))); }

hmi::AuditCheck HmiHistoryPane::verifyAudit() {
    const auto check = hmi::verifyAudit(doc_->history.audit);
    auditVerdict_ = (check.ok ? "Journal d'audit : " : "Journal d'audit ALT\xC3\x89R\xC3\x89 : ") + check.message;
    say(auditVerdict_, !check.ok);
    rebuildProperties();
    return check;
}
int HmiHistoryPane::currentTab() const { return static_cast<int>(tabs_->currentIndex()); }

std::size_t HmiHistoryPane::rowsIn(int tab) const {
    const auto& h = doc_->history;
    switch (tab) {
        case Alarms: return h.alarms.size();
        case Events: return h.events.size();
        case System: return h.system.size();
        case Samples: return h.samples.size();
        case Audit: return h.audit.size();
        default: return 0;
    }
}

void HmiHistoryPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiHistoryPane::rebuildProperties() {
    const auto& s = doc_->project.history;
    const auto c = [this](const char* f) {
        return [this, f](std::string_view v) { return setSetting(f, std::string(v)); };
    };
    PG::Category keep;
    keep.name = "Ce qui est gard\xC3\xA9";
    keep.properties.push_back(prop("Alarmes termin\xC3\xA9" "es", tf(s.alarms), PG::ValueType::Boolean, c("alarmes"),
                                   "Chaque alarme apparue, avec ses heures d'apparition, d'acquittement et de disparition."));
    keep.properties.push_back(prop("\xC3\x89v\xC3\xA9nements", tf(s.events), PG::ValueType::Boolean, c("evenements"),
                                   "Apparitions, acquittements, connexions, recettes appliqu\xC3\xA9" "es, acc\xC3\xA8s refus\xC3\xA9s..."));
    keep.properties.push_back(prop("Historique syst\xC3\xA8me", tf(s.system), PG::ValueType::Boolean, c("systeme"),
                                   "Le journal de l'IHM : navigation, scripts, erreurs."));
    keep.properties.push_back(prop("Journal d'audit", tf(s.audit), PG::ValueType::Boolean, c("audit"),
                                   "Qui a chang\xC3\xA9 quoi, quand, o\xC3\xB9, avant et apr\xC3\xA8s, pourquoi : les variables \xC3\xA9" "crites par "
                                   "l'op\xC3\xA9rateur, les recettes, les acquittements, les connexions, les signatures. Chaque ligne "
                                   "porte l'empreinte de la pr\xC3\xA9" "c\xC3\xA9" "dente : une ligne retouch\xC3\xA9" "e se voit."));
    keep.properties.push_back(prop("Variables archiv\xC3\xA9" "es (a; b)", joinList(s.archived), PG::ValueType::Text, c("archivees"),
                                   "Mesur\xC3\xA9" "es \xC3\xA0 chaque p\xC3\xA9riode d'\xC3\xA9" "chantillonnage ; les courbes en mode "
                                   "historique les relisent. Ex. : Armoires[0].ana.PT1.mes; Armoires[1].ana.PT1.mes"));
    PG::Category lim;
    lim.name = "Limites";
    lim.properties.push_back(prop("Entr\xC3\xA9" "es par liste (max)", std::to_string(s.maxEntries), PG::ValueType::Integer, c("max"),
                                  "Les plus anciennes tombent au-del\xC3\xA0."));
    lim.properties.push_back(prop("Conservation (jours)", std::to_string(s.retentionDays), PG::ValueType::Integer, c("conservation"),
                                  "Rien de plus vieux n'est gard\xC3\xA9 \xC3\xA0 l'enregistrement. 0 : sans limite de date."));
    lim.properties.push_back(prop("\xC3\x89" "chantillonnage (ms)", std::to_string(s.samplePeriodMs), PG::ValueType::Integer, c("echantillonnage"),
                                  "La p\xC3\xA9riode des mesures des variables archiv\xC3\xA9" "es."));
    PG::Category disk;
    disk.name = "Sur le disque";
    const auto& h = doc_->history;
    disk.properties.push_back(prop("Dossier", "ihm/historique/", PG::ValueType::ReadOnly, {},
                                   "alarmes.csv, evenements.csv, systeme.csv, mesures.csv : \xC3\xA9" "crits \xC3\xA0 chaque "
                                   "enregistrement du projet, relus \xC3\xA0 l'ouverture."));
    disk.properties.push_back(prop("Alarmes termin\xC3\xA9" "es ", std::to_string(h.alarms.size()), PG::ValueType::ReadOnly));
    disk.properties.push_back(prop("\xC3\x89v\xC3\xA9nements ", std::to_string(h.events.size()), PG::ValueType::ReadOnly));
    disk.properties.push_back(prop("Lignes syst\xC3\xA8me", std::to_string(h.system.size()), PG::ValueType::ReadOnly));
    disk.properties.push_back(prop("Mesures", std::to_string(h.samples.size()), PG::ValueType::ReadOnly));
    // Lot 13 : le journal d'audit - ses lignes, sa verification.
    PG::Category audit;
    audit.name = "Journal d'audit";
    audit.properties.push_back(prop("Lignes", std::to_string(h.audit.size()), PG::ValueType::ReadOnly, {},
                                    "ihm/historique/audit.csv : chaque ligne y est ajout\xC3\xA9" "e tout de suite (une panne ne la perd pas). "
                                    "Vider l'historique ne le vide pas."));
    audit.properties.push_back(prop("Limite", "100 000 lignes", PG::ValueType::ReadOnly, {},
                                    "Au-del\xC3\xA0, les plus anciennes tombent ; la v\xC3\xA9rification le dit. La conservation en jours ne s'applique pas."));
    audit.properties.push_back(prop("V\xC3\xA9rification", auditVerdict_.empty() ? std::string("pas encore (bouton V\xC3\xA9rifier l'int\xC3\xA9grit\xC3\xA9)") : auditVerdict_,
                                    PG::ValueType::ReadOnly, {},
                                    "Chaque ligne porte l'empreinte SHA-256 de la pr\xC3\xA9" "c\xC3\xA9" "dente et la sienne : une ligne retouch\xC3\xA9" "e, "
                                    "supprim\xC3\xA9" "e ou ins\xC3\xA9r\xC3\xA9" "e casse la cha\xC3\xAEne."));
    std::size_t locked = 0;
    for (const auto& a : h.accounts) locked += a.lockedAt.empty() ? 0 : 1;
    audit.properties.push_back(prop("Comptes verrouill\xC3\xA9s", std::to_string(locked), PG::ValueType::ReadOnly, {},
                                    "ihm/historique/comptes.csv : les \xC3\xA9" "checs de connexion de suite et les verrous "
                                    "(Configuration > Utilisateurs les d\xC3\xA9verrouille)."));
    grid_->setCategories({std::move(keep), std::move(lim), std::move(disk), std::move(audit)});
}

void HmiHistoryPane::rebuildTables() {
    const auto& h = doc_->history;
    const std::size_t sizes[5] = {h.alarms.size(), h.events.size(), h.system.size(), h.samples.size(), h.audit.size()};
    for (int t = 0; t < 5; ++t) {
        if (sizes[t] == shown_[t]) continue;
        shown_[t] = sizes[t];
        std::vector<std::vector<std::string>> rows;
        std::vector<ui::Tone> tones;
        // Les plus recentes en haut : c'est ce qu'on vient chercher.
        if (t == Alarms) {
            for (auto it = h.alarms.rbegin(); it != h.alarms.rend(); ++it) {
                rows.push_back({shortStamp(it->appeared), shortStamp(it->acked), shortStamp(it->cleared),
                                std::to_string(it->priority) + " - " + std::string(hmi::alarmPriorityLabel(it->priority)), it->group,
                                it->name, it->message, it->ackedBy});
                tones.push_back(priorityTone(it->priority));
            }
            models_[t] = std::make_shared<Rows>(std::vector<std::string>{"Apparue", "Acquitt\xC3\xA9" "e", "Disparue", "Priorit\xC3\xA9",
                                                                         "Groupe", "Alarme", "Message", "Par"},
                                                std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                                    ui::CellStyle s;
                                                    if (r < tones.size() && c == 3) s.fgTone = tones[r];
                                                    if (r < tones.size() && c == 5) { s.bold = true; s.icon = ui::Icon::Warning; s.iconTone = tones[r]; }
                                                    return s;
                                                });
        } else if (t == Events || t == System) {
            const auto& list = t == Events ? h.events : h.system;
            for (auto it = list.rbegin(); it != list.rend(); ++it) {
                rows.push_back({shortStamp(it->stamp), it->kind, it->source, it->message, it->user});
                tones.push_back(eventTone(it->kind));
            }
            models_[t] = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Type", "Source", "Message", "Utilisateur"}, std::move(rows),
                                                [tones](ui::RowIndex r, std::size_t c) {
                                                    ui::CellStyle s;
                                                    if (r < tones.size() && c == 1) s.fgTone = tones[r];
                                                    return s;
                                                });
        } else if (t == Audit) {
            // Lot 13 : les plus recentes en haut (les 2000 dernieres a l'ecran).
            std::size_t n = 0;
            for (auto it = h.audit.rbegin(); it != h.audit.rend() && n < 2000; ++it, ++n) {
                rows.push_back({shortStamp(it->stamp), it->user.empty() ? std::string("-") : it->user, it->kind, it->source, it->target,
                                it->before, it->after, it->reason, it->signature, it->hash.substr(0, 12) + "\xE2\x80\xA6"});
                tones.push_back(auditTone(*it));
            }
            models_[t] = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Qui", "Type", "O\xC3\xB9", "Cible", "Avant", "Apr\xC3\xA8s",
                                                                         "Motif", "Signature", "Empreinte"},
                                                std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                                    ui::CellStyle s;
                                                    if (r < tones.size() && c == 2) s.fgTone = tones[r];
                                                    if (c == 4) s.bold = true;
                                                    if (c == 9) s.monospace = true;
                                                    if (c == 8 && r < tones.size()) s.fgTone = ui::Tone::Accent;
                                                    return s;
                                                });
        } else {
            // Les mesures peuvent etre des milliers : les 2000 dernieres.
            std::size_t n = 0;
            for (auto it = h.samples.rbegin(); it != h.samples.rend() && n < 2000; ++it, ++n)
                rows.push_back({shortStamp(it->stamp), it->variable, hmi::formatNumber(it->value)});
            models_[t] = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Variable", "Valeur"}, std::move(rows),
                                                [](ui::RowIndex, std::size_t c) {
                                                    ui::CellStyle s;
                                                    if (c == 1) s.monospace = true;
                                                    if (c == 2) s.bold = true;
                                                    return s;
                                                });
        }
        tables_[t]->setModel(models_[t]);
        tabs_->setTabBadge(static_cast<std::size_t>(t), sizes[t] ? std::to_string(sizes[t]) : std::string{},
                           t == Alarms && sizes[t] ? ui::Tone::Warning : ui::Tone::Accent);
    }
    std::string msg = std::to_string(h.alarms.size()) + " alarme(s) termin\xC3\xA9" "e(s) \xC2\xB7 " + std::to_string(h.events.size())
                    + " \xC3\xA9v\xC3\xA9nement(s) \xC2\xB7 " + std::to_string(h.system.size()) + " ligne(s) syst\xC3\xA8me \xC2\xB7 "
                    + std::to_string(h.samples.size()) + " mesure(s)";
    if (!h.audit.empty() || doc_->project.history.audit) msg += " \xC2\xB7 " + std::to_string(h.audit.size()) + " ligne(s) d'audit";
    if (!doc_->project.history.archived.empty())
        msg += " \xC2\xB7 archiv\xC3\xA9" "es : " + fewOf(doc_->project.history.archived, 3);
    status_->setMessage(msg);
}

bool HmiHistoryPane::setSetting(const std::string& field, const std::string& raw, std::string* why) {
    const std::string value = trimmed(raw);
    hmi::HistorySettings s = doc_->project.history;
    const auto fail = [&](std::string reason) { if (why) *why = reason; say(reason, true); return false; };
    double n = 0;
    if (field == "alarmes") s.alarms = yes(value);
    else if (field == "audit") s.audit = yes(value);                 // lot 13
    else if (field == "evenements") s.events = yes(value);
    else if (field == "systeme") s.system = yes(value);
    else if (field == "archivees") {
        std::vector<std::string> list;
        for (const auto& e : splitList(value)) {
            const auto x = hmi::Expression::compile(e);
            if (!x.valid()) return fail("variable archiv\xC3\xA9" "e illisible : " + e + " (" + x.error() + ")");
            list.push_back(e);
        }
        s.archived = list;
    } else if (field == "max" || field == "conservation" || field == "echantillonnage") {
        if (!hmi::parseNumber(value, n)) return fail(field + " : un nombre");
        if (field == "max") {
            if (n < 10 || n > 1000000) return fail("entr\xC3\xA9" "es par liste : de 10 \xC3\xA0 1 000 000");
            s.maxEntries = static_cast<int>(n);
        } else if (field == "conservation") {
            if (n < 0 || n > 3650) return fail("conservation : de 0 \xC3\xA0 3650 jours");
            s.retentionDays = static_cast<int>(n);
        } else {
            if (n < 50 || n > 3600000) return fail("\xC3\xA9" "chantillonnage : de 50 ms \xC3\xA0 1 h");
            s.samplePeriodMs = static_cast<int>(n);
        }
    } else {
        return fail("champ inconnu : " + field);
    }
    if (s == doc_->project.history) return true;
    auto cmd = hmi::changeProject(doc_, "Historiques : " + field, [&](hmi::Project& p) { p.history = s; });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Historiques : " + field + " = " + (value.empty() ? std::string("(vide)") : value));
    return true;
}

bool HmiHistoryPane::exportCsv(int tab, const std::string& path, std::string* why) {
    if (tab < 0 || tab > 4) return false;
    const std::string csv = hmi::historyCsv(doc_->history, hmi::kHistoryFiles[tab]);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (why) *why = "impossible d'\xC3\xA9" "crire " + path;
        say("Export impossible : " + path, true);
        return false;
    }
    out << csv;
    out.close();
    say(std::to_string(rowsIn(tab)) + " ligne(s) export\xC3\xA9" "e(s) dans " + path);
    return true;
}

std::size_t HmiHistoryPane::clearHistory() {
    auto& h = doc_->history;
    const std::size_t n = h.alarms.size() + h.events.size() + h.system.size() + h.samples.size();
    // Lot 13 : le journal d'audit et l'etat des comptes ne se vident pas (c'est
    // leur raison d'etre) ; le reste, si.
    auto audit = std::move(h.audit);
    auto accounts = std::move(h.accounts);
    h = hmi::History{};
    h.audit = std::move(audit);
    h.accounts = std::move(accounts);
    doc_->dirty = true;     // a enregistrer : les fichiers ihm/historique/ seront vides
    for (auto& s : shown_) s = static_cast<std::size_t>(-1);
    refresh();
    say(std::to_string(n) + " entr\xC3\xA9" "e(s) effac\xC3\xA9" "e(s) ; enregistre le projet pour vider ihm/historique/");
    return n;
}

void HmiHistoryPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 40, b.w, std::max(0.f, b.h - 64)});
}

void HmiHistoryPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    // La simulation ecrit dans l'historique pendant que l'onglet est ouvert.
    const auto& h = doc_->history;
    if (h.alarms.size() != shown_[0] || h.events.size() != shown_[1] || h.system.size() != shown_[2] || h.samples.size() != shown_[3])
        rebuildTables();
}

// ========================================================= exporter / importer ==
HmiExchangePane::HmiExchangePane(std::string id, hmi::DocumentPtr doc, Apply apply, hmi::NameExists plcHasName,
                                 std::string projectFolder)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)), exists_(std::move(plcHasName)),
      folder_(std::move(projectFolder)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(XExport, HmiGlyph::Export, "Exporter tout le projet IHM dans une archive (.zip)", "Exporter");
    tools->add(XImport, HmiGlyph::Import, "Importer une archive : reconstruire le projet IHM, puis le contr\xC3\xB4ler", "Importer");
    tools->separator();
    tools->add(XCheck, HmiGlyph::Check, "Contr\xC3\xB4ler le projet tel qu'il est (comptes et G\xC3\xA9n\xC3\xA9rer)", "Contr\xC3\xB4ler");
    tools->separator();
    // Lot 13 : le dossier de l'IHM (vignettes, variables, alarmes, recettes, utilisateurs, scripts).
    tools->add(XWord, HmiGlyph::Export, "Dossier Word de l'IHM : chaque vue avec sa vignette, les variables, les alarmes, les recettes, "
                                        "les utilisateurs, les scripts (dossier exports/)", "Dossier Word");
    tools->add(XPdf, HmiGlyph::Export, "Dossier PDF de l'IHM : le m\xC3\xAAme, en PDF (dossier exports/)", "Dossier PDF");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    {
        auto t = std::make_unique<ui::TableView>(base + ".counts");
        t->setColumns({{"Section", 200.f}, {"Annonc\xC3\xA9", 100.f, 50.f, true, true, true, ui::Align::End},
                       {"Relu", 100.f, 50.f, true, true, true, ui::Align::End}, {"\xC3\x89tat", 120.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        counts_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"Contr\xC3\xB4le de coh\xC3\xA9rence", ui::Icon::Ok, false, false}, std::move(t));
    }
    {
        auto t = std::make_unique<ui::TableView>(base + ".files");
        t->setColumns({{"Fichier", 340.f}, {"Taille", 90.f, 50.f, true, true, true, ui::Align::End}, {"SHA-256", 300.f}, {"\xC3\x89tat", 100.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        files_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"Fichiers de l'archive", ui::Icon::Document, false, false}, std::move(t));
    }
    {
        auto t = std::make_unique<ui::TableView>(base + ".problems");
        t->setColumns({{"Probl\xC3\xA8me", 760.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        problems_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"Probl\xC3\xA8mes", ui::Icon::Error, false, false}, std::move(t));
    }
    {
        auto t = std::make_unique<ui::TableView>(base + ".issues");
        t->setColumns({{"Gravit\xC3\xA9", 128.f}, {"Cat\xC3\xA9gorie", 128.f}, {"O\xC3\xB9", 260.f}, {"Message", 520.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        issuesTable_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"G\xC3\xA9n\xC3\xA9rer", ui::Icon::Analyze, false, false}, std::move(t));
    }
    tabs->setCurrentIndex(0);
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case XExport: if (hosts_.exportArchive) hosts_.exportArchive(); break;
            case XImport: if (hosts_.importArchive) hosts_.importArchive(); break;
            case XCheck: check(); break;
            case XWord: case XPdf: {
                // Au prochain dessin : les vignettes se dessinent avec le renderer de l'ecran.
                // Lot 7 : d'abord OU (ExportTarget.hpp) - exports/ par defaut, le bouton ...
                // ailleurs ; la cible est gardee jusqu'au dessin qui ecrit le dossier.
                const bool pdf = a == XPdf;
                const auto start = [this, pdf] {
                    pendingDossierTarget() = exportTargetOverride();
                    pendingDossier_ = pdf ? 2 : 1;
                    say("Dossier de l'IHM : les vignettes se dessinent...");
                    invalidate();
                };
                if (!askExportTarget(pdf ? "le dossier de l'IHM (PDF)" : "le dossier de l'IHM (Word)",
                                     pdf ? "Documents PDF|*.pdf" : "Documents Word|*.docx", start))
                    start();
                break;
            }
            default: break;
        }
    });
    links_ += issuesTable_->activated->connect([this](ui::RowIndex r) {
        if (r < issues_.size() && hosts_.openIssue) hosts_.openIssue(issues_[r]);
    });
    rebuildTables();
    status_->setMessage("Exporter : une archive .zip de tout le projet IHM. Importer : le reconstruire depuis une archive, "
                        "puis le contr\xC3\xB4ler.");
}

void HmiExchangePane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 10.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiExchangePane::rebuildTables() {
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> bad;
    for (const auto& c : report_.counts) {
        rows.push_back({c.section, std::to_string(c.expected), std::to_string(c.found), c.ok() ? "identique" : "DIFF\xC3\x89RENT"});
        bad.push_back(!c.ok());
    }
    countsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Section", "Annonc\xC3\xA9", "Relu", "\xC3\x89tat"}, std::move(rows),
                                          [bad](ui::RowIndex r, std::size_t c) {
                                              ui::CellStyle s;
                                              if (r >= bad.size()) return s;
                                              if (c == 3) { s.fgTone = bad[r] ? ui::Tone::Error : ui::Tone::Ok; s.bold = bad[r]; }
                                              if (c == 0) { s.icon = bad[r] ? ui::Icon::Error : ui::Icon::Ok; s.iconTone = bad[r] ? ui::Tone::Error : ui::Tone::Ok; }
                                              return s;
                                          });
    counts_->setModel(countsModel_);

    rows.clear();
    bad.clear();
    for (const auto& e : report_.entries) {
        rows.push_back({e.path, bytesText(e.bytes), e.sha256.substr(0, 24) + "...", e.ok ? "intact" : "ALT\xC3\x89R\xC3\x89"});
        bad.push_back(!e.ok);
    }
    filesModel_ = std::make_shared<Rows>(std::vector<std::string>{"Fichier", "Taille", "SHA-256", "\xC3\x89tat"}, std::move(rows),
                                         [bad](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle s;
                                             if (c == 2) s.monospace = true;
                                             if (r < bad.size() && c == 3) s.fgTone = bad[r] ? ui::Tone::Error : ui::Tone::Ok;
                                             return s;
                                         });
    files_->setModel(filesModel_);

    rows.clear();
    for (const auto& p : report_.problems) rows.push_back({p});
    problemsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Probl\xC3\xA8me"}, std::move(rows), [](ui::RowIndex, std::size_t) {
        ui::CellStyle s;
        s.icon = ui::Icon::Error;
        s.iconTone = ui::Tone::Error;
        return s;
    });
    problems_->setModel(problemsModel_);

    rows.clear();
    std::vector<hmi::Issue::Severity> sev;
    sortIssues(issues_);                             // lot 13 : les erreurs d'abord
    for (const auto& i : issues_) {
        std::string where;
        if (const auto* v = doc_->project.view(i.view)) {
            where = v->name;
            if (const auto* o = v->object(i.object)) where += "/" + o->name;
        }
        if (!i.property.empty()) where += (where.empty() ? "" : " \xC2\xB7 ") + i.property;
        rows.push_back({std::string(hmi::toString(i.severity)), i.category, where, i.message});
        sev.push_back(i.severity);
    }
    // Lot 13 : la couleur de la gravite, comme dans Generer et Compiler.
    issuesModel_ = std::make_shared<IssueRows>(std::vector<std::string>{"Gravit\xC3\xA9", "Cat\xC3\xA9gorie", "O\xC3\xB9", "Message"},
                                               std::move(rows), std::move(sev), 3);
    issuesTable_->setModel(issuesModel_);

    std::size_t badCounts = 0, badFiles = 0;
    for (const auto& c : report_.counts) badCounts += !c.ok();
    for (const auto& e : report_.entries) badFiles += !e.ok;
    const auto ic = hmi::count(issues_);
    tabs_->setTabBadge(0, report_.counts.empty() ? std::string{} : badCounts ? std::to_string(badCounts) : std::string("ok"),
                       badCounts ? ui::Tone::Error : ui::Tone::Ok);
    tabs_->setTabBadge(1, report_.entries.empty() ? std::string{} : std::to_string(report_.entries.size()),
                       badFiles ? ui::Tone::Error : ui::Tone::Accent);
    tabs_->setTabBadge(2, report_.problems.empty() ? std::string{} : std::to_string(report_.problems.size()), ui::Tone::Error);
    tabs_->setTabBadge(3, issues_.empty() ? std::string{} : std::to_string(issues_.size()),
                       ic.errors ? ui::Tone::Error : ic.warnings ? ui::Tone::Warning : ui::Tone::Info);
    invalidate();
}

bool HmiExchangePane::exportTo(const std::string& zipPath, std::string* why) {
    hmi::ArchiveReport rep;
    auto st = hmi::exportArchive(doc_->project, &doc_->history, zipPath, &rep);
    if (!st) {
        const std::string reason = st.error().context.empty() ? st.error().message() : st.error().context;
        if (why) *why = reason;
        say("Export impossible : " + reason, true);
        return false;
    }
    report_ = std::move(rep);
    issues_.clear();
    archive_ = zipPath;
    operation_ = "Export\xC3\xA9" "e";
    stamp_ = hmi::wallStamp().substr(0, 19);
    rebuildTables();
    tabs_->setCurrentIndex(1);
    say("Archive \xC3\xA9" "crite : " + zipPath + " (" + std::to_string(report_.entries.size() + 1) + " fichiers, "
        + bytesText(report_.archiveBytes) + ")");
    return true;
}

bool HmiExchangePane::importFrom(const std::string& zipPath, std::string* why) {
    hmi::ArchiveReport rep;
    hmi::History history;
    hmi::LoadReport load;
    auto project = hmi::importArchive(zipPath, &history, &rep, &load);
    if (!project) {
        const std::string reason = project.error().context.empty() ? project.error().message() : project.error().context;
        if (why) *why = reason;
        report_ = std::move(rep);
        issues_.clear();
        archive_ = zipPath;
        operation_ = "Import refus\xC3\xA9";
        rebuildTables();
        say("Import impossible : " + reason, true);
        return false;
    }
    for (const auto& w : load.warnings) rep.problems.push_back("lecture : " + w);
    // LA RECONSTRUCTION : une seule commande, qui remplace le projet entier.
    // Ctrl+Z rend celui d'avant.
    const hmi::Project imported = std::move(*project);
    auto cmd = hmi::changeProject(doc_, "Importer l'archive " + zipPath.substr(zipPath.find_last_of("/\\") + 1),
                                  [&](hmi::Project& p) { p = imported; });
    if (cmd) apply_(std::move(cmd));
    doc_->history = std::move(history);
    doc_->dirty = true;
    report_ = std::move(rep);
    archive_ = zipPath;
    operation_ = "Import\xC3\xA9" "e";
    stamp_ = hmi::wallStamp().substr(0, 19);
    {
        hmi::GenerateOptions opt;                     // lot 13 : les polices de l'ecran
        opt.projectFolder = folder_;
        opt.measure = hmiTextMeasure(doc_->project);
        issues_ = hmi::generateWith(doc_->project, exists_, opt);
    }
    rebuildTables();
    const auto ic = hmi::count(issues_);
    tabs_->setCurrentIndex(report_.coherent() ? 0 : 2);
    say(std::string(report_.coherent() ? "Projet reconstruit, coh\xC3\xA9rent" : "Projet reconstruit avec des probl\xC3\xA8mes")
            + " \xC2\xB7 G\xC3\xA9n\xC3\xA9rer : " + std::to_string(ic.errors) + " erreur(s), " + std::to_string(ic.warnings)
            + " avertissement(s) \xC2\xB7 Ctrl+Z rend le projet d'avant",
        !report_.coherent() || ic.errors > 0);
    return true;
}

void HmiExchangePane::check() {
    report_ = hmi::ArchiveReport{};
    report_.counts = hmi::projectCounts(doc_->project, &doc_->history);
    report_.projectName = doc_->project.config.name;
    {
        hmi::GenerateOptions opt;                     // lot 13 : les polices de l'ecran
        opt.projectFolder = folder_;
        opt.measure = hmiTextMeasure(doc_->project);
        issues_ = hmi::generateWith(doc_->project, exists_, opt);
    }
    operation_ = "Contr\xC3\xB4le";
    stamp_ = hmi::wallStamp().substr(0, 19);
    rebuildTables();
    tabs_->setCurrentIndex(3);
    const auto ic = hmi::count(issues_);
    say("Contr\xC3\xB4le : " + std::to_string(ic.errors) + " erreur(s), " + std::to_string(ic.warnings) + " avertissement(s), "
        + std::to_string(ic.infos) + " information(s)", ic.errors > 0);
}

void HmiExchangePane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    constexpr float kHeader = 132.f;
    tabs_->setBounds({b.x, b.y + 40 + kHeader, b.w, std::max(0.f, b.h - 64 - kHeader)});
}

bool HmiExchangePane::makeDossier(gfx::IRenderer* renderer, const ui::Theme* theme, bool pdf, std::string* where) {
    const auto& p = doc_->project;
    std::map<Id, hmi::DossierImage> thumbs;
    if (renderer && theme) thumbs = viewThumbnails(*renderer, p, *theme);
    hmi::DossierOptions opt;
    opt.reports = &doc_->reports;
    const hmi::Dossier d = hmi::buildDossier(p, thumbs, opt);
    hmi::ExportRequest rq;
    std::string name = "dossier_IHM_" + p.config.name + "_" + hmi::wallStamp().substr(0, 10);
    for (auto& ch : name)
        if (std::string_view("\\/:*?\"<>| ").find(ch) != std::string_view::npos) ch = '_';
    rq.fileName = name + (pdf ? ".pdf" : ".docx");
    rq.format = pdf ? "PDF" : "Word";
    rq.source = "dossier";
    rq.rows = d.blocks.size();
    rq.data = std::make_shared<const hmi::Bytes>(pdf ? hmi::dossierPdf(d) : hmi::dossierDocx(d));
    rq.origin = "IHM > Exporter / Importer";
    std::string path;
    const bool ok = hosts_.writeFile && hosts_.writeFile(rq, &path);
    lastDossier_ = ok ? path : std::string{};
    lastDossierImages_ = d.images.size();
    if (where) *where = path;
    say(ok ? "Dossier de l'IHM \xC3\xA9" "crit : " + path + " (" + std::to_string(d.images.size()) + " vignette(s), "
                 + std::to_string(rq.data->size() / 1024) + " Ko)"
           : "Dossier impossible \xC3\xA0 \xC3\xA9" "crire" + (path.empty() ? std::string{} : " : " + path),
        !ok);
    return ok;
}

void HmiExchangePane::onPaint(const ui::PaintContext& ctx) {
    if (pendingDossier_) {
        const bool pdf = pendingDossier_ == 2;
        pendingDossier_ = 0;
        // L'endroit choisi (lot 7) : le temps de l'ecriture.
        exportTargetOverride() = std::move(pendingDossierTarget());
        pendingDossierTarget().clear();
        (void)makeDossier(&ctx.r, &ctx.theme, pdf, nullptr);
        exportTargetOverride().clear();
    }
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    // L'en-tete : ce que contient une archive, et la derniere operation.
    const gfx::Rect box{b.x + 10, b.y + 46, b.w - 20, 120};
    ctx.r.fillRect(box, c.windowBg);
    ctx.r.strokeRect(box, c.border, 1.f);
    const auto& font = ctx.theme.font.ui;
    const auto& small = ctx.theme.font.smallUi;
    const float lh = ctx.r.lineHeight(font) + 4;
    float y = box.y + 10;
    const auto line = [&](const std::string& label, const std::string& value, gfx::Color col) {
        ctx.r.drawText({box.x + 14, y}, label, small, c.textMuted);
        ctx.r.drawText({box.x + 170, y}, value, font, col);
        y += lh;
    };
    const auto st = doc_->project.statistics();
    line("Le projet", doc_->project.config.name + " : " + std::to_string(st.views) + " vue(s), " + std::to_string(st.objects)
                          + " objet(s), " + std::to_string(doc_->project.assets.resources.size()) + " ressource(s), "
                          + std::to_string(st.scripts) + " script(s), " + std::to_string(doc_->project.alarms.size()) + " alarme(s), "
                          + std::to_string(doc_->project.recipes.size()) + " recette(s), "
                          + std::to_string(doc_->project.security.users.size()) + " utilisateur(s)",
         c.text);
    line("Une archive contient", "manifeste.txt (comptes + empreintes SHA-256), ihm/ihm.txt, ihm/vues, ihm/ressources, ihm/scripts, "
                                 "ihm/historique",
         c.textMuted);
    // Lot 13 : le dernier dossier de l'IHM ecrit (Word ou PDF).
    if (!lastDossier_.empty())
        line("Dernier dossier", lastDossier_ + "  \xC2\xB7  " + std::to_string(lastDossierImages_) + " vignette(s)", c.text);
    if (archive_.empty()) {
        line("Derni\xC3\xA8re op\xC3\xA9ration", operation_, c.textMuted);
        return;
    }
    line("Derni\xC3\xA8re op\xC3\xA9ration", operation_ + (stamp_.empty() ? std::string{} : "  \xC2\xB7  " + stamp_) + "  \xC2\xB7  " + archive_
                                         + (report_.archiveBytes ? "  (" + bytesText(report_.archiveBytes) + ")" : std::string{}),
         c.text);
    const bool ok = report_.coherent();
    const auto ic = hmi::count(issues_);
    std::string verdict = ok ? "COH\xC3\x89RENT : chaque compte annonc\xC3\xA9 est retrouv\xC3\xA9, chaque fichier est intact"
                             : std::to_string(report_.problems.size()) + " PROBL\xC3\x88ME(S) : voir l'onglet Probl\xC3\xA8mes";
    if (operation_ == "Export\xC3\xA9" "e") verdict = "\xC3\x89" "crite : " + std::to_string(report_.entries.size()) + " fichier(s) + le manifeste";
    line("R\xC3\xA9sultat", verdict, ok ? c.ok : c.error);
    if (!issues_.empty() || operation_ == "Import\xC3\xA9" "e")
        line("G\xC3\xA9n\xC3\xA9rer", std::to_string(ic.errors) + " erreur(s), " + std::to_string(ic.warnings) + " avertissement(s), "
                                         + std::to_string(ic.infos) + " information(s)",
             ic.errors ? c.error : ic.warnings ? c.warning : c.ok);
}

} // namespace app
