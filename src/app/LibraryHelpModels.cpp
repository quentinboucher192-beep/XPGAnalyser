#include "LibraryHelpModels.hpp"

#include "../help/HelpIndex.hpp"   // 1.11.2 (T2) : help::seeNames, un nom par puce « Voir aussi »
#include "../project/MacroFolders.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace app {
namespace {

using project::CatalogEntry;
using project::CatalogKind;
using project::LibraryHelp;

std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.remove_suffix(1);
    return std::string(s);
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Recompose une prose pre-coupee.
//
// LE FICHIER GARDE SES LIGNES, L'ARTICLE LES RECOUPE. Une aide ecrite dans un
// editeur de texte est coupee vers 75 colonnes, parce que c'est ce qui se
// relit dans un .ddt. Affichee telle quelle dans un panneau de 900 pixels,
// elle donne une colonne etroite et en escalier au milieu d'une page vide, et
// elle ne suit pas la fenetre quand on la redimensionne.
//
// La regle : une ligne qui ne finit PAS par une ponctuation forte est la
// suite de la suivante, et les deux se joignent. Une ligne qui finit par un
// point, un deux-points ou un point-virgule termine ce qu'elle dit et garde
// son retour.
//
// Elle marche dans les deux sens : sur une prose pre-coupee, elle reforme les
// phrases ; sur une prose deja ecrite une phrase par ligne - ce que produit
// l'editeur de cet ecran - elle ne change rien, parce que chaque ligne finit
// deja par un point.
} // namespace

// Une ligne alignee : elle contient une suite d'au moins deux espaces, donc
// quelqu'un s'est donne du mal pour poser des colonnes. La table des bits d'un
// UUID en est une. Ni reflow ni softWrap n'y touchent : recomposer une colonne
// la detruit, et le lecteur perd la seule chose que cette ligne apportait.
bool estAlignee(std::string_view l) { return l.find("  ") != std::string_view::npos; }

std::string reflow(std::string_view texte) {
    std::string out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= texte.size(); ++i) {
        if (i != texte.size() && texte[i] != '\n') continue;
        auto ligne = trimmed(texte.substr(start, i - start));
        start = i + 1;
        if (ligne.empty()) { if (!out.empty()) out += '\n'; continue; }

        if (estAlignee(ligne)) {
            if (!out.empty() && out.back() != '\n') out += '\n';
            out += ligne;
            out += '\n';
            continue;
        }

        if (!out.empty() && out.back() != '\n') out += ' ';
        out += ligne;
        const char c = ligne.back();
        if (c == '.' || c == ':' || c == ';' || c == '!' || c == '?') out += '\n';
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::string softWrap(std::string_view texte, std::size_t colonnes) {
    std::string out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= texte.size(); ++i) {
        if (i != texte.size() && texte[i] != '\n') continue;
        auto ligne = std::string_view(texte).substr(start, i - start);
        start = i + 1;

        if (estAlignee(ligne)) {
            out += std::string(ligne);
            if (i != texte.size()) out += '\n';
            continue;
        }

        // Les coupures voulues sont conservees : on ne recoupe que ce qui
        // depasse.
        while (ligne.size() > colonnes) {
            auto coupe = ligne.rfind(' ', colonnes);

            // ON NE COUPE PAS JUSTE APRES UN POINT si on peut faire autrement.
            // reflow() traite une ligne finissant par une ponctuation forte
            // comme une ligne terminee : une coupure posee la deviendrait un
            // retour a la ligne permanent, et le paragraphe se fendrait en
            // deux a chaque enregistrement. Trouve par un aller-retour sur les
            // 442 champs de la bibliotheque, pas a la lecture.
            while (coupe != std::string_view::npos && coupe > 0) {
                const char c = ligne[coupe - 1];
                if (c != '.' && c != ':' && c != ';' && c != '!' && c != '?') break;
                const auto avant = ligne.rfind(' ', coupe - 1);
                if (avant == std::string_view::npos || avant == 0) break;
                coupe = avant;
            }

            if (coupe == std::string_view::npos || coupe == 0) {
                // Un mot plus long que la colonne entiere : on le laisse
                // deborder plutot que de le couper en deux moities qu'on ne
                // peut plus recopier.
                coupe = ligne.find(' ', colonnes);
                if (coupe == std::string_view::npos) break;
            }
            out += std::string(ligne.substr(0, coupe));
            out += '\n';
            ligne.remove_prefix(coupe + 1);
        }
        out += std::string(ligne);
        if (i != texte.size()) out += '\n';
    }
    return out;
}

namespace {

// "BOOL - Member" ou "INT - Input - 16". Ce qui tient sur la ligne du nom.
std::string signatureOf(const project::Declaration& d) {
    std::string s = d.type;
    if (!d.scope.empty())   s += "  " + d.scope;
    if (!d.initial.empty()) s += "  = " + d.initial;
    return s;
}

} // namespace

// ---------------------------------------------------------------- champs ----
std::vector<HelpField> helpFields(const CatalogEntry& e) {
    std::vector<HelpField> out;
    out.push_back({HelpField::Kind::Summary, {}, "R\xC3\xA9sum\xC3\xA9",
                   "une phrase : ce que c'est", false});
    out.push_back({HelpField::Kind::Usage, {}, "Utilisation",
                   "comment s'en servir, et ce qu'il ne faut pas faire", true});
    out.push_back({HelpField::Kind::Example, {}, "Exemple",
                   "un appel ST, recopi\xC3\xA9 tel quel", true});
    out.push_back({HelpField::Kind::See, {}, "Voir aussi",
                   "les \xC3\xA9l\xC3\xA9ments voisins, s\xC3\xA9par\xC3\xA9s par des virgules", false});
    out.push_back({HelpField::Kind::Since, {}, "Depuis",
                   "la version o\xC3\xB9 c'est apparu", false});
    out.push_back({HelpField::Kind::Author, {}, "Auteur", {}, false});

    for (const auto& d : e.declarations) {
        if (d.isLocal() || d.name.empty()) continue;
        out.push_back({HelpField::Kind::Param, d.name, d.name, signatureOf(d), true});
    }

    // Les codes annonces par la table en commentaire, puis ceux qui n'ont
    // qu'une aide. Les deux existent : la table est la source, mais une aide
    // ecrite pour un code absent de la table doit rester accessible, sinon on
    // ne peut plus la corriger.
    for (const auto& f : e.faultTable)
        out.push_back({HelpField::Kind::Fault, f.code, "d\xC3\xA9" "faut " + f.code, f.text, true});
    for (const auto& h : e.help.faults) {
        const bool deja = std::any_of(e.faultTable.begin(), e.faultTable.end(),
                                      [&](const project::FaultCode& f) { return f.code == h.key; });
        if (!deja)
            out.push_back({HelpField::Kind::Fault, h.key, "d\xC3\xA9" "faut " + h.key,
                           "absent de la table du fichier", true});
    }

    // Lot macros 1 : les lignes du formulaire d'une macro (et la version) sont
    // connues : elles se nomment. Les autres restent " inconnues, conservees ".
    struct Known { const char* key; const char* label; const char* hint; };
    static const Known kKnown[] = {
        {"version",        "Version",                      "la version de la macro (index.txt la reprend)"},
        {"changes",        "Nouveaut\xC3\xA9s",             "ce qui change pour qui passe \xC3\xA0 cette version"},
        {"categorie",      "Dossier",                      "le dossier de la macro dans l'onglet Macros"},
        {"champ",          "Genre du champ",               "fichier .xlsx, tache, section, oui-non, choix, nombre 1..4, nom..."},
        {"libelle",        "Libell\xC3\xA9 du champ",        "ce que le formulaire \xC3\xA9" "crit devant le champ (accents permis)"},
        {"option",         "Libell\xC3\xA9 d'un choix",       "ce que le formulaire montre pour cette valeur"},
        {"groupe",         "Groupe",                       "les champs rang\xC3\xA9s sous ce titre"},
        {"avance",         "R\xC3\xA9glages avanc\xC3\xA9s",    "les champs repli\xC3\xA9s sous \xC2\xAB R\xC3\xA9glages avanc\xC3\xA9s \xC2\xBB"},
        {"exemple",        "Exemple du nom",               "ce que le nom donne ({} : la r\xC3\xA9ponse)"},
        {"tableau",        "Tableau CSV",                  "le tableau que la macro lit (OpenTable)"},
        {"lit",            "Ce qu'elle lit",               "les onglets du classeur qu'il lui faut"},
        {"lit-facultatif", "Ce qu'elle lit s'il existe",   "les onglets facultatifs"},
        {"produit",        "Ce qu'elle produit",           "une ligne par chose produite"},
        {"appliquer",      "Texte du bouton Appliquer",    "{cl\xC3\xA9} : la r\xC3\xA9ponse"},
    };
    for (const auto& u : e.help.unknown) {
        const auto space = u.key.find(' ');
        const std::string first = lower(space == std::string::npos ? u.key : u.key.substr(0, space));
        const std::string rest = space == std::string::npos ? std::string{} : u.key.substr(space + 1);
        const Known* known = nullptr;
        for (const auto& k : kKnown)
            if (first == k.key) known = &k;
        if (known == nullptr) {
            out.push_back({HelpField::Kind::Unknown, u.key, u.key,
                           "cl\xC3\xA9 inconnue de cette version, conserv\xC3\xA9" "e telle quelle", true});
            continue;
        }
        out.push_back({HelpField::Kind::Unknown, u.key,
                       std::string(known->label) + (rest.empty() ? std::string{} : " : " + rest), known->hint, true});
    }
    return out;
}

