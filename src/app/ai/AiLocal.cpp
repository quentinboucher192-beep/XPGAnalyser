// =============================================================================
//  app/ai/AiLocal.cpp - l'IA integree (1.16.0) : voir AiLocal.hpp
// -----------------------------------------------------------------------------
//  UN MOTEUR PAR REGLES, PAS UN MODELE DE LANGAGE. La demande est coupee en mots
//  (le texte tel qu'ecrit, et replie : minuscules, sans accents) ; on y lit :
//
//    - l'intention, par ses verbes et ses mots (ajoute, associe, supprime...),
//      dans un ordre fixe (une question, une suite, un synoptique, une vue, des
//      variables, associer, animer, supprimer, dupliquer, aligner, repartir,
//      renommer, cacher, deplacer, la taille, le texte, la couleur, creer) ;
//    - les genres (le libelle du registre, sa cle, des synonymes metier, le
//      pluriel, une faute pres) et leur determinant (un, deux, les, la...) ;
//    - les objets de la vue (leur nom, leur libelle, leur texte, "P1" pour
//      Pompe_1, "pompe 1"), la selection, et le dernier objet cree ou cite ;
//    - les variables du projet et de l'automate relie (jamais inventees) ;
//    - les couleurs (les noms francais -> les couleurs du logiciel), les
//      nombres (chiffres, un..vingt), les positions (a droite de, sous...).
//
//  1.16.1 : elle COMPREND la phrase plutot que de la decouper. Un nom propose n'est
//  jamais un mot-outil (pour, qui, avec...) ni un verbe de la demande (sinon elle
//  demande) ; un type se lit en langage courant ("une structure qui s'appelle
//  Capteur avec son type, sa mesure, ses defauts") et part d'un MODELE METIER
//  (capteur, capteur TOR, pompe, vanne, moteur, variateur, cuve, regulateur) quand
//  aucun membre n'est dit ; une demande trop mal comprise rend understood = false.
//
//  Elle rend une PROPOSITION faite des operations du contrat (AiOperations.hpp) :
//  la meme validation, le meme apercu, le meme Ctrl+Z qu'un vrai fournisseur. Elle
//  se verifie elle-meme (validate) : une proposition qu'elle saurait fausse n'est
//  pas montree, la reponse dit pourquoi.
// =============================================================================
#include "AiLocal.hpp"

#include "AiAutomate.hpp"
#include "AiDemo.hpp"       // fold
#include "AiRegistry.hpp"
#include "../../hmi/HmiEdit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace app::ai {

namespace {

// Les mots accentues qui reviennent, ecrits une fois.
const char* const kOpen = "\xC2\xAB ";
const char* const kClose = " \xC2\xBB";
const char* const kArrow = " \xE2\x86\x92 ";

std::string num(double d) { return hmi::formatNumber(std::round(d)); }
std::string code(std::string_view s) { return "`" + std::string(s) + "`"; }
std::string guill(std::string_view s) { return std::string(kOpen) + std::string(s) + kClose; }

// "a, b et c"
std::string joinAnd(const std::vector<std::string>& items, std::size_t max = 8) {
    std::string s;
    const std::size_t n = std::min(items.size(), max);
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) s += (i + 1 == n && n == items.size()) ? " et " : ", ";
        s += items[i];
    }
    if (items.size() > max) s += ", \xE2\x80\xA6 (" + std::to_string(items.size()) + " en tout)";
    return s;
}
std::string codes(const std::vector<std::string>& names, std::size_t max = 8) {
    std::vector<std::string> c;
    for (const auto& n : names) c.push_back(code(n));
    return joinAnd(c, max);
}
std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n > 1 ? many : one);
}

// ---- les mots de la demande ------------------------------------------------------------
struct Tok {
    std::string raw;        // tel qu'ecrit (un nom : Pompe_1_Marche)
    std::string f;          // replie : minuscules, sans accents (pompe_1_marche)
    bool        quoted{false};
};

std::string foldWord(std::string_view raw) {
    std::string f = fold(raw);
    f.erase(std::remove(f.begin(), f.end(), ' '), f.end());
    return f;
}

// La demande en mots. Un texte entre guillemets ("Marche", << Marche >>) est un seul mot ; une
// apostrophe, un tiret, une ponctuation separent ; le point reste dans un nom (Pompe.Marche, 1.5) ;
// < > = forment leurs propres mots (une condition).
std::vector<Tok> tokenize(std::string_view s) {
    std::vector<Tok> out;
    std::string cur;
    const auto flush = [&] {
        if (!cur.empty()) out.push_back({cur, foldWord(cur), false});
        cur.clear();
    };
    const auto starts = [&](std::size_t i, std::string_view seq) { return s.substr(i, seq.size()) == seq; };
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        // les guillemets : un texte entier
        std::string_view close;
        std::size_t open = 0;
        if (c == '"') { close = "\""; open = 1; }
        else if (starts(i, "\xC2\xAB")) { close = "\xC2\xBB"; open = 2; }
        else if (starts(i, "\xE2\x80\x9C")) { close = "\xE2\x80\x9D"; open = 3; }
        if (open > 0) {
            flush();
            const std::size_t end = s.find(close, i + open);
            std::string inner(s.substr(i + open, end == std::string_view::npos ? std::string_view::npos : end - i - open));
            // sans les blancs (et les espaces insecables) du bord
            const auto blank = [](const std::string& t, std::size_t k) {
                return t[k] == ' ' || (k + 1 < t.size() && static_cast<unsigned char>(t[k]) == 0xC2 && static_cast<unsigned char>(t[k + 1]) == 0xA0);
            };
            while (!inner.empty() && blank(inner, 0)) inner.erase(0, inner[0] == ' ' ? 1 : 2);
            while (!inner.empty() && (inner.back() == ' ' || (inner.size() >= 2 && static_cast<unsigned char>(inner[inner.size() - 2]) == 0xC2
                                                              && static_cast<unsigned char>(inner.back()) == 0xA0)))
                inner.resize(inner.back() == ' ' ? inner.size() - 1 : inner.size() - 2);
            if (!inner.empty()) out.push_back({inner, foldWord(inner), true});
            i = end == std::string_view::npos ? s.size() : end + close.size();
            continue;
        }
        if (starts(i, "\xE2\x80\x99") || starts(i, "\xE2\x80\x98") || starts(i, "\xE2\x80\x9D")) { flush(); i += 3; continue; }
        if (starts(i, "\xC2\xA0") || starts(i, "\xC2\xBB")) { flush(); i += 2; continue; }
        if (c == '<' || c == '>' || c == '=' || c == '!') {
            flush();
            std::string op(1, static_cast<char>(c));
            if (i + 1 < s.size() && s[i + 1] == '=') op += '=';
            if (c == '<' && i + 1 < s.size() && s[i + 1] == '>') op = "<>";
            if (!(c == '!' && op.size() == 1)) out.push_back({op, op, false});
            i += op.size();
            continue;
        }
        if (c == '.' && !cur.empty() && i + 1 < s.size() && std::isalnum(static_cast<unsigned char>(s[i + 1]))
            && std::isalnum(static_cast<unsigned char>(cur.back()))) {
            cur += '.';
            ++i;
            continue;
        }
        if (std::isalnum(c) || c == '_' || c == '#' || c == '$' || c >= 0x80) {
            cur += static_cast<char>(c);
            ++i;
            continue;
        }
        flush();
        ++i;
    }
    flush();
    return out;
}

bool isDigits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }); }

// Un nombre ecrit en chiffres ou en lettres (un..vingt) ; -1 : pas un nombre.
int numberOf(const std::string& f) {
    if (isDigits(f) && f.size() <= 6) return std::stoi(f);
    static const std::map<std::string, int> words = {
        {"un", 1}, {"une", 1}, {"deux", 2}, {"trois", 3}, {"quatre", 4}, {"cinq", 5}, {"six", 6}, {"sept", 7}, {"huit", 8},
        {"neuf", 9}, {"dix", 10}, {"onze", 11}, {"douze", 12}, {"treize", 13}, {"quatorze", 14}, {"quinze", 15}, {"seize", 16},
        {"vingt", 20}, {"double", 2}, {"triple", 3}};
    const auto it = words.find(f);
    return it == words.end() ? -1 : it->second;
}

// Les mots qui ne designent rien (les determinants, les prepositions...).
bool stopWord(const std::string& f) {
    static const std::set<std::string> k = {
        "le", "la", "les", "l", "un", "une", "des", "du", "de", "d", "au", "aux", "a", "et", "ou", "en", "sur", "dans", "avec", "pour", "par",
        "ce", "cet", "cette", "ces", "son", "sa", "ses", "mon", "ma", "mes", "ton", "ta", "tes", "moi", "toi", "me", "te", "se", "il", "elle",
        "ils", "elles", "on", "je", "tu", "nous", "vous", "qui", "que", "qu", "quoi", "y", "s", "n", "ne", "pas", "plus", "tout", "tous",
        "toute", "toutes", "svp", "stp", "merci", "peux", "peut", "pourrais", "veux", "voudrais", "aimerais", "fais", "faire", "fait",
        "variable", "variables", "objet", "objets", "vue", "nouvelle", "nouveau", "nouvelles", "nouveaux", "autre", "autres", "est",
        "sont", "etre", "c", "j", "m", "t", "via", "vers", "depuis", "sous", "entre", "chez", "si", "sinon", "quand", "lorsque"};
    return k.count(f) != 0;
}

// Les mots d'une PARTIE d'un objet : "le texte de Bouton_1", "le fond de la cuve", "le contour de P1".
bool partWord(const std::string& f) {
    static const std::set<std::string> k = {"texte", "libelle", "intitule", "etiquette", "fond", "contour", "bordure", "bord",
                                            "couleur", "remplissage", "ecriture", "nom", "taille", "largeur", "hauteur", "trait"};
    return k.count(f) != 0;
}

// 1.16.1 : LES MOTS-OUTILS du francais (articles, prepositions, pronoms, possessifs, conjonctions, adverbes) : jamais
// un nom propose (objet, variable, type, membre, vue) - "Cree un type pour..." ne nomme pas un type << pour >>.
bool functionWord(const std::string& f) {
    static const std::set<std::string> k = {
        "le", "la", "les", "l", "un", "une", "des", "du", "de", "d", "au", "aux", "a", "et", "ou", "en", "sur", "dans", "avec", "pour", "par",
        "sans", "sous", "vers", "chez", "entre", "depuis", "via", "contre", "selon", "pendant", "apres", "avant", "ce", "cet", "cette", "ces",
        "ceci", "cela", "ca", "celui", "celle", "ceux", "celles", "son", "sa", "ses", "mon", "ma", "mes", "ton", "ta", "tes", "notre", "nos",
        "votre", "vos", "leur", "leurs", "moi", "toi", "lui", "me", "te", "se", "il", "elle", "ils", "elles", "on", "je", "j", "tu", "nous",
        "vous", "qui", "que", "qu", "quoi", "dont", "lequel", "laquelle", "quel", "quelle", "quels", "quelles", "y", "s", "n", "m", "t", "c",
        "ne", "pas", "plus", "moins", "tres", "trop", "peu", "bien", "aussi", "ainsi", "alors", "donc", "mais", "car", "puis", "ensuite",
        "si", "sinon", "quand", "lorsque", "comme", "tout", "tous", "toute", "toutes", "chaque", "quelque", "quelques", "autre", "autres",
        "meme", "memes", "nouveau", "nouvel", "nouvelle", "nouveaux", "nouvelles", "ici", "voici", "voila", "oui", "non", "svp", "stp",
        "merci", "plait", "est", "sont", "etre", "ai", "as", "ont", "etc", "bon", "ok"};
    return k.count(f) != 0;
}

// 1.16.1 : les verbes d'une demande, sous leurs formes courantes (creer, ajouter, gerer, vouloir, appeler...) : jamais un
// nom propose - "un type pour gerer les capteurs" n'a pas de membre << Gerer >>.
bool requestVerb(const std::string& f) {
    static const std::set<std::string> k = {
        "cree", "crees", "creer", "creez", "creons", "ajoute", "ajoutes", "ajouter", "ajoutez", "rajoute", "rajouter", "rajoutez", "mets",
        "met", "mettre", "mettez", "pose", "poser", "posez", "place", "placer", "placez", "fais", "faire", "faites", "fait", "gere", "geres",
        "gerer", "gerez", "veux", "veut", "voudrais", "voudrait", "voulez", "vouloir", "aimerais", "aimerait", "souhaite", "souhaiterais",
        "faut", "faudrait", "peux", "peut", "pourrais", "pourriez", "pouvez", "appelle", "appelles", "appeler", "appele", "appelee",
        "appeles", "appelees", "nomme", "nommer", "nommee", "nommes", "nommees", "definis", "definir", "definit", "definissez", "insere",
        "inserer", "genere", "generer", "fabrique", "fabriquer", "dessine", "dessiner", "associe", "associer", "relie", "relier", "lie",
        "lier", "branche", "brancher", "duplique", "dupliquer", "copie", "copier", "supprime", "supprimer", "efface", "effacer", "enleve",
        "enlever", "retire", "retirer", "vire", "virer", "renomme", "renommer", "deplace", "deplacer", "bouge", "bouger", "decale",
        "decaler", "aligne", "aligner", "repartis", "repartir", "agrandis", "agrandir", "reduis", "reduire", "change", "changer", "modifie",
        "modifier", "contient", "contenir", "contenant", "comprend", "comprenant", "ayant", "avoir", "donne", "donner", "prevois", "prevoir",
        "prepare", "preparer", "construis", "construire", "decrire", "decris", "represente", "representer"};
    return k.count(f) != 0;
}

// 1.16.1 : des mots vagues (un truc, un machin, quelque chose) : ni un nom, ni une demande comprise.
bool vagueWord(const std::string& f) {
    static const std::set<std::string> k = {"truc", "trucs", "machin", "machins", "bidule", "bidules", "chose", "choses", "quelque", "brol"};
    return k.count(f) != 0;
}

// 1.16.1 : les mots de remplissage d'une liste de membres : "des membres de gestion, par exemple, etc., tel que".
bool fillerWord(const std::string& f) {
    static const std::set<std::string> k = {
        "exemple", "exemples", "etc", "notamment", "tel", "telle", "tels", "telles", "gestion", "membre", "membres", "champ", "champs",
        "element", "elements", "donnee", "donnees", "info", "infos", "information", "informations", "propriete", "proprietes", "attribut",
        "attributs", "variable", "variables", "style", "base", "basique", "classique", "classiques", "standard", "simple", "complet",
        "complete", "habituel", "habituels", "habituelle", "habituelles", "utile", "utiles", "necessaire", "necessaires", "important",
        "importants", "principal", "principaux", "suivant", "suivants", "suivante", "suivantes", "ihm", "structure", "structures",
        "structuree", "struct", "personnalise", "personnalisee", "perso", "utilisateur", "egalement", "ensemble", "cetera", "suite"};
    return k.count(f) != 0 || vagueWord(f);
}

// 1.16.1 : LE GARDE-FOU DES NOMS. Un nom propose (objet, variable, type, membre, vue) n'est jamais un mot-outil, un verbe de
// la demande, ni un mot vague ou de remplissage ("exemple", "etc", "gestion") : la reponse demande plutot.
bool badName(std::string_view raw) {
    const std::string f = [&] {
        std::string s = fold(raw);
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        return s;
    }();
    if (f.empty()) return true;
    static const std::set<std::string> filler = {"exemple", "exemples", "etc", "gestion", "membre", "membres", "champ", "champs", "notamment",
                                                 "donnee", "donnees", "cetera"};
    return functionWord(f) || requestVerb(f) || vagueWord(f) || filler.count(f) != 0;
}

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// Un nom ramene a ses lettres et chiffres, replie : "Pompe_1" -> "pompe1", "pompe 1" -> "pompe1".
std::string norm(std::string_view s) {
    std::string out;
    for (const char c : fold(s))
        if (std::isalnum(static_cast<unsigned char>(c))) out += c;
    return out;
}

// Le mot dit est-il ce mot, son pluriel (-s, -x, -aux), ou (`typo`) a une faute pres ?
bool sameWord(const std::string& said, const std::string& word, bool typo) {
    if (said == word) return true;
    if (said.size() == word.size() + 1 && said.compare(0, word.size(), word) == 0 && (said.back() == 's' || said.back() == 'x')) return true;
    if (word.size() > 2 && word.compare(word.size() - 2, 2, "al") == 0 && said == word.substr(0, word.size() - 2) + "aux") return true;
    if (typo && word.size() >= 5 && said.size() >= 4) {
        if (distance(said, word) <= 1) return true;
        if (said.back() == 's' && distance(said.substr(0, said.size() - 1), word) <= 1) return true;
    }
    return false;
}

// ---- les couleurs ---------------------------------------------------------------------
struct ColorWord {
    const char* word;   // replie, au masculin singulier
    const char* hex;    // une couleur du logiciel (le theme, les symboles)
};
const ColorWord kColors[] = {
    {"rouge", "#E5534B"}, {"vert", "#2ECC71"}, {"bleu", "#2F6FD6"}, {"jaune", "#F2C94C"}, {"orange", "#F39C12"}, {"gris", "#5A6577"},
    {"blanc", "#FFFFFF"}, {"noir", "#1B1F26"}, {"violet", "#9B59B6"}, {"rose", "#E91E8C"}, {"cyan", "#4FA3FF"}, {"turquoise", "#1ABC9C"},
    {"marron", "#8B5A2B"}, {"beige", "#D9C7A3"}};

std::string mix(std::string_view hex, int toward, double amount) {
    if (hex.size() != 7) return std::string(hex);
    std::string out = "#";
    for (int k = 0; k < 3; ++k) {
        const int v = std::stoi(std::string(hex.substr(1 + 2 * k, 2)), nullptr, 16);
        const int m = static_cast<int>(std::lround(v + (toward - v) * amount));
        static const char* const hx = "0123456789ABCDEF";
        out += hx[(m >> 4) & 15];
        out += hx[m & 15];
    }
    return out;
}

// La couleur dite au rang `i` (un nom, un #RRGGBB) ; vide : aucune. `len` : les mots pris (bleu clair : 2).
std::string colorAt(const std::vector<Tok>& t, std::size_t i, std::size_t* len = nullptr, std::string* label = nullptr) {
    if (i >= t.size()) return {};
    const std::string& f = t[i].f;
    if (f.size() == 7 && f[0] == '#' && std::all_of(f.begin() + 1, f.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
        if (len) *len = 1;
        if (label) *label = t[i].raw;
        std::string up = t[i].raw;
        for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return up;
    }
    for (const auto& c : kColors) {
        const std::string w = c.word;
        // le feminin et le pluriel : verte, vertes, bleue, blanche, grise, violette
        const bool match = f == w || f == w + "s" || f == w + "e" || f == w + "es" || (w == "blanc" && (f == "blanche" || f == "blanches"))
                        || (w == "violet" && (f == "violette" || f == "violettes"));
        if (!match) continue;
        std::string hex = c.hex;
        std::string name = w;
        std::size_t n = 1;
        if (i + 1 < t.size()) {
            const std::string& m = t[i + 1].f;
            if (m == "clair" || m == "claire" || m == "clairs" || m == "claires") { hex = mix(hex, 255, 0.35); name += " clair"; n = 2; }
            else if (m == "fonce" || m == "foncee" || m == "fonces" || m == "foncees") { hex = mix(hex, 0, 0.35); name += " fonc\xC3\xA9"; n = 2; }
        }
        if (len) *len = n;
        if (label) *label = name;
        return hex;
    }
    return {};
}

// ---- les genres ------------------------------------------------------------------------
struct KindWord {
    std::vector<std::string> words;
    hmi::Kind                kind;
};

std::vector<std::string> splitWords(std::string_view folded) {
    std::vector<std::string> out;
    std::string w;
    for (const char c : folded) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') w += c;
        else if (!w.empty()) {
            out.push_back(w);
            w.clear();
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

// Les mots qui designent un genre : les synonymes metier, le libelle et la cle de chaque genre de la
// bibliotheque (registre), le premier mot d'un libelle s'il n'est a aucun autre ; les plus longs d'abord.
const std::vector<KindWord>& kindWords() {
    static const std::vector<KindWord> k = [] {
        using K = hmi::Kind;
        std::vector<KindWord> out;
        std::set<std::string> taken;
        const auto add = [&](std::string_view phrase, K kind) {
            auto w = splitWords(fold(phrase));
            if (w.empty()) return;
            std::string joined;
            for (const auto& x : w) joined += x + " ";
            if (!taken.insert(joined).second) return;
            out.push_back({std::move(w), kind});
        };
        // les synonymes du metier (avant le registre : ils l'emportent a longueur egale)
        add("bouton poussoir", K::PushButton);
        add("bouton a impulsion", K::PushButton);
        add("bouton de commande", K::PushButton);
        add("bp", K::PushButton);
        add("bac", K::Tank);
        add("reservoir", K::Tank);
        add("citerne", K::Tank);
        add("voyant tor", K::Indicator);
        add("led", K::Indicator);
        add("tor", K::Indicator);
        add("indicateur de niveau", K::Bargraph);
        add("barre de niveau", K::Bargraph);
        add("bargraph", K::Bargraph);
        add("titre", K::Text);
        add("libelle", K::Text);
        add("etiquette", K::Text);
        add("tuyau", K::Pipe);
        add("conduite", K::Pipe);
        add("canalisation", K::Pipe);
        add("electrovanne", K::Valve);
        add("ventilo", K::Fan);
        add("tapis", K::Conveyor);
        add("commutateur", K::Switch);
        add("cercle", K::Ellipse);
        add("trait", K::Line);
        add("champ de saisie", K::InputField);
        add("tendance", K::Trend);
        add("capteur", K::IsaInstrument);
        add("afficheur", K::NumericDisplay);
        add("variateur", K::SpeedDrive);
        const auto& reg = registry();
        std::map<std::string, int> firsts;
        for (const auto& e : reg.kinds()) {
            if (e.family.empty()) continue;
            const auto w = splitWords(fold(e.label));
            if (!w.empty()) ++firsts[w.front()];
        }
        for (const auto& e : reg.kinds()) {
            if (e.family.empty()) continue;   // hors bibliotheque : ne se cree pas
            add(e.label, e.kind);
            add(e.key, e.kind);
        }
        for (const auto& e : reg.kinds()) {
            if (e.family.empty()) continue;
            const auto w = splitWords(fold(e.label));
            if (w.size() > 1 && w.front().size() >= 4 && firsts[w.front()] == 1) add(w.front(), e.kind);
        }
        std::stable_sort(out.begin(), out.end(), [](const KindWord& a, const KindWord& b) { return a.words.size() > b.words.size(); });
        return out;
    }();
    return k;
}

// Le nom de base des objets neufs d'un genre ("Pompe", "Bouton", "Bargraphe") : le premier mot du libelle,
// sans accents.
std::string baseName(hmi::Kind k) {
    const auto w = splitWords(fold(hmi::kindLabel(k)));
    std::string b = w.empty() ? std::string(hmi::kindKey(k)) : w.front();
    if (!b.empty()) b[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(b[0])));
    return b;
}

// Un mot au pluriel : -s, -x apres -au/-eu, -al -> -aux ; deja en -s, -x, -z : tel quel.
std::string pluralWord(const std::string& w) {
    if (w.empty()) return w;
    const char last = w.back();
    if (last == 's' || last == 'x' || last == 'z') return w;
    const auto ends = [&](std::string_view e) { return w.size() > e.size() && w.compare(w.size() - e.size(), e.size(), e) == 0; };
    if (ends("au") || ends("eu")) return w + "x";
    if (ends("al")) return w.substr(0, w.size() - 2) + "aux";
    return w + "s";
}

std::string kindPlural(hmi::Kind k, std::size_t n) {
    std::string l(hmi::kindLabel(k));
    if (n <= 1) return l;
    // le nom ET ses adjectifs (Afficheur numerique -> Afficheurs numeriques), jusqu'a une preposition
    // (Bouton a impulsion -> Boutons a impulsion, Bouteille de gaz) ; un libelle deja au pluriel (Boutons
    // radio, Parametres systeme), un mot compose (multi-etats, coupe-feu), un sigle (ISA, PID), un nombre
    // (Afficheur 7 segments) restent tels quels.
    std::vector<std::string> words;
    for (std::size_t s = 0; s <= l.size();) {
        const auto sp = l.find(' ', s);
        words.push_back(l.substr(s, sp == std::string::npos ? std::string::npos : sp - s));
        if (sp == std::string::npos) break;
        s = sp + 1;
    }
    if (words.empty() || words.front().empty()) return l;
    const char first = words.front().back();
    if (first == 's' || first == 'x') return l;
    static const std::set<std::string> stops = {"\xC3\xA0", "de", "du", "des", "en", "par", "avec", "et", "sur", "pour", "au", "aux"};
    words.front() = pluralWord(words.front());
    for (std::size_t i = 1; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w.empty() || stops.count(w) || w.find('\'') != std::string::npos || w.find('-') != std::string::npos) break;
        const auto c = static_cast<unsigned char>(w[0]);
        if (std::isdigit(c) || std::isupper(c)) break;
        if (w.size() >= 2 && std::all_of(w.begin(), w.end(), [](char ch) { return std::isupper(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)); }))
            break;
        words[i] = pluralWord(w);
    }
    std::string out;
    for (const auto& w : words) out += (out.empty() ? "" : " ") + w;
    return out;
}
std::string lowerFirst(std::string s) {
    if (!s.empty() && std::isupper(static_cast<unsigned char>(s[0]))) s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}
std::string countKind(hmi::Kind k, std::size_t n) { return std::to_string(n) + " " + lowerFirst(kindPlural(k, n)); }

// Un genre au feminin (une pompe, une cuve) : l'accord de "alignees", "reparties".
bool feminine(hmi::Kind k) {
    switch (k) {
        case hmi::Kind::Pump: case hmi::Kind::Tank: case hmi::Kind::Valve: case hmi::Kind::ThreeWayValve: case hmi::Kind::Pipe:
        case hmi::Kind::Gauge: case hmi::Kind::Line: case hmi::Kind::Ellipse: case hmi::Kind::Lamp: case hmi::Kind::Image:
        case hmi::Kind::Trend: case hmi::Kind::List: case hmi::Kind::Video: case hmi::Kind::GasBottle: case hmi::Kind::Boiler:
        case hmi::Kind::Hopper: case hmi::Kind::CheckBox: case hmi::Kind::Clock: case hmi::Kind::ComboBox:
            return true;
        default:
            return false;
    }
}

bool isButtonKind(hmi::Kind k) {
    switch (k) {
        case hmi::Kind::Button: case hmi::Kind::PushButton: case hmi::Kind::IlluminatedButton: case hmi::Kind::Switch:
            return true;
        default:
            return false;
    }
}

// ---- la vue : les objets et leurs noms ---------------------------------------------------
const hmi::Object* byId(const hmi::View& v, hmi::Id id) {
    for (const auto& o : v.objects)
        if (o.id == id) return &o;
    return nullptr;
}
const hmi::Object* byName(const hmi::View& v, std::string_view name) {
    for (const auto& o : v.objects)
        if (o.name == name) return &o;
    std::string n(name);
    for (const auto& o : v.objects)
        if (norm(o.name) == norm(n) && !norm(n).empty()) return &o;
    return nullptr;
}

