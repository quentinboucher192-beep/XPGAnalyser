// =============================================================================
//  ui/KeyMap.cpp - 1.12.2 : les raccourcis des editeurs de code (voir KeyMap.hpp)
// =============================================================================
#include "KeyMap.hpp"

#include <algorithm>
#include <atomic>

namespace ui::keymap {

namespace {

std::atomic<Profile> gProfile{ Profile::VisualStudio };

// Le repli des noms : minuscules ASCII, sans accents francais (é è ê -> e, É -> e).
std::string fold(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) { out += 'e'; ++i; continue; }   // e accentues
            if (d == 0xA0 || d == 0xA2 || d == 0x80 || d == 0x82) { out += 'a'; ++i; continue; }
        }
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

struct Name { Key key; std::string_view shown; };
// L'ordre compte : le premier nom d'une touche est celui qu'on montre.
const Name kNames[] = {
    { Key::Escape, "\xC3\x89" "chap" }, { Key::Return, "Entr\xC3\xA9" "e" }, { Key::Tab, "Tab" },
    { Key::Backspace, "Retour" }, { Key::Delete, "Suppr" }, { Key::Insert, "Inser" },
    { Key::Left, "\xE2\x86\x90" }, { Key::Right, "\xE2\x86\x92" }, { Key::Up, "\xE2\x86\x91" }, { Key::Down, "\xE2\x86\x93" },
    { Key::Home, "D\xC3\xA9" "but" }, { Key::End, "Fin" }, { Key::PageUp, "Pg.Pr\xC3\xA9" "c" }, { Key::PageDown, "Pg.Suiv" },
    { Key::Space, "Espace" },
    { Key::A, "A" }, { Key::B, "B" }, { Key::C, "C" }, { Key::D, "D" }, { Key::E, "E" }, { Key::F, "F" }, { Key::G, "G" },
    { Key::H, "H" }, { Key::I, "I" }, { Key::J, "J" }, { Key::K, "K" }, { Key::L, "L" }, { Key::M, "M" }, { Key::N, "N" },
    { Key::O, "O" }, { Key::P, "P" }, { Key::Q, "Q" }, { Key::R, "R" }, { Key::S, "S" }, { Key::T, "T" }, { Key::U, "U" },
    { Key::V, "V" }, { Key::W, "W" }, { Key::X, "X" }, { Key::Y, "Y" }, { Key::Z, "Z" },
    { Key::Num0, "0" }, { Key::Num1, "1" }, { Key::Num2, "2" }, { Key::Num3, "3" }, { Key::Num4, "4" },
    { Key::Num5, "5" }, { Key::Num6, "6" }, { Key::Num7, "7" }, { Key::Num8, "8" }, { Key::Num9, "9" },
    { Key::F1, "F1" }, { Key::F2, "F2" }, { Key::F3, "F3" }, { Key::F4, "F4" }, { Key::F5, "F5" }, { Key::F6, "F6" },
    { Key::F7, "F7" }, { Key::F8, "F8" }, { Key::F9, "F9" }, { Key::F10, "F10" }, { Key::F11, "F11" }, { Key::F12, "F12" },
    { Key::Slash, "/" }, { Key::Period, "." }, { Key::Comma, "," }, { Key::Minus, "-" }, { Key::RightBracket, "]" },
    { Key::Colon, ":" }, { Key::Dollar, "$" }, { Key::Semicolon, ";" },
};
// Les autres facons de l'ecrire (deja repliees : minuscules, sans accents).
struct Alias { std::string_view text; Key key; };
const Alias kAliases[] = {
    { "escape", Key::Escape }, { "esc", Key::Escape }, { "echap", Key::Escape },
    { "entree", Key::Return }, { "return", Key::Return }, { "enter", Key::Return },
    { "retour", Key::Backspace }, { "backspace", Key::Backspace },
    { "suppr", Key::Delete }, { "delete", Key::Delete }, { "del", Key::Delete },
    { "inser", Key::Insert }, { "insert", Key::Insert },
    { "gauche", Key::Left }, { "left", Key::Left }, { "droite", Key::Right }, { "right", Key::Right },
    { "haut", Key::Up }, { "up", Key::Up }, { "bas", Key::Down }, { "down", Key::Down },
    { "debut", Key::Home }, { "home", Key::Home }, { "fin", Key::End }, { "end", Key::End },
    { "pg.prec", Key::PageUp }, { "pgprec", Key::PageUp }, { "pageup", Key::PageUp },
    { "pg.suiv", Key::PageDown }, { "pgsuiv", Key::PageDown }, { "pagedown", Key::PageDown },
    { "espace", Key::Space }, { "space", Key::Space },
};

const std::vector<Command>& catalog() {
    static const std::vector<Command> k = {
        // ---- Edition
        { "commenter", "Commenter les lignes (//)", "\xC3\x89" "dition" },
        { "decommenter", "D\xC3\xA9" "commenter les lignes", "\xC3\x89" "dition" },
        { "basculerCommentaire", "Basculer le commentaire", "\xC3\x89" "dition" },
        { "dupliquer", "Dupliquer la ligne ou la s\xC3\xA9lection", "\xC3\x89" "dition" },
        { "monterLignes", "Monter les lignes", "\xC3\x89" "dition" },
        { "descendreLignes", "Descendre les lignes", "\xC3\x89" "dition" },
        { "couperLigne", "Couper la ligne", "\xC3\x89" "dition" },
        { "supprimerLigne", "Supprimer la ligne", "\xC3\x89" "dition" },
        { "copier", "Copier (rien de choisi : la ligne)", "\xC3\x89" "dition" },
        { "couper", "Couper (rien de choisi : la ligne)", "\xC3\x89" "dition" },
        { "insererAuDessus", "Ins\xC3\xA9rer une ligne au-dessus", "\xC3\x89" "dition" },
        { "insererAuDessous", "Ins\xC3\xA9rer une ligne au-dessous", "\xC3\x89" "dition" },
        { "majuscules", "Mettre en MAJUSCULES", "\xC3\x89" "dition" },
        { "minuscules", "Mettre en minuscules", "\xC3\x89" "dition" },
        { "effacerMotAvant", "Effacer le mot d'avant", "\xC3\x89" "dition" },
        { "effacerMotApres", "Effacer le mot d'apr\xC3\xA8s", "\xC3\x89" "dition" },
        { "formaterDocument", "Mettre en forme le document", "\xC3\x89" "dition" },
        { "formaterSelection", "Mettre en forme la s\xC3\xA9lection", "\xC3\x89" "dition" },
        { "entourer", "Entourer de\xE2\x80\xA6 (IF, FOR, WHILE, CASE, TRY)", "\xC3\x89" "dition" },
        { "extrait", "Ins\xC3\xA9rer un extrait", "\xC3\x89" "dition" },
        // ---- Selection et deplacement
        { "toutChoisir", "Tout choisir", "S\xC3\xA9lection" },
        { "choisirMot", "Choisir le mot", "S\xC3\xA9lection" },
        { "accolade", "Aller au bout du bloc (IF \xE2\x86\x94 END_IF)", "S\xC3\xA9lection" },
        { "choisirBloc", "Choisir jusqu'au bout du bloc", "S\xC3\xA9lection" },
        // ---- Rechercher
        { "rechercher", "Rechercher dans le document", "Rechercher" },
        { "remplacer", "Remplacer dans le document", "Rechercher" },
        { "suivant", "Suivant", "Rechercher" },
        { "precedent", "Pr\xC3\xA9" "c\xC3\xA9" "dent", "Rechercher" },
        { "rechercherMot", "Rechercher le mot sous le curseur", "Rechercher" },
        { "rechercherPartout", "Rechercher dans tout le projet", "Rechercher", true },
        // ---- Naviguer
        { "allerLigne", "Aller \xC3\xA0 la ligne", "Naviguer" },
        { "allerDefinition", "Aller \xC3\xA0 la d\xC3\xA9" "finition", "Naviguer", true },
        { "references", "Toutes les r\xC3\xA9" "f\xC3\xA9rences", "Naviguer", true },
        { "revenir", "Revenir o\xC3\xB9 l'on \xC3\xA9tait", "Naviguer" },
        { "renommer", "Renommer partout", "Naviguer", true },
        { "diagSuivant", "Faute suivante", "Naviguer" },
        { "diagPrecedent", "Faute pr\xC3\xA9" "c\xC3\xA9" "dente", "Naviguer" },
        { "corriger", "Corriger (le nom propos\xC3\xA9 par la faute)", "Naviguer" },
        { "signet", "Poser / retirer un signet", "Naviguer" },
        { "signetSuivant", "Signet suivant", "Naviguer" },
        { "signetPrecedent", "Signet pr\xC3\xA9" "c\xC3\xA9" "dent", "Naviguer" },
        { "allerA", "Aller \xC3\xA0 / Faire\xE2\x80\xA6", "Naviguer", true },
        // ---- Aide a la saisie
        { "completer", "Compl\xC3\xA9ter le mot", "Aide \xC3\xA0 la saisie" },
        { "infoParametres", "Les param\xC3\xA8tres de l'appel", "Aide \xC3\xA0 la saisie" },
        { "infoRapide", "Info rapide (le type, la signature)", "Aide \xC3\xA0 la saisie" },
        // ---- Plis
        { "plier", "Replier / d\xC3\xA9plier le bloc", "Plis" },
        { "plierTout", "Tout replier / tout d\xC3\xA9plier", "Plis" },
        { "plierNiveau", "Replier les blocs du premier niveau", "Plis" },
        // ---- Projet
        { "compiler", "Compiler le document", "Projet", true },
        { "generer", "G\xC3\xA9n\xC3\xA9rer", "Projet", true },
        { "simuler", "D\xC3\xA9marrer la simulation", "Projet", true },
        { "arreter", "Arr\xC3\xAAter la simulation", "Projet", true },
        { "fermer", "Fermer l'onglet", "Projet", true },
    };
    return k;
}

// Le profil Visual Studio, dans l'ordre de la page d'aide.
constexpr std::pair<std::string_view, std::string_view> kVs[] = {
    { "Ctrl+K, Ctrl+C", "commenter" }, { "Ctrl+K, Ctrl+U", "decommenter" },
    { "Ctrl+/", "basculerCommentaire" }, { "Ctrl+:", "basculerCommentaire" },
    { "Ctrl+D", "dupliquer" }, { "Alt+Haut", "monterLignes" }, { "Alt+Bas", "descendreLignes" },
    { "Ctrl+L", "couperLigne" }, { "Ctrl+Maj+L", "supprimerLigne" },
    { "Ctrl+C", "copier" }, { "Ctrl+X", "couper" },
    { "Ctrl+Entr\xC3\xA9" "e", "insererAuDessus" }, { "Ctrl+Maj+Entr\xC3\xA9" "e", "insererAuDessous" },
    { "Ctrl+Maj+U", "majuscules" }, { "Ctrl+U", "minuscules" },
    { "Ctrl+Retour", "effacerMotAvant" }, { "Ctrl+Suppr", "effacerMotApres" },
    { "Ctrl+K, Ctrl+D", "formaterDocument" }, { "Ctrl+K, Ctrl+F", "formaterSelection" },
    { "Ctrl+K, Ctrl+S", "entourer" }, { "Ctrl+K, Ctrl+X", "extrait" },
    { "Ctrl+A", "toutChoisir" }, { "Ctrl+W", "choisirMot" },
    { "Ctrl+]", "accolade" }, { "Ctrl+$", "accolade" }, { "Ctrl+Maj+]", "choisirBloc" }, { "Ctrl+Maj+$", "choisirBloc" },
    { "Ctrl+F", "rechercher" }, { "Ctrl+H", "remplacer" }, { "F3", "suivant" }, { "Maj+F3", "precedent" },
    { "Ctrl+F3", "rechercherMot" }, { "Ctrl+Maj+F", "rechercherPartout" },
    { "Ctrl+G", "allerLigne" }, { "F12", "allerDefinition" }, { "Maj+F12", "references" }, { "Ctrl+-", "revenir" },
    { "Ctrl+R, Ctrl+R", "renommer" }, { "F2", "renommer" },
    { "F8", "diagSuivant" }, { "Maj+F8", "diagPrecedent" },
    { "Ctrl+.", "corriger" }, { "Alt+Entr\xC3\xA9" "e", "corriger" },
    { "Ctrl+K, Ctrl+K", "signet" }, { "Ctrl+K, Ctrl+N", "signetSuivant" }, { "Ctrl+K, Ctrl+P", "signetPrecedent" },
    { "Ctrl+T", "allerA" }, { "Ctrl+,", "allerA" },
    { "Ctrl+Espace", "completer" }, { "Ctrl+Maj+Espace", "infoParametres" }, { "Ctrl+K, Ctrl+I", "infoRapide" },
    { "Ctrl+M, Ctrl+M", "plier" }, { "Ctrl+M, Ctrl+L", "plierTout" }, { "Ctrl+M, Ctrl+O", "plierNiveau" },
    { "Ctrl+F7", "compiler" }, { "Ctrl+Maj+B", "generer" }, { "F5", "simuler" }, { "Maj+F5", "arreter" },
    { "Ctrl+F4", "fermer" },
};
// Ce que le profil classique laisse a l'appli (le sens d'avant, hors accords).
constexpr std::string_view kClassicKeeps[] = {
    "F12", "Maj+F12", "F8", "Maj+F8", "F5", "Maj+F5", "Ctrl+W", "Ctrl+H", "Ctrl+Maj+F", "F2",
};

std::vector<Binding> build(Profile p) {
    std::vector<Binding> out;
    for (const auto& [text, cmd] : kVs) {
        const auto chord = parse(text);
        if (!chord) continue;
        if (p == Profile::Classic) {
            if (chord->twoStrokes()) continue;
            if (std::find(std::begin(kClassicKeeps), std::end(kClassicKeeps), text) != std::end(kClassicKeeps)) continue;
        }
        out.push_back(Binding{ *chord, cmd });
    }
    return out;
}

}   // namespace

