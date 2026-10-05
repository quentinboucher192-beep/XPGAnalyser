#include "LibraryHelp.hpp"

#include <algorithm>
#include <cctype>

namespace project {

namespace {

constexpr std::string_view kMark = "#!";

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool sameKey(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x))
            == std::tolower(static_cast<unsigned char>(y));
    });
}

// Une ligne, et la fin de ligne qui la suivait. Les garder separees est ce qui
// permet de recopier un fichier a l'octet pres : un .ddt ecrit sous Windows a
// des CRLF, celui d'a cote des LF, et remplacer les uns par les autres ferait
// apparaitre le fichier entier comme modifie dans un gestionnaire de versions.
struct Line {
    std::string text;
    std::string eol;
};

std::vector<Line> split(std::string_view s) {
    std::vector<Line> out;
    std::size_t at = 0;
    while (at < s.size()) {
        const auto nl = s.find('\n', at);
        if (nl == std::string_view::npos) {
            out.push_back({std::string(s.substr(at)), {}});
            break;
        }
        std::size_t end = nl;
        std::string eol = "\n";
        if (end > at && s[end - 1] == '\r') {
            --end;
            eol = "\r\n";
        }
        out.push_back({std::string(s.substr(at, end - at)), std::move(eol)});
        at = nl + 1;
    }
    return out;
}

std::string join(const std::vector<Line>& lines) {
    std::string out;
    for (const auto& l : lines) {
        out += l.text;
        out += l.eol;
    }
    return out;
}

// Une ligne d'en-tete : "cle = valeur", sans point-virgule. "name = X",
// "version = 1.00". Une declaration de parametre en contient un, et un
// commentaire commence par "#".
bool isHeaderLine(std::string_view text) {
    const auto t = trim(text);
    if (t.empty() || t[0] == '#') return false;
    if (t.find(';') != std::string::npos) return false;
    const auto eq = t.find('=');
    // eq == 0 SUFFIT a garantir une cle non vide, parce que t est deja rogne :
    // aucun espace ne peut preceder le signe. Un premier jet ajoutait un
    // "la cle n'est pas que des blancs" par-dessus - une passe de mutations l'a
    // montre inatteignable, ce qui est la definition du code mort.
    return eq != std::string::npos && eq != 0;
}

bool isHelpLine(std::string_view text) {
    const auto t = trim(text);
    return t.size() >= kMark.size() && t.compare(0, kMark.size(), kMark) == 0;
}

// "#! param Fbk = le retour" -> key "param", sub "Fbk", value "le retour".
bool parseHelpLine(std::string_view text, std::string& key, std::string& sub,
                   std::string& value) {
    auto t = trim(text);
    if (!isHelpLine(t)) return false;
    t = trim(std::string_view(t).substr(kMark.size()));

    const auto eq = t.find('=');
    if (eq == std::string::npos) return false;

    const auto spec = trim(std::string_view(t).substr(0, eq));
    value = trim(std::string_view(t).substr(eq + 1));
    if (spec.empty()) return false;

    const auto space = spec.find_first_of(" \t");
    if (space == std::string::npos) {
        key = spec;
        sub.clear();
    } else {
        key = spec.substr(0, space);
        sub = trim(std::string_view(spec).substr(space + 1));
    }
    return true;
}

void appendLine(std::string& field, const std::string& value) {
    // UNE CLE REPETEE AJOUTE UNE LIGNE. C'est tout le mecanisme du texte long :
    // pas de caractere de continuation a retenir, pas de guillemets a equilibrer,
    // et une ligne d'aide reste une ligne dans l'editeur de texte.
    if (!field.empty()) field += "\n";
    field += value;
}

void set(std::vector<HelpEntry>& list, std::string_view key, std::string text, bool append) {
    for (auto& e : list) {
        if (sameKey(e.key, key)) {
            if (append) appendLine(e.text, text);
            else e.text = std::move(text);
            return;
        }
    }
    list.push_back({std::string(key), std::move(text)});
}