std::string readField(const LibraryHelp& h, const HelpField& f) {
    switch (f.kind) {
        case HelpField::Kind::Summary: return h.summary;
        case HelpField::Kind::Usage:   return h.usage;
        case HelpField::Kind::Example: return h.example;
        case HelpField::Kind::Since:   return h.since;
        case HelpField::Kind::Author:  return h.author;
        case HelpField::Kind::See: {
            std::string s;
            for (const auto& v : h.see) { if (!s.empty()) s += ", "; s += v; }
            return s;
        }
        case HelpField::Kind::Param: { const auto* t = h.param(f.key); return t ? *t : std::string{}; }
        case HelpField::Kind::Fault: { const auto* t = h.fault(f.key); return t ? *t : std::string{}; }
        case HelpField::Kind::Unknown:
            for (const auto& u : h.unknown) if (u.key == f.key) return u.text;
            return {};
    }
    return {};
}

void writeField(LibraryHelp& h, const HelpField& f, std::string value) {
    switch (f.kind) {
        case HelpField::Kind::Summary: h.summary = std::move(value); return;
        case HelpField::Kind::Usage:   h.usage   = std::move(value); return;
        case HelpField::Kind::Example: h.example = std::move(value); return;
        case HelpField::Kind::Since:   h.since   = std::move(value); return;
        case HelpField::Kind::Author:  h.author  = std::move(value); return;
        case HelpField::Kind::See: {
            h.see.clear();
            std::size_t start = 0;
            for (std::size_t i = 0; i <= value.size(); ++i) {
                if (i != value.size() && value[i] != ',') continue;
                auto part = trimmed(std::string_view(value).substr(start, i - start));
                if (!part.empty()) h.see.push_back(std::move(part));
                start = i + 1;
            }
            return;
        }
        case HelpField::Kind::Param:   h.setParam(f.key, std::move(value)); return;
        case HelpField::Kind::Fault:   h.setFault(f.key, std::move(value)); return;
        case HelpField::Kind::Unknown:
            for (auto& u : h.unknown)
                if (u.key == f.key) { u.text = std::move(value); return; }
            h.unknown.push_back({f.key, std::move(value)});
            return;
    }
}

// ------------------------------------------------------------- familles ----
int familyIndex(std::string_view category) noexcept {
    // PAR LE NOM, PAS PAR LE RANG. Trier les dossiers et prendre leur indice
    // donnerait une couleur qui change le jour ou quelqu'un ajoute un dossier
    // " Drives " entre Control et Equipment : toute la bibliotheque changerait
    // de teinte sans que rien n'ait change pour elle.
    const auto l = lower(category);
    const auto has = [&l](std::string_view w) { return l.find(w) != std::string::npos; };
    if (has("alarm") || has("alarme"))                       return 0;
    if (has("control") || has("controle") || has("mode"))    return 1;
    if (has("equip"))                                        return 2;
    if (l == "io" || has("e/s") || has("entree") || has("input") || l.rfind("io", 0) == 0)
        return 3;
    if (has("macro"))                                        return 4;
    return 5;
}

std::string categoryLabel(std::string_view category) {
    // Les dossiers de libs/ sont en anglais parce que les macros Excel les
    // nomment ainsi ; l'ecran, lui, parle francais.
    const auto l = lower(category);
    if (l == "alarms")    return "Alarmes";
    if (l == "control")   return "Contr\xC3\xB4le";
    if (l == "equipment") return "\xC3\x89quipements";
    if (l == "io")        return "Entr\xC3\xA9" "es/sorties";
    if (l == "macros")    return "Macros";
    if (l == "system")    return "Syst\xC3\xA8me";
    if (l == "comm")      return "Communication";
    if (l == "measure")   return "Mesures";
    if (l == "sequence")  return "S\xC3\xA9quences";
    if (l == "client")    return "Repris du projet";
    if (category.empty()) return "Divers";
    return std::string(category);
}

std::string_view kindName(CatalogKind k) noexcept {
    switch (k) {
        case CatalogKind::DerivedType:   return "Type d\xC3\xA9riv\xC3\xA9";
        case CatalogKind::FunctionBlock: return "Bloc fonction d\xC3\xA9riv\xC3\xA9";
        case CatalogKind::Macro:         return "Macro";
        case CatalogKind::Other:         break;
    }
    return "Fichier";
}

// ---------------------------------------------------------------- prose -----
namespace {

// Deux mots en capitales en tete de ligne : " IL NE COMMANDE RIEN ", " TROIS
// MANIERES DE CHOISIR ". C'est la convention des fichiers de libs/ pour ouvrir
// un paragraphe, et c'est la seule marque de paragraphe qu'ils portent - une
// ligne `#! usage` vide n'existe pas.
bool capsLeadIn(std::string_view t) {
    std::size_t i = 0;
    for (int word = 0; word < 2; ++word) {
        int letters = 0;
        while (i < t.size() && t[i] != ' ') {
            const auto ch = static_cast<unsigned char>(t[i]);
            const bool last = i + 1 == t.size() || t[i + 1] == ' ';
            if (ch >= 'A' && ch <= 'Z') ++letters;
            else if (ch == '\'' || ch == '-' || ch >= 0x80) {}
            else if (last && (ch == ',' || ch == '.' || ch == ':' || ch == ';' || ch == '!'))
                {}
            else return false;          // une minuscule, un chiffre, un souligne
            ++i;
        }
        if (letters < 2) return false;
        while (i < t.size() && t[i] == ' ') ++i;
        if (word == 0 && i >= t.size()) return false;
    }
    return true;
}

// "  " : l'auteur a pose des colonnes.
std::vector<std::string> columnsOf(std::string_view t) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < t.size()) {
        const auto gap = t.find("  ", i);
        const auto cell = t.substr(i, gap == std::string_view::npos ? t.size() - i : gap - i);
        if (!cell.empty()) out.push_back(trimmed(cell));
        if (gap == std::string_view::npos) break;
        i = gap;
        while (i < t.size() && t[i] == ' ') ++i;
    }
    return out;
}

std::string collapseSpaces(std::string_view t) {
    std::string out;
    for (const char c : t) {
        if (c == ' ' && !out.empty() && out.back() == ' ') continue;
        out.push_back(c);
    }
    return trimmed(out);
}

bool endsSentence(std::string_view t) {
    if (t.empty()) return false;
    const char c = t.back();
    return c == '.' || c == '!' || c == '?' || c == ':';
}
bool startsUpper(std::string_view t) {
    if (t.empty()) return false;
    const auto c = static_cast<unsigned char>(t.front());
    return (c >= 'A' && c <= 'Z') || c == 0xC3;   // 0xC3 : une capitale accentuee
}

// Le debut d'un element de liste : "- ", "* ", "1. ", "2) ", ou un numero suivi
// de deux espaces ("0  ordre fixe"). Un chiffre suivi d'UNE espace n'en est pas
// un : " 2 pompes suffisent " est une phrase.
bool itemStart(std::string_view t, std::string& label, std::string& rest) {
    if (t.size() > 2 && (t[0] == '-' || t[0] == '*') && t[1] == ' ') {
        label = "-";
        rest = trimmed(t.substr(2));
        return !rest.empty();
    }
    std::size_t d = 0;
    while (d < t.size() && d < 3 && t[d] >= '0' && t[d] <= '9') ++d;
    if (d == 0 || d + 1 >= t.size()) return false;
    if ((t[d] == ')' || t[d] == '.') && t[d + 1] == ' ') {
        label = std::string(t.substr(0, d));
        rest = trimmed(t.substr(d + 2));
        return !rest.empty();
    }
    if (t[d] == ' ' && t[d + 1] == ' ') {
        label = std::string(t.substr(0, d));
        rest = trimmed(t.substr(d));
        return !rest.empty();
    }
    return false;
}

