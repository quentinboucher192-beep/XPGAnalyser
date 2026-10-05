// ui/ThemeFile.cpp - un theme dans un fichier, ses contrastes, ceux de
// l'utilisateur (lot API 8). Voir ThemeFile.hpp.
#include "ThemeFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace ui {

namespace {

namespace fs = std::filesystem;

// Les chemins sont en UTF-8 ; std::filesystem::path(std::string) les lirait
// dans la page de code du systeme sous Windows.
fs::path pathOf8(std::string_view utf8) {
    const std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}
std::string utf8Of(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}

bool same(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// ---------------------------------------------------------------- les cles ---
// Ou vit chaque couleur : un champ de Palette, ou un champ de Brand.
enum class Slot : std::uint8_t {
    WindowBg, PanelBg, HeaderBg, RailBg, InputBg, RowAltBg, Card, CodeBg, TooltipBg,
    Text, TextMuted, TextDisabled, TextInverted, TooltipText, CodeGutter,
    Accent, AccentHover, AccentPressed, SelectionBg, SelectionText, FocusRing, Hover,
    Ok, Warning, Error, Info, Led, LedOff,
    Border, BorderStrong, GridLine, CardBorder, CardShadow, Scrollbar, ScrollbarHover,
    Keyword, Type, Comment, String, Number, Preprocessor, Function, Constant, Operator,
    Family0, Family1, Family2, Family3, Family4, Family5, ScopeIn, ScopeOut, ScopeInOut,
};

struct KeyDef {
    ThemeColorKey key;
    const char*   field;     // le nom du champ C++ : un autre nom accepte a la lecture
    Slot          slot;
};

const char* const kFonds    = "Fonds";
const char* const kTextes   = "Textes";
const char* const kAccent   = "Accent et s\xC3\xA9lection";
const char* const kEtats    = "\xC3\x89tats";
const char* const kBordures = "Bordures";
const char* const kCode     = "Code (syntaxe)";
const char* const kFamilles = "Familles et port\xC3\xA9" "es";

const std::vector<KeyDef>& defs() {
    static const std::vector<KeyDef> k{
        {{"fond.fenetre", "Fen\xC3\xAAtre", kFonds, "Derri\xC3\xA8re tout : les marges, la barre d'\xC3\xA9tat.", false}, "windowBg", Slot::WindowBg},
        {{"fond.panneau", "Panneaux", kFonds, "Les volets, les listes, les formulaires : le fond du texte courant.", false}, "panelBg", Slot::PanelBg},
        {{"fond.entete", "En-t\xC3\xAAtes", kFonds, "Les titres de colonnes, les barres d'outils, les onglets.", false}, "headerBg", Slot::HeaderBg},
        {{"fond.rail", "Barre lat\xC3\xA9rale", kFonds, "La colonne des ic\xC3\xB4nes et les bandeaux.", false}, "railBg", Slot::RailBg},
        {{"fond.champ", "Champs", kFonds, "Les champs de saisie.", false}, "inputBg", Slot::InputBg},
        {{"fond.ligne", "Lignes altern\xC3\xA9" "es", kFonds, "Une ligne sur deux dans les tableaux.", false}, "rowAltBg", Slot::RowAltBg},
        {{"fond.carte", "Cartes", kFonds, "Les surfaces sur\xC3\xA9lev\xC3\xA9" "es : les cartes de l'accueil, de la galerie.", false}, "card", Slot::Card},
        {{"fond.code", "Code", kFonds, "Le fond de l'\xC3\xA9" "diteur de code.", false}, "codeBg", Slot::CodeBg},
        {{"fond.infobulle", "Infobulles", kFonds, "Le fond des infobulles.", false}, "tooltipBg", Slot::TooltipBg},

        {{"texte", "Texte", kTextes, "Le texte courant.", false}, "text", Slot::Text},
        {{"texte.secondaire", "Texte secondaire", kTextes, "Les pr\xC3\xA9" "cisions, les sous-titres, les colonnes discr\xC3\xA8tes.", false}, "textMuted", Slot::TextMuted},
        {{"texte.desactive", "Texte d\xC3\xA9sactiv\xC3\xA9", kTextes, "Ce qui ne se clique pas pour l'instant.", false}, "textDisabled", Slot::TextDisabled},
        {{"texte.inverse", "Texte sur l'accent", kTextes, "Le texte des boutons principaux et des pastilles.", false}, "textInverted", Slot::TextInverted},
        {{"texte.infobulle", "Texte des infobulles", kTextes, "Le texte des infobulles.", false}, "tooltipText", Slot::TooltipText},
        {{"texte.marge", "Num\xC3\xA9ros de ligne", kTextes, "La marge de l'\xC3\xA9" "diteur de code.", false}, "codeGutter", Slot::CodeGutter},

        {{"accent", "Accent", kAccent, "Les liens, le bouton principal, les rep\xC3\xA8res.", false}, "accent", Slot::Accent},
        {{"accent.survol", "Accent survol\xC3\xA9", kAccent, "Le bouton principal sous la souris.", false}, "accentHover", Slot::AccentHover},
        {{"accent.appui", "Accent appuy\xC3\xA9", kAccent, "Le bouton principal enfonc\xC3\xA9.", false}, "accentPressed", Slot::AccentPressed},
        {{"selection.fond", "S\xC3\xA9lection", kAccent, "La ligne choisie d'une liste, d'un arbre, d'un tableau.", false}, "selectionBg", Slot::SelectionBg},
        {{"selection.texte", "Texte s\xC3\xA9lectionn\xC3\xA9", kAccent, "Le texte de la ligne choisie.", false}, "selectionText", Slot::SelectionText},
        {{"focus", "Cadre du focus", kAccent, "Le cadre du champ ou du bouton qui a le clavier (transparence permise).", true}, "focusRing", Slot::FocusRing},
        {{"survol", "Survol", kAccent, "La ligne sous la souris (transparence permise).", true}, "hover", Slot::Hover},

        {{"etat.ok", "Marche, OK", kEtats, "Une voie saine, un essai r\xC3\xA9ussi.", false}, "ok", Slot::Ok},
        {{"etat.alerte", "Alerte", kEtats, "\xC3\x80 regarder quand tu pourras.", false}, "warning", Slot::Warning},
        {{"etat.erreur", "D\xC3\xA9" "faut, erreur", kEtats, "La machine est arr\xC3\xAAt\xC3\xA9" "e, le programme ne passe pas.", false}, "error", Slot::Error},
        {{"etat.info", "Information", kEtats, "Une remarque, un conseil.", false}, "info", Slot::Info},
        {{"led.allumee", "LED allum\xC3\xA9" "e", kEtats, "Le banc d'essai : une sortie \xC3\xA0 1.", false}, "led", Slot::Led},
        {{"led.eteinte", "LED \xC3\xA9teinte", kEtats, "Le banc d'essai : une sortie \xC3\xA0 0.", false}, "ledOff", Slot::LedOff},

        {{"bordure", "Bordures", kBordures, "Le tour des panneaux, des champs, des boutons.", false}, "border", Slot::Border},
        {{"bordure.forte", "Bordures fortes", kBordures, "Les s\xC3\xA9parations qui doivent se voir.", false}, "borderStrong", Slot::BorderStrong},
        {{"grille", "Lignes de grille", kBordures, "Les lignes des tableaux.", false}, "gridLine", Slot::GridLine},
        {{"bordure.carte", "Bord des cartes", kBordures, "Le tour des cartes.", false}, "cardBorder", Slot::CardBorder},
        {{"ombre.carte", "Ombre des cartes", kBordures, "Le trait sous une carte (transparence permise).", true}, "cardShadow", Slot::CardShadow},
        {{"ascenseur", "Ascenseurs", kBordures, "Les barres de d\xC3\xA9" "filement.", false}, "scrollbar", Slot::Scrollbar},
        {{"ascenseur.survol", "Ascenseur survol\xC3\xA9", kBordures, "La barre de d\xC3\xA9" "filement sous la souris.", false}, "scrollbarHover", Slot::ScrollbarHover},

        {{"code.mot-cle", "Mots-cl\xC3\xA9s", kCode, "IF, THEN, END_IF...", false}, "syntaxKeyword", Slot::Keyword},
        {{"code.type", "Types", kCode, "BOOL, INT, TON, les DDT...", false}, "syntaxType", Slot::Type},
        {{"code.commentaire", "Commentaires", kCode, "(* ... *) et //", false}, "syntaxComment", Slot::Comment},
        {{"code.chaine", "Cha\xC3\xAEnes", kCode, "'Texte entre apostrophes'", false}, "syntaxString", Slot::String},
        {{"code.nombre", "Nombres", kCode, "42, 3.14, 16#FF, T#5s", false}, "syntaxNumber", Slot::Number},
        {{"code.preprocesseur", "Directives", kCode, "Les directives et les attributs.", false}, "syntaxPreprocessor", Slot::Preprocessor},
        {{"code.fonction", "Fonctions", kCode, "Les appels de fonctions et de blocs.", false}, "syntaxFunction", Slot::Function},
        {{"code.constante", "Constantes", kCode, "TRUE, FALSE, les constantes.", false}, "syntaxConstant", Slot::Constant},
        {{"code.operateur", "Op\xC3\xA9rateurs", kCode, ":=, +, AND, <=...", false}, "syntaxOperator", Slot::Operator},

        {{"famille.alarmes", "Alarmes", kFamilles, "Les blocs d'alarmes (aide, biblioth\xC3\xA8que).", false}, "family0", Slot::Family0},
        {{"famille.controle", "Contr\xC3\xB4le", kFamilles, "Les blocs de contr\xC3\xB4le.", false}, "family1", Slot::Family1},
        {{"famille.equipement", "\xC3\x89quipement", kFamilles, "Les blocs d'\xC3\xA9quipement.", false}, "family2", Slot::Family2},
        {{"famille.es", "Entr\xC3\xA9" "es / sorties", kFamilles, "Les blocs d'E/S.", false}, "family3", Slot::Family3},
        {{"famille.macros", "Macros", kFamilles, "Les macros.", false}, "family4", Slot::Family4},
        {{"famille.autre", "Autres", kFamilles, "Ce qui n'est dans aucune famille.", false}, "family5", Slot::Family5},
        {{"portee.entree", "Param\xC3\xA8tre d'entr\xC3\xA9" "e", kFamilles, "La port\xC3\xA9" "e d'un param\xC3\xA8tre : entr\xC3\xA9" "e.", false}, "scopeIn", Slot::ScopeIn},
        {{"portee.sortie", "Param\xC3\xA8tre de sortie", kFamilles, "La port\xC3\xA9" "e d'un param\xC3\xA8tre : sortie.", false}, "scopeOut", Slot::ScopeOut},
        {{"portee.es", "Entr\xC3\xA9" "e-sortie", kFamilles, "La port\xC3\xA9" "e d'un param\xC3\xA8tre : entr\xC3\xA9" "e-sortie.", false}, "scopeInOut", Slot::ScopeInOut},
    };
    return k;
}

gfx::Color* slotRef(Theme& t, Slot s) {
    auto& c = t.color;
    auto& b = t.brand;
    switch (s) {
        case Slot::WindowBg: return &c.windowBg;
        case Slot::PanelBg: return &c.panelBg;
        case Slot::HeaderBg: return &c.headerBg;
        case Slot::RailBg: return &c.railBg;
        case Slot::InputBg: return &c.inputBg;
        case Slot::RowAltBg: return &c.rowAltBg;
        case Slot::Card: return &b.card;
        case Slot::CodeBg: return &b.codeBg;
        case Slot::TooltipBg: return &b.tooltipBg;
        case Slot::Text: return &c.text;
        case Slot::TextMuted: return &c.textMuted;
        case Slot::TextDisabled: return &c.textDisabled;
        case Slot::TextInverted: return &c.textInverted;
        case Slot::TooltipText: return &b.tooltipText;
        case Slot::CodeGutter: return &b.codeGutter;
        case Slot::Accent: return &c.accent;
        case Slot::AccentHover: return &c.accentHover;
        case Slot::AccentPressed: return &c.accentPressed;
        case Slot::SelectionBg: return &c.selectionBg;
        case Slot::SelectionText: return &c.selectionText;
        case Slot::FocusRing: return &b.focusRing;
        case Slot::Hover: return &b.hover;
        case Slot::Ok: return &c.ok;
        case Slot::Warning: return &c.warning;
        case Slot::Error: return &c.error;
        case Slot::Info: return &c.info;
        case Slot::Led: return &b.led;
        case Slot::LedOff: return &b.ledOff;
        case Slot::Border: return &c.border;
        case Slot::BorderStrong: return &c.borderStrong;
        case Slot::GridLine: return &c.gridLine;
        case Slot::CardBorder: return &b.cardBorder;
        case Slot::CardShadow: return &b.cardShadow;
        case Slot::Scrollbar: return &c.scrollbar;
        case Slot::ScrollbarHover: return &c.scrollbarHover;
        case Slot::Keyword: return &c.syntaxKeyword;
        case Slot::Type: return &c.syntaxType;
        case Slot::Comment: return &c.syntaxComment;
        case Slot::String: return &c.syntaxString;
        case Slot::Number: return &c.syntaxNumber;
        case Slot::Preprocessor: return &c.syntaxPreprocessor;
        case Slot::Function: return &c.syntaxFunction;
        case Slot::Constant: return &c.syntaxConstant;
        case Slot::Operator: return &c.syntaxOperator;
        case Slot::Family0: return &b.family[0];
        case Slot::Family1: return &b.family[1];
        case Slot::Family2: return &b.family[2];
        case Slot::Family3: return &b.family[3];
        case Slot::Family4: return &b.family[4];
        case Slot::Family5: return &b.family[5];
        case Slot::ScopeIn: return &b.scopeIn;
        case Slot::ScopeOut: return &b.scopeOut;
        case Slot::ScopeInOut: return &b.scopeInOut;
    }
    return nullptr;
}

const KeyDef* defOf(std::string_view key) {
    const auto want = Theme::fold(key);
    if (want.empty()) return nullptr;
    for (const auto& d : defs())
        if (Theme::fold(d.key.key) == want || Theme::fold(d.field) == want) return &d;
    return nullptr;
}

// ------------------------------------------------------------ les regles ---
struct Rule {
    const char* label;
    const char* fg;
    const char* bg;
    double      need;     // un theme courant
    double      strict;   // un theme a contraste eleve
};

const Rule kRules[] = {
    {"texte sur la fen\xC3\xAAtre", "texte", "fond.fenetre", 7.0, 7.0},
    {"texte sur les panneaux", "texte", "fond.panneau", 7.0, 7.0},
    {"texte dans les champs", "texte", "fond.champ", 7.0, 7.0},
    {"texte du code", "texte", "fond.code", 7.0, 7.0},
    {"texte sur les en-t\xC3\xAAtes", "texte", "fond.entete", 4.5, 7.0},
    {"texte sur les cartes", "texte", "fond.carte", 4.5, 7.0},
    {"texte secondaire", "texte.secondaire", "fond.panneau", 4.5, 7.0},
    {"secondaire sur les cartes", "texte.secondaire", "fond.carte", 4.5, 7.0},
    {"s\xC3\xA9lection", "selection.texte", "selection.fond", 4.5, 7.0},
    {"accent sur les panneaux", "accent", "fond.panneau", 4.5, 4.5},
    {"bouton principal", "texte.inverse", "accent", 4.5, 4.5},
    {"infobulle", "texte.infobulle", "fond.infobulle", 7.0, 7.0},
    {"\xC3\xA9tat OK", "etat.ok", "fond.panneau", 4.5, 7.0},
    {"alerte", "etat.alerte", "fond.panneau", 4.5, 7.0},
    {"d\xC3\xA9" "faut", "etat.erreur", "fond.panneau", 4.5, 7.0},
    {"information", "etat.info", "fond.panneau", 4.5, 7.0},
    {"LED allum\xC3\xA9" "e", "led.allumee", "fond.carte", 3.0, 3.0},
    {"code : mots-cl\xC3\xA9s", "code.mot-cle", "fond.code", 3.0, 4.5},
    {"code : types", "code.type", "fond.code", 3.0, 4.5},
    {"code : commentaires", "code.commentaire", "fond.code", 3.0, 4.5},
    {"code : cha\xC3\xAEnes", "code.chaine", "fond.code", 3.0, 4.5},
    {"code : nombres", "code.nombre", "fond.code", 3.0, 4.5},
    {"code : directives", "code.preprocesseur", "fond.code", 3.0, 4.5},
    {"code : fonctions", "code.fonction", "fond.code", 3.0, 4.5},
    {"code : constantes", "code.constante", "fond.code", 3.0, 4.5},
    {"code : op\xC3\xA9rateurs", "code.operateur", "fond.code", 3.0, 4.5},
};

double channel(std::uint8_t v) {
    const double c = static_cast<double>(v) / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(gfx::Color c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }

std::uint8_t toByte(double v) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L));
}