Stroke strokeOf(const KeyDown& k) noexcept {
    Stroke s;
    s.key = k.key;
    s.ctrl = k.mods.ctrl;
    s.shift = k.mods.shift;
    s.alt = k.mods.alt;
    return s;
}

std::string keyName(Key k) {
    for (const auto& n : kNames)
        if (n.key == k) return std::string(n.shown);
    return {};
}

std::string label(const Stroke& s) {
    std::string out;
    if (s.ctrl) out += "Ctrl+";
    if (s.shift) out += "Maj+";
    if (s.alt) out += "Alt+";
    out += keyName(s.key);
    return out;
}

std::string label(const Chord& c) {
    auto out = label(c.first);
    if (c.twoStrokes()) out += ", " + label(c.second);
    return out;
}

std::optional<Stroke> parseStroke(std::string_view text) {
    Stroke s;
    std::string rest = fold(text);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    while (!rest.empty() && rest.back() == ' ') rest.pop_back();
    if (rest.empty()) return std::nullopt;
    for (;;) {
        const auto plus = rest.find('+');
        if (plus == std::string::npos || plus + 1 >= rest.size()) break;
        const auto mod = rest.substr(0, plus);
        if (mod == "ctrl" || mod == "control") s.ctrl = true;
        else if (mod == "maj" || mod == "shift") s.shift = true;
        else if (mod == "alt") s.alt = true;
        else return std::nullopt;
        rest = rest.substr(plus + 1);
    }
    for (const auto& a : kAliases)
        if (rest == a.text) { s.key = a.key; return s; }
    for (const auto& n : kNames)
        if (rest == fold(n.shown)) { s.key = n.key; return s; }
    return std::nullopt;
}

