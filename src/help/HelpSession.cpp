// =============================================================================
//  help/HelpSession.cpp
// =============================================================================
#include "HelpSession.hpp"

#include "HelpCodes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace help {
namespace {

std::string_view kindTag(TargetKind k) noexcept {
    switch (k) {
        case TargetKind::LibraryEntry: return "lib";
        case TargetKind::Parameter:    return "par";
        case TargetKind::Diagnostic:   return "dia";
        case TargetKind::Glossary:     return "glo";
        case TargetKind::Topic:        return "sujet";   // 1.11 : un sujet du centre d'aide
        default:                       return "non";
    }
}

TargetKind kindFromTag(std::string_view tag) noexcept {
    if (tag == "lib") return TargetKind::LibraryEntry;
    if (tag == "par") return TargetKind::Parameter;
    if (tag == "dia") return TargetKind::Diagnostic;
    if (tag == "glo") return TargetKind::Glossary;
    if (tag == "sujet") return TargetKind::Topic;
    return TargetKind::None;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return std::string(s.substr(a, b - a));
}

// Enleve `t` d'une liste, s'il y est. Utilise par les recents (remonter en
// tete) comme par les favoris (retirer).
void erase(std::vector<Target>& list, const Target& t) {
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Target& x) { return sameTarget(x, t); }),
               list.end());
}

std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> out;
    std::size_t              i = 0;
    while (i <= text.size()) {
        const std::size_t nl = text.find('\n', i);
        const bool last = nl == std::string_view::npos;
        std::string_view raw = text.substr(i, (last ? text.size() : nl) - i);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        out.emplace_back(raw);
        if (last) break;
        i = nl + 1;
    }
    return out;
}

// L'indentation de la ligne devant laquelle on insere.
std::string indentOf(std::string_view line) {
    std::size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return std::string(line.substr(0, n));
}

} // namespace

// ---------------------------------------------------------- une destination --
std::string formatTarget(const Target& t) {
    if (t.kind == TargetKind::None) return {};
    std::string out(kindTag(t.kind));
    out += ':';
    out += t.entry;
    if (!t.subject.empty()) { out += '#'; out += t.subject; }
    return out;
}

Target parseTarget(std::string_view s) {
    Target t;
    const std::size_t colon = s.find(':');
    if (colon == std::string_view::npos) return t;
    t.kind = kindFromTag(s.substr(0, colon));
    if (t.kind == TargetKind::None) return t;

    std::string_view rest = s.substr(colon + 1);
    const std::size_t hash = rest.find('#');
    if (hash == std::string_view::npos) {
        t.entry = trimmed(rest);
    } else {
        t.entry   = trimmed(rest.substr(0, hash));
        t.subject = trimmed(rest.substr(hash + 1));
    }
    // Une ligne tronquee ne doit pas produire une destination vide qui
    // s'empilerait dans les favoris sans rien ouvrir.
    if (t.entry.empty()) t.kind = TargetKind::None;
    return t;
}

std::string labelOf(const Target& t) {
    switch (t.kind) {
        case TargetKind::Parameter:
            return t.entry.empty() ? t.subject : t.entry + " . " + t.subject;
        case TargetKind::Diagnostic: {
            if (const Code* c = find(t.entry); c != nullptr)
                return t.entry + "  " + std::string(c->title);
            return t.entry;
        }
        case TargetKind::None: return {};
        default:               return t.entry;
    }
}

bool sameTarget(const Target& a, const Target& b) noexcept {
    return a.kind == b.kind && a.entry == b.entry && a.subject == b.subject;
}

// ------------------------------------------------------------ la navigation --
void Navigation::go(const Target& t) {
    if (t.kind == TargetKind::None) return;
    if (!history_.empty() && sameTarget(history_[index_], t)) return;

    // Bifurcation : ce qui etait devant n'a plus de sens.
    if (!history_.empty()) history_.resize(index_ + 1);
    history_.push_back(t);
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin());
    }
    index_ = history_.size() - 1;

    erase(recents_, t);
    recents_.insert(recents_.begin(), t);
    if (recents_.size() > kMaxRecents) recents_.resize(kMaxRecents);
}

Target Navigation::back() {
    if (!canBack()) return {};
    --index_;
    return history_[index_];
}

Target Navigation::forward() {
    if (!canForward()) return {};
    ++index_;
    return history_[index_];
}

const Target& Navigation::current() const noexcept {
    if (history_.empty()) return none_;
    return history_[index_];
}

void Navigation::clear() {
    history_.clear();
    index_ = 0;
}

bool Navigation::isFavourite(const Target& t) const {
    return std::any_of(favourites_.begin(), favourites_.end(),
                       [&](const Target& x) { return sameTarget(x, t); });
}

