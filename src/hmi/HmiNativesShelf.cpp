// =============================================================================
//  hmi/HmiNativesShelf.cpp - 1.12.17 : LE RANGEMENT DES NATIVES
// -----------------------------------------------------------------------------
//  " reorganise un peu mieux les natives pour que ce soit plus logique et facile
//  a naviguer " : les fonctions par ce qu'on veut faire (huit themes), des
//  sous-dossiers par geste au-dela de 20 fonctions, les conversions en un seul
//  dossier ; les types en cinq dossiers (les conteneurs expliques, chacun avec
//  un exemple que l'essai natives joue) ; les enumerations par usage ; De A a Z.
//  Une fonction a une place, et une seule : l'essai le verifie.
//
//  Les regles vont des listes nommees aux categories du catalogue : une native
//  ajoutee au catalogue tombe dans le dossier de sa categorie (ou, sans regle,
//  dans " Nombres ", que l'essai refuse : un dossier a sous-dossiers ne garde
//  aucune fonction).
// =============================================================================
#include "HmiNatives.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace hmi::natives {

namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// ------------------------------------------------------------- les dossiers ---
struct ShelfDef { const char* key; const char* title; const char* summary; bool conversions; };
// L'ordre est celui de l'arbre : un dossier suit son parent (la cle avant le dernier '/').
const ShelfDef kShelves[] = {
    {"", "Fonctions", "Toutes les fonctions du langage de l'IHM, rang\xC3\xA9" "es par ce qu'on veut faire.", false},
    {"nombres", "Nombres", "Calculer, borner, interpoler, les entiers et leurs bornes, le hasard, les bits.", false},
    {"nombres/calcul", "Calcul", "Valeur absolue, racines, puissances, logarithmes, arrondis, reste.", false},
    {"nombres/trigo", "Trigonom\xC3\xA9trie", "Sinus, cosinus, tangente, leurs inverses, ATAN2, HYPOT (en radians).", false},
    {"nombres/choisir", "Choisir et borner", "Prendre une valeur parmi d'autres, la plus petite, la plus grande, borner.", false},
    {"nombres/interpoler", "Interpoler, mettre \xC3\xA0 l'\xC3\xA9" "chelle", "Une valeur entre deux autres, une courbe donn\xC3\xA9" "e par des points.", false},
    {"nombres/entiers", "Entiers et saturation", "Diviseurs, multiples, milieu ; les calculs bloqu\xC3\xA9s aux bornes d'un type.", false},
    {"nombres/constantes", "Constantes et limites", "\xCF\x80 et ses cousines ; les bornes et les caract\xC3\xA9ristiques de chaque type.", false},
    {"nombres/aleatoire", "Al\xC3\xA9" "atoire", "Des nombres au hasard, une suite qui rejoue, une loi normale.", false},
    {"nombres/bits", "Bits et octets", "D\xC3\xA9" "caler, tourner, compter, tester, poser un bit ; retourner les octets.", false},
    {"textes", "Textes", "Couper, chercher, remplacer, composer un texte, changer la casse.", false},
    {"dates", "Dates et heures", "L'heure du poste, les parties d'une date, ajouter des jours, \xC3\xA9" "crire une date.", false},
    {"couleurs", "Couleurs", "Composer une couleur, la m\xC3\xA9langer, l'\xC3\xA9" "claircir, lire ses composantes.", false},
    {"conteneurs", "Conteneurs", "Les listes, les vecteurs, les tuples, les MAP, les ensembles, les tampons, les tableaux.", false},
    {"conteneurs/listes", "Listes", "Les 51 fonctions d'une LIST (aussi d'un ARRAY, d'un RING), rang\xC3\xA9" "es par geste.", false},
    {"conteneurs/listes/ajouter", "Ajouter et retirer", "Ajouter, ins\xC3\xA9rer, \xC3\xA9" "crire, retirer, vider, remplir.", false},
    {"conteneurs/listes/lire", "Lire et chercher", "Compter, lire un \xC3\xA9l\xC3\xA9ment, chercher une valeur ou une condition.", false},
    {"conteneurs/listes/trier", "Trier et ranger", "Trier, renverser, \xC3\xA9" "changer, d\xC3\xA9placer, faire tourner.", false},
    {"conteneurs/listes/calculer", "Calculer", "Somme, minimum, maximum, moyenne, m\xC3\xA9" "diane, \xC3\xA9" "cart type, transformer.", false},
    {"conteneurs/listes/copier", "Copier et convertir", "Une partie, deux listes bout \xC3\xA0 bout, une copie, du texte et retour.", false},
    {"conteneurs/vecteurs", "Vecteurs", "VECTOR : pousser, reprendre, ins\xC3\xA9rer, changer la taille.", false},
    {"conteneurs/tuples", "Tuples", "Lire et \xC3\xA9" "crire un champ, compter, \xC3\xA9" "crire, d\xC3\xA9" "baller dans des variables.", false},
    {"conteneurs/map", "MAP", "Une valeur par cl\xC3\xA9 : lire, \xC3\xA9" "crire, parcourir.", false},
    {"conteneurs/map/lire", "Lire", "Une cl\xC3\xA9 est-elle l\xC3\xA0, sa valeur, les cl\xC3\xA9s, les valeurs, la plus proche.", false},
    {"conteneurs/map/ecrire", "\xC3\x89" "crire et retirer", "\xC3\x89" "crire, ajouter sans \xC3\xA9" "craser, compter, fusionner, retirer.", false},
    {"conteneurs/map/parcourir", "Parcourir (it\xC3\xA9rateurs)", "MAP_BEGIN, MAP_NEXT, MAP_END ; FOR EACH le fait aussi.", false},
    {"conteneurs/set", "Ensembles (SET)", "Ajouter sans doublon, r\xC3\xA9unir, croiser, comparer deux ensembles.", false},
    {"conteneurs/ring", "Tampons (RING)", "Ajouter \xC3\xA0 un tampon circulaire ; les fonctions de listes le lisent.", false},
    {"conteneurs/tableaux", "Tableaux (ARRAY)", "Le nombre de cases d'un tableau, ses bornes.", false},
    {"conteneurs/cumuls", "Cumuls et pliages", "Plier, cumuler, les \xC3\xA9" "carts entre voisins, remplir d'une suite (<numeric>).", false},
    {"conversions", "Conversions", "D'un type \xC3\xA0 un autre : TO_\xE2\x80\xA6 depuis n'importe quel type, X_TO_Y d'un type pr\xC3\xA9" "cis.", true},
    {"conversions/to", "Vers un type : TO_\xE2\x80\xA6", "La valeur convertie vers ce type, quel que soit son type de d\xC3\xA9part.", false},
    {"ihm", "L'IHM (IHM_)", "Ce que l'IHM fait en marche : naviguer, ouvrir une popup, les utilisateurs, les alarmes\xE2\x80\xA6", false},
    {"ihm/nav", "Navigation et vues", "Afficher une vue, revenir, l'accueil, le nom de la vue montr\xC3\xA9" "e.", false},
    {"ihm/popup", "Popups", "Ouvrir, remplacer, centrer, fermer une popup.", false},
    {"ihm/user", "Utilisateurs", "Qui est connect\xC3\xA9, son groupe, son niveau ; d\xC3\xA9" "connecter.", false},
    {"ihm/alarme", "Alarmes", "Mettre une alarme de c\xC3\xB4t\xC3\xA9, la remettre, couper le son.", false},
    {"ihm/journal", "Journal et Console", "\xC3\x89" "crire dans le journal de la s\xC3\xA9" "ance, dans la Console.", false},
    {"ihm/script", "Scripts et temps", "Lancer un script g\xC3\xA9n\xC3\xA9ral, le temps depuis le lancement.", false},
    {"ihm/affichage", "Affichage et son", "La langue, le th\xC3\xA8me, les param\xC3\xA8tres syst\xC3\xA8me, un son.", false},
    {"ihm/gif", "GIF anim\xC3\xA9s", "Jouer, figer, arr\xC3\xAAter, rejouer un GIF anim\xC3\xA9.", false},
    {"ihm/equip", "\xC3\x89quipements et r\xC3\xA9seau", "Un \xC3\xA9quipement r\xC3\xA9pond-il, son ping, son esclave simul\xC3\xA9.", false},
    {"ihm/export", "Exports", "Exporter des donn\xC3\xA9" "es dans exports/ du projet.", false},
    {"references", "R\xC3\xA9" "f\xC3\xA9rences et \xC3\xA9" "changes", "Une r\xC3\xA9" "f\xC3\xA9rence, un pointeur ; \xC3\xA9" "changer deux variables.", false},
};