const std::string* get(const std::vector<HelpEntry>& list, std::string_view key) {
    for (const auto& e : list)
        if (sameKey(e.key, key)) return &e.text;
    return nullptr;
}

// Une valeur multiligne s'ecrit en autant de lignes prefixees.
void emit(std::string& out, std::string_view prefix, const std::string& value,
          const std::string& eol) {
    if (value.empty()) return;
    std::size_t at = 0;
    while (at <= value.size()) {
        auto nl = value.find('\n', at);
        if (nl == std::string::npos) nl = value.size();
        out += kMark;
        out += ' ';
        out += prefix;
        out += " = ";
        out += value.substr(at, nl - at);
        out += eol;
        if (nl == value.size()) break;
        at = nl + 1;
    }
}

} // namespace

// ---------------------------------------------------------------------------
bool LibraryHelp::empty() const noexcept {
    return summary.empty() && usage.empty() && example.empty() && since.empty()
        && author.empty() && see.empty() && params.empty() && faults.empty()
        && unknown.empty();
}

const std::string* LibraryHelp::param(std::string_view name) const {
    return get(params, name);
}
const std::string* LibraryHelp::fault(std::string_view code) const {
    return get(faults, code);
}
void LibraryHelp::setParam(std::string_view name, std::string text) {
    set(params, name, std::move(text), false);
}
void LibraryHelp::setFault(std::string_view code, std::string text) {
    set(faults, code, std::move(text), false);
}

// ---------------------------------------------------------------------------
HelpParse readHelp(std::string_view fileContents) {
    HelpParse out;
    const auto lines = split(fileContents);

    // DEUX DRAPEAUX, ET PAS UN SEUL. Un premier jet se servait de "seenFirst"
    // pour les deux questions - a-t-on vu le bloc d'aide, et a-t-on vu le nom -
    // et le nom cessait donc d'etre cherche des la premiere ligne d'aide. Or
    // poser une aide sur un fichier sans "name =" la met EN TETE : le nom se
    // retrouve alors apres elle, et n'etait plus lu.
    bool seenFirst = false, seenName = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& text = lines[i].text;

        if (!seenName) {
            const auto t = trim(text);
            if (t.size() > 5 && sameKey(t.substr(0, 5), "name ")) {
                const auto eq = t.find('=');
                if (eq != std::string::npos) {
                    out.itemName = trim(std::string_view(t).substr(eq + 1));
                    seenName = true;
                }
            }
        }

        if (!isHelpLine(text)) continue;

        // Le bloc est le PREMIER passage de lignes d'aide. Des lignes d'aide
        // egarees plus loin sont quand meme lues - rien ne doit se perdre - mais
        // la reecriture les ramene toutes ici, en un seul endroit.
        if (!seenFirst) {
            seenFirst = true;
            out.hadHelp = true;
            out.firstHelpLine = i;
            std::size_t j = i;
            while (j < lines.size() && isHelpLine(lines[j].text)) ++j;
            out.helpLineCount = j - i;
        }

        std::string key, sub, value;
        if (!parseHelpLine(text, key, sub, value)) {
            out.warnings.push_back("ligne " + std::to_string(i + 1)
                                   + " : aide sans '=' , ignoree");
            continue;
        }

        auto& h = out.help;
        if (sameKey(key, "summary") && sub.empty())      appendLine(h.summary, value);
        else if (sameKey(key, "usage") && sub.empty())   appendLine(h.usage, value);
        else if (sameKey(key, "example") && sub.empty()) appendLine(h.example, value);
        else if (sameKey(key, "since") && sub.empty())   h.since = value;
        else if (sameKey(key, "author") && sub.empty())  h.author = value;
        else if (sameKey(key, "see") && sub.empty())     h.see.push_back(value);
        else if (sameKey(key, "param") && !sub.empty())  set(h.params, sub, value, true);
        else if (sameKey(key, "fault") && !sub.empty())  set(h.faults, sub, value, true);
        else {
            // Une cle qu'on ne connait pas est gardee TELLE QUELLE, sujet
            // compris. Une version plus recente en ecrira, et les jeter ferait
            // perdre le travail de quelqu'un d'autre sans rien dire.
            const auto full = sub.empty() ? key : key + " " + sub;
            set(h.unknown, full, value, true);
        }
    }
    return out;
}

