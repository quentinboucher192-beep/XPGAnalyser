#include "HmiForms.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hmi {

const FormField* FormLayout::field(std::string_view name) const noexcept {
    for (const auto& f : fields) if (f.name == name) return &f;
    return nullptr;
}

namespace {

double scaleOf(const Object& o) { return std::clamp(o.number("fontSize", 15) / 15.0, 0.6, 3.0); }

// Un champ sous son libelle, a partir de y (avance y).
FormField labelled(std::string name, std::string label, bool secret, double pad, double w, double s, double& y) {
    FormField f;
    f.name = std::move(name);
    f.label = std::move(label);
    f.secret = secret;
    f.labelBox = {pad, y, std::max(0.0, w - 2 * pad), 16 * s};
    y += 18 * s;
    f.box = {pad, y, std::max(0.0, w - 2 * pad), 30 * s};
    y += 36 * s;
    return f;
}

} // namespace

FormLayout loginLayout(const Object& o, double w, double h, bool codeLabel) {
    (void)h;
    FormLayout l;
    const double s = scaleOf(o);
    l.scale = s;
    const double pad = 16 * s;
    double y = 12 * s;
    l.title = {pad, y, std::max(0.0, w - 2 * pad), 24 * s};
    y += 32 * s;
    l.fields.push_back(labelled("utilisateur", "Utilisateur", false, pad, w, s, y));
    const Box& ub = l.fields.back().box;
    if (o.flag("userList", true)) {
        const double a = ub.h;
        l.prev = {ub.x, ub.y, a, a};
        l.next = {ub.right() - a, ub.y, a, a};
        l.userBox = {ub.x + a + 4 * s, ub.y, std::max(0.0, ub.w - 2 * a - 8 * s), ub.h};
    }
    l.fields.push_back(labelled("motdepasse", codeLabel ? "Code (6 chiffres)" : "Mot de passe", true, pad, w, s, y));
    l.button = {pad, y + 2 * s, std::max(0.0, w - 2 * pad), 34 * s};
    y += 42 * s;
    l.message = {pad, y, std::max(0.0, w - 2 * pad), 20 * s};
    return l;
}

FormLayout passwordLayout(const Object& o, double w, double h) {
    (void)h;
    FormLayout l;
    const double s = scaleOf(o);
    l.scale = s;
    const double pad = 16 * s;
    double y = 12 * s;
    l.title = {pad, y, std::max(0.0, w - 2 * pad), 24 * s};
    y += 32 * s;
    l.fields.push_back(labelled("ancien", "Mot de passe actuel", true, pad, w, s, y));
    l.fields.push_back(labelled("nouveau", "Nouveau mot de passe", true, pad, w, s, y));
    l.fields.push_back(labelled("confirmation", "Confirmation", true, pad, w, s, y));
    l.button = {pad, y + 2 * s, std::max(0.0, w - 2 * pad), 34 * s};
    y += 42 * s;
    l.message = {pad, y, std::max(0.0, w - 2 * pad), 20 * s};
    return l;
}

std::string formHit(const Object& o, double w, double h, double x, double y) {
    switch (o.kind) {
        case Kind::InputField:
            return x >= 0 && y >= 0 && x <= w && y <= h ? "champ" : std::string{};
        case Kind::LogoutButton:
            return x >= 0 && y >= 0 && x <= w && y <= h ? "bouton" : std::string{};
        case Kind::LoginPanel: {
            const auto l = loginLayout(o, w, h);
            if (o.flag("userList", true)) {
                if (l.prev.contains(x, y)) return "precedent";
                if (l.next.contains(x, y)) return "suivant";
                if (l.userBox.contains(x, y)) return "suivant";
            } else if (const auto* f = l.field("utilisateur"); f && f->box.contains(x, y)) {
                return "champ:utilisateur";
            }
            if (const auto* f = l.field("motdepasse"); f && f->box.contains(x, y)) return "champ:motdepasse";
            if (l.button.contains(x, y)) return "bouton";
            return {};
        }
        case Kind::PasswordChange: {
            const auto l = passwordLayout(o, w, h);
            for (const auto& f : l.fields)
                if (f.box.contains(x, y)) return "champ:" + f.name;
            if (l.button.contains(x, y)) return "bouton";
            return {};
        }
        default:
            return {};
    }
}

std::string userManagerHit(const Object& o, double w, double h, double x, double y, std::size_t users) {
    // La meme geometrie que le gestionnaire de recettes (boutons, tableau).
    return recipeManagerHit(o, w, h, x, y, users);
}

std::string maskedText(std::string_view text) {
    std::string out;
    for (const char c : text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) out += "\xE2\x97\x8F";   // un point plein
    return out;
}