// "ordre fixe   les premiers disponibles" : un terme court, puis sa definition.
void splitTerm(std::string_view rest, std::string& term, std::string& text) {
    const auto gap = rest.find("  ");
    if (gap != std::string_view::npos && gap > 0 && gap <= 32) {
        term = trimmed(rest.substr(0, gap));
        text = collapseSpaces(rest.substr(gap));
    } else {
        term.clear();
        text = collapseSpaces(rest);
    }
}

} // namespace

std::vector<ProsePart> structureProse(std::string_view raw) {
    // LES FICHIERS DE libs/ ONT UNE STRUCTURE, QU'ILS N'ECRIVENT PAS. Des
    // paragraphes ouverts par deux mots en capitales, des listes numerotees
    // alignees a l'espace, des tableaux de bits en colonnes. reflow() en faisait
    // un seul paragraphe, et les colonnes s'effondraient en police
    // proportionnelle. Ici on les reconnait :
    //
    //   ligne vide, ou deux mots en capitales   -> nouveau paragraphe
    //   "0  terme  definition", "- x", "1. x"   -> element de liste
    //   "Nom[i]   definition" (deux espaces)    -> element de definition
    //   au moins deux lignes alignees de suite,
    //   trois colonnes ou plus chacune          -> tableau
    //
    // La suite d'un element va jusqu'au prochain element. APRES LE DERNIER,
    // une phrase finie suivie d'une majuscule rouvre un paragraphe : c'est la
    // seule facon de distinguer " C'est ce qu'on veut vraiment ", qui commente
    // la liste, de la definition qui la precedait.
    struct Line { std::string text; bool aligned; };
    std::vector<Line> lines;
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i <= raw.size(); ++i) {
            if (i != raw.size() && raw[i] != '\n') continue;
            auto l = raw.substr(start, i - start);
            start = i + 1;
            if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
            const auto t = trimmed(l);
            lines.push_back({t, estAlignee(t)});
        }
    }

    std::vector<ProsePart> out;
    std::string para;                       // lignes brutes, recomposees a la fin
    ProsePart* item = nullptr;
    bool itemSentenceDone = false;
    const auto flushPara = [&] {
        if (!para.empty()) {
            ProsePart p;
            p.kind = ProsePart::Kind::Paragraph;
            p.text = reflow(para);
            if (!p.text.empty()) out.push_back(std::move(p));
            para.clear();
        }
    };
    const auto closeItem = [&] { item = nullptr; itemSentenceDone = false; };

    // Un element est-il suivi, avant la fin du bloc, d'un autre element ?
    const auto moreItemsAhead = [&](std::size_t from) {
        for (std::size_t j = from; j < lines.size(); ++j) {
            const auto& t = lines[j].text;
            if (t.empty() || capsLeadIn(t)) return false;
            std::string lab, rest;
            if (itemStart(t, lab, rest)) return true;
            if (lines[j].aligned && columnsOf(t).size() >= 2) return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& t = lines[i].text;
        if (t.empty()) { flushPara(); closeItem(); continue; }

        // Un tableau : au moins deux lignes alignees consecutives, trois
        // colonnes ou plus chacune.
        if (lines[i].aligned) {
            std::size_t j = i;
            while (j < lines.size() && lines[j].aligned && columnsOf(lines[j].text).size() >= 3) ++j;
            if (j - i >= 2) {
                flushPara(); closeItem();
                ProsePart tab;
                tab.kind = ProsePart::Kind::Table;
                for (std::size_t k = i; k < j; ++k) {
                    const auto cells = columnsOf(lines[k].text);
                    for (std::size_t c = 0; c < cells.size(); ++c) {
                        if (c) tab.text += '\t';
                        tab.text += cells[c];
                    }
                    if (k + 1 < j) tab.text += '\n';
                }
                out.push_back(std::move(tab));
                i = j - 1;
                continue;
            }
        }

        std::string label, rest;
        const bool numbered = itemStart(t, label, rest);
        const auto cols = lines[i].aligned && !numbered ? columnsOf(t) : std::vector<std::string>{};
        const bool definition = cols.size() == 2 && cols[0].size() <= 32;
        if (numbered || definition) {
            flushPara();
            ProsePart p;
            p.kind = ProsePart::Kind::Item;
            if (numbered) {
                p.label = label;
                splitTerm(rest, p.term, p.text);
            } else {
                p.label = "-";
                p.term = cols[0];
                p.text = collapseSpaces(cols[1]);
            }
            out.push_back(std::move(p));
            item = &out.back();
            itemSentenceDone = endsSentence(item->text);
            continue;
        }

        if (capsLeadIn(t)) { flushPara(); closeItem(); para = t; continue; }

        if (item != nullptr) {
            // Apres le dernier element, une phrase finie suivie d'une
            // majuscule n'est plus la suite : c'est un commentaire de la liste.
            if (itemSentenceDone && startsUpper(t) && !moreItemsAhead(i + 1)) {
                closeItem();
                para = t;
                continue;
            }
            item->text += item->text.empty() ? "" : " ";
            item->text += collapseSpaces(t);
            itemSentenceDone = endsSentence(item->text);
            continue;
        }

        if (!para.empty()) para += '\n';
        para += t;
    }
    flushPara();
    return out;
}

// --------------------------------------------------------------- article ----
namespace {

ui::HelpBlock blockOf(ui::HelpBlockKind k, std::string text = {}, std::string label = {}) {
    ui::HelpBlock b;
    b.kind  = k;
    b.text  = std::move(text);
    b.label = std::move(label);
    return b;
}

ui::HelpBlock callout(int severity, std::string title, std::string text) {
    auto b = blockOf(ui::HelpBlockKind::Callout, std::move(text), std::move(title));
    b.severity = severity;
    return b;
}

// Les etiquettes d'un parametre : type, portee, valeur initiale. La portee est
// la seule qui porte une couleur - bleu entree, vert sortie, violet les deux -
// parce que c'est la seule qu'on lit en diagonale en cherchant " ou est la
// sortie ".
std::vector<ui::HelpPill> pillsOf(const project::Declaration& d) {
    std::vector<ui::HelpPill> out;
    if (!d.type.empty()) out.push_back({d.type, ui::kNoColor, ui::Tone::None});
    const auto s = lower(d.scope);
    if (s == "input")       out.push_back({"Entr\xC3\xA9" "e", ui::kNoColor, ui::Tone::Input});
    else if (s == "output") out.push_back({"Sortie", ui::kNoColor, ui::Tone::Output});
    else if (s == "inout")  out.push_back({"Entr\xC3\xA9" "e/sortie", ui::kNoColor, ui::Tone::InOut});
    if (!d.initial.empty()) out.push_back({"= " + d.initial, ui::kNoColor, ui::Tone::None});
    return out;
}

// Le resume court d'une declaration n'est dit que s'il apprend quelque chose
// de plus que l'aide longue : " 0 ordre fixe, 1 carrousel " repete au mot pres
// sous lui-meme est du bruit, et c'est le cas de la moitie des parametres
// d'une enumeration.
bool redundant(std::string_view shortText, std::string_view longText) {
    const auto norm = [](std::string_view t) {
        std::string o;
        for (const char c : t) {
            const auto u = static_cast<unsigned char>(c);
            if (std::isalnum(u)) o.push_back(static_cast<char>(std::tolower(u)));
        }
        return o;
    };
    const auto a = norm(shortText), b = norm(longText);
    return a.empty() || (!b.empty() && b.find(a) != std::string::npos);
}

std::string countLabel(std::size_t done, std::size_t total) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%zu/%zu", done, total);
    return buf;
}

} // namespace

