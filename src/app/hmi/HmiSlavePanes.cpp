// app/hmi/HmiSlavePanes.cpp - Configuration > Equipements, 1.9 : l'esclave
// simule lie (le jumeau des lots 17-18) a l'ecran - sa ligne et sa fiche (ce
// qu'il reprend du vrai, au cadenas ; ce qui est a lui ; en ce moment), la carte
// violette et ses boutons, Detacher ; dans la fiche du vrai, ce que l'IHM lit ;
// les bascules (le message du volet, l'avis) ; les reperes des lectures simulees.
#include "HmiCommPanes.hpp"

#include "HmiEquipmentHost.hpp"
#include "HmiPaneKit.hpp"
#include "HmiSimMarks.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiTwin.hpp"
#include "../../hmi/HmiZones.hpp"
#include "../../platform/Renderer.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using PG = ui::PropertyGrid;
namespace eq = hmi::equip;

namespace {

// "14:02:31" : une heure murale, en heure locale.
std::string clockText(double wall) {
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

// "1 variable", "2 variables".
std::string count(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Des mots, a la ligne a `width` (mesures de la plateforme : la mise en page se fait hors du dessin).
std::vector<std::string> wrapWords(const std::string& text, gfx::FontId font, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tried, font) > width) {
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

constexpr gfx::FontId kCardFont{13};
constexpr float kCardPad = 10.f;

std::string exceptionText(int code) {
    if (code == 0) return "aucune";
    char b[8];
    std::snprintf(b, sizeof b, "%02X", code);
    return b;
}

} // namespace

// ============================================================ la carte violette ===
HmiSlaveCard::HmiSlaveCard(std::string id) : ui::Widget(std::move(id)) {
    static const char* const kLabels[] = {"Valeurs simul\xC3\xA9" "es", "Carte m\xC3\xA9moire", "Outil Modbus", "D\xC3\xA9tacher\xE2\x80\xA6"};
    static const char* const kTips[] = {
        "Ses valeurs simul\xC3\xA9" "es : animer, la zone de mouvement, forcer (Configuration \xE2\x80\xBA \xC3\x89quipements \xE2\x80\xBA Valeurs simul\xC3\xA9" "es)",
        "Sa carte m\xC3\xA9moire : ses cases en direct, ses comportements", "L'outil Modbus, qui vise l'esclave (127.0.0.1)",
        "D\xC3\xA9tacher : il devient un esclave seulement simul\xC3\xA9, ind\xC3\xA9pendant ; le vrai appareil perd son clone (Ctrl+Z revient)"};
    const ui::Icon kIcons[] = {ui::Icon::Play, ui::Icon::LocatedVariable, ui::Icon::Settings, ui::Icon::None};
    for (int i = 0; i < 4; ++i) {
        auto b = std::make_unique<ui::Button>(kLabels[i], this->id() + ".b" + std::to_string(i));
        if (kIcons[i] != ui::Icon::None) b->setIcon(kIcons[i]);
        b->setTooltip(kTips[i]);
        auto* raw = b.get();
        buttons_[static_cast<std::size_t>(i)] = &static_cast<ui::Button&>(addChild(std::move(b)));
        links_ += raw->clicked->connect([this, i] { clicked->emit(i); });
    }
}

void HmiSlaveCard::setEquipment(std::string name) {
    if (name == equipment_) return;
    equipment_ = std::move(name);
    invalidateLayout();
    invalidate();
}

std::string HmiSlaveCard::title() const { return "Esclave simul\xC3\xA9 li\xC3\xA9 \xC3\xA0 " + equipment_; }

std::string HmiSlaveCard::body() {
    return "Sa configuration suit le vrai appareil : pour la changer, change le vrai (la ligne au-dessus). Ce qui est \xC3\xA0 lui : sa marche, "
           "ses pannes, son temps de r\xC3\xA9ponse et ses valeurs.";
}

float HmiSlaveCard::heightFor(float width) const {
    const float inner = std::max(60.f, width - 2.f * kCardPad - 8.f);
    const float lh = ui::lineHeight(kCardFont) + 3.f;
    const auto lines = wrapWords(body(), kCardFont, inner);
    // Les boutons : autant de rangees qu'il en faut.
    float x = 0.f, rows = 1.f;
    for (const auto* b : buttons_) {
        const float w = b->sizeHint().preferred.w;
        if (x > 0.f && x + w > inner) {
            rows += 1.f;
            x = 0.f;
        }
        x += w + 6.f;
    }
    return 4.f + kCardPad + 22.f + lh * static_cast<float>(lines.size()) + 8.f + rows * 32.f + kCardPad + 4.f;
}

void HmiSlaveCard::onLayout() {
    const auto b = bounds();
    const gfx::Rect card{b.x + 4.f, b.y + 4.f, b.w - 8.f, b.h - 8.f};
    const float inner = std::max(60.f, card.w - 2.f * kCardPad);
    const float lh = ui::lineHeight(kCardFont) + 3.f;
    const auto lines = wrapWords(body(), kCardFont, inner);
    float y = card.y + kCardPad + 22.f + lh * static_cast<float>(lines.size()) + 8.f;
    float x = card.x + kCardPad;
    for (auto* btn : buttons_) {
        const float w = btn->sizeHint().preferred.w;
        if (x > card.x + kCardPad && x + w > card.right() - kCardPad) {
            x = card.x + kCardPad;
            y += 32.f;
        }
        btn->setBounds({x, y, w, 28.f});
        x += w + 6.f;
    }
}

void HmiSlaveCard::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto b = bounds();
    r.fillRect(b, ctx.theme.color.panelBg);
    const gfx::Rect card{b.x + 4.f, b.y + 4.f, b.w - 8.f, b.h - 8.f};
    const bool dark = ctx.theme.isDark();
    r.fillRoundedRect(card, simmark::kLine, 6.f);
    r.fillRoundedRect({card.x + 1.f, card.y + 1.f, card.w - 2.f, card.h - 2.f}, dark ? simmark::kCard : gfx::Color::rgb(0xF1F0FD), 5.f);
    const gfx::Color violet = simmark::text(ctx.theme);
    simmark::drawLockBadge(r, {card.x + kCardPad, card.y + kCardPad + 2.f, 16.f, 16.f});
    const float ty = card.y + kCardPad + 10.f - ctx.r.lineHeight(kCardFont) * 0.5f;
    const std::string t = simmark::fitText(r, title(), kCardFont, card.w - 2.f * kCardPad - 24.f);
    r.drawText({card.x + kCardPad + 23.f, ty}, t, kCardFont, violet);
    r.drawText({card.x + kCardPad + 23.6f, ty}, t, kCardFont, violet);
    const float lh = ui::lineHeight(kCardFont) + 3.f;
    float y = card.y + kCardPad + 22.f;
    for (const auto& l : wrapWords(body(), kCardFont, std::max(60.f, card.w - 2.f * kCardPad))) {
        r.drawText({card.x + kCardPad, y}, l, kCardFont, ctx.theme.color.text);
        y += lh;
    }
}

// =============================================================== la selection ===
void HmiCommPane::selectSlave(const std::string& name) {
    const auto* e = doc_->project.equipmentByName(name);
    if (!e || !e->linkedSlave()) {
        selectEquipment(name);
        return;
    }
    equipment_ = e->name;
    slaveRow_ = true;
    portSide_ = false;
    say(e->twinLabel() + " : li\xC3\xA9 au vrai appareil ; sa configuration le suit.");
    if (static_cast<int>(tabs_->currentIndex()) != TEquipments) tabs_->setCurrentIndex(TEquipments);
    refreshEquipments();
    refreshDiagram();
    rebuildProperties();
}

std::string HmiCommPane::simulatedReadsText() const {
    auto* h = host();
    if (!h) return {};
    std::size_t n = 0;
    for (const auto& st : h->statuses()) n += st.enabled && st.viaTwin ? 1 : 0;
    if (!n) return {};
    return n > 1 ? std::to_string(n) + " \xC3\xA9quipements lus en simul\xC3\xA9" : std::string("1 \xC3\xA9quipement lu en simul\xC3\xA9");
}

// ============================================================ les reperes ===
bool HmiCommPane::setSimMarks(bool on, std::string* why) {
    if (doc_->project.station.simMarks == on) return true;
    if (!changeProject(on ? "Rep\xC3\xA9rer les lectures simul\xC3\xA9" "es" : "Ne plus rep\xC3\xA9rer les lectures simul\xC3\xA9" "es",
                       [&](hmi::Project& x) { x.station.simMarks = on; })) {
        if (why) *why = "impossible de changer les rep\xC3\xA8res";
        return false;
    }
    say(on ? std::string("En marche, une valeur lue sur un esclave simul\xC3\xA9 se rep\xC3\xA8re : un cadre violet en tirets, une pastille, le bandeau "
                         "LECTURES SIMUL\xC3\x89" "ES (le poste et Simuler l'IHM).")
           : std::string("Les lectures simul\xC3\xA9" "es ne se rep\xC3\xA8rent plus (ni cadre, ni pastille, ni bandeau) : un poste de formation, "
                         "tout simul\xC3\xA9 ?"));
    return true;
}

// ============================================================ la fiche du vrai ===
// "Son esclave simule" : la case, son nom, ce qu'il reprend du vrai, sa marche.
void HmiCommPane::rebuildCloneProperties(std::vector<PG::Category>& cats, const hmi::Equipment& e) {
    const std::string name = e.name;
    const auto field = [this, name](const char* key) {
        return [this, name, key](std::string_view v) {
            message_.clear();
            return setEquipmentField(name, key, std::string(v));
        };
    };
    PG::Category c;
    c.name = "Son esclave simul\xC3\xA9";
    c.accent = simmark::kMark;
    c.properties.push_back(prop("Cloner en esclave simul\xC3\xA9", tf(e.twin), PG::ValueType::Boolean, field("esclave_simule"),
                                "Coch\xC3\xA9 : un esclave Modbus dans l'application, avec la configuration du vrai appareil (esclave, zones, "
                                "variables, ordre des mots) - sa ligne est sous le vrai, au cadenas : il est li\xC3\xA9. D\xC3\xA9" "coch\xC3\xA9 : il est "
                                "retir\xC3\xA9 (Ctrl+Z le rend, avec ses valeurs anim\xC3\xA9" "es et forc\xC3\xA9" "es)."));
    if (e.twin) {
        auto label = prop("Esclave simul\xC3\xA9", e.twinLabel(), PG::ValueType::ReadOnly, {}, "Sa ligne, sous le vrai : sa fiche \xC3\xA0 lui (sa marche, ses pannes, ses valeurs).");
        label.valueColor = simmark::kText;
        c.properties.push_back(std::move(label));
        const auto vars = eq::boundVariables(doc_->project, e);
        auto taken = prop("Repris du vrai (li\xC3\xA9s)", "esclave, zones, " + count(vars.size(), "variable", "variables") + ", ordre des mots",
                          PG::ValueType::ReadOnly, {}, "Sa configuration suit celle du vrai appareil : pour la changer, change le vrai.");
        taken.locked = true;
        taken.lockTint = simmark::kLine;
        c.properties.push_back(std::move(taken));
        c.properties.push_back(prop("En marche", tf(e.twinRunning), PG::ValueType::Boolean, field("jumeau_marche"),
                                    "D\xC3\xA9" "coch\xC3\xA9 : il ne r\xC3\xA9pond plus (l'IHM ne peut plus basculer sur lui)."));
        auto state = prop("\xC3\x89tat", twinStateText(e), PG::ValueType::ReadOnly);
        state.valueColor = simmark::kText;
        c.properties.push_back(std::move(state));
    }
    cats.push_back(std::move(c));
}

// "L'IHM lit" : dans l'application, la bascule, le retour, le poste, maintenant.
void HmiCommPane::rebuildReadProperties(std::vector<PG::Category>& cats, const hmi::Equipment& e) {
    const std::string name = e.name;
    const auto field = [this, name](const char* key) {
        return [this, name, key](std::string_view v) {
            message_.clear();
            return setEquipmentField(name, key, std::string(v));
        };
    };
    PG::Category c;
    c.name = "L'IHM lit";
    std::vector<std::string> sources;
    for (const auto k : {hmi::ReadSource::Real, hmi::ReadSource::Slave, hmi::ReadSource::Auto}) sources.emplace_back(hmi::readSourceLabel(k));
    c.properties.push_back(prop("Dans l'application", std::string(hmi::readSourceLabel(e.appRead())), PG::ValueType::Enum, field("lecture_appli"),
                                "Ce que l'IHM lit dans l'application (la simulation de l'IHM, la conception) : le vrai appareil ; l'esclave "
                                "simul\xC3\xA9 (toutes ses valeurs sont simul\xC3\xA9" "es) ; ou automatique - le vrai, l'esclave s'il ne r\xC3\xA9pond pas "
                                "pendant \xC2\xAB Basculer apr\xC3\xA8s \xC2\xBB, le vrai d\xC3\xA8s qu'il r\xC3\xA9pond.",
                                sources));
    c.properties.push_back(prop("Basculer apr\xC3\xA8s (s)", std::to_string(e.fallbackAfterS), PG::ValueType::Integer, field("bascule_apres"),
                                "Automatique : l'esclave apr\xC3\xA8s N secondes sans r\xC3\xA9ponse du vrai (1 \xC3\xA0 3600). Avant, ses valeurs gardent "
                                "la derni\xC3\xA8re lue (et leur qualit\xC3\xA9 le dit)."));
    c.properties.push_back(prop("Revenir au vrai", e.fallbackReturn ? "d\xC3\xA8s qu'il r\xC3\xA9pond" : "non : l'esclave jusqu'au red\xC3\xA9marrage",
                                PG::ValueType::Enum, field("bascule_retour"),
                                "Pendant la bascule, le vrai est essay\xC3\xA9 toutes les 5 s : deux r\xC3\xA9ponses de suite et l'IHM revient \xC3\xA0 lui ; "
                                "ou elle reste sur l'esclave jusqu'au red\xC3\xA9marrage de l'IHM.",
                                {"d\xC3\xA8s qu'il r\xC3\xA9pond", "non : l'esclave jusqu'au red\xC3\xA9marrage"}));
    std::vector<std::string> station;
    for (const auto k : {hmi::StationRead::Real, hmi::StationRead::Auto, hmi::StationRead::Admin}) station.emplace_back(hmi::stationReadLabel(k));
    c.properties.push_back(prop("Sur le poste d'exploitation", std::string(hmi::stationReadLabel(e.stationRead)), PG::ValueType::Enum, field("lecture_poste"),
                                "Sur le poste : toujours le vrai appareil (au d\xC3\xA9part) ; automatique (comme dans l'application) ; ou au choix "
                                "d'un administrateur, sur la page Simulation de Param\xC3\xA8tres syst\xC3\xA8me (Ctrl+Alt+S).",
                                station));
    auto now = prop("Maintenant", readNowText(e), PG::ValueType::ReadOnly);
    if (auto* h = host(); h && e.enabled)
        if (const auto st = h->status(e.name); st && st->viaTwin) now.valueColor = simmark::kText;
    c.properties.push_back(std::move(now));
    cats.push_back(std::move(c));
}

std::string HmiCommPane::readNowText(const hmi::Equipment& e) const {
    if (!e.enabled) return "rien (d\xC3\xA9sactiv\xC3\xA9)";
    auto* h = host();
    const auto st = h ? h->status(e.name) : std::nullopt;
    if (!st) return std::string(hmi::readSourceLabel(e.appRead() == hmi::ReadSource::Slave ? hmi::ReadSource::Slave : hmi::ReadSource::Real));
    if (st->viaTwin) {
        if (st->fallback) return "l'esclave simul\xC3\xA9 (bascule \xC3\xA0 " + clockText(st->readSince) + ")";
        if (st->chosen) return "l'esclave simul\xC3\xA9 (page Simulation)";
        return "l'esclave simul\xC3\xA9" + (st->readSince > 0 ? " (depuis " + clockText(st->readSince) + ")" : std::string{});
    }
    if (st->readMode == "auto" && !st->readWhy.empty()) return "le vrai appareil (" + st->readWhy + ")";
    return st->chosen ? "le vrai appareil (page Simulation)" : "le vrai appareil";
}

// ========================================================= la fiche de l'esclave ===
// Ce qui est a lui : son nom, sa marche, son port, son depart, sa reponse, ses
// pannes, ses valeurs (l'esclave lie, ou l'equipement seulement simule).
void HmiCommPane::rebuildOwnSlaveProperties(std::vector<PG::Category>& cats, const hmi::Equipment& e, const std::string& title) {
    const std::string name = e.name;
    const auto field = [this, name](const char* key) {
        return [this, name, key](std::string_view v) {
            message_.clear();
            return setEquipmentField(name, key, std::string(v));
        };
    };
    PG::Category t;
    t.name = title;
    t.accent = simmark::kMark;
    t.properties.push_back(prop("Nom affich\xC3\xA9", e.twinLabel(), PG::ValueType::Text, field("jumeau_nom"),
                                "Son nom dans la liste, sur le sch\xC3\xA9ma du r\xC3\xA9seau simul\xC3\xA9 et la page Simulation (vide : \xC2\xAB <nom> \xC2\xB7 esclave simul\xC3\xA9 \xC2\xBB)."));
    t.properties.push_back(prop("En marche", tf(e.twinRunning), PG::ValueType::Boolean, field("jumeau_marche")));
    auto* h = host();
    const int port = h ? h->simulatedPort(e.name) : 0;
    t.properties.push_back(prop("Port local", port ? "127.0.0.1:" + std::to_string(port) : std::string("arr\xC3\xAAt\xC3\xA9"), PG::ValueType::ReadOnly, {},
                                "Son serveur Modbus dans ce PC (un port libre, pris au d\xC3\xA9marrage) : l'IHM, l'outil Modbus, la carte m\xC3\xA9moire y lisent."));
    std::vector<std::string> starts;
    for (const auto k : {hmi::TwinStart::Zeros, hmi::TwinStart::Initial, hmi::TwinStart::Saved}) starts.emplace_back(hmi::twinStartLabel(k));
    t.properties.push_back(prop("Au d\xC3\xA9marrage", std::string(hmi::twinStartLabel(e.twinStart)), PG::ValueType::Enum, field("jumeau_depart"),
                                "Sa m\xC3\xA9moire au d\xC3\xA9marrage : des z\xC3\xA9ros, les valeurs initiales des variables li\xC3\xA9" "es, ou la m\xC3\xA9moire gard\xC3\xA9" "e "
                                "(carte m\xC3\xA9moire : Garder la m\xC3\xA9moire).",
                                starts));
    t.properties.push_back(prop("Temps de r\xC3\xA9ponse (ms)", std::to_string(e.twinDelayMs), PG::ValueType::Integer, field("jumeau_delai"),
                                "Il r\xC3\xA9pond apr\xC3\xA8s N ms, comme un vrai appareil (ou un r\xC3\xA9seau lent)."));
    t.properties.push_back(prop("Plus ou moins (ms)", std::to_string(e.twinJitterMs), PG::ValueType::Integer, field("jumeau_gigue")));
    t.properties.push_back(prop("R\xC3\xA9pond", tf(e.twinResponds), PG::ValueType::Boolean, field("jumeau_repond"),
                                "D\xC3\xA9" "coch\xC3\xA9 : panne simul\xC3\xA9" "e - il ne r\xC3\xA9pond plus ; l'IHM qui le lit voit l'\xC3\xA9quipement injoignable."));
    t.properties.push_back(prop("Exception forc\xC3\xA9" "e", exceptionText(e.twinException), PG::ValueType::Enum, field("jumeau_exception"),
                                "Il r\xC3\xA9pond cette exception \xC3\xA0 chaque requ\xC3\xAAte, pour voir l'IHM r\xC3\xA9" "agir.",
                                {"aucune", "01", "02", "03", "04", "06", "0B"}));
    t.properties.push_back(prop("R\xC3\xA9pond au ping", tf(e.twinPing), PG::ValueType::Boolean, field("jumeau_ping")));
    t.properties.push_back(prop("Visible sur le vrai r\xC3\xA9seau", tf(e.twinExpose), PG::ValueType::Boolean, field("jumeau_visible"),
                                "Coch\xC3\xA9 : il \xC3\xA9" "coute aussi sur toutes les cartes du PC (0.0.0.0) - une supervision, un autre ma\xC3\xAEtre Modbus peut l'interroger."));
    if (e.twinExpose)
        t.properties.push_back(prop("Port visible", std::to_string(e.twinExposePort), PG::ValueType::Integer, field("jumeau_port_visible"),
                                    "1502 par d\xC3\xA9" "faut (502 demande des droits)."));
    std::size_t animated = 0;
    for (const auto& b : e.behaviors) animated += b.enabled ? 1 : 0;
    std::string values = std::to_string(animated) + " \xC2\xB7 " + std::to_string(e.forcings.size());
    if (!e.forcings.empty()) values += " (" + e.forcings.front().address + " = " + hmi::twin::numberText(e.forcings.front().value) + (e.forcings.size() > 1 ? ", ..." : "") + ")";
    t.properties.push_back(prop("Valeurs anim\xC3\xA9" "es \xC2\xB7 forc\xC3\xA9" "es", values, PG::ValueType::ReadOnly, {},
                                "Ses valeurs qui bougent toutes seules, et ses cases forc\xC3\xA9" "es : le bouton Valeurs simul\xC3\xA9" "es."));
    cats.push_back(std::move(t));
}

// "En ce moment" : lu par l'IHM, ses requetes, ses clients, sa structure SYS.Slave.
void HmiCommPane::rebuildNowProperties(std::vector<PG::Category>& cats, const hmi::Equipment& e) {
    PG::Category c;
    c.name = "En ce moment";
    auto* h = host();
    const auto st = h ? h->status(e.name) : std::nullopt;
    std::string read = "non : l'IHM lit le vrai appareil";
    const bool via = st && st->enabled && st->viaTwin;
    if (!e.enabled) read = "non (\xC3\xA9quipement d\xC3\xA9sactiv\xC3\xA9)";
    else if (e.simulated) read = via ? "oui : il n'y a que lui (pas encore de vrai appareil)" : "pas encore (arr\xC3\xAAt\xC3\xA9)";
    else if (via && st->fallback)
        read = "oui, depuis " + clockText(st->readSince) + " (le vrai ne r\xC3\xA9pond pas" + (st->realSilentSince > 0 ? " depuis " + clockText(st->realSilentSince) : std::string{}) + ")";
    else if (via) read = "oui" + (st->readSince > 0 ? ", depuis " + clockText(st->readSince) : std::string{}) + (st->chosen ? " (page Simulation)" : " (la fiche)");
    auto r = prop("Lu par l'IHM", read, PG::ValueType::ReadOnly, {}, "L'IHM lit-elle ses valeurs \xC3\xA0 la place du vrai appareil, et depuis quand.");
    if (via) r.valueColor = simmark::kText;
    c.properties.push_back(std::move(r));
    if (h) {
        const auto s = h->simulatedStats(e.name);
        std::uint64_t refused = 0;
        if (const auto bank = h->twinBank(e.name)) refused = bank->counters().forcedRefused;
        c.properties.push_back(prop("Requ\xC3\xAAtes", std::to_string(s.requests) + " \xC2\xB7 " + std::to_string(refused) + " \xC3\xA9" "criture" + (refused > 1 ? "s" : "")
                                                           + " refus\xC3\xA9" "e" + (refused > 1 ? "s" : ""),
                                    PG::ValueType::ReadOnly));
        std::string clients = s.clients == 0 ? std::string("aucun") : std::to_string(s.clients) + " connect\xC3\xA9" + (s.clients > 1 ? "s" : "");
        if (s.clients && !s.lastClient.empty()) {
            const std::string who = hmi::modbus::clientLabel(s.lastClient);
            clients += " (dernier : " + (who.empty() ? s.lastClient : who) + ")";
        }
        c.properties.push_back(prop("Clients", clients, PG::ValueType::ReadOnly));
    }
    auto sys = prop("Variables syst\xC3\xA8me", "SYS.Slave." + hmi::slaveKey(e.name), PG::ValueType::ReadOnly, {},
                    "Sa structure syst\xC3\xA8me (en marche) : .Read (l'IHM le lit), .Fallback, .RealOnline, .Running, .Requests...");
    sys.valueTone = ui::Tone::Accent;
    c.properties.push_back(std::move(sys));
    cats.push_back(std::move(c));
}

void HmiCommPane::rebuildSlaveProperties(std::vector<PG::Category>& cats, const hmi::Equipment& e) {
    // ---- repris du vrai appareil (au cadenas)
    PG::Category rep;
    rep.name = "Repris du vrai appareil";
    const auto locked = [&](std::string label, std::string value, std::string help = {}) {
        auto p = prop(std::move(label), std::move(value), PG::ValueType::ReadOnly, {}, std::move(help));
        p.locked = true;
        p.lockTint = simmark::kLine;
        rep.properties.push_back(std::move(p));
    };
    const std::string follow = "Li\xC3\xA9 au vrai appareil : change le vrai (la ligne au-dessus), l'esclave suit.";
    locked("\xC3\x89quipement", e.name, follow);
    locked("Esclave (Unit Id)", std::to_string(e.unit), follow);
    locked("Adresse simul\xC3\xA9" "e", e.twinAddress() + (e.twinHost.empty() ? " (la m\xC3\xAAme)" : " (r\xC3\xA9seau simul\xC3\xA9)"),
           "L'adresse qu'il a dans le r\xC3\xA9seau simul\xC3\xA9 : celle du vrai, sauf s'il a \xC3\xA9t\xC3\xA9 tir\xC3\xA9 sur un autre port simul\xC3\xA9.");
    locked("Zones m\xC3\xA9moire", e.zones.declared ? hmi::zones::summary(e.zones) : std::string("non d\xC3\xA9" "clar\xC3\xA9" "es (tout)"), follow);
    const auto vars = eq::boundVariables(doc_->project, e);
    std::vector<std::string> names;
    for (const auto* v : vars) names.push_back(v->name);
    locked("Variables li\xC3\xA9" "es", vars.empty() ? std::string("aucune") : std::to_string(vars.size()) + " : " + fewOf(names, 3), follow);
    locked("Ordre des mots", e.wordOrder == "fort" ? "poids fort d'abord" : "poids faible d'abord", follow);
    cats.push_back(std::move(rep));
    // ---- ce qui est a lui, et en ce moment
    rebuildOwnSlaveProperties(cats, e, "L'esclave simul\xC3\xA9");
    rebuildNowProperties(cats, e);
    sheetNote_ = "D\xC3\xA9tacher : il devient un esclave seulement simul\xC3\xA9, ind\xC3\xA9pendant ; le vrai appareil perd son clone (Ctrl+Z revient).";
}

// ================================================================ detacher ===
std::string HmiCommPane::detachSlave(const std::string& equipment, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return std::string{};
    };
    const auto* e = equipmentOf(equipment);
    if (!e) return fail("pas d'\xC3\xA9quipement " + equipment);
    if (!e->linkedSlave()) return fail(e->name + " n'a pas d'esclave simul\xC3\xA9 li\xC3\xA9 \xC3\xA0 d\xC3\xA9tacher");
    const auto& p = doc_->project;
    // Son nom : celui de l'esclave, sans collision.
    std::string name = e->twinLabel();
    for (int k = 2; p.equipmentByName(name) && k < 1000; ++k) name = e->twinLabel() + " " + std::to_string(k);
    hmi::Equipment d = *e;
    d.name = name;
    d.simulated = true;
    d.twin = false;
    d.twinName.clear();
    d.host = e->twinAddress();
    d.twinHost.clear();
    d.twinAuto = false;
    d.useTwin = true;
    d.description = "d\xC3\xA9tach\xC3\xA9 de " + e->name + (e->description.empty() ? std::string{} : " - " + e->description);
    // La memoire qu'il a en ce moment (sans variable, il ne la retrouverait pas au depart).
    if (auto* h = host())
        if (const auto bank = h->twinBank(e->name); bank && d.twinStart != hmi::TwinStart::Saved) {
            d.twinMemory = bank->snapshot();
            d.twinStart = hmi::TwinStart::Saved;
        }
    const std::string real = e->name;
    if (!changeProject("D\xC3\xA9tacher l'esclave simul\xC3\xA9 de " + real, [&](hmi::Project& x) {
            for (auto& q : x.equipments)
                if (q.name == real) {
                    q.twin = false;
                    q.twinName.clear();
                    q.twinHost.clear();
                    q.behaviors.clear();
                    q.forcings.clear();
                    q.twinMemory.clear();
                    q.twinAuto = false;
                }
            d.id = x.allocate();
            x.equipments.push_back(d);
        }))
        return fail("impossible de d\xC3\xA9tacher l'esclave de " + real);
    selectEquipment(name);
    say(name + " est maintenant un esclave seulement simul\xC3\xA9, ind\xC3\xA9pendant ; " + real + " n'a plus de clone (Ctrl+Z revient).");
    return name;
}

