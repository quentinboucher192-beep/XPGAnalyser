#include "HmiSystemMenu.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hmi {

const SettingSpec* settingSpec(std::string_view key) noexcept {
    for (const auto& s : kSettings)
        if (s.key == key) return &s;
    return nullptr;
}

const MaintenanceSpec* maintenanceSpec(std::string_view key) noexcept {
    for (const auto& m : kMaintenance)
        if (m.key == key) return &m;
    return nullptr;
}

std::vector<int> settingMinutes(std::string_view key) {
    if (key == "veille") return {0, 1, 2, 5, 10, 15, 30, 60};
    if (key == "deconnexion") return {-1, 0, 1, 2, 5, 10, 15, 30, 60};
    return {};
}

namespace {

std::string minutesText(int m) {
    if (m <= 0) return "Jamais";
    if (m % 60 == 0) return std::to_string(m / 60) + " h";
    return std::to_string(m) + " min";
}

constexpr std::string_view kKeyboards[] = {"automatique", "toujours", "jamais"};

std::string capitalized(std::string_view s) {
    std::string out(s);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

} // namespace

std::string settingText(const SystemSettings& s, std::string_view key, int projectAutoLogoutMin) {
    if (key == "luminosite") return std::to_string(s.brightness) + " %";
    if (key == "volume") return std::to_string(s.volume) + " %";
    if (key == "son") return s.soundOn ? "Activ\xC3\xA9" : "Coup\xC3\xA9";
    if (key == "veille") return minutesText(s.screensaverMin);
    if (key == "deconnexion") {
        if (s.autoLogoutMin < 0)
            return "Selon le projet (" + (projectAutoLogoutMin > 0 ? minutesText(projectAutoLogoutMin) : std::string("jamais")) + ")";
        return minutesText(s.autoLogoutMin);
    }
    if (key == "clavier") return capitalized(s.keyboard);
    // lot 13
    if (key == "texte") return std::to_string(s.textScale) + " %";
    if (key == "couleurs") return s.colorMode == "daltonien" ? "Daltonien (bleu / vermillon)" : "Normales";
    if (key == "symboles") return s.symbols ? "Activ\xC3\xA9s" : "D\xC3\xA9sactiv\xC3\xA9s";
    if (key == "theme") return s.theme == "jour" ? "Jour (clair)" : "Nuit (sombre)";
    return {};
}

std::vector<int> textScales() { return {100, 125, 150, 175}; }

bool stepSetting(SystemSettings& s, std::string_view key, std::string_view how) {
    const SystemSettings before = s;
    const auto percent = [&](int& v, int lo) {
        if (how == "plus") v = std::min(100, (v / 10) * 10 + 10);
        else if (how == "moins") v = std::max(lo, ((v + 9) / 10) * 10 - 10);
    };
    const auto cycle = [&](int& v, const std::vector<int>& list) {
        auto it = std::find(list.begin(), list.end(), v);
        std::size_t i = it == list.end() ? 0 : static_cast<std::size_t>(it - list.begin());
        if (how == "suivant") i = (i + 1) % list.size();
        else if (how == "precedent") i = (i + list.size() - 1) % list.size();
        else return;
        v = list[i];
    };
    if (key == "luminosite") percent(s.brightness, 20);
    else if (key == "volume") percent(s.volume, 0);
    else if (key == "son") { if (how == "bascule") s.soundOn = !s.soundOn; }
    else if (key == "veille") cycle(s.screensaverMin, settingMinutes(key));
    else if (key == "deconnexion") cycle(s.autoLogoutMin, settingMinutes(key));
    else if (key == "clavier") {
        std::size_t i = 0;
        for (std::size_t k = 0; k < std::size(kKeyboards); ++k)
            if (kKeyboards[k] == s.keyboard) i = k;
        if (how == "suivant") i = (i + 1) % std::size(kKeyboards);
        else if (how == "precedent") i = (i + std::size(kKeyboards) - 1) % std::size(kKeyboards);
        s.keyboard = std::string(kKeyboards[i]);
    }
    // lot 13 : l'affichage
    else if (key == "texte") {
        const auto list = textScales();
        auto it = std::find(list.begin(), list.end(), s.textScale);
        std::size_t i = it == list.end() ? 0 : static_cast<std::size_t>(it - list.begin());
        if (how == "suivant" || how == "plus") i = std::min(list.size() - 1, it == list.end() ? 0 : i + 1);
        else if (how == "precedent" || how == "moins") i = i == 0 ? 0 : i - 1;
        s.textScale = list[i];
    }
    else if (key == "couleurs") { if (how == "suivant" || how == "precedent") s.colorMode = s.colorMode == "daltonien" ? "normal" : "daltonien"; }
    else if (key == "symboles") { if (how == "bascule") s.symbols = !s.symbols; }
    else if (key == "theme") { if (how == "suivant" || how == "precedent") s.theme = s.theme == "jour" ? "nuit" : "jour"; }
    return !(s == before);
}

// ================================================================ 1.9 : onglets ===
int systemTabFrom(std::string_view text) noexcept {
    std::string l(text);
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l.find("diag") != std::string::npos) return 1;
    if (l.find("simul") != std::string::npos) return kSimulationTab;
    return 0;
}

std::string_view systemTabLabel(int tab) noexcept {
    switch (tab) {
        case 1: return "Diagnostic";
        case kSimulationTab: return "Simulation";
        default: return "R\xC3\xA9glages";
    }
}

