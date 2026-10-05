// =============================================================================
//  help/HelpTryIt.cpp
// =============================================================================
#include "HelpTryIt.hpp"

#include "../sim/Interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace help {
namespace {

using namespace domain;

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool identChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Les mots de la langue, et ceux qui ne sont jamais une variable.
bool isKeyword(std::string_view word) {
    static const std::set<std::string> kWords = {
        "IF","THEN","ELSE","ELSIF","END_IF","FOR","TO","BY","DO","END_FOR",
        "WHILE","END_WHILE","REPEAT","UNTIL","END_REPEAT","CASE","OF","END_CASE",
        "TRUE","FALSE","AND","OR","NOT","XOR","MOD","RETURN","EXIT","VAR","END_VAR",
    };
    return kWords.count(upper(word)) != 0;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// ---- le type, tel qu'il est ecrit dans un .ddt ------------------------------
// "ARRAY[0..15] OF ST_IO_Dig" ou "DINT". On ne resout rien ici : on decoupe.
struct TypeSpec {
    std::string element;                 // ST_IO_Dig, DINT
    bool         isArray{false};
    std::int64_t low{0}, high{-1};
};

TypeSpec parseTypeSpec(std::string_view text) {
    TypeSpec t;
    const auto s = trim(text);
    const auto up = upper(s);
    if (up.rfind("ARRAY", 0) == 0) {
        const auto lb = s.find('[');
        const auto dots = s.find("..", lb == std::string::npos ? 0 : lb);
        const auto rb = s.find(']', lb == std::string::npos ? 0 : lb);
        const auto of = up.find(" OF ", rb == std::string::npos ? 0 : rb);
        if (lb != std::string::npos && dots != std::string::npos
            && rb != std::string::npos && of != std::string::npos) {
            t.isArray = true;
            t.low  = std::atoll(trim(s.substr(lb + 1, dots - lb - 1)).c_str());
            t.high = std::atoll(trim(s.substr(dots + 2, rb - dots - 2)).c_str());
            t.element = trim(s.substr(of + 4));
            return t;
        }
    }
    t.element = s;
    return t;
}

void fillTypeRef(Project& p, TypeRef& ref, const TypeSpec& spec) {
    ref.name        = p.strings.intern(spec.isArray
                          ? "ARRAY[" + std::to_string(spec.low) + ".."
                            + std::to_string(spec.high) + "] OF " + spec.element
                          : spec.element);
    ref.elementType = p.strings.intern(spec.element);
    if (spec.isArray) {
        ref.klass    = TypeClass::Array;
        ref.arrayLow = spec.low;
        ref.arrayHigh = spec.high;
    }
}

const project::CatalogEntry* findEntry(const std::vector<project::CatalogEntry>& lib,
                                       std::string_view name) {
    for (const auto& e : lib)
        if (e.name == name) return &e;
    return nullptr;
}

Index findPou(const Project& p, std::string_view name) {
    for (Index i = 0; i < p.pous.size(); ++i)
        if (p.strings.text(p.pous[i].name) == name) return i;
    return kNoIndex;
}

bool hasDdt(const Project& p, std::string_view name) {
    for (const auto& d : p.derivedTypes)
        if (p.strings.text(d.name) == name) return true;
    return false;
}

// ---- monter un DDT, et ceux dont il depend ----------------------------------
void addDerivedType(Project& p, const project::CatalogEntry& e,
                    const std::vector<project::CatalogEntry>& lib,
                    std::set<std::string>& seen) {
    if (!seen.insert(e.name).second || hasDdt(p, e.name)) return;

    // Les types des membres AVANT le type lui-meme : un ARRAY OF ST_IO_Thr ne
    // se declare pas avant ST_IO_Thr, exactement comme dans l'import reel.
    for (const auto& d : e.declarations) {
        const auto spec = parseTypeSpec(d.type);
        if (const auto* dep = findEntry(lib, spec.element))
            if (dep->kind == project::CatalogKind::DerivedType)
                addDerivedType(p, *dep, lib, seen);
    }

    DerivedType ddt;
    ddt.name    = p.strings.intern(e.name);
    ddt.version = e.version;
    const auto ddtIndex = static_cast<Index>(p.derivedTypes.size());
    p.derivedTypes.push_back(ddt);

    for (const auto& d : e.declarations) {
        if (d.name.empty()) continue;
        Variable v;
        v.name  = p.strings.intern(d.name);
        v.scope = VariableScope::DerivedMember;
        v.owner = ddtIndex;
        if (!d.initial.empty()) v.initValue = p.strings.intern(d.initial);
        fillTypeRef(p, v.type, parseTypeSpec(d.type));
        p.variables.push_back(v);
        p.derivedTypes[ddtIndex].fields.push_back(static_cast<Index>(p.variables.size() - 1));
    }
}

// ---- monter un DFB : son interface ET son corps -----------------------------
Index addFunctionBlock(Project& p, const project::CatalogEntry& e,
                       const std::vector<project::CatalogEntry>& lib,
                       std::set<std::string>& seen, std::vector<std::string>& notes) {
    if (const auto already = findPou(p, e.name); already != kNoIndex) return already;

    for (const auto& d : e.declarations) {
        const auto spec = parseTypeSpec(d.type);
        if (const auto* dep = findEntry(lib, spec.element)) {
            if (dep->kind == project::CatalogKind::DerivedType)
                addDerivedType(p, *dep, lib, seen);
            else if (dep->kind == project::CatalogKind::FunctionBlock && dep->name != e.name)
                addFunctionBlock(p, *dep, lib, seen, notes);
        }
    }

    Pou pou;
    pou.name    = p.strings.intern(e.name);
    pou.kind    = PouKind::FunctionBlockType;
    pou.version = e.version;
    const auto pouIndex = static_cast<Index>(p.pous.size());
    p.pous.push_back(pou);

    for (const auto& d : e.declarations) {
        if (d.name.empty()) continue;
        Variable v;
        v.name  = p.strings.intern(d.name);
        v.owner = pouIndex;
        if (!d.initial.empty()) v.initValue = p.strings.intern(d.initial);
        fillTypeRef(p, v.type, parseTypeSpec(d.type));

        const auto scope = upper(d.scope);
        const bool parametre = scope == "INPUT" || scope == "OUTPUT" || scope == "INOUT";
        v.scope = scope == "INPUT"  ? VariableScope::Input
                : scope == "OUTPUT" ? VariableScope::Output
                : scope == "INOUT"  ? VariableScope::InOut
                                    : VariableScope::Local;
        p.variables.push_back(v);
        const auto vi = static_cast<Index>(p.variables.size() - 1);
        if (parametre) p.pous[pouIndex].parameters.push_back(vi);
        else           p.pous[pouIndex].locals.push_back(vi);
    }

    // LE CORPS. Sans lui, le bloc stocke ses entrees et ne calcule rien - et
    // l'exemple tournerait en donnant des zeros, ce qui est pire que de ne pas
    // tourner du tout.
    //
    // TOUTES LES SECTIONS, dans l'ordre du fichier. Un DFB en a parfois
    // plusieurs - Init, Main, Debug pour le moteur GRAFCET - et n'en monter que
    // la premiere faisait tourner un bloc ampute sans le dire.
    const auto sections = e.path.empty() ? std::vector<std::pair<std::string, std::string>>{}
                                         : sectionsOfLibraryFile(slurp(e.path));
    if (sections.empty()) {
        notes.push_back(e.name + " : corps introuvable, le bloc ne calculera rien "
                        "(voir XPG-2102)");
    }
    for (const auto& [nom, corps] : sections) {
        Section s;
        s.name     = p.strings.intern(nom.empty() ? std::string("Process") : nom);
        s.language = PouLanguage::ST;
        s.body     = corps;
        s.owner    = pouIndex;
        p.sections.push_back(s);
        p.pous[pouIndex].sections.push_back(static_cast<Index>(p.sections.size() - 1));
    }
    return pouIndex;
}

// ---- deduire ce que l'exemple utilise ---------------------------------------
struct Use {
    std::string name;
    std::string type;                 // deduit d'un parametre nomme, ou vide
    std::set<std::string> members;    // les .X vus sur cette base
    bool         indexed{false};      // un [ ... ] la suit
    std::int64_t highest{0};          // le plus grand indice rencontre
};

// Le DDT de la bibliotheque qui porte TOUS ces membres. Le plus petit gagne :
// ST_IO_Dig et ST_IO_Ana ont tous deux un .Val, et le plus specifique est
// celui qui n'a que ce qu'il faut. Rend une chaine vide quand plusieurs
// conviennent aussi bien - deviner au hasard serait pire que declarer en BOOL
// et le dire.
std::string ddtWithMembers(const std::vector<project::CatalogEntry>& lib,
                           const std::set<std::string>& members) {
    if (members.empty()) return {};
    // UNE SOUS-STRUCTURE N'EST PAS UN CANDIDAT. ST_IO_Thr a un `.Val` et il est
    // plus petit que ST_IO_Dig, donc « le plus petit gagne » choisissait le
    // seuil analogique pour une voie tout ou rien. Un type qui n'apparait que
    // comme MEMBRE d'un autre ne se declare pas tout seul dans un exemple.
    // Seulement les membres d'un AUTRE DDT : un type passe en parametre d'un
    // bloc reste un type qu'on declare (ST_IO_Dig est l'InOut de DFB_IO_DIG16,
    // et c'est pourtant lui qu'un exemple declare).
    std::set<std::string> sousStructures;
    for (const auto& e : lib) {
        if (e.kind != project::CatalogKind::DerivedType) continue;
        for (const auto& d : e.declarations)
            sousStructures.insert(parseTypeSpec(d.type).element);
    }

    std::string best;
    std::size_t bestSize = 0;
    int exaequo = 0;
    for (const auto& e : lib) {
        if (e.kind != project::CatalogKind::DerivedType) continue;
        if (sousStructures.count(e.name) != 0) continue;
        bool tous = true;
        for (const auto& m : members)
            if (e.declaration(m) == nullptr) { tous = false; break; }
        if (!tous) continue;
        const auto taille = e.declarations.size();
        if (best.empty() || taille < bestSize) { best = e.name; bestSize = taille; exaequo = 1; }
        else if (taille == bestSize) ++exaequo;
    }
    return exaequo == 1 ? best : std::string{};
}

// « (* declarer : Clignote : BOOL; Cur : INT := -1; *) » dans un exemple : des
// declarations EXPLICITES. Elles l'emportent sur ce que l'inference devine, et
// elles portent une valeur initiale. L'inference reste le cas courant ; ceci
// est pour ce qu'elle ne peut pas savoir - une variable nue dans la page d'un
// DDT, qu'elle prendrait pour une instance de ce DDT.
struct Declared {
    std::string name;
    std::string type;
    std::string initial;
};

std::vector<Declared> declaredInExample(std::string_view example) {
    std::vector<Declared> out;
    std::size_t at = 0;
    while ((at = example.find("(*", at)) != std::string_view::npos) {
        const auto end = example.find("*)", at + 2);
        const auto stop = end == std::string_view::npos ? example.size() : end;
        const auto inner = trim(example.substr(at + 2, stop - at - 2));
        at = end == std::string_view::npos ? example.size() : end + 2;
        if (upper(inner).rfind("DECLARE", 0) != 0) continue;
        // Le mot « declarer » (ou « declare »), puis un deux-points facultatif.
        std::size_t k = 0;
        while (k < inner.size() && std::isalpha(static_cast<unsigned char>(inner[k]))) ++k;
        auto rest = trim(std::string_view(inner).substr(k));
        if (!rest.empty() && rest.front() == ':') rest = trim(std::string_view(rest).substr(1));
        std::size_t from = 0;
        while (from < rest.size()) {
            auto semi = rest.find_first_of(";\n", from);
            if (semi == std::string::npos) semi = rest.size();
            const auto item = trim(std::string_view(rest).substr(from, semi - from));
            from = semi + 1;
            const auto colon = item.find(':');
            if (item.empty() || colon == std::string::npos) continue;
            Declared d;
            d.name = trim(std::string_view(item).substr(0, colon));
            auto typeText = trim(std::string_view(item).substr(colon + 1));
            if (const auto assign = typeText.find(":="); assign != std::string::npos) {
                d.initial = trim(std::string_view(typeText).substr(assign + 2));
                typeText  = trim(std::string_view(typeText).substr(0, assign));
            }
            d.type = typeText;
            if (d.name.empty() || d.type.empty()) continue;
            if (!std::all_of(d.name.begin(), d.name.end(), identChar)) continue;
            out.push_back(std::move(d));
        }
    }
    return out;
}

// Un decoupage lexical, pas un analyseur : on cherche les appels a parametres
// nommes, et on en tire le type de chaque argument.
std::vector<Use> inferUses(std::string_view example, const project::CatalogEntry& entry,
                           const std::vector<project::CatalogEntry>& lib,
                           const std::vector<Declared>& explicites) {
    // LES COMMENTAIRES SONT RETIRES AVANT DE CHERCHER. Un exemple commence
    // souvent par « (* declarer : Pompes : ARRAY[0..15] OF ST_EQ_Pump; *) », et
    // sans ca on declarait des variables nommees `declarer`, `ARRAY` et
    // `ST_EQ_Pump`. Le texte affiche, lui, garde ses commentaires.
    std::string sansCommentaires(example);
    for (std::size_t i = 0; i + 1 < sansCommentaires.size(); ++i) {
        if (sansCommentaires[i] != '(' || sansCommentaires[i + 1] != '*') continue;
        const auto fin = sansCommentaires.find("*)", i + 2);
        const auto stop = fin == std::string::npos ? sansCommentaires.size() : fin + 2;
        for (std::size_t j = i; j < stop; ++j)
            if (sansCommentaires[j] != '\n') sansCommentaires[j] = ' ';
        i = stop - 1;
    }
    example = sansCommentaires;

    std::vector<Use> uses;
    const auto note = [&uses](const std::string& name, const std::string& type) -> Use& {
        for (auto& u : uses) {
            if (u.name != name) continue;
            if (u.type.empty()) u.type = type;
            return u;
        }
        uses.push_back(Use{name, type, {}, false, 0});
        return uses.back();
    };

    // Les identifiants, avec leur position.
    struct Tok { std::string text; std::size_t at; };
    std::vector<Tok> idents;
    for (std::size_t i = 0; i < example.size();) {
        if (example[i] == '\'') {                       // une chaine : on saute
            ++i;
            while (i < example.size() && example[i] != '\'') ++i;
            ++i;
            continue;
        }
        if (!identChar(example[i]) || std::isdigit(static_cast<unsigned char>(example[i]))) {
            ++i;
            continue;
        }
        const auto start = i;
        while (i < example.size() && identChar(example[i])) ++i;
        idents.push_back({std::string(example.substr(start, i - start)), start});
    }

    const auto nextNonSpace = [&example](std::size_t from) -> char {
        while (from < example.size() && std::isspace(static_cast<unsigned char>(example[from])))
            ++from;
        return from < example.size() ? example[from] : '\0';
    };

    for (std::size_t k = 0; k < idents.size(); ++k) {
        const auto& t = idents[k];
        if (isKeyword(t.text)) continue;

        const auto after = nextNonSpace(t.at + t.text.size());
        const auto before = t.at == 0 ? '\0' : example[t.at - 1];

        // UN NOM DE PARAMETRE N'EST PAS UNE VARIABLE. Dans
        // `PMP(Count := 3, Eq := Pompes)`, `Count` et `Eq` sont des broches du
        // bloc ; les declarer donnait trois BOOL parasites et trois notes qui
        // n'apprenaient rien. On les reconnait a leur voisinage : un `:=` apres,
        // une parenthese ouvrante ou une virgule avant.
        {
            std::size_t j = t.at + t.text.size();
            while (j < example.size() && std::isspace(static_cast<unsigned char>(example[j]))) ++j;
            const bool suivi = j + 1 < example.size() && example[j] == ':' && example[j + 1] == '=';
            std::size_t b = t.at;
            while (b > 0 && std::isspace(static_cast<unsigned char>(example[b - 1]))) --b;
            const char precedent = b == 0 ? '\0' : example[b - 1];
            if (suivi && (precedent == '(' || precedent == ',')) continue;
        }

        // UN MEMBRE SE RATTACHE A SA BASE plutot que d'etre ignore : c'est lui
        // qui dit de quel type la base est. `CarteDI_R0S4[2].Val` ne declare
        // rien en soi, mais `.Val` designe le seul DDT de la bibliotheque qui
        // l'ait, et c'est ainsi qu'on retrouve ST_IO_Dig.
        if (before == '.') {
            if (k > 0) {
                // Remonter a la base : sauter les [n] et les .membres.
                std::size_t base = k - 1;
                while (base > 0 && example[idents[base].at] >= '0'
                       && example[idents[base].at] <= '9') --base;
                std::string racine = idents[base].text;
                for (auto& u : uses)
                    if (u.name == racine) { u.members.insert(t.text); break; }
            }
            continue;
        }

        if (after == '[') {
            auto& u = note(t.text, {});
            u.indexed = true;
            const auto lb = example.find('[', t.at);
            const auto rb = example.find(']', lb);
            if (rb != std::string_view::npos) {
                const auto inner = trim(example.substr(lb + 1, rb - lb - 1));
                if (!inner.empty() && std::isdigit(static_cast<unsigned char>(inner.front())))
                    u.highest = std::max<std::int64_t>(u.highest, std::atoll(inner.c_str()));
            }
            continue;
        }

        if (after != '(') { note(t.text, {}); continue; }

        // Un appel. Avec des parametres nommes c'est une instance de bloc ;
        // sans, c'est une fonction - INT_TO_REAL et consorts - et on ne declare
        // rien.
        const auto open = example.find('(', t.at);
        const auto close = example.find(')', open);
        if (open == std::string_view::npos || close == std::string_view::npos) continue;
        const auto args = example.substr(open + 1, close - open - 1);
        if (args.find(":=") == std::string_view::npos) continue;

        // L'instance porte le type du bloc que cette page documente - SAUF si
        // l'exemple la declare : « (* declarer : PMP : DFB_EQ_PUMP; *) ». Les
        // broches se lisent alors sur CE bloc-la, et les arguments prennent
        // ses types. C'est ce qui permet a un exemple d'appeler deux blocs.
        const project::CatalogEntry* appele = &entry;
        for (const auto& d : explicites) {
            if (upper(d.name) != upper(t.text)) continue;
            if (const auto* autre = findEntry(lib, parseTypeSpec(d.type).element))
                appele = autre;
        }
        (void)note(t.text, appele->name);

        // Chaque `param := argument`.
        std::size_t at = 0;
        while (at < args.size()) {
            const auto assign = args.find(":=", at);
            if (assign == std::string_view::npos) break;
            const auto param = trim(args.substr(at, assign - at));
            auto end = args.find(',', assign);
            if (end == std::string_view::npos) end = args.size();
            const auto arg = trim(args.substr(assign + 2, end - assign - 2));
            at = end + 1;

            // L'argument doit etre un identifiant simple : un litteral ou une
            // expression n'a rien a declarer.
            if (arg.empty() || !std::isalpha(static_cast<unsigned char>(arg.front()))) continue;
            if (!std::all_of(arg.begin(), arg.end(), identChar)) continue;
            if (isKeyword(arg)) continue;

            std::string type;
            if (const auto* d = appele->declaration(param)) type = d->type;
            (void)note(arg, type);
        }
    }

    return uses;
}

} // namespace

// -----------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> sectionsOfLibraryFile(std::string_view contents) {
    // « <<<section NOM LANGAGE>>> » ... « <<<end>>> », autant de fois qu'il y a
    // de sections. Le nom est le premier mot apres « section ».
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t at = 0;
    bool dedans = false;
    std::string nom, corps;
    while (at < contents.size()) {
        auto nl = contents.find('\n', at);
        if (nl == std::string_view::npos) nl = contents.size();
        const auto brute = contents.substr(at, nl - at);
        const auto ligne = trim(brute);
        at = nl + 1;
        if (ligne.rfind("<<<", 0) == 0) {
            if (dedans) out.emplace_back(nom, corps);
            dedans = false;
            corps.clear();
            nom.clear();
            if (upper(ligne).rfind("<<<SECTION", 0) == 0) {
                auto reste = trim(std::string_view(ligne).substr(10));
                if (reste.size() >= 3 && reste.substr(reste.size() - 3) == ">>>")
                    reste = trim(std::string_view(reste).substr(0, reste.size() - 3));
                const auto espace = reste.find(' ');
                nom = espace == std::string::npos ? reste : reste.substr(0, espace);
                dedans = true;
            }
            continue;
        }
        if (dedans) {
            corps += std::string(brute);
            corps += '\n';
        }
    }
    if (dedans) out.emplace_back(nom, corps);
    return out;
}