std::optional<Chord> parse(std::string_view text) {
    // Deux appuis : separes par ", " ou par des espaces ("Ctrl+K, Ctrl+C", "Ctrl+K Ctrl+C").
    // "Ctrl+," se lit en entier : la virgule y est la touche.
    std::string t(text);
    std::vector<std::string> parts;
    std::string cur;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        const bool sepComma = c == ',' && i + 1 < t.size() && t[i + 1] == ' ' && !cur.empty() && cur.back() != '+';
        if (sepComma || c == ' ') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty() || parts.size() > 2) return std::nullopt;
    Chord chord;
    const auto a = parseStroke(parts[0]);
    if (!a) return std::nullopt;
    chord.first = *a;
    if (parts.size() == 2) {
        const auto b = parseStroke(parts[1]);
        if (!b) return std::nullopt;
        chord.second = *b;
    }
    return chord;
}

std::string_view profileKey(Profile p) noexcept { return p == Profile::Classic ? "classique" : "vs"; }

std::string_view profileLabel(Profile p) noexcept {
    return p == Profile::Classic ? std::string_view("Classique (1.12.1, sans accords)") : std::string_view("Visual Studio");
}

std::optional<Profile> profileFromKey(std::string_view key) noexcept {
    const auto k = fold(key);
    if (k == "vs" || k == "visual studio" || k == "visualstudio") return Profile::VisualStudio;
    if (k == "classique" || k == "classic") return Profile::Classic;
    return std::nullopt;
}

