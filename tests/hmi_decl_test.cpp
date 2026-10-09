// =============================================================================
//  tests/hmi_decl_test.cpp - 1.11.18 (refonte des scripts, lot 2) : LES DECLARATIONS
//  LUES SANS PERTE (hmi::decl)
// -----------------------------------------------------------------------------
//  Le lexer de surface ; chaque forme de bloc et de declaration ; les commentaires
//  rattaches ; les fonctions internes et leur en-tete ; les fautes et leur place ;
//  compose (relu a l'identique) ; la signature des parametres. Puis le corpus : les
//  codes des essais, du guide et des sessions (tests/fixtures/decl/corpus.txt,
//  outils/corpus_declarations.py) et ceux des projets donnes - extract y lit les
//  memes declarations que splitDeclarations, chaque bloc recompose se relit a
//  l'identique, et un code coupe n'importe ou se lit sans sortir de ses bornes.
//  L'arbre du projet (hmitree::withoutDeclarations), relu par hmi::decl, y donne
//  les memes locales, parametres et variables que sa lecture d'avant (figee ici).
//
//      hmi_decl_test <corpus.txt> [<dossier de projet>...]
// =============================================================================
#include "../src/app/hmi/HmiTreeData.hpp"     // hmitree::withoutDeclarations : relu par hmi::decl
#include "../src/hmi/HmiDecl.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace hmi;
namespace dc = hmi::decl;
namespace hmitree = app::hmitree;
namespace hmikit = app::hmikit;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

constexpr std::size_t npos = static_cast<std::size_t>(-1);