// ============================================ 1.9 : les pas de la page Simulation ===
double simStep(double span, bool integer) noexcept {
    const double target = std::fabs(span) / 10.0;
    if (!(target > 0)) return integer ? 1.0 : 0.1;
    const double p = std::pow(10.0, std::floor(std::log10(target)));
    double step = p;
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (p * m >= target * 0.75) {
            step = p * m;
            break;
        }
    if (integer) step = std::max(1.0, std::round(step));
    return std::max(step, integer ? 1.0 : 0.001);
}

double simNextPeriod(double current, int direction) noexcept {
    static constexpr double kPeriods[] = {0.5, 1, 2, 3, 4, 5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600};
    constexpr std::size_t n = sizeof(kPeriods) / sizeof(kPeriods[0]);
    if (direction > 0) {
        for (const double p : kPeriods)
            if (p > current + 1e-9) return p;
        return kPeriods[n - 1];
    }
    for (std::size_t i = n; i-- > 0;)
        if (kPeriods[i] < current - 1e-9) return kPeriods[i];
    return kPeriods[0];
}

std::string simNextKind(std::string_view current, bool boolean, int direction) {
    static const std::vector<std::string> kNumeric = {"aucun", "sinus", "rampe", "aleatoire", "compteur", "constante"};
    static const std::vector<std::string> kBoolean = {"aucun", "clignote", "constante"};
    const auto& list = boolean ? kBoolean : kNumeric;
    const std::string cur = current.empty() ? std::string("aucun") : std::string(current);
    const auto it = std::find(list.begin(), list.end(), cur);
    // Un mouvement que la page ne propose pas (recopie, suit l'automate, etapes) : le premier.
    if (it == list.end()) return direction >= 0 ? list[1] : list.back();
    const std::size_t i = static_cast<std::size_t>(it - list.begin());
    return list[(i + list.size() + (direction >= 0 ? 1 : list.size() - 1)) % list.size()];
}

bool simKindHasZone(std::string_view kind) noexcept { return kind == "sinus" || kind == "rampe" || kind == "aleatoire"; }

bool simKindHasPeriod(std::string_view kind) noexcept { return simKindHasZone(kind) || kind == "compteur" || kind == "clignote"; }

int simNextException(int current, int direction) noexcept {
    static constexpr int kCodes[] = {0, 1, 2, 3, 4, 6, 11};
    constexpr int n = static_cast<int>(sizeof(kCodes) / sizeof(kCodes[0]));
    int i = 0;
    for (int k = 0; k < n; ++k)
        if (kCodes[k] == current) i = k;
    return kCodes[(i + n + (direction >= 0 ? 1 : -1)) % n];
}

std::string simExceptionText(int code) {
    if (code == 0) return "aucune";
    char b[8];
    std::snprintf(b, sizeof b, "%02X", code & 0xFF);
    return b;
}