bool Navigation::toggleFavourite(const Target& t) {
    if (t.kind == TargetKind::None) return false;
    if (isFavourite(t)) { erase(favourites_, t); return false; }
    favourites_.push_back(t);
    return true;
}

void Navigation::removeFavourite(const Target& t) { erase(favourites_, t); }

std::vector<std::string> Navigation::saveRecents() const {
    std::vector<std::string> out;
    out.reserve(recents_.size());
    for (const auto& t : recents_) out.push_back(formatTarget(t));
    return out;
}

std::vector<std::string> Navigation::saveFavourites() const {
    std::vector<std::string> out;
    out.reserve(favourites_.size());
    for (const auto& t : favourites_) out.push_back(formatTarget(t));
    return out;
}

void Navigation::restore(const std::vector<std::string>& recents,
                         const std::vector<std::string>& favourites) {
    recents_.clear();
    for (const auto& s : recents) {
        const Target t = parseTarget(s);
        if (t.kind != TargetKind::None && !isFavourite(t)) recents_.push_back(t);
        if (recents_.size() >= kMaxRecents) break;
    }
    favourites_.clear();
    for (const auto& s : favourites) {
        const Target t = parseTarget(s);
        // Un favori en double - deux versions du fichier de reglages fusionnees
        // a la main - ferait deux lignes identiques dans le menu.
        if (t.kind != TargetKind::None && !isFavourite(t)) favourites_.push_back(t);
    }
}

std::vector<Target> Navigation::stale(
    const std::vector<project::CatalogEntry>& library) const {
    const auto exists = [&](const Target& t) {
        switch (t.kind) {
            case TargetKind::Diagnostic: return find(t.entry) != nullptr;
            case TargetKind::Glossary:   return glossaryTerm(t.entry) != nullptr;
            case TargetKind::LibraryEntry:
            case TargetKind::Parameter: {
                const auto it = std::find_if(
                    library.begin(), library.end(),
                    [&](const project::CatalogEntry& e) { return e.name == t.entry; });
                if (it == library.end()) return false;
                if (t.kind == TargetKind::LibraryEntry) return true;
                // Un parametre peut avoir disparu alors que le bloc est la :
                // c'est le cas le plus frequent, et le plus discret.
                return it->declaration(t.subject) != nullptr;
            }
            // 1.11 : un sujet du centre se verifie contre son index
            // (help::center::Index::find), que la bibliotheque ne connait pas.
            case TargetKind::Topic: return true;
            default: return false;
        }
    };

    std::vector<Target> out;
    for (const auto& t : favourites_) if (!exists(t)) out.push_back(t);
    for (const auto& t : recents_)    if (!exists(t)) out.push_back(t);
    return out;
}

// --------------------------------------------- ndeg13 : reprendre l'exemple ----
bool hasExample(const project::CatalogEntry& e) noexcept {
    return !trimmed(e.help.example).empty();
}

std::string copyText(const project::CatalogEntry& e) {
    if (!hasExample(e)) return {};
    return e.help.example;
}

std::string bodyWithExample(std::string_view body, const project::CatalogEntry& e,
                            std::size_t atLine) {
    if (!hasExample(e)) return std::string(body);

    std::vector<std::string> lines = linesOf(body);
    // linesOf rend toujours au moins une ligne ; un corps vide en rend une,
    // vide, et inserer avant elle est ce qu'on veut.
    if (atLine > lines.size()) atLine = lines.size();

    const std::string indent =
        atLine < lines.size() ? indentOf(lines[atLine])
                              : (lines.empty() ? std::string{} : indentOf(lines.back()));

    std::vector<std::string> block;
    {
        std::string header = indent + "(* " + e.name;
        if (!e.version.empty()) header += " v" + e.version;
        header += " - exemple de l'aide *)";
        block.push_back(header);
    }
    for (const auto& l : linesOf(e.help.example)) {
        if (l.empty()) block.push_back({});
        else           block.push_back(indent + l);
    }
    // Une ligne vide derriere, sauf si on colle deja contre une ligne vide :
    // l'exemple doit respirer, pas creuser un trou.
    if (atLine >= lines.size() || !trimmed(lines[atLine]).empty()) block.push_back({});

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(atLine),
                 block.begin(), block.end());

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out += '\n';
    }
    return out;
}