std::string upperOf(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

// Un texte relu par le lexer : sans commentaires, une espace la ou il y avait un blanc
// (la forme des types et des valeurs initiales d'un Decl).
std::string flat(std::string_view code) {
    std::string out;
    std::size_t prevEnd = npos;
    bool gap = false;
    for (const auto& t : dc::lex(code)) {
        if (t.kind == dc::TokenKind::Comment) {
            gap = true;
            continue;
        }
        if (!out.empty() && (gap || t.begin > prevEnd)) out += ' ';
        out += t.text(code);
        prevEnd = t.end;
        gap = false;
    }
    return out;
}

// Les jetons d'un code (sans commentaires), une espace entre chacun.
std::string tokens(std::string_view code) {
    std::string out;
    for (const auto& t : dc::lex(code)) {
        if (t.kind == dc::TokenKind::Comment) continue;
        if (!out.empty()) out += ' ';
        out += t.text(code);
    }
    return out;
}

// Les declarations, en texte comparable (le groupe : son rang).
std::string summary(const std::vector<dc::Decl>& ds) {
    std::string out;
    std::size_t prev = npos;
    int rank = -1;
    for (const auto& d : ds) {
        if (d.group != prev) {
            ++rank;
            prev = d.group;
        }
        out += d.name + " | " + d.type + " | " + d.initial + " | " + d.comment + " | " + std::string(dc::keyword(d.section)) + " | "
             + (d.constant ? "C" : "") + (d.retain ? "R" : "") + " | "
             + (d.block == dc::kHeader ? std::string("en-tete") : std::to_string(d.block)) + " | " + std::to_string(rank) + "\n";
    }
    return out;
}

std::string errorsOf(const dc::Extract& x) {
    std::string out;
    for (const auto& e : x.errors)
        out += std::to_string(e.line) + ":" + std::to_string(e.column) + "+" + std::to_string(e.length) + " " + e.message + "\n";
    return out;
}

// ---------------------------------------------------------------- le lexer -----
void lexer() {
    std::printf("-- le lexer de surface\n");
    const std::string code = "x := 'd$'urgence' + \"a$\"b\"; (* a ' b *) // c (* d\n"
                             "y := T#5s + 16#FF + E_Mode#Auto;\n"
                             "$Vanne$.Ouv := 1..4 <= 2;\n"
                             "(* sur\n"
                             "deux lignes *) 'ouverte";
    const auto t = dc::lex(code);
    const auto is = [&](std::size_t k, dc::TokenKind kind, std::string_view text) {
        return k < t.size() && t[k].kind == kind && t[k].text(code) == text;
    };
    using K = dc::TokenKind;
    check(t.size() == 28, "28 jetons (" + std::to_string(t.size()) + ")");
    check(is(0, K::Word, "x") && is(1, K::Punct, ":="), "x := : un mot, une ponctuation de deux octets");
    check(is(2, K::String, "'d$'urgence'"), "'d$'urgence' : une seule chaine ($' n'est pas sa fin, comme dans le simulateur)");
    check(is(4, K::String, "\"a$\"b\""), "\"a$\"b\" : une seule chaine");
    check(is(6, K::Comment, "(* a ' b *)"), "une apostrophe dans un commentaire n'ouvre rien");
    check(is(7, K::Comment, "// c (* d"), "// jusqu'a la fin de la ligne (son (* n'ouvre rien)");
    check(is(8, K::Word, "y") && t[8].line == 2 && t[8].column == 1, "y : ligne 2, colonne 1");
    check(is(10, K::Number, "T#5s") && is(12, K::Number, "16#FF") && is(14, K::Number, "E_Mode#Auto"),
          "T#5s, 16#FF, E_Mode#Auto : des litteraux d'un jeton");
    check(is(16, K::Punct, "$") && is(17, K::Word, "Vanne") && is(18, K::Punct, "$") && is(19, K::Punct, "."),
          "$Vanne$ : un repere se lit tel quel");
    check(is(22, K::Number, "1..4") && is(23, K::Punct, "<="), "1..4 et <=");
    check(t.size() > 26 && t[26].kind == K::Comment && t[26].line == 4 && t[26].lastLine == 5,
          "un commentaire sur deux lignes : sa premiere et sa derniere ligne");
    check(is(27, K::String, "'ouverte") && t[27].line == 5 && t[27].column == 16, "une chaine sans fin : jusqu'au bout, a sa place");
    check(dc::commentText("(*  a\n   b  *)") == "a b", "commentText : blancs resserres");
    check(dc::commentText("//  x  y") == "x y" && dc::commentText("(**)").empty() && dc::commentText("(* ouvert") == "ouvert",
          "commentText : //, vide, sans fin");
    check(dc::lex("").empty() && dc::lex(" \n\t ").empty(), "un code vide : aucun jeton");
}

// ---------------------------------------------------------------- les formes ----
void formes() {
    std::printf("-- les formes de declaration\n");
    const std::string code =
        "VAR (* les compteurs *)\n"                                       // 1
        "    Compte : INT := 5;   (* le nombre *)\n"                      // 2
        "    (* deux noms *)\n"                                           // 3
        "    Dernier, Autre : STRING := 'a;b';\n"                         // 4
        "\n"                                                              // 5
        "    (* seul, apres une ligne vide *)\n"                          // 6
        "\n"                                                              // 7
        "    t : ARRAY[1..4] OF INT := [1, 2,   3, 4]; // tableau\n"      // 8
        "    p : T_POINT := (x := 1, y := 2);\n"                          // 9
        "    v : INT := $Defaut$;\n"                                      // 10
        "    (* avant END_VAR *)\n"                                       // 11
        "END_VAR\n"                                                       // 12
        "VAR_TEMP i : INT; END_VAR\n"                                     // 13
        "VAR CONSTANT RETAIN\n"                                           // 14
        "    Max : REAL := 1.5E3;\n"                                      // 15
        "END_VAR;\n"                                                      // 16
        "VAR_INPUT a, b : REAL; END_VAR\n"                                // 17
        "VAR_IN_OUT acc : T_VEC; END_VAR\n"                               // 18
        "VAR_OUTPUT ok : BOOL; END_VAR\n"                                 // 19
        "Compte := Compte + 1; (* VAR dans un commentaire *) s := 'VAR x'; y.VAR := $VAR$;\n";   // 20
    const auto x = dc::extract(code);
    check(x.errors.empty(), "aucune faute : " + errorsOf(x));
    check(x.blocks.size() == 6, "six blocs (" + std::to_string(x.blocks.size()) + ") : VAR, VAR_TEMP, VAR CONSTANT RETAIN, VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT");
    check(x.decls.size() == 12, "douze declarations (" + std::to_string(x.decls.size()) + ")");
    if (x.blocks.size() != 6 || x.decls.size() != 12) return;
    const auto& d = x.decls;
    check(d[0].name == "Compte" && d[0].type == "INT" && d[0].initial == "5" && d[0].comment == "le nombre" && d[0].line == 2
              && d[0].column == 5 && d[0].block == 0 && d[0].section == dc::Section::Var,
          "Compte : INT := 5, son commentaire de fin de ligne, sa place (2:5)");
    check(d[1].name == "Dernier" && d[2].name == "Autre" && d[1].group == d[2].group && d[1].group != d[0].group
              && d[1].type == "STRING" && d[2].initial == "'a;b'" && d[1].comment == "deux noms" && d[2].comment == "deux noms"
              && d[2].line == 4 && d[2].column == 14,
          "Dernier, Autre : un groupe, une chaine qui contient ';', le commentaire du dessus");
    check(d[3].type == "ARRAY[1..4] OF INT" && d[3].initial == "[1, 2, 3, 4]" && d[3].comment == "tableau",
          "un tableau et sa valeur initiale (blancs resserres), un commentaire //");
    check(d[4].type == "T_POINT" && d[4].initial == "(x := 1, y := 2)", "une structure : ses := entre parentheses ne coupent rien");
    check(d[5].initial == "$Defaut$", "un repere $Defaut$ : intact");
    const auto& c0 = x.blocks[0].comments;
    check(c0.size() == 3 && c0[0] == "les compteurs" && c0[1] == "seul, apres une ligne vide" && c0[2] == "avant END_VAR",
          "les commentaires du bloc : sur la ligne de VAR, apres une ligne vide, avant END_VAR");
    check(d[6].name == "i" && d[6].section == dc::Section::Temp && d[6].block == 1 && d[6].line == 13 && d[6].column == 10,
          "VAR_TEMP i : INT sur une ligne : 13:10");
    const auto& b2 = x.blocks[2];
    check(d[7].name == "Max" && d[7].constant && d[7].retain && d[7].initial == "1.5E3" && b2.constant && b2.retain
              && b2.qualifiers == std::vector<std::string>{"CONSTANT", "RETAIN"},
          "VAR CONSTANT RETAIN : les drapeaux et les qualificatifs tels qu'ecrits");
    check(b2.firstLine == 14 && b2.lastLine == 16
              && code.substr(b2.begin, b2.end - b2.begin) == "VAR CONSTANT RETAIN\n    Max : REAL := 1.5E3;\nEND_VAR;",
          "le bloc va du mot VAR au ';' qui suit END_VAR (lignes 14 a 16)");
    check(d[8].section == dc::Section::Input && d[9].section == dc::Section::Input && d[10].section == dc::Section::InOut
              && d[11].section == dc::Section::Output && d[8].group == d[9].group,
          "VAR_INPUT a, b ; VAR_IN_OUT ; VAR_OUTPUT");
    const auto params = x.parameters();
    check(params.size() == 4 && params[0]->name == "a" && params[3]->name == "ok", "parameters : a, b, acc, ok");
    check(x.of(dc::Section::Var).size() == 7 && x.of(dc::Section::Temp).size() == 1, "of(Var) : 7 ; of(Temp) : 1");
    check(x.find("compte") == &d[0] && x.find("ACC") == &d[10] && x.find("s") == nullptr, "find : sans casse ; s (le code) : aucune");
    // Le corps : les blocs blanchis, lignes et colonnes gardees ; le code intact.
    const auto lastLine = x.body.rfind('\n', x.body.size() - 2);
    check(x.body.size() == code.size() && std::count(x.body.begin(), x.body.end(), '\n') == 20
              && x.body.substr(lastLine + 1) == code.substr(lastLine + 1)
              && x.body.substr(0, lastLine).find_first_not_of(" \n") == std::string::npos,
          "le corps : les blocs blanchis (lignes et colonnes gardees), le code de la ligne 20 intact");
}

// ---------------------------------------------------------- les commentaires ---
void commentaires() {
    std::printf("-- les commentaires rattaches\n");
    const std::string code = "VAR\n"
                             "    a : INT;  (* fin a *) (* encore a *)\n"
                             "    b (* dedans *) : INT;\n"
                             "    (* au-dessus de c *)\n"
                             "    // et encore\n"
                             "    c : INT; d : INT; (* fin d *)\n"
                             "    e : INT; (* sur\n"
                             "                deux lignes *)\n"
                             "END_VAR\n";
    const auto x = dc::extract(code);
    check(x.errors.empty() && x.decls.size() == 5, "cinq declarations, aucune faute");
    if (x.decls.size() != 5) return;
    check(x.decls[0].comment == "fin a encore a", "a : ses deux commentaires de fin de ligne");
    check(x.decls[1].comment == "dedans", "b : son commentaire de dedans");
    check(x.decls[2].comment == "au-dessus de c et encore", "c : les deux lignes du dessus");
    check(x.decls[3].comment == "fin d", "d (meme ligne que c) : le commentaire qui suit son ';'");
    check(x.decls[4].comment == "sur deux lignes", "e : un commentaire sur deux lignes, resserre");
    check(x.blocks[0].comments.empty(), "aucun commentaire laisse au bloc");
}

// ---------------------------------------------------------- les fonctions ------
void fonctions() {
    std::printf("-- les fonctions internes\n");
    const std::string code = "FUNCTION Carre(x : REAL; VAR_IN_OUT acc : REAL, n : INT := 2) : REAL   (* 1 *)\n"   // 1
                             "VAR\n"                                                                              // 2
                             "    t : REAL;   (* le carre *)\n"                                                    // 3
                             "END_VAR\n"                                                                          // 4
                             "    t := x * x;\n"                                                                  // 5
                             "    Carre := t;\n"                                                                  // 6
                             "END_FUNCTION\n"                                                                     // 7
                             "VAR\n"                                                                              // 8
                             "    r : REAL;\n"                                                                    // 9
                             "END_VAR\n"                                                                          // 10
                             "r := Carre(2.0, r);\n"                                                              // 11
                             "FUNCTION Liste : ARRAY[1..3]\n"                                                     // 12
                             "    OF INT\n"                                                                       // 13
                             "    Liste := [1, 2, 3];\n"                                                          // 14
                             "END_FUNCTION;\n"                                                                    // 15
                             "FUNCTION Rien (VAR_OUTPUT CONSTANT o : BOOL)\n"                                     // 16
                             "END_FUNCTION\n";                                                                    // 17
    const auto x = dc::extract(code);
    check(x.errors.empty(), "aucune faute : " + errorsOf(x));
    check(x.functions.size() == 3, "trois fonctions internes (" + std::to_string(x.functions.size()) + ")");
    check(x.decls.size() == 1 && x.decls[0].name == "r" && x.blocks.size() == 1,
          "le premier niveau : r seul (les declarations des fonctions restent a elles)");
    if (x.functions.size() != 3) return;
    const auto& f = x.functions[0];
    check(f.name == "Carre" && f.returnType == "REAL" && f.firstLine == 1 && f.lastLine == 7 && f.blocks.size() == 1,
          "Carre : son nom, son retour, ses lignes, son bloc");
    check(f.decls.size() == 4, "Carre : quatre declarations");
    if (f.decls.size() == 4) {
        check(f.decls[0].name == "x" && f.decls[0].section == dc::Section::Input && f.decls[0].block == dc::kHeader,
              "x : un parametre d'en-tete (VAR_INPUT par defaut)");
        check(f.decls[1].name == "acc" && f.decls[1].section == dc::Section::InOut && f.decls[1].block == dc::kHeader,
              "VAR_IN_OUT acc : son mode");
        check(f.decls[2].name == "n" && f.decls[2].section == dc::Section::Input && f.decls[2].initial == "2"
                  && f.decls[2].group != f.decls[1].group,
              "n : INT := 2 apres une ',' : un autre groupe, VAR_INPUT (le mode vaut pour un groupe, comme dans le simulateur)");
        check(f.decls[3].name == "t" && f.decls[3].section == dc::Section::Var && f.decls[3].block == 0
                  && f.decls[3].comment == "le carre",
              "t : la locale de son bloc, son commentaire");
    }
    check(x.functions[1].name == "Liste" && x.functions[1].returnType == "ARRAY[1..3] OF INT" && x.functions[1].decls.empty()
              && code.substr(x.functions[1].begin, x.functions[1].end - x.functions[1].begin).back() == ';',
          "Liste : un retour sur deux lignes (OF), le ';' de END_FUNCTION");
    const auto& rien = x.functions[2];
    check(rien.name == "Rien" && rien.returnType.empty() && rien.decls.size() == 1 && rien.decls[0].section == dc::Section::Output
              && rien.decls[0].constant,
          "Rien : sans retour ; VAR_OUTPUT CONSTANT o");
    // Le corps : les fonctions restent (le simulateur les lit), le bloc du premier niveau est blanchi.
    std::istringstream lines(x.body);
    std::vector<std::string> l;
    for (std::string s; std::getline(lines, s);) l.push_back(s);
    check(l.size() == 17 && l[0] == "FUNCTION Carre(x : REAL; VAR_IN_OUT acc : REAL, n : INT := 2) : REAL   (* 1 *)" && l[1] == "VAR"
              && l[7].find_first_not_of(' ') == std::string::npos && l[8].find_first_not_of(' ') == std::string::npos
              && l[10] == "r := Carre(2.0, r);",
          "le corps : les fonctions intactes, le bloc VAR de r blanchi");
}

// ---------------------------------------------------------- les fautes ---------
void fautes() {
    std::printf("-- les fautes et leur place\n");
    {
        const auto x = dc::extract("VAR\n  a : INT;\n  b : INT\nEND_VAR\n");
        check(x.errors.size() == 1 && x.errors[0].line == 3 && x.errors[0].column == 3 && x.errors[0].length == 7
                  && x.errors[0].message == "';' attendu apr\xC3\xA8s la d\xC3\xA9" "claration \xC2\xAB b : INT \xC2\xBB",
              "un ';' manque avant END_VAR : la faute de splitDeclarations, a sa place (3:3, 7 octets) - " + errorsOf(x));
        check(x.decls.size() == 2 && x.blocks.size() == 1 && x.blocks[0].closed && x.blocks[0].errors == 1,
              "la declaration reste lue ; le bloc compte sa faute");
    }
    {
        const auto x = dc::extract("VAR\n  a INT;\n  b : ;\n  c : INT := ;\n  d : INT e : INT;\n  , f : INT;\n  g, : INT;\n  h : INT;\nEND_VAR");
        check(x.errors.size() == 6, "six declarations illisibles (" + std::to_string(x.errors.size()) + ") - " + errorsOf(x));
        check(!x.errors.empty() && x.errors[0].line == 2
                  && x.errors[0].message == "d\xC3\xA9" "claration illisible : \xC2\xAB a INT \xC2\xBB (nom : TYPE ;)",
              "la premiere : la faute de splitDeclarations, ligne 2");
        check(x.decls.size() == 1 && x.decls[0].name == "h" && x.blocks[0].errors == 6, "h reste lu ; le bloc compte six fautes");
    }
    {
        const std::string code = "VAR\n  a : INT;\n  (* note *)\nx := 1;\n";
        const auto x = dc::extract(code);
        check(x.errors.size() == 1 && x.errors[0].message == "VAR sans END_VAR" && x.errors[0].line == 1 && x.errors[0].column == 1,
              "un bloc ouvert : une seule faute, celle de splitDeclarations (le reste est sans doute du code) - " + errorsOf(x));
        check(x.blocks.size() == 1 && !x.blocks[0].closed && x.blocks[0].end == code.size() && x.decls.size() == 1
                  && x.body.find_first_not_of(" \n") == std::string::npos,
              "le bloc ouvert va jusqu'au bout du code (blanchi) ; a reste lu");
    }
    {
        const auto x = dc::extract("VAR a : INT;\nVAR_TEMP b : INT;\nEND_VAR\nx := 1;");
        check(x.errors.size() == 1 && x.errors[0].message == "VAR sans END_VAR" && x.blocks.size() == 2 && !x.blocks[0].closed
                  && x.blocks[1].closed && x.decls.size() == 2 && x.decls[1].section == dc::Section::Temp
                  && x.body.substr(x.body.rfind('\n') + 1) == "x := 1;",
              "un END_VAR oublie avant VAR_TEMP : le bloc s'arrete au mot suivant, qui est lu");
    }
    {
        const auto x = dc::extract("FUNCTION F(a : INT\nx := 1;");
        check(x.errors.size() == 2 && x.errors[0].message == "FUNCTION F : ')' attendu apr\xC3\xA8s ses param\xC3\xA8tres"
                  && x.errors[1].message == "FUNCTION F sans END_FUNCTION",
              "une fonction interne sans ')' ni END_FUNCTION - " + errorsOf(x));
        const auto y = dc::extract("FUNCTION\nEND_FUNCTION");
        check(y.errors.size() == 1 && y.errors[0].message == "FUNCTION sans nom", "FUNCTION sans nom");
    }
    {
        const auto x = dc::extract("x.VAR := 1; $VAR$ := 2; s := 'VAR a : INT; END_VAR'; (* VAR *) // VAR");
        check(x.blocks.empty() && x.errors.empty(), "un membre .VAR, un repere $VAR$, une chaine, des commentaires : aucun bloc");
    }
}

// ---------------------------------------------------------- compose ------------
void recomposer() {
    std::printf("-- compose\n");
    const std::string code = "VAR CONSTANT (* les bornes *)\n"
                             "  Max, Min : INT := 10; // a *) b\n"
                             "  c : REAL;\n"
                             "END_VAR";
    const auto x = dc::extract(code);
    const std::string want = "VAR CONSTANT (* les bornes *)\n"
                             "    Max, Min : INT := 10;   // a *) b\n"
                             "    c : REAL;\n"
                             "END_VAR";
    const std::string got = x.blocks.empty() ? std::string{} : dc::compose(x.blocks[0], x.decls, 0);
    check(got == want, "compose : un groupe par ligne, le commentaire en fin de ligne (// s'il contient *)), ceux du bloc sur la ligne de VAR\n" + got);
    if (!x.blocks.empty()) {
        auto b = x.blocks[0];
        b.constant = false;
        b.retain = true;
        const std::string flags = dc::compose(b, x.decls, 0);
        check(flags.rfind("VAR RETAIN (* les bornes *)\n", 0) == 0, "compose suit les drapeaux du bloc : sans CONSTANT, avec RETAIN");
        b.comments = {"x *) y", "z"};
        const std::string late = dc::compose(b, x.decls, 0);
        const auto y = dc::extract(late);
        check(late.rfind("VAR RETAIN (* z *)\n", 0) == 0 && late.find("\n    // x *) y\nEND_VAR") != std::string::npos
                  && y.blocks.size() == 1 && y.blocks[0].comments.size() == 2,
              "un commentaire du bloc qui contient *) : en // avant END_VAR, et il reste au bloc");
        check(y.decls.size() == 3 && y.decls[0].name == "Max" && y.decls[1].name == "Min" && y.decls[0].group == y.decls[1].group
                  && y.decls[0].comment == "a *) b" && y.decls[2].name == "c" && y.decls[0].retain && !y.decls[0].constant,
              "relu : les memes declarations, sous les drapeaux du bloc");
    }
}

// ---------------------------------------------------------- la signature -------
void signature() {
    std::printf("-- la signature des parametres\n");
    check(dc::parameterSignature(dc::extract("VAR_INPUT\n  a : REAL; (* x *)\n  b : REAL;\nEND_VAR\nRETURN;")) == "a : REAL; b : REAL;",
          "la forme de pipeline::signatureOf pour un bloc VAR_INPUT simple");
    const std::string rich = "VAR_INPUT a, b : REAL := 0.5; END_VAR\nVAR_IN_OUT v : T_VEC; END_VAR\nVAR_OUTPUT ok : BOOL; END_VAR\nVAR t : INT; END_VAR";
    const std::string sig = dc::parameterSignature(dc::extract(rich));
    check(sig == "a : REAL := 0.5; b : REAL := 0.5; VAR_IN_OUT v : T_VEC; VAR_OUTPUT ok : BOOL;", "VAR_IN_OUT, VAR_OUTPUT, une valeur par defaut : " + sig);
    const std::string same = "(* entete *)\nVAR_INPUT\n  a:REAL:=0.5;   // premier\n  b  :  REAL := 0.5 ;\nEND_VAR\n"
                             "VAR_IN_OUT\n  v : T_VEC;\nEND_VAR\nVAR_OUTPUT ok : BOOL; END_VAR\nVAR u, w : DINT; END_VAR\nok := TRUE;";
    check(dc::parameterSignature(dc::extract(same)) == sig, "blancs, commentaires, locales et code a part : la meme signature");
    check(dc::parameterSignature(dc::extract("VAR_INPUT a : REAL; b : LREAL := 0.5; END_VAR")) != dc::parameterSignature(dc::extract("VAR_INPUT a : REAL; b : REAL := 0.5; END_VAR")),
          "un type change : la signature change");
    check(dc::parameterSignature(dc::extract("VAR t : INT; END_VAR t := 1;")).empty(), "sans parametre : vide");
}

// ---------------------------------------------------------- le corpus ----------
struct Tally {
    int codes{0};
    int compared{0};
    int skipped{0};
    std::size_t decls{0};
    int recomposed{0};
    int blocks{0};
    int tree{0};
};

// extract lit les memes declarations que splitDeclarations (VAR, VAR_TEMP, VAR_INPUT),
// la ou splitDeclarations ne dit aucune faute.
void sameAsSplit(const std::string& code, const std::string& where, Tally& t) {
    ++t.codes;
    const auto x = dc::extract(code);
    // Une fonction IHM : VAR_INPUT permis ; un script a fonctions internes : elles gardent leurs blocs.
    const auto old = splitDeclarations(code, x.functions.empty(), [](std::string_view) { return true; });
    if (!old.errors.empty()) {
        ++t.skipped;
        // XPG_DECL_DETAIL=1 : ce que chacun en dit (une faute de sens - un nom reserve, un
        // double - n'est pas a extract, qui lit la surface).
        if (const char* detail = std::getenv("XPG_DECL_DETAIL"); detail && *detail == '1')
            std::printf("  (compare saute) %s\n    splitDeclarations : %s\n    extract : %s", where.c_str(), old.errors.front().message.c_str(),
                        x.errors.empty() ? "aucune faute\n" : errorsOf(x).c_str());
        return;
    }
    ++t.compared;
    std::vector<const dc::Decl*> mine;
    for (const auto& d : x.decls)
        if (d.section == dc::Section::Var || d.section == dc::Section::Temp || d.section == dc::Section::Input) mine.push_back(&d);
    std::string why;
    if (!x.errors.empty()) why = "extract dit une faute : " + errorsOf(x);
    else if (mine.size() != old.locals.size())
        why = std::to_string(mine.size()) + " declaration(s) au lieu de " + std::to_string(old.locals.size());
    else
        for (std::size_t i = 0; i < mine.size() && why.empty(); ++i) {
            const auto& o = old.locals[i];
            const auto& d = *mine[i];
            const bool section = (o.section == LocalVar::Section::Var && d.section == dc::Section::Var)
                              || (o.section == LocalVar::Section::Temp && d.section == dc::Section::Temp)
                              || (o.section == LocalVar::Section::Input && d.section == dc::Section::Input);
            const bool type = localTypeSupported(d.type) ? o.type == upperOf(d.type) : flat(o.type) == d.type;
            if (o.name != d.name || !section || !type || flat(o.initial) != d.initial || o.line != d.line)
                why = "\xC2\xAB " + o.name + " : " + o.type + " := " + o.initial + " \xC2\xBB ligne " + std::to_string(o.line) + " / \xC2\xAB "
                    + d.name + " : " + d.type + " := " + d.initial + " \xC2\xBB ligne " + std::to_string(d.line);
        }
    // splitDeclarations laisse VAR_IN_OUT et VAR_OUTPUT dans le corps (le simulateur les lit).
    const bool rich = std::any_of(x.blocks.begin(), x.blocks.end(), [](const dc::Block& b) {
        return b.section == dc::Section::InOut || b.section == dc::Section::Output;
    });
    if (why.empty() && !rich && old.body != x.body) why = "le corps differe";
    check(why.empty(), where + " : extract lit comme splitDeclarations" + (why.empty() ? std::string{} : " - " + why));
    t.decls += mine.size();
}

// Les blocs (du premier niveau et des fonctions internes) recomposes : relus a l'identique.
void roundTrip(const std::string& code, const std::string& where, Tally& t) {
    const auto x = dc::extract(code);
    if (!x.errors.empty()) return;
    struct Edit {
        std::size_t begin, end;
        std::string text;
    };
    std::vector<Edit> edits;
    for (std::size_t b = 0; b < x.blocks.size(); ++b) edits.push_back({x.blocks[b].begin, x.blocks[b].end, dc::compose(x.blocks[b], x.decls, b)});
    for (const auto& f : x.functions)
        for (std::size_t b = 0; b < f.blocks.size(); ++b) edits.push_back({f.blocks[b].begin, f.blocks[b].end, dc::compose(f.blocks[b], f.decls, b)});
    if (edits.empty()) return;
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    std::string rebuilt = code;
    for (const auto& e : edits) rebuilt.replace(e.begin, e.end - e.begin, e.text);
    const auto y = dc::extract(rebuilt);
    // Le code hors des blocs : le meme.
    const auto outside = [](const std::string& text, const dc::Extract& e) {
        std::string s = text;
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        for (const auto& b : e.blocks) spans.emplace_back(b.begin, b.end);
        for (const auto& f : e.functions)
            for (const auto& b : f.blocks) spans.emplace_back(b.begin, b.end);
        for (const auto& [a, z] : spans)
            for (std::size_t i = a; i < z && i < s.size(); ++i) s[i] = ' ';
        return tokens(s);
    };
    const auto comments = [](const std::vector<dc::Block>& bs) {
        std::vector<std::vector<std::string>> out;
        for (const auto& b : bs) {
            auto c = b.comments;
            std::sort(c.begin(), c.end());
            out.push_back(c);
        }
        return out;
    };
    std::string why;
    if (!y.errors.empty()) why = "relu avec une faute : " + errorsOf(y);
    else if (summary(x.decls) != summary(y.decls)) why = "declarations :\n" + summary(x.decls) + "  relues :\n" + summary(y.decls);
    else if (comments(x.blocks) != comments(y.blocks)) why = "les commentaires des blocs";
    else if (x.functions.size() != y.functions.size()) why = "les fonctions internes";
    else if (outside(code, x) != outside(rebuilt, y)) why = "le code hors des blocs";
    else
        for (std::size_t i = 0; i < x.functions.size() && why.empty(); ++i) {
            const auto& f = x.functions[i];
            const auto& g = y.functions[i];
            if (f.name != g.name || f.returnType != g.returnType || summary(f.decls) != summary(g.decls) || comments(f.blocks) != comments(g.blocks))
                why = "la fonction interne " + f.name;
        }
    check(why.empty(), where + " : chaque bloc recompose se relit a l'identique" + (why.empty() ? std::string{} : " - " + why));
    ++t.recomposed;
    t.blocks += static_cast<int>(edits.size());
}

// Coupe n'importe ou (un code en cours de frappe) : rien ne sort de ses bornes.
void prefixes(const std::string& code, const std::string& where) {
    std::string why;
    for (std::size_t n = 0; n <= code.size() && why.empty(); ++n) {
        const std::string part = code.substr(0, n);
        const auto x = dc::extract(part);
        const int lines = static_cast<int>(std::count(part.begin(), part.end(), '\n')) + 1;
        if (x.body.size() != part.size()) why = "le corps n'a pas la taille du code";
        for (const auto& b : x.blocks)
            if (b.begin > b.end || b.end > part.size() || b.firstLine < 1 || b.lastLine > lines || b.firstLine > b.lastLine)
                why = "un bloc hors du code";
        for (const auto& d : x.decls)
            if (d.line < 1 || d.line > lines || d.column < 1 || d.name.empty() || d.type.empty()) why = "une declaration hors du code";
        for (const auto& f : x.functions)
            if (f.begin > f.end || f.end > part.size()) why = "une fonction hors du code";
        for (const auto& e : x.errors)
            if (e.line < 1 || e.line > lines) why = "une faute hors du code";
        if (!why.empty()) why += " (coupe a " + std::to_string(n) + " octets)";
    }
    check(why.empty(), where + " : coupe n'importe ou, rien ne sort de ses bornes" + (why.empty() ? std::string{} : " - " + why));
}

// ---- L'arbre du projet : sa lecture d'avant la 1.11.18, figee ---------------------
namespace avant {
hmitree::Declared withoutDeclarations(std::string_view s) {
    using hmitree::trimmed;
    hmitree::Declared out;
    const auto up = [](std::string_view w) {
        std::string u(w);
        for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return u;
    };
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::size_t i = 0;
    while (i < s.size()) {
        if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') || (i > 0 && ident(s[i - 1]))) {
            if (s[i] == '\'') {
                const auto end = s.find('\'', i + 1);
                const std::size_t to = end == std::string_view::npos ? s.size() : end + 1;
                out.code.append(s.substr(i, to - i));
                i = to;
                continue;
            }
            out.code += s[i++];
            continue;
        }
        std::size_t k = i;
        while (k < s.size() && ident(s[k])) ++k;
        const std::string w = up(s.substr(i, k - i));
        if (w != "VAR" && w != "VAR_TEMP" && w != "VAR_INPUT") {
            out.code.append(s.substr(i, k - i));
            i = k;
            continue;
        }
        const bool input = w == "VAR_INPUT";
        const auto end = up(s).find("END_VAR", k);
        const std::string_view block = s.substr(k, (end == std::string::npos ? s.size() : end) - k);
        for (const char c : block) if (c == '\n') out.code += '\n';
        i = end == std::string::npos ? s.size() : end + 7;
        std::size_t start = 0;
        while (start < block.size()) {
            auto semi = block.find(';', start);
            if (semi == std::string_view::npos) semi = block.size();
            const std::string_view one = block.substr(start, semi - start);
            start = semi + 1;
            const auto colon = one.find(':');
            if (colon == std::string_view::npos) continue;
            std::string type(one.substr(colon + 1));
            if (const auto assign = type.find(":="); assign != std::string::npos) type.resize(assign);
            type = up(trimmed(type));
            std::size_t ns = 0;
            const std::string_view names = one.substr(0, colon);
            while (ns <= names.size()) {
                auto comma = names.find(',', ns);
                if (comma == std::string_view::npos) comma = names.size();
                std::string name = trimmed(names.substr(ns, comma - ns));
                if (const auto sp = name.find_last_of(" \t\n)"); sp != std::string::npos) name = name.substr(sp + 1);
                ns = comma + 1;
                if (name.empty()) continue;
                out.locals.insert(up(name));
                if (input) out.inputs.emplace_back(name, type);
            }
        }
    }
    return out;
}
} // namespace avant