// ================================================================= geometrie ===
namespace {

// La page Simulation : dans le corps (bord a bord), les esclaves a gauche,
// l'esclave choisi a droite. Les cases et les boutons - et + ont 26 px (au
// doigt) ; un ecran etroit met chaque ligne sur deux etages.
void simPageLayout(SimPageLayout& s, const Box& body, const SimPageShape& shape, bool compact) {
    s = SimPageLayout{};
    const Box& B = body;
    s.fontSize = compact ? 10 : B.w >= 1000 ? 13 : B.w >= 760 ? 12 : 11;
    const double pad = B.w >= 900 ? 10 : 8;
    const double btnH = std::clamp(B.h * 0.055, 26.0, 36.0);
    if (shape.locked) {
        // La carte "Reserve a un administrateur", au milieu ; rien des esclaves.
        s.locked = true;
        const double cw = std::max(60.0, std::min(520.0, B.w - 32)), ch = std::max(60.0, std::min(250.0, B.h - 24));
        const double k = ch / 250.0;
        s.lockCard = {B.x + (B.w - cw) / 2, B.y + (B.h - ch) / 2, cw, ch};
        const double icon = std::clamp(54 * k, 24.0, 54.0);
        s.lockIcon = {s.lockCard.cx() - icon / 2, s.lockCard.y + 26 * k, icon, icon};
        const double bw = std::min(150.0, cw - 40), bh = std::clamp(36 * k, 24.0, 36.0);
        s.connect = {s.lockCard.cx() - bw / 2, s.lockCard.bottom() - 26 * k - bh, bw, bh};
        return;
    }
    const double listW = std::clamp(B.w * 0.27, std::min(200.0, B.w * 0.4), 320.0);
    s.list = {B.x, B.y, listW, B.h};
    s.right = {B.x + listW, B.y, std::max(20.0, B.w - listW), B.h};

    // ---- les esclaves : le titre, les cartes, puis TOUS et ses trois boutons
    const double headH = 16, x0 = s.list.x + pad, cw = std::max(40.0, listW - 2 * pad);
    s.listHead = {x0, s.list.y + pad, cw, headH};
    const double gap = 6;
    const double third = (cw - 2 * gap) / 3;
    const bool stacked = third < 84;          // une liste etroite : deux rangees de boutons
    double y = s.list.bottom() - pad - btnH;
    if (stacked) {
        s.allUnforce = {x0, y, cw, btnH};
        y -= btnH + gap;
        const double half = (cw - gap) / 2;
        s.allAnimate = {x0, y, half, btnH};
        s.allStop = {x0 + half + gap, y, half, btnH};
    } else {
        s.allAnimate = {x0, y, third, btnH};
        s.allStop = {x0 + third + gap, y, third, btnH};
        s.allUnforce = {x0 + 2 * (third + gap), y, third, btnH};
    }
    s.allHead = {x0, y - 6 - headH, cw, headH};
    const double segH = B.h >= 560 ? 30 : 26;
    const auto cardH = [&](const SimCardShape& c) { return 9.0 + 20 + 2 + 16 + (c.linked ? 6 + segH : 0.0) + (c.why ? 5 + 16 : 0.0) + 9; };
    const double top = s.listHead.bottom() + 8, bottom = s.allHead.y - 10;
    const double cardGap = 8;
    double all = 0;
    for (const auto& c : shape.cards) all += cardH(c) + cardGap;
    double avail = bottom - top;
    std::size_t first = 0;
    if (all - cardGap > avail && !shape.cards.empty()) {
        // Trop de cartes : des fleches en bas de la liste, et la premiere carte montree.
        const double arrow = 28;
        s.listDown = {x0 + cw - arrow, bottom - arrow, arrow, arrow};
        s.listUp = {s.listDown.x - 6 - arrow, bottom - arrow, arrow, arrow};
        avail -= arrow + 6;
        // La plus haute premiere carte qui laisse voir la derniere.
        double used = 0;
        std::size_t last = shape.cards.size();
        while (last > 0 && used + cardH(shape.cards[last - 1]) <= avail) {
            used += cardH(shape.cards[last - 1]) + cardGap;
            --last;
        }
        s.listMax = std::min(last, shape.cards.size() - 1);
        if (shape.listScroll == SimPageShape::kAutoScroll) {
            // La carte choisie se voit : la plus haute premiere carte qui la montre.
            const std::size_t chosen = std::min(shape.chosen, shape.cards.size() - 1);
            while (first < chosen && first < s.listMax) {
                double fill = 0;
                bool shows = false;
                for (std::size_t k = first; k < shape.cards.size(); ++k) {
                    fill += cardH(shape.cards[k]);
                    if (fill > avail + 0.5) break;
                    if (k == chosen) {
                        shows = true;
                        break;
                    }
                    fill += cardGap;
                }
                if (shows) break;
                ++first;
            }
        } else {
            first = std::min(shape.listScroll, s.listMax);
        }
    }
    s.listFirst = first;
    y = top;
    for (std::size_t i = first; i < shape.cards.size(); ++i) {
        const auto& c = shape.cards[i];
        const double ch = cardH(c);
        if (y + ch > top + avail + 0.5) break;
        SimPageLayout::Card card;
        card.index = i;
        card.linked = c.linked;
        card.box = {x0, y, cw, ch};
        card.name = {x0 + 10, y + 9, cw - 20, 20};
        card.sub = {x0 + 10, y + 31, cw - 20, 16};
        double yy = y + 9 + 20 + 2 + 16 + 6;
        if (c.linked) {
            const double sw = (cw - 20) / 3;
            for (int k = 0; k < 3; ++k) card.seg[k] = {x0 + 10 + k * sw, yy, sw, segH};
            yy += segH + 5;
        } else {
            yy -= 1;
        }
        if (c.why) card.why = {x0 + 10, yy, cw - 20, 16};
        s.cards.push_back(card);
        y += ch + cardGap;
    }

    // ---- l'esclave choisi : son nom, l'adresse, En marche, Repond, Exception
    if (shape.cards.empty()) return;
    const double rp = 12;
    const Box inner{s.right.x + rp, s.right.y, std::max(20.0, s.right.w - 2 * rp), s.right.h};
    const double ctrlW = 110 + 12 + 102 + 12 + 66 + 116, addrW = 128;
    const bool oneLine = inner.w >= ctrlW + 220 + addrW + 24;
    const double headLine = 52;
    s.head = {s.right.x, s.right.y, s.right.w, oneLine ? headLine : headLine + 40};
    const auto controls = [&](double left, double cy, bool fromRight) {
        // fromRight : de la droite vers la gauche (une ligne) ; sinon depuis `left`.
        const double sh = 26;
        if (fromRight) {
            s.excNext = {inner.right() - 26, cy - sh / 2, 26, sh};
            s.excValue = {s.excNext.x - 64, cy - sh / 2, 64, sh};
            s.excPrev = {s.excValue.x - 26, cy - sh / 2, 26, sh};
            s.excLabel = {s.excPrev.x - 6 - 66, cy - 10, 66, 20};
            s.responds = {s.excLabel.x - 12 - 102, cy - sh / 2, 102, sh};
            s.running = {s.responds.x - 12 - 110, cy - sh / 2, 110, sh};
        } else {
            s.running = {left, cy - sh / 2, 110, sh};
            s.responds = {s.running.right() + 12, cy - sh / 2, 102, sh};
            s.excLabel = {s.responds.right() + 12, cy - 10, 66, 20};
            s.excPrev = {s.excLabel.right() + 6, cy - sh / 2, 26, sh};
            s.excValue = {s.excPrev.right(), cy - sh / 2, 64, sh};
            s.excNext = {s.excValue.right(), cy - sh / 2, 26, sh};
        }
        s.runningSwitch = {s.running.x, cy - 9, 34, 18};
        s.respondsSwitch = {s.responds.x, cy - 9, 34, 18};
    };
    if (oneLine) {
        controls(0, s.head.y + headLine / 2, true);
        const double nameW = std::max(60.0, s.running.x - 12 - addrW - 8 - inner.x);
        s.name = {inner.x, s.head.y + 6, nameW, headLine - 12};
        s.address = {s.name.right() + 8, s.head.y + headLine / 2 - 10, addrW, 20};
    } else {
        s.name = {inner.x, s.head.y + 4, std::max(60.0, inner.w - addrW - 8), headLine - 8};
        s.address = {inner.right() - addrW, s.head.y + headLine / 2 - 10, addrW, 20};
        controls(inner.x, s.head.y + headLine + 18, false);
    }

    // ---- en bas : Animer les N, Arreter, Deforcer les N ; Revenir au vrai maintenant
    const double actH = btnH + 2 * pad;
    s.actions = {s.right.x, s.right.bottom() - actH, s.right.w, actH};
    {
        const double by = s.actions.y + pad;
        double wa = 124, ws = 92, wd = 136, wb = shape.chosenLinked ? (inner.w >= 640 ? 222 : 158) : 0;
        const double need = wa + ws + wd + 2 * 8 + (wb > 0 ? wb + 16 : 0);
        if (need > inner.w) {
            const double k = std::max(0.5, (inner.w - (wb > 0 ? wb + 16 : 0) - 16) / (wa + ws + wd));
            wa *= k;
            ws *= k;
            wd *= k;
        }
        s.animateSlave = {inner.x, by, wa, btnH};
        s.stopSlave = {s.animateSlave.right() + 8, by, ws, btnH};
        s.unforceSlave = {s.stopSlave.right() + 8, by, wd, btnH};
        if (wb > 0) s.back = {inner.right() - wb, by, wb, btnH};
    }

    // ---- le tableau des valeurs : ses titres, ses lignes (une ou deux rangees)
    s.tableHead = {s.right.x, s.head.bottom(), s.right.w, 28};
    s.table = {s.right.x, s.tableHead.bottom(), s.right.w, std::max(0.0, s.actions.y - s.tableHead.bottom())};
    const std::size_t n = shape.rows.size();
    const double stepVal = 46, strip = 34;
    bool scrolling = false, threeLines = false;
    double usable = inner.w;
    for (int pass = 0; pass < 2; ++pass) {
        usable = inner.w - (scrolling ? strip : 0);
        s.twoLines = usable < 820;
        // Trop etroit pour la 2e rangee (388 px et quatre cases de 30) : trois etages.
        threeLines = s.twoLines && usable < 388.0 + 4 * 30.0;
        s.rowH = threeLines ? (B.h >= 560 ? 112 : 100) : s.twoLines ? (B.h >= 560 ? 80 : 72) : (B.h >= 560 ? 52 : 44);
        s.visible = static_cast<std::size_t>(std::max(1.0, std::floor(s.table.h / s.rowH)));
        if (n > s.visible) scrolling = true;
        else break;
    }
    if (n > s.visible) {
        s.maxScroll = n - s.visible;
        s.up = {inner.x + usable + 4, s.table.y + 4, 30, 30};
        s.down = {inner.x + usable + 4, s.table.bottom() - 34, 30, 30};
    } else {
        s.visible = n;
        s.maxScroll = 0;
    }
    s.first = std::min(shape.scroll, s.maxScroll);
    // Les colonnes (une rangee) ; deux rangees : la 1re dit la case, le nom, la valeur ;
    // la 2e le mouvement, la zone, la periode, forcer - elle tient dans la largeur :
    // les cases des valeurs retrecissent (30 px au moins). Sans elles, la 2e rangee
    // fait 388 px (le mouvement 100, la zone 126, la periode 52, forcer 86, les ecarts).
    // Trois etages : le mouvement et la zone (234 px sans les valeurs), puis la periode et forcer.
    const double sv = threeLines ? std::clamp((usable - 234.0) / 2.0, 30.0, stepVal)
                    : s.twoLines ? std::clamp((usable - 388.0) / 4.0, 30.0, stepVal) : stepVal;
    const double stepW = 26 + sv + 26;
    const double cAnim = 40, cKind = s.twoLines ? 100 : 108, cZone = 2 * stepW + 14 + 8, cPeriod = stepW, cForce = 26 + 8 + stepW, cValue = 80, g = 8;
    const double hy = s.tableHead.y, hh = s.tableHead.h;
    double xKind = 0, xZone = 0, xPeriod = 0, xForce = 0;
    const double xAnim = inner.x, xName = inner.x + cAnim, xValue = inner.x + usable - cValue;
    if (!s.twoLines) {
        xForce = xValue - g - cForce;
        xPeriod = xForce - g - cPeriod;
        xZone = xPeriod - g - cZone;
        xKind = xZone - g - cKind;
        s.colAnimate = {xAnim, hy, cAnim, hh};
        s.colName = {xName, hy, std::max(20.0, xKind - g - xName), hh};
        s.colKind = {xKind, hy, cKind, hh};
        s.colZone = {xZone, hy, cZone, hh};
        s.colPeriod = {xPeriod, hy, cPeriod, hh};
        s.colForce = {xForce, hy, cForce, hh};
        s.colValue = {xValue, hy, cValue, hh};
    } else {
        // La 2e rangee commence sous le nom s'il y a la place, sinon au bord ; trois
        // etages : la periode et forcer sur le 3e, au bord.
        const double line2 = threeLines ? cKind + cZone + g : cKind + cZone + cPeriod + cForce + 3 * g;
        xKind = usable - cAnim >= line2 ? xName : inner.x;
        xZone = xKind + cKind + g;
        xPeriod = threeLines ? xKind : xZone + cZone + g;
        xForce = xPeriod + cPeriod + g;
        s.colAnimate = {xAnim, hy, cAnim, hh};
        s.colName = {xName, hy, std::max(20.0, xValue - g - xName), hh};
        s.colValue = {xValue, hy, cValue, hh};
    }
    for (std::size_t i = s.first; i < n && i < s.first + s.visible; ++i) {
        const auto& r = shape.rows[i];
        SimPageLayout::Row row;
        row.index = i;
        row.key = r.key;
        const double ry = s.table.y + static_cast<double>(i - s.first) * s.rowH;
        row.box = {s.right.x, ry, s.right.w - (n > s.visible ? strip + 4 : 0), s.rowH};
        // La premiere (ou seule) rangee.
        const double l1 = s.twoLines ? 34 : s.rowH;
        const double c1 = ry + (s.twoLines ? 4 + l1 / 2 : l1 / 2);
        row.animate = {xAnim, c1 - 13, 26, 26};
        row.name = {xName, ry + (s.twoLines ? 2 : 4), (s.twoLines ? xValue : xKind) - g - xName, s.twoLines ? l1 : s.rowH - 8};
        row.value = {xValue, c1 - 12, cValue, 24};
        // La rangee des reglages (trois etages : le mouvement et la zone, puis la periode et forcer).
        const double below = s.rowH - l1 - 8;
        const double c2 = threeLines ? ry + 4 + l1 + below / 4 : s.twoLines ? ry + 4 + l1 + below / 2 : c1;
        const double c3 = threeLines ? ry + 4 + l1 + below * 3 / 4 : c2;
        const double sh = 26;
        row.kindPrev = {xKind, c2 - 15, 24, 30};
        row.kindValue = {xKind + 24, c2 - 15, cKind - 48, 30};
        row.kindNext = {xKind + cKind - 24, c2 - 15, 24, 30};
        if (!r.boolean && simKindHasZone(r.kind)) {
            row.zoneMinMinus = {xZone, c2 - sh / 2, 26, sh};
            row.zoneMinValue = {xZone + 26, c2 - sh / 2, sv, sh};
            row.zoneMinPlus = {xZone + 26 + sv, c2 - sh / 2, 26, sh};
            row.zoneTo = {xZone + stepW + 4, c2 - 10, 14, 20};
            const double x2 = xZone + stepW + 4 + 14 + 4;
            row.zoneMaxMinus = {x2, c2 - sh / 2, 26, sh};
            row.zoneMaxValue = {x2 + 26, c2 - sh / 2, sv, sh};
            row.zoneMaxPlus = {x2 + 26 + sv, c2 - sh / 2, 26, sh};
        }
        if (simKindHasPeriod(r.kind)) {
            row.periodMinus = {xPeriod, c3 - sh / 2, 26, sh};
            row.periodValue = {xPeriod + 26, c3 - sh / 2, sv, sh};
            row.periodPlus = {xPeriod + 26 + sv, c3 - sh / 2, 26, sh};
        }
        row.force = {xForce, c3 - 13, 26, 26};
        if (r.forced) {
            const double fx = xForce + 26 + 8;
            row.forcedMinus = {fx, c3 - sh / 2, 26, sh};
            row.forcedValue = {fx + 26, c3 - sh / 2, sv, sh};
            row.forcedPlus = {fx + 26 + sv, c3 - sh / 2, 26, sh};
        }
        s.rows.push_back(std::move(row));
    }
}

} // namespace