// Les regles : (categorie, dossier, noms). Sans nom : le reste de la categorie.
struct Rule { const char* category; const char* shelf; std::vector<std::string_view> names; };
const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        {"math", "nombres/interpoler", {"LERP"}},
        {"math", "nombres/trigo", {"SIN", "COS", "TAN", "ASIN", "ACOS", "ATAN", "ATAN2", "HYPOT"}},
        {"math", "nombres/calcul", {}},
        {"select", "nombres/choisir", {}},
        {"numeric", "nombres/interpoler", {"INTERPOLER_TABLE"}},
        {"numeric", "nombres/entiers", {"GCD", "LCM", "MIDPOINT", "ADD_SAT", "SUB_SAT", "MUL_SAT", "DIV_SAT", "SATURATE_CAST", "STATIC_CAST", "DYNAMIC_CAST"}},
        {"numeric", "conteneurs/cumuls", {}},
        {"limits", "nombres/constantes", {}},
        {"numbers", "nombres/constantes", {}},
        {"alea", "nombres/aleatoire", {}},
        {"bits", "nombres/bits", {}},
        {"texte", "textes", {}},
        {"date", "dates", {}},
        {"couleur", "couleurs", {}},
        {"tab", "conteneurs/tableaux", {}},
        {"map", "conteneurs/map/ecrire", {"MAP_SET", "MAP_INSERT", "MAP_INCREMENT", "MAP_MERGE", "MAP_REMOVE", "MAP_REMOVE_IF", "MAP_CLEAR"}},
        {"map", "conteneurs/map/parcourir", {"MAP_BEGIN", "MAP_NEXT", "MAP_END", "MAP_FOR_EACH"}},
        {"map", "conteneurs/map/lire", {}},
        {"liste", "conteneurs/listes/ajouter", {"LIST_ADD", "LIST_INSERT", "LIST_INSERT_SORTED", "LIST_APPEND", "LIST_SET", "LIST_REMOVE_AT",
                                                "LIST_REMOVE", "LIST_REMOVE_ALL", "LIST_REMOVE_IF", "LIST_POP_FIRST", "LIST_DISTINCT", "LIST_FILL",
                                                "LIST_CLEAR"}},
        {"liste", "conteneurs/listes/trier", {"LIST_SORT", "LIST_SORT_BY", "LIST_IS_SORTED", "LIST_REVERSE", "LIST_SWAP", "LIST_MOVE", "LIST_ROTATE"}},
        {"liste", "conteneurs/listes/calculer", {"LIST_SUM", "LIST_MIN", "LIST_MAX", "LIST_AVG", "LIST_MEDIAN", "LIST_VARIANCE", "LIST_STDDEV",
                                                 "LIST_TRANSFORM"}},
        {"liste", "conteneurs/listes/copier", {"LIST_SLICE", "LIST_CONCAT", "LIST_COPY", "TO_LIST", "JOIN", "SPLIT", "LIST_FILTER", "LIST_MAP"}},
        {"liste", "conteneurs/vecteurs", {"VECTOR_"}},     // un prefixe (voir matches)
        {"liste", "conteneurs/tuples", {"TUPLE_"}},
        {"liste", "conteneurs/listes/lire", {}},
        {"ensembles", "conteneurs/ring", {"RING_"}},
        {"ensembles", "conteneurs/set", {}},
        {"conv", "conversions/to", {}},
        {"ref", "references", {}},
        {"utility", "references", {}},
        {"nav", "ihm/nav", {}},
        {"popup", "ihm/popup", {}},
        {"user", "ihm/user", {}},
        {"alarme", "ihm/alarme", {}},
        {"journal", "ihm/journal", {}},
        {"script", "ihm/script", {}},
        {"affichage", "ihm/affichage", {}},
        {"gif", "ihm/gif", {}},
        {"equip", "ihm/equip", {}},
        {"export", "ihm/export", {}},
    };
    return r;
}
// Un nom de la regle : le nom exact, ou un prefixe (il finit par '_').
bool matches(std::string_view rule, std::string_view name) {
    if (!rule.empty() && rule.back() == '_') return upper(name).rfind(upper(rule), 0) == 0;
    return upper(rule) == upper(name);
}