std::string bodyOfLibraryFile(std::string_view contents) {
    // Le corps commence a la premiere ligne qui debute par "<<<". Tout ce qui
    // precede est l'en-tete et les declarations.
    std::size_t at = 0;
    while (at < contents.size()) {
        auto nl = contents.find('\n', at);
        if (nl == std::string_view::npos) nl = contents.size();
        const auto line = trim(contents.substr(at, nl - at));
        if (line.rfind("<<<", 0) == 0) {
            // LE CORPS S'ARRETE A LA SECTION SUIVANTE. Un fichier a plusieurs
            // sections, et tout rendre d'un bloc colle un "<<<section ...>>>"
            // au milieu du ST : le parseur s'arrete alors sur un '<' avec un
            // numero de ligne qui ne designe rien de comprehensible.
            std::string body;
            auto from = std::min(contents.size(), nl + 1);
            while (from < contents.size()) {
                auto end = contents.find('\n', from);
                if (end == std::string_view::npos) end = contents.size();
                if (trim(contents.substr(from, end - from)).rfind("<<<", 0) == 0) break;
                body += std::string(contents.substr(from, end - from));
                body += '\n';
                from = end + 1;
            }
            return body;
        }
        at = nl + 1;
    }
    return {};
}

// -----------------------------------------------------------------------------
core::Result<std::shared_ptr<TrySession>> TrySession::build(
    const project::CatalogEntry& entry,
    const std::vector<project::CatalogEntry>& library) {

    if (entry.help.example.empty())
        return core::fail(core::ErrorCode::InvalidArgument,
                          entry.name + " n'a pas d'exemple. L'onglet Modifier en pose un.");

    auto session = std::make_shared<TrySession>();
    session->example_ = entry.help.example;
    session->project_ = std::make_shared<Project>();
    auto& p = *session->project_;

    Task mast;
    mast.name = p.strings.intern("MAST");
    mast.type = "cyclic";
    p.tasks.push_back(mast);

    std::set<std::string> seen;
    if (entry.kind == project::CatalogKind::DerivedType)
        addDerivedType(p, entry, library, seen);
    else if (entry.kind == project::CatalogKind::FunctionBlock)
        addFunctionBlock(p, entry, library, seen, session->notes_);

    // Les variables que l'exemple nomme. Celles qu'il DECLARE dans un
    // commentaire « (* declarer : ... *) » prennent le type ecrit, et leur
    // valeur initiale ; les autres, ce que l'inference en deduit.
    const auto explicites = declaredInExample(session->example_);
    auto uses = inferUses(session->example_, entry, library, explicites);
    const auto declaree = [&explicites](const std::string& name) -> const Declared* {
        for (const auto& d : explicites)
            if (upper(d.name) == upper(name)) return &d;
        return nullptr;
    };
    for (const auto& d : explicites) {
        bool vu = false;
        for (const auto& u : uses)
            if (upper(u.name) == upper(d.name)) vu = true;
        if (!vu) uses.push_back(Use{d.name, d.type, {}, false, 0});
    }
    for (const auto& use : uses) {
        Variable v;
        v.name  = p.strings.intern(use.name);
        v.scope = VariableScope::Global;

        const auto* ecrite = declaree(use.name);
        std::string type = ecrite ? ecrite->type : use.type;
        if (ecrite && !ecrite->initial.empty()) v.initValue = p.strings.intern(ecrite->initial);

        // Le type se deduit des MEMBRES quand rien ne l'a donne : `.Val` sur
        // une base indexee designe ST_IO_Dig, et l'exemple devient jouable sans
        // qu'on ait eu a l'ecrire nulle part.
        if (type.empty() && !use.members.empty()) {
            const auto ddt = ddtWithMembers(library, use.members);
            if (!ddt.empty()) {
                type = use.indexed
                     ? "ARRAY[0.." + std::to_string(std::max<std::int64_t>(use.highest, 0))
                       + "] OF " + ddt
                     : ddt;
            }
        }
        if (type.empty() && use.indexed) {
            type = "ARRAY[0.." + std::to_string(std::max<std::int64_t>(use.highest, 0)) + "] OF BOOL";
        }
        if (type.empty()) {
            // Rien ne dit son type. Pour un DDT, l'exemple parle presque
            // toujours d'une instance de ce DDT ; sinon on declare en BOOL et
            // ON LE DIT. Une hypothese muette serait pire qu'un exemple qui ne
            // tourne pas.
            if (entry.kind == project::CatalogKind::DerivedType) {
                type = entry.name;
            } else {
                type = "BOOL";
                session->notes_.push_back(
                    use.name + " : type inconnu de l'exemple, declare en BOOL");
            }
        }
        const auto spec = parseTypeSpec(type);

        // LE TYPE DEDUIT DOIT ETRE MONTE, LUI AUSSI. Il ne fait pas partie des
        // declarations du bloc - c'est l'exemple qui l'a amene - donc rien ne
        // l'a ajoute au projet de poche. Sans ca, `CarteDI_R0S4[2].Val` designe
        // un type connu de nom et vide de contenu, et le cycle s'arrete sur
        // « n'est pas declare » pour une raison qui n'a rien a voir.
        if (const auto* dep = findEntry(library, spec.element)) {
            if (dep->kind == project::CatalogKind::DerivedType)
                addDerivedType(p, *dep, library, seen);
            else if (dep->kind == project::CatalogKind::FunctionBlock)
                addFunctionBlock(p, *dep, library, seen, session->notes_);
        }

        fillTypeRef(p, v.type, spec);
        if (const auto pou = findPou(p, spec.element); pou != kNoIndex)
            v.type.fbTypeIndex = pou;

        p.variables.push_back(v);
        session->declared_.push_back(use.name + " : " + type);
    }

    // LES ADRESSES DIRECTES. Un exemple qui ecrit `%MX40` designe un bit de la
    // memoire de l'automate ; le simulateur l'atteint par une variable LOCALISEE.
    // Sans en poser une, l'exemple s'arrete sur « %MX40 n'est pas declare », ce
    // qui est vrai et n'apprend rien.
    {
        const auto& ex = session->example_;
        std::set<std::string> adresses;
        for (std::size_t i = 0; i < ex.size(); ++i) {
            if (ex[i] != '%') continue;
            std::size_t j = i + 1;
            while (j < ex.size() && (identChar(ex[j]) || ex[j] == '.')) ++j;
            if (j > i + 1) adresses.insert(ex.substr(i, j - i));
            i = j - 1;
        }
        int n = 0;
        for (const auto& raw : adresses) {
            Variable v;
            v.name    = p.strings.intern("Adresse_" + std::to_string(++n));
            v.scope   = VariableScope::Global;
            v.located = true;
            v.address = Address::parse(raw);
            v.address.raw = raw;
            // LA TAILLE EST LA LETTRE QUI SUIT LA ZONE : %MW un INT, %MD un
            // DINT, %MF un REAL, %MX ou rien un bit - %I0.1.0, %Q0.2.3, %S6.
            // Chercher un W n'importe ou faisait de %MD1200 un BOOL.
            std::string type = "BOOL";
            if (raw.size() > 2) {
                switch (std::toupper(static_cast<unsigned char>(raw[2]))) {
                case 'W': type = "INT";  break;
                case 'D': type = "DINT"; break;
                case 'F': type = "REAL"; break;
                default:  break;
                }
            }
            fillTypeRef(p, v.type, TypeSpec{type, false, 0, -1});
            p.variables.push_back(v);
            session->declared_.push_back(raw + " : " + type + "   (adresse directe)");
        }
    }

    // La section qui porte l'exemple.
    {
        Section s;
        s.name     = p.strings.intern("Exemple");
        s.language = PouLanguage::ST;
        s.task     = p.strings.intern("MAST");
        s.order    = 0;
        s.body     = session->example_;
        p.sections.push_back(s);
        const auto si = static_cast<Index>(p.sections.size() - 1);
        Pou pou;
        pou.name = p.sections[si].name;
        pou.kind = PouKind::Section;
        pou.sections.push_back(si);
        p.pous.push_back(pou);
        p.sections[si].owner = static_cast<Index>(p.pous.size() - 1);
        p.tasks[0].sections.push_back(si);
    }
    p.buildIndices();

    // L'exemple se parse-t-il ? Le dire ici, avec la ligne, plutot que de
    // laisser le simulateur echouer sans expliquer.
    if (auto program = sim::parse(session->example_, "Exemple"); !program)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "l'exemple ne se lit pas : " + program.error().message());

    session->runtime_ = std::make_unique<sim::Runtime>(session->project_);
    if (auto r = session->runtime_->prepare("MAST"); !r)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "le projet d'epreuve n'a pas pu etre monte");

    for (const auto& d : session->runtime_->preparationDiagnostics())
        if (d.severity == sim::Diagnostic::Severity::Error) {
            session->notes_.push_back(d.message);
            session->prepErrors_.push_back(d.message);
        }

    session->names_ = session->runtime_->names();
    std::sort(session->names_.begin(), session->names_.end());

    // Les lignes de l'exemple, gardees telles quelles pour l'affichage.
    std::size_t at = 0;
    while (at <= session->example_.size()) {
        auto nl = session->example_.find('\n', at);
        if (nl == std::string::npos) nl = session->example_.size();
        session->lines_.push_back(session->example_.substr(at, nl - at));
        if (nl == session->example_.size()) break;
        at = nl + 1;
    }
    return session;
}