SystemMenuLayout systemMenuLayout(double w, double h, int tab, std::size_t diagRows, std::size_t scroll, const SimPageShape* sim) {
    SystemMenuLayout l;
    const bool compact = h < 420;
    // 1.9 : la page Simulation grandit le panneau (1200 x 800) ; elle n'existe pas
    // sur un poste qui l'a decochee (deux onglets).
    const bool present = !sim || sim->present;
    const bool simTab = tab == kSimulationTab && present;
    l.tabCount = present ? kSystemTabCount : 2;
    const double maxW = simTab ? 1200.0 : 780.0, maxH = simTab ? 800.0 : 700.0;
    const double pw = std::max(120.0, std::min(w - 24, maxW)), ph = std::max(120.0, std::min(h - 20, maxH));   // lot 13 : 700 (dix reglages)
    const double px = (w - pw) / 2, py = (h - ph) / 2;
    const double titleH = compact ? 30 : 40, tabH = compact ? 24 : 30, statusH = compact ? 22 : 28;
    l.panel = {px, py, pw, ph};
    l.title = {px, py, pw, titleH};
    l.close = {px + pw - titleH, py, titleH, titleH};
    const double tabW = std::min(160.0, (pw - 24) / l.tabCount);
    for (int k = 0; k < l.tabCount; ++k) l.tabs[k] = {px + 12 + k * (tabW + 4), py + titleH + 4, tabW, tabH};
    l.status = {px, py + ph - statusH, pw, statusH};
    l.body = {px + 12, py + titleH + tabH + 10, pw - 24, std::max(20.0, ph - titleH - tabH - 10 - statusH - 6)};
    if (simTab) {
        // Bord a bord : les esclaves a gauche, l'esclave choisi a droite.
        l.body = {px, py + titleH + tabH + 6, pw, std::max(20.0, ph - titleH - tabH - 6 - statusH)};
        l.rowH = 30;
        l.fontSize = compact ? 11 : 14;
        simPageLayout(l.sim, l.body, sim ? *sim : SimPageShape{}, compact);
        (void)diagRows;
        (void)scroll;
        return l;
    }
    // Les lignes de reglages (dix au lot 13), trois pour l'heure, une pour la maintenance.
    const double settingRows = static_cast<double>(kSettingCount - 1);
    l.rowH = std::clamp(l.body.h / (settingRows + 4.3), compact ? 8.0 : 12.0, 36.0);   // un petit ecran : des lignes plus serrees
    l.fontSize = std::clamp(l.rowH * 0.48, 8.0, 15.0);
    if (tab == 0) {
        const double labelW = std::min(230.0, l.body.w * 0.36);
        const double x0 = l.body.x + labelW + 16, right = l.body.right() - 6;
        double y = l.body.y;
        for (const auto& spec : kSettings) {
            if (spec.kind == SettingKind::Clock) continue;
            SystemMenuLayout::Setting s;
            s.key = std::string(spec.key);
            s.row = {l.body.x, y, l.body.w, l.rowH};
            s.label = {l.body.x + 8, y, labelW, l.rowH};
            const double bh = std::max(12.0, l.rowH - 8), cy = y + l.rowH / 2;
            if (spec.kind == SettingKind::Toggle) {
                const double th = std::max(12.0, l.rowH - 10);
                s.toggle = {x0, cy - th / 2, th * 2.1, th};
                s.value = {x0 + th * 2.1 + 10, y, std::max(10.0, right - x0 - th * 2.1 - 10), l.rowH};
            } else {
                s.minus = {x0, cy - bh / 2, bh, bh};
                s.plus = {right - bh, cy - bh / 2, bh, bh};
                s.value = {x0 + bh + 8, cy - bh / 2, std::max(10.0, right - x0 - 2 * bh - 16), bh};
            }
            l.settings.push_back(std::move(s));
            y += l.rowH;
        }
        l.clockRow = {l.body.x, y + 2, l.body.w, l.rowH * 3 - 4};
        const double cw = std::max(40.0, right - x0);
        l.clock = pickerLayout("date et heure", false, cw, l.clockRow.h);
        const auto shift = [&](Box& b) { b.x += x0; b.y += l.clockRow.y; };
        for (auto& f : l.clock.fields) {
            shift(f.box);
            shift(f.up);
            shift(f.down);
        }
        shift(l.clock.now);
        shift(l.clock.ok);
        y += l.rowH * 3 + 4;
        const double n = static_cast<double>(std::size(kMaintenance));
        const double gap = 8, bw = (right - x0 - gap * (n - 1)) / n, bh = std::max(12.0, l.rowH - 6);
        for (std::size_t i = 0; i < std::size(kMaintenance); ++i)
            l.buttons.push_back({std::string(kMaintenance[i].key),
                                 {x0 + static_cast<double>(i) * (bw + gap), y + (l.rowH - bh) / 2, std::max(10.0, bw), bh}});
    } else {
        // Toutes les lignes en deux colonnes si possible (deux lignes de marge
        // par colonne : un groupe ne se coupe pas) ; sinon des fleches.
        const bool two = l.body.w >= 520;
        const std::size_t cols = two ? 2 : 1;
        const double wanted = static_cast<double>((diagRows + cols - 1) / cols + 2);
        const double rowD = std::clamp(std::min(l.fontSize * 1.65, l.body.h / std::max(1.0, wanted)), 15.0, 26.0);
        const double colW = two ? (l.body.w - 16) / 2 : l.body.w;
        std::size_t perCol = static_cast<std::size_t>(std::max(1.0, std::floor(l.body.h / rowD)));
        if (diagRows > perCol * cols) {
            // Des fleches pour defiler : une ligne de moins par colonne.
            perCol = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor((l.body.h - rowD) / rowD)));
            l.up = {l.body.right() - 2 * rowD - 6, l.body.bottom() - rowD, rowD, rowD};
            l.down = {l.body.right() - rowD, l.body.bottom() - rowD, rowD, rowD};
        }
        l.diagVisible = perCol;
        l.diagRowH = rowD;
        for (std::size_t c = 0; c < cols; ++c)
            l.diagColumns.push_back({l.body.x + static_cast<double>(c) * (colW + 16), l.body.y, colW, rowD * static_cast<double>(perCol)});
        (void)scroll;
    }
    return l;
}