struct Built {
    std::vector<Shelf>       shelves;
    std::vector<std::size_t> home;          // par fonction : son dossier
    std::vector<std::size_t> alpha;
};

const Built& built() {
    static const Built b = [] {
        Built out;
        std::map<std::string, std::size_t> byKey;
        for (const auto& d : kShelves) {
            Shelf s;
            s.key = d.key;
            s.title = d.title;
            s.summary = d.summary;
            s.conversions = d.conversions;
            const std::size_t id = out.shelves.size();
            if (!s.key.empty()) {
                const auto slash = s.key.rfind('/');
                const std::string parent = slash == std::string::npos ? std::string{} : s.key.substr(0, slash);
                s.parent = byKey.count(parent) ? byKey[parent] : 0;
                out.shelves[s.parent].folders.push_back(id);
            }
            byKey[s.key] = id;
            out.shelves.push_back(std::move(s));
        }
        out.home.assign(functions().size(), kNone);
        for (std::size_t i = 0; i < functions().size(); ++i) {
            const auto& f = functions()[i];
            std::size_t at = byKey["nombres"];          // sans regle : refuse par l'essai
            for (const auto& r : rules()) {
                if (f.category != r.category) continue;
                bool ok = r.names.empty();
                for (const auto n : r.names) ok = ok || matches(n, f.name);
                if (!ok) continue;
                at = byKey[r.shelf];
                break;
            }
            out.home[i] = at;
            out.shelves[at].functions.push_back(i);
        }
        // Les totaux, des feuilles vers la racine (un dossier suit toujours son parent).
        for (std::size_t s = out.shelves.size(); s-- > 0;) {
            auto& sh = out.shelves[s];
            sh.total += sh.functions.size() + (sh.conversions ? conversions().size() : 0);
            if (s != 0) out.shelves[sh.parent].total += sh.total;
        }
        out.alpha.resize(functions().size());
        for (std::size_t i = 0; i < out.alpha.size(); ++i) out.alpha[i] = i;
        std::stable_sort(out.alpha.begin(), out.alpha.end(),
                         [](std::size_t a, std::size_t b) { return upper(functions()[a].name) < upper(functions()[b].name); });
        return out;
    }();
    return b;
}

