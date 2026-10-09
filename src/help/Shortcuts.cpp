// =============================================================================
//  help/Shortcuts.cpp - 1.11 (chantier T2) : la table des raccourcis
// -----------------------------------------------------------------------------
//  La table vient de la page des raccourcis de la maquette 1.11, validee par le
//  client le 02/10 (60 lignes : General 22, Editeur IHM 9, Scripts 5,
//  Simulation 11, Grafcet 6, Aide 7), plus Ctrl+Maj+O (l'import du .XHW,
//  1.11) : 61 lignes, General 23. Elle a ete prise dans App.cpp
//  (actions_.add), la table de HelpDocument.cpp, les sujets "raccourcis" et
//  "onglets" du guide et le LISEZ-MOI 1.10.0.
//
//  Une touche de plus : une ligne ici, dans son contexte. Une touche d'une
//  action du registre : son identifiant dans la derniere colonne, et App.cpp
//  la lit par bindingOf().
// =============================================================================
#include "Shortcuts.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : les raccourcis de chaque application

#include <algorithm>
#include <cctype>

namespace help::keys {

namespace {

const std::vector<Shortcut> kTable = {
    // ---- General
    {Context::General, "F1",
     "L'aide de l'endroit : le volet, l'objet, le nom sous le curseur",
     "", "help.open"},
    {Context::General, "Ctrl+K",
     "Aller \xC3\xA0\xE2\x80\xA6 : une variable, une vue, une alarme, un volet, une action",
     "", ""},
    {Context::General, "Ctrl+S",
     "Enregistrer le projet",
     "", "project.save,project.saveKeyboard"},
    {Context::General, "Ctrl+O",
     "Ouvrir un projet",
     "", "file.open"},
    // 1.11 : l'import du .XHW avait Ctrl+H, deja a l'Historique (1.10.1) ; il passe ici.
    {Context::General, "Ctrl+Maj+O",
     "Importer la configuration mat\xC3\xA9rielle (.XHW) dans le projet ouvert",
     "1.11", "file.importHardware"},
    {Context::General, "Ctrl+N",
     "Nouveau projet",
     "", "project.new"},
    {Context::General, "Ctrl+Z",
     "Annuler (dans un champ en cours de saisie : sa saisie d'abord)",
     "", "edit.undo"},
    {Context::General, "Ctrl+Y / Ctrl+Maj+Z",
     "R\xC3\xA9tablir",
     "", "edit.redo"},
    {Context::General, "Ctrl+H",
     "L'historique",
     "", "edit.history"},
    {Context::General, "Ctrl+J",
     "Le panneau du bas : Sorties, Console, Diagnostics (le montrer, le replier)",
     "1.11.14", ""},
    {Context::General, "Ctrl+W",
     "Fermer l'onglet ouvert",
     "", ""},
    {Context::General, "Ctrl+Tab / Ctrl+Maj+Tab",
     "Le sous-onglet suivant, pr\xC3\xA9" "c\xC3\xA9" "dent du volet",
     "", ""},
    {Context::General, "Ctrl+PgSuiv / Ctrl+PgPr\xC3\xA9" "c",
     "L'onglet suivant, pr\xC3\xA9" "c\xC3\xA9" "dent",
     "", ""},
    {Context::General, "Ctrl+F",
     "Le champ de recherche du volet (retenu d'une s\xC3\xA9" "ance \xC3\xA0 l'autre)",
     "", ""},
    {Context::General, "Ctrl+Maj+F",
     "Filtrer l'arbre du projet (cherche aussi dans le contenu)",
     "", ""},
    {Context::General, "F2",
     "Renommer ce qui est choisi",
     "", ""},
    {Context::General, "F11",
     "Plein \xC3\xA9" "cran, et retour",
     "", ""},
    {Context::General, "F12",
     "Une capture de la fen\xC3\xAAtre (dossier captures)",
     "", ""},
    {Context::General, "F7",
     "Compiler le projet IHM : le rapport complet (IHM \xE2\x80\xBA Compiler) ; dans un \xC3\xA9" "diteur, le document affich\xC3\xA9",
     "1.11.17", ""},
    {Context::General, "Suppr",
     "Supprimer ce qui est choisi",
     "", ""},
    {Context::General, "Ctrl+Alt+S",
     "Une version du projet",
     "", ""},
    {Context::General, "Ctrl+Maj+E",
     "Exporter le programme lisible (Excel, PDF, texte)",
     "", "program.export"},
    {Context::General, "Ctrl+1 / Ctrl+5",
     "API \xE2\x80\xBA Variables, API \xE2\x80\xBA Statistiques",
     "", "view.variables,view.statistics"},
    {Context::General, "Ctrl+Maj+H",
     "Revenir au menu principal",
     "", "app.home"},
    {Context::General, "Alt+\xE2\x86\x90",
     "Retour",
     "", "nav.back"},
    // ---- HmiEditor
    {Context::HmiEditor, "Ctrl+C / Ctrl+V",
     "Copier, coller (animations et actions comprises) ; un tableau : vers Excel, depuis Excel",
     "", ""},
    {Context::HmiEditor, "Ctrl+Maj+V",
     "Dans un tableau : coller en nouvelles lignes",
     "", ""},
    {Context::HmiEditor, "Fl\xC3\xA8" "ches / Maj+Fl\xC3\xA8" "ches",
     "D\xC3\xA9placer d'un pixel, d'un pas de grille",
     "", ""},
    {Context::HmiEditor, "Alt+glisser",
     "Poser une copie",
     "", ""},
    {Context::HmiEditor, "\xC3\x89" "chap",
     "Sortir d'un groupe, fermer une liste, annuler une saisie",
     "", ""},
    {Context::HmiEditor, "Ctrl+Suppr",
     "Retirer l'expression d'une case : la valeur fixe revient",
     "1.10", ""},
    {Context::HmiEditor, "Ctrl+Espace",
     "L'aide \xC3\xA0 la saisie, selon le type de la case",
     "1.10", ""},
    {Context::HmiEditor, "Entr\xC3\xA9" "e / Tab",
     "Choisir une proposition",
     "", ""},
    {Context::HmiEditor, "I",
     "La pipette de la palette des couleurs",
     "1.10", ""},
    // ---- Scripts
    {Context::Scripts, "F7",
     "Compiler le document affich\xC3\xA9 : ce script, cette fonction ou ces op\xC3\xA9rateurs, chaque erreur avec sa ligne et sa colonne",
     "1.11.17", ""},
    {Context::Scripts, "Ctrl+Espace",
     "L'aide \xC3\xA0 la saisie : fonctions du script, membres, MAP, mod\xC3\xA8les",
     "1.10", ""},
    {Context::Scripts, "Tab / Entr\xC3\xA9" "e",
     "Ins\xC3\xA9rer la proposition choisie",
     "", ""},
    {Context::Scripts, "\xE2\x86\x91 / \xE2\x86\x93",
     "Choisir dans l'aide \xC3\xA0 la saisie",
     "", ""},
    {Context::Scripts, "Ctrl+Z",
     "Annuler (une saisie continue est une seule annulation)",
     "", ""},
    // ---- Simulation
    {Context::Simulation, "F5",
     "Simuler l'API ; en pause : Continuer",
     "", "sim.run"},
    {Context::Simulation, "Maj+F5",
     "Arr\xC3\xAAter l'API",
     "", "sim.stop"},
    {Context::Simulation, "F8",
     "D\xC3\xA9marrer l'IHM simul\xC3\xA9" "e (l'API ne d\xC3\xA9marre pas avec elle)",
     "1.10", ""},
    {Context::Simulation, "Maj+F8",
     "Arr\xC3\xAAter l'IHM simul\xC3\xA9" "e (l'API continue)",
     "1.10", ""},
    {Context::Simulation, "F9",
     "Simulation \xE2\x80\xBA Vue d'ensemble ; dans le code d'une section : un point d'arr\xC3\xAAt",
     "", "sim.open"},
    {Context::Simulation, "Ctrl+F9",
     "Dans le code : activer ou d\xC3\xA9sactiver le point d'arr\xC3\xAAt",
     "", ""},
    {Context::Simulation, "F10",
     "Section suivante (en pause) ; Ctrl+F10 : jusqu'\xC3\xA0 la ligne du curseur",
     "", ""},
    {Context::Simulation, "F11",
     "Simulation \xC2\xB7 IHM : la vue seule en plein \xC3\xA9" "cran ; \xC3\x89" "chap ou F11 pour sortir",
     "1.10", ""},
    {Context::Simulation, "Ctrl+molette",
     "Le zoom de la vue, autour de la souris",
     "1.10", ""},
    {Context::Simulation, "Ctrl+Alt+Q",
     "Poste d'exploitation : demander la sortie",
     "", ""},
    {Context::Simulation, "Ctrl+Alt+S",
     "La page Simulation (Param\xC3\xA8tres syst\xC3\xA8me), en Simulation \xC2\xB7 IHM comme sur le poste d'exploitation (Administrer)",
     "", ""},
    // ---- Grafcet
    {Context::Grafcet, "Ctrl+Entr\xC3\xA9" "e",
     "Volet Code : \xC3\xA9" "crire dans le programme",
     "1.10", ""},
    {Context::Grafcet, "Ctrl+F",
     "Chercher une \xC3\xA9tape, une transition, une variable",
     "1.10", ""},
    {Context::Grafcet, "Suppr",
     "Supprimer l'\xC3\xA9l\xC3\xA9ment choisi",
     "", ""},
    {Context::Grafcet, "Ctrl+molette",
     "Zoomer autour de la souris",
     "", ""},
    {Context::Grafcet, "\xC3\x89" "chap",
     "Quitter une commande \xC3\xA0 deux clics (Transition, Relier)",
     "", ""},
    {Context::Grafcet, "Ctrl+Z / Ctrl+Y",
     "Annuler, r\xC3\xA9tablir : des deux c\xC3\xB4t\xC3\xA9s, le dessin et les sections",
     "", ""},
    // ---- Help
    {Context::Help, "F1",
     "Le centre d'aide, sur le sujet de l'endroit",
     "", ""},
    {Context::Help, "Ctrl+F",
     "Chercher dans toute l'aide",
     "1.11", ""},
    {Context::Help, "Alt+\xE2\x86\x90 / Alt+\xE2\x86\x92",
     "Sujet pr\xC3\xA9" "c\xC3\xA9" "dent, suivant",
     "1.11", ""},
    {Context::Help, "Espace",
     "Tutoriel : lecture, pause",
     "1.11", ""},
    {Context::Help, "\xE2\x86\x90 / \xE2\x86\x92",
     "Tutoriel : \xC3\xA9tape pr\xC3\xA9" "c\xC3\xA9" "dente, suivante",
     "1.11", ""},
    {Context::Help, "A",
     "Tutoriel : \xC3\x80 toi (tu fais l'\xC3\xA9tape, il v\xC3\xA9rifie)",
     "1.11", ""},
    {Context::Help, "\xC3\x89" "chap",
     "Tutoriel : sortir de \xC2\xAB \xC3\x80 toi \xC2\xBB",
     "1.11", ""},
};

// Une suite UTF-8 de deux octets C3 xx (Latin-1) -> sa lettre de base ; 0 : aucune.
char baseLetter(unsigned char b) {
    const unsigned char lo = static_cast<unsigned char>(b & 0x1F);   // A0..BF et 80..9F se replient pareil
    if (lo <= 0x05) return 'a';
    if (lo == 0x07) return 'c';
    if (lo >= 0x08 && lo <= 0x0B) return 'e';
    if (lo >= 0x0C && lo <= 0x0F) return 'i';
    if (lo == 0x11) return 'n';
    if (lo >= 0x12 && lo <= 0x16) return 'o';
    if (lo >= 0x19 && lo <= 0x1C) return 'u';
    if (lo == 0x1D || (lo == 0x1F && b >= 0xA0)) return 'y';   // pas le sz allemand (C3 9F)
    return 0;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') ++a;
    while (b > a && s[b - 1] == ' ') --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> split(std::string_view s, std::string_view sep) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (true) {
        const auto p = s.find(sep, at);
        out.push_back(trim(s.substr(at, p == std::string_view::npos ? std::string_view::npos : p - at)));
        if (p == std::string_view::npos) break;
        at = p + sep.size();
    }
    out.erase(std::remove(out.begin(), out.end(), std::string{}), out.end());
    return out;
}

// La largeur a l'ecran d'un texte UTF-8 : les octets qui ne continuent pas une lettre.
std::size_t width(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

} // namespace

std::string fold(std::string_view text, std::vector<std::size_t>* origin) {
    std::string out;
    out.reserve(text.size());
    if (origin) { origin->clear(); origin->reserve(text.size() + 1); }
    const auto put = [&](char ch, std::size_t at) { out.push_back(ch); if (origin) origin->push_back(at); };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { put(static_cast<char>(std::tolower(c)), i); continue; }
        if (c == 0xC3 && i + 1 < text.size()) {
            if (const char b = baseLetter(static_cast<unsigned char>(text[i + 1]))) { put(b, i); ++i; continue; }
        }
        if (c == 0xC5 && i + 1 < text.size()
            && (static_cast<unsigned char>(text[i + 1]) == 0x92 || static_cast<unsigned char>(text[i + 1]) == 0x93)) {
            put('o', i); put('e', i); ++i; continue;
        }
        put(static_cast<char>(c), i);
    }
    if (origin) origin->push_back(text.size());
    return out;
}

const std::vector<Shortcut>& all() { return kTable; }

const std::vector<Context>& contexts() {
    static const std::vector<Context> k = {Context::General, Context::HmiEditor, Context::Scripts,
                                           Context::Simulation, Context::Grafcet, Context::Help};
    // 1.12.0 : chaque application ses raccourcis - XPGAnalyser API sans l'editeur ni les
    // scripts de l'IHM, XPGAnalyser IHM sans le Grafcet de l'automate.
    static const std::vector<Context> api = {Context::General, Context::Simulation, Context::Grafcet, Context::Help};
    static const std::vector<Context> ihm = {Context::General, Context::HmiEditor, Context::Scripts, Context::Simulation, Context::Help};
    switch (core::edition()) {
        case core::Edition::Api: return api;
        case core::Edition::Ihm: return ihm;
        case core::Edition::Both: break;
    }
    return k;
}

std::string_view contextLabel(Context c) {
    switch (c) {
        case Context::General:    return "G\xC3\xA9n\xC3\xA9ral";
        case Context::HmiEditor:  return "\xC3\x89" "diteur IHM";
        case Context::Scripts:    return "Scripts";
        case Context::Simulation: return "Simulation";
        case Context::Grafcet:    return "Grafcet";
        case Context::Help:       return "Aide";
    }
    return {};
}

std::vector<const Shortcut*> ofContext(Context c) {
    std::vector<const Shortcut*> out;
    for (const auto& s : kTable)
        if (s.context == c) out.push_back(&s);
    return out;
}

std::vector<std::string> actionsOf(const Shortcut& s) { return split(s.actions, ","); }

std::vector<std::string> alternatives(std::string_view keys) { return split(keys, " / "); }

std::vector<std::string> keyCaps(std::string_view oneAlternative) { return split(oneAlternative, "+"); }

std::string toBinding(std::string_view keys) {
    std::string out;
    for (const auto& cap : keyCaps(keys)) {
        std::string k = cap;
        if (k == "Maj") k = "Shift";
        else if (k == "\xE2\x86\x90") k = "Left";
        else if (k == "\xE2\x86\x92") k = "Right";
        else if (k == "\xE2\x86\x91") k = "Up";
        else if (k == "\xE2\x86\x93") k = "Down";
        else if (k == "Suppr") k = "Delete";
        else if (k == "\xC3\x89" "chap") k = "Escape";
        else if (k == "Entr\xC3\xA9" "e") k = "Return";
        else if (k == "Espace") k = "Space";
        else if (k == "PgSuiv") k = "PageDown";
        else if (k == "PgPr\xC3\xA9" "c") k = "PageUp";
        if (!out.empty()) out += '+';
        out += k;
    }
    return out;
}

std::string bindingOf(std::string_view action) {
    if (action.empty()) return {};
    for (const auto& s : kTable) {
        const auto acts = actionsOf(s);
        const auto it = std::find(acts.begin(), acts.end(), action);
        if (it == acts.end()) continue;
        const auto alts = alternatives(s.keys);
        if (alts.empty()) return {};
        const auto i = static_cast<std::size_t>(it - acts.begin());
        // "Ctrl+1 / Ctrl+5" pour deux actions : la i-eme touche ; sinon la premiere
        // ("Ctrl+Y / Ctrl+Maj+Z" pour edit.redo : Ctrl+Y, celle du registre).
        return toBinding(alts.size() == acts.size() ? alts[i] : alts.front());
    }
    return {};
}

std::vector<const Shortcut*> search(std::string_view term) {
    const auto t = fold(trim(term));
    std::vector<const Shortcut*> onKey, onText;
    const auto& shown = contexts();   // 1.12.0 : ceux de l'application
    for (const auto& s : kTable) {
        if (std::find(shown.begin(), shown.end(), s.context) == shown.end()) continue;
        if (t.empty()) { onKey.push_back(&s); continue; }
        if (fold(s.keys).find(t) != std::string::npos) { onKey.push_back(&s); continue; }
        std::string hay(s.text);
        hay += ' ';
        hay += contextLabel(s.context);
        if (fold(hay).find(t) != std::string::npos) onText.push_back(&s);
    }
    // La touche d'abord : "F8" montre F8 et Maj+F8 avant une phrase qui cite F8.
    onKey.insert(onKey.end(), onText.begin(), onText.end());
    return onKey;
}

std::string sheetText(std::string_view version) {
    std::string out = "XPGAnalyser ";
    out += version;
    out += " \xE2\x80\x94 les raccourcis clavier\n";
    constexpr std::size_t kKeyColumn = 26;
    for (const auto c : contexts()) {
        out += '\n';
        out += contextLabel(c);
        out += '\n';
        for (const auto* s : ofContext(c)) {
            out += "  ";
            out += s->keys;
            const auto w = width(s->keys);
            out.append(w < kKeyColumn ? kKeyColumn - w : 1, ' ');
            out += s->text;
            if (!s->since.empty()) { out += "  ["; out += s->since; out += ']'; }
            out += '\n';
        }
    }
    return out;
}

} // namespace help::keys