namespace {

// Une case a l'ecran (vide : absente).
bool shown(const Box& b) noexcept { return b.w > 0 && b.h > 0; }
bool in(const Box& b, double x, double y) noexcept { return shown(b) && b.contains(x, y); }

constexpr const char* kSources[3] = {"vrai", "esclave", "auto"};

// Les parties d'une ligne de la page Simulation : (nom sans la cle, cadre).
std::vector<std::pair<std::string, const Box*>> rowParts(const SimPageLayout::Row& r) {
    return {{"animer:", &r.animate},
            {"mouvement:#:precedent", &r.kindPrev},
            {"mouvement:#:suivant", &r.kindNext},
            {"zone_min:#:moins", &r.zoneMinMinus},
            {"zone_min:#:plus", &r.zoneMinPlus},
            {"zone_max:#:moins", &r.zoneMaxMinus},
            {"zone_max:#:plus", &r.zoneMaxPlus},
            {"periode:#:moins", &r.periodMinus},
            {"periode:#:plus", &r.periodPlus},
            {"forcer:", &r.force},
            {"forcee:#:moins", &r.forcedMinus},
            {"forcee:#:plus", &r.forcedPlus}};
}

// "mouvement:#:suivant" + "%MW8504" -> "mouvement:%MW8504:suivant" ; "animer:" -> "animer:%MW8504".
std::string withKey(const std::string& pattern, const std::string& key) {
    const auto hash = pattern.find('#');
    if (hash == std::string::npos) return pattern + key;
    return pattern.substr(0, hash) + key + pattern.substr(hash + 1);
}

std::string simPageHit(const SimPageLayout& s, double x, double y) {
    if (s.locked) return in(s.connect, x, y) ? "connecter" : std::string{};
    for (const auto& c : s.cards) {
        if (c.linked)
            for (int k = 0; k < 3; ++k)
                if (in(c.seg[k], x, y)) return "source:" + std::to_string(c.index) + ":" + kSources[k];
        if (in(c.box, x, y)) return "esclave:" + std::to_string(c.index);
    }
    if (in(s.listUp, x, y)) return "defiler_liste:-1";
    if (in(s.listDown, x, y)) return "defiler_liste:1";
    if (in(s.allAnimate, x, y)) return "animer_tout";
    if (in(s.allStop, x, y)) return "arreter_tout";
    if (in(s.allUnforce, x, y)) return "deforcer_tout";
    if (in(s.running, x, y)) return "marche";
    if (in(s.responds, x, y)) return "repond";
    if (in(s.excPrev, x, y)) return "exception:precedent";
    if (in(s.excNext, x, y) || in(s.excValue, x, y)) return "exception:suivant";
    for (const auto& r : s.rows) {
        for (const auto& [pattern, box] : rowParts(r))
            if (in(*box, x, y)) return withKey(pattern, r.key);
        // Un clic sur le mouvement : le suivant.
        if (in(r.kindValue, x, y)) return "mouvement:" + r.key + ":suivant";
    }
    if (in(s.up, x, y)) return "defiler:-1";
    if (in(s.down, x, y)) return "defiler:1";
    if (in(s.animateSlave, x, y)) return "animer_esclave";
    if (in(s.stopSlave, x, y)) return "arreter_esclave";
    if (in(s.unforceSlave, x, y)) return "deforcer_esclave";
    if (in(s.back, x, y)) return "revenir";
    return {};
}

bool simPagePartBox(const SimPageLayout& s, std::string_view part, Box& out) {
    const auto found = [&](const Box& b) {
        out = b;
        return shown(b);
    };
    if (part == "connecter") return found(s.connect);
    if (s.locked) return false;
    static const std::pair<std::string_view, const Box SimPageLayout::*> kSimple[] = {
        {"animer_tout", &SimPageLayout::allAnimate},   {"arreter_tout", &SimPageLayout::allStop},
        {"deforcer_tout", &SimPageLayout::allUnforce}, {"marche", &SimPageLayout::running},
        {"repond", &SimPageLayout::responds},          {"exception", &SimPageLayout::excValue},
        {"exception:precedent", &SimPageLayout::excPrev}, {"exception:suivant", &SimPageLayout::excNext},
        {"defiler:-1", &SimPageLayout::up},            {"defiler:1", &SimPageLayout::down},
        {"defiler_liste:-1", &SimPageLayout::listUp},  {"defiler_liste:1", &SimPageLayout::listDown},
        {"animer_esclave", &SimPageLayout::animateSlave}, {"arreter_esclave", &SimPageLayout::stopSlave},
        {"deforcer_esclave", &SimPageLayout::unforceSlave}, {"revenir", &SimPageLayout::back},
        {"nom", &SimPageLayout::name},                 {"adresse", &SimPageLayout::address},
    };
    for (const auto& [name, member] : kSimple)
        if (part == name) return found(s.*member);
    const auto colon = part.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string_view how = part.substr(0, colon), rest = part.substr(colon + 1);
    if (how == "esclave" || how == "carte" || how == "source") {
        const auto c2 = rest.find(':');
        const std::string index(rest.substr(0, c2));
        for (const auto& c : s.cards) {
            if (std::to_string(c.index) != index) continue;
            // "carte" : toute la carte ; "esclave" : son nom et sa ligne d'etat (le
            // milieu d'une carte liee peut tomber sur le segment de la source).
            if (how == "carte") return found(c.box);
            if (how == "esclave") return found(Box{c.box.x, c.box.y, c.box.w, std::max(1.0, c.sub.bottom() + 4 - c.box.y)});
            const std::string_view what = c2 == std::string_view::npos ? std::string_view{} : rest.substr(c2 + 1);
            for (int k = 0; k < 3; ++k)
                if (what == kSources[k]) return found(c.seg[k]);
            return false;
        }
        return false;
    }
    // Une ligne : "valeur:<cle>", "ligne:<cle>", ou l'une de ses parties.
    for (const auto& r : s.rows) {
        if (how == "valeur" && rest == r.key) return found(r.value);
        if (how == "ligne" && rest == r.key) return found(r.box);
        if (how == "nom" && rest == r.key) return found(r.name);
        if (how == "mouvement" && rest == r.key) return found(r.kindValue);
        for (const auto& [pattern, box] : rowParts(r))
            if (part == withKey(pattern, r.key)) return found(*box);
    }
    return false;
}

} // namespace

