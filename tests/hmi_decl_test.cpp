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
//  1.11.18 (lot 3) : les declarations dans le modele - l'aller-retour disque des cinq
//  porteurs (format 23, seulement s'il y en a), les identifiants (absents, en double,
//  copies par une commande) ; le pont (les blocs reconstruits sur la ligne 1) : les
//  fautes a la meme ligne, la meme signature, la meme execution qu'avec des blocs ecrits.
//
//      hmi_decl_test <corpus.txt> [<dossier de projet>...]
// =============================================================================
#include "../src/app/hmi/HmiTreeData.hpp"     // hmitree::withoutDeclarations : relu par hmi::decl
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiDecl.hpp"
#include "../src/hmi/HmiCalc.hpp"        // 1.12.7 : les valeurs calculees au demarrage
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiDeclEdit.hpp"     // 1.11.18 (lot 5) : editer les declarations
#include "../src/hmi/HmiDesign.hpp"
#include "../src/hmi/HmiEnums.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPipeline.hpp"
#include "../src/hmi/HmiRetain.hpp"       // 1.11.18 (lot 5) : la remanence d'exploitation (Persistante)
#include "../src/hmi/HmiSimData.hpp"      // 1.11.18 (lot 5) : la remanence de simulation (Persistante)
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiScriptFile.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiSymbols.hpp"   // 1.12.16 : viewmembers

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace hmi;
namespace dc = hmi::decl;
namespace de = hmi::decledit;
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

// ---------------------------------------------------------- le modele (lot 3) --
Declaration declared(DeclKind kind, std::string name, std::string type, std::string value = {},
                     Storage storage = Storage::Execution, PassMode mode = PassMode::In) {
    Declaration d;
    d.kind = kind;
    d.name = std::move(name);
    d.type = std::move(type);
    d.value = std::move(value);
    d.storage = storage;
    d.mode = mode;
    return d;
}

// Un projet aux cinq porteurs : un script et une fonction generaux, l'operateur d'un type
// IHM, la fonction, le script et l'operateur d'un symbole, la redefinition d'une instance.
Project fivePlaces() {
    Project p;
    Script sc;
    sc.id = p.allocate();
    sc.name = "Compter";
    sc.event = "Cyclique";
    sc.body = "Compteur := Compteur + 1;\n";
    auto max = declared(DeclKind::Constant, "Max", "INT", "10");
    max.description = "la borne, \"entre guillemets\" ; et un point-virgule";
    sc.decls = {max, declared(DeclKind::Variable, "Compteur", "INT", "0", Storage::Kept),
                declared(DeclKind::Variable, "Tmp", "ARRAY[1..4] OF REAL", "[1.0, 2.0, 3.0, 4.0]"),
                declared(DeclKind::Variable, "Total", "DINT", "", Storage::Persistent)};
    sc.decls[1].visibility = Visibility::Private;
    p.programs.scripts.push_back(sc);
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Moyenne";
    f.returnType = "REAL";
    f.body = "Moyenne := (a + b) / 2.0;\n";
    f.decls = {declared(DeclKind::Parameter, "a", "REAL"), declared(DeclKind::Parameter, "b", "REAL", "0.5"),
               declared(DeclKind::Parameter, "v", "T_VEC", "", Storage::Execution, PassMode::InOut),
               declared(DeclKind::Parameter, "ok", "BOOL", "", Storage::Execution, PassMode::Out),
               declared(DeclKind::Variable, "t", "REAL")};
    p.programs.functions.push_back(f);
    HmiType ty;
    ty.id = p.allocate();
    ty.name = "T_VEC";
    ty.members.push_back({"x", "REAL", "", ""});
    HmiOperator plus;
    plus.id = p.allocate();
    plus.op = "+";
    plus.left = plus.right = plus.result = "T_VEC";
    plus.body = "ADD.x := A.x + B.x;\n";
    plus.decls = {declared(DeclKind::Variable, "k", "REAL", "1.0")};
    ty.operators.push_back(plus);
    p.programs.types.push_back(ty);
    View sym = makeView(p, "Vanne");
    sym.role = "symbole";
    HmiFunction o;
    o.id = p.allocate();
    o.name = "Ouvrir";
    o.isVirtual = true;
    o.body = "Ouvert := Pct > 0;\n";
    o.decls = {declared(DeclKind::Parameter, "Pct", "INT", "100")};
    sym.functions.push_back(o);
    Script open;
    open.id = p.allocate();
    open.name = "Vanne_OnOpen";
    open.event = "OnOpen";
    open.body = "n := n + 1;\n";
    open.decls = {declared(DeclKind::Variable, "n", "INT", "0", Storage::Kept)};
    sym.scripts.push_back(open);
    HmiOperator eq = plus;
    eq.id = p.allocate();
    eq.op = "=";
    eq.left = eq.right = "Vanne";
    eq.result = "BOOL";
    eq.decls = {declared(DeclKind::Constant, "Tolerance", "REAL", "0.01")};
    sym.operators.push_back(eq);
    p.views.push_back(sym);
    View use = makeView(p, "Synoptique");
    Object inst;
    inst.id = p.allocate();
    inst.kind = Kind::SymbolInstance;
    inst.name = "Vanne_1";
    inst.props.push_back({"symbol", "Vanne", ""});
    FunctionOverride fo;
    fo.function = "Ouvrir";
    fo.body = "Ouvert := FALSE;\n";
    fo.decls = {declared(DeclKind::Variable, "j", "INT")};
    inst.functionOverrides.push_back(fo);
    use.objects.push_back(inst);
    p.views.push_back(use);
    uniqueDeclarationIds(p);
    return p;
}

// Les declarations du projet, en texte comparable (porteur par porteur).
std::string declarationsOf(const Project& p) {
    std::string out;
    forEachDeclarations(p, [&](const std::vector<Declaration>& list) {
        out += "[";
        for (const auto& d : list)
            out += std::to_string(d.id) + " " + std::string(declKindKey(d.kind)) + " " + d.name + " : " + d.type + " := " + d.value + " ("
                 + std::string(storageKey(d.storage)) + ", " + std::string(passModeKey(d.mode)) + ", "
                 + std::string(visibilityKey(d.visibility)) + ") " + d.description + "; ";
        out += "]\n";
    });
    return out;
}

FileReader readerOf(const std::vector<ProjectFile>& files) {
    return [files](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path) {
                content.assign(f.data->begin(), f.data->end());
                return true;
            }
        return false;
    };
}

std::string fileText(const std::vector<ProjectFile>& files, std::string_view prefix) {
    std::string out;
    for (const auto& f : files)
        if (f.path.rfind(prefix, 0) == 0) out.append(f.data->begin(), f.data->end());
    return out;
}

void modele() {
    std::printf("-- lot 3 : les declarations dans le modele, sur disque (format 23)\n");
    const Project p = fivePlaces();
    std::size_t count = 0;
    std::set<Id> ids;
    forEachDeclarations(p, [&](const std::vector<Declaration>& list) {
        for (const auto& d : list) {
            ++count;
            ids.insert(d.id);
        }
    });
    check(count == 14 && ids.size() == 14 && !ids.count(kNoId), "quatorze declarations sur sept porteurs, chacune son identifiant");
    const auto files = serializeProject(p);
    const std::string index = fileText(files, "ihm.txt");
    const std::string views = fileText(files, "vues/");
    check(index.find("ihm format=23") != std::string::npos && index.find("(format 23)") != std::string::npos
              && views.find("(format 23)") != std::string::npos,
          "avec des declarations : l'index et les vues au format 23");
    check(index.find("declaration id=") != std::string::npos && index.find("genre=constante nom=\"Max\" type=\"INT\" valeur=\"10\"") != std::string::npos
              && index.find("genre=parametre nom=\"v\" type=\"T_VEC\" type_cle=\"ihm:" + std::to_string(p.hmiTypeByName("T_VEC")->id)
                            + "\" mode=entree_sortie") != std::string::npos   // 1.11.19 (lot 6) : la cle du type IHM
              && index.find("stockage=persistante") != std::string::npos && index.find("visibilite=privee") != std::string::npos,
          "les lignes declaration de l'index : genre, nom, type, valeur, mode, stockage, visibilite");
    check(views.find("fonction_symbole") < views.find("genre=parametre nom=\"Pct\"")
              && views.find("redefinition") < views.find("genre=variable nom=\"j\"")
              && views.find("operateur_symbole") < views.find("nom=\"Tolerance\""),
          "dans la vue : chaque declaration suit son porteur (fonction, redefinition, operateur)");
    const auto back = parseProject(readerOf(files));
    check(back.has_value(), "relu");
    if (back.has_value()) {
        check(declarationsOf(back.value()) == declarationsOf(p), "relu : les memes declarations, aux memes porteurs\n" + declarationsOf(back.value()));
        check(back.value().nextId > *ids.rbegin(), "le compteur d'identifiants passe au-dessus des declarations");
    }
    // Sans declaration : le format 22 (ou 21), lisible par la 1.11.17.
    Project plain = p;
    forEachDeclarations(plain, [](std::vector<Declaration>& list) { list.clear(); });
    const std::string plainIndex = fileText(serializeProject(plain), "ihm.txt");
    check(plainIndex.find("ihm format=21") != std::string::npos && plainIndex.find("declaration") == std::string::npos,
          "sans declaration du modele : le format 21, sans ligne declaration (la 1.11.17 l'ouvre)");
    // Un format plus recent (kFormatVersion + 1 ; 1.11.23 : 25, le 24 est celui des raccourcis) : refuse.
    {
        auto newer = files;
        std::string text = fileText(files, "ihm.txt");
        const auto at = text.find("ihm format=23");
        const std::string next = std::to_string(hmi::kFormatVersion + 1);
        text.replace(at, 13, "ihm format=" + next);
        for (auto& f : newer)
            if (f.path == "ihm.txt") f.data = std::make_shared<const Bytes>(text.begin(), text.end());
        const auto refused = parseProject(readerOf(newer));
        check(!refused.has_value() && refused.error().context.find(next) != std::string::npos, "un projet au format " + next + " : refuse, et dit pourquoi");
    }
    // A la main : une declaration sans porteur, sans nom ; deux identifiants egaux, un absent.
    {
        auto edited = files;
        std::string text = fileText(files, "ihm.txt");
        const auto first = text.rfind("declaration id=", text.find("nom=\"Max\""));   // la constante Max du script
        const auto eol = text.find('\n', first);
        std::string line = text.substr(first, eol - first);          // la constante Max
        std::string same = line;
        same.replace(same.find("nom=\"Max\""), 9, "nom=\"Max2\"");    // le meme identifiant
        std::string none = line;
        none.replace(none.find("id="), none.find(' ', none.find("id=")) - none.find("id="), "id=0");
        none.replace(none.find("nom=\"Max\""), 9, "nom=\"Max3\"");
        text.insert(eol + 1, same + "\n" + none + "\n");
        const auto head = text.find('\n', text.find("ihm format=")) + 1;
        text.insert(head, "declaration id=9999 genre=variable nom=\"Perdue\" type=\"INT\"\ndeclaration genre=variable type=\"INT\"\n");
        for (auto& f : edited)
            if (f.path == "ihm.txt") f.data = std::make_shared<const Bytes>(text.begin(), text.end());
        LoadReport report;
        const auto got = parseProject(readerOf(edited), &report);
        check(got.has_value(), "relu malgre les lignes editees a la main");
        if (got.has_value()) {
            const auto& d = got.value().programs.scripts.front().decls;
            std::set<Id> seen;
            forEachDeclarations(got.value(), [&](const std::vector<Declaration>& list) { for (const auto& x : list) seen.insert(x.id); });
            check(d.size() == 6 && d[1].name == "Max2" && d[2].name == "Max3" && d[1].id != d[0].id && d[2].id != kNoId
                      && seen.size() == 16 && !seen.count(kNoId),
                  "deux identifiants egaux, un absent : chacun en recoit un neuf");
            std::size_t ignored = 0;
            bool renewed = false;
            for (const auto& w : report.warnings) {
                if (w.find("d\xC3\xA9" "claration hors d'un script") != std::string::npos) ++ignored;
                if (w.find("identifiants absents ou en double") != std::string::npos) renewed = true;
            }
            check(ignored == 2 && renewed, "les lignes sans porteur : ignorees et dites ; les identifiants renouveles : dits");
        }
    }
    // Une commande qui copie un script : ses declarations recoivent des identifiants neufs ;
    // l'annuler rend le projet d'avant.
    {
        auto doc = std::make_shared<Document>();
        doc->project = p;
        core::CommandStack stack;
        auto c = changeProject(doc, "Dupliquer", [](Project& q) {
            Script copy = q.programs.scripts.front();
            copy.id = q.allocate();
            copy.name = "Compter_2";
            q.programs.scripts.push_back(copy);
        });
        check(c != nullptr && static_cast<bool>(stack.push(std::move(c))), "dupliquer un script : une commande");
        const auto& a = doc->project.programs.scripts[0].decls;
        const auto& b = doc->project.programs.scripts[1].decls;
        bool fresh = a.size() == b.size();
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) fresh = fresh && a[i].id != b[i].id && a[i].name == b[i].name;
        check(fresh, "la copie : les memes declarations, des identifiants neufs");
        (void)stack.undo();
        check(doc->project.programs.scripts.size() == 1 && declarationsOf(doc->project) == declarationsOf(p), "annule : le projet d'avant");
        // Une vue : une instance copiee (sa redefinition et sa variable).
        const Id use = doc->project.views[1].id;
        auto v = changeView(doc, use, "Copier", [&](Project& q, View& view) {
            Object copy = view.objects.front();
            copy.id = q.allocate();
            copy.name = "Vanne_2";
            view.objects.push_back(copy);
        });
        check(v != nullptr && static_cast<bool>(stack.push(std::move(v))), "copier une instance : une commande");
        const auto& objects = doc->project.view(use)->objects;
        check(objects.size() == 2 && objects[1].functionOverrides.front().decls.front().id != objects[0].functionOverrides.front().decls.front().id,
              "l'instance copiee : la variable de sa redefinition a un identifiant neuf");
    }
}

