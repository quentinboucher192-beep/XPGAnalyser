#include "LibraryCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace project {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' || s.back()  == '\r')) s.remove_suffix(1);
    return s;
}

bool sameKey(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

// La valeur d'une ligne d'en-tete "cle = valeur", ou rien si ce n'en est pas une.
bool headerValue(std::string_view line, std::string_view key, std::string& out) {
    const auto t = trim(line);
    if (t.size() <= key.size() || !sameKey(t.substr(0, key.size()), key)) return false;
    const auto eq = t.find('=');
    if (eq == std::string_view::npos) return false;
    // "name" doit etre le mot entier : "namespace = x" n'est pas un nom.
    if (trim(t.substr(0, eq)).size() != key.size()) return false;
    out = std::string(trim(t.substr(eq + 1)));
    return true;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

CatalogKind kindFromExtension(std::string_view fileName) {
    const auto dot = fileName.rfind('.');
    if (dot == std::string_view::npos) return CatalogKind::Other;
    const auto ext = fileName.substr(dot);
    if (sameKey(ext, ".ddt")) return CatalogKind::DerivedType;
    if (sameKey(ext, ".dfb")) return CatalogKind::FunctionBlock;
    if (sameKey(ext, ".mac")) return CatalogKind::Macro;
    return CatalogKind::Other;
}

} // namespace

std::string_view kindLabel(CatalogKind k) noexcept {
    switch (k) {
        case CatalogKind::DerivedType:   return "DDT";
        case CatalogKind::FunctionBlock: return "DFB";
        case CatalogKind::Macro:         return "MACRO";
        case CatalogKind::Other:         break;
    }
    return "?";
}

bool Declaration::isLocal() const noexcept { return sameKey(scope, "Local"); }

std::size_t CatalogEntry::documentableCount() const {
    std::size_t n = 0;
    for (const auto& d : declarations)
        if (!d.isLocal() && !d.name.empty() && !isInternal(d.name)) ++n;
    return n;
}

std::size_t CatalogEntry::documentedCount() const {
    std::size_t n = 0;
    for (const auto& d : declarations) {
        if (d.isLocal() || d.name.empty() || isInternal(d.name)) continue;
        const auto* t = paramHelp(d.name);
        if (t && !t->empty()) ++n;
    }
    return n;
}

const std::string* CatalogEntry::paramHelp(std::string_view wanted, bool* fromCommon) const {
    if (fromCommon) *fromCommon = false;
    if (const auto* own = help.param(wanted); own && !own->empty()) return own;
    for (const auto& h : inherited)
        if (sameKey(h.key, wanted) && !h.text.empty()) {
            if (fromCommon) *fromCommon = true;
            return &h.text;
        }
    return nullptr;
}

namespace {

// Le texte d'une cle inconnue (" internal ", " expect "...), ou vide.
const std::string* unknownText(const LibraryHelp& h, std::string_view key) {
    for (const auto& e : h.unknown)
        if (sameKey(e.key, key)) return &e.text;
    return nullptr;
}

std::vector<std::string> linesOf(const std::string* text) {
    std::vector<std::string> out;
    if (!text) return out;
    for (auto l : splitLines(*text)) {
        const auto t = trim(l);
        if (!t.empty()) out.emplace_back(t);
    }
    return out;
}

// "1.02" -> {1, 2}. Une version qui ne se lit pas se range en tete.
std::vector<int> versionParts(std::string_view v) {
    std::vector<int> out;
    int cur = 0;
    bool any = false;
    for (const char c : v) {
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
        else if (c == '.') { out.push_back(cur); cur = 0; any = false; }
        else break;
    }
    if (any) out.push_back(cur);
    return out;
}

} // namespace

bool CatalogEntry::isInternal(std::string_view wanted) const {
    for (const auto& n : internals())
        if (sameKey(n, wanted)) return true;
    return false;
}

std::vector<std::string> CatalogEntry::internals() const {
    std::vector<std::string> out;
    const auto* text = unknownText(help, "internal");
    if (!text) return out;
    std::string cur;
    for (const char c : *text) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<HelpEntry> CatalogEntry::changes() const {
    std::vector<HelpEntry> out;
    for (const auto& e : help.unknown) {
        if (e.key.size() <= 8 || !sameKey(std::string_view(e.key).substr(0, 8), "changes ")) continue;
        out.push_back({std::string(trim(std::string_view(e.key).substr(8))), e.text});
    }
    std::stable_sort(out.begin(), out.end(), [](const HelpEntry& a, const HelpEntry& b) {
        return versionParts(a.key) > versionParts(b.key);
    });
    return out;
}

std::vector<std::string> CatalogEntry::givens() const {
    return linesOf(unknownText(help, "given"));
}

std::vector<std::string> CatalogEntry::expectations() const {
    return linesOf(unknownText(help, "expect"));
}

std::vector<std::string> CatalogEntry::orphanHelpParams() const {
    std::vector<std::string> out;
    for (const auto& h : help.params)
        if (!declaration(h.key)) out.push_back(h.key);
    return out;
}

const Declaration* CatalogEntry::declaration(std::string_view wanted) const {
    for (const auto& d : declarations)
        if (sameKey(d.name, wanted)) return &d;
    return nullptr;
}

// ------------------------------------------------------------- les macros ---
namespace {

// Le bloc d'en-tete : le premier "(*" du fichier et son "*)". Non imbrique,
// comme dans l'interpreteur - le premier "*)" ferme, et c'est cette regle-la
// qu'il faut suivre plutot qu'une plus maligne.
struct HeaderBlock {
    bool        found{false};
    std::size_t open{0};      // index du "("
    std::size_t close{0};     // index du "*" de "*)"
};

HeaderBlock headerBlock(std::string_view s) {
    HeaderBlock b;
    const auto open = s.find("(*");
    if (open == std::string_view::npos) return b;
    const auto close = s.find("*)", open + 2);
    if (close == std::string_view::npos) return b;
    b.found = true;
    b.open  = open;
    b.close = close;
    return b;
}

// Une chaine litterale ST : 'texte'. Rend sa valeur et avance apres la
// fermeture. Les doublements d'apostrophe ne sont pas traites : aucun des
// libelles de questions n'en contient, et en inventer un traitement non
// verifie serait pire que de ne pas en avoir.
bool literalAt(std::string_view s, std::size_t& i, std::string& out) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size() || s[i] != '\'') return false;
    const auto start = ++i;
    while (i < s.size() && s[i] != '\'') ++i;
    if (i >= s.size()) return false;
    out = std::string(s.substr(start, i - start));
    ++i;
    return true;
}

void skipComma(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i < s.size() && s[i] == ',') ++i;
}

} // namespace