// ---------------------------------------------------------------- les types ---
// Les sept conteneurs : de quoi ecrire leur fiche, et un exemple joue par l'essai.
const std::vector<TypeGuide>& guides() {
    static const std::vector<TypeGuide> g = {
        {"ARRAY", "ARRAY[a..b] OF T",
         "Des cases num\xC3\xA9rot\xC3\xA9" "es de a \xC3\xA0 b, r\xC3\xA9serv\xC3\xA9" "es une fois pour toutes. Le seul conteneur qui peut vivre sur un \xC3\xA9quipement : une adresse "
         "par case, \xC3\xA0 la suite.",
         "Un plan m\xC3\xA9moire, des mesures \xC3\xA0 place fixe (une case par capteur), une table qui ne change pas de taille.",
         "VAR\n    Mesures : ARRAY[1..10] OF REAL;\n    Grille : ARRAY[0..3, 0..9] OF INT;\nEND_VAR",
         "Mesures[1] := 20.5;\nx := Mesures[10];\nGrille[2, 5] := 7;",
         "FOR i := Mesures.Low TO Mesures.High DO\n    Total := Total + Mesures[i];\nEND_FOR;",
         "Dans un \xC3\xA9quipement (une adresse par case) ou dans la m\xC3\xA9moire de l'IHM. Un indice hors de a..b est une erreur. \xC3\x80 deux dimensions : "
         "Rows lignes, Columns colonnes.",
         "float mesures[10];", "std::array<float, 10> mesures;", "conteneurs/tableaux", "ARRAY[1..10] OF REAL",
         "VAR\n    Mesures : ARRAY[1..5] OF REAL := [1.0, 2.0, 3.0, 4.0, 5.0];\n    Total : REAL;\n    i : INT;\nEND_VAR\n"
         "FOR i := Mesures.Low TO Mesures.High DO\n    Total := Total + Mesures[i];\nEND_FOR;",
         {{"DINT", "Mesures.Length", "5"}, {"DINT", "Mesures.High", "5"}, {"REAL", "Total", "15.0"}}},
        {"LIST OF", "LIST OF T",
         "Une liste de valeurs du m\xC3\xAAme type, qui grandit et r\xC3\xA9tr\xC3\xA9" "cit pendant la marche. Les indices partent de 0.",
         "Une suite qui s'allonge : des \xC3\xA9v\xC3\xA9nements, des lots, des mesures \xC3\xA0 garder.",
         "VAR\n    Mesures : LIST OF REAL := [12.5, 13.0];\nEND_VAR",
         "x := Mesures[0];      (* le premier *)\nMesures[1] := 13.2;\ny := LIST_GET(Mesures, 5, 0.0);   (* sans erreur hors de la liste *)",
         "FOR EACH m IN Mesures DO\n    Total := Total + m;\nEND_FOR;",
         "Dans la m\xC3\xA9moire de l'IHM : pas d'adresse sur un \xC3\xA9quipement. Pour l'envoyer \xC3\xA0 un \xC3\xA9quipement, la copier dans un ARRAY (LIST_COPY).",
         "/* pas de liste en C : un tableau et un compteur */\nfloat mesures[100]; int nombre = 0;",
         "std::vector<float> mesures{12.5f, 13.0f};", "conteneurs/listes", "LIST OF REAL",
         "VAR\n    Mesures : LIST OF REAL := [12.5, 13.0, 12.8];\n    Moyenne : REAL;\nEND_VAR\nLIST_ADD(Mesures, 13.4);\nMoyenne := LIST_AVG(Mesures);",
         {{"STRING", "JOIN(Mesures, ', ')", "12.5, 13, 12.8, 13.4"}, {"DINT", "Mesures.Count", "4"}, {"REAL", "Mesures.Last", "13.4"},
          {"REAL", "Moyenne", "12.925"}}},
        {"VECTOR OF", "VECTOR OF T",
         "Comme une liste, avec les gestes d'une pile : on pousse \xC3\xA0 la fin, on reprend le dernier.",
         "Une pile : d\xC3\xA9" "faire la derni\xC3\xA8re action, revenir \xC3\xA0 l'\xC3\xA9" "cran d'avant.",
         "VAR\n    Pile : VECTOR OF INT;\nEND_VAR",
         "VECTOR_PUSH(Pile, 4);\nx := Pile[0];\nHaut := VECTOR_POP(Pile);   (* le dernier, retir\xC3\xA9 *)",
         "FOR EACH v IN Pile DO\n    Total := Total + v;\nEND_FOR;",
         "Dans la m\xC3\xA9moire de l'IHM, comme une liste.",
         "/* pas de vecteur en C : un tableau et un compteur */",
         "std::vector<int> pile;\npile.push_back(4);", "conteneurs/vecteurs", "VECTOR OF INT",
         "VAR\n    Pile : VECTOR OF INT;\n    Haut : INT;\nEND_VAR\nVECTOR_PUSH(Pile, 4);\nVECTOR_PUSH(Pile, 7);\nHaut := VECTOR_POP(Pile);",
         {{"INT", "Haut", "7"}, {"DINT", "Pile.Count", "1"}}},
        {"MAP", "MAP[K] OF T",
         "Une valeur par cl\xC3\xA9 (un texte ou un entier), rang\xC3\xA9" "e par cl\xC3\xA9. Lire une cl\xC3\xA9 absente est une erreur : MAP_GET avec un d\xC3\xA9" "faut ne l'est pas.",
         "Une valeur par nom : un stock par gaz, un r\xC3\xA9glage par poste, un compteur par d\xC3\xA9" "faut.",
         "VAR\n    Stock : MAP[STRING] OF REAL;\nEND_VAR",
         "Stock['azote'] := 12.5;          (* cr\xC3\xA9" "e la cl\xC3\xA9 si elle manque *)\nx := MAP_GET(Stock, 'argon', 0.0);",
         "FOR EACH cle, valeur IN Stock DO\n    IHM_JOURNAL(cle);\nEND_FOR;",
         "Dans la m\xC3\xA9moire de l'IHM : pas d'adresse sur un \xC3\xA9quipement.",
         "/* pas de MAP en C : deux tableaux, les cl\xC3\xA9s et les valeurs */",
         "std::map<std::string, float> stock;\nstock[\"azote\"] = 12.5f;", "conteneurs/map", "MAP[STRING] OF REAL",
         "VAR\n    Stock : MAP[STRING] OF REAL;\nEND_VAR\nStock['azote'] := 12.5;\nMAP_INCREMENT(Stock, 'azote', 2.5);",
         {{"REAL", "Stock['azote']", "15.0"}, {"BOOL", "MAP_HAS(Stock, 'argon')", "FALSE"}, {"DINT", "Stock.Count", "1"}}},
        {"SET OF", "SET OF T",
         "Chaque valeur une seule fois, rang\xC3\xA9" "e. SET_ADD dit si la valeur est nouvelle : plus besoin de chercher avant d'ajouter.",
         "Des choix sans doublon : les postes actifs, les d\xC3\xA9" "fauts pr\xC3\xA9sents.",
         "VAR\n    Postes : SET OF STRING := ['P1', 'P3'];\nEND_VAR",
         "IF SET_ADD(Postes, 'P2') THEN\n    (* P2 n'y \xC3\xA9tait pas *)\nEND_IF;\nx := SET_HAS(Postes, 'P1');",
         "FOR EACH p IN Postes DO        (* dans l'ordre : P1, P2, P3 *)\n    IHM_JOURNAL(p);\nEND_FOR;",
         "Dans la m\xC3\xA9moire de l'IHM. S[0] se lit (la plus petite valeur) mais ne s'\xC3\xA9" "crit pas : l'ensemble se range tout seul.",
         "/* pas d'ensemble en C */",
         "std::set<std::string> postes{\"P1\", \"P3\"};\npostes.insert(\"P2\");", "conteneurs/set", "SET OF STRING",
         "VAR\n    Postes : SET OF STRING := ['P3', 'P1', 'P3'];\n    Nouveau : BOOL;\nEND_VAR\nNouveau := SET_ADD(Postes, 'P2');",
         {{"STRING", "JOIN(Postes, ', ')", "P1, P2, P3"}, {"BOOL", "Nouveau", "TRUE"}, {"DINT", "Postes.Count", "3"}}},
        {"RING", "RING[N] OF T",
         "Un tampon circulaire : les N derni\xC3\xA8res valeurs. Plein, la plus ancienne part. N est une constante de 1 \xC3\xA0 100000.",
         "Les N derni\xC3\xA8res mesures, une moyenne glissante, un petit historique.",
         "VAR\n    Dernieres : RING[100] OF REAL;\nEND_VAR",
         "RING_ADD(Dernieres, Mesure);\nMoyenne := LIST_AVG(Dernieres);   (* les fonctions de listes le lisent *)",
         "FOR EACH v IN Dernieres DO     (* de la plus ancienne \xC3\xA0 la plus r\xC3\xA9" "cente *)\n    Total := Total + v;\nEND_FOR;",
         "Dans la m\xC3\xA9moire de l'IHM. Capacity vaut N, Full dit qu'il est plein.",
         "/* un tableau et un indice qui tourne */\nfloat dernieres[100]; int debut = 0, nombre = 0;",
         "boost::circular_buffer<float> dernieres(100);   // ou std::deque", "conteneurs/ring", "RING[100] OF REAL",
         "VAR\n    Dernieres : RING[3] OF REAL;\nEND_VAR\nRING_ADD(Dernieres, 1.0);\nRING_ADD(Dernieres, 2.0);\nRING_ADD(Dernieres, 3.0);\n"
         "RING_ADD(Dernieres, 4.0);",
         {{"STRING", "JOIN(Dernieres, ', ')", "2, 3, 4"}, {"BOOL", "Dernieres.Full", "TRUE"}, {"REAL", "LIST_AVG(Dernieres)", "3.0"}}},
        {"TUPLE", "TUPLE(T1, T2\xE2\x80\xA6)",
         "Deux ou trois valeurs de types diff\xC3\xA9rents qui vont ensemble, lues par leur rang : t.Item1, t.Item2\xE2\x80\xA6",
         "Une mesure et son nom, une cl\xC3\xA9 et sa valeur (MAP_ITEMS en rend une liste).",
         "VAR\n    Mesure : TUPLE(STRING, REAL) := ('four', 850.0);\nEND_VAR",
         "x := Mesure.Item1;              (* 'four' *)\nMesure.Item2 := 860.0;",
         "",
         "Dans la m\xC3\xA9moire de l'IHM. Pour un \xC3\xA9quipement : une structure IHM (un type \xC3\xA0 membres).",
         "struct { const char *nom; float valeur; } mesure = {\"four\", 850.0f};",
         "std::tuple<std::string, float> mesure{\"four\", 850.0f};", "conteneurs/tuples", "TUPLE(STRING, REAL)",
         "VAR\n    Mesure : TUPLE(STRING, REAL) := ('four', 850.0);\nEND_VAR\nMesure.Item2 := 860.0;",
         {{"STRING", "TUPLE_TO_STRING(Mesure)", "('four', 860.0)"}, {"DINT", "TUPLE_SIZE(Mesure)", "2"}, {"REAL", "Mesure.Item2", "860.0"}}},
    };
    return g;
}