// Les cles par lesquelles une demande designe un objet, PAR RANG : son nom (exact, sans casse), puis son
// libelle ou son texte (courts), puis l'abrege "P1" d'un nom Pompe_1. Le premier rang qui repond l'emporte :
// "V1" est la vanne dont le libelle est V1, pas aussi Voyant_1 (abrege v1).
struct KeyHits {
    std::vector<hmi::Id> name, label, abbrev;
};
std::map<std::string, KeyHits> objectKeys(const hmi::View& v) {
    std::map<std::string, KeyHits> keys;
    const auto add = [&](std::vector<hmi::Id> KeyHits::*rank, const std::string& k, hmi::Id id) {
        if (k.empty() || isDigits(k)) return;
        auto& ids = keys[k].*rank;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    };
    for (const auto& o : v.objects) {
        add(&KeyHits::name, norm(o.name), o.id);
        for (const char* key : {"label", "text"}) {
            const std::string t = o.text(key);
            if (!t.empty() && t.size() <= 24) add(&KeyHits::label, norm(t), o.id);
        }
        // l'abrege : Pompe_12 -> p12 (la premiere lettre, le numero de la fin)
        const auto us = o.name.rfind('_');
        if (us != std::string::npos && us > 0 && us + 1 < o.name.size() && isDigits(o.name.substr(us + 1))
            && o.name.find('_') == us && std::isalpha(static_cast<unsigned char>(o.name[0])))
            add(&KeyHits::abbrev, std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(o.name[0])))) + o.name.substr(us + 1), o.id);
    }
    return keys;
}

// Un mot qui a la forme d'un nom d'objet : un identifiant (une lettre d'abord) avec un souligne, ou des
// lettres et des chiffres (Bouton_9, P12, V3) - pas une taille (200x100, x2), pas un nombre, pas une couleur.
bool looksLikeObjectName(const Tok& tk) {
    if (tk.quoted || tk.raw.empty() || !hmi::isIdentifier(tk.raw)) return false;
    if (!std::isalpha(static_cast<unsigned char>(tk.raw[0]))) return false;
    const bool digit = std::any_of(tk.raw.begin(), tk.raw.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    const bool under = tk.raw.find('_') != std::string::npos;
    if (!under && !digit) return false;
    // x2, x10 (un facteur) ; 200x100 commence par un chiffre (ecarte plus haut)
    if (!under && (tk.f[0] == 'x') && isDigits(tk.f.substr(1))) return false;
    return true;
}

// Le numero libre suivant pour un nom "Base_N" (1 + le plus grand deja pris), au moins `from`.
int nextNumber(const hmi::View& v, const std::string& base, const std::set<std::string>& reserved) {
    int best = 0;
    const auto look = [&](const std::string& n) {
        if (n.size() > base.size() + 1 && n.compare(0, base.size() + 1, base + "_") == 0 && isDigits(n.substr(base.size() + 1)))
            best = std::max(best, std::stoi(n.substr(base.size() + 1)));
    };
    for (const auto& o : v.objects) look(o.name);
    for (const auto& n : reserved) look(n);
    return best + 1;
}

bool nameFree(const hmi::View& v, const std::string& name, const std::set<std::string>& reserved) {
    if (reserved.count(name)) return false;
    return std::none_of(v.objects.begin(), v.objects.end(), [&](const hmi::Object& o) { return norm(o.name) == norm(name); });
}

std::string sanitize(std::string_view s) {
    std::string out;
    for (const char c : fold(s)) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

// ---- la place libre -----------------------------------------------------------------------
// Une grille de 20 px, une marge de 16 px autour de l'existant, dans le cadre de la vue.
struct Placer {
    const hmi::View&      v;
    std::vector<hmi::Box> taken;

    explicit Placer(const hmi::View& view) : v(view) {
        for (const auto& o : v.objects)
            if (o.parent == hmi::kNoId) {
                hmi::Box b = hmi::edit::rotatedBounds(o);
                if (hmi::kindIsSynoptic(o.kind) && o.text("labelPosition", "dessous") == "dessous" && !o.text("label").empty()) b.h += 20;
                taken.push_back(b);
            }
    }
    [[nodiscard]] bool inside(const hmi::Box& b) const { return b.x >= 0 && b.y >= 0 && b.right() <= v.width && b.bottom() <= v.height; }
    [[nodiscard]] bool clear(const hmi::Box& b) const {
        const hmi::Box g{b.x - 16, b.y - 16, b.w + 32, b.h + 32};
        return std::none_of(taken.begin(), taken.end(), [&](const hmi::Box& t) { return t.intersects(g); });
    }
    [[nodiscard]] double bottom() const {
        double b = 0;
        for (const auto& t : taken) b = std::max(b, t.bottom());
        return b;
    }
    // La premiere place libre (de haut en bas, de gauche a droite) pour un cadre w x h.
    hmi::Box find(double w, double h) const {
        for (double y = 40; y + h <= v.height; y += 20)
            for (double x = 40; x + w <= v.width; x += 20) {
                const hmi::Box b{x, y, w, h};
                if (clear(b)) return b;
            }
        return {40, std::round((bottom() + 40) / 20) * 20, w, h};   // plus de place : dessous (la validation le dira)
    }
    void take(const hmi::Box& b) { taken.push_back(b); }
};

// ---- ce que la demande contient ------------------------------------------------------------
struct Mention {
    std::size_t at{0}, len{0};
    hmi::Kind   kind{hmi::Kind::Rectangle};
    bool        plural{false};
    int         count{-1};        // un nombre dit avant (-1 : aucun)
    bool        definite{false};  // le, la, les, ce... : un objet qui existe
    bool        indefinite{false};// un, une, des, deux... : un objet neuf
    bool        all{false};       // tous, toutes
    bool        part{false};      // "le texte DE Bouton_1", "la couleur du fond" : une partie, pas un objet
};

struct NameRef {
    std::size_t          at{0}, len{0};
    std::vector<hmi::Id> ids;     // le premier rang qui repond (le nom, puis le libelle ou le texte, puis l'abrege)
    std::string          said;    // tel qu'ecrit ("V1")
};

enum class Rel : std::uint8_t { None, Right, Left, Below, Above };
struct Position {
    Rel         rel{Rel::None};
    std::size_t at{0};          // le premier mot de la position
    std::size_t refAt{0};       // le premier mot de l'objet de reference
    std::size_t end{0};         // apres l'objet de reference
    std::string missing;        // "a droite de Pompe_9" : un nom d'objet qui n'est pas dans la vue
};

struct Memory {
    std::vector<std::string> names;       // les objets crees ou cites au tour d'avant (dans l'ordre)
    std::vector<Operation>   creates;     // les creations du tour d'avant
    std::vector<Operation>   modifies;    // ses modifications
    std::string              request;
};

std::string typeWord(const std::string& f);   // un type dit (bool, reel...) -> BOOL, REAL ; vide : aucun

class Engine {
public:
    Engine(const PromptInput& in, std::string_view request, const std::vector<LocalTurn>& history)
        : in_(in), request_(request), t_(tokenize(request)) {
        f_ = " ";
        for (const auto& tk : t_) f_ += tk.f + " ";
        if (in_.view) keys_ = objectKeys(*in_.view);
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (!it->proposal) continue;
            mem_.request = it->request;
            for (const auto& op : it->proposal->ops) {
                if (op.action == Operation::Action::Create) {
                    mem_.creates.push_back(op);
                    mem_.names.push_back(op.name);
                } else if (op.action == Operation::Action::Modify || op.action == Operation::Action::Bind) {
                    if (op.action == Operation::Action::Modify) mem_.modifies.push_back(op);
                    mem_.names.push_back(op.name);
                } else {
                    for (const auto& n : op.objects) mem_.names.push_back(n);
                }
            }
            break;
        }
        scanMentions();
        scanNames();
    }

    LocalReply run();

private:
    const PromptInput&             in_;
    std::string                    request_;
    std::vector<Tok>               t_;
    std::string                    f_;   // les mots replies, entre des blancs : " ajoute 3 pompes "
    std::map<std::string, KeyHits> keys_;
    std::vector<Mention>           mentions_;
    std::vector<NameRef>           names_;
    Memory                         mem_;

    // ---- les mots
    [[nodiscard]] bool has(std::string_view w) const { return f_.find(" " + std::string(w) + " ") != std::string::npos; }
    [[nodiscard]] bool hasPrefix(std::string_view p) const {
        return std::any_of(t_.begin(), t_.end(), [&](const Tok& t) { return !t.quoted && t.f.rfind(p, 0) == 0; });
    }
    [[nodiscard]] bool phrase(std::string_view p) const { return f_.find(" " + std::string(p) + " ") != std::string::npos; }
    [[nodiscard]] std::size_t indexOf(std::string_view w, std::size_t from = 0) const {
        for (std::size_t i = from; i < t_.size(); ++i)
            if (t_[i].f == w) return i;
        return std::string::npos;
    }
    [[nodiscard]] std::size_t indexOfPrefix(std::string_view p, std::size_t from = 0) const {
        for (std::size_t i = from; i < t_.size(); ++i)
            if (!t_[i].quoted && t_[i].f.rfind(p, 0) == 0) return i;
        return std::string::npos;
    }

    // ---- le contexte
    [[nodiscard]] const hmi::View* view() const { return in_.view; }
    [[nodiscard]] const hmi::Object* object(hmi::Id id) const { return view() ? byId(*view(), id) : nullptr; }
    [[nodiscard]] std::vector<const hmi::Object*> selection() const {
        std::vector<const hmi::Object*> out;
        if (!view()) return out;
        for (const auto id : in_.selection)
            if (const auto* o = object(id)) out.push_back(o);
        return out;
    }
    [[nodiscard]] std::vector<const hmi::Object*> remembered() const {
        std::vector<const hmi::Object*> out;
        if (!view()) return out;
        for (const auto& n : mem_.names)
            if (const auto* o = byName(*view(), n); o && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
        return out;
    }
    [[nodiscard]] std::vector<const hmi::Object*> ofKind(hmi::Kind k) const {
        std::vector<const hmi::Object*> out;
        if (!view()) return out;
        for (const auto& o : view()->objects)
            if (sameFamily(k, o.kind)) out.push_back(&o);
        return out;
    }
    // "bouton" dit tous les boutons (a impulsion, lumineux, interrupteur) ; "vanne", aussi la vanne 3 voies
    [[nodiscard]] static bool sameFamily(hmi::Kind said, hmi::Kind k) {
        if (said == k) return true;
        if (said == hmi::Kind::Button) return isButtonKind(k);
        if (said == hmi::Kind::Valve) return k == hmi::Kind::ThreeWayValve;
        return false;
    }
    [[nodiscard]] std::string viewName() const { return view() ? guill(view()->name) : std::string("la vue"); }

    // ---- les variables (IHM, puis l'automate relie)
    struct Var {
        std::string name, type;
        bool        plc{false};
    };
    [[nodiscard]] std::vector<Var> variables() const {
        std::vector<Var> out;
        if (in_.project)
            for (const auto& v : in_.project->programs.variables) out.push_back({v.name, v.type, false});
        if (in_.env.plcVariables)
            for (const auto& [n, ty] : in_.env.plcVariables())
                if (std::none_of(out.begin(), out.end(), [&](const Var& v) { return norm(v.name) == norm(n); })) out.push_back({n, ty, true});
        return out;
    }
    [[nodiscard]] bool exists(const std::string& name) const { return in_.project && variableExists(*in_.project, name, in_.env); }

    // ---- les analyses
    void scanMentions();
    void scanNames();
    [[nodiscard]] const Mention* mentionAt(std::size_t i) const {
        for (const auto& m : mentions_)
            if (i >= m.at && i < m.at + m.len) return &m;
        return nullptr;
    }
    [[nodiscard]] const NameRef* nameAt(std::size_t i) const {
        for (const auto& n : names_)
            if (i >= n.at && i < n.at + n.len) return &n;
        return nullptr;
    }
    [[nodiscard]] Position position(std::size_t from = 0) const;
    struct Target {
        std::vector<const hmi::Object*> objects;
        std::string                     question;   // non vide : il faut demander
        std::string                     how;        // "la s\xC3\xA9lection", "la derniere cr\xC3\xA9\xC3\xA9e"
        std::vector<std::string>        suggestions;   // avec la question : la demande refaite sur chaque candidat
    };
    // Un nom de variable du projet ou de l'automate relie (pas un objet manquant).
    [[nodiscard]] bool isVariableName(const std::string& raw) const {
        if (exists(raw)) return true;
        if (in_.env.plcName && in_.env.plcName(raw)) return true;
        const auto vars = variables();
        return std::any_of(vars.begin(), vars.end(), [&](const Var& v) { return norm(v.name) == norm(raw); });
    }
    // Au rang i, un nom d'objet qui n'est pas dans la vue (Bouton_9, P7) : ni un objet, ni un genre, ni une variable.
    [[nodiscard]] bool unknownObjectAt(std::size_t i) const {
        return i < t_.size() && view() && looksLikeObjectName(t_[i]) && !nameAt(i) && !mentionAt(i) && !isVariableName(t_[i].raw);
    }
    // Au rang i, un mot a majuscule qui n'est rien de connu (ni objet, ni genre, ni couleur, ni variable, ni un mot
    // de la phrase : droite, haut, fois...).
    [[nodiscard]] bool unknownWordAt(std::size_t i) const {
        if (i >= t_.size() || !view()) return false;
        const Tok& tk = t_[i];
        if (tk.quoted || tk.f.size() < 3 || nameAt(i) || mentionAt(i)) return false;
        const auto c0 = static_cast<unsigned char>(tk.raw[0]);
        const bool capital = std::isupper(c0) || (c0 == 0xC3 && tk.raw.size() > 1 && static_cast<unsigned char>(tk.raw[1]) >= 0x80
                                                  && static_cast<unsigned char>(tk.raw[1]) <= 0x9E);
        if (!capital) return false;
        static const std::set<std::string> known = {"droite", "gauche", "haut", "bas", "centre", "milieu", "fois", "copie", "copies", "clair",
                                                    "claire", "fonce", "foncee", "merci", "ihm", "vue", "pixels", "pourcent", "marche", "arret"};
        if (known.count(tk.f) || stopWord(tk.f) || partWord(tk.f) || numberOf(tk.f) >= 0 || !colorAt(t_, i).empty() || !typeWord(tk.f).empty())
            return false;
        return !isVariableName(tk.raw);
    }
    // Les objets de la vue dont le nom ressemble a `said` (les plus proches d'abord).
    [[nodiscard]] std::vector<std::string> nearObjects(const std::string& said) const;
    // "Je ne trouve pas << Bouton_9 >> dans << Vue >>" et les plus proches ; la demande refaite sur eux.
    [[nodiscard]] Target missingObject(const std::string& said) const;
    // La demande, le mot `said` remplace par chaque nom (des suggestions courtes, a cliquer).
    [[nodiscard]] std::vector<std::string> retold(const std::string& said, const std::vector<std::string>& names) const;
    // Les objets designes entre [from, to) : les noms, les genres (les, la), "celle de droite", la
    // selection, le dernier objet cite. `kindHint` : un genre impose (aligner les boutons).
    [[nodiscard]] Target targets(std::size_t from, std::size_t to, bool many, bool fallback = true) const;
    [[nodiscard]] Target firstTarget(std::size_t from, std::size_t to) const;

    // ---- les reponses
    Proposal base(std::string summary) const {
        Proposal p;
        p.id = newId("op");
        p.view = view() ? view()->name : std::string{};
        p.viewId = view() ? view()->id : hmi::kNoId;
        p.fingerprint = view() ? fingerprint(*view()) : std::string{};
        p.request = request_;
        p.summary = std::move(summary);
        return p;
    }
    LocalReply finish(Proposal p, std::string text) const;
    LocalReply needView() const {
        LocalReply r;
        r.text = "Ouvre d'abord une vue dans l'\xC3\xA9" "diteur (double-clic sur une vue de l'arbre du projet) : je propose mes "
                 "modifications sur la vue de l'onglet ouvert.";
        return r;
    }
    LocalReply ask(std::string text, std::vector<std::string> suggestions = {}) const {
        LocalReply r;
        r.text = std::move(text);
        r.suggestions = std::move(suggestions);
        return r;
    }
    LocalReply notUnderstood() const;
    std::vector<std::string> examples() const;

    LocalReply help() const;
    LocalReply countQuestion() const;
    LocalReply listVariables() const;
    LocalReply listKinds() const;
    LocalReply listViews() const;
    LocalReply followUp() const;
    LocalReply synoptic() const;
    LocalReply createView() const;
    LocalReply createVariables() const;
    LocalReply createType() const;
    LocalReply bind() const;
    LocalReply animate() const;
    LocalReply remove() const;
    LocalReply duplicate() const;
    LocalReply arrange(bool align) const;
    LocalReply rename() const;
    LocalReply visibility(bool show) const;
    LocalReply move() const;
    LocalReply resize() const;
    LocalReply setText() const;
    LocalReply color() const;
    struct NewObject {
        hmi::Kind   kind;
        std::string text, color, name;
        std::string colorName;   // la couleur dite ("rouge", "bleu clair") ; vide : le #RRGGBB
    };
    // `into` : la vue neuve d'un creer_vue (les objets s'y posent) ; nul : la vue ouverte.
    LocalReply create(std::vector<NewObject> wanted, const Position& pos, std::string intro = {}, const hmi::View* into = nullptr) const;
    LocalReply createFromMentions(const hmi::View* into = nullptr) const;

    // Une variable dite : son nom exact s'il existe ; sinon les candidates.
    struct VarFind {
        std::string              name;        // trouvee (sure)
        std::string              asked;       // le nom ecrit (introuvable)
        std::vector<std::string> candidates;  // plusieurs possibles, ou les plus proches
    };
    [[nodiscard]] VarFind findVariable(std::size_t from, std::size_t to, const hmi::Object* about) const;
    [[nodiscard]] std::string colorKey(const hmi::Object& o, std::string* what = nullptr) const;
};

// ---- les genres dits, et leur determinant ---------------------------------------------------
void Engine::scanMentions() {
    const auto& words = kindWords();
    for (std::size_t i = 0; i < t_.size();) {
        if (t_[i].quoted || t_[i].raw.find('_') != std::string::npos) {
            ++i;
            continue;
        }
        bool found = false;
        // d'abord le mot exact (ou son pluriel) ; puis, seul, a une faute pres - pas une couleur, pas un mot
        // courant (aligne n'est pas une ligne, jaune pas une jauge), les deux premieres lettres justes
        static const std::set<std::string> common = {
            "aligne", "alignes", "ligne", "jaune", "cache", "monte", "texte", "teste", "pose", "place", "trace", "barre", "grande", "grand",
            "petite", "porte", "carte", "marche", "change", "changer", "changes", "modifie", "modifier", "ajoute", "ajouter", "supprime",
            "deplace", "renomme", "associe", "relie", "duplique", "copie", "montre", "affiche", "ecris", "ecrire", "rends", "agrandis",
            "reduis", "descends", "decale", "efface", "retire", "enleve", "repartis", "branche", "mettre", "creer", "cree", "pareil"};
        const bool typoOk = colorAt(t_, i).empty() && !common.count(t_[i].f) && !stopWord(t_[i].f);
        for (int pass = 0; pass < 2 && !found; ++pass)
        for (const auto& kw : words) {
            const std::size_t n = kw.words.size();
            if (i + n > t_.size()) continue;
            if (pass == 1 && (n != 1 || !typoOk || t_[i].f.size() < 2 || kw.words[0].compare(0, 2, t_[i].f, 0, 2) != 0)) continue;
            bool ok = true;
            for (std::size_t j = 0; j < n && ok; ++j) ok = !t_[i + j].quoted && sameWord(t_[i + j].f, kw.words[j], pass == 1);
            if (!ok) continue;
            Mention m;
            m.at = i;
            m.len = n;
            m.kind = kw.kind;
            m.plural = t_[i].f != kw.words[0] && (t_[i].f.back() == 's' || t_[i].f.back() == 'x');
            // le determinant, avant (des adjectifs sautes : nouvelle, autre, petite...)
            static const std::set<std::string> skip = {"nouvelle", "nouvelles", "nouveau", "nouveaux", "autre", "autres", "petit", "petite",
                                                       "petits", "petites", "grand", "grande", "grands", "grandes", "gros", "grosse", "seule"};
            std::size_t j = i;
            while (j > 0 && skip.count(t_[j - 1].f)) --j;
            if (j > 0) {
                const std::string& d = t_[j - 1].f;
                const int nb = numberOf(d);
                if (nb > 0 && d != "un" && d != "une") {
                    m.count = nb;
                    m.indefinite = true;
                    m.plural = m.plural || nb > 1;
                } else if (d == "un" || d == "une") {
                    m.count = 1;
                    m.indefinite = true;
                } else if (d == "des" || d == "quelques" || d == "plusieurs" || d == "d") {
                    m.indefinite = true;
                    m.plural = true;
                } else if (d == "le" || d == "la" || d == "les" || d == "l" || d == "du" || d == "au" || d == "aux" || d == "ce"
                           || d == "cet" || d == "cette" || d == "ces" || d == "son" || d == "sa" || d == "ses") {
                    m.definite = true;
                    if (d == "les" || d == "ces" || d == "aux" || d == "ses") m.plural = true;
                    if (j >= 2 && (t_[j - 2].f == "tous" || t_[j - 2].f == "toutes")) m.all = true;
                } else if (d == "tous" || d == "toutes") {
                    m.definite = true;
                    m.all = true;
                    m.plural = true;
                }
            }
            // "le texte DE Bouton_1", "le libelle de la pompe" : une propriete de l'objet qui suit, pas le genre Texte
            if (!m.indefinite && partWord(t_[i].f) && i + n < t_.size() && (t_[i + n].f == "de" || t_[i + n].f == "du" || t_[i + n].f == "d"))
                m.part = true;
            mentions_.push_back(m);
            i += n;
            found = true;
            break;
        }
        if (!found) ++i;
    }
}

// ---- les objets nommes ------------------------------------------------------------------------
void Engine::scanNames() {
    if (!view()) return;
    for (std::size_t i = 0; i < t_.size();) {
        bool found = false;
        // "le texte DE ...", "le fond de ...", "le contour de ..." : une propriete, pas un objet (meme si un
        // objet a pour texte "Texte")
        if (partWord(t_[i].f) && i + 1 < t_.size() && (t_[i + 1].f == "de" || t_[i + 1].f == "du" || t_[i + 1].f == "d")
            && (i == 0 || !(t_[i - 1].f == "un" || t_[i - 1].f == "une"))) {
            ++i;
            continue;
        }
        for (std::size_t n = std::min<std::size_t>(4, t_.size() - i); n >= 1; --n) {
            // une fenetre de mots : pas un mot vide seul, pas un nombre seul
            if (n == 1 && (stopWord(t_[i].f) || isDigits(t_[i].f) || numberOf(t_[i].f) > 0)) break;
            if (stopWord(t_[i].f) || (n > 1 && stopWord(t_[i + n - 1].f))) continue;
            bool quotedIn = false;
            for (std::size_t j = 0; j < n; ++j) quotedIn = quotedIn || t_[i + j].quoted;
            if (quotedIn) continue;
            std::string key;
            for (std::size_t j = 0; j < n; ++j) key += norm(t_[i + j].raw);
            const auto it = keys_.find(key);
            if (it == keys_.end()) continue;
            const KeyHits& h = it->second;
            std::string said;
            for (std::size_t j = 0; j < n; ++j) said += (j ? " " : "") + t_[i + j].raw;
            names_.push_back({i, n, !h.name.empty() ? h.name : !h.label.empty() ? h.label : h.abbrev, said});
            i += n;
            found = true;
            break;
        }
        if (!found) ++i;
    }
}

Position Engine::position(std::size_t from) const {
    Position p;
    for (std::size_t i = from; i < t_.size(); ++i) {
        const std::string& w = t_[i].f;
        Rel r = Rel::None;
        std::size_t next = i + 1;
        if ((w == "droite" || w == "gauche") && i > 0 && (t_[i - 1].f == "a" || t_[i - 1].f == "la")) {
            r = w == "droite" ? Rel::Right : Rel::Left;
        } else if (w == "cote" && i > 0 && t_[i - 1].f == "a") {
            r = Rel::Right;
        } else if (w == "pres") {
            r = Rel::Right;
        } else if (w == "sous" || w == "dessous" || w == "en-dessous") {
            r = Rel::Below;
        } else if (w == "dessus" || w == "audessus") {
            r = Rel::Above;
        }
        if (r == Rel::None) continue;
        // "a droite DE <objet>" : il faut un objet apres ; "vers la droite" seul n'est pas une position relative
        while (next < t_.size() && (t_[next].f == "de" || t_[next].f == "d" || t_[next].f == "du" || t_[next].f == "des"
                                    || t_[next].f == "la" || t_[next].f == "le" || t_[next].f == "l" || t_[next].f == "les"
                                    || t_[next].f == "cette" || t_[next].f == "ce" || t_[next].f == "chaque"))
            ++next;
        if (next >= t_.size()) continue;
        if (!nameAt(next) && !mentionAt(next)) {
            // "a droite de Pompe_9" : un nom d'objet qui n'est pas dans la vue - dit, jamais remplace
            if (unknownObjectAt(next)) {
                p.rel = r;
                p.at = i > 0 && (t_[i - 1].f == "a" || t_[i - 1].f == "la" || t_[i - 1].f == "au") ? i - 1 : i;
                p.refAt = next;
                p.end = next + 1;
                p.missing = t_[next].raw;
                return p;
            }
            continue;
        }
        p.rel = r;
        p.at = i > 0 && (t_[i - 1].f == "a" || t_[i - 1].f == "la" || t_[i - 1].f == "au") ? i - 1 : i;
        p.refAt = next;
        std::size_t end = next;
        if (const auto* m = mentionAt(next)) end = m->at + m->len;
        if (const auto* n = nameAt(end)) end = n->at + n->len;
        else if (const auto* n2 = nameAt(next)) end = std::max(end, n2->at + n2->len);
        p.end = end;
        return p;
    }
    return p;
}

// ---- les objets designes ----------------------------------------------------------------------
Engine::Target Engine::targets(std::size_t from, std::size_t to, bool many, bool fallback) const {
    Target t;
    if (!view()) return t;
    to = std::min(to, t_.size());
    std::vector<const hmi::Object*> out;
    const auto push = [&](const hmi::Object* o) {
        if (o && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
    };
    // "celle de droite", "celui du haut" : parmi la selection, sinon les derniers cites, sinon le genre dit
    std::string side;
    for (std::size_t i = from; i + 2 < to + 1 && i + 2 <= t_.size(); ++i) {
        const std::string& a = t_[i].f;
        if (a == "celle" || a == "celui" || a == "celles" || a == "ceux" || a == "le" || a == "la") {
            const std::string& b = i + 1 < t_.size() ? t_[i + 1].f : std::string{};
            const std::string& c = i + 2 < t_.size() ? t_[i + 2].f : std::string{};
            if ((b == "de" || b == "du") && (c == "droite" || c == "gauche" || c == "haut" || c == "bas")) side = c;
            if (b == "plus" && (c == "a" || c == "en") && i + 3 < t_.size()) {
                const std::string& d = t_[i + 3].f;
                if (d == "droite" || d == "gauche" || d == "haut" || d == "bas") side = d;
            }
        }
    }
    // un nom d'objet qui n'est pas dans la vue (Bouton_9) : dit, avec les plus proches - jamais remplace en
    // silence par le dernier objet cite
    for (std::size_t i = from; i < to; ++i)
        if (unknownObjectAt(i)) return missingObject(t_[i].raw);
    bool namedSomething = false;
    (void)many;
    for (const auto& n : names_) {
        if (n.at < from || n.at >= to) continue;
        namedSomething = true;
        std::vector<const hmi::Object*> hits;
        for (const auto id : n.ids)
            if (const auto* o = object(id)) hits.push_back(o);
        // le genre dit juste avant : "les boutons Marche" (un pluriel) les prend tous ; "la pompe P1" restreint
        const Mention* before = nullptr;
        for (const auto& m : mentions_)
            if (m.at + m.len == n.at && !m.part) before = &m;
        if (hits.size() > 1 && before && (before->plural || before->all)) {
            for (const auto* o : hits) push(o);
            continue;
        }
        if (hits.size() > 1 && before) {
            std::vector<const hmi::Object*> same;
            for (const auto* o : hits)
                if (sameFamily(before->kind, o->kind)) same.push_back(o);
            if (!same.empty()) hits = same;
        }
        // la selection departage
        if (hits.size() > 1) {
            std::vector<const hmi::Object*> chosen;
            for (const auto* o : hits)
                if (std::find(in_.selection.begin(), in_.selection.end(), o->id) != in_.selection.end()) chosen.push_back(o);
            if (chosen.size() == 1) hits = chosen;
        }
        if (hits.size() == 1) {
            push(hits.front());
            continue;
        }
        // une designation au singulier qui repond a plusieurs objets : demander, jamais les prendre tous
        std::vector<std::string> list;
        for (const auto* o : hits) list.push_back(o->name);
        t.question = guill(n.said) + " d\xC3\xA9signe " + std::to_string(list.size()) + " objets dans " + viewName() + " : " + codes(list)
                     + ". Lequel ? \xC3\x89" "cris son nom exact (par exemple " + code(list.front()) + "), ou s\xC3\xA9lectionne-le, puis redemande.";
        t.suggestions = retold(n.said, list);
        return t;
    }
    for (const auto& m : mentions_) {
        if (m.at < from || m.at >= to) continue;
        if (m.part) continue;   // "le texte DE ...", "le fond de ..." : une partie de l'objet qui suit
        // un genre suivi d'un nom (la pompe P1) : le nom suffit ; "le texte DE Bouton_1" : une partie, pas un objet
        if (nameAt(m.at + m.len)) continue;
        if (m.at + m.len < t_.size() && (t_[m.at + m.len].f == "de" || t_[m.at + m.len].f == "du" || t_[m.at + m.len].f == "d")
            && std::any_of(names_.begin(), names_.end(), [&](const NameRef& n) { return n.at >= from && n.at < to; }))
            continue;
        if (m.indefinite && !m.definite) continue;
        namedSomething = true;
        auto all = ofKind(m.kind);
        // la selection d'abord : "les pompes" choisies, "la cuve" choisie
        std::vector<const hmi::Object*> chosen;
        for (const auto* o : selection())
            if (sameFamily(m.kind, o->kind)) chosen.push_back(o);
        if (!m.all && !chosen.empty()) all = chosen;
        if (all.empty()) {
            t.question = "Il n'y a pas de " + lowerFirst(std::string(hmi::kindLabel(m.kind))) + " dans " + viewName() + ".";
            return t;
        }
        if (!side.empty() && all.size() > 1) {
            const auto key = [&](const hmi::Object* o) {
                const auto b = hmi::edit::rotatedBounds(*o);
                return side == "droite" ? b.cx() : side == "gauche" ? -b.cx() : side == "bas" ? b.cy() : -b.cy();
            };
            push(*std::max_element(all.begin(), all.end(), [&](const auto* a, const auto* b) { return key(a) < key(b); }));
            t.how = "celui " + std::string(side == "droite" || side == "gauche" ? "de " : "du ") + side;
            side.clear();
            continue;
        }
        if (m.plural || m.all) {
            for (const auto* o : all) push(o);
        } else if (all.size() == 1) {
            push(all.front());
        } else {
            std::vector<std::string> list;
            for (const auto* o : all) list.push_back(o->name);
            t.question = "Il y a " + countKind(m.kind, all.size()) + " dans " + viewName() + " : " + codes(list)
                         + ". Laquelle ? \xC3\x89" "cris son nom, ou s\xC3\xA9lectionne-la, puis redemande.";
            return t;
        }
    }
    // rien de nomme, mais un mot ecrit comme un nom (une majuscule, pas le premier mot : "Mets Demarreur en rouge") qui
    // n'est ni un objet, ni un genre, ni un mot connu : dit aussi, plutot que le dernier objet cite
    if (out.empty() && !namedSomething)
        for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i)
            if (unknownWordAt(i)) return missingObject(t_[i].raw);
    if (out.empty() && fallback && !namedSomething) {
        auto sel = selection();
        const bool selWords = phrase("la selection") || phrase("les objets choisis") || has("selectionnes") || has("selectionnee")
                           || has("selectionne") || has("selectionnees");
        if (!sel.empty()) {
            t.how = "la s\xC3\xA9lection";
            if (!side.empty() && sel.size() > 1) {
                const auto key = [&](const hmi::Object* o) {
                    const auto b = hmi::edit::rotatedBounds(*o);
                    return side == "droite" ? b.cx() : side == "gauche" ? -b.cx() : side == "bas" ? b.cy() : -b.cy();
                };
                out.push_back(*std::max_element(sel.begin(), sel.end(), [&](const auto* a, const auto* b) { return key(a) < key(b); }));
                t.how = "celui " + std::string(side == "droite" || side == "gauche" ? "de " : "du ") + side + " de la s\xC3\xA9lection";
            } else {
                out = sel;
            }
        } else if (!selWords) {
            auto rem = remembered();
            if (!rem.empty()) {
                if (!side.empty() && rem.size() > 1) {
                    const auto key = [&](const hmi::Object* o) {
                        const auto b = hmi::edit::rotatedBounds(*o);
                        return side == "droite" ? b.cx() : side == "gauche" ? -b.cx() : side == "bas" ? b.cy() : -b.cy();
                    };
                    out.push_back(*std::max_element(rem.begin(), rem.end(), [&](const auto* a, const auto* b) { return key(a) < key(b); }));
                } else {
                    out = many ? rem : std::vector<const hmi::Object*>{rem.back()};
                    if (!many && rem.size() > 1) out = {rem.front()};
                }
                t.how = "le dernier objet cit\xC3\xA9";
            }
        }
        if (out.empty()) {
            t.question = "Quel objet ? \xC3\x89" "cris son nom (par exemple " + std::string(view()->objects.empty() ? "`Pompe_1`" : code(view()->objects.front().name))
                         + "), ou s\xC3\xA9lectionne-le dans la vue, puis redemande.";
            return t;
        }
    }
    t.objects = std::move(out);
    return t;
}

Engine::Target Engine::firstTarget(std::size_t from, std::size_t to) const {
    Target t = targets(from, to, false);
    if (t.objects.size() > 1) t.objects.resize(1);
    return t;
}

std::vector<std::string> Engine::nearObjects(const std::string& said) const {
    std::vector<std::pair<std::size_t, std::string>> scored;
    if (!view()) return {};
    const std::string ns = norm(said);
    const std::size_t limit = std::max<std::size_t>(2, ns.size() / 2);
    for (const auto& o : view()->objects) {
        std::size_t d = distance(norm(o.name), ns);
        for (const char* key : {"label", "text"})
            if (const std::string tx = o.text(key); !tx.empty() && tx.size() <= 24) d = std::min(d, distance(norm(tx), ns));
        if (d <= limit) scored.emplace_back(d, o.name);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first || (a.first == b.first && a.second < b.second); });
    std::vector<std::string> out;
    for (const auto& [d, n] : scored)
        if (out.size() < 4) out.push_back(n);
    return out;
}

Engine::Target Engine::missingObject(const std::string& said) const {
    Target t;
    const auto near = nearObjects(said);
    t.question = "Je ne trouve pas " + guill(said) + " dans la vue " + viewName()
                 + (near.empty() ? std::string(".") : " ; les plus proches : " + codes(near) + ".")
                 + " Je ne le remplace pas par un autre objet : \xC3\xA9" "cris le nom exact, ou s\xC3\xA9lectionne l'objet, puis redemande.";
    t.suggestions = retold(said, near);
    return t;
}

// Le nombre de caracteres (pas d'octets) d'un texte UTF-8.
std::size_t chars(std::string_view s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}
constexpr std::size_t kSuggestionMax = 45;   // une suggestion tient sur une ligne du panneau

std::vector<std::string> Engine::retold(const std::string& said, const std::vector<std::string>& names) const {
    std::vector<std::string> out;
    const auto at = request_.find(said);
    if (at == std::string::npos) return out;
    for (const auto& n : names) {
        std::string s = request_;
        s.replace(at, said.size(), n);
        while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
        if (chars(s) <= kSuggestionMax && out.size() < 3) out.push_back(std::move(s));
    }
    return out;
}

// ---- les variables ----------------------------------------------------------------------------
Engine::VarFind Engine::findVariable(std::size_t from, std::size_t to, const hmi::Object* about) const {
    VarFind r;
    const auto vars = variables();
    to = std::min(to, t_.size());
    // un nom ecrit tel quel (Pompe_1_Marche, PT101, API.Moteur) : exact, ou sans casse ni soulignes
    std::vector<std::string> words;
    for (std::size_t i = from; i < to; ++i) {
        const Tok& tk = t_[i];
        if (tk.quoted) continue;
        // l'objet vise lui-meme (son nom ecrit) n'est pas la variable ; un mot qui est aussi le texte d'un autre
        // objet (Marche) peut l'etre
        if (const auto* n = nameAt(i); n && about && tk.raw.find('_') == std::string::npos
                                       && std::find(n->ids.begin(), n->ids.end(), about->id) != n->ids.end())
            continue;
        const bool identifier = tk.raw.find('_') != std::string::npos || tk.raw.find('.') != std::string::npos
                             || (std::any_of(tk.raw.begin(), tk.raw.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })
                                 && std::any_of(tk.raw.begin(), tk.raw.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }))
                             || (tk.raw.size() >= 2 && std::all_of(tk.raw.begin(), tk.raw.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)); }));
        if (identifier) {
            if (exists(tk.raw)) {
                // le nom tel que le projet l'ecrit
                for (const auto& v : vars)
                    if (norm(v.name) == norm(tk.raw)) {
                        r.name = v.name;
                        return r;
                    }
                r.name = tk.raw;
                return r;
            }
            for (const auto& v : vars)
                if (norm(v.name) == norm(tk.raw)) {
                    r.name = v.name;
                    return r;
                }
            // une faute (une lettre) sur un long nom : sure si une seule ressemble
            std::vector<std::string> close;
            for (const auto& v : vars)
                if (norm(tk.raw).size() >= 6 && distance(norm(v.name), norm(tk.raw)) <= 1) close.push_back(v.name);
            if (close.size() == 1) {
                r.name = close.front();
                return r;
            }
            r.asked = tk.raw;
            if (in_.project) r.candidates = nearVariables(*in_.project, tk.raw);
            if (r.candidates.empty())
                for (const auto& c : close) r.candidates.push_back(c);
            return r;
        }
        if (!stopWord(tk.f) && tk.f.size() >= 3 && numberOf(tk.f) < 0 && colorAt(t_, i).empty()) words.push_back(tk.f);
    }
    if (words.empty()) return r;
    // des mots (le niveau, la marche de la pompe 1) : les variables qui les contiennent tous
    const auto varWords = [](const std::string& n) {
        std::vector<std::string> out;
        std::string w;
        for (const char c : fold(n)) {
            if (std::isalpha(static_cast<unsigned char>(c))) w += c;
            else {
                if (!w.empty()) out.push_back(w);
                w.clear();
                if (std::isdigit(static_cast<unsigned char>(c))) out.push_back(std::string(1, c));
            }
        }
        if (!w.empty()) out.push_back(w);
        return out;
    };
    struct Scored {
        int         score;
        std::string name;
    };
    std::vector<Scored> scored;
    for (const auto& v : vars) {
        const auto vw = varWords(v.name);
        int hit = 0;
        bool all = true;
        for (const auto& w : words) {
            const bool in = std::any_of(vw.begin(), vw.end(), [&](const std::string& x) {
                return x == w || (w.size() >= 4 && x.rfind(w.substr(0, 4), 0) == 0) || sameWord(w, x, true);
            });
            hit += in ? 1 : 0;
            all = all && in;
        }
        if (!all) continue;
        int score = hit * 10;
        // l'objet vise aide a choisir : Cuve_1 -> Niveau_Cuve_1 plutot que Niveau_Cuve_2 (son numero ne
        // compte que si un de ses mots concorde aussi : Vanne_1 ne choisit pas Pompe_1_Marche)
        if (about) {
            int alpha = 0, digit = 0;
            for (const auto& w : varWords(about->name))
                if (std::find(vw.begin(), vw.end(), w) != vw.end()) (std::isdigit(static_cast<unsigned char>(w[0])) ? digit : alpha) += 1;
            score += alpha * 3 + (alpha > 0 ? digit * 3 : 0);
        }
        scored.push_back({score, v.name});
    }
    if (scored.empty()) {
        r.asked = words.front();
        if (in_.project) r.candidates = nearVariables(*in_.project, words.front());
        return r;
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    if (scored.size() == 1 || scored[0].score > scored[1].score) {
        r.name = scored[0].name;
        return r;
    }
    for (const auto& s : scored)
        if (s.score == scored[0].score && r.candidates.size() < 6) r.candidates.push_back(s.name);
    return r;
}