// `fg` en ne changeant que sa luminosite (HSL), jusqu'a `need` contre `bg` :
// la plus petite variation, vers le clair ou vers le fonce. Faux : impossible.
bool reach(gfx::Color& fg, gfx::Color bg, double need) {
    if (contrastRatio(fg, bg) >= need) return true;
    const Hsl h = toHsl(fg);
    bool found = false;
    double bestDelta = 2.0;
    gfx::Color best = fg;
    for (const int dir : {+1, -1}) {
        const double room = dir > 0 ? 1.0 - h.l : h.l;
        const auto at = [&](double delta) {
            Hsl x = h;
            x.l = std::clamp(h.l + static_cast<double>(dir) * delta, 0.0, 1.0);
            return fromHsl(x, fg.a);
        };
        if (contrastRatio(at(room), bg) < need) continue;
        double lo = 0.0, hi = room;
        for (int i = 0; i < 32; ++i) {
            const double mid = (lo + hi) * 0.5;
            if (contrastRatio(at(mid), bg) >= need) hi = mid;
            else lo = mid;
        }
        gfx::Color cand = at(hi);
        // L'arrondi a l'octet peut retomber juste sous le seuil : un pas de plus.
        for (int k = 0; k < 64 && contrastRatio(cand, bg) < need; ++k) {
            hi = std::min(room, hi + 0.002);
            cand = at(hi);
        }
        if (contrastRatio(cand, bg) < need) continue;
        if (hi < bestDelta) {
            bestDelta = hi;
            best = cand;
            found = true;
        }
    }
    if (found) fg = best;
    return found;
}