std::vector<Declaration> macroQuestions(std::string_view s) {
    std::vector<Declaration> out;
    struct Form { const char* call; const char* type; };
    // LA PARENTHESE FAIT PARTIE DU MOTIF, et c'est elle qui desambiguise :
    // "AskNumber(" ne commence pas par "Ask(" mais par "AskN". Une passe de
    // mutations l'a montre - inverser l'ordre des formes ne change rien, alors
    // qu'un commentaire ecrit ici affirmait le contraire. Retirer la
    // parenthese, en revanche, ferait lire toute question numerique comme une
    // question texte.
    const Form formes[] = {
        {"AskChoice(", "choix"},
        {"AskNumber(", "nombre"},
        {"Ask(",       "texte"},
    };

    for (std::size_t i = 0; i < s.size(); ++i) {
        // Le bloc d'en-tete decrit la macro, il ne l'execute pas : un "Ask("
        // cite dans la prose n'est pas une question posee.
        if (s.compare(i, 2, "(*") == 0) {
            const auto end = s.find("*)", i + 2);
            i = (end == std::string_view::npos) ? s.size() : end + 1;
            continue;
        }
        for (const auto& f : formes) {
            const auto n = std::strlen(f.call);
            if (s.compare(i, n, f.call) != 0) continue;
            // "AskNumber(" commence par "Ask" : on ne doit pas le lire deux
            // fois. Les formes sont essayees de la plus longue a la plus
            // courte, et on saute apres la parenthese des qu'une colle.
            std::size_t p = i + n;
            Declaration d;
            d.type  = f.type;
            // Pas de portee : une question n'en a pas, et repeter "Question"
            // sur chaque ligne d'une liste de questions n'apprend rien.
            d.scope.clear();
            std::string key, prompt;
            if (!literalAt(s, p, key)) break;
            skipComma(s, p);
            if (!literalAt(s, p, prompt)) break;
            d.name    = key;
            d.comment = prompt;

            // La valeur proposee : le DERNIER litteral de l'appel.
            //
            // On ne peut pas chercher la parenthese fermante avec find(')') :
            // une liste de choix contient des parentheses, et
            // 'Section de tache (globale),Dans une unite' faisait terminer
            // l'appel au milieu de la chaine. Il faut traverser en sautant les
            // litteraux, ce qui est la seule facon de savoir si un ')' est du
            // code ou du texte.
            {
                std::size_t q = p;
                int profondeur = 1;
                std::string dernier;
                while (q < s.size() && profondeur > 0) {
                    if (s[q] == '\'') {
                        std::string lit;
                        if (!literalAt(s, q, lit)) break;
                        dernier = std::move(lit);
                        continue;
                    }
                    if (s[q] == '(') ++profondeur;
                    else if (s[q] == ')') --profondeur;
                    ++q;
                }
                if (profondeur == 0) d.initial = std::move(dernier);
            }

            const bool deja = std::any_of(out.begin(), out.end(),
                [&](const Declaration& x) { return sameKey(x.name, d.name); });
            if (!deja && !d.name.empty()) out.push_back(std::move(d));
            i = p;
            break;
        }
    }
    return out;
}