std::string shortDuration(double seconds) {
    if (seconds < 0) return "-";
    const long long t = static_cast<long long>(std::floor(seconds));
    char b[48];
    if (t >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min", t / 3600, (t / 60) % 60);
    else if (t >= 60) std::snprintf(b, sizeof b, "%lld min %02lld s", t / 60, t % 60);
    else std::snprintf(b, sizeof b, "%lld s", t);
    return b;
}

std::string fillUserInfo(std::string_view pattern, const UserInfoValues& v) {
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '{') { out += pattern[i]; continue; }
        const auto end = pattern.find('}', i);
        if (end == std::string_view::npos) { out += pattern.substr(i); break; }
        const std::string_view key = pattern.substr(i + 1, end - i - 1);
        if (key == "login") out += v.login;
        else if (key == "nom") out += v.name.empty() ? v.login : v.name;
        else if (key == "groupe") out += v.group.empty() ? std::string("-") : v.group;
        else if (key == "niveau") out += std::to_string(v.level);
        else if (key == "depuis") out += shortDuration(v.since);
        else if (key == "reste") out += v.remaining < 0 ? std::string("-") : shortDuration(v.remaining);
        else out += "{" + std::string(key) + "}";
        i = end;
    }
    return out;
}

// ------------------------------------------------------------------ clavier ---
Box keyboardSize(std::string_view mode) {
    if (mode == "numerique") return {0, 0, 280, 250};
    return {0, 0, 720, 270};
}

std::vector<KeyCap> keyboardLayout(std::string_view mode, double w, double h, bool shift) {
    std::vector<KeyCap> keys;
    const double pad = 8, gap = 6;
    if (mode == "numerique") {
        // 4 colonnes x 4 lignes : les chiffres, la virgule, le signe, effacer,
        // annuler, valider.
        const char* rows[4][4] = {{"7", "8", "9", "\xE2\x8C\xAB"},
                                  {"4", "5", "6", "\xC3\x89" "chap"},
                                  {"1", "2", "3", "-"},
                                  {"0", ",", ".", "\xE2\x86\xB5"}};
        const double kw = (w - 2 * pad - 3 * gap) / 4, kh = (h - 2 * pad - 3 * gap) / 4;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                KeyCap k;
                k.label = rows[r][c];
                k.box = {pad + c * (kw + gap), pad + r * (kh + gap), kw, kh};
                if (r == 0 && c == 3) k.command = "retour";
                else if (r == 1 && c == 3) k.command = "echap";
                else if (r == 3 && c == 3) k.command = "entree";
                else k.text = k.label;
                keys.push_back(std::move(k));
            }
        return keys;
    }
    // Complet : AZERTY, 5 rangees.
    const std::vector<std::vector<std::string>> letters = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
        {"a", "z", "e", "r", "t", "y", "u", "i", "o", "p"},
        {"q", "s", "d", "f", "g", "h", "j", "k", "l", "m"},
        {"w", "x", "c", "v", "b", "n", ",", ".", "-", "_"},
    };
    const double cols = 11;
    const double kw = (w - 2 * pad - (cols - 1) * gap) / cols, kh = (h - 2 * pad - 4 * gap) / 5;
    for (std::size_t r = 0; r < letters.size(); ++r) {
        const double indent = r == 2 ? kw * 0.3 : r == 3 ? kw * 0.6 : 0;
        for (std::size_t c = 0; c < letters[r].size(); ++c) {
            KeyCap k;
            std::string t = letters[r][c];
            if (shift && t.size() == 1 && t[0] >= 'a' && t[0] <= 'z') t[0] = static_cast<char>(t[0] - 'a' + 'A');
            k.label = t;
            k.text = t;
            k.box = {pad + indent + static_cast<double>(c) * (kw + gap), pad + static_cast<double>(r) * (kh + gap), kw, kh};
            keys.push_back(std::move(k));
        }
        // La derniere colonne : effacer, puis entree sur deux rangees.
        KeyCap side;
        const double x = pad + (cols - 1) * (kw + gap);
        if (r == 0) { side.label = "\xE2\x8C\xAB"; side.command = "retour"; side.box = {x, pad, kw, kh}; keys.push_back(side); }
        if (r == 1) {
            side.label = "\xE2\x86\xB5"; side.command = "entree";
            side.box = {x, pad + (kh + gap), kw, 2 * kh + gap};
            keys.push_back(side);
        }
    }
    const double y = pad + 4 * (kh + gap);
    const auto add = [&](std::string label, std::string text, std::string command, double x, double width) {
        KeyCap k;
        k.label = std::move(label);
        k.text = std::move(text);
        k.command = std::move(command);
        k.box = {x, y, width, kh};
        keys.push_back(std::move(k));
    };
    add(shift ? "MAJ" : "Maj", {}, "maj", pad, kw * 1.5);
    add("\xC3\x89" "chap", {}, "echap", pad + kw * 1.5 + gap, kw * 1.5);
    add("espace", " ", {}, pad + kw * 3 + 2 * gap, kw * 5 + 4 * gap);
    add("@", "@", {}, pad + kw * 8 + 7 * gap, kw);
    add("\xE2\x86\x90", {}, "gauche", pad + kw * 9 + 8 * gap, kw);
    add("\xE2\x86\x92", {}, "droite", pad + kw * 10 + 9 * gap, kw);
    return keys;
}

int keyboardHit(const std::vector<KeyCap>& keys, double x, double y) {
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i].box.contains(x, y)) return static_cast<int>(i);
    return -1;
}

} // namespace hmi
