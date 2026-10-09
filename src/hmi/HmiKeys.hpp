// =============================================================================
//  hmi/HmiKeys.hpp - les touches des raccourcis des vues                1.11.23
// -----------------------------------------------------------------------------
//  Un raccourci d'une vue (ou d'une popup) : une touche, avec ou sans Ctrl, Maj,
//  Alt, liee a une action (Action::key, son declencheur : Touche enfoncee,
//  relachee, maintenue, repetee). La touche s'ecrit comme on la lit sur le
//  clavier - "F5", "Ctrl+S", "Maj+Entree", "Alt+Haut" - sans casse ni accents ;
//  elle s'enregistre sous une forme ASCII stable ("Shift+Enter", "Alt+Up") et se
//  montre en francais ("Maj+Entree" avec son accent).
//
//  Les chiffres : Digit0..Digit9 (en ST, SYS.Key.1 se lirait comme un bit) ; ils
//  se tapent "0".."9". SYS.Key.<touche> : la touche est enfoncee (BOOL).
//
//  EN-TETE SEUL, sans l'interface : le moteur, la verification, le stockage et
//  l'editeur le lisent ; la correspondance avec ui::Key est faite par le canevas.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hmi::keys {

inline constexpr int kHoldDefaultMs   = 1000;   // Touche maintenue : la duree par defaut
inline constexpr int kRepeatDefaultMs = 200;    // Touche repetee : la periode par defaut

struct KeyName {
    std::string_view token;     // la forme enregistree : "Enter", "Digit5", "F2"
    std::string_view label;     // la forme montree : "Entree" (accentue), "5", "F2"
    std::string_view alias1{};  // d'autres facons de l'ecrire (sans accents ni casse)
    std::string_view alias2{};
};

inline constexpr KeyName kKeys[] = {
    {"A", "A"}, {"B", "B"}, {"C", "C"}, {"D", "D"}, {"E", "E"}, {"F", "F"}, {"G", "G"}, {"H", "H"}, {"I", "I"},
    {"J", "J"}, {"K", "K"}, {"L", "L"}, {"M", "M"}, {"N", "N"}, {"O", "O"}, {"P", "P"}, {"Q", "Q"}, {"R", "R"},
    {"S", "S"}, {"T", "T"}, {"U", "U"}, {"V", "V"}, {"W", "W"}, {"X", "X"}, {"Y", "Y"}, {"Z", "Z"},
    {"Digit0", "0", "0"}, {"Digit1", "1", "1"}, {"Digit2", "2", "2"}, {"Digit3", "3", "3"}, {"Digit4", "4", "4"},
    {"Digit5", "5", "5"}, {"Digit6", "6", "6"}, {"Digit7", "7", "7"}, {"Digit8", "8", "8"}, {"Digit9", "9", "9"},
    {"F1", "F1"}, {"F2", "F2"}, {"F3", "F3"}, {"F4", "F4"}, {"F5", "F5"}, {"F6", "F6"}, {"F7", "F7"},
    {"F8", "F8"}, {"F9", "F9"}, {"F10", "F10"}, {"F11", "F11"}, {"F12", "F12"},
    {"Enter", "Entr\xC3\xA9" "e", "return", "retour chariot"},
    {"Escape", "\xC3\x89" "chap", "esc", "echappement"},
    {"Space", "Espace", "barre espace"},
    {"Tab", "Tab", "tabulation"},
    {"Backspace", "Retour arri\xC3\xA8re", "retour", "effacement"},
    {"Delete", "Suppr", "supprimer", "del"},
    {"Insert", "Inser", "ins", "insertion"},
    {"Home", "D\xC3\xA9" "but", "origine"},
    {"End", "Fin"},
    {"PageUp", "Page pr\xC3\xA9" "c", "pgprec", "page precedente"},
    {"PageDown", "Page suiv", "pgsuiv", "page suivante"},
    {"Up", "Haut", "fleche haut"},
    {"Down", "Bas", "fleche bas"},
    {"Left", "Gauche", "fleche gauche"},
    {"Right", "Droite", "fleche droite"},
};