// -----------------------------------------------------------------------------
core::Result<std::shared_ptr<TrySession>> TrySession::buildProgram(
    std::string_view declarations, std::string_view program,
    const std::vector<project::CatalogEntry>& library) {

    auto session = std::make_shared<TrySession>();
    session->example_ = std::string(program);
    session->project_ = std::make_shared<Project>();
    auto& p = *session->project_;

    Task mast;
    mast.name = p.strings.intern("MAST");
    mast.type = "cyclic";
    p.tasks.push_back(mast);

    // Les commentaires (* *) des declarations sont retires avant de lire : un
    // essai commente ses variables comme on commente un programme.
    std::string decl(declarations);
    for (std::size_t i = 0; i + 1 < decl.size(); ++i) {
        if (decl[i] != '(' || decl[i + 1] != '*') continue;
        const auto fin = decl.find("*)", i + 2);
        const auto stop = fin == std::string::npos ? decl.size() : fin + 2;
        for (std::size_t j = i; j < stop; ++j)
            if (decl[j] != '\n') decl[j] = ' ';
        i = stop - 1;
    }

    // Les types standard que le simulateur connait par leur nom : ils se
    // declarent sans rien monter.
    static const std::set<std::string> kStandard = {
        "BOOL","EBOOL","INT","UINT","DINT","UDINT","REAL","WORD","DWORD","BYTE","TIME",
        "TON","TOF","TP","CTU","CTD","R_TRIG","F_TRIG","RS","SR",
    };

    std::set<std::string> seen;
    std::size_t at = 0;
    while (at < decl.size()) {
        auto nl = decl.find('\n', at);
        if (nl == std::string::npos) nl = decl.size();
        auto line = trim(std::string_view(decl).substr(at, nl - at));
        at = nl + 1;
        if (line.empty()) continue;
        if (!line.empty() && line.back() == ';') line = trim(line.substr(0, line.size() - 1));
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            return core::fail(core::ErrorCode::InvalidArgument,
                              "declaration sans ':' : " + line);
        const auto name = trim(line.substr(0, colon));
        auto typeText = trim(line.substr(colon + 1));
        // Une valeur initiale « := 20 » est acceptee et posee.
        std::string initial;
        if (const auto assign = typeText.find(":="); assign != std::string::npos) {
            initial  = trim(typeText.substr(assign + 2));
            typeText = trim(typeText.substr(0, assign));
        }
        const auto spec = parseTypeSpec(typeText);

        if (const auto* dep = findEntry(library, spec.element)) {
            if (dep->kind == project::CatalogKind::DerivedType)
                addDerivedType(p, *dep, library, seen);
            else if (dep->kind == project::CatalogKind::FunctionBlock)
                addFunctionBlock(p, *dep, library, seen, session->notes_);
        } else if (kStandard.count(upper(spec.element)) == 0
                   && upper(spec.element).rfind("STRING", 0) != 0) {
            return core::fail(core::ErrorCode::InvalidArgument,
                              name + " : type inconnu '" + spec.element + "'");
        }

        Variable v;
        v.name  = p.strings.intern(name);
        v.scope = VariableScope::Global;
        if (!initial.empty()) v.initValue = p.strings.intern(initial);
        fillTypeRef(p, v.type, spec);
        if (const auto pou = findPou(p, spec.element); pou != kNoIndex)
            v.type.fbTypeIndex = pou;
        p.variables.push_back(v);
        session->declared_.push_back(name + " : " + typeText);
    }

    {
        Section s;
        s.name     = p.strings.intern("Essai");
        s.language = PouLanguage::ST;
        s.task     = p.strings.intern("MAST");
        s.order    = 0;
        s.body     = session->example_;
        p.sections.push_back(s);
        const auto si = static_cast<Index>(p.sections.size() - 1);
        Pou pou;
        pou.name = p.sections[si].name;
        pou.kind = PouKind::Section;
        pou.sections.push_back(si);
        p.pous.push_back(pou);
        p.sections[si].owner = static_cast<Index>(p.pous.size() - 1);
        p.tasks[0].sections.push_back(si);
    }
    p.buildIndices();

    if (auto parsed = sim::parse(session->example_, "Essai"); !parsed)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "le programme d'essai ne se lit pas : " + parsed.error().message());

    session->runtime_ = std::make_unique<sim::Runtime>(session->project_);
    if (auto r = session->runtime_->prepare("MAST"); !r)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "le projet d'essai n'a pas pu etre monte");
    for (const auto& d : session->runtime_->preparationDiagnostics())
        if (d.severity == sim::Diagnostic::Severity::Error)
            return core::fail(core::ErrorCode::InvalidArgument, d.message);

    session->names_ = session->runtime_->names();
    std::sort(session->names_.begin(), session->names_.end());
    return session;
}