namespace {

std::string render(const LibraryHelp& h, const std::string& eol) {
    std::string out;
    emit(out, "summary", h.summary, eol);
    emit(out, "usage", h.usage, eol);
    emit(out, "example", h.example, eol);
    emit(out, "since", h.since, eol);
    emit(out, "author", h.author, eol);
    for (const auto& s : h.see) emit(out, "see", s, eol);
    for (const auto& e : h.params) emit(out, "param " + e.key, e.text, eol);
    for (const auto& e : h.faults) emit(out, "fault " + e.key, e.text, eol);
    for (const auto& e : h.unknown) emit(out, e.key, e.text, eol);
    return out;
}

} // namespace

std::string renderHelpBlock(const LibraryHelp& h) {
    // L'ORDRE EST TOUJOURS LE MEME, et il n'est donc pas celui du fichier
    // d'origine si celui-ci etait range autrement. La premiere reecriture
    // normalise le bloc, toutes les suivantes rendent exactement le meme texte.
    // C'est la propriete qui compte : une sauvegarde qui change le fichier a
    // chaque fois, sans que rien n'ait ete modifie, rend tout historique
    // illisible.
    return render(h, "\n");
}

std::string writeHelp(std::string_view fileContents, const LibraryHelp& help) {
    auto lines = split(fileContents);

    // La fin de ligne du fichier, pas la notre. Un .ddt en CRLF reste en CRLF.
    std::string eol = "\n";
    for (const auto& l : lines)
        if (!l.eol.empty()) { eol = l.eol; break; }

    const auto parsed = readHelp(fileContents);
    const auto bloc = render(help, eol);

    std::vector<Line> out;
    out.reserve(lines.size() + 8);

    bool posee = false, vuNom = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        // Le bloc d'origine est remplace ICI, en une fois.
        if (parsed.hadHelp && i == parsed.firstHelpLine) {
            for (const auto& l : split(bloc)) out.push_back(l);
            posee = true;
            i += parsed.helpLineCount - 1;
            continue;
        }
        // Toute autre ligne d'aide egaree disparait : elle a ete relue et
        // reecrite dans le bloc.
        if (parsed.hadHelp && isHelpLine(lines[i].text)) continue;

        out.push_back(lines[i]);

        // Pas d'aide dans le fichier : on la pose apres tout l'EN-TETE, pas
        // juste apres "name =".
        //
        // Un fichier reel porte "name = X" puis "version = 1.00", et parfois
        // d'autres cles. S'inserer entre les deux couperait l'en-tete en deux
        // pour aucune raison. On attend donc la derniere ligne "cle = valeur"
        // du bloc de tete.
        // LE NOM EST NOTE AVANT LE TEST, pas apres : "name =" est lui-meme une
        // ligne d'en-tete, et c'est la derniere quand le fichier n'a rien
        // d'autre. Le noter apres faisait manquer ce cas, et l'aide partait
        // alors en tete du fichier.
        if (!vuNom) {
            const auto t = trim(lines[i].text);
            if (t.size() > 5 && sameKey(t.substr(0, 5), "name ")) vuNom = true;
        }
        if (!parsed.hadHelp && !posee && !bloc.empty() && vuNom
            && isHeaderLine(lines[i].text)
            && (i + 1 >= lines.size() || !isHeaderLine(lines[i + 1].text))) {
            for (const auto& l : split(bloc)) out.push_back(l);
            posee = true;
        }
    }

    // Un fichier sans "name =" du tout : l'aide va en tete, plutot que nulle
    // part. Perdre ce que l'utilisateur vient d'ecrire serait le pire des choix.
    if (!posee && !bloc.empty()) {
        auto tete = split(bloc);
        out.insert(out.begin(), tete.begin(), tete.end());
    }

    return join(out);
}

} // namespace project