// ---------------------------------------------------------- le pont (lot 3) ----
class FakePlc final : public sim::Environment {
public:
    std::map<std::string, sim::Value> values;
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        it->second = v;
        return true;
    }
    bool exists(std::string_view n) override { return values.count(std::string(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

Variable hmiVariable(Project& p, std::string name, std::string type) {
    Variable v;
    v.id = p.allocate();
    v.name = std::move(name);
    v.type = std::move(type);
    return v;
}

void pont() {
    std::printf("-- lot 3 : le pont (les declarations reconstruites sur la ligne 1)\n");
    // La reconstruction.
    {
        std::vector<Declaration> d = {declared(DeclKind::Constant, "Max", "INT", "10"),
                                      declared(DeclKind::Variable, "Compteur", "INT", "0", Storage::Kept),
                                      declared(DeclKind::Variable, "Tmp", "REAL"),
                                      declared(DeclKind::Variable, "Total", "DINT", "", Storage::Persistent)};
        for (std::size_t i = 0; i < d.size(); ++i) d[i].id = static_cast<Id>(100 + i);
        const std::string body = "Compteur := Compteur + 1;\nTotal := Max;\n";
        const auto c = dc::composeCode(body, d, dc::Role::Script);
        const std::string head = "VAR CONSTANT Max : INT := 10; END_VAR VAR Compteur : INT := 0; END_VAR VAR_TEMP Tmp : REAL; END_VAR "
                                 "VAR Total : DINT; END_VAR ";
        check(c.text == head + body && c.prefix == head.size(), "un script : VAR CONSTANT, VAR (Conservee), VAR_TEMP (Execution), VAR (Persistante), sur la ligne 1\n" + c.text);
        check(std::count(c.text.begin(), c.text.end(), '\n') == std::count(body.begin(), body.end(), '\n'), "les lignes du corps ne bougent pas");
        check(c.inDeclarations(1, 5) && !c.inDeclarations(1, static_cast<int>(head.size()) + 1) && !c.inDeclarations(2, 3)
                  && c.bodyColumn(1, static_cast<int>(head.size()) + 1) == 1 && c.bodyColumn(2, 7) == 7,
              "les colonnes de la ligne 1 rendues au corps ; la ligne 2 telle quelle");
        check(c.declarationAt(1, static_cast<int>(head.find("Tmp")) + 1) == 102 && c.declarationAt(1, 1) == kNoId,
              "une colonne dans les declarations : la sienne");
        const auto plain = dc::composeCode(body, {}, dc::Role::Script);
        check(plain.text == body && plain.prefix == 0 && !plain.inDeclarations(1, 1), "sans declaration : le corps tel quel");
        auto multi = d;
        multi[0].value = "1\n+ 2";
        const std::string flatValue = dc::composeCode(body, multi, dc::Role::Script).text;
        check(std::count(flatValue.begin(), flatValue.end(), '\n') == 2 && flatValue.find("Max : INT := 1 + 2;") != std::string::npos,
              "une valeur sur deux lignes : remise sur une (les lignes du corps tiennent)");
    }
    // Une fonction : ses parametres dans leur ordre, par blocs.
    {
        HmiFunction f;
        f.name = "F";
        f.body = "F := a;\n";
        f.decls = {declared(DeclKind::Parameter, "a", "REAL"), declared(DeclKind::Parameter, "v", "T_VEC", "", Storage::Execution, PassMode::InOut),
                   declared(DeclKind::Parameter, "b", "REAL", "0.5"), declared(DeclKind::Variable, "t", "REAL", "", Storage::Kept)};
        check(dc::codeOf(f) == "VAR_INPUT a : REAL; END_VAR VAR_IN_OUT v : T_VEC; END_VAR VAR_INPUT b : REAL := 0.5; END_VAR VAR t : REAL; END_VAR F := a;\n",
              "une fonction : VAR_INPUT, VAR_IN_OUT, VAR_INPUT dans l'ordre des parametres ; ses variables : VAR\n" + dc::codeOf(f));
        FunctionOverride o;
        o.function = "F";
        o.body = "F := 2.0 * a;\n";
        o.decls = {declared(DeclKind::Variable, "k", "INT")};
        check(dc::codeOf(o, &f) == "VAR_INPUT a : REAL; END_VAR VAR_IN_OUT v : T_VEC; END_VAR VAR_INPUT b : REAL := 0.5; END_VAR VAR k : INT; END_VAR F := 2.0 * a;\n",
              "une redefinition : les parametres de sa fonction, puis ses locales\n" + dc::codeOf(o, &f));
        std::string storage;
        const HmiFunction none;
        check(&dc::codeOf(none, storage) == &none.body && &dc::codeOf(f, storage) == &storage, "sans declaration : le corps, sans copie");
    }
    // Les fautes des declarations elles-memes.
    {
        std::vector<Declaration> bad = {declared(DeclKind::Variable, "", "INT"), declared(DeclKind::Variable, "2x", "INT"),
                                        declared(DeclKind::Variable, "IF", "INT"), declared(DeclKind::Variable, "a", "INT"),
                                        declared(DeclKind::Variable, "A", "REAL"), declared(DeclKind::Variable, "n", ""),
                                        declared(DeclKind::Variable, "m", "BIDULE"), declared(DeclKind::Constant, "C", "INT"),
                                        declared(DeclKind::Parameter, "p", "INT"), declared(DeclKind::Variable, "Ok", "BOOL"),
                                        declared(DeclKind::Variable, "Bloc", "INT")};
        std::vector<Declaration> valid;
        const auto e = dc::checkDeclarations(bad, dc::Role::Script, "VAR\n  Bloc : INT;\nEND_VAR\nOk := TRUE;", {}, nullptr, &valid);
        std::string all;
        for (const auto& x : e) all += x.message + "\n";
        const auto says = [&](std::string_view what) { return all.find(what) != std::string::npos; };
        check(e.size() == 9 && says("d\xC3\xA9" "claration sans nom") && says("\xC2\xAB 2x \xC2\xBB : nom illisible")
                  && says("\xC2\xAB IF \xC2\xBB : nom r\xC3\xA9serv\xC3\xA9") && says("\xC2\xAB A \xC2\xBB : d\xC3\xA9" "clar\xC3\xA9" "e deux fois")
                  && says("\xC2\xAB n \xC2\xBB : type manquant") && says("\xC2\xAB m \xC2\xBB : type non pris en charge : BIDULE")
                  && says("\xC2\xAB C \xC2\xBB : une constante sans valeur") && says("\xC2\xAB p \xC2\xBB : un param\xC3\xA8tre dans un script")
                  && says("\xC2\xAB Bloc \xC2\xBB : d\xC3\xA9" "clar\xC3\xA9" "e deux fois (aussi dans un bloc VAR du code)"),
              "les fautes des declarations, chacune nommee (" + std::to_string(e.size()) + ")\n" + all);
        check(valid.size() == 2 && valid[0].name == "a" && valid[1].name == "Ok", "les justes : a et Ok");
        check(std::all_of(e.begin(), e.end(), [](const ScriptDiagnostic& x) { return x.line == 0; }), "ligne 0 : tout le code");
        const auto f = dc::checkDeclarations({declared(DeclKind::Variable, "k", "INT", "", Storage::Kept)}, dc::Role::Function, "");
        const auto o = dc::checkDeclarations({declared(DeclKind::Parameter, "x", "INT")}, dc::Role::Operator, "");
        const auto r = dc::checkDeclarations({declared(DeclKind::Parameter, "x", "INT")}, dc::Role::Function, "", {},
                                             &bad);
        check(f.size() == 1 && f[0].message.find("une fonction n'a pas de m\xC3\xA9moire") != std::string::npos
                  && o.size() == 1 && o[0].message.find("op\xC3\xA9rateur") != std::string::npos
                  && r.size() == 1 && r[0].message.find("red\xC3\xA9" "finition") != std::string::npos,
              "une fonction sans memoire ; un operateur et une redefinition sans parametre a eux");
    }
    // Les controles : un script a declarations du modele et le meme a blocs textuels.
    {
        Project p;
        Script text;
        text.name = "S";
        text.body = "VAR\n  Compteur : INT := 0;\nEND_VAR\nCompteur := Compteur + 1;\nx := ;\n";
        Script model;
        model.name = "S";
        model.body = "\n\n\nCompteur := Compteur + 1;\nx := ;\n";
        model.decls = {declared(DeclKind::Variable, "Compteur", "INT", "0", Storage::Kept)};
        const auto a = checkScript(text);
        const auto b = checkScript(model);
        check(!a.empty() && !b.empty() && a.front().line == 5 && b.front().line == 5 && a.front().message == b.front().message,
              "une faute de la ligne 5 : la meme, a la meme ligne (" + (b.empty() ? std::string("aucune") : b.front().message) + ")");
        Script badDecl = model;
        badDecl.decls.push_back(declared(DeclKind::Variable, "Ecart", "BIDULE"));
        const auto c = checkScript(badDecl);
        check(!c.empty() && c.front().line == 0 && c.front().message.find("Ecart") != std::string::npos
                  && std::any_of(c.begin(), c.end(), [](const ScriptDiagnostic& x) { return x.line == 5; }),
              "une declaration fautive : dite ligne 0 ; le code reste controle (sa faute ligne 5)");
        Script empty;
        empty.decls = model.decls;
        const auto v = checkScript(empty);
        check(v.size() == 1 && v[0].message == "script vide", "des declarations sans code : script vide");
    }
    // Une fonction : signature, texte, controle, interface du build.
    {
        HmiFunction text;
        text.name = "Moyenne";
        text.returnType = "REAL";
        text.body = "VAR_INPUT\n  a : REAL;\n  b : REAL := 0.5;\nEND_VAR\nMoyenne := (a + b) / 2.0;\n";
        HmiFunction model = text;
        model.body = "\n\n\n\nMoyenne := (a + b) / 2.0;\n";
        model.decls = {declared(DeclKind::Parameter, "a", "REAL"), declared(DeclKind::Parameter, "b", "REAL", "0.5")};
        check(functionSignature(model) == functionSignature(text) && functionSignature(model) == "Moyenne(a : REAL, b : REAL) : REAL",
              "la signature : la meme (" + functionSignature(model) + ")");
        check(checkFunction(model).empty() && checkFunction(text).empty(), "le controle : aucune faute, des deux cotes");
        check(functionText(model).rfind("FUNCTION Moyenne : REAL VAR_INPUT a : REAL; b : REAL := 0.5; END_VAR \n", 0) == 0,
              "le texte de la fonction : ses parametres sur la ligne de FUNCTION");
        check(hmi::pipeline::signatureOf(dc::codeOf(model)) == hmi::pipeline::signatureOf(text.body), "l'interface du build : la meme");
        HmiFunction self = model;
        self.decls.push_back(declared(DeclKind::Variable, "Moyenne", "REAL"));
        const auto s = checkFunction(self);
        check(s.size() == 1 && s[0].line == 0 && s[0].message.find("nom de la fonction") != std::string::npos,
              "une declaration du nom de la fonction : dite une fois, ligne 0");
    }
    // La meme execution : un script et une fonction a declarations du modele, et les memes a blocs textuels.
    {
        const auto run = [](bool modeled) {
            Project p;
            p.programs.variables.push_back(hmiVariable(p, "Total", "INT"));
            p.programs.variables.push_back(hmiVariable(p, "Fois", "INT"));
            p.programs.variables.push_back(hmiVariable(p, "Moy", "REAL"));
            p.programs.variables.push_back(hmiVariable(p, "Borne", "INT"));
            HmiFunction f;
            f.id = p.allocate();
            f.name = "Moyenne";
            f.returnType = "REAL";
            Script s;
            s.id = p.allocate();
            s.name = "Compter";
            s.event = "Appel";
            if (modeled) {
                f.body = "Moyenne := (a + b) / 2.0;\n";
                f.decls = {declared(DeclKind::Parameter, "a", "REAL"), declared(DeclKind::Parameter, "b", "REAL", "1.0")};
                s.body = "Compteur := Compteur + 1;\nTmp := Tmp + 1;\nTotal := Compteur;\nFois := Tmp;\nMoy := Moyenne(5.0);\nBorne := Max;\n";
                s.decls = {declared(DeclKind::Constant, "Max", "INT", "7"), declared(DeclKind::Variable, "Compteur", "INT", "0", Storage::Kept),
                           declared(DeclKind::Variable, "Tmp", "INT", "10")};
            } else {
                f.body = "VAR_INPUT\n  a : REAL;\n  b : REAL := 1.0;\nEND_VAR\nMoyenne := (a + b) / 2.0;\n";
                s.body = "VAR CONSTANT Max : INT := 7; END_VAR VAR Compteur : INT := 0; END_VAR VAR_TEMP Tmp : INT := 10; END_VAR "
                         "Compteur := Compteur + 1;\nTmp := Tmp + 1;\nTotal := Compteur;\nFois := Tmp;\nMoy := Moyenne(5.0);\nBorne := Max;\n";
            }
            p.programs.functions.push_back(f);
            p.programs.scripts.push_back(s);
            uniqueDeclarationIds(p);
            FakePlc plc;
            Runtime rt;
            rt.bind(&p, &plc);
            rt.start(0.0);
            std::string out, why;
            for (int k = 0; k < 3; ++k) {
                const bool ok = rt.callScript("Compter", 0.1 * k, &why);
                out += (ok ? "ok" : "erreur " + why) + " Total=" + rt.variable("Total")->display() + " Fois=" + rt.variable("Fois")->display()
                     + " Moy=" + rt.variable("Moy")->display() + " Borne=" + rt.variable("Borne")->display() + "\n";
            }
            return out;
        };
        const std::string text = run(false), model = run(true);
        check(model == text && text.find("Total=3 Fois=11 Moy=3") != std::string::npos && text.find("Borne=7") != std::string::npos,
              "trois appels : la meme execution (Conservee gardee, Execution remise, defaut d'un parametre, constante)\n" + text + model);
    }
    // Un appel de la fonction depuis une expression de vue : l'arite lue du modele.
    {
        Project p;
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Double";
        f.returnType = "REAL";
        f.body = "Double := 2.0 * x;\n";
        f.decls = {declared(DeclKind::Parameter, "x", "REAL")};
        p.programs.functions.push_back(f);
        uniqueDeclarationIds(p);
        hmi::scriptcheck::Scope scope;
        scope.project = &p;
        const auto ok = hmi::scriptcheck::check(scope, "y := Double(1.0);");
        const auto bad = hmi::scriptcheck::check(scope, "y := Double(1.0, 2.0);");
        check(std::none_of(ok.begin(), ok.end(), [](const auto& x) { return x.message.find("argument") != std::string::npos; })
                  && std::any_of(bad.begin(), bad.end(), [](const auto& x) { return x.message.find("argument") != std::string::npos; }),
              "un appel : ses arguments comptes d'apres les parametres du modele");
        // Une faute de la ligne 1 du corps : a sa colonne dans le corps (pas dans le texte reconstruit).
        scope.plcKnown = [](std::string_view) { return false; };
        const auto v = hmi::scriptcheck::check(scope, "y := Inconnu_Total + Ref;\n", {declared(DeclKind::Variable, "Ref", "INT", "1")},
                                               dc::Role::Script);
        check(std::any_of(v.begin(), v.end(), [](const auto& x) { return x.line == 1 && x.column == 6 && x.message.find("Inconnu_Total") != std::string::npos; })
                  && std::none_of(v.begin(), v.end(), [](const auto& x) { return x.message.find("Ref") != std::string::npos; }),
              "une faute de la ligne 1 : sa colonne dans le corps (6) ; la variable du modele est connue");
    }
}

// ---------------------------------------------------------- le suivi (lot 3) ----
void suivi() {
    std::printf("-- lot 3 : les declarations suivent (fichier .xpgst, renommages, rechercher)\n");
    // Le fichier .xpgst d'une vue : un bloc (*# declaration ... *) par declaration (decision D4).
    {
        Project p;
        View v = makeView(p, "Pompe");
        Script s;
        s.id = p.allocate();
        s.name = "Pompe.OnCycle";
        s.event = "OnCycle";
        s.body = "Tours := Tours + 1;\n";
        auto tours = declared(DeclKind::Variable, "Tours", "DINT", "0", Storage::Kept);
        tours.description = "le compte des cycles, \"entre guillemets\" *) et une fin de commentaire";
        tours.visibility = Visibility::Private;
        s.decls = {declared(DeclKind::Constant, "Max", "INT", "100"), tours};
        v.scripts.push_back(s);
        const auto file = hmi::scriptfile::fromView(v);
        const std::string text = hmi::scriptfile::write(file);
        check(text.find("format=2") != std::string::npos && text.find("(*# declaration genre=constante nom=Max type=INT valeur=100 visibilite=publique *)") != std::string::npos
                  && text.find("stockage=conservee visibilite=privee") != std::string::npos && text.find("VAR") == std::string::npos,
              "le fichier : le format 2, une ligne (*# declaration *) par declaration, aucun VAR dans le code\n" + text);
        hmi::scriptfile::File back;
        std::string why;
        check(hmi::scriptfile::read(text, back, &why) && back.entries.size() == 1 && back.entries[0].decls.size() == 2
                  && back.entries[0].decls[1].description == tours.description && back.entries[0].decls[1].storage == Storage::Kept
                  && back.entries[0].decls[1].visibility == Visibility::Private && back.entries[0].decls[0].value == "100",
              "relu : les memes declarations (description, stockage, visibilite) - " + why);
        View target = makeView(p, "Autre");
        check(hmi::scriptfile::applyToView(p, target, back, {}, hmi::scriptfile::Mode::Replace) == 1 && target.scripts.size() == 1
                  && target.scripts[0].decls.size() == 2 && target.scripts[0].decls[1].name == "Tours",
              "importe dans une autre vue : le script et ses declarations");
        View plain = v;
        plain.scripts[0].decls.clear();
        check(hmi::scriptfile::write(hmi::scriptfile::fromView(plain)).find("format=1") != std::string::npos,
              "sans declaration : le format 1 (la 1.11.17 le lit)");
        hmi::scriptfile::File stray;
        check(!hmi::scriptfile::read("(*# xpgst format=2 genre=scripts-vue *)\n(*# declaration genre=variable nom=X type=INT *)\n", stray, &why)
                  && why.find("hors d'un script") != std::string::npos,
              "une declaration hors d'un script : refusee, et dit pourquoi");
    }
    // Renommer une fonction, une valeur d'enumeration : les valeurs des declarations suivent.
    {
        Project p;
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Seuil";
        f.returnType = "INT";
        f.body = "Seuil := 10;\n";
        p.programs.functions.push_back(f);
        HmiType mode;
        mode.id = p.allocate();
        mode.name = "T_MODE";
        mode.kind = HmiTypeKind::Enumeration;
        mode.values = {{"Auto", 0, "", ""}, {"Manu", 1, "", ""}};
        p.programs.types.push_back(mode);
        Script s;
        s.id = p.allocate();
        s.name = "Regler";
        s.body = "Limite := Limite + 1;\n";
        s.decls = {declared(DeclKind::Variable, "Limite", "INT", "Seuil() + 1", Storage::Kept),
                   declared(DeclKind::Variable, "Etat", "T_MODE", "T_MODE#Auto")};
        p.programs.scripts.push_back(s);
        uniqueDeclarationIds(p);
        const auto callers = functionCallers(p, "Seuil");
        check(std::any_of(callers.begin(), callers.end(), [](const std::string& c) { return c.find("Regler") != std::string::npos; }),
              "Appelee par : le script dont une valeur initiale appelle la fonction");
        check(renameFunctionEverywhere(p, "Seuil", "Seuil_Haut") > 0 && p.programs.scripts[0].decls[0].value == "Seuil_Haut() + 1",
              "renommer la fonction : la valeur initiale suit (" + p.programs.scripts[0].decls[0].value + ")");
        check(renameEnumValue(p, "T_MODE", "Auto", "Automatique") > 0 && p.programs.scripts[0].decls[1].value == "T_MODE#Automatique",
              "renommer une valeur d'enumeration : la valeur initiale suit (" + p.programs.scripts[0].decls[1].value + ")");
        // Rechercher et remplacer : le type et la valeur d'une declaration aussi.
        hmi::design::FindOptions o;
        o.wholeWord = true;
        check(hmi::design::replaceAll(p, "T_MODE", "T_ETAT", o) >= 2 && p.programs.scripts[0].decls[1].type == "T_ETAT"
                  && p.programs.scripts[0].decls[1].value == "T_ETAT#Automatique",
              "remplacer partout : le type et la valeur de la declaration");
    }
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
        mine.push_back(&d);       // 1.11.20 : splitDeclarations lit aussi VAR_IN_OUT et VAR_OUTPUT
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
                              || (o.section == LocalVar::Section::Input && d.section == dc::Section::Input)
                              || (o.section == LocalVar::Section::InOut && d.section == dc::Section::InOut)
                              || (o.section == LocalVar::Section::Output && d.section == dc::Section::Output);
            const bool type = localTypeSupported(d.type) ? o.type == upperOf(d.type) : flat(o.type) == d.type;
            if (o.name != d.name || !section || !type || flat(o.initial) != d.initial || o.line != d.line)
                why = "\xC2\xAB " + o.name + " : " + o.type + " := " + o.initial + " \xC2\xBB ligne " + std::to_string(o.line) + " / \xC2\xAB "
                    + d.name + " : " + d.type + " := " + d.initial + " \xC2\xBB ligne " + std::to_string(d.line);
        }
    // 1.11.20 : splitDeclarations blanchit aussi VAR_IN_OUT et VAR_OUTPUT : les corps sont les memes.
    if (why.empty() && old.body != x.body) why = "le corps differe";
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

const Declaration* declNamed(const std::vector<Declaration>& list, std::string_view name) {
    for (const auto& d : list)
        if (d.name == name) return &d;
    return nullptr;
}

void edition() {
    std::printf("-- lot 5 : editer les declarations (hmi::decledit)\n");
    using K = de::Place::Kind;
    // Les sept porteurs : chacun se retrouve, avec son role et ses onglets.
    {
        Project p = fivePlaces();
        const View* sym = nullptr;
        const View* use = nullptr;
        for (const auto& v : p.views) (v.name == "Vanne" ? sym : use) = &v;
        const de::Place script{K::Script, kNoId, kNoId, kNoId, p.programs.scripts[0].id, {}, {}};
        const de::Place fn{K::Function, kNoId, kNoId, kNoId, p.programs.functions[0].id, {}, {}};
        const de::Place op{K::TypeOperator, kNoId, kNoId, p.programs.types[0].id, p.programs.types[0].operators[0].id, {}, {}};
        const de::Place sfn{K::SymbolFunction, sym->id, kNoId, kNoId, sym->functions[0].id, {}, {}};
        const de::Place vs{K::ViewScript, sym->id, kNoId, kNoId, sym->scripts[0].id, {}, {}};
        const de::Place sop{K::SymbolOperator, sym->id, kNoId, kNoId, sym->operators[0].id, {}, {}};
        const de::Place ov{K::Override, use->id, use->objects[0].id, kNoId, kNoId, "Ouvrir", {}};
        const auto tabs = [&](const de::Place& at) {
            std::string out;
            for (const auto t : de::tabsOf(de::locate(p, at))) out += de::tabLabel(t, de::locate(p, at).role) + " ";
            return out;
        };
        check(tabs(script) == "Constantes Variables " && tabs(vs) == "Constantes Variables ", "un script : Constantes, Variables (" + tabs(script) + ")");
        check(tabs(fn) == "Param\xC3\xA8tres Locales Constantes " && tabs(sfn) == tabs(fn), "une fonction : Parametres, Locales, Constantes (" + tabs(fn) + ")");
        check(tabs(op) == "Locales Constantes " && tabs(sop) == tabs(op), "un operateur : Locales, Constantes (" + tabs(op) + ")");
        check(tabs(ov) == "Locales Constantes " && de::locate(p, ov).inherited && de::locate(p, ov).inherited->size() == 1,
              "une redefinition : Locales, Constantes ; les parametres de sa fonction (" + tabs(ov) + ")");
        check(!de::locate(p, de::Place{K::Script, kNoId, kNoId, kNoId, 999999, {}, {}}).valid(), "un code introuvable : invalide");
        Script c;
        c.id = p.allocate();
        c.name = "EnC";
        c.lang = ScriptLang::C;
        p.programs.scripts.push_back(c);
        const de::Place cs{K::Script, kNoId, kNoId, kNoId, c.id, {}, {}};
        std::string why;
        check(de::tabsOf(de::locate(p, cs)).empty() && !de::add(p, cs, de::Tab::Variables, -1, {}, nullptr, &why)
                  && why.find("C ou C++") != std::string::npos,
              "un script C : aucun onglet, rien a declarer (" + why + ")");

        // AJOUTER : un nom libre, les valeurs par defaut, la visibilite du porteur.
        Id made = kNoId;
        check(de::add(p, script, de::Tab::Constants, -1, {}, &made, &why) && made != kNoId, "ajouter une constante au script");
        const auto* k1 = declNamed(p.programs.scripts[0].decls, "Constante");
        check(k1 && k1->id == made && k1->type == "REAL" && k1->value == "0.0" && k1->visibility == Visibility::Public && k1->kind == DeclKind::Constant,
              "la constante neuve : Constante REAL 0.0 Public");
        check(de::rowsOf(p.programs.scripts[0].decls, de::Tab::Constants).size() == 2
                  && p.programs.scripts[0].decls[1].name == "Constante",
              "... rangee apres la derniere constante (les genres restent groupes)");
        check(de::add(p, script, de::Tab::Constants, -1, {}, nullptr, &why) && declNamed(p.programs.scripts[0].decls, "Constante2"),
              "la suivante : Constante2");
        check(!de::add(p, script, de::Tab::Constants, -1, "max", nullptr, &why) && why.find("d\xC3\xA9j\xC3\xA0") != std::string::npos,
              "un nom pris (sans casse) : refuse (" + why + ")");
        check(!de::add(p, script, de::Tab::Variables, -1, "IF", nullptr, &why) && why.find("r\xC3\xA9serv") != std::string::npos,
              "un mot reserve : refuse (" + why + ")");
        check(!de::add(p, script, de::Tab::Variables, -1, "2x", nullptr, &why) && why.find("illisible") != std::string::npos,
              "un nom illisible : refuse (" + why + ")");
        check(!de::add(p, script, de::Tab::Parameters, -1, {}, nullptr, &why) && why.find("param\xC3\xA8tre") != std::string::npos,
              "un parametre dans un script : refuse (" + why + ")");
        check(!de::add(p, ov, de::Tab::Parameters, -1, {}, nullptr, &why), "un parametre dans une redefinition : refuse");
        check(!de::add(p, ov, de::Tab::Variables, -1, "Pct", nullptr, &why) && why.find("red\xC3\xA9" "finie") != std::string::npos,
              "une locale de redefinition au nom d'un parametre de sa fonction : refusee (" + why + ")");
        check(!de::add(p, fn, de::Tab::Variables, -1, "moyenne", nullptr, &why) && why.find("nom de la fonction") != std::string::npos,
              "une locale au nom de la fonction : refusee (" + why + ")");
        check(!de::add(p, op, de::Tab::Variables, -1, "A", nullptr, &why) && !de::add(p, op, de::Tab::Constants, -1, "Resultat", nullptr, &why)
                  && !de::add(p, op, de::Tab::Constants, -1, "ADD", nullptr, &why),
              "un operateur : a, b, Resultat et ADD sont les siens");
        check(de::add(p, fn, de::Tab::Variables, -1, {}, &made, &why), "ajouter une locale a la fonction");
        const auto* loc = declNamed(p.programs.functions[0].decls, "Locale");
        check(loc && loc->visibility == Visibility::Private && loc->storage == Storage::Execution && loc->type == "INT",
              "la locale neuve : Locale INT Execution, Privee (une locale reste locale)");
        check(de::add(p, fn, de::Tab::Parameters, 0, {}, &made, &why) && p.programs.functions[0].decls[1].name == "Parametre",
              "un parametre apres la premiere ligne : a la deuxieme place");
        // Un bloc VAR encore dans le code : son nom est pris.
        p.programs.scripts[0].body = "VAR Vieux : INT; END_VAR\nCompteur := Compteur + 1;\n";
        check(!de::add(p, script, de::Tab::Variables, -1, "Vieux", nullptr, &why) && why.find("bloc VAR") != std::string::npos,
              "un nom encore declare dans un bloc VAR du code : refuse (" + why + ")");
        p.programs.scripts[0].body = "Compteur := Compteur + 1;\n";
    }
    // RENOMMER : le code suit (ni commentaire, ni chaine, ni membre, ni argument nomme).
    {
        Project p;
        Script s;
        s.id = p.allocate();
        s.name = "Calcul";
        s.body = "Compteur := Compteur + 1; (* Compteur *)\nMsg := 'Compteur';\nx.Compteur := Compteur;\nF(Compteur := 2);\nG(Compteur => y);\n"
                 "Mode := E_Mode#Compteur;\n";
        s.decls = {declared(DeclKind::Variable, "Compteur", "INT", "0", Storage::Kept), declared(DeclKind::Constant, "Pas", "INT", "Compteur * 2"),
                   declared(DeclKind::Variable, "Libre", "INT")};
        p.programs.scripts.push_back(s);
        uniqueDeclarationIds(p);
        const de::Place at{K::Script, kNoId, kNoId, kNoId, s.id, {}, {}};
        const auto uses = de::usesIn(s.body, "compteur");
        check(uses.size() == 3 && uses[0].line == 1 && uses[0].column == 1 && uses[1].column == 13 && uses[2].line == 3 && uses[2].column == 15,
              "les utilisations : 3 (1:1, 1:13, 3:15) - ni le commentaire, ni la chaine, ni le membre, ni les arguments nommes, ni E_Mode#Compteur");
        check(de::usageCount(de::locate(p, at), 0) == 4, "le compte : 3 dans le code, 1 dans la valeur d'une autre declaration");
        std::string why;
        check(de::set(p, at, de::Tab::Variables, 0, de::Column::Name, " Total ", &why), "renommer Compteur en Total : " + why);
        const auto& b = p.programs.scripts[0].body;
        check(b == "Total := Total + 1; (* Compteur *)\nMsg := 'Compteur';\nx.Compteur := Total;\nF(Compteur := 2);\nG(Compteur => y);\n"
                   "Mode := E_Mode#Compteur;\n",
              "le code suit, le reste ne bouge pas :\n" + b);
        check(p.programs.scripts[0].decls[1].value == "Total * 2" && p.programs.scripts[0].decls[0].name == "Total",
              "la valeur d'une autre declaration suit");
        // Un nom que le code emploie deja pour autre chose : la declaration employee ne le prend pas.
        p.programs.scripts[0].body += "Niveau := Total;\n";
        check(!de::set(p, at, de::Tab::Variables, 0, de::Column::Name, "Niveau", &why) && why.find("confondrait") != std::string::npos,
              "renommer vers un nom deja employe : refuse (" + why + ")");
        check(de::set(p, at, de::Tab::Variables, 1, de::Column::Name, "Niveau", &why) && p.programs.scripts[0].decls[2].name == "Niveau",
              "une declaration jamais employee peut le prendre (c'est la declarer) : " + why);
        check(de::set(p, at, de::Tab::Variables, 0, de::Column::Name, "TOTAL", &why) && p.programs.scripts[0].body.find("TOTAL := TOTAL + 1;") == 0,
              "changer la casse : le code suit");
        // Les trous d'IHM_JOURNAL et d'IHM_LOG lisent les variables du code : ils suivent aussi.
        p.programs.scripts[0].body = "TOTAL := TOTAL + 1;\nIHM_JOURNAL('total {TOTAL} ({TOTAL:0.0} ; {{TOTAL}} ; {x.TOTAL})');\n"
                                     "IHM_LOG(INFO, 'fois : {TOTAL * 2}');\nMsg := '{TOTAL}';\n";
        const auto holes = de::usesIn(p.programs.scripts[0].body, "Total");
        check(holes.size() == 5 && holes[2].line == 2 && holes[2].column == 21 && holes[3].column == 30 && holes[4].line == 3,
              "les utilisations : le code (2) et les trous de IHM_JOURNAL (2) et d'IHM_LOG (1) - ni {{...}}, ni un membre, ni une autre chaine ("
                  + std::to_string(holes.size()) + ")");
        check(de::set(p, at, de::Tab::Variables, 0, de::Column::Name, "Cumul", &why)
                  && p.programs.scripts[0].body == "Cumul := Cumul + 1;\nIHM_JOURNAL('total {Cumul} ({Cumul:0.0} ; {{TOTAL}} ; {x.TOTAL})');\n"
                                                    "IHM_LOG(INFO, 'fois : {Cumul * 2}');\nMsg := '{TOTAL}';\n",
              "renommer : les trous suivent, le reste ne bouge pas\n" + p.programs.scripts[0].body);
    }
    // Le parametre d'une fonction de symbole : ses redefinitions le lisent aussi.
    {
        Project p = fivePlaces();
        for (auto& v : p.views)
            for (auto& o : v.objects)
                for (auto& fo : o.functionOverrides) fo.body = "Ouvert := Pct > 50;\n";
        const View* sym = nullptr;
        for (const auto& v : p.views)
            if (v.name == "Vanne") sym = &v;
        const de::Place sfn{K::SymbolFunction, sym->id, kNoId, kNoId, sym->functions[0].id, {}, {}};
        std::string why;
        check(de::set(p, sfn, de::Tab::Parameters, 0, de::Column::Name, "Pourcent", &why), "renommer le parametre Pct : " + why);
        std::string bodies;
        for (const auto& v : p.views) {
            for (const auto& f : v.functions) bodies += f.body;
            for (const auto& o : v.objects)
                for (const auto& fo : o.functionOverrides) bodies += fo.body;
        }
        check(bodies == "Ouvert := Pourcent > 0;\nOuvert := Pourcent > 50;\n", "la fonction et sa redefinition suivent :\n" + bodies);
    }
    // LES CASES : le type, le stockage, le mode, la visibilite, la valeur, la documentation.
    {
        Project p = fivePlaces();
        const de::Place script{K::Script, kNoId, kNoId, kNoId, p.programs.scripts[0].id, {}, {}};
        const de::Place fn{K::Function, kNoId, kNoId, kNoId, p.programs.functions[0].id, {}, {}};
        auto& sd = p.programs.scripts[0].decls;
        std::string why;
        check(de::set(p, script, de::Tab::Variables, 0, de::Column::Type, "dint", &why) && sd[1].type == "DINT", "le type : dint -> DINT");
        check(de::set(p, script, de::Tab::Variables, 0, de::Column::Type, "t_vec", &why) && sd[1].type == "T_VEC", "un type IHM : t_vec -> T_VEC");
        check(de::set(p, script, de::Tab::Variables, 0, de::Column::Type, "ARRAY[1..3] OF INT", &why) && sd[1].type == "ARRAY[1..3] OF INT",
              "un tableau : accepte");
        check(!de::set(p, script, de::Tab::Variables, 0, de::Column::Type, "BIDULE", &why) && sd[1].type == "ARRAY[1..3] OF INT"
                  && why.find("BIDULE") != std::string::npos,
              "un type inconnu : refuse, la case ne bouge pas (" + why + ")");
        check(de::set(p, script, de::Tab::Variables, 0, de::Column::Storage, "VAR_TEMP", &why) && sd[1].storage == Storage::Execution
                  && de::set(p, script, de::Tab::Variables, 0, de::Column::Storage, "retain", &why) && sd[1].storage == Storage::Kept
                  && de::set(p, script, de::Tab::Variables, 0, de::Column::Storage, "PERSISTANTE", &why) && sd[1].storage == Storage::Persistent
                  && de::set(p, script, de::Tab::Variables, 0, de::Column::Storage, "Ex\xC3\xA9" "cution", &why) && sd[1].storage == Storage::Execution,
              "le stockage : VAR_TEMP, retain, PERSISTANTE, Execution (accentue)");
        check(!de::set(p, script, de::Tab::Variables, 0, de::Column::Storage, "parfois", &why), "un stockage inconnu : refuse");
        check(!de::set(p, fn, de::Tab::Variables, 0, de::Column::Storage, "Conserv\xC3\xA9" "e", &why) && why.find("m\xC3\xA9moire") != std::string::npos,
              "une locale de fonction Conservee : refusee (" + why + ")");
        auto& fd = p.programs.functions[0].decls;
        check(de::set(p, fn, de::Tab::Parameters, 0, de::Column::Mode, "IN_OUT", &why) && fd[0].mode == PassMode::InOut
                  && de::set(p, fn, de::Tab::Parameters, 0, de::Column::Mode, "E/S", &why) && fd[0].mode == PassMode::InOut
                  && de::set(p, fn, de::Tab::Parameters, 0, de::Column::Mode, "sortie", &why) && fd[0].mode == PassMode::Out
                  && de::set(p, fn, de::Tab::Parameters, 0, de::Column::Mode, "Entr\xC3\xA9" "e", &why) && fd[0].mode == PassMode::In,
              "le mode : IN_OUT, E/S, sortie, Entree");
        check(!de::set(p, script, de::Tab::Variables, 0, de::Column::Mode, "IN", &why), "un mode sur une variable : refuse");
        check(de::set(p, script, de::Tab::Constants, 0, de::Column::Visibility, "priv\xC3\xA9" "e", &why) && sd[0].visibility == Visibility::Private
                  && de::set(p, script, de::Tab::Constants, 0, de::Column::Visibility, "Public", &why) && sd[0].visibility == Visibility::Public,
              "la visibilite : privee, Public");
        check(de::add(p, fn, de::Tab::Variables, -1, {}, nullptr, &why) && !de::set(p, fn, de::Tab::Variables, 1, de::Column::Visibility, "Public", &why)
                  && why.find("locale") != std::string::npos,
              "une locale de fonction (privee) rendue publique : refusee (" + why + ")");
        check(!de::set(p, script, de::Tab::Constants, 0, de::Column::Value, "  ", &why) && sd[0].value == "10", "une constante sans valeur : refusee");
        check(de::set(p, script, de::Tab::Variables, 1, de::Column::Value, "", &why) && sd[2].value.empty(), "une variable sans valeur initiale : permise");
        // Une valeur venue d'Excel (en francais) : la virgule decimale, les espaces de milliers, VRAI / FAUX.
        check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "REAL", &why) && de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "2,5", &why)
                  && sd[3].value == "2.5",
              "2,5 -> 2.5 (" + sd[3].value + ")");
        check(de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "-1 234,75", &why) && sd[3].value == "-1234.75", "-1 234,75 -> -1234.75 (" + sd[3].value + ")");
        check(de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "Max * 0.5", &why) && sd[3].value == "Max * 0.5", "une expression reste telle quelle");
        check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "BOOL", &why) && de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "Vrai", &why)
                  && sd[3].value == "TRUE" && de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "faux", &why) && sd[3].value == "FALSE",
              "BOOL : Vrai -> TRUE, faux -> FALSE");
        check(!de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "1 +* 2", &why) && sd[3].value == "FALSE" && why.find("illisible") != std::string::npos,
              "une valeur illisible : refusee, la case ne bouge pas (" + why + ")");
        // Un tableau : une seule valeur remplit toutes ses cases ; 1.12.2 : une liste [..] se lit (le
        // moteur la range case par case) ; une liste illisible est refusee (elle rendrait tout le code
        // illisible a l'execution).
        const bool one = de::set(p, script, de::Tab::Variables, 1, de::Column::Value, "1.5", &why);
        check(one && sd[2].value == "1.5", "un tableau : une valeur pour toutes ses cases - " + why);
        const bool list = de::set(p, script, de::Tab::Variables, 1, de::Column::Value, "[1.0, 2.0, 3.0, 4.0]", &why);
        check(list && sd[2].value == "[1.0, 2.0, 3.0, 4.0]", "1.12.2 : une liste [..] : prise - " + why);
        const bool broken = de::set(p, script, de::Tab::Variables, 1, de::Column::Value, "[1.0, 2.0", &why);
        check(!broken && sd[2].value == "[1.0, 2.0, 3.0, 4.0]" && why.find("illisible") != std::string::npos, "une liste sans ] : refusee - " + why);
        check(de::set(p, script, de::Tab::Variables, 1, de::Column::Value, "1.5", &why) && sd[2].value == "1.5", "retour a 1.5");
        // 1.11.21 : LE TYPE CHANGE, LA VALEUR QUI NE LUI CONVIENT PLUS EST RETIREE (la session de la
        // 1.11.19 : 0 garde pour un ARRAY[1..4] OF T_Four) ; une valeur qui ne convient pas : refusee.
        {
            std::string note;
            check(sd[3].type == "BOOL" && sd[3].value == "FALSE", "le depart : BOOL := FALSE");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "INT", &why, &note) && sd[3].type == "INT" && sd[3].value.empty()
                      && note == "valeur FALSE retir\xC3\xA9" "e : un BOOL (FALSE) ne va pas dans un INT",
                  "BOOL -> INT : FALSE retiree (" + note + ")");
            check(!de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "40000", &why) && sd[3].value.empty()
                      && why.find("hors des bornes d'un INT") != std::string::npos,
                  "40000 pour un INT : refuse (" + why + ")");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "7", &why) && sd[3].value == "7", "7 pour un INT : pris");
            note.clear();
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "DINT", &why, &note) && sd[3].value == "7" && note.empty(),
                  "INT -> DINT : 7 convient, garde, rien a dire");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "REAL", &why, &note) && sd[3].value == "7" && note.empty(),
                  "DINT -> REAL : 7 convient (un entier pour un reel)");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "t_vec", &why, &note) && sd[3].type == "T_VEC" && sd[3].value.empty()
                      && note.find("valeur 7 retir\xC3\xA9" "e : une structure (T_VEC) prend les valeurs initiales de son type") == 0,
                  "REAL -> T_VEC : 7 retiree (" + note + ")");
            check(!de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "5", &why) && why.find("une structure (T_VEC)") != std::string::npos,
                  "5 pour une structure : refuse (" + why + ")");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "ARRAY[1..4] OF T_VEC", &why, &note)
                      && !de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "0", &why)
                      && why.find("les cases d'un tableau de T_VEC prennent les valeurs initiales de leur type") != std::string::npos,
                  "0 pour un ARRAY[1..4] OF T_VEC : refuse (" + why + ")");
            note.clear();
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "STRING", &why, &note) && note.empty()
                      && de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "'abc'", &why) && sd[3].value == "'abc'"
                      && de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "REAL", &why, &note) && sd[3].value.empty()
                      && note.find("un STRING ('abc') ne va pas dans un REAL") != std::string::npos,
                  "STRING -> REAL : 'abc' retiree (" + note + ")");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "Max * 0.5", &why)
                      && de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "INT", &why, &note) && sd[3].value == "Max * 0.5",
                  "une expression : on ne sait pas la juger, gardee");
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "16#FF", &why) && sd[3].value == "16#FF"
                      && !de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "16#FFFFF", &why),
                  "16#FF pour un INT : pris ; 16#FFFFF : hors des bornes");
            // Une constante qui change de type : la valeur nulle du nouveau type (elle en a une).
            note.clear();
            check(sd[0].kind == DeclKind::Constant && sd[0].value == "10"
                      && de::set(p, script, de::Tab::Constants, 0, de::Column::Type, "STRING", &why, &note) && sd[0].value == "''"
                      && note == "valeur 10 remplac\xC3\xA9" "e par '' : un INT (10) ne va pas dans un STRING",
                  "une constante INT 10 -> STRING : '' (" + note + ")");
            // Une enumeration : une de ses valeurs.
            HmiType mode;
            mode.id = p.allocate();
            mode.name = "T_MODE";
            mode.kind = HmiTypeKind::Enumeration;
            mode.values = {{"Auto", 0, "", ""}, {"Manu", 1, "", ""}};
            p.programs.types.push_back(mode);
            check(de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "T_MODE", &why, &note) && sd[3].value.empty()
                      && de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "T_MODE#Manu", &why)
                      && de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "Auto", &why)
                      && !de::set(p, script, de::Tab::Variables, 2, de::Column::Value, "Arret", &why)
                      && why.find("Arret n'est pas une valeur de T_MODE (Auto, Manu...)") != std::string::npos,
                  "une enumeration : T_MODE#Manu, Auto pris ; Arret refuse (" + why + ")");
            de::set(p, script, de::Tab::Variables, 2, de::Column::Type, "REAL", &why);   // la suite ne connait pas T_MODE
            p.programs.types.pop_back();
            check(de::valueMisfit(p, "BOOL", "TRUE").empty() && de::valueMisfit(p, "TIME", "T#5s").empty() && !de::valueMisfit(p, "TIME", "5").empty()
                      && de::valueMisfit(p, "LREAL", "2.5").empty() && !de::valueMisfit(p, "INT", "2.5").empty()
                      && de::valueMisfit(p, "ARRAY[1..3] OF REAL", "1.5").empty() && de::valueMisfit(p, "BIDULE", "3").empty(),
                  "valueMisfit : TRUE BOOL, T#5s TIME, 2.5 LREAL, 1.5 pour un ARRAY OF REAL ; 5 TIME et 2.5 INT non ; un type inconnu : rien");
        }
        check(de::set(p, script, de::Tab::Constants, 0, de::Column::Description, "la borne\nhaute\t(bar)", &why) && sd[0].description == "la borne haute (bar)",
              "la documentation : sur une ligne");
        // Les libelles se relisent.
        bool round = true;
        for (const auto s : {Storage::Execution, Storage::Kept, Storage::Persistent}) round = round && de::storageFromText(de::storageLabel(s)) == s;
        for (const auto m : {PassMode::In, PassMode::InOut, PassMode::Out}) round = round && de::modeFromText(de::modeLabel(m)) == m;
        for (const auto v : {Visibility::Public, Visibility::Private}) round = round && de::visibilityFromText(de::visibilityLabel(v)) == v;
        for (const auto s : {Storage::Execution, Storage::Kept, Storage::Persistent}) round = round && de::storageFromText(storageKey(s)) == s;
        for (const auto m : {PassMode::In, PassMode::InOut, PassMode::Out}) round = round && de::modeFromText(passModeKey(m)) == m;
        check(round, "chaque libelle (et chaque cle du disque) se relit");
        check(de::typeChoices(p).front() == "BOOL" && de::typeChoices(p).back() == "T_VEC", "les types proposes : de base, puis ceux du projet");
    }
    // DUPLIQUER, DEPLACER, SUPPRIMER.
    {
        Project p = fivePlaces();
        const de::Place fn{K::Function, kNoId, kNoId, kNoId, p.programs.functions[0].id, {}, {}};
        const de::Place script{K::Script, kNoId, kNoId, kNoId, p.programs.scripts[0].id, {}, {}};
        std::string why;
        std::vector<Id> made;
        check(de::duplicate(p, script, de::Tab::Constants, {0}, &made, &why) && made.size() == 1, "dupliquer Max");
        const auto& sd = p.programs.scripts[0].decls;
        check(sd[1].name == "Max_copie" && sd[1].id == made[0] && sd[1].id != sd[0].id && sd[1].value == "10" && sd[1].description == sd[0].description,
              "Max_copie, juste apres, un identifiant neuf, le reste copie");
        check(de::duplicate(p, script, de::Tab::Constants, {0}, &made, &why) && declNamed(sd, "Max_copie2"), "encore : Max_copie2");
        check(de::duplicate(p, script, de::Tab::Variables, {0, 2}, &made, &why) && made.size() == 2 && declNamed(sd, "Compteur_copie")
                  && declNamed(sd, "Total_copie"),
              "deux lignes d'un coup");
        auto names = [&](const std::vector<Declaration>& list, de::Tab t) {
            std::string out;
            for (const auto i : de::rowsOf(list, t)) out += list[i].name + " ";
            return out;
        };
        check(names(sd, de::Tab::Variables) == "Compteur Compteur_copie Tmp Total Total_copie ", "l'ordre : " + names(sd, de::Tab::Variables));
        const auto& fd = p.programs.functions[0].decls;
        check(!de::move(p, fn, de::Tab::Parameters, 0, -1, &why) && why.find("t\xC3\xAAte") != std::string::npos, "monter le premier : refuse");
        check(!de::move(p, fn, de::Tab::Parameters, 3, +1, &why), "descendre le dernier : refuse");
        check(de::move(p, fn, de::Tab::Parameters, 0, +1, &why) && names(fd, de::Tab::Parameters) == "b a v ok ",
              "descendre a : b a v ok (l'ordre des appels sans nom)");
        check(names(fd, de::Tab::Variables) == "t ", "les locales ne bougent pas");
        check(hmi::functionSignature(p.programs.functions[0]).find("Moyenne(b") == 0, "la signature suit l'ordre : " + hmi::functionSignature(p.programs.functions[0]));
        check(de::remove(p, script, de::Tab::Variables, {1, 4}, &why) && names(sd, de::Tab::Variables) == "Compteur Tmp Total ", "supprimer deux lignes");
        check(!de::remove(p, script, de::Tab::Variables, {7}, &why), "une ligne hors de l'onglet : refuse");
    }
    // LES FAUTES, ligne a ligne.
    {
        Project p = fivePlaces();
        p.programs.functions[0].decls.push_back(declared(DeclKind::Variable, "Moyenne", "REAL"));
        p.programs.types[0].operators[0].decls.push_back(declared(DeclKind::Variable, "b", "REAL"));
        p.programs.scripts[0].decls.push_back(declared(DeclKind::Constant, "Vide", "INT"));
        const de::Place script{K::Script, kNoId, kNoId, kNoId, p.programs.scripts[0].id, {}, {}};
        const de::Place fn{K::Function, kNoId, kNoId, kNoId, p.programs.functions[0].id, {}, {}};
        const de::Place op{K::TypeOperator, kNoId, kNoId, p.programs.types[0].id, p.programs.types[0].operators[0].id, {}, {}};
        const auto fs = de::faults(p, de::locate(p, script));
        const auto ff = de::faults(p, de::locate(p, fn));
        const auto fo = de::faults(p, de::locate(p, op));
        check(fs.size() == 5 && fs[0].empty() && fs[4].find("sans valeur") != std::string::npos, "le script : seule Vide est fautive (" + fs[4] + ")");
        check(ff.back().find("nom de la fonction") != std::string::npos && ff[0].empty(), "la fonction : la locale Moyenne (" + ff.back() + ")");
        check(fo.back().find("op\xC3\xA9rateur") != std::string::npos && fo[0].empty(), "l'operateur : la locale b (" + fo.back() + ")");
        const auto diags = dc::checkDeclarations(p.programs.types[0].operators[0].decls, dc::Role::Operator, p.programs.types[0].operators[0].body);
        check(diags.size() == 1 && diags[0].message.find("\xC2\xAB b \xC2\xBB") != std::string::npos, "Compiler le dit aussi : " + (diags.empty() ? std::string("rien") : diags[0].message));
    }
    // UNE COMMANDE : Ctrl+Z rend le projet d'avant ; le code edite tourne.
    {
        auto doc = std::make_shared<Document>();
        Project& p = doc->project;
        p.programs.variables.push_back(hmiVariable(p, "Somme", "INT"));
        Script s;
        s.id = p.allocate();
        s.name = "Cumul";
        s.event = "Appel";
        s.body = "Total := Total + Pas;\nSomme := Total;\n";
        p.programs.scripts.push_back(s);
        const de::Place at{K::Script, kNoId, kNoId, kNoId, s.id, {}, {}};
        const Project before = p;
        core::CommandStack stack;
        const auto apply = [&](const std::string& label, const std::function<bool(Project&, std::string*)>& edit) {
            std::string why;
            bool ok = false;
            auto cmd = changeProject(doc, label, [&](Project& q) { ok = edit(q, &why); });
            if (cmd) (void)stack.push(std::move(cmd));
            return ok;
        };
        check(apply("Ajouter", [&](Project& q, std::string* w) { return de::add(q, at, de::Tab::Constants, -1, "Pas", nullptr, w); })
                  && apply("Valeur", [&](Project& q, std::string* w) { return de::set(q, at, de::Tab::Constants, 0, de::Column::Value, "5", w); })
                  && apply("Type", [&](Project& q, std::string* w) { return de::set(q, at, de::Tab::Constants, 0, de::Column::Type, "INT", w); })
                  && apply("Ajouter", [&](Project& q, std::string* w) { return de::add(q, at, de::Tab::Variables, -1, "Total", nullptr, w); })
                  && apply("Stockage", [&](Project& q, std::string* w) {
                         return de::set(q, at, de::Tab::Variables, 0, de::Column::Storage, "Conserv\xC3\xA9" "e", w);
                     }),
              "cinq gestes, cinq commandes");
        check(p.programs.scripts[0].decls.size() == 2 && p.programs.scripts[0].decls[0].id != p.programs.scripts[0].decls[1].id,
              "deux declarations, deux identifiants");
        FakePlc plc;
        Runtime rt;
        rt.bind(&p, &plc);
        rt.start(0.0);
        std::string why;
        for (int k = 0; k < 3; ++k) (void)rt.callScript("Cumul", 0.1 * k, &why);
        check(rt.variable("Somme")->display() == "15", "trois appels : Total Conservee, Pas constante = 5 -> Somme = 15 (" + rt.variable("Somme")->display() + why + ")");
        while (stack.canUndo()) (void)stack.undo();
        check(p.programs == before.programs, "tout annule : le projet d'avant, a l'identique");
    }
}