std::string baseWord(const std::string& baseKey, bool dark) {
    if (baseKey == "Dark") return "sombre";
    if (baseKey == "Light") return "claire";
    if (!baseKey.empty() && Theme::isBuiltIn(baseKey)) return Theme::labelOf(baseKey);
    return dark ? "sombre" : "claire";
}

// "sombre", "claire", "Nord", "Contraste eleve" -> la cle du theme integre ("" : inconnue).
std::string baseKeyOf(std::string_view word) {
    const auto f = Theme::fold(word);
    if (f == "sombre" || f == "dark" || f == "fonce") return "Dark";
    if (f == "claire" || f == "clair" || f == "light") return "Light";
    if (!Theme::isBuiltIn(word)) return {};
    return Theme::keyOf(word);
}

std::size_t codePoints(std::string_view s) {
    std::size_t n = 0;
    for (const char ch : s)
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++n;
    return n;
}

std::string guillemets(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

} // namespace

// ============================================================ les couleurs ===
const std::vector<ThemeColorKey>& themeColorKeys() {
    static const std::vector<ThemeColorKey> k = [] {
        std::vector<ThemeColorKey> v;
        for (const auto& d : defs()) v.push_back(d.key);
        return v;
    }();
    return k;
}

const std::vector<std::string>& themeColorGroups() {
    static const std::vector<std::string> k{kFonds, kTextes, kAccent, kEtats, kBordures, kCode, kFamilles};
    return k;
}