namespace {

CatalogEntry parseMacroFile(std::string_view contents, std::string_view fileName) {
    CatalogEntry e;
    e.fileName = std::string(fileName);
    e.kind     = CatalogKind::Macro;

    const auto b = headerBlock(contents);
    if (b.found) {
        // Les lignes "#!" de l'en-tete forment l'aide. On les donne au MEME
        // lecteur que les .ddt : deux analyseurs pour un seul format finissent
        // toujours par ne plus lire la meme chose.
        auto bloc = contents.substr(b.open + 2, b.close - b.open - 2);
        std::string aide;
        for (auto raw : splitLines(bloc)) {
            const auto t = trim(raw);
            if (t.rfind("#!", 0) == 0) { aide += std::string(t); aide += '\n'; }
        }
        if (!aide.empty()) {
            auto parsed = readHelp(aide);
            e.help     = std::move(parsed.help);
            e.hasHelp  = parsed.hadHelp;
            e.warnings = std::move(parsed.warnings);
            // LA VERSION D'UNE MACRO vit dans son aide (" #! version = 1.10 ") :
            // une macro n'a pas d'en-tete " version = " comme un .ddt, et sans
            // cela seule l'index la connaissait - si bien qu'elle divergeait du
            // fichier au premier oubli.
            if (const auto* v = unknownText(e.help, "version")) e.version = std::string(trim(*v));
        }
        // Le nom : le premier mot de la ligne d'ouverture.
        const auto premiereFin = contents.find('\n', b.open);
        auto premiere = trim(contents.substr(b.open + 2,
            (premiereFin == std::string_view::npos ? contents.size() : premiereFin) - b.open - 2));
        std::size_t k = 0;
        while (k < premiere.size() && !std::isspace(static_cast<unsigned char>(premiere[k]))) ++k;
        e.name = std::string(premiere.substr(0, k));
    }

    e.declarations = macroQuestions(contents);

    if (e.name.empty() && !fileName.empty()) {
        const auto dot = e.fileName.rfind('.');
        e.name = dot == std::string::npos ? e.fileName : e.fileName.substr(0, dot);
    }
    return e;
}

} // namespace

std::string writeMacroHelp(std::string_view contents, const LibraryHelp& help,
                           std::string* error) {
    const auto rendu = renderHelpBlock(help);
    if (rendu.find("*)") != std::string::npos) {
        if (error) *error = "un texte d'aide contient \"*)\", qui fermerait le "
                            "commentaire et transformerait la suite en code";
        return std::string(contents);
    }

    const std::string eol = contents.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    auto b = headerBlock(contents);

    if (!b.found) {
        // Pas d'en-tete : on en pose un. Une macro sans bloc de tete est rare
        // mais pas fautive, et refuser de la documenter pour autant serait une
        // limite arbitraire.
        if (rendu.empty()) return std::string(contents);
        std::string out = "(*" + eol;
        for (auto l : splitLines(rendu)) { out += std::string(l); out += eol; }
        out += "*)" + eol + eol;
        out += std::string(contents);
        return out;
    }

    // La prose : tout ce qui est dans le bloc sauf les lignes "#!" deja la.
    const auto interieur = contents.substr(b.open + 2, b.close - b.open - 2);
    std::vector<std::string_view> prose;
    for (auto raw : splitLines(interieur))
        if (trim(raw).rfind("#!", 0) != 0) prose.push_back(raw);
    // Les lignes vides de fin de prose disparaissent : sans ca, chaque
    // enregistrement ajouterait la sienne.
    while (!prose.empty() && trim(prose.back()).empty()) prose.pop_back();

    // LE "*)" REVIENT SUR SA PROPRE LIGNE, et il faut le dire parce que c'est
    // la seule ligne du fichier que cette fonction reformate. Beaucoup
    // d'en-tetes finissent par "... pas essayer. *)" : le fermant est colle au
    // dernier mot de la prose. Glisser l'aide entre les deux oblige a le
    // deplacer. Ce qui reste garanti, et qui est ce qui compte : le corps de
    // la macro, apres le bloc, revient octet pour octet, et la prose garde ses
    // mots. Le blanc laisse par le fermant est rogne, sinon la ligne finirait
    // par une espace de plus a chaque enregistrement.

    std::string out(contents.substr(0, b.open + 2));
    for (std::size_t i = 0; i < prose.size(); ++i) {
        auto ligne = prose[i];
        if (i + 1 == prose.size())
            while (!ligne.empty() && (ligne.back() == ' ' || ligne.back() == '\t'
                                      || ligne.back() == '\r'))
                ligne.remove_suffix(1);
        out += std::string(ligne);
        if (i + 1 < prose.size() || !rendu.empty()) out += eol;
    }
    if (!rendu.empty()) {
        out += eol;
        for (auto l : splitLines(rendu)) { out += "   " + std::string(l); out += eol; }
    }
    out += std::string(contents.substr(b.close));
    return out;
}