namespace {

sim::Value valueOfText(const std::string& value) {
    std::string up;
    for (const char c : value) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::size_t b = 0, e = up.size();
    while (b < e && std::isspace(static_cast<unsigned char>(up[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(up[e - 1]))) --e;
    up = up.substr(b, e - b);
    if (up == "TRUE" || up == "ON")   return sim::Value::boolean(true);
    if (up == "FALSE" || up == "OFF") return sim::Value::boolean(false);
    // Une duree : T#1s500ms, TIME#2m, t#49d17h. Sans ca, « T#5s » se lisait 0 :
    // la table d'essai posait une temporisation nulle sans rien dire.
    if (up.rfind("T#", 0) == 0 || up.rfind("TIME#", 0) == 0) {
        std::int64_t ms = 0;
        std::size_t i = up.find('#') + 1;
        bool lu = false;
        while (i < up.size()) {
            if (up[i] == '_') { ++i; continue; }
            std::size_t j = i;
            while (j < up.size() && (std::isdigit(static_cast<unsigned char>(up[j])) || up[j] == '.')) ++j;
            if (j == i) break;
            const double n = std::atof(up.substr(i, j - i).c_str());
            std::size_t k = j;
            while (k < up.size() && std::isalpha(static_cast<unsigned char>(up[k]))) ++k;
            const auto unite = up.substr(j, k - j);
            if      (unite == "D")  ms += static_cast<std::int64_t>(n * 86400000.0);
            else if (unite == "H")  ms += static_cast<std::int64_t>(n * 3600000.0);
            else if (unite == "M")  ms += static_cast<std::int64_t>(n * 60000.0);
            else if (unite == "S")  ms += static_cast<std::int64_t>(n * 1000.0);
            else if (unite == "MS") ms += static_cast<std::int64_t>(n);
            else break;
            lu = true;
            i = k;
        }
        if (lu) return sim::Value::time(ms);
    }
    // Un litteral IEC : 16#2026, 2#1010, 8#17, avec des soulignes permis.
    // Sans ca, « 16#2026 » se lisait 16, et un mot BCD d'horloge devenait
    // un 16 que rien ne signalait.
    if (const auto hash = up.find('#'); hash != std::string::npos) {
        const auto base = std::atoi(up.substr(0, hash).c_str());
        std::string digits;
        for (const char c : up.substr(hash + 1)) if (c != '_') digits += c;
        if (base == 2 || base == 8 || base == 16)
            return sim::Value::integer(sim::Type::DInt,
                static_cast<std::int64_t>(std::strtoll(digits.c_str(), nullptr, base)));
    }
    if (up.find('.') != std::string::npos || up.find('E') != std::string::npos)
        return sim::Value::real(std::atof(up.c_str()));
    return sim::Value::integer(sim::Type::DInt, std::atoll(up.c_str()));
}

} // namespace

bool TrySession::write(const std::string& name, const std::string& value) {
    if (!runtime_) return false;
    // Une adresse directe (%SW0, %MW100) n'existe qu'une fois nommee par le
    // programme. La lire la cree, avec le type que dit son prefixe : sans ca,
    // on ne pouvait pas poser un mot systeme avant le premier cycle.
    if (!name.empty() && name.front() == '%') {
        sim::Value ignored;
        (void)runtime_->read(name, ignored);
    }
    if (!runtime_->known(name)) return false;
    return runtime_->set(name, valueOfText(value));
}

std::string TrySession::read(const std::string& name) const {
    sim::Value v;
    if (!runtime_ || !runtime_->get(name, v)) return {};
    return v.display();
}

// -----------------------------------------------------------------------------
TryReport TrySession::observe() const {
    TryReport out;
    out.notes    = notes_;
    out.declared = declared_;
    if (!runtime_) { out.failure = "aucune session"; return out; }
    out.ran   = true;
    out.scans = runtime_->scanCount();
    fillReport(out);
    return out;
}

TryReport TrySession::step(std::int64_t deltaMs) {
    TryReport out;
    out.notes    = notes_;
    out.declared = declared_;
    if (!runtime_) { out.failure = "aucune session"; return out; }

    const auto report = runtime_->step(deltaMs);
    out.ran   = true;
    out.scans = runtime_->scanCount();
    for (const auto& d : report.diagnostics)
        if (d.severity == sim::Diagnostic::Severity::Error && out.failure.empty())
            out.failure = d.message;
    if (report.halted && out.failure.empty()) out.failure = "le cycle a ete arrete";

    fillReport(out);
    return out;
}

std::string TrySession::advance(std::int64_t deltaMs) {
    if (!runtime_) return "aucune session";
    const auto report = runtime_->step(deltaMs);
    lastStatements_ = report.statements;
    for (const auto& d : report.diagnostics)
        if (d.severity == sim::Diagnostic::Severity::Error) return d.message;
    if (report.halted) return "le cycle a ete arrete";
    return {};
}

// Le releve lui-meme : les valeurs, et ce que chaque ligne touche. Partage par
// `step` et `observe` pour qu'un tableau rafraichi et un tableau apres cycle ne
// puissent pas montrer deux choses differentes.
void TrySession::fillReport(TryReport& out) const {
    const auto observe = [this](const std::string& name) -> Observed {
        sim::Value v;
        Observed o{name, {}, runtime_->isForced(name), {}};
        if (runtime_->get(name, v)) {
            o.value = v.display();
            if (v.type() != sim::Type::Unknown) o.type = std::string(sim::toString(v.type()));
        }
        return o;
    };

    for (const auto& n : names_) out.all.push_back(observe(n));

    // Ce que CHAQUE ligne touche : les noms qu'elle contient, y compris les
    // membres d'instance. C'est ce qui permet de lire l'exemple et ses valeurs
    // d'un seul coup d'oeil, au lieu de chercher dans un tableau a cote.
    for (const auto& line : lines_) {
        TryLine tl;
        tl.text = line;
        for (const auto& n : names_) {
            if (line.find(n) == std::string::npos) continue;
            // Le nom complet d'un membre - "IO_Pompes.Count" - n'apparait pas
            // tel quel dans "IO_Pompes(Count := 2)". On accepte donc aussi le
            // couple instance + membre sur la meme ligne.
            tl.values.push_back(observe(n));
        }
        if (tl.values.empty()) {
            for (const auto& n : names_) {
                const auto dot = n.find('.');
                if (dot == std::string::npos) continue;
                if (line.find(n.substr(0, dot)) != std::string::npos
                    && line.find(n.substr(dot + 1)) != std::string::npos)
                    tl.values.push_back(observe(n));
            }
        }
        out.lines.push_back(std::move(tl));
    }
}

bool TrySession::force(const std::string& name, const std::string& value) {
    if (!runtime_ || !runtime_->known(name)) return false;
    sim::Value current;
    (void)runtime_->get(name, current);

    // La valeur se lit comme dans un fichier de forcage : TRUE/FALSE, 42, 1.5.
    const auto up = upper(trim(value));
    sim::Value v;
    if (up == "TRUE" || up == "1" || up == "ON")        v = sim::Value::boolean(true);
    else if (up == "FALSE" || up == "0" || up == "OFF") v = sim::Value::boolean(false);
    else if (value.find('.') != std::string::npos)      v = sim::Value::real(std::atof(value.c_str()));
    else v = sim::Value::integer(sim::Type::DInt, std::atoll(value.c_str()));
    return runtime_->force(name, v);
}

bool TrySession::unforce(const std::string& name) {
    return runtime_ && runtime_->unforce(name);
}

void TrySession::reset() { if (runtime_) runtime_->reset(); }

// -----------------------------------------------------------------------------
namespace {

// Deux valeurs affichees sont-elles la meme ? TRUE et true, 16 et 16.0 : oui.
// Les nombres se comparent a un millieme pres, parce qu'un REAL calcule par
// cycles successifs ne tombe jamais pile.
bool sameValue(const std::string& read, const std::string& wanted) {
    const auto a = upper(trim(read)), b = upper(trim(wanted));
    if (a == b) return true;
    char* ea = nullptr;
    char* eb = nullptr;
    const double x = std::strtod(a.c_str(), &ea);
    const double y = std::strtod(b.c_str(), &eb);
    if (ea == a.c_str() || eb == b.c_str() || *ea != '\0' || *eb != '\0') return false;
    return std::fabs(x - y) <= 1e-3 * std::max(1.0, std::fabs(y));
}

struct Expectation {
    long long   cycles{0};
    std::string name;
    std::string value;
    std::string text;           // la ligne telle qu'ecrite, pour le message
};

} // namespace

TryAllRow tryOne(const project::CatalogEntry& e, const std::vector<project::CatalogEntry>& library) {
    TryAllRow row;
    row.name       = e.name;
    row.category   = e.category;
    row.hasExample = !e.help.example.empty();
    if (!row.hasExample) return row;

    auto session = TrySession::build(e, library);
    if (!session) {
        row.failure = session.error().context.empty() ? session.error().message()
                                                       : session.error().context;
        return row;
    }
    auto& s = **session;

    // UN BLOC QUI NE SE LIT PAS FAIT ECHOUER L'ESSAI. Le simulateur saute un
    // corps qu'il ne sait pas lire, et l'exemple « passait » sur un bloc qui
    // ne calculait rien - c'est ainsi qu'un CASE a labels multiples est
    // reste invisible.
    if (!s.preparationErrors().empty()) {
        row.failure = "le projet d'essai ne se prepare pas : " + s.preparationErrors().front();
        return row;
    }

    // Ce qui est pose avant de lancer : « X := v ». POSE UNE FOIS, PAS FORCE :
    // le programme peut le changer ensuite. Un mode courant pose a -1 doit
    // pouvoir devenir 2 au premier cycle ; force, il resterait a -1 et le
    // gestionnaire croirait demarrer a chaque cycle.
    for (const auto& g : e.givens()) {
        const auto at = g.find(":=");
        if (at == std::string::npos) { row.failure = "given illisible : " + g; return row; }
        const auto name = trim(std::string_view(g).substr(0, at));
        const auto value = trim(std::string_view(g).substr(at + 2));
        if (!s.write(name, value)) { row.failure = "given : " + name + " n'existe pas dans l'exemple"; return row; }
    }

    // Ce qui est attendu : « n : X = v », dans l'ordre des cycles.
    std::vector<Expectation> attendus;
    for (const auto& x : e.expectations()) {
        Expectation ex;
        ex.text = x;
        const auto colon = x.find(':');
        const auto eq = x.find('=', colon == std::string::npos ? 0 : colon);
        if (colon == std::string::npos || eq == std::string::npos) {
            row.failure = "expect illisible : " + x;
            return row;
        }
        ex.cycles = std::atoll(trim(std::string_view(x).substr(0, colon)).c_str());
        ex.name   = trim(std::string_view(x).substr(colon + 1, eq - colon - 1));
        ex.value  = trim(std::string_view(x).substr(eq + 1));
        if (ex.cycles < 1) ex.cycles = 1;
        attendus.push_back(std::move(ex));
    }
    std::stable_sort(attendus.begin(), attendus.end(),
                     [](const Expectation& a, const Expectation& b) { return a.cycles < b.cycles; });
    row.expectations = attendus.size();

    long long fait = 0;
    const long long jusqua = attendus.empty() ? 1 : attendus.back().cycles;
    std::size_t prochain = 0;
    while (fait < jusqua) {
        const auto report = s.step();
        ++fait;
        if (!report.failure.empty()) { row.failure = report.failure; return row; }
        while (prochain < attendus.size() && attendus[prochain].cycles == fait) {
            const auto& ex = attendus[prochain++];
            const auto lu = s.read(ex.name);
            if (lu.empty() && !s.variableNames().empty()) {
                bool connu = false;
                for (const auto& n : s.variableNames()) if (n == ex.name) connu = true;
                if (!connu) {
                    if (row.failure.empty()) row.failure = "expect : " + ex.name + " n'existe pas dans l'exemple";
                    continue;
                }
            }
            if (sameValue(lu, ex.value)) ++row.expectationsOk;
            else if (row.failure.empty())
                row.failure = "attendu " + ex.name + " = " + ex.value + " apres "
                            + std::to_string(ex.cycles) + " cycle(s), lu " + lu;
        }
    }
    row.ok = row.failure.empty();
    return row;
}

TryAllReport tryAll(const std::vector<project::CatalogEntry>& library) {
    TryAllReport out;
    for (const auto& e : library) {
        if (e.kind != project::CatalogKind::DerivedType
            && e.kind != project::CatalogKind::FunctionBlock) continue;
        auto row = tryOne(e, library);
        if (row.hasExample) {
            ++out.withExample;
            if (row.ok) ++out.ok;
        }
        out.rows.push_back(std::move(row));
    }
    return out;
}

} // namespace help