Profile current() noexcept { return gProfile.load(); }
void setCurrent(Profile p) noexcept { gProfile.store(p); }

const std::vector<Command>& commands() { return catalog(); }

const Command* command(std::string_view id) noexcept {
    for (const auto& c : catalog())
        if (c.id == id) return &c;
    return nullptr;
}

const std::vector<Binding>& bindings(Profile p) {
    static const std::vector<Binding> vs = build(Profile::VisualStudio);
    static const std::vector<Binding> classic = build(Profile::Classic);
    return p == Profile::Classic ? classic : vs;
}

std::vector<Chord> chordsOf(Profile p, std::string_view cmd) {
    std::vector<Chord> out;
    for (const auto& b : bindings(p))
        if (b.command == cmd) out.push_back(b.chord);
    return out;
}

std::string_view single(Profile p, const Stroke& s) {
    for (const auto& b : bindings(p))
        if (!b.chord.twoStrokes() && b.chord.first == s) return b.command;
    return {};
}

bool startsChord(Profile p, const Stroke& s) {
    for (const auto& b : bindings(p))
        if (b.chord.twoStrokes() && b.chord.first == s) return true;
    return false;
}

Resolver::Result Resolver::feed(Profile p, const KeyDown& k) {
    Result r;
    if (k.key == Key::Unknown) return r;              // Ctrl, Maj, Alt seuls : l'attente continue
    const Stroke s = strokeOf(k);
    if (first_) {
        // La touche tenue se repete : elle n'est pas la seconde de l'accord.
        if (k.repeat && s == *first_) { r.outcome = Outcome::Pending; r.chord = label(s); return r; }
        Chord c{ *first_, s };
        first_.reset();
        r.chord = label(c);
        for (const auto& b : bindings(p))
            if (b.chord == c) { r.outcome = Outcome::Command; r.command = b.command; return r; }
        r.outcome = Outcome::Unknown;
        return r;
    }
    if (!k.repeat && startsChord(p, s)) {
        first_ = s;
        r.outcome = Outcome::Pending;
        r.chord = label(s);
        return r;
    }
    const auto cmd = single(p, s);
    if (!cmd.empty()) {
        r.outcome = Outcome::Command;
        r.command = cmd;
        r.chord = label(s);
    }
    return r;
}

std::string pendingMessage(std::string_view firstStroke) {
    return "(" + std::string(firstStroke) + ") a \xC3\xA9t\xC3\xA9 appuy\xC3\xA9. Attente de la seconde touche de l'accord\xE2\x80\xA6";
}

std::string unknownMessage(std::string_view chord) {
    return "La combinaison de touches (" + std::string(chord) + ") n'est pas une commande.";
}

}   // namespace ui::keymap