ui::HelpArticle buildHelpArticle(const CatalogEntry& e, const ArticleContext& context) {
    using K = ui::HelpBlockKind;
    ui::HelpArticle a;
    auto push = [&a](ui::HelpBlock b) { a.blocks.push_back(std::move(b)); };
    const ui::Tone famille = ui::familyTone(familyIndex(e.category));

    // ---- l'en-tete ---------------------------------------------------------
    {
        auto h = blockOf(K::Hero, e.name);
        h.tone = famille;
        h.pills.push_back({std::string(project::kindLabel(e.kind)), ui::kNoColor, famille});
        if (!e.version.empty()) h.pills.push_back({"v" + e.version, ui::kNoColor, ui::Tone::None});
        const std::string racine = context.rootLabel.empty() ? std::string("Biblioth\xC3\xA8ques")
                                                             : context.rootLabel;
        h.links.push_back({racine, "cat:", context.rootLabel.empty() ? std::string("Toutes les biblioth\xC3\xA8ques")
                                                                     : "Tout l'onglet " + racine,
                           ui::kNoColor, ui::Tone::None});
        if (context.byFolder) {
            // Lot macros 1 : les dossiers de l'onglet Macros, un maillon par niveau.
            std::string chemin;
            std::size_t from = 0;
            while (!context.folder.empty() && from <= context.folder.size()) {
                const auto slash = context.folder.find('/', from);
                const auto seg = context.folder.substr(from, (slash == std::string::npos ? context.folder.size() : slash) - from);
                chemin += (chemin.empty() ? "" : "/") + seg;
                h.links.push_back({seg, "cat:" + chemin, "Tout le dossier " + seg, ui::kNoColor, ui::Tone::None});
                if (slash == std::string::npos) break;
                from = slash + 1;
            }
        } else if (!e.category.empty()) {
            h.links.push_back({categoryLabel(e.category), "cat:" + e.category,
                               "Tout le dossier " + categoryLabel(e.category),
                               ui::kNoColor, ui::Tone::None});
        }
        h.links.push_back({e.name, {}, {}, ui::kNoColor, ui::Tone::None});
        std::string sous(kindName(e.kind));
        if (!e.fileName.empty()) sous += "  \xC2\xB7  " + e.fileName;
        h.label = std::move(sous);
        push(std::move(h));
    }

    // ---- le resume, et ce qui ne va pas -------------------------------------
    if (!e.help.summary.empty()) push(blockOf(K::Lead, reflow(e.help.summary)));
    else push(callout(1, "Pas encore de r\xC3\xA9sum\xC3\xA9",
                      "L'onglet Modifier \xC3\xA9" "crit directement dans le fichier de la "
                      "biblioth\xC3\xA8que. Une phrase suffit pour commencer : ce que c'est."));

    for (const auto& w : e.warnings) push(callout(1, {}, w));
    const auto orphelins = e.orphanHelpParams();
    if (!orphelins.empty()) {
        std::string s = "Elle documente ";
        for (std::size_t i = 0; i < orphelins.size(); ++i) {
            if (i) s += ", ";
            s += orphelins[i];
        }
        s += ", qui n'existe plus dans le fichier. Elle est conserv\xC3\xA9" "e, mais plus rien "
             "ne l'affiche \xC3\xA0 sa place.";
        push(callout(1, "Aide orpheline", s));
    }

    // ---- lot macros 1 : ce que la macro lit, produit, enchaine ------------------
    const project::macro::MacroSpec* spec =
        e.kind == CatalogKind::Macro ? context.spec : nullptr;
    if (spec != nullptr) {
        if (context.canLaunch) {
            auto l = blockOf(K::Links);
            l.links.push_back({"Lancer " + e.name, "run:" + e.name,
                               "Ouvre son formulaire dans l'onglet Macros : les questions, puis l'aper\xC3\xA7u",
                               ui::kNoColor, ui::Tone::Accent});
            push(std::move(l));
        }
        if (!spec->reads.empty() || !spec->readsOptional.empty() || !spec->tables.empty()) {
            push(blockOf(K::Heading, "Ce qu'elle lit"));
            // Les onglets en puces : dix-sept lignes " l'onglet X " se lisaient
            // comme une liste a cocher, une rangee de puces se parcourt d'un coup.
            if (!spec->reads.empty()) {
                auto l = blockOf(K::Links);
                l.label = "Les onglets du classeur qu'il lui faut";
                for (const auto& r : spec->reads)
                    l.links.push_back({r, {}, "Onglet requis : sans lui, la macro s'arr\xC3\xAAte en le disant",
                                       ui::kNoColor, famille});
                push(std::move(l));
            }
            if (!spec->readsOptional.empty()) {
                auto l = blockOf(K::Links);
                l.label = "Ceux qu'elle lit s'ils existent (sinon, l'\xC3\xA9tape qui s'en sert est saut\xC3\xA9" "e)";
                for (const auto& r : spec->readsOptional)
                    l.links.push_back({r, {}, "Onglet facultatif", ui::kNoColor, ui::Tone::Muted});
                push(std::move(l));
            }
            for (const auto& t : spec->tables) {
                auto b = blockOf(K::Bullet, (t.label.empty() ? t.name : t.label)
                                                + " : un fichier CSV, choisi dans le formulaire (bouton ..., Ctrl+V ou en le glissant)",
                                 "tableau");
                b.tone = famille;
                push(std::move(b));
            }
        }
        if (!spec->produces.empty()) {
            push(blockOf(K::Heading, "Ce qu'elle produit"));
            for (const auto& p : spec->produces) push(blockOf(K::Bullet, p, "-"));
        }
        if (!spec->launches.empty()) {
            push(blockOf(K::Heading, "Les macros qu'elle encha\xC3\xAEne"));
            push(blockOf(K::Paragraph, "Dans cet ordre. Chacune se lance aussi seule ; le formulaire de "
                                       + e.name + " permet d'en d\xC3\xA9" "cocher."));
            auto l = blockOf(K::Links);
            int rang = 0;
            for (const auto& m : spec->launches)
                l.links.push_back({std::to_string(++rang) + ". " + m, "lib:" + m, "Ouvrir la page de " + m,
                                   ui::kNoColor, famille});
            push(std::move(l));
        }
        if (!spec->applyText.empty()) {
            // Les {cle} du texte : le libelle du champ, entre guillemets.
            std::string t = spec->applyText;
            for (std::size_t at = t.find('{'); at != std::string::npos; at = t.find('{', at)) {
                const auto end = t.find('}', at);
                if (end == std::string::npos) break;
                const auto key = t.substr(at + 1, end - at - 1);
                const auto* f = spec->field(key);
                const std::string mot = "\xC2\xAB" + (f && !f->label.empty() ? f->label : key) + "\xC2\xBB";
                t.replace(at, end - at + 1, mot);
                at += mot.size();
            }
            push(callout(0, "Ce que fait Appliquer", t + ". Un seul Ctrl+Z reprend tout."));
        }
    }

    // ---- l'utilisation -------------------------------------------------------
    if (!e.help.usage.empty()) {
        push(blockOf(K::Heading, "Utilisation"));
        for (auto& part : structureProse(e.help.usage)) {
            switch (part.kind) {
                case ProsePart::Kind::Paragraph:
                    push(blockOf(K::Paragraph, std::move(part.text)));
                    break;
                case ProsePart::Kind::Item: {
                    auto b = blockOf(K::Bullet, std::move(part.text), std::move(part.label));
                    b.detail = std::move(part.term);
                    b.tone = famille;
                    push(std::move(b));
                    break;
                }
                case ProsePart::Kind::Table:
                    push(blockOf(K::Table, std::move(part.text)));
                    break;
            }
        }
    }

    // ---- l'exemple -------------------------------------------------------------
    if (!e.help.example.empty()) {
        push(blockOf(K::Heading, "Exemple"));
        push(blockOf(K::Code, e.help.example));
    }

    // ---- ce que l'exemple verifie -------------------------------------------------
    // Les " given " et " expect " de l'exemple : ce qu'on pose avant de lancer,
    // et ce qu'on doit lire apres. C'est la partie de l'aide que " Tout essayer "
    // controle - la montrer, c'est dire au lecteur ce qui est garanti.
    {
        const auto poses = e.givens();
        const auto attendus = e.expectations();
        if (!poses.empty() || !attendus.empty()) {
            push(blockOf(K::Heading, "Ce que l'exemple v\xC3\xA9rifie"));
            for (const auto& g : poses) {
                auto b = blockOf(K::Bullet, g, "pose");
                b.tone = famille;
                push(std::move(b));
            }
            for (const auto& x : attendus) {
                // " 3 : X = v " : le nombre de cycles devant, le reste en texte.
                std::string label = "attendu", texte = x;
                if (const auto deux = x.find(':'); deux != std::string::npos) {
                    const auto n = trimmed(std::string_view(x).substr(0, deux));
                    if (!n.empty() && std::all_of(n.begin(), n.end(),
                                                  [](unsigned char c) { return std::isdigit(c) != 0; })) {
                        label = "apr\xC3\xA8s " + n + (n == "1" ? " cycle" : " cycles");
                        texte = trimmed(std::string_view(x).substr(deux + 1));
                    }
                }
                auto b = blockOf(K::Bullet, texte, label);
                b.tone = famille;
                push(std::move(b));
            }
        }
    }

    // ---- les parametres -------------------------------------------------------
    // L'aide d'un parametre est la sienne, ou celle du fichier COMMUN.hlp du
    // dossier ; les champs declares internes (" #! internal ") ne s'affichent
    // pas ici mais dans leur propre section, plus bas.
    const auto total = e.documentableCount();
    if (total > 0) {
        const auto faits = e.documentedCount();
        push(blockOf(K::Heading, spec != nullptr ? "Ce qu'elle demande" : "Param\xC3\xA8tres"));

        // L'anneau dit combien ; ses puces disent LESQUELS, et y menent.
        auto ring = blockOf(K::Ring, {}, countLabel(faits, total));
        ring.value = static_cast<float>(faits) / static_cast<float>(total);
        ring.muted = faits == total;
        std::vector<std::string> trous;
        for (const auto& d : e.declarations) {
            if (d.isLocal() || d.name.empty() || e.isInternal(d.name)) continue;
            const auto* aide = e.paramHelp(d.name);
            if (!aide || aide->empty()) trous.push_back(d.name);
        }
        if (trous.empty()) {
            ring.text = spec != nullptr ? "Toutes les questions ont leur aide."
                                        : "Tous les param\xC3\xA8tres sont document\xC3\xA9s.";
        } else if (spec != nullptr) {
            ring.text = std::to_string(trous.size())
                      + (trous.size() > 1 ? " questions attendent leur aide :"
                                          : " question attend son aide :");
            for (const auto& t : trous)
                ring.links.push_back({t, "par:" + e.name + "#" + t,
                                      "\xC3\x89" "crire l'aide de " + t,
                                      ui::kNoColor, ui::Tone::Warning});
        } else {
            ring.text = std::to_string(trous.size())
                      + (trous.size() > 1 ? " param\xC3\xA8tres attendent leur aide :"
                                          : " param\xC3\xA8tre attend son aide :");
            for (const auto& t : trous)
                ring.links.push_back({t, "par:" + e.name + "#" + t,
                                      "\xC3\x89" "crire l'aide de " + t,
                                      ui::kNoColor, ui::Tone::Warning});
        }
        push(std::move(ring));

        for (const auto& d : e.declarations) {
            if (d.isLocal() || d.name.empty() || e.isInternal(d.name)) continue;
            bool commune = false;
            const auto* aide = e.paramHelp(d.name, &commune);
            const bool vide = !aide || aide->empty();
            if (spec != nullptr) {
                // LA QUESTION TELLE QUE LE FORMULAIRE LA POSE : son libelle, son
                // genre (fichier, tache, oui-non...), sa cle, son groupe.
                const auto* f = spec->field(d.name);
                const std::string libelle = f != nullptr && !f->label.empty() ? f->label
                                          : d.comment.empty() ? d.name : d.comment;
                auto t = blockOf(K::Term, vide ? std::string{} : reflow(*aide), libelle);
                t.detail = f != nullptr ? project::macro::describe(*f) : d.type;
                if (!d.initial.empty()) t.detail += "   \xC2\xB7   propos\xC3\xA9 : " + d.initial;
                t.muted = vide;
                t.pills.push_back({d.name, ui::kNoColor, ui::Tone::None});
                if (f != nullptr && !f->group.empty())
                    t.pills.push_back({f->group, ui::kNoColor, famille});
                if (f != nullptr && f->advanced)
                    t.pills.push_back({"r\xC3\xA9glage avanc\xC3\xA9", ui::kNoColor, ui::Tone::Muted});
                if (f != nullptr && f->optional)
                    t.pills.push_back({"facultatif", ui::kNoColor, ui::Tone::Muted});
                if (f != nullptr && !f->example.empty())
                    t.extra = "Exemple : " + project::macro::exampleFor(*f, d.initial);
                push(std::move(t));
                continue;
            }
            auto t = blockOf(K::Term, vide ? std::string{} : reflow(*aide), d.name);
            t.detail = signatureOf(d);            // l'export et la recherche le lisent
            t.extra  = (!vide && redundant(d.comment, *aide)) ? std::string{} : d.comment;
            t.muted  = vide;
            t.pills  = pillsOf(d);
            // Une aide venue de COMMUN.hlp se dit : la corriger se fait la-bas,
            // pour toute la famille, pas dans ce fichier.
            if (commune) t.pills.push_back({"aide commune", ui::kNoColor, ui::Tone::None});
            push(std::move(t));
        }
        // Les questions a cle calculee ("mode*nom") : une boucle les pose, le
        // code n'en cite aucune en toutes lettres.
        if (spec != nullptr) {
            for (const auto& f : spec->fields) {
                if (!f.isPattern()) continue;
                auto t = blockOf(K::Term, f.help.empty() ? std::string{} : reflow(f.help),
                                 f.label.empty() ? f.key : f.label);
                t.detail = project::macro::describe(f) + "   \xC2\xB7   une par \xC3\xA9l\xC3\xA9ment (" + f.key + ")";
                t.pills.push_back({f.key, ui::kNoColor, ui::Tone::None});
                if (!f.group.empty()) t.pills.push_back({f.group, ui::kNoColor, famille});
                if (f.advanced) t.pills.push_back({"r\xC3\xA9glage avanc\xC3\xA9", ui::kNoColor, ui::Tone::Muted});
                t.muted = f.help.empty();
                push(std::move(t));
            }
        }
    }

    // ---- les champs internes ------------------------------------------------------
    {
        std::string liste;
        for (const auto& d : e.declarations) {
            if (d.isLocal() || d.name.empty() || !e.isInternal(d.name)) continue;
            liste += (liste.empty() ? "" : ", ") + d.name;
        }
        if (!liste.empty()) {
            push(blockOf(K::Heading, "Champs internes"));
            push(blockOf(K::Paragraph,
                         "Tenus par le bloc d'un cycle \xC3\xA0 l'autre : \xC3\xA0 lire en table d'animation "
                         "pour comprendre ce qu'il fait, jamais \xC3\xA0 \xC3\xA9" "crire. " + liste + "."));
        }
    }

    // ---- les codes de defaut ----------------------------------------------------
    if (!e.faultTable.empty() || !e.help.faults.empty()) {
        push(blockOf(K::Heading, "Codes de d\xC3\xA9" "faut"));
        for (const auto& f : e.faultTable) {
            const auto* aide = e.help.fault(f.code);
            std::string texte = f.text;
            if (aide && !aide->empty()) texte += "\n" + reflow(*aide);
            push(blockOf(K::Fault, texte, f.code));
        }
        for (const auto& h : e.help.faults) {
            const bool deja = std::any_of(e.faultTable.begin(), e.faultTable.end(),
                                          [&](const project::FaultCode& f) { return f.code == h.key; });
            if (!deja) {
                auto b = blockOf(K::Fault, reflow(h.text), h.key);
                b.muted = true;
                push(std::move(b));
            }
        }
    }

    // ---- l'historique ---------------------------------------------------------------
    // " #! changes 1.02 = ... " : ce qu'il faut savoir en passant d'une version a
    // l'autre, du plus recent au plus ancien.
    {
        const auto historique = e.changes();
        if (!historique.empty()) {
            push(blockOf(K::Heading, "Historique des versions"));
            for (const auto& c : historique) {
                auto t = blockOf(K::Term, reflow(c.text), "v" + c.key);
                t.tone = famille;
                push(std::move(t));
            }
        }
    }

    // ---- voir aussi ---------------------------------------------------------------
    // 1.11.2 (T2, decision 141) : une puce par nom. La ligne « see = AppelerSR, EnsureSection,
    // GenererModes » d'une macro reste entiere dans e.help.see (une seule puce jusqu'a la 1.11.1) ;
    // help::seeNames la coupe aux virgules et aux points-virgules, comme les renvois de l'index.
    if (const auto see = ::help::seeNames(e.help); !see.empty()) {
        push(blockOf(K::Heading, "Voir aussi"));
        auto l = blockOf(K::Links);
        for (const auto& v : see)
            l.links.push_back({v, "lib:" + v, "Ouvrir " + v, ui::kNoColor, ui::Tone::None});
        push(std::move(l));
    }

    if (!e.help.since.empty() || !e.help.author.empty()) {
        push(blockOf(K::Separator));
        std::string s;
        if (!e.help.since.empty())  s += "depuis " + e.help.since;
        if (!e.help.author.empty()) { if (!s.empty()) s += "   \xC2\xB7   "; s += e.help.author; }
        push(blockOf(K::Subtitle, s));
    }
    return a;
}