bool HmiCommPane::askDetachSlave(const std::string& equipment) {
    const auto* e = equipmentOf(equipment);
    if (!e || !e->linkedSlave() || !hosts_.ask) return false;
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiDetach";
    spec.title = "D\xC3\xA9tacher \xC2\xAB " + e->twinLabel() + " \xC2\xBB";
    spec.text = e->twinLabel() + " devient un esclave seulement simul\xC3\xA9, ind\xC3\xA9pendant : il garde sa configuration, ses mouvements, ses "
                "for\xC3\xA7" "ages et sa m\xC3\xA9moire, mais ne suit plus " + e->name + ".\n" + e->name + " perd son clone ; ses variables li\xC3\xA9" "es "
                "restent \xC3\xA0 lui.\nCtrl+Z revient.";
    spec.confirm = "D\xC3\xA9tacher";
    const std::string real = e->name;
    hosts_.ask(std::move(spec), [this, real](bool ok, const HmiAskDialog::Answer&) {
        if (ok) (void)detachSlave(real);
    });
    return true;
}

// ================================================================ les bascules ===
//  Une bascule (le vrai -> l'esclave, ou l'inverse) : le message du volet (sa
//  barre d'etat) et un avis en bas a droite, quelques secondes.
void HmiCommPane::noteSwitches(double time) {
    auto* h = host();
    if (!h) return;
    if (!switchInit_) {
        switchInit_ = true;
        switchSeen_ = h->lastSwitch();
        return;
    }
    for (const auto& s : h->switchesAfter(switchSeen_)) {
        switchSeen_ = s.seq;
        // Le message du volet (la maquette M1) ; l'avis dit le reste (s.text).
        const std::string at = clockText(s.at);
        if (s.toSlave && s.fallback)
            say(s.equipment + " ne r\xC3\xA9pond plus depuis " + std::to_string(s.afterS) + " s : l'IHM lit son esclave simul\xC3\xA9 (" + at + ").");
        else if (s.toSlave) say(s.equipment + " : l'IHM lit son esclave simul\xC3\xA9 (" + at + ").");
        else say(s.equipment + " : l'IHM lit de nouveau le vrai appareil (" + at + ").");
        toast_.title = at + " \xC2\xB7 " + s.equipment;
        toast_.text = s.text;
        toast_.since = time;
        toast_.until = time + 8.0;
    }
}

// Les boutons de la carte violette : ses valeurs simulees, sa carte memoire,
// l'outil Modbus qui le vise (127.0.0.1), Detacher...
void HmiCommPane::runSlaveCard(int action) {
    const auto* e = equipmentOf(equipment_);
    if (!e || !e->linkedSlave()) return;
    const std::string name = e->name;
    switch (action) {
        case HmiSlaveCard::Values: showValues(name); break;
        case HmiSlaveCard::Map:
            showMap(name, true);
            tabs_->setCurrentIndex(TMap);
            break;
        case HmiSlaveCard::Tool: {
            const int port = host() ? host()->simulatedPort(name) : 0;
            if (!port) {
                say(e->twinLabel() + " n'est pas en marche : l'outil Modbus n'a rien \xC3\xA0 viser (fiche : En marche).", true);
                break;
            }
            if (hosts_.openTool) hosts_.openTool("127.0.0.1", port, e->unit, "cyclique");
            break;
        }
        case HmiSlaveCard::Detach:
            if (!askDetachSlave(name)) (void)detachSlave(name);
            break;
        default: break;
    }
}

} // namespace app
