#pragma once
// 1.11 (T1, tranche 13) : le nom d'une touche tel que le calque le dessine.
//
// Le geste `touche` garde le nom ecrit dans le .tuto ("Return", "maj+end",
// "ctrl+a" : les noms de ScriptRunner, que lit app::UiDriver::parseKey). A
// l'ecran, la touche dessinee au-dessus de la bulle porte le nom francais, celui
// des menus et des infobulles de l'appli : "Entree", "Maj+Fin", "Ctrl+A", les
// fleches. Un nom inconnu est rendu tel quel (et `known` dit faux).
//
// Sans ecran, sans fichier .cpp : tutorial_test le verifie, et verifie que
// chaque `touche` des tutoriels ecrits a un nom connu.
// Les noms acceptes sont ceux de UiDriver::parseKey (garder les deux tables ensemble).

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace help {

inline std::string keyLabel(std::string_view combo, bool* known = nullptr) {
    static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
        {"return", "Entr\xC3\xA9" "e"}, {"entree", "Entr\xC3\xA9" "e"},
        {"escape", "\xC3\x89" "chap"}, {"echap", "\xC3\x89" "chap"},
        {"delete", "Suppr"}, {"suppr", "Suppr"},
        {"tab", "Tab"},
        {"space", "Espace"}, {"espace", "Espace"},
        {"backspace", "Retour arri\xC3\xA8re"}, {"retour", "Retour arri\xC3\xA8re"},
        {"insert", "Inser"},
        {"left", "\xE2\x86\x90"}, {"gauche", "\xE2\x86\x90"},
        {"right", "\xE2\x86\x92"}, {"droite", "\xE2\x86\x92"},
        {"up", "\xE2\x86\x91"}, {"haut", "\xE2\x86\x91"},
        {"down", "\xE2\x86\x93"}, {"bas", "\xE2\x86\x93"},
        {"home", "D\xC3\xA9" "but"}, {"debut", "D\xC3\xA9" "but"},
        {"end", "Fin"}, {"fin", "Fin"},
        {"pageup", "Pg pr\xC3\xA9" "c"}, {"pagedown", "Pg suiv"},
        {"ctrl", "Ctrl"}, {"maj", "Maj"}, {"shift", "Maj"}, {"alt", "Alt"},
    };
    auto lower = [](std::string_view s) {
        std::string out(s);
        for (auto& ch : out)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return out;
    };
    // Un morceau : un nom de la table, F1 a F12, une lettre ou un chiffre.
    auto part = [&](std::string_view p, bool modifier) -> std::pair<std::string, bool> {
        const auto low = lower(p);
        for (const auto& [name, label] : kNames)
            if (low == name) {
                const bool mod = name == "ctrl" || name == "maj" || name == "shift" || name == "alt";
                if (mod == modifier) return {std::string(label), true};
            }
        if (modifier) return {std::string(p), false};
        // 1.12.2 : les signes des raccourcis des editeurs (Ctrl+/, Ctrl+], Ctrl+., Ctrl+,, Ctrl+-).
        if (low.size() == 1 && std::string_view("/.,-]:$;").find(low[0]) != std::string_view::npos) return {low, true};
        if (low.size() == 1 && ((low[0] >= 'a' && low[0] <= 'z') || (low[0] >= '0' && low[0] <= '9'))) {
            std::string up = low;
            if (up[0] >= 'a' && up[0] <= 'z') up[0] = static_cast<char>(up[0] - 'a' + 'A');
            return {up, true};
        }
        if (low.size() >= 2 && low.size() <= 3 && low[0] == 'f') {
            const auto n = low.substr(1);
            bool digits = true;
            for (const char ch : n) digits = digits && ch >= '0' && ch <= '9';
            if (digits && n[0] != '0' && std::stoi(n) <= 12) return {"F" + n, true};
        }
        return {std::string(p), false};
    };
    // Comme parseKey : les modificateurs devant, separes par "+" ("ctrl+maj+tab").
    std::string out;
    bool ok = !combo.empty();
    std::string_view rest = combo;
    for (;;) {
        const auto plus = rest.find('+');
        if (plus == std::string_view::npos || plus + 1 >= rest.size()) break;
        const auto [label, isKnown] = part(rest.substr(0, plus), true);
        out += label;
        out += '+';
        ok = ok && isKnown;
        rest = rest.substr(plus + 1);
    }
    const auto [label, isKnown] = part(rest, false);
    out += label;
    ok = ok && isKnown;
    if (known) *known = ok;
    return ok ? out : std::string(combo);
}

} // namespace help