// L'arbre (hmitree::withoutDeclarations) : les memes locales, parametres, variables
// et chaines que sa lecture d'avant, la ou celle-ci lisait juste (ni VAR_IN_OUT ni
// VAR_OUTPUT, ni fonction interne, ni chaine "...", ni faute).
void sameAsTree(const std::string& code, const std::string& where, int& compared) {
    const std::string s = hmitree::withoutComments(code);
    const auto x = dc::extract(s);
    const bool rich = std::any_of(x.blocks.begin(), x.blocks.end(), [](const dc::Block& b) {
        return b.section == dc::Section::InOut || b.section == dc::Section::Output;
    });
    if (!x.errors.empty() || !x.functions.empty() || rich || s.find('"') != std::string::npos) return;
    const auto a = avant::withoutDeclarations(s);
    const auto b = hmitree::withoutDeclarations(s);
    const auto strings = [](const std::string& c) {
        std::vector<std::string> out;
        for (const auto& t : dc::lex(c))
            if (t.kind == dc::TokenKind::String) out.emplace_back(t.text(c));
        return out;
    };
    std::string why;
    if (a.locals != b.locals) why = "les locales";
    else if (a.inputs != b.inputs) why = "les parametres";
    else if (hmikit::variablePaths(a.code) != hmikit::variablePaths(b.code)) why = "les variables du code";
    else if (strings(a.code) != strings(b.code)) why = "les chaines du code";
    check(why.empty(), where + " : l'arbre lit comme avant" + (why.empty() ? std::string{} : " - " + why));
    ++compared;
}