ui::HelpArticle buildLibraryOverview(const std::vector<CatalogEntry>& entries,
                                     std::string_view category, std::string_view rootLabel) {
    const std::string racine = rootLabel.empty() ? std::string("Biblioth\xC3\xA8ques") : std::string(rootLabel);
    using K = ui::HelpBlockKind;
    ui::HelpArticle a;
    auto push = [&a](ui::HelpBlock b) { a.blocks.push_back(std::move(b)); };

    std::vector<std::string> cats;
    std::size_t faits = 0, total = 0, blocs = 0;
    for (const auto& e : entries) {
        if (!category.empty() && e.category != category) continue;
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end()) cats.push_back(e.category);
        faits += e.documentedCount();
        total += e.documentableCount();
        ++blocs;
    }

    auto h = blockOf(K::Hero, category.empty() ? racine : categoryLabel(category));
    h.tone = category.empty() ? ui::Tone::Accent : ui::familyTone(familyIndex(category));
    h.pills.push_back({std::to_string(blocs) + (blocs > 1 ? " \xC3\xA9l\xC3\xA9ments" : " \xC3\xA9l\xC3\xA9ment"),
                       ui::kNoColor, h.tone});
    if (!category.empty()) {
        h.links.push_back({racine, "cat:", rootLabel.empty() ? std::string("Toutes les biblioth\xC3\xA8ques")
                                                             : "Tout l'onglet " + racine,
                           ui::kNoColor, ui::Tone::None});
        h.links.push_back({categoryLabel(category), {}, {}, ui::kNoColor, ui::Tone::None});
        h.label = "Dossier libs/" + std::string(category);
    } else {
        h.label = std::to_string(cats.size()) + " dossiers dans libs/";
    }
    push(std::move(h));

    if (blocs == 0) {
        auto vide = blockOf(K::Empty, "Ce dossier est vide.");
        vide.icon = ui::Icon::FolderOpen;
        vide.links.push_back({"Toutes les biblioth\xC3\xA8ques", "cat:", {}, ui::kNoColor,
                              ui::Tone::None});
        push(std::move(vide));
        return a;
    }

    push(blockOf(K::Lead, category.empty()
        ? "Tout ce que libs/ partage entre les projets. Chaque puce ouvre sa page ; sa "
          "couleur dit o\xC3\xB9 en est sa documentation."
        : "Chaque puce ouvre sa page ; sa couleur dit o\xC3\xB9 en est sa documentation."));

    if (total > 0) {
        auto ring = blockOf(K::Ring, {}, countLabel(faits, total));
        ring.value = static_cast<float>(faits) / static_cast<float>(total);
        ring.muted = faits == total;
        ring.text = std::to_string(faits) + " param\xC3\xA8tres document\xC3\xA9s sur "
                  + std::to_string(total) + ".";
        // Les moins avances d'abord : c'est la que le travail est.
        std::vector<const CatalogEntry*> todo;
        for (const auto& e : entries) {
            if (!category.empty() && e.category != category) continue;
            if (e.documentableCount() > e.documentedCount()) todo.push_back(&e);
        }
        std::stable_sort(todo.begin(), todo.end(), [](const CatalogEntry* x, const CatalogEntry* y) {
            const float fx = static_cast<float>(x->documentedCount()) / static_cast<float>(x->documentableCount());
            const float fy = static_cast<float>(y->documentedCount()) / static_cast<float>(y->documentableCount());
            return fx < fy;
        });
        if (!todo.empty()) {
            ring.text += " Les moins avanc\xC3\xA9s :";
            for (std::size_t i = 0; i < todo.size() && i < 8; ++i) {
                const auto* e = todo[i];
                const float f = static_cast<float>(e->documentedCount())
                              / static_cast<float>(e->documentableCount());
                ring.links.push_back({e->name, "lib:" + e->name,
                                      countLabel(e->documentedCount(), e->documentableCount())
                                          + " param\xC3\xA8tres document\xC3\xA9s",
                                      ui::kNoColor, ui::coverageTone(f)});
            }
        }
        push(std::move(ring));
    }

    for (const auto& cat : cats) {
        if (category.empty()) push(blockOf(K::Heading, categoryLabel(cat)));
        auto l = blockOf(K::Links);
        for (const auto& e : entries) {
            if (e.category != cat) continue;
            const auto n = e.documentableCount();
            const float f = n == 0 ? 1.f : static_cast<float>(e.documentedCount()) / static_cast<float>(n);
            std::string tip = e.help.summary.empty() ? std::string("Pas encore de r\xC3\xA9sum\xC3\xA9")
                                                     : reflow(e.help.summary);
            if (n > 0) tip += "  (" + countLabel(e.documentedCount(), n) + ")";
            l.links.push_back({e.name, "lib:" + e.name, std::move(tip), ui::kNoColor,
                               n == 0 ? ui::Tone::None : ui::coverageTone(f)});
        }
        push(std::move(l));
    }
    return a;
}