// La propriete de couleur d'un objet : celle qui se voit le plus (le liquide d'une cuve, la couleur
// en marche d'un symbole, le remplissage, le texte).
std::string Engine::colorKey(const hmi::Object& o, std::string* what) const {
    const auto pick = [&](const char* key, const char* label) {
        if (!o.find(key)) return false;
        if (what) *what = label;
        return true;
    };
    // la partie dite : le texte, le fond, le contour, a l'arret, en defaut
    if (has("texte") || phrase("du texte") || has("ecriture")) {
        if (pick("textColor", "la couleur de son texte")) return "textColor";
    }
    if (has("contour") || has("bordure") || has("bord")) {
        if (pick("stroke", "son contour")) return "stroke";
    }
    if (has("fond") || has("remplissage")) {
        if (pick("fill", "son fond")) return "fill";
    }
    if (has("arret") || has("eteint") || has("eteinte")) {
        if (pick("colorOff", "sa couleur \xC3\xA0 l'arr\xC3\xAAt")) return "colorOff";
    }
    if (has("defaut")) {
        if (pick("colorFault", "sa couleur en d\xC3\xA9" "faut")) return "colorFault";
    }
    if (hmi::kindHasLevel(o.kind) && pick("fillColor", "la couleur de son contenu")) return "fillColor";
    if (o.find("colorOn") && pick("colorOn", "sa couleur en marche")) return "colorOn";
    if (o.kind == hmi::Kind::Text && pick("textColor", "la couleur de son texte")) return "textColor";
    if (pick("fill", "son remplissage")) return "fill";
    if (pick("textColor", "la couleur de son texte")) return "textColor";
    if (pick("stroke", "son trait")) return "stroke";
    return {};
}