std::vector<TypeShelf> buildTypeShelves() {
    std::vector<TypeShelf> out(5);
    out[0] = {"nombres-bits", "Nombres et bits", "Les bool\xC3\xA9" "ens, les entiers sign\xC3\xA9s et non sign\xC3\xA9s, les mots, les r\xC3\xA9" "els.", {}};
    out[1] = {"textes-dates", "Textes, dur\xC3\xA9" "es et dates", "Les textes, les caract\xC3\xA8res, les dur\xC3\xA9" "es, les dates et les heures.", {}};
    out[2] = {"conteneurs", "Conteneurs", "Plusieurs valeurs dans une variable : lequel choisir, et comment s'en servir.", {}};
    out[3] = {"references", "R\xC3\xA9" "f\xC3\xA9rences", "D\xC3\xA9signer une variable, parcourir une MAP pas \xC3\xA0 pas.", {}};
    out[4] = {"generiques", "G\xC3\xA9n\xC3\xA9riques", "ANY, ANY_NUM\xE2\x80\xA6 : ce qu'une signature accepte. Ils ne se d\xC3\xA9" "clarent pas.", {}};
    const auto& base = tree().types;
    for (std::size_t i = 0; i < base.size(); ++i)
        out[base[i].category.rfind("Textes", 0) == 0 ? 1 : 0].entries.push_back({TypeEntry::Kind::Base, i, base[i].name});
    const auto place = [](std::string_view name) -> int {
        if (name == "STRING[n]") return 1;
        if (name == "REF_TO" || name == "POINTER TO" || name == "MAP_ITERATOR") return 3;
        return 2;
    };
    // Les conteneurs dans l'ordre de " Lequel choisir ? " ; le reste dans celui du catalogue.
    for (const auto& g : guides())
        for (std::size_t i = 0; i < constructed().size(); ++i)
            if (constructed()[i].name == g.name) out[2].entries.push_back({TypeEntry::Kind::Constructed, i, std::string(g.name)});
    for (std::size_t i = 0; i < constructed().size(); ++i) {
        const auto name = constructed()[i].name;
        const int at = place(name);
        if (at == 2 && typeGuide(name)) continue;
        out[static_cast<std::size_t>(at)].entries.push_back({TypeEntry::Kind::Constructed, i, std::string(name)});
    }
    for (std::size_t i = 0; i < generics().size(); ++i) out[4].entries.push_back({TypeEntry::Kind::Generic, i, std::string(generics()[i].name)});
    return out;
}