// ---------------------------------------------------------------------------
CatalogEntry parseLibraryFile(std::string_view contents, std::string_view fileName) {
    if (kindFromExtension(fileName) == CatalogKind::Macro)
        return parseMacroFile(contents, fileName);

    CatalogEntry e;
    e.fileName = std::string(fileName);
    e.kind     = kindFromExtension(fileName);

    // L'aide passe par le lecteur qui l'a ecrite. Une deuxieme implementation
    // ici finirait par diverger de celle qui ecrit les fichiers, et c'est le
    // genre de divergence qu'on decouvre quand un bloc perd sa doc.
    auto parsed = readHelp(contents);
    e.help      = std::move(parsed.help);
    e.hasHelp   = parsed.hadHelp;
    e.name      = parsed.itemName;
    e.warnings  = std::move(parsed.warnings);

    bool inBody = false;                 // passe a vrai au premier "<<<"
    bool seenDeclaration = false;

    for (auto raw : splitLines(contents)) {
        const auto t = trim(raw);
        if (t.empty()) continue;

        if (t.rfind("<<<", 0) == 0) { inBody = true; continue; }
        if (inBody) continue;

        if (t[0] == '#') {
            // La table des codes de defaut, ecrite en commentaire au-dessus des
            // declarations : "# 1  le relais thermique a declenche". Elle n'est
            // lue qu'AVANT la premiere declaration, sinon un commentaire de
            // corps commencant par un chiffre s'y ajouterait.
            if (seenDeclaration) continue;
            if (t.size() > 1 && t[1] == '!') continue;     // c'est de l'aide
            auto body = trim(t.substr(1));
            std::size_t i = 0;
            while (i < body.size() && std::isdigit(static_cast<unsigned char>(body[i]))) ++i;
            if (i == 0 || i >= body.size()) continue;
            if (body[i] != ' ' && body[i] != '\t') continue;
            e.faultTable.push_back({std::string(body.substr(0, i)),
                                    std::string(trim(body.substr(i)))});
            continue;
        }

        if (t.find(';') == std::string_view::npos) {
            std::string v;
            if (headerValue(t, "name", v))    { if (e.name.empty()) e.name = v; continue; }
            if (headerValue(t, "version", v)) { e.version = v; continue; }
            continue;
        }

        // Une declaration : name ; type ; scope ; initial ; comment
        //
        // On ne coupe QUE QUATRE FOIS. Le commentaire est le dernier champ et
        // contient des points-virgules des qu'il est ecrit en francais ; le
        // couper en cinq morceaux perdrait tout ce qui suit le premier.
        std::string fields[5];
        std::size_t start = 0, field = 0;
        while (field < 4) {
            const auto semi = t.find(';', start);
            if (semi == std::string_view::npos) break;
            fields[field++] = std::string(trim(t.substr(start, semi - start)));
            start = semi + 1;
        }
        fields[field] = std::string(trim(t.substr(start)));

        Declaration d;
        d.name    = std::move(fields[0]);
        d.type    = std::move(fields[1]);
        d.scope   = std::move(fields[2]);
        d.initial = std::move(fields[3]);
        d.comment = std::move(fields[4]);
        if (d.name.empty()) continue;
        seenDeclaration = true;
        e.declarations.push_back(std::move(d));
    }

    if (e.name.empty() && !fileName.empty()) {
        const auto dot = e.fileName.rfind('.');
        e.name = dot == std::string::npos ? e.fileName : e.fileName.substr(0, dot);
    }
    return e;
}