const ThemeColorKey* themeColorKey(std::string_view key) {
    const auto* d = defOf(key);
    return d ? &d->key : nullptr;
}

gfx::Color* themeColor(Theme& t, std::string_view key) {
    const auto* d = defOf(key);
    return d ? slotRef(t, d->slot) : nullptr;
}

const gfx::Color* themeColor(const Theme& t, std::string_view key) {
    return themeColor(const_cast<Theme&>(t), key);
}

bool parseThemeColor(std::string_view text, gfx::Color& out, bool alpha) {
    std::string s = trim(text);
    if (!s.empty() && s.front() == '#') s.erase(s.begin());
    if (s.size() != 6 && !(alpha && s.size() == 8)) return false;
    unsigned v[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        unsigned d = 0;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        if (i % 2 == 0) v[i / 2] = d * 16;
        else v[i / 2] += d;
    }
    out = {static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]), static_cast<std::uint8_t>(v[2]),
           static_cast<std::uint8_t>(v[3])};
    return true;
}

std::string themeColorText(gfx::Color c, bool alpha) {
    char buf[16];
    if (alpha && c.a != 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

Hsl toHsl(gfx::Color c) {
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    Hsl out;
    out.l = (mx + mn) * 0.5;
    const double d = mx - mn;
    if (d > 1e-12) {
        out.s = out.l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
        if (mx == r) out.h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if (mx == g) out.h = (b - r) / d + 2.0;
        else out.h = (r - g) / d + 4.0;
        out.h *= 60.0;
    }
    return out;
}

gfx::Color fromHsl(const Hsl& x, std::uint8_t alpha) {
    const double h = std::fmod(std::fmod(x.h, 360.0) + 360.0, 360.0) / 360.0;
    const double s = std::clamp(x.s, 0.0, 1.0), l = std::clamp(x.l, 0.0, 1.0);
    if (s <= 0.0) return {toByte(l), toByte(l), toByte(l), alpha};
    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    const auto hue = [&](double t) {
        if (t < 0.0) t += 1.0;
        if (t > 1.0) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    return {toByte(hue(h + 1.0 / 3.0)), toByte(hue(h)), toByte(hue(h - 1.0 / 3.0)), alpha};
}

// ========================================================= les contrastes ===
double contrastRatio(gfx::Color a, gfx::Color b) {
    const double la = luminance(a) + 0.05, lb = luminance(b) + 0.05;
    return la > lb ? la / lb : lb / la;
}

std::string contrastText(double ratio) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f:1", ratio);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::vector<ContrastCheck> checkContrasts(const Theme& t) {
    std::vector<ContrastCheck> out;
    const bool strict = t.isHighContrast();
    for (const auto& r : kRules) {
        const auto* fg = themeColor(t, r.fg);
        const auto* bg = themeColor(t, r.bg);
        if (!fg || !bg) continue;
        ContrastCheck c;
        c.label = r.label;
        c.fg = r.fg;
        c.bg = r.bg;
        c.ratio = contrastRatio(*fg, *bg);
        c.need = strict ? r.strict : r.need;
        c.pass = c.ratio + 1e-9 >= c.need;
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<ContrastCheck> contrastFailures(const Theme& t) {
    auto all = checkContrasts(t);
    all.erase(std::remove_if(all.begin(), all.end(), [](const ContrastCheck& c) { return c.pass; }), all.end());
    return all;
}

std::vector<std::string> saturationProblems(const Theme& t) {
    static const char* const kShell[] = {"fond.fenetre", "fond.panneau", "fond.entete", "fond.rail", "fond.champ", "fond.ligne",
                                         "fond.carte", "fond.code", "fond.infobulle", "texte", "texte.secondaire",
                                         "texte.desactive", "texte.inverse", "texte.infobulle", "texte.marge", "bordure",
                                         "bordure.forte", "grille", "bordure.carte", "ascenseur", "ascenseur.survol", "led.eteinte"};
    const auto chroma = [](gfx::Color c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); };
    std::vector<std::string> out;
    int shellMax = 0;
    for (const char* k : kShell) {
        const auto c = chroma(*themeColor(t, k));
        shellMax = std::max(shellMax, c);
        if (c > 80) out.push_back(std::string(k) + " trop satur\xC3\xA9" "e (" + std::to_string(c) + ")");
    }
    for (const char* k : {"etat.ok", "etat.alerte", "etat.erreur"}) {
        const auto c = chroma(*themeColor(t, k));
        if (c <= shellMax) out.push_back(std::string(k) + " pas plus satur\xC3\xA9" "e que la coque (" + std::to_string(c) + " / " + std::to_string(shellMax) + ")");
    }
    return out;
}

std::vector<std::string> fixContrasts(Theme& t) {
    std::vector<std::string> changed;
    const auto note = [&](const char* key) {
        if (std::find(changed.begin(), changed.end(), key) == changed.end()) changed.emplace_back(key);
    };
    const bool strict = t.isHighContrast();
    for (int pass = 0; pass < 4; ++pass) {
        bool any = false;
        for (const auto& r : kRules) {
            auto* fg = themeColor(t, r.fg);
            auto* bg = themeColor(t, r.bg);
            if (!fg || !bg) continue;
            const double need = strict ? r.strict : r.need;
            if (contrastRatio(*fg, *bg) + 1e-9 >= need) continue;
            // Un peu de marge : un theme corrige ne doit pas retomber sous le
            // seuil pour un arrondi (les tests calculent en float ou en double).
            const double target = need + 0.01;
            gfx::Color f = *fg;
            if (reach(f, *bg, target)) {
                *fg = f;
                note(r.fg);
                any = true;
                continue;
            }
            // Le premier plan ne suffit pas (un fond de luminosite moyenne) : le
            // fond s'eloigne de lui, un pas a la fois, jusqu'a ce qu'il suffise.
            const bool fgLighter = luminance(*fg) >= luminance(*bg);
            const Hsl hb = toHsl(*bg);
            for (int step = 1; step <= 40; ++step) {
                Hsl x = hb;
                x.l = std::clamp(hb.l + (fgLighter ? -0.025 : 0.025) * static_cast<double>(step), 0.0, 1.0);
                const gfx::Color b2 = fromHsl(x, bg->a);
                gfx::Color f2 = *fg;
                if (!reach(f2, b2, target)) continue;
                if (!same(f2, *fg)) note(r.fg);
                *bg = b2;
                *fg = f2;
                note(r.bg);
                any = true;
                break;
            }
        }
        if (!any) break;
    }
    return changed;
}

Theme deriveFromAccent(const Theme& from, gfx::Color accent, bool dark) {
    const Theme base = Theme::byName(dark ? "Dark" : "Light");
    Theme t = from;
    accent.a = 255;
    const Hsl a = toHsl(accent);
    const double h = a.h;
    // Des neutres A PEINE teintes de l'accent : la coque reste desaturee (la
    // regle de l'application), mais elle va avec lui. Un accent gris : des
    // gris neutres (sa "teinte" ne veut rien dire).
    const double tinted = a.s < 0.08 ? 0.0 : 1.0;
    const auto tint = [&](double s, double l) { return fromHsl({h, s * tinted, l}); };
    auto& c = t.color;
    auto& b = t.brand;
    if (dark) {
        c.windowBg = tint(0.14, 0.075);  c.panelBg = tint(0.14, 0.10);   c.headerBg = tint(0.13, 0.135);
        c.railBg = tint(0.12, 0.165);    c.inputBg = tint(0.14, 0.06);   c.rowAltBg = tint(0.13, 0.115);
        c.border = tint(0.12, 0.22);     c.borderStrong = tint(0.10, 0.31); c.gridLine = tint(0.12, 0.15);
        c.text = tint(0.25, 0.92);       c.textMuted = tint(0.10, 0.66); c.textDisabled = tint(0.08, 0.40);
        c.textInverted = tint(0.20, 0.05);
        c.selectionBg = fromHsl({h, std::min(a.s, 0.55), 0.25});
        c.selectionText = tint(0.20, 0.96);
        c.scrollbar = tint(0.10, 0.25);  c.scrollbarHover = tint(0.10, 0.36);
        b.card = tint(0.13, 0.13);       b.cardBorder = tint(0.12, 0.20);
        b.codeBg = tint(0.14, 0.065);    b.codeGutter = c.textDisabled;
        b.tooltipBg = tint(0.20, 0.04);  b.tooltipText = c.text;
        b.ledOff = c.scrollbar;
        b.cardShadow = gfx::Color{0, 0, 0, 90};
        b.hover = gfx::Color{255, 255, 255, 14};
    } else {
        c.windowBg = tint(0.16, 0.935);  c.panelBg = tint(0.20, 0.985);  c.headerBg = tint(0.16, 0.90);
        c.railBg = tint(0.15, 0.865);    c.inputBg = gfx::Color{255, 255, 255, 255}; c.rowAltBg = tint(0.18, 0.965);
        c.border = tint(0.14, 0.80);     c.borderStrong = tint(0.10, 0.63); c.gridLine = tint(0.14, 0.905);
        c.text = tint(0.22, 0.11);       c.textMuted = tint(0.10, 0.38); c.textDisabled = tint(0.08, 0.63);
        c.textInverted = gfx::Color{255, 255, 255, 255};
        c.selectionBg = fromHsl({h, std::min(a.s, 0.65), 0.88});
        c.selectionText = tint(0.25, 0.07);
        c.scrollbar = tint(0.12, 0.77);  c.scrollbarHover = tint(0.10, 0.63);
        b.card = gfx::Color{255, 255, 255, 255}; b.cardBorder = tint(0.14, 0.87);
        b.codeBg = tint(0.18, 0.965);    b.codeGutter = c.textDisabled;
        b.tooltipBg = c.text;            b.tooltipText = c.panelBg;
        b.ledOff = c.border;
        b.cardShadow = c.text.withAlpha(26);
        b.hover = c.text.withAlpha(10);
    }
    c.accent = accent;
    // Les etats et la coloration du code : ceux de la base, qu'on sait lire.
    c.ok = base.color.ok; c.warning = base.color.warning; c.error = base.color.error; c.info = base.color.info;
    c.syntaxKeyword = base.color.syntaxKeyword; c.syntaxType = base.color.syntaxType;
    c.syntaxComment = base.color.syntaxComment; c.syntaxString = base.color.syntaxString;
    c.syntaxNumber = base.color.syntaxNumber; c.syntaxPreprocessor = base.color.syntaxPreprocessor;
    c.syntaxFunction = base.color.syntaxFunction; c.syntaxConstant = base.color.syntaxConstant;
    c.syntaxOperator = c.text;
    fixContrasts(t);
    // L'accent a pu bouger (sa luminosite) : le survol et l'appui le suivent.
    const Hsl fixedAccent = toHsl(c.accent);
    c.accentHover = fromHsl({fixedAccent.h, fixedAccent.s, std::min(1.0, fixedAccent.l + 0.07)});
    c.accentPressed = fromHsl({fixedAccent.h, fixedAccent.s, std::max(0.0, fixedAccent.l - 0.08)});
    b.led = c.ok;
    b.focusRing = c.accent.withAlpha(dark ? 170 : 150);
    fixContrasts(t);
    return t;
}

// ============================================================ le fichier ===
std::string writeThemeText(const Theme& t) {
    std::ostringstream out;
    out << "# Th\xC3\xA8me de PLC Project Analyzer - un fichier texte : il se lit, se modifie et se partage.\n"
        << "# Une couleur par ligne, \xC2\xAB cl\xC3\xA9 = #RRGGBB \xC2\xBB ; une couleur absente reprend celle de la base.\n"
        << "# Une ligne qui commence par # est un commentaire.\n"
        << "format = 1\n";
    std::string name = t.name;
    std::replace(name.begin(), name.end(), '\n', ' ');
    std::replace(name.begin(), name.end(), '\r', ' ');
    out << "nom = " << trim(name) << '\n';
    const bool dark = t.isDark();
    out << "famille = " << (t.family.empty() ? (dark ? "Sombres" : "Clairs") : t.family) << '\n';
    out << "base = " << baseWord(t.base, dark) << '\n';
    if (!t.description.empty()) {
        std::string d = t.description;
        std::replace(d.begin(), d.end(), '\n', ' ');
        std::replace(d.begin(), d.end(), '\r', ' ');
        out << "description = " << trim(d) << '\n';
    }
    if (!t.author.empty()) {
        std::string a = t.author;
        std::replace(a.begin(), a.end(), '\n', ' ');
        std::replace(a.begin(), a.end(), '\r', ' ');
        out << "auteur = " << trim(a) << '\n';
    }
    const char* group = "";
    for (const auto& d : defs()) {
        if (std::string_view(group) != d.key.group) {
            group = d.key.group;
            out << "\n# " << group << '\n';
        }
        out << d.key.key << " = " << themeColorText(*slotRef(const_cast<Theme&>(t), d.slot), d.key.alpha) << '\n';
    }
    return out.str();
}

ThemeRead readThemeText(std::string_view text) {
    ThemeRead r;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    const auto refuse = [&](int line, const std::string& raw, std::string why) {
        r.ok = false;
        r.line = line;
        r.error = raw.empty() ? std::move(why) : "ligne " + std::to_string(line) + " : " + guillemets(raw) + " : " + why;
        return r;
    };

    std::string name, family, description, author, baseKey;
    int familyLine = 0;
    struct Set { const KeyDef* def; gfx::Color color; };
    std::vector<Set> sets;
    int lineNo = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string raw = trim(text.substr(at, end - at));
        at = end + 1;
        ++lineNo;
        if (raw.empty() || raw.front() == '#') {
            if (end >= text.size()) break;
            continue;
        }
        const auto eq = raw.find('=');
        if (eq == std::string::npos)
            return refuse(lineNo, raw, "il manque le signe = (\xC2\xAB cl\xC3\xA9 = valeur \xC2\xBB).");
        const std::string key = trim(std::string_view(raw).substr(0, eq));
        const std::string value = trim(std::string_view(raw).substr(eq + 1));
        const auto k = Theme::fold(key);
        if (k.empty()) return refuse(lineNo, raw, "il manque la cl\xC3\xA9 avant le signe =.");
        if (k == "format") {
            if (value != "1")
                return refuse(lineNo, raw, "ce format n'est pas connu de cette version (elle lit le format 1).");
        } else if (k == "nom" || k == "name") {
            if (auto why = themeNameProblem(value); !why.empty()) return refuse(lineNo, raw, why);
            name = value;
        } else if (k == "famille" || k == "family") {
            family = value;
            familyLine = lineNo;
        } else if (k == "base") {
            baseKey = baseKeyOf(value);
            if (baseKey.empty())
                return refuse(lineNo, raw, "base inconnue : \xC2\xAB sombre \xC2\xBB, \xC2\xAB claire \xC2\xBB ou le nom d'un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9.");
        } else if (k == "description") {
            description = value;
        } else if (k == "auteur" || k == "author") {
            author = value;
        } else if (const auto* d = defOf(key)) {
            gfx::Color col{};
            if (!parseThemeColor(value, col, d->key.alpha))
                return refuse(lineNo, raw, d->key.alpha ? "une couleur s'\xC3\xA9" "crit #RRGGBB (ou #RRGGBBAA)."
                                                        : "une couleur s'\xC3\xA9" "crit #RRGGBB.");
            sets.push_back({d, col});
        } else {
            r.notes.push_back("ligne " + std::to_string(lineNo) + " : cl\xC3\xA9 inconnue " + guillemets(key) + ", ignor\xC3\xA9" "e");
        }
        if (end >= text.size()) break;
    }
    if (name.empty()) return refuse(0, {}, "le nom manque (une ligne \xC2\xAB nom = ... \xC2\xBB).");

    // La base : dite, sinon devinee du fond des panneaux (sombre ou claire).
    if (baseKey.empty()) {
        bool dark = false;
        for (const auto& s : sets)
            if (s.def->slot == Slot::PanelBg) dark = luminance(s.color) < 0.4;
        baseKey = dark ? "Dark" : "Light";
        r.notes.push_back("pas de ligne \xC2\xAB base = \xE2\x80\xA6 \xC2\xBB : base " + std::string(dark ? "sombre" : "claire"));
    }
    Theme t = Theme::byName(baseKey);
    std::vector<bool> given(defs().size(), false);
    for (const auto& s : sets) {
        *slotRef(t, s.def->slot) = s.color;
        given[static_cast<std::size_t>(s.def - defs().data())] = true;
    }
    r.missing = static_cast<int>(std::count(given.begin(), given.end(), false));
    t.name = name;
    t.base = baseKey;
    t.description = description;
    t.author = author;
    t.user = true;
    if (!family.empty()) {
        const auto f = Theme::familyOf(family);
        if (f.empty()) {
            r.notes.push_back("ligne " + std::to_string(familyLine) + " : famille inconnue " + guillemets(family) + ", rang\xC3\xA9 dans "
                              + (t.isDark() ? "Sombres" : "Clairs"));
            t.family = t.isDark() ? "Sombres" : "Clairs";
        } else {
            t.family = f;
        }
    }
    // Sans famille : celle de la base (Nord : Colores ; sombre : Sombres).
    r.ok = true;
    r.theme = std::move(t);
    return r;
}

ThemeRead readThemeFile(const std::string& path) {
    ThemeRead r;
    std::error_code ec;
    const auto p = pathOf8(path);
    const auto size = fs::file_size(p, ec);
    if (ec) {
        r.error = "fichier introuvable ou illisible : " + path;
        return r;
    }
    if (size > 256 * 1024) {
        r.error = "ce fichier est bien trop gros pour un th\xC3\xA8me (" + std::to_string(size / 1024) + " Ko)";
        return r;
    }
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        r.error = "fichier illisible : " + path;
        return r;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return readThemeText(buf.str());
}

bool writeThemeFile(const Theme& t, const std::string& path, std::string* error) {
    const auto p = pathOf8(path);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "impossible d'\xC3\xA9" "crire " + path;
        return false;
    }
    out << writeThemeText(t);
    out.close();
    if (!out) {
        if (error) *error = "l'\xC3\xA9" "criture de " + path + " n'a pas abouti";
        return false;
    }
    return true;
}

std::string themeNameProblem(std::string_view name) {
    const std::string n = trim(name);
    if (n.empty()) return "le nom est vide.";
    if (codePoints(n) > 60) return "60 caract\xC3\xA8res au plus.";
    for (const char ch : n)
        if (static_cast<unsigned char>(ch) < 0x20) return "le nom tient sur une ligne.";
    return {};
}

std::string freeThemeName(std::string_view wanted, std::string_view except) {
    std::string base = trim(wanted);
    if (base.empty()) base = "Mon th\xC3\xA8me";
    const auto taken = [&](const std::string& n) {
        if (Theme::isBuiltIn(n)) return true;
        const auto f = Theme::fold(n);
        if (!except.empty() && Theme::fold(except) == f) return false;
        for (const auto& u : Theme::userThemes())
            if (Theme::fold(u.name) == f) return true;
        return false;
    };
    if (!taken(base)) return base;
    // "Nord (2)" deja pris : "Nord (3)", pas "Nord (2) (2)".
    if (base.size() > 4 && base.back() == ')') {
        const auto open = base.rfind(" (");
        if (open != std::string::npos
            && std::all_of(base.begin() + static_cast<std::ptrdiff_t>(open) + 2, base.end() - 1, [](char ch) { return ch >= '0' && ch <= '9'; })
            && open + 3 < base.size())
            base.erase(open);
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string candidate = base + " (" + std::to_string(n) + ")";
        if (!taken(candidate)) return candidate;
    }
    return base + " (1000)";
}

// ============================================== les themes de l'utilisateur ===
namespace {

struct Library {
    std::string                        folder;
    std::map<std::string, std::string> files;     // nom replie -> chemin (UTF-8)
};
Library& library() {
    static Library l;
    return l;
}

// Un nom de fichier sur de partout (Windows compris) : "Usine bleue.xpgtheme".
std::string fileStem(std::string_view name) {
    std::string s;
    for (const char ch : name) {
        const auto u = static_cast<unsigned char>(ch);
        if (u < 0x20 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) s += '_';
        else s += ch;
    }
    s = trim(s);
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.size() > 80) {
        std::size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
        s.resize(cut);
    }
    if (s.empty()) s = "theme";
    static const char* const kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
                                            "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
                                            "lpt7", "lpt8", "lpt9"};
    std::string lower = s;
    for (auto& ch : lower) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    for (const char* r : kReserved)
        if (lower == r) s += '_';
    return s;
}