// Pour comparer : en minuscules, sans accents (les lettres accentuees du francais
// en UTF-8), sans espaces, tirets, points ni soulignes.
[[nodiscard]] inline std::string fold(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {                 // e accentues, a, i, o, u, c cedille
            const auto d = static_cast<unsigned char>(s[i + 1]);
            ++i;
            switch (d) {
                case 0xA0: case 0xA2: case 0xA4: case 0x80: case 0x82: case 0x84: out += 'a'; break;
                case 0xA7: case 0x87: out += 'c'; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0x88: case 0x89: case 0x8A: case 0x8B: out += 'e'; break;
                case 0xAE: case 0xAF: case 0x8E: case 0x8F: out += 'i'; break;
                case 0xB4: case 0xB6: case 0x94: case 0x96: out += 'o'; break;
                case 0xB9: case 0xBB: case 0xBC: case 0x99: case 0x9B: case 0x9C: out += 'u'; break;
                default: break;
            }
            continue;
        }
        if (c == ' ' || c == '-' || c == '_' || c == '.' || c == '\t') continue;
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

// La touche d'un nom (le sien, son libelle, un alias) ; nul : inconnue.
[[nodiscard]] inline const KeyName* keyOf(std::string_view name) {
    const std::string f = fold(name);
    if (f.empty()) return nullptr;
    for (const auto& k : kKeys)
        if (fold(k.token) == f || fold(k.label) == f || (!k.alias1.empty() && fold(k.alias1) == f)
            || (!k.alias2.empty() && fold(k.alias2) == f))
            return &k;
    return nullptr;
}
// Sa forme enregistree ("" : inconnue).
[[nodiscard]] inline std::string_view tokenOf(std::string_view name) {
    const auto* k = keyOf(name);
    return k ? k->token : std::string_view{};
}

struct Chord {
    std::string key;            // le jeton : "F5", "Enter", "Digit1"
    bool        ctrl{false}, shift{false}, alt{false};
    bool operator==(const Chord&) const = default;
};

// "Ctrl+Maj+F5", "maj + entree", "F2" ; nul (why le dit) : rien, un modificateur
// inconnu, une touche inconnue, ou des modificateurs seuls.
[[nodiscard]] inline std::optional<Chord> parseChord(std::string_view text, std::string* why = nullptr) {
    const auto fail = [&](std::string message) -> std::optional<Chord> {
        if (why) *why = std::move(message);
        return std::nullopt;
    };
    std::string_view rest = text;
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) rest.remove_suffix(1);
    if (rest.empty()) return fail("aucune touche");
    Chord c;
    for (;;) {
        const auto plus = rest.find('+');
        if (plus == std::string_view::npos) break;
        const std::string mod = fold(rest.substr(0, plus));
        if (mod == "ctrl" || mod == "control" || mod == "controle" || mod == "ctl") c.ctrl = true;
        else if (mod == "maj" || mod == "shift" || mod == "majuscule") c.shift = true;
        else if (mod == "alt") c.alt = true;
        else return fail("modificateur inconnu : " + std::string(rest.substr(0, plus)) + " (Ctrl, Maj ou Alt)");
        rest.remove_prefix(plus + 1);
    }
    if (fold(rest).empty()) return fail("une touche apr\xC3\xA8s Ctrl, Maj ou Alt");
    const auto* k = keyOf(rest);
    if (!k)
        return fail("touche inconnue : " + std::string(rest)
                    + " (A \xC3\xA0 Z, 0 \xC3\xA0 9, F1 \xC3\xA0 F12, Entr\xC3\xA9" "e, \xC3\x89" "chap, Espace, Suppr, Inser, D\xC3\xA9" "but, Fin, "
                      "Page pr\xC3\xA9" "c, Page suiv, Haut, Bas, Gauche, Droite)");
    c.key = std::string(k->token);
    return c;
}

// La forme enregistree : "Ctrl+Shift+Alt+F5".
[[nodiscard]] inline std::string canonical(const Chord& c) {
    return std::string(c.ctrl ? "Ctrl+" : "") + (c.shift ? "Shift+" : "") + (c.alt ? "Alt+" : "") + c.key;
}
// La forme montree : "Ctrl+Maj+Alt+Entree" (accentuee).
[[nodiscard]] inline std::string label(const Chord& c) {
    const auto* k = keyOf(c.key);
    return std::string(c.ctrl ? "Ctrl+" : "") + (c.shift ? "Maj+" : "") + (c.alt ? "Alt+" : "")
         + (k ? std::string(k->label) : c.key);
}
// Un texte enregistre, montre en francais (tel quel s'il ne se lit pas).
[[nodiscard]] inline std::string label(std::string_view stored) {
    const auto c = parseChord(stored);
    return c ? label(*c) : std::string(stored);
}
// La touche enregistree `stored` est-elle celle-ci (memes Ctrl, Maj, Alt) ?
[[nodiscard]] inline bool matches(std::string_view stored, const Chord& c) {
    const auto s = parseChord(stored);
    return s && *s == c;
}
[[nodiscard]] inline bool isFunctionKey(std::string_view token) {
    return token.size() >= 2 && token.size() <= 3 && token.front() == 'F' && token[1] >= '0' && token[1] <= '9';
}
// Une touche que l'application ou le poste gardent : la raison ("" : libre).
[[nodiscard]] inline std::string reserved(const Chord& c) {
    if (c.key == "F1" && !c.ctrl && !c.alt) return "F1 ouvre l'aide";
    if ((c.key == "F11" || c.key == "F12") && !c.ctrl && !c.alt)
        return c.key + " est pris par le poste d'exploitation (plein \xC3\xA9" "cran, capture)";
    if (c.key == "Q" && c.ctrl && c.alt) return "Ctrl+Alt+Q quitte le poste d'exploitation";
    if (c.key == "S" && c.ctrl && c.alt) return "Ctrl+Alt+S ouvre le menu du poste d'exploitation";
    if (c.key == "Tab" && !c.ctrl && !c.alt) return "Tab passe d'un champ \xC3\xA0 l'autre";
    if (c.key == "F4" && c.alt && !c.ctrl) return "Alt+F4 ferme la fen\xC3\xAAtre";
    return {};
}

} // namespace hmi::keys