std::vector<std::string> simPageParts(const SystemMenuLayout& l) {
    std::vector<std::string> out;
    const auto& s = l.sim;
    if (s.locked) {
        out.push_back("connecter");
        return out;
    }
    for (const auto& c : s.cards) {
        out.push_back("esclave:" + std::to_string(c.index));
        if (c.linked)
            for (const char* src : kSources) out.push_back("source:" + std::to_string(c.index) + ":" + src);
    }
    for (const char* p : {"defiler_liste:-1", "defiler_liste:1", "animer_tout", "arreter_tout", "deforcer_tout", "marche", "repond",
                          "exception:precedent", "exception:suivant", "defiler:-1", "defiler:1", "animer_esclave", "arreter_esclave",
                          "deforcer_esclave", "revenir"}) {
        Box b;
        if (simPagePartBox(s, p, b)) out.emplace_back(p);
    }
    for (const auto& r : s.rows)
        for (const auto& [pattern, box] : rowParts(r))
            if (shown(*box)) out.push_back(withKey(pattern, r.key));
    return out;
}

std::string systemMenuHit(const SystemMenuLayout& l, int tab, double x, double y) {
    if (!l.panel.contains(x, y)) return "dehors";
    if (l.close.contains(x, y)) return "fermer";
    if (l.tabs[0].contains(x, y)) return "onglet:reglages";
    if (l.tabs[1].contains(x, y)) return "onglet:diagnostic";
    if (l.tabCount > kSimulationTab && in(l.tabs[kSimulationTab], x, y)) return "onglet:simulation";
    if (tab == kSimulationTab) return l.tabCount > kSimulationTab ? simPageHit(l.sim, x, y) : std::string{};
    if (tab == 0) {
        for (const auto& s : l.settings) {
            const auto* spec = settingSpec(s.key);
            if (!spec) continue;
            if (spec->kind == SettingKind::Toggle) {
                if (s.toggle.contains(x, y) || s.value.contains(x, y)) return "bascule:" + s.key;
                continue;
            }
            const bool percent = spec->kind == SettingKind::Percent;
            if (s.minus.contains(x, y)) return (percent ? "moins:" : "precedent:") + s.key;
            if (s.plus.contains(x, y)) return (percent ? "plus:" : "suivant:") + s.key;
            // Un clic sur la valeur d'un choix : le suivant.
            if (!percent && s.value.contains(x, y)) return "suivant:" + s.key;
        }
        for (const auto& f : l.clock.fields) {
            if (f.up.contains(x, y)) return "heure:plus:" + f.name;
            if (f.down.contains(x, y)) return "heure:moins:" + f.name;
        }
        if (l.clock.now.contains(x, y)) return "heure:maintenant";
        if (l.clock.ok.contains(x, y)) return "heure:appliquer";
        for (const auto& b : l.buttons)
            if (b.box.contains(x, y)) return "action:" + b.key;
    } else {
        if (l.up.w > 0 && l.up.contains(x, y)) return "defiler:-1";
        if (l.down.w > 0 && l.down.contains(x, y)) return "defiler:1";
    }
    return {};
}