// ---------------------------------------------- le stockage Persistante (lot 5) ----
void persistante() {
    std::printf("-- lot 5 : le stockage Persistante (rendu au lancement suivant)\n");
    const auto project = [](Storage storage, const std::string& type) {
        Project p;
        p.programs.variables.push_back(hmiVariable(p, "Somme", "REAL"));
        Script s;
        s.id = p.allocate();
        s.name = "Compter";
        s.event = "Appel";
        s.body = "Total := Total + 1;\nSomme := Total;\n";
        s.decls = {declared(DeclKind::Variable, "Total", type, "0", storage)};
        p.programs.scripts.push_back(s);
        Script idle = s;                       // un script qui ne tourne pas dans la seance
        idle.id = p.allocate();
        idle.name = "Jamais";
        idle.decls = {declared(DeclKind::Variable, "Garde", "INT", "0", Storage::Persistent)};
        idle.body = "Garde := Garde + 1;\n";
        p.programs.scripts.push_back(idle);
        uniqueDeclarationIds(p);
        return p;
    };
    const auto session = [](const Project& p, const std::vector<simdata::Cell>* start, int calls, std::string& somme,
                            std::vector<simdata::Cell>* captured, std::string* journal = nullptr) {
        FakePlc plc;
        Runtime rt;
        rt.bind(&p, &plc);
        if (start) rt.setStartData(*start);
        rt.start(0.0);
        std::string why;
        for (int k = 0; k < calls; ++k) (void)rt.callScript("Compter", 0.1 * (k + 1), &why);
        somme = rt.variable("Somme")->display();
        if (captured) *captured = rt.captureData();
        if (journal)
            for (const auto& e : rt.journal()) *journal += e.message + "\n";
        return rt.persistentPending();
    };
    Project p = project(Storage::Persistent, "INT");
    std::string somme;
    std::vector<simdata::Cell> cells;
    (void)session(p, nullptr, 3, somme, &cells);
    const auto decl = std::find_if(cells.begin(), cells.end(), [](const simdata::Cell& c) { return simdata::isDeclarationCell(c); });
    check(somme == "3" && decl != cells.end() && decl->variable == p.programs.scripts[0].decls[0].id && decl->value.asInteger() == 3
              && decl->name == "Compter.Total" && decl->declared == "INT",
          "trois appels : Total = 3, pris a l'arret (une case de declaration, son identifiant) - " + somme);
    // L'instantane de simulation : ecrit, relu.
    simdata::Snapshot snap;
    snap.date = "2026-10-09 10:00:00";
    snap.cells = cells;
    simdata::Snapshot back;
    std::string why;
    check(simdata::parse(simdata::serialize(snap), back, &why) && back.cells.size() == cells.size()
              && std::any_of(back.cells.begin(), back.cells.end(), [](const simdata::Cell& c) { return simdata::isDeclarationCell(c); }),
          "l'instantane de simulation garde la case (ecrit, relu) - " + why);
    // Le lancement suivant : Total repart de 3.
    (void)session(p, &back.cells, 1, somme, nullptr);
    check(somme == "4", "le lancement suivant : Total continue (4) - " + somme);
    // Un script qui ne tourne pas de la seance garde la valeur qui lui avait ete rendue.
    auto withIdle = back.cells;
    simdata::Cell g;
    g.variable = p.programs.scripts[1].decls[0].id;
    g.name = "Jamais.Garde";
    g.declared = "INT";
    g.path = std::string(simdata::kDeclarationPath);
    g.value = sim::Value::integer(sim::Type::Int, 7);
    withIdle.push_back(g);
    std::vector<simdata::Cell> again;
    const std::size_t pending = session(p, &withIdle, 1, somme, &again);
    check(pending == 1, "le script qui n'a pas tourne : sa valeur attend sa premiere execution (Jamais.Garde)");
    check(std::any_of(again.begin(), again.end(), [](const simdata::Cell& c) { return c.name == "Jamais.Garde" && c.value.asInteger() == 7; }),
          "... et l'arret la reprend telle quelle (7)");
    // Une variable Conservee repart de sa valeur initiale au lancement suivant.
    Project kept = project(Storage::Kept, "INT");
    std::vector<simdata::Cell> keptCells;
    (void)session(kept, nullptr, 3, somme, &keptCells);
    check(std::none_of(keptCells.begin(), keptCells.end(), [](const simdata::Cell& c) { return simdata::isDeclarationCell(c); }),
          "Conservee : pas de case a l'arret");
    (void)session(kept, &keptCells, 1, somme, nullptr);
    check(somme == "1", "Conservee : le lancement suivant repart de 0 (1) - " + somme);
    // Un type change : INT -> DINT converti ; INT -> STRING incompatible : la valeur initiale, dite.
    Project wider = p;
    wider.programs.scripts[0].decls[0].type = "DINT";
    (void)session(wider, &back.cells, 1, somme, nullptr);
    check(somme == "4", "INT devenu DINT : la valeur convertie (4) - " + somme);
    Project text = p;
    text.programs.scripts[0].decls[0].type = "STRING";
    text.programs.scripts[0].decls[0].value = "''";
    text.programs.scripts[0].body = "Total := CONCAT(Total, 'x');\n";
    std::string journal;
    (void)session(text, &back.cells, 1, somme, nullptr, &journal);
    check(journal.find("Compter.Total : INT devient STRING") != std::string::npos, "INT devenu STRING : la valeur initiale, et le journal le dit\n" + journal);
    // Le poste d'exploitation : le meme circuit (hmi::retain).
    retain::Store store;
    check(retain::merge(store, p, cells, "2026-10-09 10:00:00"), "le stockage du poste prend la case");
    const auto restored = retain::retainedCells(p, store);
    (void)session(p, &restored, 1, somme, nullptr);
    check(somme == "4", "le poste : rendue au lancement suivant (4) - " + somme);
    retain::Store reread;
    check(retain::parse(retain::serialize(store), reread, &why) && reread.entries.size() == store.entries.size(),
          "le fichier du poste garde la case - " + why);
}