// --------------------------------------------------------- les enumerations ---
struct EnumDef { const char* key; const char* title; const char* summary; std::vector<std::string_view> names; };
const std::vector<EnumDef>& enumDefs() {
    static const std::vector<EnumDef> d = {
        {"", "\xC3\x89num\xC3\xA9rations", "Les listes de mots que le langage conna\xC3\xAEt : NOM#Valeur vaut son nombre.", {}},
        {"fonctions", "Pour les fonctions IHM_", "Ce qu'attend une fonction IHM_ : une transition, une place de popup, un niveau de journal\xE2\x80\xA6", {}},
        {"calculs", "Pour les listes et les cumuls", "Les comparaisons, les op\xC3\xA9rations et les transformations des fonctions de listes.",
         {"COMPARAISON", "OPERATION", "TRANSFORMATION"}},
        {"dates", "Dates", "Les jours de la semaine, les mois.", {"JOUR", "MOIS"}},
        {"variables", "Pour tes variables", "Des \xC3\xA9num\xC3\xA9rations toutes faites : priorit\xC3\xA9, qualit\xC3\xA9 d'une mesure, \xC3\xA9tat d'un moteur, mode de marche.",
         {"PRIORITE_ALARME", "QUALITE", "ETAT_MOTEUR", "MODE_MARCHE"}},
        {"objets", "Pour les propri\xC3\xA9t\xC3\xA9s des objets", "Les choix d'une propri\xC3\xA9t\xC3\xA9 d'objet, \xC3\xA9" "crits dans une expression \xC6\x92x.", {}},
        {"objets/textes", "Textes et mise en forme", "", {"POLICE", "ALIGNEMENT", "ORIENTATION", "PLACE_LIBELLE", "STYLE_TITRE", "DEFILEMENT",
                                                          "SENS_DEFILEMENT"}},
        {"objets/images", "Images et voyants", "", {"FORME_VOYANT", "ETIREMENT", "RECOLORATION", "LECTURE_GIF", "DEPART_GIF", "FIN_GIF",
                                                    "CORRECTION_QR"}},
        {"objets/saisie", "Saisie et boutons", "", {"TYPE_SAISIE", "CLAVIER_VIRTUEL", "CONFIRMATION", "OPERATION_BOUTON", "STYLE_SELECTEUR",
                                                    "SIGNATURE", "NIVEAU_ACCES", "ONGLET_PARAMETRES"}},
        {"objets/dates", "Dates et dur\xC3\xA9" "es", "", {"CHAMPS_DATE", "RESOLUTION_PLANNING", "STYLE_HORLOGE", "FORMAT_DUREE"}},
        {"objets/courbes", "Courbes et graphiques", "", {"MODE_COURBE", "ECHELLE", "INTERPOLATION", "ETIQUETTES_PARTS", "SOURCE_HISTORIQUE"}},
        {"objets/alarmes", "Alarmes", "", {"ALARME_MONTREE", "ALARMES_COMPTEES", "PERIODE_STATS", "CLASSEMENT"}},
        {"objets/procede", "Proc\xC3\xA9" "d\xC3\xA9 (vannes, tuyauterie)", "", {"COMMANDE_VANNE", "FORME_TUBE", "MONTAGE_ISA", "VANNE3_VARIANTE",
                                                                "VANNE3_BOISSEAU", "VANNE3_COMMANDE"}},
        {"objets/navigation", "Navigation et langues", "", {"STYLE_NAVIGATION", "CHEMIN_ARIANE", "PLACE_ONGLETS", "STYLE_LANGUE",
                                                            "LIBELLE_LANGUE"}},
        {"objets/fichiers", "Fichiers", "", {"FORMAT_FICHIER"}},
    };
    return d;
}

