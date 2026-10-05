// =============================================================================
//  hmi/HmiExprGuide.cpp - 1.11 (chantier T3, D5) : la table des types
//                         d'expression (voir l'en-tete)
// -----------------------------------------------------------------------------
//  Les variables d'exemple du banc (HmiExprBench) : V : ARRAY[0..3] OF
//  T_VANNE (Pos REAL, Defaut BOOL, Bouge BOOL, Cmd BOOL), V[0].Pos = 42,
//  Pression = 3.8, Mode = T_MODE#Auto (Arret, Auto, Manu, Defaut),
//  Pompe_Marche = TRUE, Debit_P3, Niveau_Cuve, Vue_Demandee, SYS.UserLevel = 2,
//  les vues Vue_Accueil, Vue_Armoire_A, Vue_Synoptique, Vue_Commandes.
//  Chaque exemple s'ecrit pour elles : l'essai les evalue.
// =============================================================================
#include "HmiExprGuide.hpp"

#include <algorithm>

namespace hmi::exprguide {

namespace {

using exprcheck::Want;

std::vector<TypeEntry> build() {
    std::vector<TypeEntry> t;

    // ---------------------------------------------------------------- BOOL --
    t.push_back(TypeEntry{
        Kind::Bool, "bool", "BOOL", "B", "BOOL",
        "Pompe_Marche AND NOT V[0].Defaut",
        "condition                (* TRUE ou FALSE *)\n"
        "Pompe_Marche AND NOT V[0].Defaut",
        Want::Bool, true,
        {{"AND", "et"}, {"OR", "ou"}, {"XOR", "l'un ou l'autre, pas les deux"}, {"NOT", "le contraire"},
         {"= <>", "\xC3\xA9gal, diff\xC3\xA9rent"}, {"< <= > >=", "comparer deux nombres"}},
        {{"SEL(c, a, b)", "BOOL", "b si c est vraie, a sinon (CEI 61131-3)"},
         {"ABS(x) > s", "BOOL", "un \xC3\xA9" "cart plus grand qu'un seuil"}},
        {{"=Pompe_Marche AND NOT V[0].Defaut", "en marche et sans d\xC3\xA9" "faut"},
         {"=Pression > 3.5", "une comparaison"},
         {"=Mode = T_MODE#Auto", "le mode est Auto"},
         {"=SYS.UserLevel >= 2", "un niveau d'acc\xC3\xA8s suffisant"}},
        {{"=Pompe_Marche && !V[0].Defaut", "=Pompe_Marche AND NOT V[0].Defaut",
          "&& et ! sont du C : en ST, on \xC3\xA9" "crit AND et NOT."},
         {"=Pression", "=Pression > 0",
          "Pression est un REAL : une case BOOL attend une condition."},
         {"=Pompe_Marche = 1", "=Pompe_Marche",
          "Un BOOL ne se compare pas \xC3\xA0 un entier : \xC3\xA9" "cris son nom seul (ou = TRUE)."}},
    });

    // -------------------------------------------------------------- entier --
    t.push_back(TypeEntry{
        Kind::Integer, "entier", "Entier", "12", "INT",
        "REAL_TO_INT(V[0].Pos) MOD 10",
        "42   -7   16#FF   2#1010    (* d\xC3\xA9" "cimal, hexad\xC3\xA9" "cimal, binaire *)\n"
        "SYS.UserLevel * 10",
        Want::Number, true,
        {{"+ - *", "calculer"}, {"/", "la division enti\xC3\xA8re : 7 / 2 = 3"}, {"MOD", "le reste : 7 MOD 2 = 1"},
         {"16#FF", "en hexad\xC3\xA9" "cimal"}, {"2#1010", "en binaire"}},
        {{"ABS(x)", "INT", "la valeur absolue"},
         {"MIN(a, b)  MAX(a, b)", "INT", "le plus petit, le plus grand"},
         {"LIMIT(mn, x, mx)", "INT", "x born\xC3\xA9 entre mn et mx"},
         {"REAL_TO_INT(x)", "INT", "un r\xC3\xA9" "el arrondi \xC3\xA0 l'entier"}},
        {{"=SYS.UserLevel * 10", "un calcul : 20"},
         {"=REAL_TO_INT(V[0].Pos) MOD 10", "le chiffre des unit\xC3\xA9s : 2"},
         {"=16#FF", "en hexad\xC3\xA9" "cimal : 255"},
         {"=LIMIT(0, REAL_TO_INT(Pression * 10.0), 100)", "born\xC3\xA9 de 0 \xC3\xA0 100 : 38"}},
        {{"=V[0].Pos MOD 10", "=REAL_TO_INT(V[0].Pos) MOD 10",
          "MOD ne se fait que sur des entiers : V[0].Pos est un REAL, convertis-le d'abord."},
         {"=SYS.UserLevel / 0", "=SYS.UserLevel / 2",
          "Une division par z\xC3\xA9ro ne donne rien : l'automate s'arr\xC3\xAAterait dessus."}},
    });

    // ---------------------------------------------------------------- reel --
    t.push_back(TypeEntry{
        Kind::Real, "reel", "R\xC3\xA9" "el", "1,5", "REAL",
        "Pression * 1.05",
        "3.8   0.5   1.5E3      (* un point, jamais de virgule *)\n"
        "Pression * 1.05",
        Want::Number, true,
        {{"+ - * /", "calculer"}, {"< <= > >=", "comparer"}, {"1.5E3", "en notation scientifique : 1500.0"}},
        {{"ABS(x)", "REAL", "la valeur absolue"},
         {"SQRT(x)", "REAL", "la racine carr\xC3\xA9" "e"},
         {"LIMIT(mn, x, mx)", "REAL", "x born\xC3\xA9 entre mn et mx"},
         {"INT_TO_REAL(i)", "REAL", "un entier en r\xC3\xA9" "el"}},
        {{"=Pression * 1.05", "5 % de plus : 3.99"},
         {"=LIMIT(0.0, Pression, 3.0)", "born\xC3\xA9 de 0 \xC3\xA0 3 : 3.0"},
         {"=V[0].Pos / 100.0", "une fraction : 0.42"},
         {"=INT_TO_REAL(SYS.UserLevel) * 0.5", "un entier converti : 1.0"}},
        {{"=Pression * 1,05", "=Pression * 1.05",
          "En ST, la virgule s\xC3\xA9pare les arguments : un nombre d\xC3\xA9" "cimal s'\xC3\xA9" "crit avec un point."},
         {"=Pression MOD 2", "=REAL_TO_INT(Pression) MOD 2",
          "MOD ne se fait que sur des entiers : Pression est un REAL."}},
    });

    // --------------------------------------------------------------- texte --
    t.push_back(TypeEntry{
        Kind::Text, "texte", "Texte", "'a'", "STRING",
        "CONCAT('Mode : ', TO_STRING(Mode))",
        "'un texte'          (* entre apostrophes *)\n"
        "CONCAT('Mode : ', TO_STRING(Mode))",
        Want::Text, true,
        {{"'\xE2\x80\xA6'", "un texte"}, {"CONCAT", "mettre bout \xC3\xA0 bout"}, {"= <>", "comparer deux textes"}},
        {{"CONCAT(a, b, \xE2\x80\xA6)", "STRING", "les textes bout \xC3\xA0 bout"},
         {"LEN(s)", "INT", "le nombre de caract\xC3\xA8res"},
         {"LEFT(s, n)  RIGHT(s, n)", "STRING", "les n premiers, les n derniers"},
         {"TO_STRING(m)", "STRING", "le texte affich\xC3\xA9 d'une \xC3\xA9num\xC3\xA9ration"}},
        {{"='En marche'", "un texte fixe"},
         {"=CONCAT('Vanne ', 'V-201')", "deux textes bout \xC3\xA0 bout"},
         {"=SEL(Pompe_Marche, 'Arr\xC3\xAAt\xC3\xA9" "e', 'En marche')", "selon une condition"},
         {"=CONCAT('Mode : ', TO_STRING(Mode))", "le texte d'une \xC3\xA9num\xC3\xA9ration"}},
        {{"=\"En marche\"", "='En marche'",
          "En ST, un texte s'\xC3\xA9" "crit entre apostrophes, pas entre guillemets."},
         {"='Vanne ' + 'V-201'", "=CONCAT('Vanne ', 'V-201')",
          "+ ne colle pas deux textes : CONCAT le fait."}},
    });

    // ------------------------------------------------------- texte a trous --
    t.push_back(TypeEntry{
        Kind::Template, "texte-a-trous", "Texte \xC3\xA0 trous {\xE2\x80\xA6}", "{ }", "texte",
        "Ouverture : {V[0].Pos} %",
        "texte {expression} texte      (* pas de = devant *)\n"
        "Pression : {Pression:0.00} bar",
        Want::Text, false,
        {{"{\xE2\x80\xA6}", "une expression dans le texte"}, {":0.0", "les d\xC3\xA9" "cimales"}, {":0.0%", "un pourcentage (\xC3\x97" "100)"},
         {":Oui|Non", "le texte d'un BOOL : vrai, puis faux"}, {":t", "une dur\xC3\xA9" "e : 2 min 05 s"}, {":X4", "en hexad\xC3\xA9" "cimal"}},
        {{"{x:0.0}  {x:0.##}", "texte", "1 d\xC3\xA9" "cimale ; 2 au plus"},
         {"{x:000}", "texte", "au moins 3 chiffres : 007"},
         {"{b:Oui|Non}", "texte", "le texte d'un bool\xC3\xA9" "en"},
         {"{d:t}", "texte", "une dur\xC3\xA9" "e lisible"}},
        {{"Ouverture : {V[0].Pos} %", "une valeur dans le texte"},
         {"Pression : {Pression:0.00} bar", "deux d\xC3\xA9" "cimales"},
         {"{Pompe_Marche:En marche|\xC3\x80 l'arr\xC3\xAAt}", "le texte d'un BOOL"},
         {"Mode : {TO_STRING(Mode)}", "le texte d'une \xC3\xA9num\xC3\xA9ration"}},
        {{"=Ouverture : {V[0].Pos} %", "Ouverture : {V[0].Pos} %",
          "Un texte \xC3\xA0 trous ne commence pas par = : le texte s'\xC3\xA9" "crit tel quel, les trous entre accolades."},
         {"Ouverture : {V[0].Pos %", "Ouverture : {V[0].Pos} %",
          "L'accolade n'est pas ferm\xC3\xA9" "e."}},
    });

    // ------------------------------------------------------------- couleur --
    t.push_back(TypeEntry{
        Kind::Color, "couleur", "Couleur", "#", "COLOR",
        "SEL(V[0].Pos > 80, '#E53935', '#43A047')",
        "'#RRGGBB'          (* une cha\xC3\xAEne ; '#RRGGBBAA' : avec la transparence *)\n"
        "SEL(condition, si FAUX, si VRAI)",
        Want::Color, true,
        {{"'#RRGGBB'", "une couleur"}, {"SEL", "deux couleurs"}, {"MUX", "plusieurs, par un entier"}},
        {{"SEL(c, a, b)", "COLOR", "b si c est vraie, a sinon (CEI 61131-3)"},
         {"MUX(k, a, b, c, \xE2\x80\xA6)", "COLOR", "la k-i\xC3\xA8me, \xC3\xA0 partir de 0"}},
        {{"=SEL(V[0].Pos > 80, '#E53935', '#43A047')", "vert au-del\xC3\xA0 de 80, rouge sinon"},
         {"=SEL(V[0].Defaut, SEL(Pompe_Marche, '#8A94A3', '#2ECC71'), '#E5534B')", "gris, vert en marche, rouge en d\xC3\xA9" "faut"},
         {"=MUX(TO_INT(Mode), '#586E75', '#2ECC71', '#268BD2', '#E5534B')", "une couleur par mode (0 \xC3\xA0 3)"},
         {"='#2A7FBF80'", "avec 50 % de transparence"}},
        {{"=SEL(V[0].Pos > 80, #E53935, #43A047)", "=SEL(V[0].Pos > 80, '#E53935', '#43A047')",
          "Caract\xC3\xA8re inattendu # : dans une expression, une couleur est une cha\xC3\xAEne, entre apostrophes."},
         {"=SEL(V[0].Pos > 80, '#E53935', '#43A047')", "=SEL(V[0].Pos > 80, '#43A047', '#E53935')",
          "Pour du rouge au-del\xC3\xA0 de 80, le rouge va en dernier : SEL(condition, si FAUX, si VRAI), comme le "
          "simulateur (norme CEI). \xC3\x89" "crite \xC3\xA0 gauche, la cuve est rouge jusqu'\xC3\xA0 80.",
          true},
         {"='rouge'", "='#E53935'",
          "Un nom de couleur se choisit dans la palette (elle ins\xC3\xA8re le code) : dans une expression, '#RRGGBB'."}},
    });

    // ------------------------------------------------------- duree / heure --
    t.push_back(TypeEntry{
        Kind::Time, "duree", "Dur\xC3\xA9" "e / heure", "T#", "TIME",
        "T#5s \xC2\xB7 T#1m30s",
        "T#1h2m30s   T#500ms     (* une dur\xC3\xA9" "e, toujours avec son unit\xC3\xA9 *)\n"
        "ms  s  m  h  d          (* les unit\xC3\xA9s *)",
        Want::Any, true,
        {{"T#\xE2\x80\xA6", "une dur\xC3\xA9" "e"}, {"+ -", "ajouter, retirer"}, {"< <= > >=", "comparer deux dur\xC3\xA9" "es"}},
        {{"TIME_TO_DINT(t)", "DINT", "la dur\xC3\xA9" "e en millisecondes"},
         {"DINT_TO_TIME(n)", "TIME", "des millisecondes en dur\xC3\xA9" "e"},
         {"{t:t}", "texte", "dans un texte \xC3\xA0 trous : 2 min 05 s"}},
        {{"=T#5s", "cinq secondes"},
         {"=T#1m30s + T#15s", "une somme : 1 min 45 s"},
         {"=SEL(Pompe_Marche, T#0s, T#2m)", "selon une condition"},
         {"=T#500ms", "une demi-seconde"}},
        {{"=T#5", "=T#5s",
          "Une dur\xC3\xA9" "e a toujours son unit\xC3\xA9 : ms, s, m, h ou d."},
         {"=5s", "=T#5s",
          "Une dur\xC3\xA9" "e commence par T# (ou TIME#)."}},
    });

    // ----------------------------------------------------------------- vue --
    t.push_back(TypeEntry{
        Kind::View, "vue", "Vue", "V", "VUE",
        "'Vue_Synoptique'",
        "'Nom_De_La_Vue'     (* le nom d'une vue du projet, entre apostrophes *)\n"
        "SEL(condition, 'Vue_A', 'Vue_B')",
        Want::Text, true,
        {{"'Vue_\xE2\x80\xA6'", "une vue"}, {"SEL", "deux vues"}, {"MUX", "une vue par num\xC3\xA9ro"}},
        {{"SEL(c, a, b)", "STRING", "la vue b si c est vraie, a sinon"},
         {"MUX(k, a, b, \xE2\x80\xA6)", "STRING", "la k-i\xC3\xA8me vue, \xC3\xA0 partir de 0"}},
        {{"='Vue_Synoptique'", "une vue fixe"},
         {"=SEL(V[0].Defaut, 'Vue_Accueil', 'Vue_Armoire_A')", "l'armoire quand la vanne est en d\xC3\xA9" "faut"},
         {"=Vue_Demandee", "une variable texte qui porte le nom"},
         {"=MUX(SYS.UserLevel, 'Vue_Accueil', 'Vue_Accueil', 'Vue_Commandes', 'Vue_Commandes')", "selon le niveau d'acc\xC3\xA8s"}},
        {{"=Vue_Synoptique", "='Vue_Synoptique'",
          "Sans apostrophes, Vue_Synoptique est lu comme une variable : le nom d'une vue est un texte."},
         {"='Vue_Synoptiqe'", "='Vue_Synoptique'",
          "Cette vue n'existe pas : veux-tu dire Vue_Synoptique ?"}},
    });

    // --------------------------------------------------------- enumeration --
    t.push_back(TypeEntry{
        Kind::Enum, "enumeration", "\xC3\x89num\xC3\xA9ration", "E#", "T_MODE",
        "Mode = T_MODE#Auto",
        "T_MODE#Valeur      (* le type, #, la valeur *)\n"
        "Mode = T_MODE#Auto",
        Want::Any, true,
        {{"T_MODE#\xE2\x80\xA6", "une valeur"}, {"= <>", "comparer deux valeurs du m\xC3\xAAme type"}},
        {{"TO_STRING(m)", "STRING", "le texte affich\xC3\xA9 : \xC2\xAB Automatique \xC2\xBB"},
         {"TO_INT(m)", "INT", "sa valeur : 1"},
         {"TO_T_MODE(x)", "T_MODE", "depuis un texte ou un entier (contr\xC3\xB4l\xC3\xA9)"}},
        {{"=T_MODE#Manu", "une valeur"},
         {"=SEL(Pompe_Marche, T_MODE#Arret, T_MODE#Auto)", "deux valeurs"},
         {"=Mode", "une variable de ce type"},
         {"=TO_T_MODE(2)", "depuis un entier (contr\xC3\xB4l\xC3\xA9)"}},
        {{"=Mode = 1", "=Mode = T_MODE#Auto",
          "T_MODE et INT ne se comparent pas : \xC3\xA9" "cris T_MODE#Auto (ou TO_INT(Mode) = 1).", false, "bool"},
         {"=T_MODE#Automatique", "=T_MODE#Auto",
          "Cette valeur n'existe pas : veux-tu dire T_MODE#Auto ?"}},
    });

    // ---------------------------------------------------- membre et element --
    t.push_back(TypeEntry{
        Kind::Member, "membre", "Membre et \xC3\xA9l\xC3\xA9ment", "[.]", "le type du membre",
        "V[0].Pos",
        "Tableau[indice].Membre     (* l'indice va de la borne basse \xC3\xA0 la borne haute *)\n"
        "V[0].Pos   V[SYS.UserLevel].Defaut",
        Want::Any, true,
        {{"[ ]", "un \xC3\xA9l\xC3\xA9ment du tableau"}, {".", "un membre de la structure"}, {"[i + 1]", "un indice calcul\xC3\xA9"}},
        {{"SEL(c, V[0].Pos, V[1].Pos)", "REAL", "l'un ou l'autre \xC3\xA9l\xC3\xA9ment"},
         {"MAX(V[0].Pos, V[1].Pos)", "REAL", "le plus grand des deux"}},
        {{"=V[0].Pos", "un membre d'un \xC3\xA9l\xC3\xA9ment : 42"},
         {"=V[1].Defaut OR V[2].Defaut", "deux \xC3\xA9l\xC3\xA9ments"},
         {"=V[SYS.UserLevel].Pos", "un indice calcul\xC3\xA9 : V[2]"},
         {"=MAX(V[0].Pos, V[1].Pos)", "le plus grand des deux"}},
        {{"=V.Pos", "=V[0].Pos",
          "V est un tableau : dis lequel, de V[0] \xC3\xA0 V[3]."},
         {"=V[4].Pos", "=V[3].Pos",
          "Indice hors bornes : V va de 0 \xC3\xA0 3."},
         {"=V[0].Position", "=V[0].Pos",
          "Le membre Position n'existe pas dans T_VANNE : veux-tu dire Pos ?"}},
    });

    // ------------------------------------------------------ liste de choix --
    // A VERIFIER (tranche 2) : les proprietes a valeurs fixes de HmiPanels.cpp
    // qui acceptent une expression ; l'exemple suit la maquette (le texte d'une
    // liste, choisi par SEL / MUX).
    t.push_back(TypeEntry{
        Kind::Choice, "liste", "Liste de choix", "\xE2\x89\xA1", "un choix de la liste",
        "SEL(Pompe_Marche, 'gauche', 'centre')",
        "'valeur'           (* une des valeurs de la liste, entre apostrophes *)\n"
        "SEL(condition, 'valeur si FAUX', 'valeur si VRAI')",
        Want::Text, true,
        {{"'\xE2\x80\xA6'", "une valeur de la liste"}, {"SEL", "deux valeurs"}, {"MUX", "une valeur par num\xC3\xA9ro"}},
        {{"SEL(c, a, b)", "STRING", "b si c est vraie, a sinon"},
         {"MUX(k, a, b, \xE2\x80\xA6)", "STRING", "la k-i\xC3\xA8me, \xC3\xA0 partir de 0"}},
        {{"='centre'", "une valeur fixe"},
         {"=SEL(Pompe_Marche, 'gauche', 'centre')", "selon une condition"},
         {"=MUX(TO_INT(Mode), 'gauche', 'centre', 'droite', 'droite')", "selon le mode"}},
        {{"='milieu'", "='centre'",
          "Cette valeur n'est pas dans la liste : gauche, centre ou droite."}},
    });

    return t;
}

// Sans casse : "Couleur" et "couleur" sont la meme cle.
bool sameKey(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

} // namespace

const std::vector<TypeEntry>& all() {
    static const std::vector<TypeEntry> table = build();
    return table;
}

const TypeEntry* find(std::string_view key) {
    if (key.size() > 5 && sameKey(key.substr(0, 5), "expr-")) key.remove_prefix(5);
    for (const auto& e : all())
        if (sameKey(e.key, key)) return &e;
    return nullptr;
}

const TypeEntry* forKind(Kind k) {
    for (const auto& e : all())
        if (e.kind == k) return &e;
    return nullptr;
}

const TypeEntry* forWant(exprcheck::Want w) {
    switch (w) {
        case exprcheck::Want::Bool:   return forKind(Kind::Bool);
        case exprcheck::Want::Number: return forKind(Kind::Real);
        case exprcheck::Want::Text:   return forKind(Kind::Text);
        case exprcheck::Want::Color:  return forKind(Kind::Color);
        case exprcheck::Want::Any:    break;
    }
    return forKind(Kind::Member);
}

std::string topicKey(const TypeEntry& e) {
    return "expr-" + std::string(e.key);
}

} // namespace hmi::exprguide