bool systemMenuPartBox(const SystemMenuLayout& l, int tab, std::string_view part, Box& out) {
    const auto found = [&](const Box& b) {
        out = b;
        return b.w > 0 && b.h > 0;
    };
    if (part == "fermer") return found(l.close);
    if (part == "onglet:reglages") return found(l.tabs[0]);
    if (part == "onglet:diagnostic") return found(l.tabs[1]);
    if (part == "onglet:simulation") return l.tabCount > kSimulationTab && found(l.tabs[kSimulationTab]);
    if (tab == kSimulationTab) return l.tabCount > kSimulationTab && simPagePartBox(l.sim, part, out);
    if (tab == 1) {
        if (part == "defiler:-1") return found(l.up);
        if (part == "defiler:1") return found(l.down);
        return false;
    }
    const auto colon = part.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string_view how = part.substr(0, colon), key = part.substr(colon + 1);
    for (const auto& s : l.settings) {
        if (s.key != key) continue;
        if (how == "moins" || how == "precedent") return found(s.minus);
        if (how == "plus" || how == "suivant") return found(s.plus);
        if (how == "bascule") return found(s.toggle);
        if (how == "valeur") return found(s.value);
        return false;
    }
    if (how == "heure") {
        if (key == "maintenant") return found(l.clock.now);
        if (key == "appliquer") return found(l.clock.ok);
        for (const auto& f : l.clock.fields) {
            if (key == "plus:" + f.name) return found(f.up);
            if (key == "moins:" + f.name) return found(f.down);
        }
        return false;
    }
    if (how == "action")
        for (const auto& b : l.buttons)
            if (b.key == key) return found(b.box);
    return false;
}