// ---- la fin d'une reponse : la verification ---------------------------------------------------
LocalReply Engine::finish(Proposal p, std::string text) const {
    LocalReply r;
    // LA PORTEE, comme le chemin des IA en ligne (proposalFromTool) : une proposition qui commence par creer_vue
    // vise la vue neuve (son nom ; aucun id tant qu'elle n'existe pas) ; une qui ne cree que des variables ou des
    // types ne vise aucune vue.
    if (createsView(p)) {
        p.view = newViewName(p);
        p.viewId = hmi::kNoId;
    } else if (!usesOpenView(p)) {
        p.view.clear();
        p.viewId = hmi::kNoId;
    }
    // UNE OPERATION QUI NE CHANGE AUCUNE VALEUR NE SE PROPOSE PAS : une propriete deja a cette valeur (et sans
    // formule) est retiree ; une modification vide aussi.
    if (view() && usesOpenView(p) && !createsView(p)) {
        std::vector<std::string> same;
        bool onlyModify = true;
        for (auto& op : p.ops) {
            if (op.action != Operation::Action::Modify) {
                onlyModify = false;
                continue;
            }
            const auto* o = byName(*view(), op.name);
            if (!o) continue;
            const bool unchanged = !op.props.empty() && std::all_of(op.props.begin(), op.props.end(), [&](const auto& kv) {
                const auto* pr = o->find(kv.first);
                if (!pr || !pr->expr.empty()) return false;
                if (pr->value == kv.second) return true;
                // un nombre (200 et 200.0), une couleur (#e5534b et #E5534B)
                std::string a = pr->value, b = kv.second;
                for (auto* s : {&a, &b})
                    for (auto& c : *s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (a == b) return true;
                char* ea = nullptr;
                char* eb = nullptr;
                const double da = std::strtod(pr->value.c_str(), &ea), db = std::strtod(kv.second.c_str(), &eb);
                return !pr->value.empty() && !kv.second.empty() && ea && *ea == '\0' && eb && *eb == '\0' && da == db;
            });
            if (unchanged) {
                same.push_back(op.name);
                op.props.clear();
            }
        }
        std::erase_if(p.ops, [](const Operation& op) { return op.action == Operation::Action::Modify && op.props.empty(); });
        if (p.ops.empty() && onlyModify && !same.empty())
            return ask("Rien ne changerait : " + codes(same) + (same.size() > 1 ? " ont" : " a") + " d\xC3\xA9j\xC3\xA0 ces valeurs. Je ne propose rien.");
    }
    if (in_.project) {
        // l'empreinte de la proposition : la vue ouverte, et ce qu'elle cree (une vue, des variables, des types)
        p.fingerprint = proposalPrint(*in_.project, p);
        const Check ck = validate(*in_.project, p, in_.env);
        std::vector<std::string> real;
        // ce que le contrat ne valide pas encore (creer_vue, creer_variables ; une formule "=..." dans une
        // propriete) : la validation complete en decidera, la carte le dira
        for (const auto& e : ck.errors)
            if (e.find("pas encore disponible") == std::string::npos && e.find(" = \xC2\xAB =") == std::string::npos) real.push_back(e);
        if (!real.empty()) {
            r.text = "Je n'ai pas pu pr\xC3\xA9parer une proposition valable : " + real.front()
                     + (real.size() > 1 ? " (et " + plural(real.size() - 1, "autre probl\xC3\xA8me", "autres probl\xC3\xA8mes") + ")" : std::string{})
                     + "\nReformule, ou choisis l'objet dans la vue.";
            r.suggestions = examples();
            r.understood = false;   // 1.16.1 : mal comprise - une IA connectee peut la reprendre
            return r;
        }
        // LA PLACE (l'aide : un objet qui deborde de la vue est signale) : la verification le dit, la reponse aussi
        // - sauf si le texte le dit deja
        std::vector<std::string> outside;
        const std::string marker = " d\xC3\xA9" "passe du cadre de la vue";
        for (const auto& w : ck.warnings)
            if (const auto at = w.find(marker); at != std::string::npos && at > 0) outside.push_back(w.substr(0, at));
        if (!outside.empty() && text.find("cadre") == std::string::npos)
            text += "\nAttention : " + codes(outside, 6) + (outside.size() > 1 ? " d\xC3\xA9passeront" : " d\xC3\xA9passera")
                    + " du cadre de la vue (l'aper\xC3\xA7u le montre).";
    }
    r.text = std::move(text);
    r.proposal = std::move(p);
    return r;
}

std::vector<std::string> Engine::examples() const {
    // des exemples sur les VRAIS noms de la vue (jamais un objet qui n'existe pas) ; une vue vide : creer.
    // Courts : une suggestion tient sur une ligne du panneau (kSuggestionMax caracteres).
    std::string pump, any;
    if (view()) {
        for (const auto& o : view()->objects) {
            if (any.empty() && o.parent == hmi::kNoId) any = o.name;
            if (pump.empty() && o.kind == hmi::Kind::Pump) pump = o.name;
        }
        for (const auto& o : view()->objects)
            if (pump.empty() && hmi::kindIsSynoptic(o.kind) && o.find("variable")) pump = o.name;
    }
    if (pump.empty()) pump = any;
    std::vector<std::string> out;
    if (pump.empty()) {
        out = {"Ajoute 3 pompes", "Cr\xC3\xA9" "e une cuve et un bargraphe", "Synoptique de 2 cuves et 2 pompes par cuve",
               "Ajoute deux boutons Marche et Arr\xC3\xAAt", "Que sais-tu faire ?"};
    } else {
        std::string var;
        if (in_.project && !in_.project->programs.variables.empty()) var = in_.project->programs.variables.front().name;
        out = {"Ajoute 3 pompes", "Mets " + pump + " en rouge"};
        if (!var.empty()) out.push_back("Associe " + pump + " \xC3\xA0 " + var);
        out.push_back("Ajoute un voyant vert \xC3\xA0 droite de " + pump);
        out.push_back("Que sais-tu faire ?");
    }
    std::erase_if(out, [](const std::string& s) { return chars(s) > kSuggestionMax; });
    return out;
}

LocalReply Engine::notUnderstood() const {
    LocalReply r;
    r.text = "Je n'ai pas compris cette demande. Je suis l'**IA int\xC3\xA9gr\xC3\xA9" "e** : je comprends des phrases simples sur la vue "
             "ouverte (cr\xC3\xA9" "er, modifier, associer, aligner, supprimer...), pas tout le fran\xC3\xA7" "ais. Essaie une de ces "
             "formulations, ou demande-moi **ce que je sais faire**.";
    r.suggestions = examples();
    r.understood = false;
    return r;
}

// ---- les questions -----------------------------------------------------------------------------
LocalReply Engine::help() const {
    LocalReply r;
    // les formulations entre `...` : courtes (kSuggestionMax caracteres), le panneau ne les coupe pas
    r.text = "## Ce que je comprends\n"
             "Je suis l'**IA int\xC3\xA9gr\xC3\xA9" "e** au logiciel : sans connexion, sans compte, rien ne quitte ce PC. Je comprends des "
             "phrases simples sur **la vue ouverte** :\n"
             "- **cr\xC3\xA9" "er** des objets : `Ajoute 3 pompes`, `Mets deux boutons Marche et Arr\xC3\xAAt sous P1`, "
             "`Ajoute un voyant rouge \xC3\xA0 droite de la cuve` ;\n"
             "- un **synoptique** : `Synoptique de 3 cuves et 2 pompes par cuve` ;\n"
             "- **modifier** : `Mets P1 en rouge`, `Change le texte de Bouton_1 en D\xC3\xA9marrer`, `Agrandis la cuve`, "
             "`D\xC3\xA9place V1 \xC3\xA0 droite de P2`, `Monte P1 de 50`, `Cache V1`, `Renomme Cuve_1 en Bac_Eau` ;\n"
             "- **associer** une variable : `Associe P1 \xC3\xA0 Pompe_1_Marche`, `Relie la cuve au niveau` ;\n"
             "- **cr\xC3\xA9" "er des variables**, **un type**, **une vue** : `Cr\xC3\xA9" "e la variable Pompe_3_Marche en BOOL`, "
             "`Cr\xC3\xA9" "e un type T_Pompe avec Marche en BOOL`, `Cr\xC3\xA9" "e un type pour une pompe` (un mod\xC3\xA8le de d\xC3\xA9part : "
             "capteur, pompe, vanne, moteur, variateur, cuve...), `Cr\xC3\xA9" "e les variables de l'automate`, "
             "`Cr\xC3\xA9" "e une vue Vue_Process en 1920x1080` ;\n"
             "- **aligner, r\xC3\xA9partir, dupliquer, supprimer** : `Aligne les boutons en haut`, `R\xC3\xA9partis les pompes`, "
             "`Duplique P1 3 fois vers la droite`, `Supprime les vannes` ;\n"
             "- **animer** : `Fais clignoter V1 quand Defaut_V1`, `P1 verte si Pompe_1_Marche sinon rouge` ;\n"
             "- des **questions** : `Combien d'objets ?`, `Quelles variables ?`, `Quels genres d'objets ?`.\n"
             "Je cherche les noms dans ton projet (sans casse ni accents, `P1` pour `Pompe_1`) et **je n'invente jamais une "
             "variable** ; un objet que je ne trouve pas, je le dis. Chaque proposition passe par l'aper\xC3\xA7u, et **Ctrl+Z** la "
             "reprend d'un coup. Pour des demandes plus libres, branche une IA en ligne ou sur ce PC (Param\xC3\xA8tres de "
             "l'Assistant IA).";
    r.suggestions = examples();
    return r;
}

LocalReply Engine::countQuestion() const {
    if (has("variable") || has("variables")) {
        const auto vars = variables();
        std::size_t plc = 0;
        for (const auto& v : vars) plc += v.plc ? 1 : 0;
        return ask("Le projet a **" + plural(vars.size() - plc, "variable", "variables") + "** dans l'IHM"
                   + (plc > 0 ? std::string(", et l'automate ") + (in_.env.plcSource && !in_.env.plcSource().empty() ? "de **" + in_.env.plcSource() + "**" : std::string("reli\xC3\xA9"))
                                    + " en donne " + std::to_string(plc) + " de plus" : std::string{}) + ".");
    }
    if (!view()) return needView();
    for (const auto& m : mentions_) {
        const auto all = ofKind(m.kind);
        std::vector<std::string> names;
        for (const auto* o : all) names.push_back(o->name);
        return ask("Il y a **" + countKind(m.kind, all.size()) + "** dans " + viewName() + (names.empty() ? std::string(".") : " : " + codes(names) + "."));
    }
    std::map<hmi::Kind, std::size_t> byKind;
    std::size_t top = 0;
    for (const auto& o : view()->objects) {
        ++byKind[o.kind];
        top += o.parent == hmi::kNoId ? 1 : 0;
    }
    std::vector<std::pair<hmi::Kind, std::size_t>> list(byKind.begin(), byKind.end());
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    // accordes : "9 pompes, 2 boutons \xC3\xA0 impulsion, 2 afficheurs num\xC3\xA9riques"
    std::vector<std::string> parts;
    for (const auto& [k, n] : list) parts.push_back(countKind(k, n));
    return ask("La vue " + viewName() + " a **" + plural(view()->objects.size(), "objet", "objets") + "**"
               + (top != view()->objects.size() ? " (" + std::to_string(top) + " au premier niveau)" : std::string{})
               + (parts.empty() ? std::string(".") : " : " + joinAnd(parts, 10) + "."));
}

LocalReply Engine::listVariables() const {
    const auto vars = variables();
    if (vars.empty()) return ask("Le projet n'a **aucune variable** pour l'instant. Tu peux en cr\xC3\xA9" "er : `Cr\xC3\xA9" "e une variable Niveau_Cuve REAL`.");
    // un filtre : "quelles variables de niveau"
    std::vector<std::string> filter;
    for (const auto& tk : t_)
        if (!stopWord(tk.f) && tk.f != "quelles" && tk.f != "quels" && tk.f != "liste" && tk.f != "existe" && tk.f != "existent"
            && tk.f != "donne" && tk.f != "montre" && tk.f != "projet" && tk.f != "ihm" && tk.f.size() >= 3)
            filter.push_back(tk.f);
    std::vector<std::string> shown;
    std::size_t matched = 0;
    for (const auto& v : vars) {
        const std::string n = fold(v.name);
        if (!filter.empty() && std::none_of(filter.begin(), filter.end(), [&](const std::string& w) { return n.find(w) != std::string::npos; })) continue;
        ++matched;
        if (shown.size() < 30) shown.push_back(code(v.name) + " (" + v.type + (v.plc ? ", automate" : "") + ")");
    }
    if (matched == 0) {
        std::vector<std::string> all;
        for (const auto& v : vars)
            if (all.size() < 12) all.push_back(code(v.name));
        return ask("Aucune variable ne contient " + joinAnd(filter) + ". Les variables du projet : " + joinAnd(all, 12) + ".");
    }
    std::string text = "Le projet a **" + plural(matched, "variable", "variables") + "**" + (filter.empty() ? std::string{} : " qui r\xC3\xA9pondent")
                       + " :\n";
    for (const auto& s : shown) text += "- " + s + "\n";
    if (matched > shown.size()) text += "- \xE2\x80\xA6 et " + std::to_string(matched - shown.size()) + " autres.\n";
    text += "Je ne lis jamais leur valeur en marche : seulement leur nom et leur type.";
    return ask(std::move(text));
}

LocalReply Engine::listKinds() const {
    // LE COMPTE DU REGISTRE (AiRegistry : tous les genres du logiciel) ; la bibliotheque de l'editeur en propose la
    // plupart, les autres naissent autrement (un groupe d'une selection...) : dits a part, pour que les nombres
    // s'expliquent.
    std::map<std::string, std::vector<std::string>> fam;
    std::vector<std::string> outside;
    std::size_t inLibrary = 0;
    for (const auto& e : registry().kinds()) {
        if (e.family.empty()) {
            outside.push_back(e.label);
            continue;
        }
        ++inLibrary;
        fam[e.family].push_back(e.label);
    }
    const std::size_t all = registry().kinds().size();
    std::string text = "Le logiciel a **" + std::to_string(all) + " genres** d'objets ; **" + std::to_string(inLibrary)
                       + "** se posent depuis la biblioth\xC3\xA8que de l'\xC3\xA9" "diteur, et je sais les cr\xC3\xA9" "er. Par famille :\n";
    for (const auto& [f, labels] : fam) text += "- **" + f + "** : " + joinAnd(labels, 8) + " ;\n";
    if (!outside.empty())
        text += "Les " + std::to_string(outside.size()) + " autres (" + joinAnd(outside, 8) + ") ne sont pas dans la biblioth\xC3\xA8que : ils "
                "naissent autrement (un groupe d'une s\xC3\xA9lection, par exemple), je ne les cr\xC3\xA9" "e pas.\n";
    text += "Je les reconnais par leur libell\xC3\xA9, au pluriel, et par quelques mots du m\xC3\xA9tier (bac ou r\xC3\xA9servoir : une cuve ; "
            "bouton poussoir ; voyant TOR ; tuyau : une tuyauterie).";
    return ask(std::move(text));
}

LocalReply Engine::listViews() const {
    if (!in_.project || in_.project->views.empty()) return ask("Le projet n'a aucune vue.");
    std::vector<std::string> list;
    for (const auto& v : in_.project->views) list.push_back(code(v.name) + " (" + std::to_string(v.width) + "\xC3\x97" + std::to_string(v.height) + ")");
    return ask("Le projet a **" + plural(list.size(), "vue", "vues") + "** : " + joinAnd(list, 20) + ".");
}

// ---- creer des objets ---------------------------------------------------------------------------
LocalReply Engine::create(std::vector<NewObject> wanted, const Position& pos, std::string intro, const hmi::View* into) const {
    if (!view() && !into) return needView();
    if (wanted.empty()) return notUnderstood();
    const hmi::View& v = into ? *into : *view();
    constexpr std::size_t kMax = 60;
    std::string capped;
    if (wanted.size() > kMax) {
        capped = " Je me limite \xC3\xA0 " + std::to_string(kMax) + " objets par demande.";
        wanted.resize(kMax);
    }
    // la reference d'une position relative
    const hmi::Object* ref = nullptr;
    if (pos.rel != Rel::None && !into && !pos.missing.empty()) {
        const Target t = missingObject(pos.missing);
        return ask(t.question, t.suggestions);
    }
    if (pos.rel != Rel::None && !into) {
        const Target t = firstTarget(pos.refAt, pos.end);
        if (!t.question.empty()) return ask(t.question, t.suggestions);
        if (!t.objects.empty()) ref = t.objects.front();
    }
    // les noms : Base_N libre, ou "Marche_Pompe_1" pour un bouton pose pres d'un objet
    std::set<std::string> reserved;
    std::vector<std::string> names;
    for (auto& w : wanted) {
        std::string name;
        if (!w.name.empty() && hmi::isIdentifier(w.name) && nameFree(v, w.name, reserved)) name = w.name;
        else if (!w.text.empty() && isButtonKind(w.kind)) {
            const std::string b = sanitize(w.text) + (ref ? "_" + ref->name : std::string("_") + baseName(w.kind));
            name = nameFree(v, b, reserved) ? b : b + "_" + std::to_string(nextNumber(v, b, reserved));
        } else {
            const std::string b = baseName(w.kind);
            name = b + "_" + std::to_string(nextNumber(v, b, reserved));
        }
        reserved.insert(name);
        names.push_back(name);
    }
    // les tailles par defaut du genre (le registre)
    std::vector<hmi::Box> boxes;
    for (const auto& w : wanted) {
        const auto* e = registry().find(w.kind);
        double ww = e ? e->w : 120, hh = e ? e->h : 80;
        if (ww <= 0) ww = 120;
        if (hh <= 0) hh = w.kind == hmi::Kind::Line ? 0 : 80;
        boxes.push_back({0, 0, ww, hh});
    }
    const double gap = 24;
    const auto labelRoom = [](hmi::Kind k) { return hmi::kindIsSynoptic(k) ? 20.0 : 0.0; };
    Placer placer(v);
    std::string where;
    if (ref) {
        const hmi::Box rb = hmi::edit::rotatedBounds(*ref);
        const double extra = hmi::kindIsSynoptic(ref->kind) && !ref->text("label").empty() && ref->text("labelPosition", "dessous") == "dessous" ? 20 : 0;
        double totalW = 0, maxH = 0;
        for (const auto& b : boxes) {
            totalW += b.w;
            maxH = std::max(maxH, b.h);
        }
        totalW += gap * static_cast<double>(boxes.size() - 1);
        double x = 0, y = 0;
        bool row = true;
        switch (pos.rel) {
            case Rel::Right: x = rb.right() + 30; y = rb.cy() - maxH / 2; where = "\xC3\xA0 droite de " + code(ref->name); break;
            case Rel::Left: x = rb.x - 30 - totalW; y = rb.cy() - maxH / 2; where = "\xC3\xA0 gauche de " + code(ref->name); break;
            case Rel::Below: x = rb.cx() - totalW / 2; y = rb.bottom() + extra + 24; where = "sous " + code(ref->name); break;
            case Rel::Above: x = rb.cx() - totalW / 2; y = rb.y - 24 - maxH - labelRoom(wanted.front().kind); where = "au-dessus de " + code(ref->name); break;
            case Rel::None: break;
        }
        // une rangee (sous, au-dessus) ou une ligne (a droite, a gauche) ; decalee tant qu'elle recouvre
        (void)row;
        const auto layout = [&](double ox, double oy) {
            std::vector<hmi::Box> out;
            double cx = ox;
            for (const auto& b : boxes) {
                out.push_back({std::round(cx), std::round(oy + (maxH - b.h) / 2), b.w, b.h});
                cx += b.w + gap;
            }
            return out;
        };
        double dx = pos.rel == Rel::Right ? 10 : pos.rel == Rel::Left ? -10 : 0;
        double dy = pos.rel == Rel::Below ? 10 : pos.rel == Rel::Above ? -10 : 0;
        x = std::max(0.0, x);
        y = std::max(0.0, y);
        auto out = layout(x, y);
        for (int k = 0; k < 40; ++k) {
            bool ok = true;
            for (std::size_t i = 0; i < out.size() && ok; ++i) {
                hmi::Box b = out[i];
                b.h += labelRoom(wanted[i].kind);
                // la reference elle-meme ne compte pas : on se pose a cote d'elle
                const hmi::Box g{b.x - 8, b.y - 8, b.w + 16, b.h + 16};
                for (const auto& tb : placer.taken)
                    if (tb.intersects(g) && !(tb.x == rb.x && tb.y == rb.y && tb.w == rb.w)) ok = false;
            }
            if (ok) break;
            x = std::max(0.0, x + dx);
            y = std::max(0.0, y + dy);
            out = layout(x, y);
        }
        boxes = out;
    } else {
        // une place libre : la rangee entiere si elle tient, sinon une grille ; sinon chacun la sienne
        double totalW = 0, maxH = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            totalW += boxes[i].w;
            maxH = std::max(maxH, boxes[i].h + labelRoom(wanted[i].kind));
        }
        totalW += gap * static_cast<double>(boxes.size() - 1);
        std::size_t perRow = boxes.size();
        if (totalW > v.width - 80) {
            const double cell = totalW / static_cast<double>(boxes.size());
            perRow = std::max<std::size_t>(1, static_cast<std::size_t>((v.width - 80 + gap) / (cell + gap)));
        }
        const std::size_t rows = (boxes.size() + perRow - 1) / perRow;
        double rowW = 0;
        for (std::size_t i = 0; i < perRow && i < boxes.size(); ++i) rowW += boxes[i].w + (i ? gap : 0);
        const hmi::Box area = placer.find(rowW, maxH * static_cast<double>(rows) + gap * static_cast<double>(rows - 1));
        double cx = area.x, cy = area.y;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (i > 0 && i % perRow == 0) {
                cx = area.x;
                cy += maxH + gap;
            }
            boxes[i].x = cx;
            boxes[i].y = cy;
            cx += boxes[i].w + gap;
        }
        where = "\xC3\xA0 un endroit libre de " + (into ? guill(into->name) : viewName()) + " (x = " + num(boxes.front().x) + ", y = " + num(boxes.front().y) + ")";
    }
    // les operations
    std::map<hmi::Kind, std::size_t> perKind;
    std::vector<hmi::Kind> order;
    Proposal p = base("");
    std::vector<std::pair<std::string, std::vector<std::string>>> colored;   // "rouge" -> les objets
    std::vector<std::string> texts;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        const auto& w = wanted[i];
        if (!perKind[w.kind]++) order.push_back(w.kind);
        Operation op;
        op.action = Operation::Action::Create;
        op.kind = std::string(hmi::kindKey(w.kind));
        op.name = names[i];
        op.props = {{"x", num(boxes[i].x)}, {"y", num(boxes[i].y)}, {"w", num(boxes[i].w)}, {"h", num(boxes[i].h)}};
        const hmi::Object tmpl = hmi::makeObject(w.kind, 1, names[i], 0, 0, hmi::kNoId);
        if (!w.text.empty()) {
            if (tmpl.find("text")) op.props.emplace_back("text", w.text);
            else if (tmpl.find("label")) op.props.emplace_back("label", w.text);
            texts.push_back(guill(w.text));
        } else if (tmpl.find("label") && hmi::kindIsSynoptic(w.kind)) {
            // le libelle d'un symbole : P3 pour Pompe_3, Cuve 2 pour une cuve
            const auto us = names[i].rfind('_');
            const std::string n = us == std::string::npos ? std::string{} : names[i].substr(us + 1);
            if (isDigits(n)) op.props.emplace_back("label", hmi::kindHasLevel(w.kind) ? baseName(w.kind) + " " + n : std::string(1, names[i][0]) + n);
        }
        if (!w.color.empty()) {
            std::string key;
            if (hmi::kindHasLevel(w.kind) && tmpl.find("fillColor")) key = "fillColor";
            else if (tmpl.find("colorOn")) key = "colorOn";
            else if (w.kind == hmi::Kind::Text && tmpl.find("textColor")) key = "textColor";
            else if (tmpl.find("fill")) key = "fill";
            else if (tmpl.find("textColor")) key = "textColor";
            else if (tmpl.find("stroke")) key = "stroke";
            if (!key.empty()) {
                op.props.emplace_back(key, w.color);
                const std::string cn = w.colorName.empty() ? code(w.color) : w.colorName;
                auto it = std::find_if(colored.begin(), colored.end(), [&](const auto& c) { return c.first == cn; });
                if (it == colored.end()) colored.push_back({cn, {names[i]}});
                else it->second.push_back(names[i]);
            }
        }
        p.ops.push_back(std::move(op));
    }
    std::vector<std::string> parts;
    for (const auto k : order) parts.push_back(countKind(k, perKind[k]));
    p.summary = parts.size() == 1 ? "Ajouter " + parts.front() : "Ajouter " + joinAnd(parts);
    std::string text = intro;
    text += "Je propose **" + joinAnd(parts) + "** : " + codes(names) + ", " + where + ".";
    if (!texts.empty()) text += " Textes : " + joinAnd(texts) + ".";
    // "En rouge : `Voyant_1`." (la couleur dite, pas seulement les noms)
    for (std::size_t c = 0; c < colored.size(); ++c)
        text += std::string(c == 0 ? " En " : " ; en ") + colored[c].first + " : " + codes(colored[c].second) + (c + 1 == colored.size() ? "." : "");
    text += capped;
    bool needVariable = false;
    for (const auto& w : wanted) {
        const auto* e = registry().find(w.kind);
        if (e && (e->shows || e->writes)) needVariable = true;
    }
    if (needVariable)
        text += "\nAucune variable n'est associ\xC3\xA9" "e : je n'invente pas de nom. Dis-moi laquelle relier, par exemple `Associe "
                + names.front() + " \xC3\xA0 ...`.";
    return finish(std::move(p), std::move(text));
}

LocalReply Engine::createFromMentions(const hmi::View* into) const {
    if (!view() && !into) return needView();
    const Position pos = into ? Position{} : position();
    std::vector<NewObject> wanted;
    std::vector<const Mention*> news;
    for (const auto& m : mentions_) {
        if (pos.rel != Rel::None && m.at >= pos.refAt && m.at < pos.end) continue;   // l'objet de reference
        if (m.definite && !m.indefinite) continue;
        // 1.16.1 : "3 pompes en ligne" : une disposition, pas une Ligne a poser
        if (m.kind == hmi::Kind::Line && m.at > 0 && (t_[m.at - 1].f == "en" || t_[m.at - 1].f == "a")) continue;
        news.push_back(&m);
    }
    // "cree la pompe P5" : un genre defini, un nom neuf - avec un vrai verbe de creation (1.16.1 : pas "je veux voir le
    // niveau de la cuve", ni "place la vanne sous la cuve")
    const bool strongCreate = hasPrefix("ajout") || hasPrefix("rajout") || hasPrefix("cree") || has("creer") || has("cre") || hasPrefix("insere")
                           || hasPrefix("dessin") || hasPrefix("genere") || hasPrefix("fabriqu");
    if (news.empty() && strongCreate)
        for (const auto& m : mentions_)
            if (!(pos.rel != Rel::None && m.at >= pos.refAt && m.at < pos.end)) news.push_back(&m);
    if (news.empty()) return notUnderstood();
    for (std::size_t k = 0; k < news.size(); ++k) {
        const Mention& m = *news[k];
        const std::size_t stop = k + 1 < news.size() ? news[k + 1]->at : (pos.rel != Rel::None && pos.at > m.at ? pos.at : t_.size());
        // apres le genre : une couleur, des textes (Marche et Arret, "Marche"), un nom (appele X)
        std::string color, colorName, name;
        std::vector<std::string> texts;
        for (std::size_t i = m.at + m.len; i < stop; ++i) {
            const Tok& tk = t_[i];
            std::size_t len = 0;
            std::string label;
            if (const std::string c = colorAt(t_, i, &len, &label); !c.empty() && !tk.quoted) {
                color = c;
                colorName = label;
                i += len - 1;
                continue;
            }
            if ((tk.f.rfind("appel", 0) == 0 || tk.f.rfind("nomm", 0) == 0) && i + 1 < stop) {
                name = t_[i + 1].raw;
                ++i;
                continue;
            }
            // un mot en majuscule, ou entre guillemets : un texte (Marche, Arret) ; 1.16.1 : pour des boutons, aussi un mot de
            // commande en minuscules ("2 boutons marche et arret")
            bool capital = !tk.raw.empty() && (std::isupper(static_cast<unsigned char>(tk.raw[0])) || static_cast<unsigned char>(tk.raw[0]) == 0xC3);
            if (!capital && !tk.quoted && isButtonKind(m.kind) && (i == m.at + m.len || t_[i - 1].f == "et" || t_[i - 1].f == "ou"))
                for (const char* w : {"marche", "arret", "start", "stop", "reset", "acquit", "demarr", "ouvr", "ferm", "auto", "manu"})
                    if (tk.f.rfind(w, 0) == 0) {
                        capital = true;
                        std::string up = tk.raw;
                        up[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(up[0])));
                        texts.push_back(up);
                    }
            if (capital && !tk.quoted && !std::isupper(static_cast<unsigned char>(tk.raw[0])) && static_cast<unsigned char>(tk.raw[0]) != 0xC3) continue;
            if (tk.quoted || (capital && !stopWord(tk.f) && numberOf(tk.f) < 0 && !mentionAt(i))) {
                // un nom d'objet voulu (Pompe_Eau, P5) plutot qu'un texte, pour un symbole sans texte
                if (!tk.quoted && tk.raw.find('_') != std::string::npos) {
                    name = tk.raw;
                    continue;
                }
                texts.push_back(tk.raw);
            }
        }
        // 1.16.1 : le garde-fou des noms - "une pompe appelee Pour" : un mot de la phrase, pas un nom
        if (!name.empty() && (badName(name) || !hmi::isIdentifier(name)))
            return ask(guill(name) + " ne peut pas \xC3\xAAtre le nom " + std::string(feminine(m.kind) ? "de la " : "du ") + lowerFirst(kindPlural(m.kind, 1))
                       + " : c'est un mot de la phrase, pas un nom (des lettres sans accent, des chiffres et _). Quel nom ? Par exemple `Ajoute une "
                         "pompe appel\xC3\xA9" "e Pompe_Eau`.");
        // 1.16.1 : un titre, un texte non cite : la suite de la phrase depuis son premier mot ("un titre Station de pompage")
        if (m.kind == hmi::Kind::Text && texts.size() == 1 && std::none_of(t_.begin(), t_.end(), [](const Tok& x) { return x.quoted; })) {
            std::size_t first = m.at + m.len;
            while (first < stop && t_[first].raw != texts.front()) ++first;
            std::string all;
            for (std::size_t i = first; i < stop; ++i) {
                if (!colorAt(t_, i).empty()) break;
                const bool elided = !all.empty() && all.back() == '\'';
                all += (all.empty() || elided ? "" : " ") + t_[i].raw;
                if (t_[i].f.size() <= 2 && (t_[i].f == "d" || t_[i].f == "l" || t_[i].f == "qu" || t_[i].f == "j")) all += '\'';
            }
            if (!all.empty()) texts.front() = all;
        }
        int count = m.count;
        if (count < 0) count = m.plural ? (texts.size() > 1 ? static_cast<int>(texts.size()) : -1) : 1;
        if (count < 0)
            return ask("Combien de " + lowerFirst(kindPlural(m.kind, 2)) + " ? Dis-le en chiffres ou en lettres.",
                       {"Ajoute 2 " + lowerFirst(kindPlural(m.kind, 2)), "Ajoute 3 " + lowerFirst(kindPlural(m.kind, 2))});
        if (count > 60) return ask("C'est beaucoup : je pose au plus 60 objets par demande. Demande-les en plusieurs fois.");
        // des boutons Marche, Arret... : des boutons de commande (a impulsion), comme le synoptique
        hmi::Kind kind = m.kind;
        if (kind == hmi::Kind::Button)
            for (const auto& tx : texts) {
                const std::string f = foldWord(tx);
                for (const char* w : {"marche", "arret", "start", "stop", "ouvr", "ferm", "acquit", "reset", "demarr"})
                    if (f.rfind(w, 0) == 0) kind = hmi::Kind::PushButton;
            }
        for (int c = 0; c < count; ++c) {
            NewObject w;
            w.kind = kind;
            w.color = color;
            w.colorName = colorName;
            if (static_cast<std::size_t>(c) < texts.size()) w.text = texts[static_cast<std::size_t>(c)];
            else if (texts.size() == 1) w.text = texts.front();
            if (count == 1) w.name = name;
            wanted.push_back(std::move(w));
        }
    }
    return create(std::move(wanted), pos, {}, into);
}

// ---- "et une autre", "la meme en bleu", "pareil pour P2" -----------------------------------------
LocalReply Engine::followUp() const {
    if (!view()) return needView();
    // "pareil pour Bouton_9" : un objet qui n'est pas dans la vue - dit
    for (std::size_t i = 0; i < t_.size(); ++i)
        if (unknownObjectAt(i)) {
            const Target m = missingObject(t_[i].raw);
            return ask(m.question, m.suggestions);
        }
    std::size_t clen = 0;
    std::string color, colorName;
    for (std::size_t i = 0; i < t_.size() && color.empty(); ++i) color = colorAt(t_, i, &clen, &colorName);
    // pareil pour un autre objet : les memes modifications
    if (!mem_.modifies.empty() && !names_.empty()) {
        const Target t = targets(0, t_.size(), true, false);
        if (!t.question.empty()) return ask(t.question, t.suggestions);
        if (!t.objects.empty()) {
            Proposal p = base("La m\xC3\xAAme modification");
            std::vector<std::string> done;
            for (const auto* o : t.objects) {
                for (const auto& prev : mem_.modifies) {
                    Operation op;
                    op.action = Operation::Action::Modify;
                    op.name = o->name;
                    for (const auto& [k, val] : prev.props) {
                        if (k == "x" || k == "y" || k == "w" || k == "h") continue;
                        if (!o->find(k)) continue;
                        op.props.emplace_back(k, color.empty() || !hmi::roles::colorOk(val) ? val : color);
                    }
                    if (!op.props.empty()) p.ops.push_back(std::move(op));
                }
                done.push_back(o->name);
            }
            if (p.ops.empty()) return ask("La modification d'avant ne s'applique pas \xC3\xA0 " + codes(done) + " (ses propri\xC3\xA9t\xC3\xA9s ne sont pas les m\xC3\xAAmes).");
            return finish(std::move(p), "Je propose **la m\xC3\xAAme modification** qu'\xC3\xA0 la demande d'avant (" + guill(mem_.request) + ") sur "
                                            + codes(done) + ".");
        }
    }
    if (mem_.creates.empty())
        return ask("Je ne sais pas \xC3\xA0 quoi \xC3\xA7" "a se rapporte : la demande d'avant n'a rien cr\xC3\xA9\xC3\xA9 ni modifi\xC3\xA9. Dis-moi "
                   "le genre d'objet, par exemple `Ajoute une pompe`.",
                   examples());
    // une autre (ou N autres) des memes, de la meme couleur sauf si une autre est dite
    int count = 1;
    for (const auto& tk : t_)
        if (const int n = numberOf(tk.f); n > 1) count = n;
    std::vector<NewObject> wanted;
    std::vector<hmi::Kind> kinds;
    std::map<hmi::Kind, std::vector<const Operation*>> byKind;
    for (const auto& op : mem_.creates)
        if (const auto k = hmi::kindFromKey(op.kind)) {
            if (byKind[*k].empty()) kinds.push_back(*k);
            byKind[*k].push_back(&op);
        }
    if (kinds.empty()) return ask("Je ne reconnais pas les objets de la demande d'avant. Dis-moi le genre d'objet, par exemple `Ajoute une pompe`.", examples());
    // "une autre" : un objet du dernier genre cree ; "les memes" : tout le groupe d'avant
    const bool group = has("memes") || has("pareils") || phrase("la meme chose") || phrase("le meme groupe");
    std::vector<const Operation*> models;
    if (group) {
        for (const auto& op : mem_.creates)
            if (hmi::kindFromKey(op.kind)) models.push_back(&op);
    } else {
        models.push_back(byKind[kinds.back()].back());
    }
    for (int c = 0; c < count; ++c)
        for (const auto* op : models) {
            NewObject w;
            w.kind = *hmi::kindFromKey(op->kind);
            for (const auto& [k, val] : op->props) {
                if (k == "text") w.text = val;
                if (hmi::roles::colorOk(val) && (k == "fillColor" || k == "colorOn" || k == "fill" || k == "textColor")) w.color = val;
            }
            if (!color.empty()) {
                w.color = color;
                w.colorName = colorName;
            }
            wanted.push_back(std::move(w));
        }
    return create(std::move(wanted), position(), "Comme \xC3\xA0 la demande d'avant (" + guill(mem_.request) + ").\n");
}