// ------------------------------------------------ 1.12.7 : les valeurs calculees ----
Declaration computedDecl(DeclKind kind, std::string name, std::string type, std::string calc,
                         std::vector<Declaration> locals = {}, Storage storage = Storage::Execution) {
    Declaration d = declared(kind, std::move(name), std::move(type), {}, storage);
    d.computed = true;
    d.calc = std::move(calc);
    d.locals = std::move(locals);
    return d;
}

// Reglages : Base (ecrite, publique), Seuils (MAP calculee avec une locale et une boucle FOR sans
// declaration), Total (lit Seuils), Echelle (lit une variable IHM), Secret (privee), Compteur (une
// variable Conservee a la valeur initiale calculee). Usage : lit Reglages.X par leur nom qualifie.
Project calcProject() {
    Project p;
    p.programs.variables.push_back(hmiVariable(p, "Pleine_Echelle", "REAL"));
    p.programs.variables.back().initial = "250.0";
    for (const char* n : {"Lu", "Bas", "Vu", "Ech"}) p.programs.variables.push_back(hmiVariable(p, n, "REAL"));
    for (const char* n : {"N", "C2"}) p.programs.variables.push_back(hmiVariable(p, n, "DINT"));
    Script r;
    r.id = p.allocate();
    r.name = "Reglages";
    r.event = "Appel";
    r.body = "Vu := Total + Base;\nC2 := Compteur;\nCompteur := Compteur + 1;\nEch := Echelle;\n";
    Declaration noms = declared(DeclKind::Variable, "Noms", "ARRAY[0..2] OF STRING", "['bas', 'moyen', 'haut']");
    r.decls = {declared(DeclKind::Constant, "Base", "REAL", "10.0"),
               computedDecl(DeclKind::Constant, "Seuils", "MAP[STRING] OF REAL",
                            "(* un seuil par nom *)\nFOR i := 0 TO 2 DO\n    Seuils[Noms[i]] := Base * (i + 1);\nEND_FOR;\n", {noms}),
               computedDecl(DeclKind::Constant, "Total", "REAL", "Total := Seuils['bas'] + Seuils['haut'];\n"),
               computedDecl(DeclKind::Constant, "Echelle", "REAL", "Echelle := Pleine_Echelle / 100.0;\n"),
               declared(DeclKind::Constant, "Secret", "INT", "3"),
               computedDecl(DeclKind::Variable, "Compteur", "DINT", "Compteur := REAL_TO_DINT(Total);\n", {}, Storage::Kept)};
    r.decls[4].visibility = Visibility::Private;
    p.programs.scripts.push_back(r);
    Script u;
    u.id = p.allocate();
    u.name = "Usage";
    u.event = "Appel";
    u.body = "Lu := Reglages.Total;\nBas := Reglages.Seuils['bas'];\nN := MAP_SIZE(Reglages.Seuils);\n";
    p.programs.scripts.push_back(u);
    uniqueDeclarationIds(p);
    return p;
}