std::vector<DiagLine> diagLines(const std::vector<DiagGroup>& groups, const SystemMenuLayout& l, std::size_t scroll,
                                std::size_t* maxScroll) {
    std::vector<DiagLine> out;
    if (maxScroll) *maxScroll = 0;
    const std::size_t per = l.diagVisible, cols = l.diagColumns.size();
    if (per == 0 || cols == 0) return out;
    // (groupe, ligne) a la suite ; (-1, -1) : une ligne laissee vide.
    std::vector<std::pair<long, int>> flat, arranged;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        flat.emplace_back(static_cast<long>(g), -1);
        for (std::size_t r = 0; r < groups[g].rows.size(); ++r) flat.emplace_back(static_cast<long>(g), static_cast<int>(r));
    }
    const std::size_t capacity = per * cols;
    if (flat.size() <= capacity) {
        // Tout tient : un groupe qui deborderait de sa colonne passe a la suivante.
        std::size_t used = 0;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const std::size_t size = 1 + groups[g].rows.size();
            if (used > 0 && used + size > per && size <= per) {
                arranged.insert(arranged.end(), per - used, {-1L, -1});
                used = 0;
            }
            arranged.emplace_back(static_cast<long>(g), -1);
            for (std::size_t r = 0; r < groups[g].rows.size(); ++r) arranged.emplace_back(static_cast<long>(g), static_cast<int>(r));
            used = (used + size) % per;
        }
        if (arranged.size() > capacity) arranged = flat;
        scroll = 0;
    } else {
        arranged = flat;
        const std::size_t last = flat.size() - capacity;
        if (maxScroll) *maxScroll = last;
        scroll = std::min(scroll, last);
    }
    for (std::size_t i = scroll; i < arranged.size() && i < scroll + capacity; ++i) {
        if (arranged[i].first < 0) continue;
        const std::size_t k = i - scroll, c = k / per, line = k % per;
        const Box& col = l.diagColumns[c];
        out.push_back({static_cast<std::size_t>(arranged[i].first), arranged[i].second, c,
                       {col.x, col.y + static_cast<double>(line) * l.diagRowH, col.w, l.diagRowH}});
    }
    return out;
}

} // namespace hmi