// ---- le synoptique de station de pompage -------------------------------------------------------
LocalReply Engine::synoptic() const {
    if (!view()) return needView();
    // les nombres de la phrase : "3 cuves", "2 pompes par cuve", "6 pompes"
    int tanks = -1, pumps = -1;
    bool perTank = false;
    for (const auto& m : mentions_) {
        if (m.count <= 0) continue;
        if (m.kind == hmi::Kind::Tank && tanks < 0) tanks = m.count;
        if (m.kind == hmi::Kind::Pump && pumps < 0) {
            pumps = m.count;
            const std::size_t after = m.at + m.len;
            perTank = after + 1 < t_.size() && t_[after].f == "par" && sameWord(t_[after + 1].f, "cuve", true);
        }
    }
    if (tanks < 0) tanks = 3;
    if (pumps < 0) {
        pumps = 2;
        perTank = true;
    }
    if (!perTank) pumps = std::max(1, (pumps + tanks - 1) / tanks);
    if (tanks > 8 || pumps > 6) return ask("C'est beaucoup pour une vue : au plus 8 cuves et 6 pompes par cuve. Redemande avec moins.");
    const bool buttons = !(phrase("sans bouton") || phrase("sans boutons"));
    const bool valves = !(phrase("sans vanne") || phrase("sans vannes"));
    const hmi::View& v = *view();
    const double margin = 40, pumpW = 90, step = 110;
    const double colNeed = std::max(300.0, 40 + pumps * step);
    const double W = std::max(static_cast<double>(v.width), margin * 2 + colNeed * tanks);
    const double colW = std::max(colNeed, (std::min(W, static_cast<double>(v.width)) - 2 * margin) / tanks);
    Placer placer(v);
    double y0 = 24;
    const double used = placer.bottom();
    const double height = buttons ? 650 : 560;
    if (used > 0) y0 = used + 30;
    if (y0 + height > v.height && used + 30 + height > v.height) y0 = used > 0 ? used + 30 : 24;
    std::set<std::string> reserved;
    const auto fresh = [&](const std::string& wanted) {
        if (nameFree(v, wanted, reserved)) {
            reserved.insert(wanted);
            return wanted;
        }
        std::string n = wanted + "_" + std::to_string(nextNumber(v, wanted, reserved));
        reserved.insert(n);
        return n;
    };
    Proposal p = base("Synoptique de station de pompage");
    const auto add = [&](hmi::Kind k, const std::string& name, double x, double y, double w, double h,
                         std::vector<std::pair<std::string, std::string>> more) {
        Operation op;
        op.action = Operation::Action::Create;
        op.kind = std::string(hmi::kindKey(k));
        op.name = fresh(name);
        op.props = {{"x", num(x)}, {"y", num(y)}, {"w", num(w)}, {"h", num(h)}};
        for (auto& m : more) op.props.push_back(std::move(m));
        p.ops.push_back(std::move(op));
    };
    add(hmi::Kind::Text, "Titre_Station", margin, y0, 520, 40, {{"text", "Station de pompage"}, {"fontSize", "26"}, {"align", "gauche"}});
    int nPumps = 0, nButtons = 0, nValves = 0;
    for (int col = 0; col < tanks; ++col) {
        const double cx = margin + col * colW;
        const std::string n = std::to_string(col + 1);
        add(hmi::Kind::Tank, "Cuve_" + n, cx + 40, y0 + 70, 150, 200, {{"label", "Cuve " + n}, {"max", "100"}, {"unit", "%"}});
        add(hmi::Kind::Bargraph, "Niveau_Cuve_" + n, cx + 214, y0 + 70, 56, 200, {{"unit", "%"}});
        double py = y0 + 300;
        if (valves) {
            add(hmi::Kind::Valve, "Vanne_" + n, cx + 75, y0 + 300, 80, 64, {{"label", "V" + n}});
            ++nValves;
            py = y0 + 392;
        }
        add(hmi::Kind::Pipe, "Collecteur_" + n, cx + 20, py, std::max(120.0, pumps * step - 20), 20, {});
        for (int k = 0; k < pumps; ++k) {
            const std::string pn = n + std::to_string(k + 1);
            const double px = cx + 20 + k * step;
            add(hmi::Kind::Pump, "Pompe_" + pn, px, py + 38, pumpW, 80, {{"label", "P" + pn}});
            ++nPumps;
            if (buttons) {
                add(hmi::Kind::PushButton, "Marche_P" + pn, px, py + 164, 96, 36,
                    {{"text", "Marche"}, {"fill", "#2F7D4F"}, {"stroke", "#24603C"}, {"fontSize", "14"}});
                add(hmi::Kind::PushButton, "Arret_P" + pn, px, py + 208, 96, 36,
                    {{"text", "Arr\xC3\xAAt"}, {"fill", "#7D3A36"}, {"stroke", "#5E2B28"}, {"fontSize", "14"}});
                nButtons += 2;
            }
        }
    }
    std::string text = "## Synoptique de station de pompage\nJe propose **" + std::to_string(p.ops.size()) + " objets** dans " + viewName() + ", "
                       + plural(static_cast<std::size_t>(tanks), "colonne", "colonnes") + " (une par cuve) :\n"
                       "- **" + countKind(hmi::Kind::Tank, static_cast<std::size_t>(tanks)) + "** avec leur niveau en %, et **"
                       + countKind(hmi::Kind::Bargraph, static_cast<std::size_t>(tanks)) + "** \xC3\xA0 c\xC3\xB4t\xC3\xA9 ;\n";
    if (nValves) text += "- **" + countKind(hmi::Kind::Valve, static_cast<std::size_t>(nValves)) + "** de sortie ;\n";
    text += "- **" + countKind(hmi::Kind::Pipe, static_cast<std::size_t>(tanks)) + "** vers les pompes ;\n"
            "- **" + countKind(hmi::Kind::Pump, static_cast<std::size_t>(nPumps)) + "**, " + plural(static_cast<std::size_t>(pumps), "par cuve", "par cuve") + " ;\n";
    if (nButtons) text += "- **" + countKind(hmi::Kind::PushButton, static_cast<std::size_t>(nButtons)) + "** : Marche et Arr\xC3\xAAt sous chaque pompe ;\n";
    text += "- un titre.\n";
    if (W > v.width + 1) text += "La vue est \xC3\xA9troite pour tout \xC3\xA7" "a : des objets d\xC3\xA9passeront du cadre (l'aper\xC3\xA7u le montre).\n";
    text += "Je n'ai associ\xC3\xA9 **aucune variable** : je n'invente pas de nom. Ensuite, demande par exemple `Associe Pompe_11 \xC3\xA0 ...`.";
    return finish(std::move(p), std::move(text));
}

// ---- une vue ------------------------------------------------------------------------------------
LocalReply Engine::createView() const {
    if (!in_.project) return ask("Ouvre d'abord un projet IHM.");
    std::string name;
    int w = 1920, h = 1080;
    std::string bg;
    const std::size_t at = indexOf("vue") != std::string::npos ? indexOf("vue") : indexOf("ecran");
    for (std::size_t i = 0; i < t_.size(); ++i) {
        const Tok& tk = t_[i];
        // la taille : 1920x1080, 1280 x 800, 800 par 600
        const auto x = tk.f.find('x');
        if (x != std::string::npos && x > 0 && isDigits(tk.f.substr(0, x)) && isDigits(tk.f.substr(x + 1))) {
            w = std::stoi(tk.f.substr(0, x));
            h = std::stoi(tk.f.substr(x + 1));
            continue;
        }
        if (isDigits(tk.f) && i + 2 < t_.size() && (t_[i + 1].f == "x" || t_[i + 1].f == "par" || t_[i + 1].f == "sur") && isDigits(t_[i + 2].f)) {
            w = std::stoi(tk.f);
            h = std::stoi(t_[i + 2].f);
            i += 2;
            continue;
        }
        std::size_t len = 0;
        if (const std::string c = colorAt(t_, i, &len); !c.empty() && i > 0 && (t_[i - 1].f == "fond" || (i > 1 && t_[i - 2].f == "fond"))) {
            bg = c;
            continue;
        }
        if (name.empty() && at != std::string::npos && i > at && !tk.quoted && !stopWord(tk.f) && (tk.raw.find('_') != std::string::npos
                                                                                                    || std::isupper(static_cast<unsigned char>(tk.raw[0]))))
            name = tk.raw;
        if (name.empty() && tk.quoted) name = sanitize(tk.raw);
        if ((tk.f.rfind("appel", 0) == 0 || tk.f.rfind("nomm", 0) == 0) && i + 1 < t_.size()) name = t_[i + 1].quoted ? sanitize(t_[i + 1].raw) : t_[i + 1].raw;
    }
    if (w < 100 || h < 100 || w > 10000 || h > 10000) return ask("Cette taille (" + std::to_string(w) + "\xC3\x97" + std::to_string(h) + ") n'est pas une taille de vue : entre 100 et 10000 pixels.");
    if (name.empty()) {
        int n = 1;
        while (in_.project->viewByName("Vue_" + std::to_string(n))) ++n;
        name = "Vue_" + std::to_string(n);
    }
    if (badName(name)) return ask(guill(name) + " ne peut pas \xC3\xAAtre le nom de la vue : c'est un mot de la phrase. Quel nom ? Par exemple `Cr\xC3\xA9" "e une vue Vue_Process`.");
    if (!hmi::isIdentifier(name)) return ask(guill(name) + " n'est pas un nom de vue valable (des lettres, des chiffres et _).");
    if (in_.project->viewByName(name)) return ask("La vue " + code(name) + " existe d\xC3\xA9j\xC3\xA0 dans le projet. Choisis un autre nom.");
    Proposal p = base("Cr\xC3\xA9" "er la vue " + name);
    Operation op;
    op.action = Operation::Action::View;
    op.name = name;
    op.props = {{"largeur", std::to_string(w)}, {"hauteur", std::to_string(h)}};
    if (!bg.empty()) op.props.emplace_back("fond", bg);
    p.ops.push_back(std::move(op));
    std::string text = "Je propose de cr\xC3\xA9" "er la vue **" + name + "** en " + std::to_string(w) + "\xC3\x97" + std::to_string(h)
                       + (bg.empty() ? std::string{} : ", fond " + bg) + ".";
    // "une vue Vue_Pompes avec 3 pompes" : les objets se posent dans la vue neuve (les operations qui suivent
    // un creer_vue la visent)
    if (std::any_of(mentions_.begin(), mentions_.end(), [](const Mention& m) { return m.indefinite; })) {
        hmi::View fresh;
        fresh.name = name;
        fresh.width = w;
        fresh.height = h;
        LocalReply objs = createFromMentions(&fresh);
        if (!objs.proposal) return objs;   // une question (combien ?), dite telle quelle
        for (auto& o : objs.proposal->ops) p.ops.push_back(std::move(o));
        text += " Dedans : " + objs.text;
    }
    text += "\nElle s'ouvrira apr\xC3\xA8s Appliquer ; Ctrl+Z retire tout.";
    return finish(std::move(p), std::move(text));
}

// ---- des variables -----------------------------------------------------------------------------
std::string typeWord(const std::string& f) {
    static const std::map<std::string, std::string> k = {
        {"bool", "BOOL"}, {"booleen", "BOOL"}, {"booleenne", "BOOL"}, {"booleens", "BOOL"}, {"booleennes", "BOOL"}, {"tor", "BOOL"},
        {"int", "INT"}, {"entier", "INT"}, {"entiers", "INT"}, {"dint", "DINT"}, {"uint", "UINT"}, {"udint", "UDINT"}, {"word", "WORD"},
        {"dword", "DWORD"}, {"real", "REAL"}, {"reel", "REAL"}, {"reels", "REAL"}, {"reelle", "REAL"}, {"reelles", "REAL"}, {"flottant", "REAL"},
        {"lreal", "LREAL"}, {"string", "STRING"}, {"chaine", "STRING"}, {"time", "TIME"}, {"duree", "TIME"}};
    const auto it = k.find(f);
    return it == k.end() ? std::string{} : it->second;
}

// Les mots d'un nom : coupes au _, aux chiffres et aux majuscules ("Pompe_1_Marche", "SeuilHaut", "HeuresMarche"), replies.
std::vector<std::string> nameWords(std::string_view name) {
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&] {
        if (!cur.empty()) out.push_back(foldWord(cur));
        cur.clear();
    };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c == '_' || c == ' ' || c == '.' || std::isdigit(c)) {
            flush();
            continue;
        }
        if (std::isupper(c) && !cur.empty() && !std::isupper(static_cast<unsigned char>(cur.back()))) flush();
        cur += static_cast<char>(c);
    }
    flush();
    std::erase_if(out, [](const std::string& w) { return w.empty(); });
    return out;
}

// Le type tire du nom, par ses mots (1.16.1) : un defaut, une alarme, le mode auto : BOOL ; un type, une unite, un libelle :
// STRING ; un temps, une duree : TIME ; des heures, un compteur, un nombre : DINT ; un etat, un mode : INT ; un niveau, une
// mesure, une consigne, un seuil, un min : REAL ; sinon BOOL (une marche, une commande, ouverte).
std::string typeFromName(const std::string& name) {
    const std::vector<std::string> words = nameWords(name);
    const auto starts = [&](std::initializer_list<const char*> stems) {
        for (const auto& w : words)
            for (const char* s : stems)
                if (w.rfind(s, 0) == 0) return true;
        return false;
    };
    const auto is = [&](std::initializer_list<const char*> exact) {
        for (const auto& w : words)
            for (const char* s : exact)
                if (w == s || w == std::string(s) + "s") return true;
        return false;
    };
    if (starts({"defaut", "alarm", "alerte", "auto", "manu"})) return "BOOL";
    if (is({"type", "nom", "tag", "texte", "message", "marque", "modele", "reference", "repere", "libelle", "unite", "description", "commentaire",
            "designation", "fabricant", "serie", "matricule"}))
        return "STRING";
    if (starts({"temps", "duree", "delai", "tempo", "retard"})) return "TIME";
    if (starts({"heure", "compteur", "cycle", "demarrage", "nombre", "manoeuvres"}) || is({"nb"})) return "DINT";
    if (is({"etat", "mode", "etape", "code", "numero", "adresse", "rang", "index", "indice", "choix", "priorite"})) return "INT";
    if (starts({"niveau", "temperature", "temp", "pression", "debit", "mesure", "consigne", "vitesse", "poids", "valeur", "courant", "tension",
                "seuil", "position", "puissance", "frequence", "couple", "intensite", "volume", "hauteur", "capacite", "ouverture", "recopie",
                "pourcent", "taux", "densite", "conductivit", "energie", "echelle", "moyenne", "ecart"})
        || is({"min", "max", "mini", "maxi", "minimum", "maximum", "kp", "ki", "kd", "ti", "td", "gain", "offset", "ph"}))
        return "REAL";
    return "BOOL";
}

LocalReply Engine::createVariables() const {
    if (!in_.project) return ask("Ouvre d'abord un projet IHM.");
    const bool plc = has("automate") || has("api") || has("plc") || has("ddt") || has("ddts");
    const bool types = has("type") || has("types") || has("ddt") || has("ddts") || has("structure") || has("structures");
    // le filtre : "qui commencent par PT", "contenant Pompe"
    std::string filter;
    for (std::size_t i = 0; i + 1 < t_.size(); ++i)
        if ((t_[i].f == "par" && i > 0 && t_[i - 1].f.rfind("commenc", 0) == 0) || t_[i].f.rfind("contien", 0) == 0 || t_[i].f.rfind("contenant", 0) == 0)
            filter = t_[i + 1].raw;
    // les noms ecrits, CHACUN AVEC SON TYPE : "X en REAL et Y en BOOL", "X et Y en BOOL", "X : REAL, Y : BOOL" - un
    // type dit vaut pour les noms ecrits depuis le type d'avant ; un type dit avant tout nom ("2 variables REAL X
    // et Y") vaut pour ceux qui n'en ont pas
    std::vector<Operation::VariableSpec> specs;
    std::vector<std::string> already, invalid;
    std::string leading;
    std::vector<std::size_t> pending;   // les specs sans type depuis le dernier type dit
    for (std::size_t i = 0; i < t_.size(); ++i) {
        const Tok& tk = t_[i];
        // un type elementaire (BOOL, reel...) ou un type IHM du projet (T_Pompe)
        std::string ty = tk.quoted ? std::string{} : typeWord(tk.f);
        if (ty.empty() && !tk.quoted)
            if (const auto* ht = in_.project->hmiTypeByName(tk.raw)) ty = ht->name;
        if (!ty.empty()) {
            if (pending.empty() && specs.empty()) leading = ty;
            for (const auto k : pending) specs[k].type = ty;
            pending.clear();
            continue;
        }
        if (tk.quoted || stopWord(tk.f) || tk.raw == filter || badName(tk.raw)) continue;   // 1.16.1 : jamais un mot-outil ni un verbe
        const bool identifier = tk.raw.find('_') != std::string::npos
                             || (std::isupper(static_cast<unsigned char>(tk.raw[0])) && i > 0 && !plc
                                 && (stopWord(t_[i - 1].f) || t_[i - 1].f.rfind("variable", 0) == 0 || t_[i - 1].raw.find('_') != std::string::npos));
        if (!identifier) continue;
        if (!hmi::isIdentifier(tk.raw)) {
            invalid.push_back(tk.raw);
            continue;
        }
        if (exists(tk.raw)) {
            already.push_back(tk.raw);
            continue;
        }
        if (std::any_of(specs.begin(), specs.end(), [&](const auto& s) { return norm(s.name) == norm(tk.raw); })) continue;
        Operation::VariableSpec s;
        s.name = tk.raw;
        pending.push_back(specs.size());
        specs.push_back(std::move(s));
    }
    if (specs.empty() && plc) {
        // l'automate relie : ses types (DDT) ; ou ses variables, avec les types qu'elles emploient (fromPlc : une
        // proposition entiere, chaque type une fois)
        auto ops = types && !hasPrefix("variable") ? typesFromPlc(*in_.project, in_.env, filter) : fromPlc(*in_.project, in_.env, filter);
        // RELIE ? L'entree ne le dit pas a part : les listes de l'automate (Env::plcVariables, plcTypes) sont la, ou
        // non, et vides sans automate. Ni variable globale ni DDT : aucun automate n'est relie.
        const bool linked = (in_.env.plcVariables && !in_.env.plcVariables().empty()) || (in_.env.plcTypes && !in_.env.plcTypes().empty());
        if (ops.empty()) {
            if (!linked)
                return ask("**Je n'ai pas encore lu d'automate** : joins \xC3\xA0 ta demande l'export de son programme (le fichier **.XPG** de "
                           "Control Expert, bouton Joindre) - je lis ses variables globales et ses types (DDT), puis je les propose en "
                           "variables et types IHM. L'\xC3\xA9quipement \xC2\xAB Automate du projet \xC2\xBB (Configuration \xE2\x80\xBA \xC3\x89quipements) "
                           "n'est qu'une liaison Modbus : ses adresses, pas un programme dont je lirais les variables. Sinon, cr\xC3\xA9" "e-les par "
                           "leur nom et leur type, ou d\xC3\xA9" "cris le type IHM :",
                           {"Cr\xC3\xA9" "e la variable PT101 en REAL", "Cr\xC3\xA9" "e un type T_Pompe avec Marche en BOOL"});
            // 1.16.0 (captures) : des instances de bloc (un DFB, un TON) n'ont pas d'equivalent IHM - le dire, plutot
            // que "deja dans l'IHM".
            if (!types) {
                std::vector<std::string> ddtNames, blocks;
                if (in_.env.plcTypes)
                    for (const auto& d : in_.env.plcTypes()) ddtNames.push_back(d.name);
                std::size_t matched = 0;
                for (const auto& [n, ty] : in_.env.plcVariables()) {
                    std::string bare = n.size() > 4 && (n.compare(0, 4, "API.") == 0 || n.compare(0, 4, "api.") == 0) ? n.substr(4) : n;
                    if (!filter.empty() && fold(bare).find(fold(filter)) == std::string::npos) continue;
                    ++matched;
                    if (hmiTypeOfPlc(ty, ddtNames, *in_.project).empty()) blocks.push_back(bare + " : " + ty);
                }
                if (matched > 0 && blocks.size() == matched) {
                    std::vector<std::string> shown(blocks.begin(), blocks.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(blocks.size(), 6)));
                    return ask("Rien \xC3\xA0 cr\xC3\xA9" "er : " + std::string(blocks.size() > 1 ? "les variables de l'automate" : "la variable de l'automate")
                               + (filter.empty() ? std::string{} : " qui r\xC3\xA9pondent \xC3\xA0 " + code(filter)) + " sont des **instances de bloc** ("
                               + codes(shown) + (blocks.size() > shown.size() ? ", ..." : "") + ") : un bloc fonction n'a pas d'\xC3\xA9quivalent "
                               "dans l'IHM. Cr\xC3\xA9" "e les variables IHM de ses sorties par leur nom et leur type, par exemple "
                               "`Cr\xC3\xA9" "e la variable PT1_Mesure en REAL`.");
                }
            }
            return ask(std::string("Rien \xC3\xA0 cr\xC3\xA9" "er : ") + (types ? "chaque type de l'automate" : "chaque variable de l'automate")
                       + (filter.empty() ? std::string{} : " qui r\xC3\xA9pond \xC3\xA0 " + code(filter)) + " a d\xC3\xA9j\xC3\xA0 son \xC3\xA9quivalent dans l'IHM"
                       + " (ou l'automate n'en donne aucun).");
        }
        std::size_t nv = 0, nt = 0;
        for (const auto& op : ops) {
            if (op.action == Operation::Action::Variables) nv += op.variables.size();
            if (op.action == Operation::Action::Type) ++nt;
        }
        std::vector<std::string> what;
        if (nt) what.push_back(plural(nt, "type IHM", "types IHM"));
        if (nv) what.push_back(plural(nv, "variable", "variables"));
        Proposal p = base(nv ? "Variables depuis l'automate" : "Types IHM depuis l'automate");
        p.ops = std::move(ops);
        const std::string source = in_.env.plcSource ? in_.env.plcSource() : std::string{};
        return finish(std::move(p), "Je propose de cr\xC3\xA9" "er **" + joinAnd(what) + "** depuis "
                                        + (source.empty() ? std::string("l'automate reli\xC3\xA9") : "l'automate de **" + source + "**")
                                        + (filter.empty() ? std::string{} : " (ceux qui r\xC3\xA9pondent \xC3\xA0 " + code(filter) + ")")
                                        + " : les types d'abord, dans l'ordre o\xC3\xB9 ils s'emploient. L'aper\xC3\xA7u les montre tous ; rien n'est "
                                          "cr\xC3\xA9\xC3\xA9 avant Appliquer.");
    }
    if (specs.empty()) {
        std::string why;
        if (!already.empty()) why = (already.size() > 1 ? "Les variables " : "La variable ") + codes(already) + (already.size() > 1 ? " existent" : " existe") + " d\xC3\xA9j\xC3\xA0. ";
        if (!invalid.empty()) why += codes(invalid) + " n'est pas un nom de variable valable. ";
        return ask(why + "Quelles variables ? \xC3\x89" "cris leurs noms et leur type, par exemple `Cr\xC3\xA9" "e les variables Pompe_3_Marche et "
                         "Pompe_4_Marche en BOOL`.",
                   {"Cr\xC3\xA9" "e une variable Niveau_Cuve REAL", "Cr\xC3\xA9" "e les variables de l'automate"});
    }
    // le type : dit, sinon tire du nom (une marche, un defaut : BOOL ; un niveau, une mesure : REAL) - et dit
    std::vector<std::string> guessed;
    for (auto& s : specs) {
        if (s.type.empty()) s.type = leading;
        if (s.type.empty()) {
            s.type = typeFromName(s.name);
            guessed.push_back(s.name + " : " + s.type);
        }
        static const std::set<std::string> numeric = {"INT", "DINT", "UINT", "UDINT", "WORD", "DWORD", "REAL", "LREAL"};
        s.initial = s.type == "BOOL" ? "FALSE" : numeric.count(s.type) ? "0" : std::string{};   // STRING, TIME, un type IHM : rien
    }
    Proposal p = base(specs.size() > 1 ? "Cr\xC3\xA9" "er " + std::to_string(specs.size()) + " variables" : "Cr\xC3\xA9" "er la variable " + specs.front().name);
    Operation op;
    op.action = Operation::Action::Variables;
    op.variables = specs;
    p.ops.push_back(std::move(op));
    std::vector<std::string> list;
    for (const auto& s : specs) list.push_back(code(s.name) + " (" + s.type + ")");
    std::string text = "Je propose de cr\xC3\xA9" "er **" + plural(specs.size(), "variable", "variables") + "** dans les variables de l'IHM : " + joinAnd(list) + ".";
    if (!guessed.empty()) text += "\nLe type n'\xC3\xA9tait pas dit : je l'ai tir\xC3\xA9 du nom (" + joinAnd(guessed) + ") - redemande avec le type s'il ne convient pas.";
    if (!already.empty()) text += "\n" + codes(already) + (already.size() > 1 ? " existent" : " existe") + " d\xC3\xA9j\xC3\xA0 : rien \xC3\xA0 faire pour " + (already.size() > 1 ? "elles." : "elle.");
    text += "\nL'aper\xC3\xA7u les montre avant d'appliquer ; Ctrl+Z les retire.";
    return finish(std::move(p), std::move(text));
}

// ---- un type IHM (une structure) ------------------------------------------------------------------
// "Cree un type T_Pompe avec Marche en BOOL et Vitesse en REAL" : l'operation creer_type (comme Types IHM > Nouveau).
// 1.16.1 : LA PHRASE EST LUE, PAS DECOUPEE. Le nom vient de "qui s'appelle X", "nomme X", "appele X", "la structure X",
// "un type X" - jamais d'un mot-outil ni d'un verbe (sans nom sur, elle demande) ; les membres, de la liste en langage
// courant ("son type, sa mesure, ses defauts", "avec une mesure, un defaut et un etat", "comme la mesure et l'unite"),
// sans les articles, les possessifs ni le remplissage ("des membres de gestion", "par exemple", "etc"). Un type qui decrit
// un equipement (un capteur, une pompe, une vanne...) sans membres dits part d'un MODELE METIER, et le dit.