std::string newFilePath(std::string_view name) {
    const auto& lib = library();
    const std::string stem = fileStem(name);
    for (int n = 1; n < 1000; ++n) {
        const std::string file = stem + (n == 1 ? std::string{} : " (" + std::to_string(n) + ")") + ".xpgtheme";
        const auto p = pathOf8(lib.folder) / pathOf8(file);
        std::error_code ec;
        if (!fs::exists(p, ec)) return utf8Of(p);
    }
    return utf8Of(pathOf8(lib.folder) / pathOf8(stem + " (1000).xpgtheme"));
}

bool sameColors(const Theme& a, const Theme& b) {
    for (const auto& d : defs())
        if (!same(*slotRef(const_cast<Theme&>(a), d.slot), *slotRef(const_cast<Theme&>(b), d.slot))) return false;
    return Theme::fold(a.family) == Theme::fold(b.family) && a.base == b.base;
}

} // namespace

void UserThemes::setFolder(std::string folder) {
    auto& lib = library();
    lib.folder = std::move(folder);
    lib.files.clear();
    if (lib.folder.empty()) return;
    std::error_code ec;
    fs::create_directories(pathOf8(lib.folder), ec);
}

const std::string& UserThemes::folder() { return library().folder; }

std::vector<std::string> UserThemes::load() {
    auto& lib = library();
    lib.files.clear();
    std::vector<std::string> problems;
    std::vector<Theme> found;
    if (lib.folder.empty()) {
        Theme::setUserThemes({});
        return problems;
    }
    std::vector<fs::path> paths;
    std::error_code ec;
    for (fs::directory_iterator it(pathOf8(lib.folder), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        auto ext = utf8Of(it->path().extension());
        for (auto& ch : ext) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        if (ext == ".xpgtheme") paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& p : paths) {
        const std::string file = utf8Of(p.filename());
        auto r = readThemeFile(utf8Of(p));
        if (!r.ok) {
            problems.push_back(file + " : " + r.error);
            continue;
        }
        if (Theme::isBuiltIn(r.theme.name)) {
            problems.push_back(file + " : " + guillemets(r.theme.name) + " est le nom d'un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9, il n'est pas charg\xC3\xA9");
            continue;
        }
        const auto key = Theme::fold(r.theme.name);
        if (lib.files.count(key)) {
            problems.push_back(file + " : " + guillemets(r.theme.name) + " est d\xC3\xA9j\xC3\xA0 dans " + utf8Of(pathOf8(lib.files[key]).filename()));
            continue;
        }
        lib.files[key] = utf8Of(p);
        found.push_back(std::move(r.theme));
    }
    Theme::setUserThemes(std::move(found));
    return problems;
}

std::string UserThemes::pathOf(std::string_view name) {
    const auto& lib = library();
    const auto it = lib.files.find(Theme::fold(name));
    return it == lib.files.end() ? std::string{} : it->second;
}

bool UserThemes::save(const Theme& t, std::string* error) {
    auto& lib = library();
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (lib.folder.empty()) return fail("pas de dossier pour tes th\xC3\xA8mes");
    if (auto why = themeNameProblem(t.name); !why.empty()) return fail(why);
    if (Theme::isBuiltIn(t.name)) return fail(guillemets(t.name) + " est un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9 : choisis un autre nom.");
    std::string path = pathOf(t.name);
    if (path.empty()) path = newFilePath(t.name);
    Theme copy = t;
    copy.name = trim(t.name);
    copy.user = true;
    if (!writeThemeFile(copy, path, error)) return false;
    lib.files[Theme::fold(copy.name)] = path;
    Theme::putUserTheme(std::move(copy));
    return true;
}

bool UserThemes::remove(std::string_view name, std::string* error) {
    auto& lib = library();
    const std::string path = pathOf(name);
    if (!path.empty()) {
        std::error_code ec;
        fs::remove(pathOf8(path), ec);
        if (ec) {
            if (error) *error = "impossible de retirer " + path + " (" + ec.message() + ")";
            return false;
        }
        lib.files.erase(Theme::fold(name));
    }
    if (!Theme::removeUserTheme(name) && path.empty()) {
        if (error) *error = guillemets(name) + " n'est pas un de tes th\xC3\xA8mes";
        return false;
    }
    return true;
}

bool UserThemes::rename(std::string_view from, const std::string& to, std::string* error) {
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    const Theme* src = nullptr;
    for (const auto& u : Theme::userThemes())
        if (Theme::fold(u.name) == Theme::fold(from)) src = &u;
    if (!src) return fail(guillemets(from) + " n'est pas un de tes th\xC3\xA8mes (un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9 ne se renomme pas)");
    const std::string newName = trim(to);
    if (auto why = themeNameProblem(newName); !why.empty()) return fail(why);
    if (Theme::isBuiltIn(newName)) return fail(guillemets(newName) + " est le nom d'un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9.");
    if (freeThemeName(newName, from) != newName) return fail(guillemets(newName) + " est d\xC3\xA9j\xC3\xA0 le nom d'un de tes th\xC3\xA8mes.");
    Theme t = *src;
    const std::string oldPath = pathOf(from);
    const std::string oldName = t.name;
    t.name = newName;
    auto& lib = library();
    // Le fichier suit le nom : un nouveau fichier, puis l'ancien retire. Un
    // nom qui ne change que de casse garde son fichier.
    lib.files.erase(Theme::fold(oldName));
    Theme::removeUserTheme(oldName);
    if (Theme::fold(newName) == Theme::fold(oldName) && !oldPath.empty()) lib.files[Theme::fold(newName)] = oldPath;
    if (!save(t, error)) {
        // Rien n'est perdu : l'ancien fichier est toujours la, on le reprend.
        Theme back = t;
        back.name = oldName;
        if (!oldPath.empty()) lib.files[Theme::fold(oldName)] = oldPath;
        Theme::putUserTheme(std::move(back));
        return false;
    }
    if (!oldPath.empty() && oldPath != pathOf(newName)) {
        std::error_code ec;
        fs::remove(pathOf8(oldPath), ec);
    }
    return true;
}

UserThemes::Imported UserThemes::importFile(const std::string& path) {
    Imported out;
    auto r = readThemeFile(path);
    if (!r.ok) {
        out.line = r.line;
        out.error = r.error;
        return out;
    }
    out.notes = r.notes;
    out.missing = r.missing;
    Theme t = std::move(r.theme);
    const std::string original = trim(t.name);
    // Le meme theme, deja la (un fichier importe deux fois, et peut-etre deja
    // renomme "Nuit (2)" la premiere fois) : rien a ajouter.
    const auto sameName = [&](const std::string& n) {
        if (Theme::fold(n) == Theme::fold(original)) return true;
        return n.size() > original.size() + 3 && n.compare(0, original.size() + 2, original + " (") == 0 && n.back() == ')';
    };
    for (const auto& u : Theme::userThemes())
        if (sameName(u.name) && sameColors(u, t)) {
            out.ok = true;
            out.name = u.name;
            out.notes.push_back(guillemets(u.name) + " est d\xC3\xA9j\xC3\xA0 dans tes th\xC3\xA8mes, \xC3\xA0 l'identique");
            return out;
        }
    t.name = freeThemeName(original);
    if (t.name != original) out.renamedFrom = original;
    std::string why;
    if (!save(t, &why)) {
        out.error = why;
        return out;
    }
    out.ok = true;
    out.name = t.name;
    return out;
}

bool UserThemes::exportTheme(std::string_view name, const std::string& path, std::string* error) {
    const auto key = Theme::keyOf(name);
    if (key.empty()) {
        if (error) *error = "th\xC3\xA8me inconnu : " + std::string(name);
        return false;
    }
    Theme t = Theme::byName(key);
    // Un theme integre s'exporte sous son libelle ("Nuit", pas "Night") : c'est
    // le nom qu'il portera chez celui qui l'importe.
    if (!t.user) t.name = Theme::labelOf(key);
    return writeThemeFile(t, path, error);
}

} // namespace ui