std::vector<EnumShelf> buildEnumShelves() {
    std::vector<EnumShelf> out;
    std::map<std::string, std::size_t> byKey;
    for (const auto& d : enumDefs()) {
        EnumShelf s;
        s.key = d.key;
        s.title = d.title;
        s.summary = d.summary;
        const std::size_t id = out.size();
        if (!s.key.empty()) {
            const auto slash = s.key.rfind('/');
            const std::string parent = slash == std::string::npos ? std::string{} : s.key.substr(0, slash);
            s.parent = byKey.count(parent) ? byKey[parent] : 0;
            out[s.parent].folders.push_back(id);
        }
        byKey[s.key] = id;
        out.push_back(std::move(s));
    }
    for (std::size_t e = 0; e < enums().size(); ++e) {
        const auto& en = enums()[e];
        std::size_t at = kNone;
        if (!en.function.empty()) at = byKey["fonctions"];
        for (std::size_t d = 0; at == kNone && d < enumDefs().size(); ++d)
            for (const auto n : enumDefs()[d].names)
                if (n == en.name) at = d;
        if (at == kNone) at = en.uses.empty() ? byKey["variables"] : byKey["objets"];   // sans regle : l'essai le dit
        out[at].enums.push_back(e);
    }
    for (std::size_t s = out.size(); s-- > 0;) {
        out[s].total += out[s].enums.size();
        if (s != 0) out[out[s].parent].total += out[s].total;
    }
    return out;
}

} // namespace