// Les modeles metier : des types classiques de l'industrie (des membres sans accent, des types elementaires, decrits).
struct TypeModel {
    const char*                                        key;      // "pompe"
    const char*                                        label;    // "pompe" (dans la reponse)
    const char*                                        name;     // le nom propose : T_Pompe
    const char*                                        description;
    std::vector<Operation::MemberSpec>                 members;
};
const std::vector<TypeModel>& typeModels() {
    static const std::vector<TypeModel> k = {
        {"capteur", "capteur analogique", "T_Capteur", "Capteur analogique : la mesure, son \xC3\xA9" "chelle, ses seuils et ses alarmes",
         {{"Mesure", "REAL", "Valeur mesur\xC3\xA9" "e, en unit\xC3\xA9 physique"},
          {"Unite", "STRING", "Unit\xC3\xA9 de la mesure (bar, \xC2\xB0" "C, m3/h...)"},
          {"Min", "REAL", "D\xC3\xA9" "but d'\xC3\xA9" "chelle"},
          {"Max", "REAL", "Fin d'\xC3\xA9" "chelle"},
          {"SeuilHaut", "REAL", "Seuil d'alarme haute"},
          {"SeuilBas", "REAL", "Seuil d'alarme basse"},
          {"AlarmeHaute", "BOOL", "Mesure au-dessus du seuil haut"},
          {"AlarmeBasse", "BOOL", "Mesure au-dessous du seuil bas"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut capteur (hors \xC3\xA9" "chelle, rupture de fil)"}}},
        {"capteur_tor", "capteur TOR", "T_CapteurTOR", "Capteur tout ou rien : d\xC3\xA9tecteur, fin de course, contact",
         {{"Etat", "BOOL", "\xC3\x89tat du contact (vrai : d\xC3\xA9tect\xC3\xA9)"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut du capteur (discordance, c\xC3\xA2" "ble coup\xC3\xA9)"},
          {"Inverse", "BOOL", "Logique invers\xC3\xA9" "e (contact \xC3\xA0 ouverture)"},
          {"Filtre", "TIME", "Temps de filtrage (anti-rebond)"},
          {"Forcage", "BOOL", "For\xC3\xA7" "age actif (maintenance)"}}},
        {"pompe", "pompe", "T_Pompe", "Pompe : commande, retour de marche, modes et compteurs",
         {{"Marche", "BOOL", "Retour de marche (la pompe tourne)"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut (disjoncteur, thermique, discordance)"},
          {"Auto", "BOOL", "Mode automatique (faux : manuel)"},
          {"Commande", "BOOL", "Ordre de marche envoy\xC3\xA9 \xC3\xA0 la pompe"},
          {"Disponible", "BOOL", "Pr\xC3\xAAte \xC3\xA0 d\xC3\xA9marrer (sans d\xC3\xA9" "faut, en automatique)"},
          {"HeuresMarche", "DINT", "Compteur d'heures de marche"},
          {"NbDemarrages", "DINT", "Nombre de d\xC3\xA9marrages"}}},
        {"vanne", "vanne", "T_Vanne", "Vanne tout ou rien : commande et fins de course",
         {{"Ouverte", "BOOL", "Fin de course ouverte"},
          {"Fermee", "BOOL", "Fin de course ferm\xC3\xA9" "e"},
          {"Commande", "BOOL", "Ordre d'ouverture"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut (discordance : ni ouverte ni ferm\xC3\xA9" "e apr\xC3\xA8s le temps de man\xC5\x93uvre)"},
          {"Auto", "BOOL", "Mode automatique (faux : manuel)"},
          {"TempsManoeuvre", "TIME", "Temps maximal d'ouverture ou de fermeture"}}},
        {"moteur", "moteur", "T_Moteur", "Moteur : commande, retour de marche, d\xC3\xA9" "fauts et compteurs",
         {{"Marche", "BOOL", "Retour de marche (contacteur ferm\xC3\xA9)"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut (disjoncteur, discordance)"},
          {"DefautThermique", "BOOL", "D\xC3\xA9" "faut thermique (relais de protection)"},
          {"Auto", "BOOL", "Mode automatique (faux : manuel)"},
          {"Commande", "BOOL", "Ordre de marche"},
          {"Courant", "REAL", "Courant absorb\xC3\xA9 (A)"},
          {"HeuresMarche", "DINT", "Compteur d'heures de marche"}}},
        {"variateur", "variateur de vitesse", "T_Variateur", "Variateur de vitesse : commande, consigne, vitesse et \xC3\xA9tat",
         {{"Marche", "BOOL", "Retour de marche"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut variateur"},
          {"Pret", "BOOL", "Pr\xC3\xAAt (sous tension, sans d\xC3\xA9" "faut)"},
          {"Auto", "BOOL", "Mode automatique (faux : manuel)"},
          {"Commande", "BOOL", "Ordre de marche"},
          {"Consigne", "REAL", "Consigne de vitesse (%)"},
          {"Vitesse", "REAL", "Vitesse mesur\xC3\xA9" "e (tr/min)"},
          {"Courant", "REAL", "Courant moteur (A)"}}},
        {"cuve", "cuve", "T_Cuve", "Cuve : niveau, volume et alarmes de niveau",
         {{"Niveau", "REAL", "Niveau (%)"},
          {"Volume", "REAL", "Volume contenu (m3)"},
          {"Capacite", "REAL", "Capacit\xC3\xA9 de la cuve (m3)"},
          {"NiveauHaut", "BOOL", "Niveau haut atteint (d\xC3\xA9tecteur)"},
          {"NiveauBas", "BOOL", "Niveau bas atteint (d\xC3\xA9tecteur)"},
          {"AlarmeHaute", "BOOL", "Alarme de niveau tr\xC3\xA8s haut"},
          {"AlarmeBasse", "BOOL", "Alarme de niveau tr\xC3\xA8s bas"}}},
        {"regulateur", "r\xC3\xA9gulateur PID", "T_Regulateur", "R\xC3\xA9gulateur PID : mesure, consigne, sortie et r\xC3\xA9glages",
         {{"Mesure", "REAL", "Mesure (grandeur r\xC3\xA9gl\xC3\xA9" "e)"},
          {"Consigne", "REAL", "Consigne"},
          {"Sortie", "REAL", "Sortie (%)"},
          {"Auto", "BOOL", "Mode automatique (faux : manuel)"},
          {"Kp", "REAL", "Gain proportionnel"},
          {"Ti", "REAL", "Temps int\xC3\xA9gral (s)"},
          {"Td", "REAL", "Temps d\xC3\xA9riv\xC3\xA9 (s)"},
          {"Defaut", "BOOL", "D\xC3\xA9" "faut de r\xC3\xA9gulation (\xC3\xA9" "cart trop grand)"}}},
    };
    return k;
}
const TypeModel* typeModel(std::string_view key) {
    for (const auto& m : typeModels())
        if (key == m.key) return &m;
    return nullptr;
}
// Le modele d'un mot (replie) : capteur, sonde, transmetteur -> capteur analogique ; detecteur -> capteur TOR ; bac,
// reservoir -> cuve... ; vide : aucun.
std::string modelWord(const std::string& f) {
    static const std::map<std::string, std::string> k = {
        {"capteur", "capteur"}, {"sonde", "capteur"}, {"transmetteur", "capteur"}, {"instrument", "capteur"}, {"analogique", "capteur"},
        {"detecteur", "capteur_tor"}, {"pompe", "pompe"}, {"vanne", "vanne"}, {"electrovanne", "vanne"}, {"robinet", "vanne"},
        {"moteur", "moteur"}, {"variateur", "variateur"}, {"cuve", "cuve"}, {"bac", "cuve"}, {"reservoir", "cuve"}, {"citerne", "cuve"},
        {"regulateur", "regulateur"}, {"regulation", "regulateur"}, {"regul", "regulateur"}, {"pid", "regulateur"}};
    if (const auto it = k.find(f); it != k.end()) return it->second;
    if (f.size() > 3 && (f.back() == 's' || f.back() == 'x'))
        if (const auto it = k.find(f.substr(0, f.size() - 1)); it != k.end()) return it->second;
    return {};
}
// Un nom de membre ramene a son singulier, pour comparer (Defauts et Defaut, AlarmesHautes...).
std::string memberKey(const std::string& name) {
    std::string out;
    for (const auto& w : nameWords(name)) out += (w.size() > 3 && (w.back() == 's' || w.back() == 'x')) ? w.substr(0, w.size() - 1) : w;
    return out;
}
// Un nom de membre ecrit en langage courant : chaque mot sans accent, une majuscule en tete ("seuil haut" -> SeuilHaut) ;
// un mot deja ecrit comme un nom (SeuilHaut, Marche, PT1_Mesure) reste tel quel.
std::string memberName(const std::vector<Tok>& words) {
    if (words.size() == 1) {
        const std::string& raw = words.front().raw;
        const bool written = hmi::isIdentifier(raw) && std::any_of(raw.begin(), raw.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)); });
        if (words.front().quoted) return sanitize(raw);
        if (written || (hmi::isIdentifier(raw) && raw.find('_') != std::string::npos)) return raw;
    }
    std::string out;
    for (const auto& w : words) {
        std::string s = sanitize(w.raw);
        s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
        if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        out += s;
    }
    return out;
}
// Le mot-cle d'un type a creer : "type", "structure" (un type IHM), pas "de type REAL", "son type" (une precision, un membre).
std::size_t typeNoun(const std::vector<Tok>& t) {
    for (std::size_t i = 0; i < t.size(); ++i) {
        const std::string& f = t[i].f;
        if (t[i].quoted || !(f == "type" || f == "structure" || f == "struct" || f == "udt" || f == "dut")) continue;
        if (i > 0) {
            const std::string& p = t[i - 1].f;
            if (p == "de" || p == "du" || p == "d" || p == "son" || p == "sa" || p == "ses" || p == "leur" || p == "leurs" || p == "quel"
                || p == "quels" || p == "quelle" || p == "en" || p == "le" || p == "au" || p == "aux" || p == "a" || p == "dans" || p == "ce"
                || p == "cet" || p == "cette" || p == "chaque" || p == "meme")
                return std::string::npos;
        }
        return i;
    }
    return std::string::npos;
}

LocalReply Engine::createType() const {
    if (!in_.project) return ask("Ouvre d'abord un projet IHM.");
    // les mots, et les virgules : elles separent les membres ("son type, sa mesure")
    std::vector<Tok> u;
    {
        std::string piece;
        const auto flush = [&] {
            for (auto& tk : tokenize(piece)) u.push_back(std::move(tk));
            piece.clear();
        };
        for (const char c : request_) {
            if (c == ',' || c == ';' || c == '\n' || c == '(' || c == ')') {
                flush();
                u.push_back({",", ",", false});
            } else {
                piece += c;
            }
        }
        flush();
    }
    const std::size_t at = typeNoun(u);
    if (at == std::string::npos) return notUnderstood();
    const auto typeOf = [&](const Tok& tk) -> std::string {
        if (tk.quoted) return {};
        if (std::string ty = typeWord(tk.f); !ty.empty()) return ty;
        if (const auto* ht = in_.project->hmiTypeByName(tk.raw)) return ht->name;
        return {};
    };
    const auto nameLike = [](const Tok& tk) {
        if (tk.quoted || tk.raw.find('_') != std::string::npos) return true;
        if (std::any_of(tk.raw.begin(), tk.raw.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) return true;
        const auto c0 = static_cast<unsigned char>(tk.raw[0]);
        return std::isupper(c0) || (c0 == 0xC3 && tk.raw.size() > 1 && static_cast<unsigned char>(tk.raw[1]) >= 0x80 && static_cast<unsigned char>(tk.raw[1]) <= 0x9E);
    };
    const auto asName = [](const Tok& tk) { return tk.quoted || !hmi::isIdentifier(tk.raw) ? sanitize(tk.raw) : tk.raw; };
    // 1. LE NOM, tel qu'ecrit : "qui s'appelle X", "nomme(e) X", "appele(e) X", "dont le nom est X" ; sinon le mot juste apres
    // "type" ("la structure X", "un type X") s'il est ecrit comme un nom (une majuscule, un _, un chiffre) - un mot du
    // metier en minuscules ("un type pompe") dit ce que le type decrit, pas son nom. Jamais un mot-outil ni un verbe.
    std::string name, refused;
    std::size_t nameAt = std::string::npos, namingAt = std::string::npos;
    for (std::size_t i = 0; i < u.size() && nameAt == std::string::npos; ++i) {
        const std::string& f = u[i].f;
        if (u[i].quoted || !(f.rfind("appel", 0) == 0 || f.rfind("nomm", 0) == 0 || f == "nom")) continue;
        std::size_t j = i + 1;
        while (j < u.size() && (u[j].f == "le" || u[j].f == "la" || u[j].f == "l" || u[j].f == "est" || u[j].f == "sera" || u[j].f == "serait"
                                || u[j].f == "de" || u[j].f == "d" || u[j].f == "du" || u[j].f == "simplement"))
            ++j;
        if (j >= u.size() || u[j].f == ",") continue;
        if (f == "nom" && !nameLike(u[j])) continue;   // "un nom de type", "le nom de la pompe"
        namingAt = i;
        nameAt = j;
        if (badName(u[j].raw) || !typeWord(u[j].f).empty()) refused = u[j].raw;
        else name = asName(u[j]);
    }
    std::size_t k = at + 1;
    while (k < u.size() && (u[k].f == "ihm" || u[k].f == "structure" || u[k].f == "structuree" || u[k].f == "struct" || u[k].f == "utilisateur"
                            || u[k].f == "dut" || u[k].f == "udt"
                            || u[k].f == "personnalise" || u[k].f == "personnalisee" || u[k].f == "complet" || u[k].f == "simple"
                            || u[k].f == "donnee" || u[k].f == "donnees"
                            || ((u[k].f == "de" || u[k].f == "d") && k + 1 < u.size() && (u[k + 1].f == "donnee" || u[k + 1].f == "donnees"))))
        ++k;
    // (un type IHM qui existe deja, "un type T_Pompe" : c'est bien le nom voulu - deja pris, ce sera dit)
    if (nameAt == std::string::npos && k < u.size() && u[k].f != "," && typeWord(u[k].f).empty() && !functionWord(u[k].f) && !requestVerb(u[k].f)
        && !fillerWord(u[k].f) && !isDigits(u[k].f)) {
        if (nameLike(u[k]) || modelWord(u[k].f).empty()) {
            nameAt = k;
            if (badName(u[k].raw)) refused = u[k].raw;
            else name = asName(u[k]);
        }
    }
    if (!name.empty() && !hmi::isIdentifier(name)) {
        refused = name;
        name.clear();
    }
    // 2. OU COMMENCENT LES MEMBRES : apres "avec", "contenant", "comme"... ; sinon apres le nom ; sinon nulle part (un type
    // "pour gerer les capteurs" n'en dit aucun).
    std::size_t from = std::string::npos;
    for (std::size_t i = at + 1; i < u.size() && from == std::string::npos; ++i) {
        const std::string& f = u[i].f;
        if (i == nameAt) continue;
        if (f == "avec" || f == "contenant" || f == "comprenant" || f == "incluant" || f == "ayant" || f == "comme" || f.rfind("compos", 0) == 0
            || f.rfind("constitu", 0) == 0 || ((f == "contient" || f == "comprend" || f == "a") && i > 0 && u[i - 1].f == "qui"))
            from = i + 1;
    }
    // (sauf un but : "un type T_Dosage pour une pompe" decrit le type, il ne liste pas ses membres)
    if (from == std::string::npos && nameAt != std::string::npos && nameAt + 1 < u.size()) {
        static const std::set<std::string> aim = {"pour", "qui", "de", "d", "du", "des", "dans", "sur", "afin", "en", "a", "au"};
        if (!aim.count(u[nameAt + 1].f)) from = nameAt + 1;
    }
    // 3. CE QUE LE TYPE DECRIT (avant les membres, et son nom) : un modele metier
    const std::size_t subjectEnd = from == std::string::npos ? u.size() : from;
    std::string modelKey;
    std::size_t modelAt = std::string::npos;
    for (std::size_t i = 0; i < subjectEnd; ++i)
        if (const std::string m = u[i].quoted ? std::string{} : modelWord(u[i].f); !m.empty() && (modelAt == std::string::npos || i < modelAt)) {
            modelKey = m;
            modelAt = i;
        }
    if (modelKey.empty() && !name.empty())
        for (const auto& w : nameWords(name))
            if (const std::string m = modelWord(w); !m.empty()) {
                modelKey = m;
                break;
            }
    const auto said = [&](std::string_view w) {
        for (const auto& tk : u)
            if (tk.f == w) return true;
        return false;
    };
    if (modelKey == "capteur" && (said("tor") || said("detecteur") || said("course") || said("contact") || said("rien")
                                  || (!name.empty() && fold(name).find("tor") != std::string::npos)))
        modelKey = "capteur_tor";
    if (modelKey == "moteur" && said("variateur")) modelKey = "variateur";
    const TypeModel* model = modelKey.empty() ? nullptr : typeModel(modelKey);
    // 4. LES MEMBRES en langage courant : une virgule, "et", "ou", un article ou un possessif en commencent un autre ; "de"
    // lie ("heures de marche" -> HeuresMarche), un adjectif aussi ("alarme haute" -> AlarmeHaute) ; un type dit ("en
    // BOOL", "de type REAL") vaut pour les membres ecrits depuis le type d'avant.
    std::vector<Operation::MemberSpec> members;
    std::vector<bool> typed;   // le type du membre a ete dit
    std::vector<std::size_t> pending;
    std::string leading;
    std::vector<std::string> invalid;
    std::vector<Tok> cur;
    bool join = false;
    const auto close = [&] {
        if (cur.empty()) return;
        const std::string mn = memberName(cur);
        cur.clear();
        join = false;
        if (mn.empty() || !hmi::isIdentifier(mn) || badName(mn)) {
            if (!mn.empty() && !badName(mn)) invalid.push_back(mn);
            return;
        }
        if (!name.empty() && norm(mn) == norm(name)) return;
        if (std::any_of(members.begin(), members.end(), [&](const auto& m) { return memberKey(m.name) == memberKey(mn); })) return;
        pending.push_back(members.size());
        members.push_back({mn, {}, {}});
        typed.push_back(false);
    };
    static const std::set<std::string> modifiers = {"haut", "haute", "bas", "basse", "min", "max", "mini", "maxi", "minimum", "maximum",
                                                    "nominal", "nominale", "moyen", "moyenne", "total", "totale", "tres", "general",
                                                    "generale", "thermique", "instantane", "instantanee"};
    for (std::size_t i = from == std::string::npos ? u.size() : from; i < u.size(); ++i) {
        const Tok& tk = u[i];
        if (i == nameAt || i == namingAt || tk.f == ",") {
            close();
            continue;
        }
        if (const std::string ty = typeOf(tk); !ty.empty()) {
            close();
            if (pending.empty() && members.empty()) leading = ty;
            for (const auto m : pending) {
                members[m].type = ty;
                typed[m] = true;
            }
            pending.clear();
            continue;
        }
        if (tk.f == "type" && i + 1 < u.size() && !typeOf(u[i + 1]).empty()) continue;   // "de type REAL"
        if ((tk.f == "de" || tk.f == "du" || tk.f == "d") && !cur.empty() && i + 1 < u.size() && typeOf(u[i + 1]).empty()
            && !(u[i + 1].f == "type" && i + 2 < u.size() && !typeOf(u[i + 2]).empty())) {
            join = true;
            continue;
        }
        if (join && (tk.f == "la" || tk.f == "l" || tk.f == "le" || tk.f == "les")) continue;
        if (!tk.quoted && !cur.empty() && modifiers.count(tk.f)) {
            cur.push_back(tk);
            continue;
        }
        if (!tk.quoted && (functionWord(tk.f) || fillerWord(tk.f) || requestVerb(tk.f) || isDigits(tk.f) || tk.f == "ou")) {
            close();
            continue;
        }
        if (tk.quoted || tk.raw.find('_') != std::string::npos) {   // un nom ecrit tel quel : un membre a lui seul
            close();
            cur.push_back(tk);
            close();
            continue;
        }
        if (!cur.empty() && !join) close();
        cur.push_back(tk);
        join = false;
        if (cur.size() >= 4) close();
    }
    close();
    // la liste est ouverte ("par exemple", "etc", "notamment", "entre autres") : le modele la complete
    const bool openList = said("exemple") || said("etc") || said("notamment") || said("cetera") || (said("entre") && said("autres"));
    std::vector<std::string> added;
    // 5. LE TYPE DE CHAQUE MEMBRE : dit ; sinon celui du modele (le meme membre) ; sinon tire du nom - et dit
    std::vector<std::string> guessed;
    for (std::size_t i = 0; i < members.size(); ++i) {
        auto& m = members[i];
        if (m.type.empty() && !leading.empty()) {
            m.type = leading;
            typed[i] = true;
        }
        const Operation::MemberSpec* same = nullptr;
        if (model)
            for (const auto& mm : model->members)
                if (memberKey(mm.name) == memberKey(m.name)) same = &mm;
        if (same) m.description = same->description;
        if (m.type.empty()) m.type = same ? same->type : typeFromName(m.name);
        if (!typed[i]) guessed.push_back(m.name + " : " + m.type);
    }
    if (model && openList && !members.empty())
        for (const auto& mm : model->members)
            if (std::none_of(members.begin(), members.end(), [&](const auto& m) { return memberKey(m.name) == memberKey(mm.name); })) {
                members.push_back(mm);
                added.push_back(mm.name);
            }
    const bool fromModel = model && members.empty();
    if (fromModel) members = model->members;
    // 6. LE NOM : celui qui est dit ; sinon celui du modele (T_Pompe) ; sinon, demander
    std::vector<std::string> sugg = {"Cr\xC3\xA9" "e un type pour une pompe", "Cr\xC3\xA9" "e un type pour un capteur",
                                     "Cr\xC3\xA9" "e un type T_Pompe avec Marche en BOOL"};
    if (!refused.empty() && name.empty())
        return ask(guill(refused) + " ne peut pas \xC3\xAAtre le nom du type : c'est un mot de la phrase, pas un nom. Quel nom lui donner ? "
                   "\xC3\x89" "cris-le apr\xC3\xA8s \xC2\xAB qui s'appelle \xC2\xBB ou \xC2\xAB nomm\xC3\xA9 \xC2\xBB, par exemple `Cr\xC3\xA9" "e une "
                   "structure nomm\xC3\xA9" "e T_Capteur avec Mesure en REAL`.",
                   sugg);
    if (members.empty()) {
        if (!name.empty())
            return ask("Quels membres pour le type " + code(name) + " ? \xC3\x89" "cris-les avec leur type, par exemple `Cr\xC3\xA9" "e un type " + name
                           + " avec Marche en BOOL et Vitesse en REAL`. Je connais aussi des **mod\xC3\xA8les de d\xC3\xA9part** : capteur, capteur TOR, "
                             "pompe, vanne, moteur, variateur, cuve, r\xC3\xA9gulateur (`Cr\xC3\xA9" "e un type pour une pompe`).",
                       {"Cr\xC3\xA9" "e un type " + name + " avec Marche en BOOL", "Cr\xC3\xA9" "e un type pour une pompe"});
        // une phrase qui dit autre chose (un truc, un mot inconnu) : pas comprise - une IA connectee le peut peut-etre
        bool unclear = false;
        for (const auto& tk : u) {
            if (tk.f == "," || tk.f == u[at].f) continue;
            if (vagueWord(tk.f)) unclear = true;
            else if (!functionWord(tk.f) && !fillerWord(tk.f) && !requestVerb(tk.f) && typeWord(tk.f).empty() && !isDigits(tk.f) && tk.f != "ihm"
                     && tk.f != "type" && tk.f != "structure" && tk.f != "struct")
                unclear = true;
        }
        if (unclear) {
            LocalReply r = ask("Je n'ai pas compris quel **type IHM** cr\xC3\xA9" "er. Dis-moi son nom et ses membres, par exemple `Cr\xC3\xA9" "e un type "
                               "T_Pompe avec Marche en BOOL et Vitesse en REAL` ; ou ce qu'il d\xC3\xA9" "crit : je connais des **mod\xC3\xA8les de "
                               "d\xC3\xA9part** (capteur, capteur TOR, pompe, vanne, moteur, variateur, cuve, r\xC3\xA9gulateur).",
                               sugg);
            r.understood = false;
            return r;
        }
        return ask("Quel nom pour le type, et que d\xC3\xA9" "crit-il ? Par exemple `Cr\xC3\xA9" "e un type T_Pompe avec Marche en BOOL`, ou "
                   "`Cr\xC3\xA9" "e un type pour une pompe` (un **mod\xC3\xA8le de d\xC3\xA9part** : capteur, capteur TOR, pompe, vanne, moteur, "
                   "variateur, cuve, r\xC3\xA9gulateur).",
                   sugg);
    }
    std::string text;
    if (name.empty() && model) {
        name = model->name;
        if (in_.project->hmiTypeByName(name)) {
            std::string other;
            for (int n = 2; n < 1000 && other.empty(); ++n)
                if (!in_.project->hmiTypeByName(name + "_" + std::to_string(n))) other = name + "_" + std::to_string(n);
            text = code(name) + " existe d\xC3\xA9j\xC3\xA0 dans le projet : je propose " + code(other) + ". ";
            name = other;
        }
    }
    if (name.empty()) {
        std::vector<std::string> list;
        for (const auto& m : members) list.push_back(m.name);
        return ask("Quel nom pour le type qui aura " + plural(members.size(), "membre", "membres") + " (" + codes(list, 6) + ") ? \xC3\x89" "cris-le "
                   "apr\xC3\xA8s \xC2\xAB type \xC2\xBB ou \xC2\xAB nomm\xC3\xA9 \xC2\xBB, par exemple `Cr\xC3\xA9" "e un type T_Moteur avec Marche et "
                   "Defaut`.",
                   {"Cr\xC3\xA9" "e un type T_Moteur avec Marche et Defaut"});
    }
    if (const auto* ht = in_.project->hmiTypeByName(name)) {
        std::string other;
        for (int n = 2; n < 1000 && other.empty(); ++n)
            if (!in_.project->hmiTypeByName(name + "_" + std::to_string(n))) other = name + "_" + std::to_string(n);
        return ask("Le type IHM " + code(ht->name) + " existe d\xC3\xA9j\xC3\xA0 dans le projet : choisis un autre nom, par exemple " + code(other) + ".");
    }
    Proposal p = base("Cr\xC3\xA9" "er le type " + name);
    Operation op;
    op.action = Operation::Action::Type;
    op.name = name;
    op.members = members;
    if (model && (fromModel || !added.empty())) op.props.emplace_back("description", model->description);
    p.ops.push_back(std::move(op));
    std::vector<std::string> list;
    for (const auto& m : members) list.push_back(code(m.name) + " (" + m.type + ")");
    if (fromModel) {
        text += "Voici un **mod\xC3\xA8le de d\xC3\xA9part**, \xC3\xA0 adapter : le **type IHM " + name + "** (" + model->label + ", un mod\xC3\xA8le classique de "
                "l'industrie), avec " + plural(members.size(), "membre", "membres") + " : " + joinAnd(list, 20) + ". Chaque membre a sa description. "
                "Pour d'autres membres, dis-les : `Cr\xC3\xA9" "e un type " + name + " avec Marche et Defaut`.";
    } else {
        text += "Je propose de cr\xC3\xA9" "er le **type IHM " + name + "** (une structure, comme Types IHM \xE2\x80\xBA Nouveau), avec "
                + plural(members.size(), "membre", "membres") + " : " + joinAnd(list, 20) + ".";
        if (!added.empty())
            text += "\nTa liste \xC3\xA9tait ouverte (\xC2\xAB par exemple \xC2\xBB, \xC2\xAB etc \xC2\xBB) : je l'ai compl\xC3\xA9t\xC3\xA9" "e avec le mod\xC3\xA8le "
                    + std::string(model->label) + " (" + codes(added, 12) + "). Redemande sans \xC2\xAB par exemple \xC2\xBB pour n'avoir que les tiens.";
        if (!guessed.empty())
            text += "\nLe type n'\xC3\xA9tait pas dit : je l'ai tir\xC3\xA9 du nom (" + joinAnd(guessed) + ") - redemande avec le type s'il ne convient pas.";
    }
    if (!invalid.empty()) text += "\n" + codes(invalid) + " : pas un nom de membre valable (des lettres sans accent, des chiffres et _), laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9.";
    text += "\nL'aper\xC3\xA7u le montre avant d'appliquer ; Ctrl+Z le retire. Ensuite : `Cr\xC3\xA9" "e la variable Etat_P1 en " + name + "`.";
    return finish(std::move(p), std::move(text));
}

// ---- associer une variable ---------------------------------------------------------------------
LocalReply Engine::bind() const {
    if (!view()) return needView();
    // l'objet : avant le mot de liaison (a, au, avec, sur, variable) s'il est la, sinon le premier designe
    std::size_t verb = indexOfPrefix("associ");
    if (verb == std::string::npos) verb = indexOfPrefix("reli");
    if (verb == std::string::npos) verb = indexOfPrefix("lie");
    if (verb == std::string::npos) verb = indexOfPrefix("branch");
    if (verb == std::string::npos) verb = indexOfPrefix("connect");
    if (verb == std::string::npos) verb = indexOfPrefix("anim");
    if (verb == std::string::npos) verb = 0;
    std::size_t link = std::string::npos;
    for (std::size_t i = verb + 1; i < t_.size(); ++i)
        if ((t_[i].f == "a" || t_[i].f == "au" || t_[i].f == "avec" || t_[i].f == "sur" || t_[i].f.rfind("variable", 0) == 0) && i > verb + 1) {
            link = i;
            break;
        }
    Target t = link == std::string::npos ? targets(verb + 1, t_.size(), false) : targets(verb + 1, link, false);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    if (t.objects.size() != 1) {
        std::vector<std::string> n;
        for (const auto* o : t.objects) n.push_back(o->name);
        return ask("J'associe une variable \xC3\xA0 **un** objet \xC3\xA0 la fois : lequel, parmi " + codes(n) + " ?");
    }
    const hmi::Object& o = *t.objects.front();
    if (!o.find("variable")) return ask(code(o.name) + " (" + std::string(hmi::kindLabel(o.kind)) + ") ne se relie pas \xC3\xA0 une variable.");
    std::size_t from = link == std::string::npos ? verb + 1 : link;
    // apres l'objet (s'il est ecrit apres le lien : "associe a Pompe_1_Marche la pompe")
    VarFind vf = findVariable(from, t_.size(), &o);
    if (vf.name.empty() && vf.asked.empty() && vf.candidates.empty() && link != std::string::npos) vf = findVariable(verb + 1, link, &o);
    if (!vf.name.empty()) {
        Proposal p = base(o.name + kArrow + vf.name);
        Operation op;
        op.action = Operation::Action::Bind;
        op.name = o.name;
        op.variable = vf.name;
        p.ops.push_back(std::move(op));
        const bool shows = registry().find(o.kind) && registry().find(o.kind)->writes;
        return finish(std::move(p), "Je propose d'associer **" + o.name + "** \xC3\xA0 la variable **" + vf.name + "** (sa propri\xC3\xA9t\xC3\xA9 `variable` : "
                                        + (shows ? std::string("la commande \xC3\xA9" "crit dans cette variable).") : std::string("son \xC3\xA9tat viendra de l\xC3\xA0).")));
    }
    if (!vf.asked.empty()) {
        std::vector<std::string> sugg;
        // un nom ecrit (Pompe_9_Marche) ou un mot qui decrit (le niveau) : dit autrement ; pour un mot, le nom
        // propose a la creation est parlant (Niveau_Cuve_1), pas le mot seul
        const bool described = !looksLikeObjectName({vf.asked, foldWord(vf.asked), false}) && vf.asked.find('_') == std::string::npos
                            && vf.asked.find('.') == std::string::npos;
        std::string fresh = vf.asked;
        if (described) {
            fresh = sanitize(vf.asked) + "_" + o.name;
            if (!hmi::isIdentifier(fresh) || exists(fresh)) fresh.clear();
        }
        std::string text = described ? "**Aucune variable du projet ne r\xC3\xA9pond \xC3\xA0 " + guill(vf.asked) + "**, et je n'en invente pas : une "
                                       "variable fausse ne s'allumerait jamais en exploitation."
                                     : "**La variable " + vf.asked + " n'existe pas** dans ce projet, et je ne l'invente pas : une variable "
                                       "fausse ne s'allumerait jamais en exploitation.";
        // d'abord les candidates, puis (ou, s'il y en a) la phrase pour la creer
        if (!vf.candidates.empty()) {
            text += "\n- les plus proches : " + codes(vf.candidates) + " ;";
            for (std::size_t i = 0; i < vf.candidates.size() && i < 2; ++i) sugg.push_back("Associe " + o.name + " \xC3\xA0 " + vf.candidates[i]);
        }
        if (!fresh.empty() && hmi::isIdentifier(fresh)) {
            const std::string ty = hmi::roles::valueRole(o.kind) == hmi::roles::Role::Number ? "REAL" : "BOOL";
            text += std::string(vf.candidates.empty() ? "\n- cr\xC3\xA9" "e-la d'abord : `" : "\n- ou cr\xC3\xA9" "e-la d'abord : `") + "Cr\xC3\xA9" "e la variable " + fresh
                    + " en " + ty + "`, puis redemande l'association.";
            sugg.push_back("Cr\xC3\xA9" "e la variable " + fresh + " en " + ty);
        }
        std::erase_if(sugg, [](const std::string& s) { return chars(s) > kSuggestionMax; });
        return ask(std::move(text), std::move(sugg));
    }
    if (!vf.candidates.empty()) {
        std::vector<std::string> sugg;
        for (std::size_t i = 0; i < vf.candidates.size() && i < 4; ++i) sugg.push_back("Associe " + o.name + " \xC3\xA0 " + vf.candidates[i]);
        return ask("Plusieurs variables conviennent : " + codes(vf.candidates) + ". Laquelle associer \xC3\xA0 **" + o.name + "** ?", std::move(sugg));
    }
    return ask("Quelle variable associer \xC3\xA0 **" + o.name + "** ? \xC3\x89" "cris son nom, par exemple `Associe " + o.name + " \xC3\xA0 Pompe_1_Marche`.");
}

// ---- animer : clignoter, une couleur selon une variable, visible selon une variable ------------
LocalReply Engine::animate() const {
    if (!view()) return needView();
    // la condition : apres "quand", "si", "lorsque"
    std::size_t cond = std::string::npos;
    for (std::size_t i = 0; i < t_.size(); ++i)
        if (t_[i].f == "quand" || t_[i].f == "si" || t_[i].f == "lorsque" || t_[i].f == "lorsqu") {
            cond = i;
            break;
        }
    if (cond == std::string::npos) return notUnderstood();
    std::size_t condEnd = t_.size();
    const std::size_t otherwise = indexOf("sinon", cond);
    if (otherwise != std::string::npos) condEnd = otherwise;
    // l'objet : avant la condition
    Target t = targets(0, cond, true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    if (t.objects.empty()) return ask("Quel objet animer ? \xC3\x89" "cris son nom.");
    // la condition : une variable existante, peut-etre comparee a un nombre ("Niveau_Cuve_1 > 80", "depasse 80")
    VarFind vf = findVariable(cond + 1, condEnd, t.objects.front());
    if (vf.name.empty()) {
        if (!vf.asked.empty())
            return ask("**La variable " + vf.asked + " n'existe pas** dans ce projet, et je ne l'invente pas."
                       + (vf.candidates.empty() ? std::string{} : " Les plus proches : " + codes(vf.candidates) + "."),
                       vf.candidates.empty() ? std::vector<std::string>{} : std::vector<std::string>{});
        if (!vf.candidates.empty()) return ask("Quelle variable ? Plusieurs conviennent : " + codes(vf.candidates) + ".");
        return ask("Quelle variable pour la condition ? \xC3\x89" "cris son nom exact (par exemple `quand Defaut_V1`).");
    }
    std::string condition = vf.name;
    bool negate = false;
    for (std::size_t i = cond + 1; i < condEnd; ++i) {
        const std::string& w = t_[i].f;
        std::string op;
        if (w == ">" || w == "<" || w == ">=" || w == "<=" || w == "=" || w == "<>") op = w;
        else if (w == "depasse" || w == "superieur" || w == "plus") op = ">";
        else if (w == "inferieur" || w == "moins") op = "<";
        else if (w == "faux" || w == "false" || phrase("n est pas")) negate = true;
        if (op.empty()) continue;
        for (std::size_t j = i + 1; j < condEnd; ++j)
            if (isDigits(t_[j].f) || (t_[j].f.find('.') != std::string::npos && std::isdigit(static_cast<unsigned char>(t_[j].f[0])))) {
                condition = vf.name + " " + op + " " + t_[j].raw;
                break;
            }
        break;
    }
    if (negate) condition = "NOT " + condition;
    Proposal p = base("Animer " + t.objects.front()->name);
    std::vector<std::string> lines;
    const bool blink = hasPrefix("clignot");
    const bool hide = hasPrefix("cach") || hasPrefix("masqu");
    const bool show = hasPrefix("montr") || hasPrefix("affich") || hasPrefix("appar") || has("visible");
    // les couleurs : avant "si" (vrai), apres "sinon" (faux)
    std::string onColor, offColor;
    for (std::size_t i = 0; i < t_.size(); ++i) {
        std::size_t len = 0;
        const std::string c = colorAt(t_, i, &len);
        if (c.empty()) continue;
        if (i < cond && onColor.empty()) onColor = c;
        else if (otherwise != std::string::npos && i > otherwise && offColor.empty()) offColor = c;
        else if (i > cond && onColor.empty()) onColor = c;
    }
    const bool simple = condition == vf.name;
    std::vector<std::string> cannot;   // les objets qui ne savent pas clignoter : dits a part, la proposition garde les autres
    for (const auto* o : t.objects) {
        Operation op;
        op.action = Operation::Action::Modify;
        op.name = o->name;
        if (blink) {
            if (o->find("fault") && simple) {
                // le defaut d'un symbole : sa couleur de defaut, clignotante
                op.props.emplace_back("fault", condition);
                lines.push_back(code(o->name) + " : son d\xC3\xA9" "faut (`fault`) suit " + code(condition) + " - rouge clignotant quand elle est vraie");
            } else if (o->find("blink")) {
                op.props.emplace_back("blink", "=" + condition);
                lines.push_back(code(o->name) + " : le clignotement (`blink`) suit la formule `=" + condition + "`");
            } else if (o->find("fault")) {
                op.props.emplace_back("fault", condition);
                lines.push_back(code(o->name) + " : son d\xC3\xA9" "faut (`fault`) suit `" + condition + "`");
            } else {
                cannot.push_back(code(o->name) + " (" + lowerFirst(std::string(hmi::kindLabel(o->kind))) + ")");
                continue;
            }
        } else if (hide || show) {
            const std::string f = hide ? (condition.rfind("NOT ", 0) == 0 ? condition.substr(4) : "NOT " + condition) : condition;
            op.props.emplace_back("visible", "=" + (hide && simple ? "NOT " + vf.name : f));
            lines.push_back(code(o->name) + std::string(hide ? " : cach\xC3\xA9" : " : visible") + " quand " + code(condition) + " (la formule de `visible`)");
        } else if (!onColor.empty() || !offColor.empty()) {
            if (o->find("colorOn") && o->find("colorOff") && o->find("variable") && simple) {
                // un symbole : ses couleurs en marche et a l'arret, et sa variable
                if (!onColor.empty()) op.props.emplace_back("colorOn", onColor);
                if (!offColor.empty()) op.props.emplace_back("colorOff", offColor);
                op.props.emplace_back("variable", vf.name);
                lines.push_back(code(o->name) + " : reli\xC3\xA9 \xC3\xA0 " + code(vf.name) + (onColor.empty() ? std::string{} : ", " + onColor + " en marche")
                                + (offColor.empty() ? std::string{} : ", " + offColor + " \xC3\xA0 l'arr\xC3\xAAt"));
            } else {
                std::string what;
                const std::string key = colorKey(*o, &what);
                if (key.empty()) return ask(code(o->name) + " n'a pas de couleur que je sache changer.");
                const std::string cur = o->text(key);
                const std::string yes = onColor.empty() ? cur : onColor, no = offColor.empty() ? cur : offColor;
                op.props.emplace_back(key, "=SEL(" + condition + ", '" + no + "', '" + yes + "')");
                lines.push_back(code(o->name) + " : " + what + " suit la formule `=SEL(" + condition + ", '" + no + "', '" + yes + "')` (SEL : si faux, si vrai)");
            }
        } else {
            return ask("Que doit faire " + code(o->name) + " quand " + code(condition) + " ? Clignoter, changer de couleur, appara\xC3\xAEtre ?",
                       {"Fais clignoter " + o->name + " quand " + vf.name, o->name + " verte si " + vf.name + " sinon rouge"});
        }
        p.ops.push_back(std::move(op));
    }
    if (p.ops.empty())
        return ask(joinAnd(cannot) + (cannot.size() > 1 ? " n'ont" : " n'a") + " pas de clignotement que je sache piloter (ni `blink`, ni d\xC3\xA9" "faut `fault`).");
    std::string text = "Je propose d'animer :\n";
    for (const auto& l : lines) text += "- " + l + " ;\n";
    if (!cannot.empty())
        text += "Pas de clignotement possible pour " + joinAnd(cannot) + " : " + (cannot.size() > 1 ? "ils restent" : "il reste") + " tel" + (cannot.size() > 1 ? "s" : "")
                + " quel" + (cannot.size() > 1 ? "s" : "") + ".\n";
    // une formule (=...) n'est dite que si la proposition en pose une
    const bool formula = std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) {
        return std::any_of(op.props.begin(), op.props.end(), [](const auto& kv) { return !kv.second.empty() && kv.second.front() == '='; });
    });
    if (formula) text += "La formule ne lit que des variables qui existent ; le moteur la r\xC3\xA9\xC3\xA9value \xC3\xA0 chaque cycle.";
    else text += "La variable existe dans le projet ; l'objet la suit en marche.";
    return finish(std::move(p), std::move(text));
}

// ---- supprimer, dupliquer, aligner, repartir -------------------------------------------------------
LocalReply Engine::remove() const {
    if (!view()) return needView();
    Target t = targets(0, t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    std::vector<std::string> names;
    for (const auto* o : t.objects)
        if (o->parent == hmi::kNoId || std::none_of(t.objects.begin(), t.objects.end(), [&](const auto* q) { return q->id == o->parent; }))
            names.push_back(o->name);
    Proposal p = base("Supprimer " + (names.size() == 1 ? names.front() : std::to_string(names.size()) + " objets"));
    Operation op;
    op.action = Operation::Action::Remove;
    op.objects = names;
    p.ops.push_back(std::move(op));
    return finish(std::move(p), "Je propose de supprimer **" + plural(names.size(), "objet", "objets") + "** : " + codes(names, 12) + ".\n"
                                    + (names.size() > 1 ? "C'est une suppression multiple : **une confirmation** te sera demand\xC3\xA9" "e avant d'appliquer. "
                                                        : std::string{})
                                    + "Ctrl+Z la reprendra d'un coup.");
}

LocalReply Engine::duplicate() const {
    if (!view()) return needView();
    Target t = targets(0, t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    int count = 1;
    for (std::size_t i = 0; i < t_.size(); ++i) {
        const int n = numberOf(t_[i].f);
        if (n > 0 && i + 1 < t_.size() && (t_[i + 1].f == "fois" || t_[i + 1].f.rfind("copie", 0) == 0 || t_[i + 1].f == "exemplaires")) count = n;
    }
    if (count > 50) return ask("Au plus 50 copies par demande.");
    const bool down = has("bas") || has("dessous") || has("descendant") || phrase("en colonne") || phrase("vers le bas");
    const bool left = has("gauche");
    const bool up = has("haut") || has("dessus");
    hmi::Box all = hmi::edit::rotatedBounds(*t.objects.front());
    for (const auto* o : t.objects) {
        const auto b = hmi::edit::rotatedBounds(*o);
        const double r = std::max(all.right(), b.right()), bt = std::max(all.bottom(), b.bottom());
        all.x = std::min(all.x, b.x);
        all.y = std::min(all.y, b.y);
        all.w = r - all.x;
        all.h = bt - all.y;
    }
    const double label = hmi::kindIsSynoptic(t.objects.front()->kind) ? 20 : 0;
    Operation op;
    op.action = Operation::Action::Duplicate;
    for (const auto* o : t.objects) op.objects.push_back(o->name);
    op.dx = down || up ? 0 : (left ? -(all.w + 20) : all.w + 20);
    op.dy = down ? all.h + label + 20 : up ? -(all.h + label + 20) : 0;
    op.count = count;
    // des reperes $...$ dans ses proprietes : ils avancent d'une copie a l'autre (comme Ctrl+Maj+D)
    for (const auto* o : t.objects)
        for (const auto& pr : o->props)
            if (std::count(pr.value.begin(), pr.value.end(), '$') >= 2 || std::count(pr.expr.begin(), pr.expr.end(), '$') >= 2) op.markers = true;
    Proposal p = base("Dupliquer " + (op.objects.size() == 1 ? op.objects.front() : std::to_string(op.objects.size()) + " objets"));
    const std::string dir = down ? "vers le bas" : up ? "vers le haut" : left ? "vers la gauche" : "vers la droite";
    std::string text = "Je propose de dupliquer " + codes(op.objects) + " **" + (count > 1 ? std::to_string(count) + " fois**" : std::string("une fois**"))
                       + ", " + dir + " (d\xC3\xA9" "calage " + num(op.dx) + ", " + num(op.dy) + "), comme Ctrl+D : des noms neufs, ses propri\xC3\xA9t\xC3\xA9s et "
                       "sa variable suivent.";
    if (op.markers) text += " Ses **rep\xC3\xA8res $...$** avancent d'une copie \xC3\xA0 l'autre, comme Ctrl+Maj+D.";
    p.ops.push_back(std::move(op));
    return finish(std::move(p), std::move(text));
}

LocalReply Engine::arrange(bool alignIt) const {
    if (!view()) return needView();
    Target t = targets(0, t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    // sans genre ni nom : "aligne les boutons" a ete lu ; "aligne-les" prend la selection
    std::vector<std::string> names;
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto* o : t.objects) {
        if (o->parent != hmi::kNoId) continue;
        names.push_back(o->name);
        const auto b = o->box();
        minX = std::min(minX, b.x);
        maxX = std::max(maxX, b.x);
        minY = std::min(minY, b.y);
        maxY = std::max(maxY, b.y);
    }
    if (alignIt && names.size() < 2) return ask("Il faut au moins deux objets \xC3\xA0 aligner : s\xC3\xA9lectionne-les, ou nomme-les (`Aligne les boutons en haut`).");
    if (!alignIt && names.size() < 3) return ask("Il faut au moins trois objets \xC3\xA0 r\xC3\xA9partir (\xC3\xA0 \xC3\xA9" "carts \xC3\xA9gaux).");
    const bool row = (maxX - minX) >= (maxY - minY);
    Operation op;
    op.action = alignIt ? Operation::Action::Align : Operation::Action::Distribute;
    op.objects = names;
    std::string how;
    if (alignIt) {
        op.edge = row ? "haut" : "gauche";
        if (has("bas")) op.edge = "bas";
        else if (has("haut")) op.edge = "haut";
        else if (has("gauche")) op.edge = "gauche";
        else if (has("droite")) op.edge = "droite";
        else if (has("centre") || has("milieu")) op.edge = row ? "centre-v" : "centre-h";
        how = op.edge == "centre-v" || op.edge == "centre-h" ? "sur leur **centre**" : "sur leur **bord " + op.edge + "**";
    } else {
        op.edge = row ? "horizontal" : "vertical";
        if (has("verticalement") || has("vertical") || phrase("en colonne")) op.edge = "vertical";
        if (has("horizontalement") || has("horizontal") || phrase("en ligne")) op.edge = "horizontal";
        how = "\xC3\xA0 **\xC3\xA9" "carts \xC3\xA9gaux** (" + std::string(op.edge == "horizontal" ? "horizontalement" : "verticalement") + ")";
    }
    // DEJA FAIT : une operation qui ne change rien ne se propose pas
    {
        std::vector<hmi::Box> boxes;
        for (const auto* o : t.objects)
            if (o->parent == hmi::kNoId) boxes.push_back(o->box());
        const auto same = [&](auto value) {
            return std::all_of(boxes.begin(), boxes.end(), [&](const hmi::Box& b) { return std::abs(value(b) - value(boxes.front())) < 0.5; });
        };
        bool done = false;
        if (alignIt) {
            if (op.edge == "haut") done = same([](const hmi::Box& b) { return b.y; });
            else if (op.edge == "bas") done = same([](const hmi::Box& b) { return b.bottom(); });
            else if (op.edge == "gauche") done = same([](const hmi::Box& b) { return b.x; });
            else if (op.edge == "droite") done = same([](const hmi::Box& b) { return b.right(); });
            else if (op.edge == "centre-v") done = same([](const hmi::Box& b) { return b.cy(); });
            else if (op.edge == "centre-h") done = same([](const hmi::Box& b) { return b.cx(); });
        } else {
            const bool h = op.edge == "horizontal";
            std::stable_sort(boxes.begin(), boxes.end(), [&](const hmi::Box& a, const hmi::Box& b) { return h ? a.x < b.x : a.y < b.y; });
            std::vector<double> gaps;
            for (std::size_t i = 1; i < boxes.size(); ++i) gaps.push_back(h ? boxes[i].x - boxes[i - 1].right() : boxes[i].y - boxes[i - 1].bottom());
            done = !gaps.empty() && std::all_of(gaps.begin(), gaps.end(), [&](double g) { return std::abs(g - gaps.front()) < 0.5; });
        }
        if (done) {
            // "les boutons" : le genre dit (accorde) ; sinon les noms
            std::string who = "Les objets " + codes(names, 12);
            bool fem = false;
            for (const auto& m : mentions_)
                if (m.plural && !m.part) {
                    who = "Les " + lowerFirst(kindPlural(m.kind, 2));
                    fem = feminine(m.kind);
                    break;
                }
            const std::string e = fem ? "e" : "";
            return ask(who + " sont d\xC3\xA9j\xC3\xA0 " + (alignIt ? "align\xC3\xA9" + e + "s " + (op.edge == "centre-v" || op.edge == "centre-h" ? std::string("sur leur centre")
                                                                                                                  : (op.edge == "gauche" || op.edge == "droite" ? "\xC3\xA0 " : "en ") + op.edge)
                                                               : "r\xC3\xA9parti" + e + "s \xC3\xA0 \xC3\xA9" "carts \xC3\xA9gaux")
                       + " : rien \xC3\xA0 changer, je ne propose rien.");
        }
    }
    Proposal p = base(alignIt ? "Aligner " + std::to_string(names.size()) + " objets" : "R\xC3\xA9partir " + std::to_string(names.size()) + " objets");
    p.ops.push_back(std::move(op));
    return finish(std::move(p), std::string("Je propose ") + (alignIt ? "d'aligner " : "de r\xC3\xA9partir ") + codes(names, 12) + " " + how
                                    + (alignIt ? " : le geste Aligner de la barre d'outils." : " : le geste Distribuer de la barre d'outils."));
}

// ---- renommer -----------------------------------------------------------------------------------------
LocalReply Engine::rename() const {
    if (!view()) return needView();
    // "renomme X en Y" : le nouveau nom apres "en" (ou "par", "comme")
    std::size_t at = std::string::npos;
    for (std::size_t i = t_.size(); i-- > 1;)
        if (t_[i].f == "en" || t_[i].f == "par" || t_[i].f == "comme") {
            at = i;
            break;
        }
    if (at == std::string::npos || at + 1 >= t_.size()) return ask("Quel nouveau nom ? \xC3\x89" "cris par exemple `Renomme Cuve_1 en Bac_Eau`.");
    Target t = firstTarget(0, at);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    if (t.objects.empty()) return ask("Quel objet renommer ?");
    const hmi::Object& o = *t.objects.front();
    const std::string wanted = t_[at + 1].quoted ? sanitize(t_[at + 1].raw) : t_[at + 1].raw;
    if (badName(wanted)) return ask(guill(wanted) + " ne peut pas \xC3\xAAtre un nom d'objet : c'est un mot de la phrase. \xC3\x89" "cris le nouveau nom, par exemple `Renomme " + o.name + " en Bac_Eau`.");
    if (!hmi::edit::validName(wanted)) return ask(guill(wanted) + " n'est pas un nom d'objet valable (des lettres, des chiffres et _, sans espace).");
    if (!nameFree(*view(), wanted, {})) return ask("Le nom " + code(wanted) + " est d\xC3\xA9j\xC3\xA0 pris dans " + viewName() + ".");
    if (!view()->descendantsOf(o.id).empty())
        return ask(code(o.name) + " tient d'autres objets : renomme-le dans l'inspecteur (la case Nom), je ne sais pas le faire sans toucher \xC3\xA0 ses enfants.");
    // Il n'y a pas d'operation "renommer" : une copie exacte a la meme place, sous le nouveau nom, puis
    // l'original retire - dit tel quel, et repris d'un Ctrl+Z.
    Proposal p = base("Renommer " + o.name + " en " + wanted);
    Operation dup;
    dup.action = Operation::Action::Duplicate;
    dup.objects = {o.name};
    dup.names = {wanted};
    dup.dx = 0;
    dup.dy = 0;
    p.ops.push_back(std::move(dup));
    Operation rem;
    rem.action = Operation::Action::Remove;
    rem.objects = {o.name};
    p.ops.push_back(std::move(rem));
    return finish(std::move(p), "Je propose de renommer **" + o.name + "** en **" + wanted + "**. Mes op\xC3\xA9rations n'ont pas de \xC2\xAB renommer \xC2\xBB : "
                                    "c'est une copie exacte \xC3\xA0 la m\xC3\xAAme place (ses propri\xC3\xA9t\xC3\xA9s, sa variable et ses actions suivent), "
                                    "puis l'original retir\xC3\xA9 - l'aper\xC3\xA7u le montre, Ctrl+Z reprend tout. Une formule ailleurs qui citait " + code(o.name)
                                    + " est \xC3\xA0 revoir.");
}

// ---- cacher, montrer ------------------------------------------------------------------------------------
LocalReply Engine::visibility(bool show) const {
    if (!view()) return needView();
    Target t = targets(0, t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    Proposal p = base(std::string(show ? "Montrer " : "Cacher ") + t.objects.front()->name);
    std::vector<std::string> names;
    for (const auto* o : t.objects) {
        Operation op;
        op.action = Operation::Action::Modify;
        op.name = o->name;
        op.props = {{"visible", show ? "TRUE" : "FALSE"}};
        p.ops.push_back(std::move(op));
        names.push_back(o->name);
    }
    return finish(std::move(p), std::string("Je propose de ") + (show ? "rendre **visible" : "**cacher") + "** " + codes(names)
                                    + " en marche (la propri\xC3\xA9t\xC3\xA9 `visible`" + (show ? " \xC3\xA0 TRUE" : " \xC3\xA0 FALSE")
                                    + ") : dans l'\xC3\xA9" "diteur, l'objet reste l\xC3\xA0.");
}

// ---- deplacer -----------------------------------------------------------------------------------------
LocalReply Engine::move() const {
    if (!view()) return needView();
    const Position pos = position();
    if (pos.rel != Rel::None && !pos.missing.empty()) {
        const Target m = missingObject(pos.missing);
        return ask(m.question, m.suggestions);
    }
    Target t = targets(0, pos.rel != Rel::None ? pos.at : t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    Proposal p = base("D\xC3\xA9placer " + t.objects.front()->name);
    std::vector<std::string> lines, notes;
    const hmi::View& v = *view();
    // la place qu'un objet prend (son libelle sous un symbole compte)
    const auto labelRoom = [](const hmi::Object& o) {
        return hmi::kindIsSynoptic(o.kind) && !o.text("label").empty() && o.text("labelPosition", "dessous") == "dessous" ? 20.0 : 0.0;
    };
    if (pos.rel != Rel::None) {
        const Target r = firstTarget(pos.refAt, pos.end);
        if (!r.question.empty()) return ask(r.question, r.suggestions);
        if (r.objects.empty()) return ask("\xC3\x80 c\xC3\xB4t\xC3\xA9 de quel objet ?");
        const hmi::Object& ref = *r.objects.front();
        const hmi::Box rb = hmi::edit::rotatedBounds(ref);
        // les places prises : les autres objets du premier niveau (ni ceux qu'on deplace, ni la reference)
        std::vector<std::pair<hmi::Box, std::string>> taken;
        for (const auto& o : v.objects) {
            if (o.parent != hmi::kNoId || o.id == ref.id) continue;
            if (std::any_of(t.objects.begin(), t.objects.end(), [&](const hmi::Object* q) { return q->id == o.id; })) continue;
            hmi::Box b = hmi::edit::rotatedBounds(o);
            b.h += labelRoom(o);
            taken.emplace_back(b, o.name);
        }
        double cursor = 0;
        const double extra = labelRoom(ref);
        for (const auto* o : t.objects) {
            if (o == r.objects.front()) continue;
            const hmi::Box b = o->box();
            const double room = labelRoom(*o);
            double x = b.x, y = b.y;
            switch (pos.rel) {
                case Rel::Right: x = rb.right() + 30 + cursor; y = rb.cy() - b.h / 2; break;
                case Rel::Left: x = rb.x - 30 - b.w - cursor; y = rb.cy() - b.h / 2; break;
                case Rel::Below: x = rb.cx() - b.w / 2 + cursor; y = rb.bottom() + extra + 24; break;
                case Rel::Above: x = rb.cx() - b.w / 2 + cursor; y = rb.y - 24 - b.h - room; break;
                case Rel::None: break;
            }
            // la place juste a cote est prise : on avance (dans le sens dit) jusqu'a la premiere place libre
            std::string blocker;
            for (int k = 0; k < 60; ++k) {
                const hmi::Box g{x - 8, y - 8, b.w + 16, b.h + room + 16};
                const auto hit = std::find_if(taken.begin(), taken.end(), [&](const auto& tb) { return tb.first.intersects(g); });
                if (hit == taken.end()) break;
                if (blocker.empty()) blocker = hit->second;
                switch (pos.rel) {
                    case Rel::Right: x = hit->first.right() + 24; break;
                    case Rel::Left: x = hit->first.x - 24 - b.w; break;
                    case Rel::Below: y = hit->first.bottom() + 24; break;
                    case Rel::Above: y = hit->first.y - 24 - b.h - room; break;
                    case Rel::None: break;
                }
            }
            x = std::round(x);
            y = std::round(y);
            const bool outside = x < 0 || y < 0 || x + b.w > v.width || y + b.h + room > v.height;
            Operation op;
            op.action = Operation::Action::Modify;
            op.name = o->name;
            op.props = {{"x", num(x)}, {"y", num(y)}};
            p.ops.push_back(std::move(op));
            lines.push_back(code(o->name) + " en x = " + num(x) + ", y = " + num(y));
            if (!blocker.empty())
                notes.push_back("la place juste " + std::string(pos.rel == Rel::Right ? "\xC3\xA0 droite" : pos.rel == Rel::Left ? "\xC3\xA0 gauche" : pos.rel == Rel::Below ? "dessous" : "au-dessus")
                                + " est prise par " + code(blocker) + " : je d\xC3\xA9" "cale " + code(o->name) + " jusqu'\xC3\xA0 la premi\xC3\xA8re place libre");
            if (outside) notes.push_back(code(o->name) + " d\xC3\xA9passera du cadre de la vue (" + std::to_string(v.width) + " \xC3\x97 " + std::to_string(v.height) + ") : il n'y a pas de place libre plus pr\xC3\xA8s");
            hmi::Box nb{x, y, b.w, b.h + room};
            taken.emplace_back(nb, o->name);
            // a droite, a gauche : le suivant se pose apres celui-ci (il est dans `taken`) ; dessous, au-dessus : en rangee
            if (pos.rel == Rel::Below || pos.rel == Rel::Above) cursor += b.w + 24;
        }
        if (p.ops.empty()) return ask("Un objet ne se place pas \xC3\xA0 c\xC3\xB4t\xC3\xA9 de lui-m\xC3\xAAme.");
        const std::string rel = pos.rel == Rel::Right ? "\xC3\xA0 droite de " : pos.rel == Rel::Left ? "\xC3\xA0 gauche de " : pos.rel == Rel::Below ? "sous " : "au-dessus de ";
        std::string text = "Je propose de d\xC3\xA9placer " + joinAnd(lines) + ", " + rel + code(ref.name) + ", sans recouvrir un autre objet.";
        for (const auto& n : notes) text += "\n- " + n + ".";
        return finish(std::move(p), std::move(text));
    }
    // un decalage : "monte de 50", "decale de 40 vers la droite", "descends P1" (20 par defaut)
    double amount = 20;
    for (const auto& tk : t_)
        if (isDigits(tk.f) && !nameAt(static_cast<std::size_t>(&tk - t_.data()))) amount = std::stod(tk.f);
    double dx = 0, dy = 0;
    if (hasPrefix("mont") || has("haut")) dy = -amount;
    else if (hasPrefix("descen") || has("bas")) dy = amount;
    else if (has("gauche")) dx = -amount;
    else if (has("droite")) dx = amount;
    // une place : "en 100, 200", "x 100 y 200"
    double ax = -1, ay = -1;
    for (std::size_t i = 0; i + 1 < t_.size(); ++i) {
        if ((t_[i].f == "x" || t_[i].f == "x=") && isDigits(t_[i + 1].f)) ax = std::stod(t_[i + 1].f);
        if ((t_[i].f == "y" || t_[i].f == "y=") && isDigits(t_[i + 1].f)) ay = std::stod(t_[i + 1].f);
        if (t_[i].f == "=" && i > 0 && i + 1 < t_.size() && isDigits(t_[i + 1].f)) {
            if (t_[i - 1].f == "x") ax = std::stod(t_[i + 1].f);
            if (t_[i - 1].f == "y") ay = std::stod(t_[i + 1].f);
        }
    }
    if (ax < 0 && ay < 0) {
        const std::size_t en = indexOf("en");
        if (en != std::string::npos && en + 2 < t_.size() && isDigits(t_[en + 1].f) && isDigits(t_[en + 2].f)) {
            ax = std::stod(t_[en + 1].f);
            ay = std::stod(t_[en + 2].f);
        }
    }
    if (ax < 0 && ay < 0 && dx == 0 && dy == 0)
        return ask("O\xC3\xB9 le d\xC3\xA9placer ? Par exemple `D\xC3\xA9place " + t.objects.front()->name + " \xC3\xA0 droite de ...`, `Monte "
                   + t.objects.front()->name + " de 50`, ou `D\xC3\xA9place " + t.objects.front()->name + " en 100, 200`.");
    std::vector<std::string> bounded;
    for (const auto* o : t.objects) {
        const hmi::Box b = o->box();
        double x = ax >= 0 ? ax : b.x + dx, y = ay >= 0 ? ay : b.y + dy;
        // UN OBJET RESTE DANS LA VUE : borne au cadre, et dit (l'aide : un objet qui deborde est signale)
        const double wantX = x, wantY = y;
        x = std::max(0.0, std::min(x, static_cast<double>(v.width) - b.w));
        y = std::max(0.0, std::min(y, static_cast<double>(v.height) - b.h));
        if (x != wantX || y != wantY)
            bounded.push_back(code(o->name) + " s'arr\xC3\xAAte au bord de la vue (" + (y != wantY ? "y = " + num(y) + " au lieu de " + num(wantY) : "x = " + num(x) + " au lieu de " + num(wantX))
                              + ") : plus loin, l'objet sortirait de la vue");
        Operation op;
        op.action = Operation::Action::Modify;
        op.name = o->name;
        if (x != b.x) op.props.emplace_back("x", num(x));
        if (y != b.y) op.props.emplace_back("y", num(y));
        if (op.props.empty()) continue;
        p.ops.push_back(std::move(op));
        lines.push_back(code(o->name) + " en x = " + num(x) + ", y = " + num(y));
    }
    if (p.ops.empty())
        return ask(bounded.empty() ? codes({t.objects.front()->name}) + " est d\xC3\xA9j\xC3\xA0 \xC3\xA0 cette place."
                                   : codes({t.objects.front()->name}) + " est d\xC3\xA9j\xC3\xA0 au bord de la vue : plus loin, l'objet en sortirait. Rien \xC3\xA0 d\xC3\xA9placer.");
    std::string text = "Je propose de d\xC3\xA9placer " + joinAnd(lines) + ".";
    for (const auto& n : bounded) text += "\n- " + n + ".";
    return finish(std::move(p), std::move(text));
}

// ---- la taille ----------------------------------------------------------------------------------------
LocalReply Engine::resize() const {
    if (!view()) return needView();
    Target t = targets(0, t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    double factor = 1, w = -1, h = -1;
    if (hasPrefix("agrandi") || phrase("plus grand") || phrase("plus grande") || phrase("plus gros") || phrase("plus grosse")) factor = 1.25;
    if (hasPrefix("redui") || hasPrefix("reduire") || hasPrefix("retreci") || hasPrefix("rapetiss") || phrase("plus petit") || phrase("plus petite")) factor = 0.8;
    if (phrase("deux fois plus grand") || phrase("deux fois plus grande") || has("double")) factor = 2;
    if (phrase("deux fois plus petit") || phrase("deux fois plus petite") || has("moitie")) factor = 0.5;
    for (std::size_t i = 0; i < t_.size(); ++i) {
        const std::string& f = t_[i].f;
        const auto valueAfter = [&](std::size_t k) -> double {
            for (std::size_t j = k + 1; j < t_.size() && j <= k + 3; ++j)
                if (isDigits(t_[j].f)) return std::stod(t_[j].f);
            return -1;
        };
        if (f == "largeur") w = valueAfter(i);
        if (f == "hauteur") h = valueAfter(i);
        if ((f == "large" || f == "larges") && i > 0 && isDigits(t_[i - 1].f)) w = std::stod(t_[i - 1].f);
        if ((f == "haut" || f == "hauts") && i > 0 && isDigits(t_[i - 1].f)) h = std::stod(t_[i - 1].f);
        if (f == "taille") {
            for (std::size_t j = i + 1; j < t_.size() && j <= i + 3; ++j) {
                const auto x = t_[j].f.find('x');
                if (x != std::string::npos && x > 0 && isDigits(t_[j].f.substr(0, x)) && isDigits(t_[j].f.substr(x + 1))) {
                    w = std::stod(t_[j].f.substr(0, x));
                    h = std::stod(t_[j].f.substr(x + 1));
                }
            }
        }
        // un pourcentage : "agrandis de 50 %"
        if (isDigits(f) && i + 1 < t_.size() && (t_[i + 1].f == "pourcent" || t_[i + 1].raw == "%") && factor != 1)
            factor = factor > 1 ? 1 + std::stod(f) / 100 : std::max(0.1, 1 - std::stod(f) / 100);
    }
    if (factor == 1 && w < 0 && h < 0)
        return ask("Quelle taille ? Par exemple `Agrandis " + t.objects.front()->name + "`, `R\xC3\xA9" "duis-la`, ou `Largeur 200`.");
    Proposal p = base("Taille de " + t.objects.front()->name);
    std::vector<std::string> lines;
    for (const auto* o : t.objects) {
        const hmi::Box b = o->box();
        double nw = w >= 0 ? w : b.w * factor, nh = h >= 0 ? h : b.h * factor;
        if (w >= 0 && h < 0 && factor == 1) nh = b.h;
        if (h >= 0 && w < 0 && factor == 1) nw = b.w;
        Operation op;
        op.action = Operation::Action::Modify;
        op.name = o->name;
        // agrandir garde le centre
        if (w < 0 && h < 0) {
            op.props.emplace_back("x", num(std::max(0.0, b.cx() - nw / 2)));
            op.props.emplace_back("y", num(std::max(0.0, b.cy() - nh / 2)));
        }
        op.props.emplace_back("w", num(nw));
        op.props.emplace_back("h", num(nh));
        p.ops.push_back(std::move(op));
        lines.push_back(code(o->name) + " : " + num(b.w) + "\xC3\x97" + num(b.h) + kArrow + num(nw) + "\xC3\x97" + num(nh));
    }
    return finish(std::move(p), "Je propose la nouvelle taille de " + joinAnd(lines) + ".");
}

// ---- le texte -----------------------------------------------------------------------------------------
LocalReply Engine::setText() const {
    if (!view()) return needView();
    // le nouveau texte : entre guillemets, sinon apres "en", ":", "par"
    std::string text;
    std::size_t at = t_.size();
    for (std::size_t i = 0; i < t_.size(); ++i)
        if (t_[i].quoted) {
            text = t_[i].raw;
            at = i;
            break;
        }
    if (text.empty()) {
        // apres le dernier "en" / "par" : la fin de la phrase telle qu'ecrite
        for (std::size_t i = t_.size(); i-- > 1;)
            if (t_[i].f == "en" || t_[i].f == "par") {
                at = i;
                break;
            }
        if (at < t_.size()) {
            for (std::size_t i = at + 1; i < t_.size(); ++i) text += (text.empty() ? "" : " ") + t_[i].raw;
        }
    }
    if (text.empty()) return ask("Quel texte ? \xC3\x89" "cris-le entre guillemets, par exemple `Change le texte de Bouton_1 en \"D\xC3\xA9marrer\"`.");
    // l'objet : le texte d'un bouton, d'un libelle (pas le genre Texte dit comme mot)
    Target t = targets(0, at < t_.size() && t_[at].quoted ? t_.size() : at, true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    Proposal p = base("Texte de " + t.objects.front()->name);
    std::vector<std::string> names;
    for (const auto* o : t.objects) {
        std::string key = o->find("text") ? "text" : o->find("label") ? "label" : o->find("textOn") ? "textOn" : std::string{};
        if (key.empty()) return ask(code(o->name) + " (" + std::string(hmi::kindLabel(o->kind)) + ") n'a pas de texte que je sache changer.");
        Operation op;
        op.action = Operation::Action::Modify;
        op.name = o->name;
        op.props = {{key, text}};
        p.ops.push_back(std::move(op));
        names.push_back(o->name);
    }
    return finish(std::move(p), "Je propose d'\xC3\xA9" "crire " + guill(text) + " sur " + codes(names) + ".");
}

// ---- la couleur -----------------------------------------------------------------------------------------
LocalReply Engine::color() const {
    if (!view()) return needView();
    std::string hex, label;
    std::size_t at = t_.size();
    for (std::size_t i = 0; i < t_.size() && hex.empty(); ++i) {
        hex = colorAt(t_, i, nullptr, &label);
        if (!hex.empty()) at = i;
    }
    Target t = targets(0, t_.size(), true);
    if (!t.question.empty()) return ask(t.question, t.suggestions);
    (void)at;
    Proposal p = base(t.objects.size() == 1 ? t.objects.front()->name + " en " + label : std::to_string(t.objects.size()) + " objets en " + label);
    std::vector<std::string> lines, already;
    for (const auto* o : t.objects) {
        std::string what;
        const std::string key = colorKey(*o, &what);
        if (key.empty()) {
            lines.push_back(code(o->name) + " : pas de couleur que je sache changer");
            continue;
        }
        // deja de cette couleur (sans formule) : rien a proposer pour lui
        if (const auto* pr = o->find(key); pr && pr->expr.empty()) {
            std::string cur = pr->value;
            for (auto& c : cur) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (cur == hex) {
                already.push_back(code(o->name));
                continue;
            }
        }
        Operation op;
        op.action = Operation::Action::Modify;
        op.name = o->name;
        op.props = {{key, hex}};
        p.ops.push_back(std::move(op));
        lines.push_back(code(o->name) + " (" + std::string(hmi::kindLabel(o->kind)) + ") : " + what + " (`" + key + "`)");
    }
    if (p.ops.empty() && !already.empty())
        return ask(joinAnd(already) + (already.size() > 1 ? " sont" : " est") + " d\xC3\xA9j\xC3\xA0 en **" + label + "** (`" + hex + "`) : rien \xC3\xA0 changer, je ne propose rien.");
    if (p.ops.empty()) return ask(joinAnd(lines) + ".");
    std::string text = "Je propose de passer en **" + label + "** (`" + hex + "`) " + (lines.size() == 1 ? lines.front() : ":\n- " + [&] {
        std::string s;
        for (const auto& l : lines) s += (s.empty() ? "" : " ;\n- ") + l;
        return s;
    }()) + ".";
    if (!t.how.empty()) text += " (" + t.how + ")";
    if (!already.empty()) text += "\n" + joinAnd(already) + (already.size() > 1 ? " sont" : " est") + " d\xC3\xA9j\xC3\xA0 en " + label + ".";
    return finish(std::move(p), std::move(text));
}

// ---- l'aiguillage -------------------------------------------------------------------------------------
LocalReply Engine::run() {
    if (t_.empty()) return help();
    const bool question = request_.find('?') != std::string::npos;
    // les questions
    if (phrase("que sais tu") || phrase("que peux tu") || phrase("tu sais faire") || phrase("tu peux faire") || phrase("qu est ce que tu")
        || phrase("sais tu faire") || ((has("aide") || has("help")) && mentions_.empty()) || phrase("comment ca marche") || phrase("que comprends tu")
        || (t_.size() <= 2 && (has("bonjour") || has("salut") || has("aide"))))
        return help();
    if (has("combien")) return countQuestion();
    const bool createVerb = hasPrefix("ajout") || hasPrefix("rajout") || hasPrefix("cree") || has("creer") || has("cre") || hasPrefix("insere")
                         || hasPrefix("dessin") || phrase("il me faut") || phrase("je veux") || hasPrefix("genere") || hasPrefix("fabriqu");
    const bool weakCreate = has("mets") || has("met") || has("mettre") || has("pose") || has("poser") || has("place") || has("placer")
                         || hasPrefix("positionn");
    // 1.16.1 : les tournures d'une envie ("je voudrais", "j'aimerais", "il faudrait", "j'ai besoin de") et d'une fabrication
    // ("fais-moi", "prevois", "prepare") : elles demandent une vue, des variables, un type comme "cree".
    const bool wantVerb = has("voudrais") || has("voudrait") || has("voudrions") || has("aimerais") || has("aimerait") || hasPrefix("souhait")
                       || has("faudrait") || phrase("il faut") || has("besoin") || phrase("je veux") || phrase("on veut");
    const bool makeVerb = has("fais") || has("faire") || has("faites") || hasPrefix("prevoi") || hasPrefix("prepar") || hasPrefix("construi")
                       || has("donne") || has("donnez");
    if ((has("quelles") || has("quels") || has("liste") || has("lister") || phrase("montre moi") || phrase("donne moi") || question) && !createVerb) {
        if (has("variables") || has("variable")) return listVariables();
        if (has("vues")) return listViews();
        if (has("genres") || phrase("types d objets") || phrase("genres d objets") || phrase("objets peux tu") || phrase("objets connais tu"))
            return listKinds();
    }
    // une suite : "et une autre", "encore une", "la meme en bleu", "pareil pour P2"
    const bool follow = phrase("une autre") || phrase("un autre") || has("encore") || phrase("une de plus") || phrase("un de plus") || has("pareil")
                     || phrase("la meme") || phrase("le meme") || has("idem") || phrase("les memes") || (has("aussi") && t_.size() <= 4);
    if (follow && mentions_.empty() && !hasPrefix("supprim") && !hasPrefix("dupliqu")) return followUp();
    // le synoptique
    if (has("synoptique") || (has("station") && has("pompage") && !has("titre") && !has("texte") && !has("libelle") && !has("intitule") && !hasPrefix("ecri")))
        return synoptic();
    // une vue neuve
    if ((createVerb || wantVerb || makeVerb) && (phrase("une vue") || phrase("nouvelle vue") || phrase("un ecran") || phrase("une page")
                                                 || (phrase("la vue") && mentions_.empty()))
        && !(has("variable") || has("variables")))
        return createView();
    // 1.16.1 : "ajoute un champ Vitesse au type T_Pompe" : modifier un type existant n'est pas une operation de l'Assistant
    if ((has("champ") || has("champs") || has("membre") || has("membres")) && (phrase("au type") || phrase("du type") || phrase("a la structure")
                                                                              || phrase("de la structure") || phrase("dans le type") || phrase("dans la structure"))) {
        LocalReply r = ask("Je ne sais pas **modifier un type IHM qui existe** : ajoute le membre dans **Types IHM** (double-clic sur le type), ou "
                           "d\xC3\xA9" "cris un nouveau type complet, par exemple `Cr\xC3\xA9" "e un type T_Pompe2 avec Marche et Vitesse`.",
                           {"Cr\xC3\xA9" "e un type pour une pompe", "Que sais-tu faire ?"});
        r.understood = false;
        return r;
    }
    // un type IHM (une structure) : "cree un type T_Pompe avec Marche en BOOL", "cree une structure T_Vanne avec ..."
    // 1.16.1 : aussi "je voudrais un type moteur", "fais-moi une structure pour un variateur", et sans verbe "une structure
    // pour une pompe", "un type Vanne" ; pas "une pompe de type centrifuge" (une precision de l'objet).
    const std::size_t typeAt = typeNoun(t_);
    const bool typeAlone = typeAt == 1 && (t_[0].f == "un" || t_[0].f == "une" || t_[0].f == "nouveau" || t_[0].f == "nouvelle");
    if (typeAt != std::string::npos && (createVerb || wantVerb || makeVerb || hasPrefix("defini") || typeAlone) && !has("types") && !has("structures")
        && !has("automate") && !has("api") && !has("plc") && !has("ddt") && !has("ddts") && !hasPrefix("variable"))
        return createType();
    // des variables
    if ((createVerb || wantVerb || makeVerb || hasPrefix("propos") || hasPrefix("import")) && (hasPrefix("variable") || has("ddt") || has("ddts")
                                                                       || ((has("types") || has("type")) && (has("automate") || has("ihm")))))
        return createVariables();
    // creer des objets neufs ("cree 4 voyants alignes") : avant les gestes d'edition que la phrase peut citer
    {
        const Position early = position();
        const bool fresh = std::any_of(mentions_.begin(), mentions_.end(), [&](const Mention& m) {
            return m.indefinite && !(early.rel != Rel::None && m.at >= early.refAt && m.at < early.end);
        });
        if (createVerb && fresh && !hasPrefix("associ") && !hasPrefix("reli")) return createFromMentions();
    }
    // associer
    if (hasPrefix("associ") || hasPrefix("reli") || has("lie") || has("lier") || hasPrefix("branch") || hasPrefix("connect")
        || (hasPrefix("anim") && has("avec") && !has("si") && !has("quand") && !has("lorsque")))
        return bind();
    // animer : clignoter, une couleur ou une visibilite selon une condition
    if (hasPrefix("clignot") || ((has("si") || has("quand") || has("lorsque")) && (has("sinon") || hasPrefix("cach") || hasPrefix("montr")
                                                                                 || hasPrefix("affich") || hasPrefix("appar") || !colorAt(t_, 0).empty()
                                                                                 || std::any_of(t_.begin(), t_.end(), [&](const Tok& tk) { return !colorAt(t_, static_cast<std::size_t>(&tk - t_.data())).empty(); }))))
        return animate();
    // supprimer
    if (hasPrefix("supprim") || hasPrefix("effac") || hasPrefix("enleve") || hasPrefix("retir") || has("vire") || hasPrefix("detrui"))
        return remove();
    // dupliquer
    if (hasPrefix("dupliqu") || hasPrefix("copie") || hasPrefix("clone") || hasPrefix("recopie")) return duplicate();
    // aligner, repartir
    if (hasPrefix("align")) return arrange(true);
    if (hasPrefix("repart") || hasPrefix("distribu") || phrase("espace les") || hasPrefix("espace")) return arrange(false);
    // renommer
    if (hasPrefix("renomm") || phrase("change le nom") || phrase("appelle le") || phrase("appelle la")) return rename();
    // cacher, montrer
    if (hasPrefix("cache") || hasPrefix("masqu") || phrase("rends invisible") || phrase("rend invisible")) return visibility(false);
    if ((hasPrefix("montre") || hasPrefix("affiche") || phrase("rends visible") || phrase("fais apparaitre"))
        && (!names_.empty() || std::any_of(mentions_.begin(), mentions_.end(), [](const Mention& m) { return m.definite; }))
        && std::none_of(mentions_.begin(), mentions_.end(), [](const Mention& m) { return m.indefinite; }))
        return visibility(true);
    // la taille
    if (hasPrefix("agrandi") || hasPrefix("redui") || hasPrefix("reduire") || hasPrefix("retreci") || hasPrefix("rapetiss") || has("largeur")
        || has("hauteur") || has("taille") || phrase("plus grand") || phrase("plus grande") || phrase("plus petit") || phrase("plus petite"))
        return resize();
    // deplacer
    const Position pos = position();
    const bool moveVerb = hasPrefix("deplac") || has("bouge") || has("bouger") || hasPrefix("decal") || hasPrefix("mont") || hasPrefix("descend")
                       || hasPrefix("pousse") || hasPrefix("gliss");
    bool newObjects = false;
    for (const auto& m : mentions_)
        if (m.indefinite && !(pos.rel != Rel::None && m.at >= pos.refAt && m.at < pos.end)) newObjects = true;
    if (moveVerb && !newObjects) return move();
    if (weakCreate && !newObjects && pos.rel != Rel::None && !names_.empty() && names_.front().at < pos.at) return move();
    // 1.16.1 : "place la vanne sous la cuve" (un objet qui existe, devant la position) : le deplacer
    if (weakCreate && !newObjects && pos.rel != Rel::None
        && std::any_of(mentions_.begin(), mentions_.end(), [&](const Mention& m) { return m.definite && !m.indefinite && m.at < pos.at; }))
        return move();
    // le texte
    const bool textWord = has("texte") || has("libelle") || has("intitule") || hasPrefix("ecri") || has("titre");
    bool quotedText = false;
    for (const auto& tk : t_) quotedText = quotedText || tk.quoted;
    // "mets le texte de Bouton_1 en rouge" : une couleur (celle du texte), pas le texte "rouge"
    bool textIsColor = false;
    for (std::size_t i = t_.size(); i-- > 1;)
        if (t_[i].f == "en") {
            textIsColor = i + 1 < t_.size() && !t_[i + 1].quoted && !colorAt(t_, i + 1).empty();
            break;
        }
    if (textWord && !textIsColor && !newObjects && (hasPrefix("chang") || hasPrefix("modifi") || hasPrefix("ecri") || weakCreate || quotedText) && !createVerb)
        return setText();
    // la couleur d'un objet qui existe
    std::string anyColor;
    for (std::size_t i = 0; i < t_.size() && anyColor.empty(); ++i) anyColor = colorAt(t_, i);
    if (!anyColor.empty() && !newObjects && !(createVerb && !mentions_.empty())) return color();
    // creer
    if (!mentions_.empty() && (createVerb || weakCreate || newObjects)) return createFromMentions();
    if (createVerb && mentions_.empty()) {
        // 1.16.1 : un verbe de creation sans genre connu ("il me faut un machin") : pas comprise (understood = false) - une IA
        // connectee, si elle est reglee, peut la reprendre
        LocalReply r = ask("Quel genre d'objet ? Je connais ceux de la biblioth\xC3\xA8que (une pompe, une cuve, un bouton, un voyant...). Par exemple "
                           "`Ajoute 2 pompes`. Demande `Quels genres d'objets ?` pour la liste.",
                           examples());
        r.understood = false;
        return r;
    }
    if (follow) return followUp();
    return notUnderstood();
}

} // namespace

LocalReply understand(const PromptInput& in, std::string_view request) { return understand(in, request, {}); }

LocalReply understand(const PromptInput& in, std::string_view request, const std::vector<LocalTurn>& history) {
    Engine e(in, request, history);
    LocalReply r = e.run();
    // des suggestions COURTES : une ligne du panneau, jamais coupee au bord
    std::erase_if(r.suggestions, [](const std::string& s) { return chars(s) > kSuggestionMax; });
    return r;
}

const std::vector<std::string>& localSuggestions() {
    // la conversation vide ne connait pas la vue : des phrases qui marchent meme sur une vue vide (creer, demander),
    // courtes (une ligne du panneau)
    static const std::vector<std::string> k = {
        "Que sais-tu faire ?",
        "Ajoute 3 pompes",
        "Synoptique de 3 cuves et 2 pompes par cuve",
        "Ajoute une cuve et un bargraphe",
        "Ajoute deux boutons Marche et Arr\xC3\xAAt",
        "Combien d'objets ?",
    };
    return k;
}

} // namespace app::ai