void calculees() {
    std::printf("-- 1.12.7 : les valeurs calculees au demarrage\n");
    const Project p = calcProject();
    // Le disque : calculee=oui, le calcul, ses locales - relus a l'identique.
    {
        const auto files = serializeProject(p);
        const std::string index = fileText(files, "ihm.txt");
        check(index.find("calculee=oui") != std::string::npos && index.find("calcul=") != std::string::npos
                  && index.find("locale nom=\"Noms\" type=\"ARRAY[0..2] OF STRING\"") != std::string::npos,
              "sur disque : calculee=oui, le calcul, la ligne de sa locale");
        const auto back = parseProject(readerOf(files));
        check(back.has_value() && back.value().programs.scripts.size() == 2
                  && back.value().programs.scripts[0].decls == p.programs.scripts[0].decls,
              "relu : les memes declarations (calculees, leur code, leurs locales)");
        Project plain = p;
        for (auto& d : plain.programs.scripts[0].decls) d.computed = false;
        check(!(plain.programs.scripts[0].decls == p.programs.scripts[0].decls), "calculee ou non : deux declarations differentes");
    }
    // Le texte qui tourne : la declaration (a ecrire), ses locales, le DINT de la boucle, les constantes du script.
    {
        const Script& r = p.programs.scripts[0];
        const std::string code = calc::runnableCode(r, r.decls[1]);
        check(code.find("VAR_TEMP Seuils : MAP[STRING] OF REAL; Noms : ARRAY[0..2] OF STRING := ['bas', 'moyen', 'haut']; i : DINT;") == 0
                  && code.find("VAR CONSTANT Base : REAL := 10.0; Total : REAL; Echelle : REAL; Secret : INT := 3; END_VAR") != std::string::npos
                  && code.find("Compteur") == std::string::npos,
              "le calcul d'une constante : elle, ses locales, i (DINT), les constantes du script, pas ses variables\n" + code);
        check(std::count(code.begin(), code.end(), '\n') == std::count(r.decls[1].calc.begin(), r.decls[1].calc.end(), '\n'),
              "les lignes du calcul ne bougent pas");
        const std::string var = calc::runnableCode(r, r.decls[5]);
        check(var.find("Compteur : DINT;") != std::string::npos && var.find("VAR CONSTANT") != std::string::npos,
              "le calcul d'une variable : les constantes aussi\n" + var);
        check(dc::codeOf(r).find("Seuils : MAP[STRING] OF REAL; Total : REAL;") != std::string::npos,
              "le script : ses constantes calculees sans valeur ecrite (le moteur la donne)\n" + dc::codeOf(r));
    }
    // L'ordre : Seuils avant Total, les constantes avant la variable.
    {
        const auto pl = calc::plan(p);
        std::string order;
        for (const auto& s : pl.order) order += s.path + " ";
        check(pl.cycle.empty() && order == "Reglages.Seuils Reglages.Total Reglages.Echelle Reglages.Compteur ",
              "le plan : Seuils, Total (qui la lit), Echelle, puis la variable - " + order);
    }
    // Le moteur : calcule au demarrage, lu dans le script, par nom qualifie ailleurs, jamais ecrit.
    {
        Runtime rt;
        rt.bind(&p, nullptr);              // sans automate : les fonctions standard de l'IHM (REAL_TO_DINT)
        rt.start(0.0);
        std::string journal;
        for (const auto& e : rt.journal()) journal += e.source + " " + e.message + "\n";
        check(rt.running() && rt.startError().empty(), "demarre (" + rt.startError() + ")\n" + journal);
        const auto seuils = rt.computedValue(p.programs.scripts[0].id, "Seuils");
        check(seuils && sim::display(*seuils) == "{'bas': 10, 'haut': 30, 'moyen': 20}",
              "Seuils calculee : " + (seuils ? sim::display(*seuils) : std::string("rien")));
        std::string why;
        const bool a = rt.callScript("Reglages", 0.1, &why);
        const bool b = rt.callScript("Usage", 0.2, &why);
        const auto v = [&](const char* n) { return rt.variable(n)->display(); };
        check(a && b && v("Vu") == "50" && v("C2") == "40" && v("Ech") == "2.5" && v("Lu") == "40" && v("Bas") == "10" && v("N") == "3",
              "lues : Total + Base = " + v("Vu") + ", Compteur = " + v("C2") + ", Echelle = " + v("Ech") + " ; Reglages.Total = " + v("Lu")
                  + ", Reglages.Seuils['bas'] = " + v("Bas") + ", MAP_SIZE = " + v("N") + " " + why);
        (void)rt.callScript("Reglages", 0.3, &why);
        check(v("C2") == "41", "la variable a valeur initiale calculee reste Conservee : 41 au 2e appel - " + v("C2"));
        check(journal.find("Reglages.Total = 40") != std::string::npos && journal.find("Reglages.Seuils = {'bas': 10") != std::string::npos,
              "la Console dit chaque valeur calculee\n" + journal);
    }
    // Ecrire une constante qualifiee : refuse.
    for (const char* body : {"Reglages.Total := 1.0;\n", "MAP_REMOVE(Reglages.Seuils, 'bas');\n", "Reglages.Seuils['x'] := 1.0;\n"}) {
        Project q = calcProject();
        q.programs.scripts[1].body = body;
        Runtime rt;
        rt.bind(&q, nullptr);
        rt.start(0.0);
        std::string why;
        const bool ok = rt.callScript("Usage", 0.1, &why);
        const auto seuils = rt.computedValue(q.programs.scripts[0].id, "Seuils");
        check(!ok && why.find("constante") != std::string::npos && seuils && seuils->textKeys.size() == 3,
              std::string("une constante qualifiee ne s'ecrit pas : ") + body + " -> " + why);
    }
    // Une erreur de calcul arrete le demarrage ; une boucle aussi.
    {
        Project q = calcProject();
        q.programs.scripts[0].decls[2].calc = "Total := Seuils['absent'];\n";
        Runtime rt;
        rt.bind(&q, nullptr);
        rt.start(0.0);
        check(!rt.running() && rt.startError().find("calcul Reglages.Total") != std::string::npos,
              "une cle absente : le demarrage s'arrete et dit ou - " + rt.startError());
        // La ligne de la Console y mene (un double-clic) : le script, la declaration, la ligne du calcul.
        const JournalEntry* err = nullptr;
        for (const auto& e : rt.journal())
            if (e.kind == "Erreur") err = &e;
        check(err && err->script == q.programs.scripts[0].id && err->declaration == "Total" && err->line == 1,
              "l'erreur du calcul porte son script, sa declaration (Total) et sa ligne (1) - "
                  + (err ? err->declaration + " ligne " + std::to_string(err->line) : std::string("pas d'erreur au journal")));
        Project c = calcProject();
        c.programs.scripts[0].decls[1].calc = "Seuils['a'] := Total;\n";
        const auto pl = calc::plan(c);
        check(!pl.cycle.empty() && pl.cycle.find("Reglages.Seuils") != std::string::npos && pl.cycle.find("Reglages.Total") != std::string::npos,
              "Seuils lit Total qui lit Seuils : la boucle - " + pl.cycle);
        Runtime rc;
        rc.bind(&c, nullptr);
        rc.start(0.0);
        check(!rc.running() && rc.startError().find("boucle") != std::string::npos, "une boucle : le demarrage s'arrete - " + rc.startError());
    }
    // Les fautes propres a un calcul.
    {
        Project q = calcProject();
        auto& r = q.programs.scripts[0];
        const auto says = [&](const Declaration& d, std::string_view what) {
            const auto a = calc::analyse(q, r, d);
            std::string all;
            for (const auto& i : a.issues) all += (i.error ? "E " : "A ") + i.message + "\n";
            return std::make_pair(all.find(what) != std::string::npos, all);
        };
        const auto t1 = says(computedDecl(DeclKind::Constant, "K", "INT", "VAR\n  x : INT;\nEND_VAR\nK := 1;\n"), "VAR \xE2\x80\xA6 END_VAR");
        check(t1.first, "un bloc VAR dans le code : refuse (onglet Locales)\n" + t1.second);
        const auto t2 = says(computedDecl(DeclKind::Constant, "K", "INT", "Base := 2.0;\nK := 1;\n"), "Base est une constante : elle ne s'\xC3\xA9" "crit pas");
        check(t2.first, "une constante du script ecrite : refusee\n" + t2.second);
        const auto t3 = says(computedDecl(DeclKind::Constant, "K", "DINT", "K := Compteur;\n"), "Compteur est une variable du script");
        check(t3.first, "une constante qui lit une variable du script : refusee\n" + t3.second);
        const auto t4 = says(computedDecl(DeclKind::Constant, "K", "INT", "x := 1;\n"), "K ne re\xC3\xA7oit aucune valeur");
        check(t4.first, "un calcul qui n'ecrit pas sa constante : averti\n" + t4.second);
        Script& u = q.programs.scripts[1];
        u.decls = {computedDecl(DeclKind::Constant, "K", "INT", "K := Reglages.Secret + Reglages.Absent + Reglages.Compteur;\n")};
        const auto a = calc::analyse(q, u, u.decls[0]);
        std::string all;
        for (const auto& i : a.issues) all += i.message + "\n";
        check(all.find("Reglages.Secret est priv\xC3\xA9" "e : seul Reglages la lit") != std::string::npos
                  && all.find("Reglages.Absent n'existe pas") != std::string::npos
                  && all.find("Reglages.Compteur est une variable de Reglages") != std::string::npos,
              "les constantes d'un autre script : privee, inconnue, une variable - refusees\n" + all);
        const auto good = calc::analyse(q, r, r.decls[2]);
        check(!good.hasError() && good.assigns && good.reads == std::vector<std::string>{"Seuils"},
              "Total : juste, ecrit, lit Seuils");
        // La fenetre du calcul, « Lit » : tout ce qu'il lit (ecrit ou calcule, une variable IHM aussi).
        const auto seuils = calc::analyse(q, r, r.decls[1]);
        const auto echelle = calc::analyse(q, r, r.decls[3]);
        check(seuils.reads.empty() && seuils.uses == std::vector<std::string>{"Base"} && echelle.uses == std::vector<std::string>{"Pleine_Echelle"},
              "Lit : Seuils lit Base (ecrite), Echelle lit la variable IHM Pleine_Echelle");
    }
    // « Lu par » : les calculs, le code du script, les scripts qui lisent Reglages.X.
    {
        Project q = calcProject();
        const auto& r = q.programs.scripts[0];
        const auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (const auto& x : v) s += (s.empty() ? "" : " | ") + x;
            return s;
        };
        const auto base = calc::readers(q, r, r.decls[0]);
        check(join(base) == "calcul de Reglages.Seuils | le code de Reglages", "Base : lue par le calcul de Seuils et le code de Reglages - " + join(base));
        const auto total = calc::readers(q, r, r.decls[2]);
        check(join(total) == "calcul de Reglages.Compteur | le code de Reglages | le script Usage",
              "Total : lue par le calcul de Compteur, le code de Reglages, le script Usage (Reglages.Total) - " + join(total));
        const auto secret = calc::readers(q, r, r.decls[4]);
        check(secret.empty(), "Secret : lue par personne - " + join(secret));
    }
    // « Deplacer dans Locales » : un bloc VAR du code va au bout des locales.
    {
        std::string code = "VAR\n    k : REAL := 2.0;   // un facteur\nEND_VAR\nK := k * 3.0;\n", locals = "a : INT := 1;";
        std::string why;
        check(decledit::moveVarBlocks(code, locals, &why) && code == "K := k * 3.0;\n" && locals == "a : INT := 1;\nk : REAL := 2.0;   // un facteur\n",
              "Deplacer dans Locales : le bloc quitte le code, sa locale suit les autres - [" + code + "] [" + locals + "] " + why);
        std::string none = "K := 1;\n", l2;
        check(!decledit::moveVarBlocks(none, l2, &why) && why.find("pas de bloc") != std::string::npos, "pas de bloc : rien a deplacer - " + why);
    }
    // Les controles du projet : un script qui lit une constante privee d'un autre, un calcul fautif.
    {
        Project q = calcProject();
        q.programs.scripts[1].body += "Lu := Reglages.Secret;\nReglages.Total := 2.0;\n";
        q.programs.scripts[0].decls[3].calc = "Echelle := Inconnue_X / 100.0;\n";
        const auto issues = generate(q, [](std::string_view) { return false; });
        std::string all;
        for (const auto& i : issues)
            if (i.category == "Script") all += i.property + " : " + i.message + "\n";
        check(all.find("Usage : Reglages.Secret est priv\xC3\xA9" "e") != std::string::npos
                  && all.find("Usage : Reglages.Total est une constante : elle ne s'\xC3\xA9" "crit pas") != std::string::npos
                  && all.find("Reglages : calcul de Echelle, ligne 1 : variable inexistante : Inconnue_X") != std::string::npos
                  && all.find("Reglages.Total") != std::string::npos,
              "Generer : la constante privee lue, la constante ecrite, le nom inconnu du calcul\n" + all);
        check(all.find("Lu := Reglages.Seuils") == std::string::npos && all.find("variable inexistante : Reglages") == std::string::npos,
              "Reglages.X n'est pas une variable inexistante\n" + all);
        Project ok = calcProject();
        std::string clean;
        for (const auto& i : generate(ok, [](std::string_view) { return false; }))
            if (i.category == "Script" && i.severity == Issue::Severity::Error) clean += i.property + " : " + i.message + "\n";
        for (const auto& i : compile(ok))
            if (i.category == "Script" && i.severity == Issue::Severity::Error) clean += i.property + " : " + i.message + "\n";
        check(clean.empty(), "le projet juste : aucune erreur de script\n" + clean);
        Project syn = calcProject();
        syn.programs.scripts[0].decls[2].calc = "Total := (Seuils['bas'];\n";
        std::string bad;
        for (const auto& i : compile(syn))
            if (i.category == "Script") bad += i.property + " : " + i.message + "\n";
        check(bad.find("Reglages : calcul de Total, ligne 1") != std::string::npos, "Compiler : la syntaxe d'un calcul, sa ligne\n" + bad);
        // La faute y mene : la declaration et la ligne du calcul (Compiler, Generer).
        bool led = false;
        for (const auto& i : compile(syn))
            if (i.category == "Script" && i.message.find("calcul de Total") != std::string::npos)
                led = led || (i.declaration == "Total" && i.line == 1 && i.script == syn.programs.scripts[0].id);
        Project gen = calcProject();
        gen.programs.scripts[0].decls[3].calc = "Echelle := 1.0;\nEchelle := Inconnue_Y / 100.0;\n";
        bool genLed = false;
        for (const auto& i : generate(gen, [](std::string_view) { return false; }))
            if (i.message.find("Inconnue_Y") != std::string::npos) genLed = genLed || (i.declaration == "Echelle" && i.line == 2);
        check(led && genLed, "Compiler et Generer : la faute d'un calcul porte sa declaration et sa ligne (la fenetre du calcul s'y ouvre)");
    }
    // L'edition : le calcul et ses locales (une par ligne, sans VAR), le renommage dans les calculs et
    // dans Reglages.X, une valeur ecrite qui remplace le calcul, le defaut d'une MAP (plus de 0.0).
    {
        using K = de::Place::Kind;
        Project q = calcProject();
        const de::Place at{K::Script, kNoId, kNoId, kNoId, q.programs.scripts[0].id, {}, {}};
        std::string why, note;
        // La ligne 0 de l'onglet Constantes : Base ; 1 : Seuils ; 2 : Total ; 3 : Echelle ; 4 : Secret.
        check(de::setCalculation(q, at, de::Tab::Constants, 4, "Secret := 2 + 2;", "k : INT := 1;   // un compteur\nm : REAL;", &why),
              "Secret devient calculee, deux locales : " + why);
        const auto& sec = q.programs.scripts[0].decls[4];
        check(sec.computed && sec.calc == "Secret := 2 + 2;\n" && sec.locals.size() == 2 && sec.locals[0].name == "k"
                  && sec.locals[0].value == "1" && sec.locals[0].description == "un compteur" && sec.locals[1].type == "REAL",
              "le calcul et ses locales (nom, type, valeur, documentation)\n" + de::localsText(sec.locals));
        check(de::localsText(sec.locals) == "k : INT := 1;   // un compteur\nm : REAL;\n", "les locales en texte : " + de::localsText(sec.locals));
        check(!de::setCalculation(q, at, de::Tab::Constants, 4, "Secret := 1;", "VAR\nk : INT;\nEND_VAR", &why) && why.find("pas de VAR") != std::string::npos,
              "des locales avec VAR : refusees - " + why);
        check(!de::setCalculation(q, at, de::Tab::Constants, 4, "Secret := 1;", "k : INT;\nK : REAL;", &why) && why.find("deux fois") != std::string::npos,
              "une locale en double : refusee - " + why);
        check(!de::setCalculation(q, at, de::Tab::Constants, 4, "Secret := 1;", "Secret : INT;", &why) && why.find("nom de la d") != std::string::npos,
              "une locale du nom de la declaration : refusee - " + why);
        check(de::setCalculation(q, at, de::Tab::Constants, 4, "VAR\n  j : INT := 2;   // un pas\nEND_VAR\nSecret := j * 2;\n", "k : INT;", &why)
                  && q.programs.scripts[0].decls[4].locals.size() == 2 && q.programs.scripts[0].decls[4].locals[1].name == "j"
                  && q.programs.scripts[0].decls[4].locals[1].value == "2" && q.programs.scripts[0].decls[4].calc == "Secret := j * 2;\n",
              "un bloc VAR colle dans le code : range dans les locales - " + why + "\n" + q.programs.scripts[0].decls[4].calc);
        check(de::setCalculation(q, at, de::Tab::Constants, 4, "  \n", "", &why) && !q.programs.scripts[0].decls[4].computed
                  && q.programs.scripts[0].decls[4].value == "3",
              "un code vide : la valeur redevient ecrite (3) - " + why);
        // Renommer Base : le calcul de Seuils le lit ; renommer Total (publique) : Usage lit Reglages.Total.
        check(de::set(q, at, de::Tab::Constants, 0, de::Column::Name, "Base2", &why), "renommer Base : " + why);
        check(q.programs.scripts[0].decls[1].calc.find("Base2 * (i + 1)") != std::string::npos, "le calcul de Seuils suit : " + q.programs.scripts[0].decls[1].calc);
        check(de::set(q, at, de::Tab::Constants, 2, de::Column::Name, "Somme", &why), "renommer Total : " + why);
        check(q.programs.scripts[0].decls[2].calc.rfind("Somme := ", 0) == 0 && q.programs.scripts[1].body.find("Lu := Reglages.Somme;") != std::string::npos
                  && q.programs.scripts[0].body.find("Vu := Somme + Base2;") != std::string::npos,
              "son propre calcul, Reglages.Somme dans Usage, le corps du script\n" + q.programs.scripts[0].decls[2].calc + q.programs.scripts[1].body);
        // Dupliquer une valeur calculee : la copie ecrit son propre nom.
        std::vector<Id> copies;
        check(de::duplicate(q, at, de::Tab::Constants, {2}, &copies, &why) && q.programs.scripts[0].decls[3].name == "Somme_copie"
                  && q.programs.scripts[0].decls[3].computed && q.programs.scripts[0].decls[3].calc.rfind("Somme_copie := ", 0) == 0,
              "dupliquer Somme : Somme_copie ecrit Somme_copie - " + q.programs.scripts[0].decls[3].calc);
        check(de::remove(q, at, de::Tab::Constants, {3}, &why), "la copie retiree");
        // Une valeur ecrite dans la case : le calcul s'arrete (Ctrl+Z le rend).
        check(de::set(q, at, de::Tab::Constants, 3, de::Column::Value, "2.5", &why, &note) && !q.programs.scripts[0].decls[3].computed
                  && q.programs.scripts[0].decls[3].value == "2.5" && note.find("remplace le calcul") != std::string::npos,
              "une valeur ecrite remplace le calcul - " + note);
        // Le defaut de la MAP : une constante neuve (0.0) passee en MAP prend [] (avant : 0.0).
        Id made = kNoId;
        check(de::add(q, at, de::Tab::Constants, -1, "Table", &made, &why), "une constante neuve : " + why);
        const std::size_t last = de::rowsOf(q.programs.scripts[0].decls, de::Tab::Constants).size() - 1;
        note.clear();
        const bool typed = de::set(q, at, de::Tab::Constants, last, de::Column::Type, "MAP[STRING] OF INT", &why, &note);
        std::string tableValue = "?";
        for (const auto& d : q.programs.scripts[0].decls)
            if (d.name == "Table") tableValue = d.value;
        check(typed && tableValue == "[]", "passee en MAP : [] - " + tableValue + " " + note + " " + why);
        check(!de::valueMisfit(q, "MAP[STRING] OF INT", "0.0").empty() && de::valueMisfit(q, "MAP[STRING] OF INT", "['a' := 1]").empty()
                  && de::valueMisfit(q, "ARRAY[1..3] OF REAL", "0.0").empty(),
              "0.0 ne va pas dans une MAP ; ['a' := 1] oui ; un tableau prend toujours une valeur seule");
    }
    // Le fichier des scripts de vue (.xpgst) : le calcul et ses locales vont et reviennent.
    {
        View v;
        v.name = "Vue_Calcul";
        Script sc;
        sc.event = "OnOpen";
        sc.lang = ScriptLang::ST;
        sc.body = "x := Grille[1];\n";
        Declaration loc = declared(DeclKind::Variable, "k", "INT", "2");
        loc.description = "un pas";
        sc.decls = {computedDecl(DeclKind::Constant, "Grille", "ARRAY[1..3] OF INT", "FOR i := 1 TO 3 DO\n    Grille[i] := i * k;\nEND_FOR;\n", {loc})};
        v.scripts.push_back(sc);
        const auto f = hmi::scriptfile::fromView(v);
        const std::string text = hmi::scriptfile::write(f);
        hmi::scriptfile::File back;
        std::string why;
        const bool ok = hmi::scriptfile::read(text, back, &why);
        check(ok && back.entries.size() == 1 && back.entries[0].decls.size() == 1 && back.entries[0].decls[0].computed
                  && back.entries[0].decls[0].calc == sc.decls[0].calc && back.entries[0].decls[0].locals.size() == 1
                  && back.entries[0].decls[0].locals[0].name == "k" && back.entries[0].decls[0].locals[0].value == "2"
                  && back.entries[0].decls[0].locals[0].description == "un pas",
              "le .xpgst : le calcul et sa locale relus - " + why + "\n" + text);
    }
    // Le build incremental : Usage depend de Reglages (ses constantes publiques sont son interface).
    {
        Project q = calcProject();
        const auto els = hmi::pipeline::collect(q);
        const hmi::pipeline::Element* usage = nullptr;
        const hmi::pipeline::Element* reg = nullptr;
        for (const auto& e : els) {
            if (e.name == "Usage") usage = &e;
            if (e.name == "Reglages" && e.kind == hmi::pipeline::ElementKind::Script) reg = &e;
        }
        const bool dep = usage && reg && std::any_of(usage->deps.begin(), usage->deps.end(), [&](const hmi::pipeline::Dependency& d) {
                             return d.key == reg->key && d.mode == hmi::pipeline::DepMode::Interface;
                         });
        check(dep && reg->iface.find("Seuils : MAP[STRING] OF REAL = calcul ") != std::string::npos && reg->iface.find("Secret") == std::string::npos,
              "le build : Usage depend de l'interface de Reglages, qui dit ses constantes publiques (pas Secret) - " + (reg ? reg->iface : std::string("?")));
        Project r2 = calcProject();
        r2.programs.scripts[0].decls[2].calc = "Total := Seuils['bas'];\n";
        const auto els2 = hmi::pipeline::collect(r2);
        std::string iface2;
        for (const auto& e : els2)
            if (e.name == "Reglages" && e.kind == hmi::pipeline::ElementKind::Script) iface2 = e.iface;
        check(reg && iface2 != reg->iface, "un calcul change : l'interface de Reglages aussi (ceux qui la lisent se recompilent)");
    }
    // Une constante calculee n'est pas « une constante sans valeur » ; une fonction n'en a pas.
    {
        const auto f = dc::declarationFaults({computedDecl(DeclKind::Constant, "K", "INT", "K := 1;")}, dc::Role::Script, "");
        const auto g = dc::declarationFaults({computedDecl(DeclKind::Constant, "K", "INT", "K := 1;")}, dc::Role::Function, "");
        check(f[0].empty() && g[0].size() == 1 && g[0][0].find("qu'\xC3\xA0 un script") != std::string::npos,
              "une constante calculee : juste dans un script ; refusee dans une fonction");
    }
}
}