ui::HelpArticle buildFolderOverview(const std::vector<CatalogEntry>& entries,
                                    const std::vector<std::string>& folders,
                                    std::string_view folder, std::string_view rootLabel) {
    // LA PAGE D'UN DOSSIER DE MACROS (lot macros 1). Ce qu'il range, sous-dossier
    // par sous-dossier, chaque macro en puce avec son resume au survol.
    using K = ui::HelpBlockKind;
    namespace mm = project::macro;
    ui::HelpArticle a;
    auto push = [&a](ui::HelpBlock b) { a.blocks.push_back(std::move(b)); };
    const std::string racine = rootLabel.empty() ? std::string("Macros") : std::string(rootLabel);
    const std::string dossier(folder);
    const auto folderAt = [&](std::size_t i) { return i < folders.size() ? folders[i] : std::string{}; };

    std::size_t n = 0;
    ui::Tone tone = ui::Tone::Accent;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!dossier.empty() && !mm::insideFolder(folderAt(i), dossier)) continue;
        if (n++ == 0) tone = ui::familyTone(familyIndex(entries[i].category));
    }

    auto h = blockOf(K::Hero, dossier.empty() ? racine : mm::folderLeaf(dossier));
    h.tone = tone;
    h.pills.push_back({std::to_string(n) + (n > 1 ? " macros" : " macro"), ui::kNoColor, tone});
    h.links.push_back({racine, "cat:", "Tout l'onglet " + racine, ui::kNoColor, ui::Tone::None});
    {
        std::string chemin;
        std::size_t from = 0;
        while (!dossier.empty() && from <= dossier.size()) {
            const auto slash = dossier.find('/', from);
            const auto seg = dossier.substr(from, (slash == std::string::npos ? dossier.size() : slash) - from);
            chemin += (chemin.empty() ? "" : "/") + seg;
            const bool dernier = slash == std::string::npos;
            h.links.push_back({seg, dernier ? std::string{} : "cat:" + chemin, {}, ui::kNoColor, ui::Tone::None});
            if (dernier) break;
            from = slash + 1;
        }
    }
    h.label = dossier.empty() ? std::string("Les macros de libs/Macros, rang\xC3\xA9" "es comme dans l'onglet Macros")
                              : "Un dossier de l'onglet Macros (un rangement : il ne change aucun nom)";
    push(std::move(h));

    if (n == 0) {
        auto vide = blockOf(K::Empty, "Ce dossier est vide.");
        vide.icon = ui::Icon::FolderOpen;
        vide.links.push_back({"Toutes les macros", "cat:", {}, ui::kNoColor, ui::Tone::None});
        push(std::move(vide));
        return a;
    }
    push(blockOf(K::Lead, "Chaque puce ouvre la page de sa macro : ce qu'elle demande, ce qu'elle lit, "
                          "ce qu'elle produit. On la lance depuis l'onglet Macros (Entr\xC3\xA9" "e ou double-clic)."));

    // Les macros rangees directement ici, puis chaque sous-dossier (a toute
    // profondeur) sous son titre, dans l'ordre de l'onglet.
    const auto linksOf = [&](const std::string& exact) {
        auto l = blockOf(K::Links);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (folderAt(i) != exact) continue;
            const auto& e = entries[i];
            std::string tip = e.help.summary.empty() ? std::string("Pas encore de r\xC3\xA9sum\xC3\xA9")
                                                     : reflow(e.help.summary);
            l.links.push_back({e.name, "lib:" + e.name, std::move(tip), ui::kNoColor,
                               e.help.summary.empty() ? ui::Tone::Warning : ui::familyTone(familyIndex(e.category))});
        }
        return l;
    };
    if (auto ici = linksOf(dossier); !ici.links.empty()) push(std::move(ici));
    std::vector<std::string> sous;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto f = folderAt(i);
        if (f == dossier || (!dossier.empty() && !mm::insideFolder(f, dossier))) continue;
        if (std::find(sous.begin(), sous.end(), f) == sous.end()) sous.push_back(f);
    }
    for (const auto& f : sous) {
        const std::string titre = dossier.empty() ? f : f.substr(std::min(f.size(), dossier.size() + 1));
        std::string joli;                                   // "A/B" -> "A > B"
        for (const char ch : titre) {
            if (ch == '/') joli += " \xC2\xBB ";
            else joli += ch;
        }
        push(blockOf(K::Heading, joli));
        push(linksOf(f));
    }
    return a;
}