// ------------------------------------------ ndeg17 : ouvrir le fichier source --
SourceLocation locate(const project::CatalogEntry& e, std::string_view subject) {
    SourceLocation loc;
    loc.path = e.path;
    if (e.path.empty()) return loc;
    loc.found = true;
    if (subject.empty()) return loc;

    std::ifstream f(e.path, std::ios::binary);
    if (!f) return loc;
    std::ostringstream all;
    all << f.rdbuf();
    const auto lines = linesOf(all.str());

    // La ligne de declaration d'abord - c'est la que se corrige un type - et a
    // defaut la ligne d'aide qui documente le sujet.
    std::size_t helpLine = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trimmed(lines[i]);
        if (t.empty()) continue;

        if (t.rfind("#!", 0) == 0) {
            if (helpLine == 0 && t.find(subject) != std::string::npos)
                helpLine = i + 1;
            continue;
        }
        if (t[0] == '#') continue;
        if (t.rfind("<<<", 0) == 0) break;

        // " Fbk ; EBOOL ; Member ; ... " : le nom est avant le premier
        // point-virgule, et la comparaison porte sur lui seul - chercher
        // `subject` n'importe ou dans la ligne ferait repondre `Val` pour
        // `ValMax`.
        const std::size_t semi = t.find(';');
        if (semi == std::string::npos) continue;
        if (trimmed(std::string_view(t).substr(0, semi)) == subject) {
            loc.line = i + 1;
            return loc;
        }
    }
    if (helpLine != 0) loc.line = helpLine;
    return loc;
}

std::string openInEditor(const SourceLocation& loc) {
    if (loc.path.empty()) return "cette entree n'a pas de fichier sur le disque";

    // Un chemin est une donnee, pas une commande : on le met entre guillemets,
    // et on refuse ce qui pourrait fermer les guillemets. Un nom de fichier
    // venu d'un scan de dossier n'a aucune raison d'en contenir.
    if (loc.path.find('"') != std::string::npos)
        return "chemin refuse : " + loc.path;

    std::string command;
#ifdef _WIN32
    command = "start \"\" \"" + loc.path + "\"";
#elif defined(__APPLE__)
    command = "open \"" + loc.path + "\"";
#else
    command = "xdg-open \"" + loc.path + "\" >/dev/null 2>&1 &";
#endif
    const int rc = std::system(command.c_str());
    if (rc != 0) return "aucune application n'a pu ouvrir " + loc.path;
    return {};
}

// ------------------------------------- la boite aux lettres de F1 ------------
namespace {
Target& mailbox() {
    static Target pending;
    return pending;
}
} // namespace

void setPendingTarget(const Target& t) { mailbox() = t; }

Target takePendingTarget() {
    Target t = mailbox();
    mailbox() = Target{};      // vidangee : une destination ne sert qu'une fois
    return t;
}

bool hasPendingTarget() noexcept { return mailbox().kind != TargetKind::None; }

namespace {
std::string& launchBox() {
    static std::string box;
    return box;
}
} // namespace

void setPendingMacroLaunch(std::string name) { launchBox() = std::move(name); }

std::string takePendingMacroLaunch() {
    std::string name = std::move(launchBox());
    launchBox().clear();
    return name;
}

// --------------------------------------------- ndeg20 : la visite guidee -------
const std::vector<TourStep>& tour() {
    // Cinq etapes, et cinq seulement. Une visite de douze ecrans se saute au
    // troisieme, et le lecteur perd aussi les deux premiers.
    static const std::vector<TourStep> steps = {
        {"tree",
         "Tout est range comme dans libs/",
         "L'arbre a gauche est le dossier de la bibliotheque : une branche par "
         "categorie, une feuille par fichier. Ce que vous lisez ici est ecrit "
         "dans le .ddt ou le .dfb lui-meme, jamais a cote."},
        {"search",
         "Cherchez n'importe quoi",
         "Un nom de bloc, un parametre, un mot d'une phrase, un code XPG-nnnn ou "
         "un terme du glossaire. Les resultats sont classes : un nom exact passe "
         "devant une phrase qui contient le mot."},
        {"article",
         "La page dit aussi qui vous appelle",
         "Sous les parametres, \xC2\xAB Utilise par \xC2\xBB est calcule : les blocs qui "
         "prennent ce type en parametre apparaissent sans que personne ait eu a "
         "l'ecrire. C'est ce qui repond a \xC2\xAB qu'est-ce que je casse si je change "
         "ca \xC2\xBB."},
        {"try",
         "Essayer fait tourner l'exemple",
         "Le bouton monte un projet de poche autour de l'exemple et le fait "
         "tourner dans le vrai simulateur : la valeur de chaque variable "
         "s'affiche a cote de chaque ligne. Forcez une entree, relancez. Rien de "
         "tout cela n'atteint votre projet."},
        {"f1",
         "F1 depuis n'importe ou",
         "Le curseur sur un nom dans une section, sur une ligne de diagnostic ou "
         "sur un noeud de l'arbre : F1 ouvre la bonne page directement. "
         "Precedent et Suivant fonctionnent comme dans un navigateur, et "
         "l'etoile garde une page pour demain."},
    };
    return steps;
}

} // namespace help