// ------------------------------------- 1.12.16 : les constantes et fonctions des vues ----
// Vue_Four : Seuil_Haut (ecrite, publique), Couleurs (MAP, privee), Paliers (calculee au demarrage, lit
// Seuil_Haut), Pourcent (publique, RETURN), CouleurNiveau (privee, lit Seuil_Haut et Couleurs) ; son
// script d'ouverture les lit par leur nom. Pop_Reglage : Pas (lit son parametre Cuve : a l'ouverture).
// Bilan (un script general) : Vue_Four.Pourcent(...), Vue_Four.Seuil_Haut, Vue_Four.Paliers[4].
Project vueProject() {
    Project p;
    HmiType cuve;
    cuve.id = p.allocate();
    cuve.name = "T_Cuve";
    cuve.members.push_back({"Capacite", "REAL", "", ""});
    cuve.members.push_back({"Consigne", "REAL", "", ""});
    p.programs.types.push_back(cuve);
    p.programs.variables.push_back(hmiVariable(p, "Niveau", "REAL"));
    p.programs.variables.back().initial = "50.0";
    for (const char* n : {"R1", "R2", "R3", "Pas_Lu"}) p.programs.variables.push_back(hmiVariable(p, n, "REAL"));
    p.programs.variables.push_back(hmiVariable(p, "C", "STRING"));
    p.programs.variables.push_back(hmiVariable(p, "Cuve_A", "T_Cuve"));
    p.programs.variables.back().initial = "(Capacite := 40.0, Consigne := 10.0)";
    View four = makeView(p, "Vue_Four");
    four.constants = {declared(DeclKind::Constant, "Seuil_Haut", "REAL", "85.0"),
                      declared(DeclKind::Constant, "Couleurs", "MAP[STRING] OF STRING", "['haut' := '#E5534B', 'ok' := '#2EA043']"),
                      computedDecl(DeclKind::Constant, "Paliers", "ARRAY[0..4] OF REAL",
                                   "FOR i := 0 TO 4 DO\n    Paliers[i] := Seuil_Haut / 4.0 * i;\nEND_FOR;\n")};
    four.constants[1].visibility = Visibility::Private;
    HmiFunction pourcent;
    pourcent.id = p.allocate();
    pourcent.name = "Pourcent";
    pourcent.returnType = "REAL";
    pourcent.body = "IF Max = 0.0 THEN\n    RETURN 0.0;\nEND_IF;\nRETURN LIMIT(0.0, Valeur * 100.0 / Max, 100.0);\n";
    pourcent.decls = {declared(DeclKind::Parameter, "Valeur", "REAL"), declared(DeclKind::Parameter, "Max", "REAL")};
    HmiFunction couleur;
    couleur.id = p.allocate();
    couleur.name = "CouleurNiveau";
    couleur.returnType = "STRING";
    couleur.visibility = Visibility::Private;
    couleur.body = "CouleurNiveau := SEL(N >= Seuil_Haut, Couleurs['ok'], Couleurs['haut']);\n";
    couleur.decls = {declared(DeclKind::Parameter, "N", "REAL")};
    four.functions = {pourcent, couleur};
    Script open;
    open.id = p.allocate();
    open.name = "Ouverture";
    open.event = "OnOpen";
    open.body = "R1 := Pourcent(Niveau, Seuil_Haut);\nC := CouleurNiveau(Niveau);\nR2 := Paliers[2];\n";
    four.scripts.push_back(open);
    Object txt;
    txt.id = p.allocate();
    txt.kind = Kind::Text;
    txt.name = "Txt_Pourcent";
    txt.props.push_back({"text", "", "Pourcent(Niveau, Seuil_Haut)"});
    txt.props.push_back({"textColor", "", "CouleurNiveau(Niveau)"});
    txt.props.push_back({"w", "", "Paliers[1] * 4.0"});
    txt.layer = four.layers.empty() ? kNoId : four.layers.front().id;
    four.objects.push_back(txt);
    p.views.push_back(four);
    View pop = makeView(p, "Pop_Reglage");
    pop.role = "popup";
    ViewParam prm;
    prm.name = "Cuve";
    prm.type = "T_Cuve";
    pop.params.push_back(prm);
    pop.constants = {computedDecl(DeclKind::Constant, "Pas", "REAL", "Pas := Cuve.Capacite / 20.0;\n")};
    Script popOpen;
    popOpen.id = p.allocate();
    popOpen.name = "Ouverture";
    popOpen.event = "OnOpen";
    popOpen.body = "Pas_Lu := Pas;\n";
    pop.scripts.push_back(popOpen);
    p.views.push_back(pop);
    View accueil = makeView(p, "Vue_Accueil");
    Object resume;
    resume.id = p.allocate();
    resume.kind = Kind::Text;
    resume.name = "Txt_Resume";
    resume.props.push_back({"text", "", "Vue_Four.Pourcent(Niveau, Vue_Four.Seuil_Haut)"});
    resume.layer = accueil.layers.empty() ? kNoId : accueil.layers.front().id;
    accueil.objects.push_back(resume);
    p.views.push_back(accueil);
    Script bilan;
    bilan.id = p.allocate();
    bilan.name = "Bilan";
    bilan.event = "Appel";
    bilan.body = "R1 := Vue_Four.Pourcent(Niveau, Vue_Four.Seuil_Haut);\nR2 := Vue_Four.Paliers[4];\nR3 := Vue_Four.Seuil_Haut;\n";
    p.programs.scripts.push_back(bilan);
    uniqueDeclarationIds(p);
    return p;
}