// Le corpus des essais : "@@ <n> <source>:<ligne> <octets>", puis les octets.
void corpusFile(const std::string& path, Tally& t) {
    std::ifstream in(path, std::ios::binary);
    check(static_cast<bool>(in), "le corpus s'ouvre : " + path);
    std::string head;
    int count = 0;
    while (std::getline(in, head)) {
        if (head.rfind("@@ ", 0) != 0) continue;
        std::istringstream h(head.substr(3));
        int n = 0;
        std::string where;
        std::size_t size = 0;
        h >> n >> where >> size;
        std::string code(size, '\0');
        in.read(code.data(), static_cast<std::streamsize>(size));
        in.get();                                               // sa fin de ligne
        ++count;
        const std::string label = "corpus " + std::to_string(n) + " (" + where + ")";
        sameAsSplit(code, label, t);
        roundTrip(code, label, t);
        prefixes(code, label);
        sameAsTree(code, label, t.tree);
    }
    check(count >= 150, "le corpus des essais : " + std::to_string(count) + " codes (150 au moins)");
}

// Un projet : chaque code ST (scripts, fonctions, scripts de vue, operateurs, redefinitions).
void corpusProject(const std::string& folder, Tally& t) {
    auto loaded = load(folder);
    check(loaded.has_value(), "le projet IHM s'ouvre : " + folder);
    if (!loaded.has_value()) return;
    int count = 0;
    forEachCode(static_cast<const Project&>(loaded.value()), [&](const std::string& text, CodeForm form, const CodeSite& site) {
        if (form != CodeForm::Code || text.find_first_not_of(" \t\r\n") == std::string::npos) return;
        ++count;
        const std::string label = folder + " : " + site.where;
        sameAsSplit(text, label, t);
        roundTrip(text, label, t);
        sameAsTree(text, label, t.tree);
    });
    check(count > 0, folder + " : " + std::to_string(count) + " codes ST");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : hmi_decl_test <corpus.txt> [<dossier de projet>...]\n");
        return 2;
    }
    std::printf("-- 1.11.18 (refonte des scripts, lot 2) : les declarations lues sans perte\n");
    lexer();
    formes();
    commentaires();
    fonctions();
    fautes();
    recomposer();
    signature();
    Tally t;
    std::printf("-- le corpus : %s\n", argv[1]);
    corpusFile(argv[1], t);
    for (int i = 2; i < argc; ++i) {
        std::printf("-- le projet : %s\n", argv[i]);
        corpusProject(argv[i], t);
    }
    std::printf("  %d codes ; %d compares a splitDeclarations (%zu declarations), %d ou il dit une faute ; %d recomposes (%d blocs) ;"
                " %d compares a l'arbre d'avant\n",
                t.codes, t.compared, t.decls, t.skipped, t.recomposed, t.blocks, t.tree);
    check(t.compared >= 100 && t.decls >= 150, "l'egalite porte sur 100 codes et 150 declarations au moins");
    check(t.tree >= 100, "l'arbre : compare sur 100 codes au moins");
    // La signature d'une fonction dans l'arbre : ses VAR_INPUT, comme avant.
    HmiFunction f;
    f.name = "Moyenne";
    f.returnType = "REAL";
    f.body = "VAR_INPUT\n  a, b : real; (* les deux *)\nEND_VAR\nVAR t : REAL; END_VAR\nMoyenne := (a + b) / 2;";
    check(hmitree::signatureOf(f) == "Moyenne(a : REAL, b : REAL) : REAL", "hmitree::signatureOf : " + hmitree::signatureOf(f));
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
