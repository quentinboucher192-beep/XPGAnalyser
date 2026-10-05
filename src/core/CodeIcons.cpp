// =============================================================================
//  core/CodeIcons.cpp - 1.8.0 : voir CodeIcons.hpp
// =============================================================================
#include "CodeIcons.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>

namespace core::codeicons {

namespace {

constexpr std::array<Info, kCount> kInfos{{
    {"init", "Initialisation", "D\xC3\xA9marrage, valeurs par d\xC3\xA9" "faut, bits syst\xC3\xA8me (%S)", 0x5AA9E6},
    {"tor", "Entr\xC3\xA9" "es TOR", "Entr\xC3\xA9" "es tout-ou-rien, anti-rebonds, for\xC3\xA7" "ages", 0x3FB96A},
    {"ana", "Mesures analogiques", "Capteurs, mises \xC3\xA0 l'\xC3\xA9" "chelle, filtres", 0x2EC4B6},
    {"sorties", "Sorties", "Commande des sorties physiques (%Q)", 0xFF9F43},
    {"grafcet", "Grafcet / s\xC3\xA9quence", "\xC3\x89tapes et transitions d'un cycle", 0xC792EA},
    {"actions", "Actions de grafcet", "Ce que fait chaque \xC3\xA9tape d'un grafcet", 0xB388FF},
    {"config", "Configuration / recettes", "Param\xC3\xA8tres, recettes, lecture et \xC3\xA9" "criture de configurations", 0xAAB4C3},
    {"reset", "R\xC3\xA9initialisation", "Remises \xC3\xA0 z\xC3\xA9ro, acquittements, reset", 0xF78C6C},
    {"alarme", "Alarmes / d\xC3\xA9" "fauts", "D\xC3\xA9tection des d\xC3\xA9" "fauts, d\xC3\xA9" "clenchements, messages", 0xF0605A},
    {"securite", "S\xC3\xA9" "curit\xC3\xA9 / surveillance", "Arr\xC3\xAAts d'urgence, verrouillages, surpressions", 0xE5484D},
    {"ihm", "IHM / affichage", "\xC3\x89" "changes avec les \xC3\xA9" "crans, voyants, textes", 0x6EA8FF},
    {"comm", "Communication", "Modbus, r\xC3\xA9seau, \xC3\xA9" "changes avec d'autres \xC3\xA9quipements", 0x4DD0E1},
    {"calcul", "Calcul / r\xC3\xA9gulation", "Formules, PID, compteurs, conversions", 0xE0A93B},
    {"tempo", "Temporisations / horloge", "Tempos, horodatage, date et heure", 0xFFD166},
    {"matrice", "Matrice / routage", "Aiguillage des commandes vers les sorties", 0x8BD450},
    {"rapports", "Rapports / archivage", "Historiques, rapports, journaux, supervision", 0xC9A27E},
    {"donnees", "Structure de donn\xC3\xA9" "es", "Un type, une table, une zone m\xC3\xA9moire (DDT, tableaux)", 0x80CBC4},
    {"debug", "Diagnostic / mise au point", "Traces, DEBUG, essais", 0x9FB0C0},
}};

constexpr float kPi = 3.14159265358979f;

Path line(float x1, float y1, float x2, float y2) { return Path{{{x1, y1}, {x2, y2}}, false, false}; }

Path poly(std::initializer_list<Point> pts, bool closed = false) { return Path{std::vector<Point>(pts), closed, false}; }

// Un arc de cercle (degres ; 0 = a droite, 90 = en bas : l'axe y descend).
Path arc(float cx, float cy, float r, float a0, float a1) {
    const float span = std::fabs(a1 - a0);
    const int n = std::max(4, static_cast<int>(span / 15.f));
    Path p;
    for (int i = 0; i <= n; ++i) {
        const float a = (a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n)) * kPi / 180.f;
        p.points.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return p;
}

Path circle(float cx, float cy, float r, bool filled = false) {
    Path p = arc(cx, cy, r, 0.f, 360.f);
    p.points.pop_back();
    p.closed = true;
    p.filled = filled;
    return p;
}

Path rect(float x, float y, float w, float h) { return poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, true); }

Path roundRect(float x, float y, float w, float h, float r) {
    Path p;
    const auto corner = [&](float cx, float cy, float a0) {
        const auto a = arc(cx, cy, r, a0, a0 + 90.f);
        p.points.insert(p.points.end(), a.points.begin(), a.points.end());
    };
    corner(x + w - r, y + r, 270.f);
    corner(x + w - r, y + h - r, 0.f);
    corner(x + r, y + h - r, 90.f);
    corner(x + r, y + r, 180.f);
    p.closed = true;
    return p;
}

std::vector<Path> build(std::size_t i) {
    std::vector<Path> g;
    switch (i) {
        case 0:   // init : le symbole marche / arret
            g.push_back(line(8.f, 1.8f, 8.f, 7.2f));
            g.push_back(arc(8.f, 7.47f, 5.f, 224.f, -44.f));
            break;
        case 1:   // tor : un interrupteur a glissiere
            g.push_back(roundRect(1.5f, 4.5f, 13.f, 7.f, 3.5f));
            g.push_back(circle(11.f, 8.f, 2.f, true));
            break;
        case 2:   // ana : un cadran et son aiguille
            g.push_back(arc(8.f, 12.f, 6.f, 180.f, 360.f));
            g.push_back(line(8.f, 12.f, 11.2f, 7.5f));
            g.push_back(line(4.f, 9.8f, 5.f, 10.4f));
            g.push_back(line(8.f, 6.f, 8.f, 7.2f));
            g.push_back(line(12.f, 9.8f, 11.f, 10.4f));
            break;
        case 3:   // sorties : une fleche vers une borne
            g.push_back(line(2.f, 8.f, 10.5f, 8.f));
            g.push_back(poly({{7.5f, 4.8f}, {10.7f, 8.f}, {7.5f, 11.2f}}));
            g.push_back(line(13.5f, 2.5f, 13.5f, 13.5f));
            break;
        case 4:   // grafcet : deux etapes et une transition
            g.push_back(rect(4.5f, 1.5f, 7.f, 4.2f));
            g.push_back(line(8.f, 5.7f, 8.f, 10.3f));
            g.push_back(line(5.3f, 8.f, 10.7f, 8.f));
            g.push_back(rect(4.5f, 10.3f, 7.f, 4.2f));
            break;
        case 5:   // actions : un eclair
            g.push_back(poly({{9.2f, 1.5f}, {3.8f, 9.f}, {7.9f, 9.f}, {6.8f, 14.5f}, {12.2f, 7.f}, {8.1f, 7.f}}, true));
            break;
        case 6:   // config : trois curseurs
            g.push_back(line(3.f, 4.f, 7.f, 4.f));
            g.push_back(line(10.5f, 4.f, 13.f, 4.f));
            g.push_back(line(3.f, 8.f, 4.5f, 8.f));
            g.push_back(line(8.f, 8.f, 13.f, 8.f));
            g.push_back(line(3.f, 12.f, 9.f, 12.f));
            g.push_back(line(12.5f, 12.f, 13.f, 12.f));
            g.push_back(circle(8.8f, 4.f, 1.6f));
            g.push_back(circle(6.2f, 8.f, 1.6f));
            g.push_back(circle(10.8f, 12.f, 1.6f));
            break;
        case 7:   // reset : une fleche qui revient
            g.push_back(arc(8.f, 8.f, 5.f, 180.f, -132.6f));
            g.push_back(poly({{2.6f, 1.9f}, {2.6f, 4.9f}, {5.6f, 4.9f}}));
            break;
        case 8:   // alarme : le triangle d'avertissement
            g.push_back(poly({{8.f, 2.f}, {14.5f, 13.5f}, {1.5f, 13.5f}}, true));
            g.push_back(line(8.f, 6.2f, 8.f, 9.8f));
            g.push_back(circle(8.f, 11.8f, 0.7f, true));
            break;
        case 9:   // securite : un bouclier et sa coche
            g.push_back(poly({{8.f, 1.8f}, {13.f, 3.6f}, {13.f, 7.7f}, {12.3f, 10.3f}, {10.5f, 12.5f}, {8.f, 14.2f},
                              {5.5f, 12.5f}, {3.7f, 10.3f}, {3.f, 7.7f}, {3.f, 3.6f}}, true));
            g.push_back(poly({{5.8f, 8.2f}, {7.4f, 9.8f}, {10.4f, 6.6f}}));
            break;
        case 10:  // ihm : un ecran sur son pied
            g.push_back(roundRect(1.5f, 2.5f, 13.f, 8.8f, 1.f));
            g.push_back(line(5.5f, 14.f, 10.5f, 14.f));
            g.push_back(line(8.f, 11.3f, 8.f, 14.f));
            break;
        case 11:  // comm : trois noeuds relies
            g.push_back(circle(3.8f, 8.f, 2.f));
            g.push_back(circle(12.2f, 3.8f, 2.f));
            g.push_back(circle(12.2f, 12.2f, 2.f));
            g.push_back(line(5.6f, 7.1f, 10.4f, 4.7f));
            g.push_back(line(5.6f, 8.9f, 10.4f, 11.3f));
            break;
        case 12:  // calcul : une calculatrice
            g.push_back(roundRect(2.5f, 1.5f, 11.f, 13.f, 1.2f));
            g.push_back(line(5.f, 4.5f, 11.f, 4.5f));
            for (const float y : {8.f, 11.f})
                for (const float x : {5.2f, 8.f, 10.8f}) g.push_back(circle(x, y, 0.75f, true));
            break;
        case 13:  // tempo : un chronometre
            g.push_back(circle(8.f, 8.6f, 5.4f));
            g.push_back(poly({{8.f, 5.6f}, {8.f, 8.6f}, {10.f, 10.f}}));
            g.push_back(line(6.4f, 1.6f, 9.6f, 1.6f));
            break;
        case 14:  // matrice : une grille
            g.push_back(roundRect(2.f, 2.f, 12.f, 12.f, 1.f));
            g.push_back(line(6.f, 2.f, 6.f, 14.f));
            g.push_back(line(10.f, 2.f, 10.f, 14.f));
            g.push_back(line(2.f, 6.f, 14.f, 6.f));
            g.push_back(line(2.f, 10.f, 14.f, 10.f));
            break;
        case 15:  // rapports : une page ecrite
            g.push_back(poly({{3.5f, 1.5f}, {9.5f, 1.5f}, {12.5f, 4.5f}, {12.5f, 14.5f}, {3.5f, 14.5f}}, true));
            g.push_back(poly({{9.5f, 1.5f}, {9.5f, 4.5f}, {12.5f, 4.5f}}));
            g.push_back(line(5.5f, 8.f, 10.5f, 8.f));
            g.push_back(line(5.5f, 10.5f, 10.5f, 10.5f));
            g.push_back(line(5.5f, 13.f, 8.5f, 13.f));
            break;
        case 16: {  // donnees : des accolades
            const std::initializer_list<Point> left{{5.5f, 2.5f}, {4.2f, 2.7f}, {3.6f, 3.4f}, {3.5f, 4.5f}, {3.5f, 6.f}, {3.2f, 7.2f}, {2.f, 8.f},
                                                    {3.2f, 8.8f}, {3.5f, 10.f}, {3.5f, 11.5f}, {3.7f, 12.8f}, {4.3f, 13.4f}, {5.5f, 13.5f}};
            g.push_back(poly(left));
            Path right;
            for (const auto& p : left) right.points.push_back({16.f - p.x, p.y});
            g.push_back(std::move(right));
            break;
        }
        default:  // debug : un insecte
            g.push_back(roundRect(4.5f, 4.5f, 7.f, 9.f, 3.5f));
            g.push_back(arc(8.f, 4.5f, 2.f, 180.f, 360.f));
            g.push_back(line(8.f, 7.f, 8.f, 13.5f));
            g.push_back(line(2.f, 7.5f, 4.5f, 7.5f));
            g.push_back(line(11.5f, 7.5f, 14.f, 7.5f));
            g.push_back(line(2.5f, 12.f, 4.7f, 12.f));
            g.push_back(line(11.3f, 12.f, 13.5f, 12.f));
            g.push_back(line(3.f, 3.5f, 4.8f, 5.f));
            g.push_back(line(13.f, 3.5f, 11.2f, 5.f));
            break;
    }
    return g;
}

std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

bool has(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }
bool starts(const std::string& s, std::string_view p) { return s.rfind(p, 0) == 0; }
bool ends(const std::string& s, std::string_view p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

} // namespace

const Info& info(std::size_t i) noexcept { return kInfos[i < kCount ? i : 0]; }

const std::vector<Path>& glyph(std::size_t i) {
    static const std::array<std::vector<Path>, kCount> all = [] {
        std::array<std::vector<Path>, kCount> a;
        for (std::size_t k = 0; k < kCount; ++k) a[k] = build(k);
        return a;
    }();
    return all[i < kCount ? i : 0];
}

int indexOf(std::string_view key) noexcept {
    for (std::size_t i = 0; i < kCount; ++i)
        if (kInfos[i].key == key) return static_cast<int>(i);
    return -1;
}

std::string_view suggest(std::string_view name, Kind kind) {
    if (kind == Kind::Ddt) return "donnees";
    const auto n = lower(name);
    if (ends(n, "_actions") || starts(n, "actions")) return "actions";
    if (has(n, "debug") || starts(n, "test")) return "debug";
    if (starts(n, "sfc_surpression") || has(n, "surveillance") || has(n, "securit") || has(n, "urgence")) return "securite";
    if (starts(n, "sfc_") || has(n, "grafcet") || has(n, "sequence") || has(n, "logigramme")) return "grafcet";
    if (starts(n, "init") || has(n, "initialis") || has(n, "demarrage") || has(n, "d\xC3\xA9marrage")) return "init";
    if (ends(n, "_tor") || has(n, "entree") || has(n, "entr\xC3\xA9" "e") || has(n, "_di")) return "tor";
    if (ends(n, "_ana") || has(n, "capteur") || has(n, "mesure") || has(n, "analog")) return "ana";
    if (has(n, "sortie") || has(n, "toutfermer") || has(n, "output")) return "sorties";
    if (has(n, "reset") || has(n, "raz") || has(n, "acquit")) return "reset";
    if (has(n, "config") || has(n, "recette") || has(n, "parametre") || has(n, "building") || has(n, "modification")) return "config";
    if (has(n, "affichage") || ends(n, "_ihm") || has(n, "ihm") || has(n, "hmi") || has(n, "ecran")) return "ihm";
    if (has(n, "report") || has(n, "rapport") || has(n, "archiv") || has(n, "journal") || has(n, "histo")) return "rapports";
    if (has(n, "matrice") || has(n, "distribution") || has(n, "routage")) return "matrice";
    if (has(n, "declench") || has(n, "defaut") || has(n, "d\xC3\xA9" "faut") || has(n, "alarm") || has(n, "vide")) return "alarme";
    if (has(n, "rtc") || has(n, "horloge") || has(n, "tempo") || has(n, "heure") || has(n, "duree") || has(n, "dur\xC3\xA9" "e")) return "tempo";
    if (has(n, "comm") || has(n, "modbus") || has(n, "reseau") || has(n, "r\xC3\xA9seau") || has(n, "ethernet")) return "comm";
    if (has(n, "convert") || has(n, "calcul") || has(n, "_to_") || has(n, "pid") || has(n, "regul") || has(n, "compteur")) return "calcul";
    if (has(n, "pretes") || has(n, "actives")) return "securite";
    if (kind == Kind::Dfb && has(n, "grafcet")) return "grafcet";
    return {};
}

std::string_view kindWord(Kind kind) noexcept {
    switch (kind) {
        case Kind::Section:     return "section";
        case Kind::Unit:        return "unit";
        case Kind::Dfb:         return "dfb";
        case Kind::DfbSection:  return "dfbsection";
        case Kind::Ddt:         return "ddt";
        case Kind::HmiScript:   return "hmiscript";
        case Kind::HmiFunction: return "hmifunction";
    }
    return "section";
}

std::string keyOf(Kind kind, std::string_view owner, std::string_view name) {
    std::string k(kindWord(kind));
    k += ':';
    if (!owner.empty()) {
        k += owner;
        k += '/';
    }
    k += name;
    return k;
}

} // namespace core::codeicons