void vueConstantes11216() {
    std::printf("-- 1.12.16 : les constantes et fonctions des vues et des popups\n");
    const Project p = vueProject();
    const View& four = p.views[0];
    // Le disque : constantes_vue, la portee privee d'une fonction, calculee a l'ouverture - relus a l'identique.
    {
        Project q = p;
        q.views[1].constants[0].reopen = true;
        const auto files = serializeProject(q);
        std::string all;
        for (const auto& [name, data] : files) all += fileText(files, name);
        check(all.find("constantes_vue\n") != std::string::npos && all.find("portee=privee") != std::string::npos
                  && all.find("ouverture=oui") != std::string::npos && all.find("format=26") != std::string::npos,
              "sur disque : constantes_vue, portee=privee, ouverture=oui, format 26");
        const auto back = parseProject(readerOf(files));
        check(back.has_value() && back.value().views.size() == 3 && back.value().views[0].constants == q.views[0].constants
                  && back.value().views[0].functions == q.views[0].functions && back.value().views[1].constants == q.views[1].constants,
              "relu : les memes constantes et fonctions des vues");
        Project none = p;
        for (auto& v : none.views) { v.constants.clear(); v.functions.clear(); }
        std::string plain;
        const auto noneFiles = serializeProject(none);
        for (const auto& [name, data] : noneFiles) plain += fileText(noneFiles, name);
        check(plain.find("format=26") == std::string::npos, "sans elles : le format d'avant (une 1.12.15 l'ouvre)");
    }
    // Ce que la vue porte, et qui la lit.
    {
        viewmembers::Member m;
        std::string why;
        check(viewmembers::resolve(p, "Vue_Four.Seuil_Haut", m, &why) && m.constant && m.constant->name == "Seuil_Haut",
              "Vue_Four.Seuil_Haut : la constante publique");
        check(!viewmembers::resolve(p, "Vue_Four.Couleurs['ok']", m, &why) && why.find("priv\xC3\xA9" "e") != std::string::npos,
              "Vue_Four.Couleurs d'ailleurs : privee, dit pourquoi - " + why);
        check(viewmembers::resolve(p, "Vue_Four.Couleurs", m, &why, &four) && m.constant, "... la vue elle-meme la lit");
        check(viewmembers::resolve(p, "Vue_Four.Pourcent", m, &why) && m.functions.size() == 1, "Vue_Four.Pourcent : la fonction publique");
        check(!viewmembers::resolve(p, "Vue_Four.CouleurNiveau", m, &why) && why.find("l'appelle") != std::string::npos,
              "Vue_Four.CouleurNiveau d'ailleurs : privee - " + why);
        check(!viewmembers::resolve(p, "Vue_Four.Open", m, &why) && why.empty(), "Vue_Four.Open : pas un membre (une propriete publique)");
        const std::string q = viewmembers::qualify("x := Pourcent(Seuil_Haut, 'Seuil_Haut') + v.Seuil_Haut; (* Seuil_Haut *)\nSeuil_Haut2 := 1;", four);
        check(q == "x := Vue_Four.Pourcent(Vue_Four.Seuil_Haut, 'Seuil_Haut') + v.Seuil_Haut; (* Seuil_Haut *)\nSeuil_Haut2 := 1;",
              "qualifier le code de la vue : les noms seuls, ni chaines, ni commentaires, ni membres\n" + q);
        check(viewmembers::computedAtOpen(p, p.views[1], p.views[1].constants[0]) && !viewmembers::computedAtOpen(p, four, four.constants[2]),
              "Pas lit le parametre Cuve : a l'ouverture ; Paliers : au demarrage");
        std::string what;
        check(viewmembers::nameTaken(four, "seuil_haut", &what) && what == "la constante Seuil_Haut" && !viewmembers::nameTaken(four, "Autre"),
              "un nom pris : " + what);
    }
    // L'ordre des calculs : Paliers (au demarrage) ; Pas attend l'ouverture.
    {
        const auto pl = calc::plan(p);
        std::string order;
        for (const auto& s : pl.order) order += s.path + " ";
        check(pl.cycle.empty() && order == "Vue_Four.Paliers ", "le plan : Vue_Four.Paliers seule - " + order);
    }
    // Le moteur.
    {
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        std::string journal;
        for (const auto& e : rt.journal()) journal += e.source + " " + e.message + "\n";
        check(rt.running() && rt.startError().empty(), "demarre (" + rt.startError() + ")\n" + journal);
        check(rt.navigate(four.id, Transition{}, 0.1), "Vue_Four s'ouvre");
        const auto v = [&](const char* n) { const auto* x = rt.variable(n); return x ? x->display() : std::string("?"); };
        journal.clear();
        for (const auto& e : rt.journal()) journal += e.source + " " + e.message + "\n";
        const auto fx = [&](const View& view, const char* key) {
            for (const auto& pr : view.objects.front().props)
                if (pr.key == key)
                    if (auto r = rt.evaluatedProp(view.id, Kind::Text, pr)) return *r;
            return std::string("?");
        };
        check(fx(four, "text").rfind("58.82", 0) == 0 && fx(four, "textColor") == "#2EA043" && fx(four, "w") == "85",
              "les fx de la vue : Pourcent(...) = " + fx(four, "text") + ", CouleurNiveau(...) = " + fx(four, "textColor")
                  + ", Paliers[1] * 4.0 = " + fx(four, "w"));
        check(fx(p.views[2], "text").rfind("58.82", 0) == 0, "une fx ailleurs : Vue_Four.Pourcent(...) = " + fx(p.views[2], "text"));
        check(v("R1").rfind("58.82", 0) == 0 && v("C") == "'#2EA043'" && v("R2") == "42.5",
              "dans la vue, par leur nom : Pourcent(Niveau, Seuil_Haut) = " + v("R1") + ", CouleurNiveau(Niveau) = " + v("C")
                  + ", Paliers[2] = " + v("R2") + "\n" + journal);
        std::string why;
        const bool ok = rt.callScript("Bilan", 0.2, &why);
        check(ok && v("R1").rfind("58.82", 0) == 0 && v("R2") == "85" && v("R3") == "85",
              "ailleurs, par Vue_Four. : Pourcent = " + v("R1") + ", Paliers[4] = " + v("R2") + ", Seuil_Haut = " + v("R3") + " " + why);
        check(rt.openPopup(p.views[1].id, Transition{}, 0.3, "Cuve := Cuve_A", {}, kNoId), "Pop_Reglage s'ouvre (Cuve := Cuve_A)");
        journal.clear();
        for (const auto& e : rt.journal()) journal += e.source + " " + e.message + "\n";
        check(v("Pas_Lu") == "2", "Pas, calculee a l'ouverture, lit Cuve.Capacite : " + v("Pas_Lu") + "\n" + journal);
    }
    // Generer et Compiler : le projet est bon.
    {
        const auto said = [](const std::vector<Issue>& list) {
            std::string out;
            for (const auto& i : list)
                if (i.severity != Issue::Severity::Info) out += "  [" + std::string(toString(i.severity)) + "] " + i.category + " " + i.property + " l" + std::to_string(i.line) + " : " + i.message + "\n";
            return out;
        };
        const NameExists none = [](std::string_view) { return false; };   // pas d'automate : un nom inconnu est une faute
        const auto g = generate(p, none);
        const auto c = compileWith(p, none);
        check(count(g).errors == 0 && count(c).errors == 0, "Generer et Compiler : aucune erreur\n" + said(g) + "--\n" + said(c));
        // Ce qu'ils refusent : une privee lue d'ailleurs, une constante ecrite, un nom pris, un membre inconnu,
        // une constante du demarrage qui lit une de l'ouverture.
        Project q = vueProject();
        q.programs.scripts[0].body = "C := Vue_Four.Couleurs['ok'];\nC := Vue_Four.CouleurNiveau(1.0);\nVue_Four.Seuil_Haut := 2.0;\nR1 := Vue_Four.Inconnu;\n";
        q.views[0].scripts[0].body += "Seuil_Haut := 3.0;\n";
        q.views[2].objects.front().props.push_back({"textColor", "", "Vue_Four.Couleurs['ok']"});
        Object barre;
        barre.id = q.allocate();
        barre.kind = Kind::Rectangle;
        barre.name = "Paliers";
        barre.layer = q.views[0].layers.empty() ? kNoId : q.views[0].layers.front().id;
        q.views[0].objects.push_back(barre);
        q.views[1].constants.push_back(computedDecl(DeclKind::Constant, "Double", "REAL", "Double := Pas * 2.0;\n"));
        q.views[1].constants.back().reopen = false;
        q.views[1].constants[0].reopen = true;
        uniqueDeclarationIds(q);
        const auto g2 = generate(q, none);
        const auto c2 = compileWith(q, none);
        const std::string all = said(g2) + said(c2);
        const auto saidIt = [&](const char* part) { return all.find(part) != std::string::npos; };
        check(saidIt("Vue_Four.Couleurs est priv\xC3\xA9" "e : seule Vue_Four la lit"), "Vue_Four.Couleurs d'ailleurs : privee\n" + all);
        check(saidIt("Vue_Four.CouleurNiveau est priv\xC3\xA9" "e : seule Vue_Four l'appelle"), "Vue_Four.CouleurNiveau() d'ailleurs : privee");
        check(saidIt("Vue_Four.Seuil_Haut est une constante : elle ne s'\xC3\xA9" "crit pas"), "Vue_Four.Seuil_Haut := ... : refuse");
        check(saidIt("Seuil_Haut est une constante de la vue Vue_Four"), "Seuil_Haut := ... dans la vue : refuse");
        check(saidIt("Paliers : nom d\xC3\xA9j\xC3\xA0 pris dans la vue par l'objet Paliers"), "un objet du meme nom : refuse");
        check(saidIt("Inconnu"), "Vue_Four.Inconnu : dit");
        check(std::count_if(g2.begin(), g2.end(), [](const Issue& i) {
                  return i.category != "Script" && i.message.find("Vue_Four.Couleurs est priv") != std::string::npos;
              }) >= 1,
              "une fx d'une autre vue qui lit Vue_Four.Couleurs : privee aussi");
        check(saidIt("Double se calcule au d\xC3\xA9marrage : elle ne lit pas Pas"), "une du demarrage qui lit une de l'ouverture : refuse");
    }
    // Ecrire une constante de vue : refuse.
    {
        Project q = vueProject();
        q.programs.scripts[0].body = "Vue_Four.Seuil_Haut := 1.0;\n";
        Runtime rt;
        rt.bind(&q, nullptr);
        rt.start(0.0);
        std::string why;
        const bool ok = rt.callScript("Bilan", 0.1, &why);
        check(!ok && why.find("constante") != std::string::npos, "Vue_Four.Seuil_Haut := 1.0 : refuse - " + why);
    }
}
 // namespace


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
    modele();
    pont();
    suivi();
    edition();
    persistante();
    calculees();
    vueConstantes11216();                    // 1.12.16 : les constantes et fonctions des vues et des popups
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