// ---------------------------------------------------------------------------
namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, std::string_view data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    return static_cast<bool>(out);
}

} // namespace

std::vector<HelpEntry> commonHelpOf(const std::string& folder) {
    namespace fs = std::filesystem;
    std::string contents;
    if (!readFile((fs::path(folder) / "COMMUN.hlp").string(), contents)) return {};
    return readHelp(contents).help.params;
}

void applyCommonHelp(CatalogEntry& e, const std::vector<HelpEntry>& common) {
    e.inherited.clear();
    for (const auto& d : e.declarations) {
        if (d.name.empty()) continue;
        if (const auto* own = e.help.param(d.name); own && !own->empty()) continue;
        for (const auto& c : common)
            if (sameKey(c.key, d.name)) { e.inherited.push_back(c); break; }
    }
}

std::vector<CatalogEntry> scanLibrary(const std::string& root) {
    namespace fs = std::filesystem;
    std::vector<CatalogEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;

    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        // Lot macros 1 : les dossiers en "_" sont a l'application (la corbeille
        // des macros, leurs modeles) : ce qu'ils contiennent n'est pas publie.
        if (it->is_directory(ec)) {
            const auto nom = it->path().filename().string();
            if (!nom.empty() && nom.front() == '_') it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;

        const auto path = it->path();
        if (kindFromExtension(path.filename().string()) == CatalogKind::Other) continue;

        std::string contents;
        if (!readFile(path.string(), contents)) continue;

        auto e = parseLibraryFile(contents, path.filename().string());
        e.path = path.string();
        const auto parent = path.parent_path();
        e.category = (parent == fs::path(root)) ? std::string("(racine)")
                                                : parent.filename().string();
        out.push_back(std::move(e));
    }

    // L'aide commune, un fichier par dossier, lu une fois.
    {
        std::map<std::string, std::vector<HelpEntry>> parDossier;
        for (auto& e : out) {
            const auto dossier = fs::path(e.path).parent_path().string();
            auto it = parDossier.find(dossier);
            if (it == parDossier.end()) it = parDossier.emplace(dossier, commonHelpOf(dossier)).first;
            applyCommonHelp(e, it->second);
        }
    }

    std::sort(out.begin(), out.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
        if (a.category != b.category) return a.category < b.category;
        // Les DDT avant les DFB dans une categorie : on lit la structure avant
        // le bloc qui la traite, et c'est l'ordre dans lequel on les ecrit.
        if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.name < b.name;
    });
    return out;
}

bool reloadEntry(CatalogEntry& e) {
    if (e.path.empty()) return false;
    std::string contents;
    if (!readFile(e.path, contents)) return false;
    const auto category = e.category;
    const auto path     = e.path;
    e = parseLibraryFile(contents, e.fileName);
    e.category = category;
    e.path     = path;
    applyCommonHelp(e, commonHelpOf(std::filesystem::path(path).parent_path().string()));
    return true;
}

SaveOutcome saveHelp(CatalogEntry& e, const LibraryHelp& help) {
    if (e.path.empty()) return {false, "cette entree n'a pas de fichier"};

    std::string before;
    if (!readFile(e.path, before)) return {false, "lecture impossible : " + e.path};

    std::string erreur;
    const auto after = e.kind == CatalogKind::Macro
                     ? writeMacroHelp(before, help, &erreur)
                     : writeHelp(before, help);
    if (!erreur.empty()) return {false, erreur};
    if (after == before) {
        e.help    = help;
        e.hasHelp = !help.empty();
        applyCommonHelp(e, commonHelpOf(std::filesystem::path(e.path).parent_path().string()));
        return {true, "aucun changement"};
    }
    if (!writeFile(e.path, after)) return {false, "ecriture impossible : " + e.path};

    // On relit ce qui est REELLEMENT sur le disque. Comparer a ce qu'on voulait
    // ecrire est le seul moyen de distinguer "enregistre" de "cru enregistre".
    std::string check;
    if (!readFile(e.path, check)) return {false, "relecture impossible apres ecriture"};
    if (check != after) return {false, "le fichier relu ne correspond pas a ce qui a ete ecrit"};

    const auto category = e.category;
    const auto path     = e.path;
    e = parseLibraryFile(check, e.fileName);
    e.category = category;
    e.path     = path;
    applyCommonHelp(e, commonHelpOf(std::filesystem::path(path).parent_path().string()));
    return {true, "enregistre"};
}

} // namespace project