std::string sourceOf(const CatalogEntry& e) {
    // LES DECLARATIONS EN SYNTAXE IEC, et pas au format du fichier. Le fichier
    // ecrit " Count ; INT ; Input ; 16 ; commentaire " : lisible par la macro
    // Excel, faux pour le surligneur ST - une apostrophe dans un commentaire
    // ouvrait une chaine qui repeignait le reste de la ligne en rouge. En
    // VAR_INPUT / VAR_OUTPUT, avec les commentaires entre (* *), c'est ce que
    // Control Expert montre, et chaque couleur dit vrai. Le fichier tel quel
    // reste a un clic : " Ouvrir le fichier source ".
    std::string s;
    s += "(* " + (e.fileName.empty() ? e.name : e.fileName);
    if (!e.version.empty()) s += "   -   version " + e.version;
    s += " *)\n";
    const auto sectionOf = [](const project::Declaration& d) -> int {
        const auto sc = lower(d.scope);
        return sc == "input" ? 0 : sc == "output" ? 1 : sc == "inout" ? 2 : sc == "local" ? 4 : 3;
    };
    static constexpr const char* kHeads[] = {"VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT",
                                             "STRUCT", "VAR"};
    for (int section = 0; section < 5; ++section) {
        bool opened = false;
        for (const auto& d : e.declarations) {
            if (sectionOf(d) != section || d.name.empty()) continue;
            if (!opened) { s += "\n" + std::string(kHeads[section]) + "\n"; opened = true; }
            std::string line = "    " + d.name + " : " + d.type;
            if (!d.initial.empty()) line += " := " + d.initial;
            line += ";";
            if (!d.comment.empty()) {
                // Pas de colonne a l'espace : la police de l'editeur est
                // proportionnelle chez certains, et une colonne ratee de
                // quelques pixels se voit plus qu'un commentaire colle.
                // "*)" dans un commentaire fermerait le commentaire : on le casse.
                std::string c = d.comment;
                for (std::size_t at = c.find("*)"); at != std::string::npos; at = c.find("*)", at + 2))
                    c.replace(at, 2, "* )");
                line += "   (* " + c + " *)";
            }
            s += line + "\n";
        }
        if (opened) s += section == 3 ? "END_STRUCT\n" : "END_VAR\n";
    }
    return s;
}

// ----------------------------------------------------------------- arbre ----
namespace {
constexpr std::uint64_t kKindShift = 56;
enum class NodeKind : std::uint8_t { Root = 1, Category = 2, Entry = 3 };

ui::NodeId pack(NodeKind k, std::uint32_t index) {
    return (static_cast<std::uint64_t>(k) << kKindShift) | (index + 1);
}
NodeKind kindOf(ui::NodeId n) {
    return static_cast<NodeKind>((n >> kKindShift) & 0xFF);
}
std::uint32_t indexOf(ui::NodeId n) {
    return static_cast<std::uint32_t>(n & 0xFFFFFFFFull) - 1;
}
} // namespace

LibraryHelpTreeModel::LibraryHelpTreeModel(std::vector<CatalogEntry> entries, TreeOptions options)
    : entries_(std::move(entries)), rootLabel_(std::move(options.rootLabel)) {
    // DEUX RANGEMENTS. Les blocs vont dans le dossier de libs/ qui les contient
    // (une categorie, un niveau). Les macros (lot macros 1) vont dans les
    // dossiers de l'onglet Macros, qui s'imbriquent ("A/B") et ont leur ordre.
    byFolder_ = !options.folders.empty();
    entryFolder_.resize(entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i)
        entryFolder_[i] = byFolder_ ? (i < options.folders.size() ? options.folders[i] : std::string{})
                                    : entries_[i].category;
    if (byFolder_) {
        // Les dossiers dans l'ordre de l'onglet, et seulement ceux qui ont
        // quelque chose : un dossier vide n'a rien a expliquer.
        std::vector<std::string> needed;
        for (const auto& f : entryFolder_) {
            for (std::string p = f; !p.empty(); p = project::macro::folderParent(p))
                if (std::find(needed.begin(), needed.end(), p) == needed.end()) needed.push_back(p);
        }
        for (const auto& f : options.folderOrder)
            if (std::find(needed.begin(), needed.end(), f) != needed.end()) (void)ensureFolder(f);
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& f = entryFolder_[i];
        if (byFolder_ && f.empty()) { rootItems_.push_back(i); continue; }
        const auto at = ensureFolder(f);
        folders_[at].items.push_back(i);
        if (folders_[at].items.size() == 1 && folders_[at].family == 5)
            folders_[at].family = familyIndex(entries_[i].category);
        for (int p = static_cast<int>(at); p >= 0; p = folders_[static_cast<std::size_t>(p)].parent)
            ++folders_[static_cast<std::size_t>(p)].deepCount;
    }
    // Un dossier sans macro a lui prend la famille de ses sous-dossiers.
    for (std::size_t i = folders_.size(); i-- > 0;)
        if (folders_[i].family == 5 && !folders_[i].subfolders.empty())
            folders_[i].family = folders_[folders_[i].subfolders.front()].family;
}

std::size_t LibraryHelpTreeModel::ensureFolder(const std::string& path) {
    for (std::size_t i = 0; i < folders_.size(); ++i)
        if (folders_[i].path == path) return i;
    int parent = -1;
    if (byFolder_) {
        if (const auto up = project::macro::folderParent(path); !up.empty())
            parent = static_cast<int>(ensureFolder(up));
    }
    Folder f;
    f.path   = path;
    f.label  = byFolder_ ? project::macro::folderLeaf(path) : categoryLabel(path);
    f.parent = parent;
    f.family = byFolder_ ? 5 : familyIndex(path);
    folders_.push_back(std::move(f));
    const auto index = folders_.size() - 1;
    if (parent < 0) rootFolders_.push_back(index);
    else            folders_[static_cast<std::size_t>(parent)].subfolders.push_back(index);
    return index;
}

ui::NodeId LibraryHelpTreeModel::root() const { return pack(NodeKind::Root, 0); }

std::size_t LibraryHelpTreeModel::childCount(ui::NodeId n) const {
    switch (kindOf(n)) {
        case NodeKind::Root:     return rootFolders_.size() + rootItems_.size();
        case NodeKind::Category: {
            const auto i = indexOf(n);
            return i < folders_.size() ? folders_[i].subfolders.size() + folders_[i].items.size() : 0;
        }
        case NodeKind::Entry:    return 0;
    }
    return 0;
}