const std::vector<Shelf>& shelves() { return built().shelves; }

std::size_t shelfIndex(std::string_view key) noexcept {
    const auto& s = shelves();
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i].key == key) return i;
    return kNone;
}

const Shelf* shelf(std::string_view key) noexcept {
    const auto i = shelfIndex(key);
    return i == kNone ? nullptr : &shelves()[i];
}

std::size_t homeOf(std::size_t function) noexcept {
    const auto& h = built().home;
    return function < h.size() ? h[function] : kNone;
}

std::string shelfPath(std::size_t s, std::string_view separator) {
    std::string out;
    while (s != kNone && s < shelves().size() && s != 0) {
        out = shelves()[s].title + (out.empty() ? std::string{} : std::string(separator) + out);
        s = shelves()[s].parent;
    }
    return out;
}

const std::vector<std::size_t>& alphabetical() { return built().alpha; }

std::string_view cppHeader(const Function& f) noexcept {
    if (f.category == "numeric") return "<numeric>";
    if (f.category == "limits") return "<limits>";
    if (f.category == "numbers") return "<numbers>";
    if (f.category == "utility") return "<utility>";
    return {};
}

std::size_t categoryShelf(std::string_view category) {
    // Le plus proche dossier commun des fonctions de la categorie (" categorie:map " -> MAP).
    std::vector<std::size_t> homes;
    for (std::size_t i = 0; i < functions().size(); ++i)
        if (functions()[i].category == category) homes.push_back(homeOf(i));
    if (homes.empty()) return kNone;
    const auto chain = [](std::size_t s) {
        std::vector<std::size_t> c;
        while (s != kNone && s < shelves().size()) {
            c.push_back(s);
            if (s == 0) break;
            s = shelves()[s].parent;
        }
        std::reverse(c.begin(), c.end());
        return c;
    };
    auto common = chain(homes.front());
    for (const auto h : homes) {
        const auto c = chain(h);
        std::size_t n = 0;
        while (n < common.size() && n < c.size() && common[n] == c[n]) ++n;
        common.resize(n);
    }
    return common.empty() ? 0 : common.back();
}

const std::vector<TypeShelf>& typeShelves() {
    static const std::vector<TypeShelf> s = buildTypeShelves();
    return s;
}

const std::vector<TypeGuide>& typeGuides() { return guides(); }

const TypeGuide* typeGuide(std::string_view name) noexcept {
    for (const auto& g : guides())
        if (upper(g.name) == upper(name)) return &g;
    return nullptr;
}

const std::vector<EnumShelf>& enumShelves() {
    static const std::vector<EnumShelf> s = buildEnumShelves();
    return s;
}

std::size_t enumShelfOf(std::size_t e) noexcept {
    const auto& s = enumShelves();
    for (std::size_t i = 0; i < s.size(); ++i)
        if (std::find(s[i].enums.begin(), s[i].enums.end(), e) != s[i].enums.end()) return i;
    return kNone;
}

} // namespace hmi::natives