ui::NodeId LibraryHelpTreeModel::childAt(ui::NodeId n, std::size_t i) const {
    // Les dossiers d'abord, puis les entrees : comme l'explorateur de fichiers.
    const auto pick = [](const std::vector<std::size_t>& subs, const std::vector<std::size_t>& items,
                         std::size_t k) -> ui::NodeId {
        if (k < subs.size()) return pack(NodeKind::Category, static_cast<std::uint32_t>(subs[k]));
        k -= subs.size();
        if (k < items.size()) return pack(NodeKind::Entry, static_cast<std::uint32_t>(items[k]));
        return ui::kInvalidNode;
    };
    switch (kindOf(n)) {
        case NodeKind::Root:
            return pick(rootFolders_, rootItems_, i);
        case NodeKind::Category: {
            const auto c = indexOf(n);
            if (c >= folders_.size()) return ui::kInvalidNode;
            return pick(folders_[c].subfolders, folders_[c].items, i);
        }
        case NodeKind::Entry: break;
    }
    return ui::kInvalidNode;
}

bool LibraryHelpTreeModel::hasChildren(ui::NodeId n) const { return childCount(n) > 0; }

std::string LibraryHelpTreeModel::text(ui::NodeId n) const {
    // LE NOM, SEUL. Le compteur et la couverture sont dans la pastille de
    // droite (style().badge) : colles au nom, ils se lisaient comme une partie
    // du nom et disparaissaient les premiers quand l'arbre se retrecit.
    switch (kindOf(n)) {
        case NodeKind::Root:
            return rootLabel_.empty() ? std::string("Biblioth\xC3\xA8ques") : rootLabel_;
        case NodeKind::Category: {
            const auto i = indexOf(n);
            return i < folders_.size() ? folders_[i].label : std::string{};
        }
        case NodeKind::Entry: {
            const auto i = indexOf(n);
            return i < entries_.size() ? entries_[i].name : std::string{};
        }
    }
    return {};
}

ui::CellStyle LibraryHelpTreeModel::style(ui::NodeId n) const {
    ui::CellStyle s;
    switch (kindOf(n)) {
        case NodeKind::Root:
            s.icon = ui::Icon::Library;
            s.bold = true;
            s.iconTone = ui::Tone::Accent;
            s.badge = std::to_string(entries_.size());
            break;
        case NodeKind::Category: {
            const auto i = indexOf(n);
            s.icon = ui::Icon::Folder;
            if (i < folders_.size()) {
                // Le dossier porte la couleur de sa famille : c'est la meme que
                // le lisere de chacune de ses pages.
                s.iconTone = ui::familyTone(folders_[i].family);
                s.badge = std::to_string(folders_[i].deepCount);
            }
            break;
        }
        case NodeKind::Entry: {
            const auto i = indexOf(n);
            if (i >= entries_.size()) break;
            const auto& e = entries_[i];
            s.icon = e.kind == CatalogKind::FunctionBlock ? ui::Icon::FunctionBlock
                   : e.kind == CatalogKind::DerivedType   ? ui::Icon::DerivedType
                   : e.kind == CatalogKind::Macro         ? ui::Icon::Play
                                                          : ui::Icon::Document;
            s.iconTone = ui::familyTone(familyIndex(e.category));
            // L'icone porte l'etat en plus du genre : un bloc sans resume est
            // signale la ou l'oeil passe deja.
            if (!e.hasHelp || e.help.summary.empty()) {
                s.iconColor = gfx::Color::rgb(0xB0'7D'2B);
                s.iconTone  = ui::Tone::Warning;
            }
            // La couverture, en pastille : vert complet, ambre a moitie, rouge
            // en dessous. Les memes paliers que l'anneau de la page.
            if (const auto total = e.documentableCount(); total > 0) {
                const auto faits = e.documentedCount();
                s.badge = countLabel(faits, total);
                s.badgeTone = ui::coverageTone(static_cast<float>(faits)
                                               / static_cast<float>(total));
            }
            break;
        }
    }
    return s;
}

const CatalogEntry* LibraryHelpTreeModel::entry(ui::NodeId n) const {
    if (kindOf(n) != NodeKind::Entry) return nullptr;
    const auto i = indexOf(n);
    return i < entries_.size() ? &entries_[i] : nullptr;
}

std::optional<std::string> LibraryHelpTreeModel::folderOf(ui::NodeId n) const {
    switch (kindOf(n)) {
        case NodeKind::Root:     return std::string{};
        case NodeKind::Category: {
            const auto i = indexOf(n);
            if (i < folders_.size()) return folders_[i].path;
            return std::nullopt;
        }
        case NodeKind::Entry:    return std::nullopt;
    }
    return std::nullopt;
}

std::string LibraryHelpTreeModel::folderOfEntry(std::size_t index) const {
    return index < entryFolder_.size() ? entryFolder_[index] : std::string{};
}

ui::NodeId LibraryHelpTreeModel::nodeForFolder(std::string_view path) const {
    if (path.empty()) return root();
    for (std::size_t i = 0; i < folders_.size(); ++i)
        if (folders_[i].path == path) return pack(NodeKind::Category, static_cast<std::uint32_t>(i));
    return ui::kInvalidNode;
}

CatalogEntry* LibraryHelpTreeModel::mutableEntry(ui::NodeId n) {
    return const_cast<CatalogEntry*>(entry(n));
}

ui::NodeId LibraryHelpTreeModel::nodeForEntry(std::size_t index) const {
    if (index >= entries_.size()) return ui::kInvalidNode;
    return pack(NodeKind::Entry, static_cast<std::uint32_t>(index));
}

bool LibraryHelpTreeModel::matches(ui::NodeId n, std::string_view term) const {
    if (term.empty()) return true;
    const auto t = lower(term);
    const auto* e = entry(n);
    if (!e) {
        // Un dossier correspond quand son nom correspond ; sinon il ne
        // disparait pas pour autant, parce que TreeView garde les ancetres
        // d'une correspondance.
        if (kindOf(n) == NodeKind::Category) {
            const auto i = indexOf(n);
            return i < folders_.size() && (lower(folders_[i].path).find(t) != std::string::npos
                                           || lower(folders_[i].label).find(t) != std::string::npos);
        }
        return true;
    }
    if (lower(e->name).find(t) != std::string::npos
        || lower(e->category).find(t) != std::string::npos
        || lower(e->help.summary).find(t) != std::string::npos) return true;
    if (byFolder_) {
        const auto i = indexOf(n);
        if (i < entryFolder_.size() && lower(entryFolder_[i]).find(t) != std::string::npos) return true;
    }
    // Un nom de parametre aussi : taper " Thermal " montre les blocs qui en ont
    // un, ce qui est la question qu'on se pose en le tapant.
    return std::any_of(e->declarations.begin(), e->declarations.end(),
                       [&](const project::Declaration& d) {
                           return !d.isLocal() && lower(d.name).find(t) != std::string::npos;
                       });
}

// ------------------------------------------------------- liste de champs ----
void HelpFieldListModel::setFields(std::vector<HelpField> fields, const LibraryHelp* draft) {
    fields_ = std::move(fields);
    draft_  = draft;
}

std::string HelpFieldListModel::text(ui::RowIndex row) const {
    if (row >= fields_.size()) return {};
    const auto& f = fields_[row];
    const bool rempli = draft_ && !readField(*draft_, f).empty();
    // Le marqueur est le premier caractere de la ligne, pas une colonne : dans
    // une liste etroite, une colonne d'etat est la premiere chose qu'on coupe.
    return (rempli ? "+ " : "- ") + f.label;
}

ui::CellStyle HelpFieldListModel::style(ui::RowIndex row) const {
    ui::CellStyle s;
    if (row >= fields_.size()) return s;
    const auto& f = fields_[row];
    const bool rempli = draft_ && !readField(*draft_, f).empty();
    // Rempli : une coche verte. Vide : le texte attenue, par le THEME - le
    // gris fixe d'avant disparaissait sur le fond sombre.
    // Vide : un triangle pale, pas une alerte - il garde la colonne alignee et
    // dit " a documenter " sans crier.
    s.icon     = rempli ? ui::Icon::Ok : ui::Icon::Warning;
    s.iconTone = rempli ? ui::Tone::Ok : ui::Tone::Muted;
    if (!rempli) {
        s.fg     = gfx::Color::rgb(0x8A'8A'8A);
        s.fgTone = ui::Tone::Muted;
    }
    if (f.kind == HelpField::Kind::Summary) s.bold = true;
    if (f.kind == HelpField::Kind::Param || f.kind == HelpField::Kind::Fault) s.monospace = true;
    return s;
}

const HelpField* HelpFieldListModel::at(ui::RowIndex row) const {
    return row < fields_.size() ? &fields_[row] : nullptr;
}

} // namespace app
