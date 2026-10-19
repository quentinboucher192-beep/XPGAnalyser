// =============================================================================
//  tests/hmi_natives1216_test.cpp - 1.12.16 : LES 39 NATIVES std::
// -----------------------------------------------------------------------------
//  Chaque native de la maquette 1.12.16 dans le moteur de l'IHM (un script ST) :
//  <cmath> (CEIL, FLOOR, FMOD, HYPOT, ATAN2, LERP, ISNAN, ISFINITE), <string> et
//  <format> (STARTS_WITH, ENDS_WITH, CONTAINS, TRIM, TRIM_LEFT, TRIM_RIGHT, FORMAT),
//  <chrono> (YEAR ... SECOND, WEEKDAY, ADD_DAYS, DAYS_BETWEEN, FORMAT_DT), les listes
//  et les statistiques (LIST_LOWER_BOUND, LIST_BINARY_SEARCH, LIST_TRANSFORM, LIST_MEDIAN,
//  LIST_VARIANCE, LIST_STDDEV, INTERPOLER_TABLE), les ensembles (SET_SYMMETRIC_DIFFERENCE,
//  SET_INCLUDES), RANDOM_NORMAL, <bit> (BYTESWAP, WORDSWAP) et <utility> (SWAP, EXCHANGE) :
//  le cas ordinaire, le bord, la faute dite (et attrapee par TRY). Puis une expression fx
//  et un texte a trous, Compiler (le nombre d'arguments, une constante ecrite, ce qui ne
//  va pas dans une expression, FORMAT et ses {}, SWAP et ses variables, LIST_TRANSFORM et
//  sa fonction), les valeurs calculees au demarrage et le catalogue.
//
//  NATIVES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiCalc.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiNatives.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/sim/DialectLib.hpp"
#include "../src/sim/Interpreter.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using namespace hmi;
namespace nt = hmi::natives;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("NATIVES_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

// Une fonction IHM du projet : nom, type rendu, corps (ses VAR_INPUT d'abord).
void addFunction(Project& p, const char* name, const char* returns, const char* body) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = returns;
    f.body = body;
    p.programs.functions.push_back(f);
}

// Les variables et les fonctions IHM du projet d'essai : R (le resultat), des variables simples, une liste,
// un tableau ; VersKelvin, Etiquette (un parametre), Moyenne (deux).
void fillProject(Project& p, const std::string& resultType) {
    const auto var = [&p](const char* n, const std::string& t, const char* init) {
        Variable v;
        v.id = p.allocate();
        v.name = n;
        v.type = t;
        v.initial = init;
        p.programs.variables.push_back(v);
    };
    var("R", resultType.empty() ? "STRING" : resultType, "");
    var("Niveau", "REAL", "42.5");
    var("Compteur", "INT", "7");
    var("Mot", "WORD", "16#1234");
    var("Texte", "STRING", "'Pompe 3'");
    var("ListeIhm", "LIST OF REAL", "[3.0, 1.0, 2.0]");
    var("TabIhm", "ARRAY[1..5] OF INT", "[10, 20, 30, 40, 50]");
    addFunction(p, "VersKelvin", "REAL", "VAR_INPUT c : REAL; END_VAR\nVersKelvin := c + 273.15;");
    addFunction(p, "Etiquette", "STRING", "VAR_INPUT x : REAL; END_VAR\nEtiquette := CONCAT('#', REAL_TO_STRING(x));");
    addFunction(p, "Moyenne", "REAL", "VAR_INPUT a : REAL; b : REAL; END_VAR\nMoyenne := (a + b) / 2.0;");
}

struct Bench {
    Project p;
    Runtime rt;
    Bench(const std::string& resultType, const std::string& body) {
        fillProject(p, resultType);
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = body;
        p.programs.scripts.push_back(s);
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
    }
    std::string run(std::string* error) {
        std::string err;
        const bool ok = rt.callScript("Essai", 0.2, &err);
        if (error) *error = ok ? std::string{} : err;
        sim::Value out;
        return rt.environment().read("R", out) ? shown(out) : std::string("#");
    }
};

// Les variables d'essai (des locales du script).
const char* kDecl =
    "VAR\n"
    "  Mesures : LIST OF REAL := [12.5, 8.0, 15.2, 8.0, 3.1];\n"
    "  Triee : LIST OF REAL := [10.0, 20.0, 30.0, 30.0, 40.0];\n"
    "  Vide : LIST OF REAL;\n"
    "  Carres : LIST OF REAL;\n"
    "  Noms : LIST OF STRING;\n"
    "  Entiers : LIST OF INT := [1, 2, 3];\n"
    "  Tab : ARRAY[1..5] OF INT := [10, 20, 30, 40, 50];\n"
    "  Pile : VECTOR OF STRING := ['a', 'b', 'c'];\n"
    "  Stats : LIST OF REAL := [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0];\n"
    "  Dernieres : RING[5] OF REAL := [9.0, 1.0, 3.0, 2.0, 4.0];\n"
    "  Xs : LIST OF REAL := [0.0, 10.0, 20.0];\n"
    "  Ys : LIST OF REAL := [0.0, 100.0, 200.0];\n"
    "  Ouvertures : ARRAY[0..3] OF REAL := [0.0, 25.0, 50.0, 100.0];\n"
    "  Debits : ARRAY[0..3] OF REAL := [0.0, 10.0, 40.0, 100.0];\n"
    "  A : SET OF STRING := ['P1', 'P2', 'P3'];\n"
    "  B : SET OF STRING := ['P2', 'P3', 'P4'];\n"
    "  VideS : SET OF STRING;\n"
    "  Sr : SET OF REAL;\n"
    "  P : TUPLE(INT, INT) := (1, 2);\n"
    "  Jour : DATE := D#2026-10-19;\n"
    "  Heure : TIME_OF_DAY := TOD#14:30:05;\n"
    "  Quand : DATE_AND_TIME := DT#2026-10-19-14:30:05;\n"
    "  Ia : INT := 1;\n"
    "  Ib : INT := 2;\n"
    "  Ra : REAL := 1.5;\n"
    "  i : DINT;\n"
    "  x : REAL;\n"
    "  y : REAL;\n"
    "  s : STRING;\n"
    "  s2 : STRING;\n"
    "  ok : BOOL;\n"
    "  w : WORD := 16#1234;\n"
    "  dw : DWORD := 16#12345678;\n"
    "  lw : ULINT := 16#0102030405060708;\n"
    "  n : INT := -2;\n"
    "  oc : BYTE := 16#12;\n"
    "  di : DINT := 16#1234;\n"
    "END_VAR\n"
    "VAR CONSTANT\n"
    "  Ca : INT := 4;\n"
    "  Cr : REAL := 1.0;\n"
    "  Fixes : LIST OF REAL := [1.0];\n"
    "END_VAR\n";

std::string run(const std::string& type, const std::string& body, std::string* err = nullptr) {
    Bench b(type, std::string(kDecl) + body);
    return b.run(err);
}

// Le resultat attendu (et pas d'erreur).
void same(const std::string& type, const std::string& body, const std::string& want, const std::string& what) {
    std::string err;
    const std::string got = run(type, body, &err);
    check(err.empty() && got == want, what + " : " + got + (err.empty() ? "" : " [" + err + "]") + ", attendu " + want);
}
// Une erreur qui dit `part`.
void fails(const std::string& body, const std::string& part, const std::string& what) {
    std::string err;
    (void)run("STRING", body, &err);
    check(!err.empty() && err.find(part) != std::string::npos, what + " : [" + err + "], attendu \xC2\xAB " + part + " \xC2\xBB");
}

// ------------------------------------------------------------------------- <cmath> ---
void maths() {
    std::printf("-- <cmath> : CEIL, FLOOR, FMOD, HYPOT, ATAN2, LERP, ISNAN, ISFINITE\n");
    same("REAL", "R := CEIL(2.1);", "3", "CEIL(2.1)");
    same("REAL", "R := CEIL(-2.1);", "-2", "CEIL(-2.1) : vers le haut");
    same("REAL", "R := FLOOR(2.9);", "2", "FLOOR(2.9)");
    same("REAL", "R := FLOOR(-2.1);", "-3", "FLOOR(-2.1) : vers le bas (TRUNC irait vers zero)");
    same("STRING", "R := TO_STRING(CEIL(Compteur) / 2) + ' ' + TO_STRING(FLOOR(Compteur) / 2);", "3 3", "CEIL et FLOOR d'un INT : un INT (7 / 2 = 3)");
    same("REAL", "R := FMOD(370.0, 360.0);", "10", "FMOD(370.0, 360.0)");
    same("REAL", "R := FMOD(-370.0, 360.0);", "-10", "FMOD : du signe de X");
    same("REAL", "R := FMOD(7.5, 2.0);", "1.5", "FMOD(7.5, 2.0)");
    same("REAL", "R := FMOD(370, 360);", "10", "FMOD de deux entiers : un REAL");
    fails("x := FMOD(1.0, 0.0);", "FMOD : division par z\xC3\xA9ro", "FMOD par 0 : une erreur, comme MOD");
    same("STRING", "TRY\n  x := FMOD(1.0, 0.0);\nCATCH s\n  R := s;\nEND_TRY", "FMOD : division par z\xC3\xA9ro", "FMOD par 0 dans un TRY : le CATCH a le message");
    same("REAL", "R := HYPOT(3.0, 4.0);", "5", "HYPOT(3, 4)");
    same("BOOL", "R := ABS(HYPOT(1.0E200, 1.0E200) / 1.0E200 - 1.41421356) < 1.0E-6;", "TRUE", "HYPOT sans debordement (1E200)");
    same("REAL", "R := ATAN2(1.0, -1.0);", "2.35619", "ATAN2 : le deuxieme quadrant (ATAN se tromperait)");
    same("REAL", "R := ATAN2(-1.0, -1.0);", "-2.35619", "ATAN2 : le troisieme quadrant");
    same("BOOL", "R := ABS(ATAN2(0.0, -1.0) - NUMBERS.PI) < 1.0E-12 AND ATAN2(1.0, 1.0) * 4.0 > 3.1415926;", "TRUE", "ATAN2(0, -1) = pi");
    same("REAL", "R := LERP(10.0, 20.0, 0.25);", "12.5", "LERP(10, 20, 0.25)");
    same("REAL", "R := LERP(10.0, 20.0, 1.0);", "20", "LERP a T = 1 : B exactement");
    same("REAL", "R := LERP(10.0, 20.0, 1.5);", "25", "LERP extrapole au-dela de 1");
    same("BOOL", "R := ISNAN(NUMERIC_LIMITS<REAL>.QUIET_NAN()) AND NOT ISNAN(1.0) AND NOT ISNAN(Compteur);", "TRUE", "ISNAN : NaN, un reel, un entier");
    same("BOOL", "R := ISFINITE(1.0 / 3.0) AND NOT ISFINITE(NUMERIC_LIMITS<REAL>.INFINITY()) AND NOT ISFINITE(NUMERIC_LIMITS<LREAL>.QUIET_NAN()) AND ISFINITE(Compteur);",
         "TRUE", "ISFINITE : un reel, l'infini, NaN, un entier");
    fails("x := CEIL('abc');", "CEIL : un nombre est attendu, pas 'abc'", "CEIL d'un texte");
    fails("ok := ISNAN(TRUE);", "ISNAN : un nombre est attendu, pas TRUE", "ISNAN d'un BOOL");
    fails("x := HYPOT(T#1s, 1.0);", "HYPOT : un nombre est attendu, pas T#1000ms", "HYPOT d'une duree");
    fails("x := FMOD(1.0);", "mauvais nombre d'arguments : FMOD(X, Y)", "FMOD a un argument");
    fails("x := CEIL(Mesures);", "CEIL : une valeur simple est attendue, pas un LIST OF REAL", "CEIL d'une liste");
}

// -------------------------------------------------------------- <string>, <format> ---
void textes() {
    std::printf("-- <string> et <format> : STARTS_WITH, ENDS_WITH, CONTAINS, TRIM, FORMAT\n");
    same("BOOL", "R := STARTS_WITH('E12-four', 'E12') AND NOT STARTS_WITH('E12-four', 'e12') AND STARTS_WITH('abc', '') AND NOT STARTS_WITH('ab', 'abc');",
         "TRUE", "STARTS_WITH : la casse compte, un debut vide, plus long que le texte");
    same("BOOL", "R := ENDS_WITH('releve.csv', '.csv') AND NOT ENDS_WITH('releve.csv', '.CSV') AND NOT ENDS_WITH('csv', 'x.csv');", "TRUE",
         "ENDS_WITH");
    same("BOOL", "R := CONTAINS('pompe en d\xC3\xA9" "faut', 'd\xC3\xA9" "faut') AND CONTAINS(Texte, 'Pompe') AND NOT CONTAINS(Texte, 'pompe');", "TRUE",
         "CONTAINS (accents compris, la casse compte)");
    same("STRING", "R := CONCAT('[', TRIM('  Four 2  '), ']');", "[Four 2]", "TRIM");
    same("STRING", "R := CONCAT('[', TRIM('$T a b $N'), ']');", "[a b]", "TRIM : tabulations et retours a la ligne");
    same("STRING", "R := CONCAT('[', TRIM_LEFT('  a '), ']') + CONCAT('[', TRIM_RIGHT('  a '), ']');", "[a ][  a]", "TRIM_LEFT, TRIM_RIGHT");
    same("STRING", "R := CONCAT('[', TRIM('   '), ']');", "[]", "TRIM d'espaces seuls : un texte vide");
    fails("ok := STARTS_WITH(5, 'a');", "STARTS_WITH : un texte est attendu, pas 5", "STARTS_WITH d'un nombre");
    // FORMAT
    same("STRING", "R := FORMAT('{:.1f} bar au poste {:02}', 2.345, 7);", "2.3 bar au poste 07", "FORMAT : l'exemple de la fiche");
    same("STRING", "R := FORMAT('{} + {} = {}', 2, 2.5, 'quatre et demi');", "2 + 2.5 = quatre et demi", "FORMAT : {} prend la valeur suivante");
    same("STRING", "R := FORMAT('{}|{}|{}', TRUE, T#1500ms, Jour);", "TRUE|T#1500ms|D#2026-10-19", "FORMAT d'un BOOL, d'une duree, d'une date");
    same("STRING", "R := FORMAT('[{:5}][{:<5}][{:>5}][{:^5}]', 42, 42, 'ab', 'ab');", "[   42][42   ][   ab][ ab  ]", "FORMAT : largeur et alignement");
    same("STRING", "R := FORMAT('[{:*^7}][{:5}]', 'ab', 'ab');", "[**ab***][ab   ]", "FORMAT : remplissage ; un texte a gauche par defaut");
    same("STRING", "R := FORMAT('{:05}|{:05}|{:05.1f}', 42, -42, 3.14159);", "00042|-0042|003.1", "FORMAT {:05} : des zeros devant (apres le signe)");
    same("STRING", "R := FORMAT('{:x}|{:X}|{:#x}|{:b}|{:o}|{:x}', 255, 255, 255, 5, 8, Mot);", "ff|FF|0xff|101|10|1234", "FORMAT : hexadecimal, binaire, octal");
    same("STRING", "R := FORMAT('{:+}|{:+.1f}|{: }', 5, 2.26, 7);", "+5|+2.3| 7", "FORMAT : le signe");
    same("STRING", "R := FORMAT('{{{}}} {{ok}}', 3);", "{3} {ok}", "FORMAT : {{ et }}");
    same("STRING", "R := FORMAT('{:.2f}|{:e}|{:.3}', 7, 1234.5, 3.14159);", "7.00|1.234500e+03|3.14", "FORMAT : un entier en {:.2f}, {:e}, {:.3}");
    same("STRING", "R := FORMAT('[{:6}][{:.2}]', '\xC3\xA9t\xC3\xA9', '\xC3\xA9t\xC3\xA9');", "[\xC3\xA9t\xC3\xA9   ][\xC3\xA9t]",
         "FORMAT compte des caracteres, pas des octets (UTF-8)");
    same("STRING", "R := FORMAT('sans trou');", "sans trou", "FORMAT sans {} ni valeur");
    fails("s := FORMAT('{} et {}', 1);", "FORMAT : 2 {} pour 1 valeur", "FORMAT : plus de {} que de valeurs");
    fails("s := FORMAT('{}', 1, 2);", "FORMAT : 1 {} pour 2 valeurs", "FORMAT : plus de valeurs que de {}");
    fails("s := FORMAT('{:q}', 1);", "FORMAT : {:q} : format inconnu", "FORMAT : un format inconnu");
    fails("s := FORMAT('{0}', 1);", "FORMAT : {0} \xE2\x80\x94 {} prend la valeur suivante", "FORMAT : {0} (un numero)");
    fails("s := FORMAT('a } b');", "FORMAT : une } seule", "FORMAT : une } seule");
    fails("s := FORMAT('a { b');", "FORMAT : une { sans }", "FORMAT : une { sans }");
    fails("s := FORMAT('{:x}', 2.5);", "FORMAT : {:x} : va avec un entier, pas 2.5", "FORMAT {:x} d'un reel");
    fails("s := FORMAT('{:d}', 'abc');", "FORMAT : {:d} : va avec un nombre, pas 'abc'", "FORMAT {:d} d'un texte");
    fails("s := FORMAT('{:.2}', 7);", "FORMAT : {:.2} : la pr\xC3\xA9" "cision va avec un r\xC3\xA9" "el", "FORMAT {:.2} d'un entier");
    fails("s := FORMAT(5, 1);", "FORMAT : un texte est attendu, pas 5", "FORMAT : le modele est un texte");
}

// ------------------------------------------------------------------------ <chrono> ---
void dates() {
    std::printf("-- <chrono> : YEAR ... SECOND, WEEKDAY, ADD_DAYS, DAYS_BETWEEN, FORMAT_DT\n");
    same("STRING", "R := FORMAT('{}-{}-{} {}:{}:{}', YEAR(Quand), MONTH(Quand), DAY(Quand), HOUR(Quand), MINUTE(Quand), SECOND(Quand));",
         "2026-10-19 14:30:5", "YEAR, MONTH, DAY, HOUR, MINUTE, SECOND d'un DATE_AND_TIME");
    same("STRING", "R := FORMAT('{}/{}/{}', DAY(Jour), MONTH(Jour), YEAR(Jour));", "19/10/2026", "DAY, MONTH, YEAR d'une DATE");
    same("STRING", "R := FORMAT('{}:{}:{}', HOUR(Heure), MINUTE(Heure), SECOND(Heure));", "14:30:5", "HOUR, MINUTE, SECOND d'un TIME_OF_DAY");
    same("INT", "R := HOUR(TOD#23:59:59.999) * 100 + SECOND(TOD#23:59:59.999);", "2359", "les millisecondes ne comptent pas");
    same("STRING", "R := FORMAT('{}{}{}{}', WEEKDAY(D#2026-10-19), WEEKDAY(D#2026-10-25), WEEKDAY(D#1970-01-01), WEEKDAY(DT#1969-12-31-23:00:00));",
         "1743", "WEEKDAY ISO : lundi 1, dimanche 7, le 1970-01-01 un jeudi, le 1969-12-31 un mercredi");
    same("STRING", "R := TO_STRING(ADD_DAYS(Quand, 14));", "DT#2026-11-02-14:30:05", "ADD_DAYS d'un DT : un DT, a la meme heure");
    same("STRING", "R := TO_STRING(ADD_DAYS(D#2024-02-28, 1)) + ' ' + TO_STRING(ADD_DAYS(D#2023-02-28, 1));", "D#2024-02-29 D#2023-03-01",
         "ADD_DAYS d'une DATE : une DATE (2024 bissextile)");
    same("STRING", "R := TO_STRING(ADD_DAYS(D#2027-01-01, -1));", "D#2026-12-31", "ADD_DAYS en arriere");
    same("DINT", "R := DAYS_BETWEEN(DT#2026-10-01-08:00:00, DT#2026-10-19-14:30:05);", "18", "DAYS_BETWEEN : des jours entiers");
    same("DINT", "R := DAYS_BETWEEN(DT#2026-10-19-14:30:05, DT#2026-10-01-08:00:00);", "-18", "DAYS_BETWEEN : B avant A, negatif");
    same("DINT", "R := DAYS_BETWEEN(D#2026-01-01, DT#2026-12-31-23:59:59);", "364", "DAYS_BETWEEN d'une DATE a un DT");
    same("BOOL", "Quand := ADD_DAYS(Quand, 1000);\nR := DAYS_BETWEEN(DT#2026-10-19-14:30:05, Quand) = 1000;", "TRUE",
         "DAYS_BETWEEN au-dela de 49 jours (pas un TIME)");
    same("STRING", "R := FORMAT_DT(Quand, '%d/%m/%Y %H:%M:%S');", "19/10/2026 14:30:05", "FORMAT_DT : %d %m %Y %H %M %S");
    same("STRING", "R := FORMAT_DT(Quand, '%A %d %B, %H:%M');", "lundi 19 octobre, 14:30", "FORMAT_DT : l'exemple de la fiche");
    same("STRING", "R := FORMAT_DT(Quand, '%a %y %b %%');", "lun. 26 oct. %", "FORMAT_DT : %a %y %b %%");
    same("STRING", "R := FORMAT_DT(D#2026-02-05, '%A %B %b') + '|' + FORMAT_DT(D#2026-08-15, '%A %B %b') + '|' + FORMAT_DT(D#2026-12-25, '%A %B %b');",
         "jeudi f\xC3\xA9vrier f\xC3\xA9vr.|samedi ao\xC3\xBBt ao\xC3\xBBt|vendredi d\xC3\xA9" "cembre d\xC3\xA9" "c.", "FORMAT_DT : les jours et les mois en francais");
    same("STRING", "R := FORMAT_DT(Heure, '%Hh%M');", "14h30", "FORMAT_DT d'un TIME_OF_DAY");
    same("STRING", "R := FORMAT_DT(Jour, '%d.%m.%y %H:%M');", "19.10.26 00:00", "FORMAT_DT d'une DATE : minuit");
    fails("s := FORMAT_DT(Heure, '%d');", "FORMAT_DT : %d : un TIME_OF_DAY n'a pas de date", "FORMAT_DT : %d d'une heure");
    fails("s := FORMAT_DT(Quand, '%Q');", "FORMAT_DT : %Q inconnu (%d %m %Y %y %H %M %S %A %a %B %b %%)", "FORMAT_DT : un code inconnu");
    fails("s := FORMAT_DT(Quand, 'fin %');", "FORMAT_DT : un % seul", "FORMAT_DT : un % a la fin");
    fails("i := YEAR(Heure);", "YEAR : une date est attendue (DATE_AND_TIME, DATE), pas TOD#14:30:05", "YEAR d'une heure");
    fails("i := HOUR(Jour);", "HOUR : une heure est attendue (DATE_AND_TIME, TIME_OF_DAY), pas D#2026-10-19", "HOUR d'une date");
    fails("i := WEEKDAY(5);", "WEEKDAY : une date est attendue (DATE_AND_TIME, DATE), pas 5", "WEEKDAY d'un nombre");
    fails("Quand := ADD_DAYS(Quand, 1.5);", "ADD_DAYS : N est un nombre entier de jours, pas 1.5", "ADD_DAYS de 1.5 jour");
    fails("Heure := ADD_DAYS(Heure, 1);", "ADD_DAYS : une date est attendue (DATE_AND_TIME, DATE)", "ADD_DAYS d'une heure");
    fails("Jour := ADD_DAYS(D#9999-12-31, 1);", "ADD_DAYS : la date sort du calendrier", "ADD_DAYS au-dela de l'an 9999");
}

// ------------------------------------------------------- les listes et les statistiques ---
void listes() {
    std::printf("-- les listes : LIST_LOWER_BOUND, LIST_BINARY_SEARCH, LIST_MEDIAN, LIST_VARIANCE, LIST_STDDEV, LIST_TRANSFORM\n");
    same("STRING", "R := FORMAT('{} {} {} {} {}', LIST_LOWER_BOUND(Triee, 30.0), LIST_LOWER_BOUND(Triee, 25.0), LIST_LOWER_BOUND(Triee, 5.0), "
                   "LIST_LOWER_BOUND(Triee, 99.0), LIST_LOWER_BOUND(Vide, 1.0));",
         "2 2 0 5 0", "LIST_LOWER_BOUND : la premiere des egales, la place, avant tout, aucun (L.Count), une liste vide");
    same("STRING", "R := FORMAT('{} {}', LIST_LOWER_BOUND(Tab, 30), LIST_LOWER_BOUND(Tab, 99));", "3 6", "LIST_LOWER_BOUND d'un ARRAY[1..5] : a ses indices");
    same("STRING", "R := TO_STRING(LIST_BINARY_SEARCH(Triee, 30.0)) + TO_STRING(LIST_BINARY_SEARCH(Triee, 25.0)) + TO_STRING(LIST_BINARY_SEARCH(Vide, 1.0));",
         "TRUEFALSEFALSE", "LIST_BINARY_SEARCH");
    same("BOOL", "R := LIST_BINARY_SEARCH(Pile, 'b') AND LIST_BINARY_SEARCH(Tab, 40);", "TRUE", "LIST_BINARY_SEARCH : des textes, un ARRAY");
    fails("i := LIST_LOWER_BOUND(Triee, 'abc');", "LIST_LOWER_BOUND : 'abc' n'est pas un REAL", "LIST_LOWER_BOUND d'un texte dans des REAL");
    fails("ok := LIST_BINARY_SEARCH(5, 1);", "LIST_BINARY_SEARCH : une LIST, un VECTOR ou un ARRAY est attendu", "LIST_BINARY_SEARCH d'un nombre");
    same("REAL", "R := LIST_MEDIAN(Dernieres);", "3", "LIST_MEDIAN d'un RING (impair) : la valeur du milieu");
    same("REAL", "R := LIST_MEDIAN(Stats);", "4.5", "LIST_MEDIAN (pair) : la moyenne des deux du milieu");
    same("REAL", "R := LIST_MEDIAN(Tab);", "30", "LIST_MEDIAN d'un ARRAY d'entiers");
    same("STRING", "x := LIST_MEDIAN(Mesures);\nR := JOIN(Mesures, ';');", "12.5;8;15.2;8;3.1", "LIST_MEDIAN ne range pas la liste");
    fails("x := LIST_MEDIAN(Vide);", "LIST_MEDIAN : la liste est vide", "LIST_MEDIAN d'une liste vide");
    fails("x := LIST_MEDIAN(Pile);", "LIST_MEDIAN : des nombres sont attendus, pas un VECTOR OF STRING", "LIST_MEDIAN de textes");
    same("REAL", "R := LIST_VARIANCE(Stats);", "4", "LIST_VARIANCE : divisee par n");
    same("REAL", "R := LIST_VARIANCE(Stats, TRUE);", "4.57143", "LIST_VARIANCE d'un echantillon : divisee par n - 1");
    same("REAL", "R := LIST_STDDEV(Stats);", "2", "LIST_STDDEV");
    same("REAL", "R := LIST_STDDEV(Stats, TRUE);", "2.13809", "LIST_STDDEV d'un echantillon");
    same("REAL", "Mesures := [5.0];\nR := LIST_VARIANCE(Mesures) + LIST_STDDEV(Mesures, FALSE);", "0", "une seule valeur : 0");
    fails("Mesures := [5.0];\nx := LIST_STDDEV(Mesures, TRUE);", "LIST_STDDEV : un \xC3\xA9" "chantillon d'une seule valeur", "un echantillon d'une valeur");
    fails("x := LIST_VARIANCE(Vide);", "LIST_VARIANCE : la liste est vide", "LIST_VARIANCE d'une liste vide");
    fails("x := LIST_VARIANCE(Stats, 1);", "LIST_VARIANCE : TRUE ou FALSE est attendu, pas 1", "LIST_VARIANCE : ECHANTILLON est un BOOL");
    // LIST_TRANSFORM
    same("STRING", "i := LIST_TRANSFORM(Entiers, Carres, TRANSFORMATION#Carre);\nR := TO_STRING(i) + ' ' + JOIN(Carres, ',');", "3 1,4,9",
         "LIST_TRANSFORM par TRANSFORMATION#Carre : rend le nombre");
    same("STRING", "Carres := [7.0];\ni := LIST_TRANSFORM(Mesures, Carres, TRANSFORMATION#Neg);\nR := JOIN(Carres, ',');", "-12.5,-8,-15.2,-8,-3.1",
         "LIST_TRANSFORM vide d'abord la destination");
    same("STRING", "i := LIST_TRANSFORM(Xs, Xs, TRANSFORMATION#Racine);\nR := JOIN(Xs, ',');", "0,3.16228,4.47214", "LIST_TRANSFORM d'une liste dans elle-meme");
    same("STRING", "i := LIST_TRANSFORM(Tab, Carres, TRANSFORMATION#Inverse);\nR := JOIN(Carres, ';');", "0.1;0.05;0.0333333;0.025;0.02",
         "LIST_TRANSFORM d'un ARRAY");
    same("STRING", "Mesures := [0.0, 100.0];\ni := LIST_TRANSFORM(Mesures, Carres, 'VersKelvin');\nR := TO_STRING(i) + ' ' + JOIN(Carres, ';');",
         "2 273.15;373.15", "LIST_TRANSFORM par une fonction du projet (son nom)");
    same("STRING", "i := LIST_TRANSFORM(Mesures, Noms, 'Etiquette');\nR := JOIN(Noms, ';');", "#12.5;#8;#15.2;#8;#3.1",
         "LIST_TRANSFORM : une fonction qui rend un texte, vers une LIST OF STRING");
    same("STRING", "s := 'VersKelvin';\ni := LIST_TRANSFORM(Xs, Carres, s);\nR := JOIN(Carres, ';');", "273.15;283.15;293.15", "LIST_TRANSFORM : OP dans une variable");
    same("STRING", "FUNCTION Double(v : REAL) : REAL\n  Double := v * 2.0;\nEND_FUNCTION\ni := LIST_TRANSFORM(Xs, Carres, 'Double');\nR := JOIN(Carres, ',');", "0,20,40",
         "LIST_TRANSFORM par une fonction interne du script");
    same("STRING", "i := LIST_TRANSFORM(Xs, ListeIhm, TRANSFORMATION#Carre);\nR := JOIN(ListeIhm, ',');", "0,100,400", "LIST_TRANSFORM vers une liste IHM (en place)");
    fails("i := LIST_TRANSFORM(Xs, Carres, 'Versklvin');", "LIST_TRANSFORM : Versklvin n'est pas une fonction du projet", "LIST_TRANSFORM : une fonction inconnue");
    fails("i := LIST_TRANSFORM(Xs, Tab, TRANSFORMATION#Carre);", "LIST_TRANSFORM : un ARRAY a une taille fixe (5 cases)", "LIST_TRANSFORM vers un ARRAY");
    fails("i := LIST_TRANSFORM(Xs, Carres, OPERATION#Somme);",
          "LIST_TRANSFORM : OP est une TRANSFORMATION (TRANSFORMATION#Carre\xE2\x80\xA6) ou le nom d'une fonction du projet ('VersKelvin'), pas OPERATION#Somme",
          "LIST_TRANSFORM avec une OPERATION");
    fails("i := LIST_TRANSFORM(Xs, Carres, 9);", "LIST_TRANSFORM : OP est une TRANSFORMATION", "LIST_TRANSFORM avec 9");
    fails("i := LIST_TRANSFORM(Pile, Carres, TRANSFORMATION#Carre);", "LIST_TRANSFORM : une TRANSFORMATION calcule sur des nombres, pas sur un VECTOR OF STRING",
          "LIST_TRANSFORM de textes par une TRANSFORMATION");
    fails("i := LIST_TRANSFORM(Mesures, Entiers, TRANSFORMATION#Identite);", "LIST_TRANSFORM : 12.5 n'est pas un entier (INT)", "LIST_TRANSFORM : un REAL dans des INT");
    fails("i := LIST_TRANSFORM(Xs, Fixes, TRANSFORMATION#Carre);", "LIST_TRANSFORM : une constante ne s'\xC3\xA9" "crit pas", "LIST_TRANSFORM vers une constante");
    fails("i := LIST_TRANSFORM(Xs, Sr, TRANSFORMATION#Carre);", "LIST_TRANSFORM : un SET se range tout seul", "LIST_TRANSFORM vers un SET");
    same("STRING", "i := LIST_TRANSFORM(Fixes, Carres, TRANSFORMATION#Carre);\nR := JOIN(Carres, ',');", "1", "LIST_TRANSFORM lit une constante");
}

// ----------------------------------------------------------------- INTERPOLER_TABLE ---
void interpoler() {
    std::printf("-- INTERPOLER_TABLE\n");
    same("REAL", "R := INTERPOLER_TABLE(Xs, Ys, 15.0);", "150", "INTERPOLER_TABLE : entre deux points");
    same("STRING", "R := FORMAT('{} {} {} {} {}', INTERPOLER_TABLE(Xs, Ys, 0.0), INTERPOLER_TABLE(Xs, Ys, 10.0), INTERPOLER_TABLE(Xs, Ys, 20.0), "
                   "INTERPOLER_TABLE(Xs, Ys, -5.0), INTERPOLER_TABLE(Xs, Ys, 99.0));",
         "0 100 200 0 200", "INTERPOLER_TABLE : les points exacts, avant le premier, apres le dernier");
    same("STRING", "R := FORMAT('{} {}', INTERPOLER_TABLE(Ouvertures, Debits, 75.0), INTERPOLER_TABLE(Ouvertures, Debits, 30.0));", "70 16",
         "INTERPOLER_TABLE d'une courbe en ARRAY, a pas irreguliers");
    same("REAL", "Xs := [5.0];\nYs := [42.0];\nR := INTERPOLER_TABLE(Xs, Ys, 1000.0);", "42", "INTERPOLER_TABLE d'un seul point");
    fails("Ys := [1.0];\nx := INTERPOLER_TABLE(Xs, Ys, 1.0);", "INTERPOLER_TABLE : X a 3 valeurs, Y en a 1 \xE2\x80\x94 il en faut autant",
          "INTERPOLER_TABLE : X et Y pas de la meme taille");
    fails("Xs := [0.0, 20.0, 10.0];\nx := INTERPOLER_TABLE(Xs, Ys, 1.0);", "INTERPOLER_TABLE : X doit cro\xC3\xAEtre : X[2] = 10 ne d\xC3\xA9passe pas X[1] = 20",
          "INTERPOLER_TABLE : X qui ne croit pas");
    fails("x := INTERPOLER_TABLE(Debits, Ys, 1.0);", "INTERPOLER_TABLE : X a 4 valeurs, Y en a 3", "INTERPOLER_TABLE : un ARRAY et une LIST de tailles differentes");
    fails("x := INTERPOLER_TABLE(Vide, Vide, 1.0);", "INTERPOLER_TABLE : la table est vide", "INTERPOLER_TABLE vide");
    fails("x := INTERPOLER_TABLE(Xs, Ys, NUMERIC_LIMITS<REAL>.QUIET_NAN());", "INTERPOLER_TABLE : V n'est pas un nombre (NaN)", "INTERPOLER_TABLE de NaN");
    fails("x := INTERPOLER_TABLE(Xs, Pile, 1.0);", "INTERPOLER_TABLE : des nombres sont attendus, pas un VECTOR OF STRING", "INTERPOLER_TABLE de textes");
}

// --------------------------------------------------------------------- les ensembles ---
void ensembles() {
    std::printf("-- les ensembles : SET_SYMMETRIC_DIFFERENCE, SET_INCLUDES\n");
    same("STRING", "R := JOIN(SET_SYMMETRIC_DIFFERENCE(A, B), ',');", "P1,P4", "SET_SYMMETRIC_DIFFERENCE : l'exemple de la fiche");
    same("STRING", "R := JOIN(SET_SYMMETRIC_DIFFERENCE(A, VideS), ',') + ' ' + TO_STRING(SET_SYMMETRIC_DIFFERENCE(A, A).Count);", "P1,P2,P3 0",
         "SET_SYMMETRIC_DIFFERENCE d'un ensemble vide, de lui-meme");
    same("STRING", "Pile := ['P9', 'P1', 'P1'];\nR := JOIN(SET_SYMMETRIC_DIFFERENCE(A, Pile), ',');", "P2,P3,P9",
         "SET_SYMMETRIC_DIFFERENCE d'une liste (rangee, sans doublon)");
    same("STRING", "R := TO_STRING(SET_INCLUDES(A, B)) + TO_STRING(SET_INCLUDES(A, VideS)) + TO_STRING(SET_INCLUDES(VideS, A));", "FALSETRUEFALSE",
         "SET_INCLUDES : pas toutes, un B vide, un A vide");
    same("BOOL", "Pile := ['P3', 'P1'];\nR := SET_INCLUDES(A, Pile);", "TRUE", "SET_INCLUDES : chaque valeur de B est dans A");
    fails("ok := SET_INCLUDES(A, 5);", "SET_INCLUDES : un SET est attendu (ou une LIST, un ARRAY), pas un nombre", "SET_INCLUDES d'un nombre");
}

// ---------------------------------------------------------------------- RANDOM_NORMAL ---
void hasard() {
    std::printf("-- RANDOM_NORMAL\n");
    same("STRING", "RANDOM_SEED(42);\nx := RANDOM_NORMAL(10.0, 2.0);\ny := RANDOM_NORMAL(10.0, 2.0);\nRANDOM_SEED(42);\n"
                   "R := TO_STRING(x = RANDOM_NORMAL(10.0, 2.0)) + TO_STRING(y = RANDOM_NORMAL(10.0, 2.0)) + TO_STRING(x <> y);",
         "TRUETRUETRUE", "RANDOM_SEED rejoue RANDOM_NORMAL");
    same("STRING", "RANDOM_SEED(42);\nx := RANDOM_NORMAL(0.0, 1.0);\ny := RANDOM();\nRANDOM_SEED(42);\nx := RANDOM();\nx := RANDOM();\nR := TO_STRING(y = RANDOM());",
         "TRUE", "RANDOM_NORMAL prend deux tirages de la suite de RANDOM (la troisieme valeur suit)");
    same("REAL", "R := RANDOM_NORMAL(5.0, 0.0);", "5", "RANDOM_NORMAL d'ecart 0 : la moyenne");
    fails("x := RANDOM_NORMAL(1.0, -1.0);", "RANDOM_NORMAL : l'\xC3\xA9" "cart type est positif ou nul, pas -1", "RANDOM_NORMAL d'ecart negatif");
    // Sur 20000 tirages : la moyenne et l'ecart type a peu pres (a quelques ecarts de l'estimation).
    nt::seedRandom(123);
    double sum = 0.0, squares = 0.0;
    const int draws = 20000;
    bool all = true;
    for (int k = 0; k < draws; ++k) {
        sim::Value v;
        all = nt::call("RANDOM_NORMAL", {sim::Value::real(10.0), sim::Value::real(2.0)}, v) && all;
        sum += v.asReal();
        squares += v.asReal() * v.asReal();
    }
    const double mean = sum / draws, deviation = std::sqrt(squares / draws - mean * mean);
    char buf[96];
    std::snprintf(buf, sizeof buf, " (moyenne %.4f, ecart type %.4f)", mean, deviation);
    check(all && std::fabs(mean - 10.0) < 0.05 && std::fabs(deviation - 2.0) < 0.05, std::string("RANDOM_NORMAL(10, 2) sur 20000 tirages") + buf);
    nt::seedRandom(7);
    sim::Value a, b;
    (void)nt::call("RANDOM_NORMAL", {sim::Value::real(0.0), sim::Value::real(1.0)}, a);
    nt::seedRandom(7);
    (void)nt::call("RANDOM_NORMAL", {sim::Value::real(0.0), sim::Value::real(1.0)}, b);
    check(a.asReal() == b.asReal() && std::isfinite(a.asReal()), "RANDOM_NORMAL : la meme graine, la meme valeur (" + shown(a) + ")");
    std::string why;
    check(!nt::call("RANDOM_NORMAL", {sim::Value::text("a"), sim::Value::real(1.0)}, a, &why) && why.find("un nombre est attendu, pas 'a'") != std::string::npos,
          "RANDOM_NORMAL('a', 1.0) : " + why);
    check(!nt::call("RANDOM_NORMAL", {sim::Value::real(1.0)}, a, &why) && why.find("2 argument(s) attendu(s), 1") != std::string::npos,
          "RANDOM_NORMAL(1.0) : " + why);
}

// -------------------------------------------------------------------------- <bit> ---
void bits() {
    std::printf("-- <bit> : BYTESWAP, WORDSWAP\n");
    same("WORD", "R := BYTESWAP(16#1234);", "13330", "BYTESWAP(16#1234) = 16#3412 (un litteral : 16 bits)");
    same("WORD", "R := BYTESWAP(w);", "13330", "BYTESWAP d'un WORD");
    same("WORD", "R := BYTESWAP(Mot);", "13330", "BYTESWAP d'une variable IHM WORD");
    same("UDINT", "R := BYTESWAP(dw);", "2018915346", "BYTESWAP d'un DWORD : 16#78563412");
    same("UDINT", "R := BYTESWAP(16#12345678);", "2018915346", "BYTESWAP d'un litteral de 32 bits");
    same("INT", "R := BYTESWAP(n);", "-257", "BYTESWAP d'un INT (-2 = 16#FFFE : 16#FEFF)");
    same("DINT", "R := BYTESWAP(di);", "873594880", "BYTESWAP d'un DINT : ses 32 bits (16#34120000)");
    same("BOOL", "R := BYTESWAP(lw) = 16#0807060504030201;", "TRUE", "BYTESWAP d'un ULINT : ses 8 octets");
    same("STRING", "R := FORMAT('{:x}', BYTESWAP(BYTESWAP(dw)));", "12345678", "BYTESWAP deux fois : la valeur");
    fails("i := BYTESWAP(oc);", "BYTESWAP : un BYTE n'a qu'un octet (rien \xC3\xA0 inverser)", "BYTESWAP d'un BYTE");
    fails("i := BYTESWAP(2.5);", "BYTESWAP : un mot ou un entier est attendu (WORD, DWORD, INT, DINT, UINT, UDINT, LWORD), pas REAL", "BYTESWAP d'un reel");
    same("DWORD", "R := WORDSWAP(16#12345678);", "1450709556", "WORDSWAP(16#12345678) = 16#56781234");
    same("DWORD", "R := WORDSWAP(dw);", "1450709556", "WORDSWAP d'un DWORD");
    same("BOOL", "R := WORDSWAP(lw) = 16#0708050603040102;", "TRUE", "WORDSWAP d'un ULINT : ses quatre mots a l'envers");
    same("DWORD", "R := WORDSWAP(16#1234);", "305397760", "WORDSWAP d'un petit litteral : 32 bits (16#12340000)");
    fails("i := WORDSWAP(w);", "WORDSWAP : un entier de 32 bits (ou 64) est attendu (DWORD, DINT, UDINT, LWORD), pas un WORD", "WORDSWAP d'un WORD");
}

// ---------------------------------------------------------------------- <utility> ---
void utility() {
    std::printf("-- <utility> : SWAP, EXCHANGE\n");
    same("INT", "SWAP(Ia, Ib);\nR := Ia * 10 + Ib;", "21", "SWAP de deux INT");
    same("STRING", "s := 'gauche';\ns2 := 'droite';\nok := SWAP(s, s2);\nR := s + ' ' + s2 + ' ' + TO_STRING(ok);", "droite gauche TRUE",
         "SWAP de deux textes, rend TRUE");
    same("REAL", "x := 3.5;\nSWAP(x, Niveau);\nR := x * 100.0 + Niveau;", "4253.5", "SWAP d'une locale et d'une variable IHM");
    same("STRING", "SWAP(Mesures[0], Mesures[4]);\nR := JOIN(Mesures, ';');", "3.1;8;15.2;8;12.5", "SWAP de deux elements d'une liste");
    same("INT", "SWAP(P.Item1, P.Item2);\nR := P.Item1 * 10 + P.Item2;", "21", "SWAP de deux membres");
    same("STRING", "SWAP(Xs, Ys);\nR := JOIN(Xs, ',') + ' ' + JOIN(Ys, ',');", "0,100,200 0,10,20", "SWAP de deux listes entieres");
    same("STRING", "SWAP(TabIhm[1], TabIhm[5]);\nR := JOIN(TabIhm, ',');", "50,20,30,40,10", "SWAP de deux cases d'un tableau IHM");
    fails("SWAP(Ia, Ra);", "SWAP : Ia est un INT, Ra un REAL \xE2\x80\x94 deux variables du m\xC3\xAAme type", "SWAP d'un INT et d'un REAL");
    fails("SWAP(Ia, 5);", "SWAP : une variable est attendue en 2e argument, pas 5", "SWAP d'une valeur");
    fails("SWAP(Ia + 1, Ib);", "SWAP : une variable est attendue en 1er argument, pas Ia + 1", "SWAP d'une expression");
    fails("SWAP(Ia, Ca);", "SWAP : une constante ne s'\xC3\xA9" "crit pas", "SWAP d'une constante");
    fails("SWAP(A[0], A[1]);", "ne s'\xC3\xA9" "crit pas en place", "SWAP de deux elements d'un SET");
    same("STRING", "TRY\n  SWAP(Ia, Ra);\nCATCH s\n  R := s;\nEND_TRY", "SWAP : Ia est un INT, Ra un REAL \xE2\x80\x94 deux variables du m\xC3\xAAme type",
         "SWAP refuse dans un TRY : le CATCH a le message");
    same("INT", "R := EXCHANGE(Ia, 7) * 10 + Ia;", "17", "EXCHANGE rend l'ancienne valeur et ecrit la nouvelle");
    same("INT", "Ia := 3;\nR := EXCHANGE(Ia, 7) * 10 + Ia;", "37", "EXCHANGE : l'exemple de la fiche");
    same("STRING", "s := EXCHANGE(Pile[1], 'z');\nR := s + JOIN(Pile, '');", "bazc", "EXCHANGE d'un element");
    same("INT", "R := EXCHANGE(Compteur, 9) * 10 + Compteur;", "79", "EXCHANGE d'une variable IHM");
    same("STRING", "Carres := EXCHANGE(Xs, Ys);\nR := JOIN(Carres, ',') + ' ' + JOIN(Xs, ',');", "0,10,20 0,100,200", "EXCHANGE d'une liste entiere");
    same("INT", "i := EXCHANGE(Ia, 7.0);\nR := Ia;", "7", "EXCHANGE : 7.0 va dans un INT");
    fails("i := EXCHANGE(Ia, 'abc');", "EXCHANGE : 'abc' n'est pas un INT", "EXCHANGE d'un texte dans un INT");
    fails("i := EXCHANGE(Ia, 7.5);", "EXCHANGE : 7.5 n'est pas un entier (INT)", "EXCHANGE de 7.5 dans un INT");
    fails("i := EXCHANGE(5, 1);", "EXCHANGE : une variable est attendue en 1er argument, pas 5", "EXCHANGE d'une valeur");
    fails("x := EXCHANGE(Cr, 2.0);", "EXCHANGE : une constante ne s'\xC3\xA9" "crit pas", "EXCHANGE d'une constante");
}

// ------------------------------------------------- une expression fx, un texte a trous ---
void expressions() {
    std::printf("-- une expression fx, un texte a trous\n");
    Project p;
    fillProject(p, "REAL");
    exprcheck::Context c;
    c.project = &p;
    c.known = [&p](std::string_view n) { return p.variable(n) != nullptr; };
    const auto said = [&](const std::string& e, exprcheck::Want want = exprcheck::Want::Any) {
        std::string all;
        for (const auto& pb : exprcheck::check(c, e, want)) all += " | " + pb.message;
        return all;
    };
    for (const char* e : {"CEIL(Niveau) + FLOOR(Niveau) + FMOD(Niveau, 10.0)", "HYPOT(3.0, 4.0) * ATAN2(1.0, 1.0) + LERP(0.0, 1.0, 0.5)",
                          "YEAR(MAINTENANT()) + WEEKDAY(MAINTENANT()) + DAYS_BETWEEN(DT#2026-01-01-00:00:00, MAINTENANT())",
                          "LIST_MEDIAN(ListeIhm) + LIST_STDDEV(ListeIhm, TRUE) + LIST_LOWER_BOUND(ListeIhm, 2.0)", "INTERPOLER_TABLE(ListeIhm, ListeIhm, 1.5)",
                          "BYTESWAP(Mot) + RANDOM_NORMAL(0.0, 1.0)"}) {
        const std::string m = said(e, exprcheck::Want::Number);
        check(m.empty(), std::string("fx : ") + e + " se lit (un nombre)" + m);
    }
    check(said("ISNAN(Niveau) OR STARTS_WITH(Texte, 'P') OR LIST_BINARY_SEARCH(ListeIhm, 1.0) OR SET_INCLUDES(ListeIhm, ListeIhm)", exprcheck::Want::Bool).empty(),
          "fx : ISNAN, STARTS_WITH, LIST_BINARY_SEARCH, SET_INCLUDES : des BOOL");
    for (const char* e : {"FORMAT('{:.1f} bar', Niveau)", "FORMAT_DT(MAINTENANT(), '%H:%M')", "TRIM(Texte)"})
        check(said(e).empty(), std::string("fx : ") + e + " se lit" + said(e));
    check(said("SWAP(Niveau, Niveau)").find("SWAP \xC3\xA9" "change deux variables : une expression ne change rien") != std::string::npos,
          "fx : SWAP est refusee (elle ecrit)" + said("SWAP(Niveau, Niveau)"));
    check(said("EXCHANGE(Niveau, 1.0)").find("EXCHANGE \xC3\xA9" "crit sa variable : une expression ne change rien") != std::string::npos,
          "fx : EXCHANGE est refusee (elle ecrit)" + said("EXCHANGE(Niveau, 1.0)"));
    check(said("LIST_TRANSFORM(ListeIhm, ListeIhm, TRANSFORMATION#Carre)").find("LIST_TRANSFORM change son objet") != std::string::npos,
          "fx : LIST_TRANSFORM est refusee (elle ecrit sa destination)");
    check(said("FMOD(1.0)").find("FMOD prend 2 arguments, pas 1") != std::string::npos, "fx : FMOD a un argument - dit" + said("FMOD(1.0)"));
    const auto roots = scanRoots("FORMAT('{:.1f} bar', Niveau) + FORMAT_DT(MAINTENANT(), '%H')");
    check(roots.size() == 1 && roots.front() == "Niveau", "fx : les racines de FORMAT(...) - Niveau seul");
    // En marche : la valeur ; une native qui ecrit ne passe pas (l'expression ne change rien).
    Bench b("REAL", "R := 0;");
    const auto value = [&](const std::string& e) {
        const auto r = Expression::compile(e).evaluate(b.rt.environment());
        return r ? shown(*r) : "[" + r.error().message() + "]";
    };
    check(value("FORMAT('{:.1f} bar', Niveau)") == "42.5 bar", "fx en marche : FORMAT('{:.1f} bar', Niveau) = " + value("FORMAT('{:.1f} bar', Niveau)"));
    check(value("CEIL(Niveau) + FLOOR(Niveau)") == "85", "fx en marche : CEIL(42.5) + FLOOR(42.5) = " + value("CEIL(Niveau) + FLOOR(Niveau)"));
    check(value("FORMAT_DT(DT#2026-10-19-14:30:05, '%A')") == "lundi", "fx en marche : FORMAT_DT(..., '%A') = " + value("FORMAT_DT(DT#2026-10-19-14:30:05, '%A')"));
    check(value("RANDOM_NORMAL(5.0, 0.0)") == "5", "fx en marche : RANDOM_NORMAL(5.0, 0.0) = " + value("RANDOM_NORMAL(5.0, 0.0)"));
    const auto swapped = Expression::compile("SWAP(Niveau, Niveau)").evaluate(b.rt.environment());
    sim::Value level;
    check(!swapped && b.rt.environment().read("Niveau", level) && level.asReal() == 42.5, "fx en marche : SWAP n'ecrit rien (une erreur)");
    // Un texte a trous : un trou va jusqu'a la '}' hors des chaines (les {} du modele de FORMAT).
    const auto text = [&](const std::string& t) { return TextTemplate::compile(t).render(b.rt.environment()); };
    check(text("P = {FORMAT('{:05.1f}', Niveau)} bar") == "P = 042.5 bar", "texte a trous : {FORMAT('{:05.1f}', Niveau)} = " + text("P = {FORMAT('{:05.1f}', Niveau)} bar"));
    check(text("{FORMAT('{{x}} {}', Compteur)} et {Compteur}") == "{x} 7 et 7", "texte a trous : FORMAT et ses {{ }} - " + text("{FORMAT('{{x}} {}', Compteur)} et {Compteur}"));
    check(text("{Texte = 'a} fin") == "Faux fin", "texte a trous : une chaine qui ne se ferme pas, comme avant (" + text("{Texte = 'a} fin") + ")");
    check(text("{Compteur} {{brut}} {Texte}") == "7 {brut} Pompe 3", "texte a trous : rien ne change sans chaine dans un trou (" + text("{Compteur} {{brut}} {Texte}") + ")");
    const auto holes = exprcheck::templateExpressions("{FORMAT('{:.1f} bar', Niveau)} et {Niveau:0.0}");
    check(holes.size() == 2 && holes[0] == "FORMAT('{:.1f} bar', Niveau)" && holes[1] == "Niveau",
          "Compiler decoupe un texte a trous comme le moteur (" + std::to_string(holes.size()) + " trous)");
}

// ------------------------------------------------------------------------- Compiler ---
std::vector<scriptcheck::Finding> walk(const std::string& body) {
    Project p;
    fillProject(p, "REAL");
    scriptcheck::Scope sc;
    sc.project = &p;
    return scriptcheck::check(sc, std::string(kDecl) + body);
}
bool says(const std::vector<scriptcheck::Finding>& fs, const std::string& part, scriptcheck::Finding::Severity sev) {
    for (const auto& f : fs)
        if (f.severity == sev && f.message.find(part) != std::string::npos) return true;
    return false;
}
int count(const std::vector<scriptcheck::Finding>& fs, const std::string& part) {
    int k = 0;
    for (const auto& f : fs) k += f.message.find(part) != std::string::npos ? 1 : 0;
    return k;
}
std::string all(const std::vector<scriptcheck::Finding>& fs) {
    std::string s;
    for (const auto& f : fs) s += std::string(" | ") + (f.severity == scriptcheck::Finding::Severity::Error ? "E " : "A ") + f.message;
    return s;
}
void compiler() {
    std::printf("-- Compiler\n");
    using S = scriptcheck::Finding::Severity;
    {
        const auto fs = walk("x := CEIL(Ra) + FLOOR(Ra) + FMOD(Ra, 2.0) + HYPOT(Ra, Ra) + ATAN2(Ra, Ra) + LERP(0.0, Ra, 0.5);\n"
                             "ok := ISNAN(Ra) OR ISFINITE(Ra) OR STARTS_WITH(s, 'a') OR ENDS_WITH(s, 'b') OR CONTAINS(s, 'c');\n"
                             "s := TRIM(s) + TRIM_LEFT(s) + TRIM_RIGHT(s) + FORMAT('{:.1f} et {}', Ra, Ia);\n"
                             "i := YEAR(Quand) + MONTH(Quand) + DAY(Jour) + HOUR(Heure) + MINUTE(Quand) + SECOND(Quand) + WEEKDAY(Jour);\n"
                             "Quand := ADD_DAYS(Quand, 2);\ni := DAYS_BETWEEN(Jour, Quand);\ns := FORMAT_DT(Quand, '%d/%m/%Y');\n"
                             "i := LIST_LOWER_BOUND(Triee, 3.0);\nok := LIST_BINARY_SEARCH(Triee, 3.0);\ni := LIST_TRANSFORM(Xs, Carres, 'VersKelvin');\n"
                             "i := LIST_TRANSFORM(Entiers, Carres, TRANSFORMATION#Carre);\nx := LIST_MEDIAN(Stats) + LIST_VARIANCE(Stats, TRUE) + LIST_STDDEV(Stats);\n"
                             "x := INTERPOLER_TABLE(Xs, Ys, 15.0);\nB := SET_SYMMETRIC_DIFFERENCE(A, B);\nok := SET_INCLUDES(A, B);\n"
                             "x := RANDOM_NORMAL(0.0, 1.0);\nw := BYTESWAP(w);\ndw := WORDSWAP(dw);\nSWAP(Ia, Ib);\nIa := EXCHANGE(Ib, 3);");
        bool error = false;
        for (const auto& f : fs) error = error || f.severity == S::Error;
        check(!error, "un script qui emploie les 39 natives se lit sans erreur" + all(fs));
    }
    {
        // Dans un script, une native qui ecrit ecrit vraiment : pas l'avertissement d'une expression fx.
        const auto fs = walk("x := EXCHANGE(Ra, 2.0);\nok := SWAP(Ia, Ib);\ni := LIST_TRANSFORM(Xs, Carres, TRANSFORMATION#Carre);\nok := LIST_SET(Mesures, 0, 1.0);");
        check(count(fs, "une expression") == 0, "Ancien := EXCHANGE(...), ok := SWAP(...), i := LIST_TRANSFORM(...) dans un script : rien sur une expression" + all(fs));
    }
    {
        const auto fs = walk("x := FMOD(1.0);\ni := YEAR();\ns := TRIM(s, s);");
        check(says(fs, "FMOD prend 2 arguments, pas 1", S::Error) && says(fs, "YEAR prend 1 argument, pas 0", S::Error) && says(fs, "TRIM prend 1 argument, pas 2", S::Error),
              "le nombre d'arguments : des erreurs" + all(fs));
    }
    {
        const auto fs = walk("s := FORMAT('{} et {}', 1);\ns := FORMAT('{0}', 1);\ns := FORMAT('}', 1);\ns := FORMAT('{:.2f} {{}}', Ra);");
        check(says(fs, "FORMAT : 2 {} pour 1 valeur", S::Error) && says(fs, "FORMAT : {0} \xE2\x80\x94 {} prend la valeur suivante", S::Error)
                  && says(fs, "FORMAT : une } seule", S::Error) && count(fs, "FORMAT") == 3,
              "FORMAT : ses {} et ses valeurs, quand le modele est ecrit" + all(fs));
    }
    {
        const auto fs = walk("SWAP(Ia, 5);\nSWAP(Ia, Ra);\nx := EXCHANGE(5, Ra);\nSWAP(Mesures[0], Mesures[1]);\nSWAP(s, s2);");
        check(says(fs, "SWAP : une variable est attendue en 2e argument, pas 5", S::Error)
                  && says(fs, "SWAP : Ia est un INT, Ra un REAL \xE2\x80\x94 deux variables du m\xC3\xAAme type", S::Error)
                  && says(fs, "EXCHANGE : une variable est attendue en 1er argument, pas 5", S::Error) && count(fs, "SWAP") == 2,
              "SWAP et EXCHANGE : des variables, du meme type" + all(fs));
    }
    {
        const auto fs = walk("SWAP(Ia, Ca);\nSWAP(Ca, Ia);\nx := EXCHANGE(Cr, 2.0);\ni := LIST_TRANSFORM(Xs, Fixes, TRANSFORMATION#Carre);\n"
                             "i := LIST_TRANSFORM(Fixes, Carres, TRANSFORMATION#Carre);\nx := LIST_MEDIAN(Fixes);");
        check(count(fs, "Ca est une constante : SWAP ne peut pas la changer") == 2 && says(fs, "Cr est une constante : EXCHANGE ne peut pas la changer", S::Error)
                  && count(fs, "Fixes est une constante : LIST_TRANSFORM ne peut pas la changer") == 1 && !says(fs, "LIST_MEDIAN ne peut pas", S::Error),
              "une constante ecrite par SWAP (ses deux arguments), EXCHANGE, LIST_TRANSFORM (sa destination) : dite" + all(fs));
    }
    {
        const auto fs = walk("i := LIST_TRANSFORM(Xs, Carres, 'Versklvin');\ni := LIST_TRANSFORM(Xs, Carres, 'Moyenne');\ni := LIST_TRANSFORM(Xs, Carres, 'Etiquette');\n"
                             "i := LIST_TRANSFORM(Pile, Carres, 'VersKelvin');\ni := LIST_TRANSFORM(Xs, Carres, OPERATION#Somme);\ni := LIST_TRANSFORM(Xs, Sr, 'VersKelvin');");
        check(says(fs, "LIST_TRANSFORM : Versklvin n'est pas une fonction du projet \xE2\x80\x94 veux-tu dire VersKelvin ?", S::Error)
                  && says(fs, "LIST_TRANSFORM : Moyenne prend 2 param\xC3\xA8tres \xE2\x80\x94 OP est une fonction \xC3\xA0 un param\xC3\xA8tre", S::Error)
                  && says(fs, "LIST_TRANSFORM : Etiquette rend une STRING, Carres attend des REAL", S::Error)
                  && says(fs, "LIST_TRANSFORM : VersKelvin prend un REAL, les \xC3\xA9l\xC3\xA9ments de Pile sont des STRING", S::Error)
                  && says(fs, "LIST_TRANSFORM : OP est une TRANSFORMATION (TRANSFORMATION#Carre\xE2\x80\xA6) ou le nom d'une fonction du projet ('VersKelvin'), pas OPERATION#Somme",
                          S::Error)
                  && says(fs, "LIST_TRANSFORM : un SET se range tout seul", S::Error),
              "LIST_TRANSFORM : sa fonction (inconnue, deux parametres, ses types), une OPERATION, un SET" + all(fs));
    }
    {
        // Sans projet (l'exemple d'une fiche) : la fonction nommee ne se cherche pas.
        std::string errors;
        for (const auto& d : checkScript(ScriptLang::ST, "LIST_TRANSFORM(Celsius, Kelvins, 'VersKelvin');", "Essai"))
            if (d.severity == ScriptDiagnostic::Severity::Error && d.message.find("n'est pas une fonction") != std::string::npos) errors += " | " + d.message;
        check(errors.empty(), "sans projet : LIST_TRANSFORM(..., 'VersKelvin') ne cherche pas la fonction" + errors);
    }
}

// ---------------------------------------------------------- les valeurs calculees ---
void calculs() {
    std::printf("-- les valeurs calculees au demarrage : SWAP, EXCHANGE, LIST_TRANSFORM ecrivent\n");
    Project p;
    Script r;
    r.id = p.allocate();
    r.name = "Reglages";
    r.event = "Appel";
    const auto decl = [](DeclKind k, const char* name, const char* type, const char* value) {
        Declaration d;
        d.kind = k;
        d.name = name;
        d.type = type;
        d.value = value;
        return d;
    };
    Declaration k = decl(DeclKind::Constant, "K", "REAL", "");
    k.computed = true;
    r.decls = {decl(DeclKind::Constant, "Base", "REAL", "1.0"), decl(DeclKind::Constant, "Autre", "REAL", "2.0"),
               decl(DeclKind::Constant, "Seuils", "LIST OF REAL", "[1.0, 2.0]"), decl(DeclKind::Constant, "Copie", "LIST OF REAL", "[]"), k};
    p.programs.scripts.push_back(r);
    const auto issues = [&](const std::string& code) {
        Script& s = p.programs.scripts[0];
        s.decls.back().calc = code;
        std::string out;
        for (const auto& i : calc::analyse(p, s, s.decls.back()).issues) out += (i.error ? "E " : "A ") + i.message + " | ";
        return out;
    };
    const std::string written = " est une constante : elle ne s'\xC3\xA9" "crit pas";
    const std::string swap = issues("SWAP(Base, Autre);\nK := Base;\n");
    check(swap.find("Base" + written) != std::string::npos && swap.find("Autre" + written) != std::string::npos, "SWAP ecrit ses deux arguments : " + swap);
    const std::string exchange = issues("K := EXCHANGE(Base, 3.0);\n");
    check(exchange.find("Base" + written) != std::string::npos, "EXCHANGE ecrit son premier argument : " + exchange);
    const std::string transform = issues("LIST_TRANSFORM(Seuils, Copie, TRANSFORMATION#Carre);\nK := 1.0;\n");
    check(transform.find("Copie" + written) != std::string::npos && transform.find("Seuils" + written) == std::string::npos,
          "LIST_TRANSFORM ecrit sa destination, pas sa source : " + transform);
    const std::string reads = issues("K := LIST_MEDIAN(Seuils) + FMOD(Base, Autre);\n");
    check(reads.find(written) == std::string::npos, "LIST_MEDIAN, FMOD lisent : " + reads);
}

// ----------------------------------------------------------------------- le catalogue ---
void catalogue() {
    std::printf("-- le catalogue : les 39 fiches, std::utility, ce que sert le dialecte\n");
    const auto* utility = nt::category("utility");
    check(utility && utility->title == "std::utility" && utility->group == "Biblioth\xC3\xA8ques", "la categorie std::utility, dans Bibliotheques");
    const std::vector<std::string> names = {"CEIL", "FLOOR", "FMOD", "HYPOT", "ATAN2", "LERP", "ISNAN", "ISFINITE", "STARTS_WITH", "ENDS_WITH",
                                            "CONTAINS", "TRIM", "TRIM_LEFT", "TRIM_RIGHT", "FORMAT", "YEAR", "MONTH", "DAY", "HOUR", "MINUTE",
                                            "SECOND", "WEEKDAY", "ADD_DAYS", "DAYS_BETWEEN", "FORMAT_DT", "LIST_LOWER_BOUND", "LIST_BINARY_SEARCH",
                                            "LIST_TRANSFORM", "LIST_MEDIAN", "LIST_VARIANCE", "LIST_STDDEV", "INTERPOLER_TABLE", "SET_SYMMETRIC_DIFFERENCE",
                                            "SET_INCLUDES", "RANDOM_NORMAL", "BYTESWAP", "WORDSWAP", "SWAP", "EXCHANGE"};
    check(names.size() == 39, "39 natives");
    for (const auto& n : names) {
        const auto* f = nt::function(n);
        check(f && !f->example.empty() && !f->summary.empty(), n + " : sa fiche et son exemple");
        if (!f) continue;
        const bool statement = n == "SWAP" || n == "EXCHANGE" || n == "LIST_TRANSFORM";
        check(f->expression == !statement, n + (statement ? " : pas dans une expression fx (elle ecrit)" : " : dans une expression fx"));
        const auto* lib = sim::lib::native(n);
        // RANDOM_NORMAL suit la suite de RANDOM (une native propre a l'IHM) ; les autres, le dialecte.
        check(n == "RANDOM_NORMAL" ? (!lib && nt::isOwnFunction(n)) : (lib && !nt::isOwnFunction(n)), n + " : servie par " + (lib ? "le dialecte" : "l'IHM"));
        int mn = 0, mx = 0;
        if (lib && n != "FORMAT" && nt::arity(n, mn, mx))
            check(mn == lib->min && mx == lib->max, n + " : le meme nombre d'arguments dans la fiche et le dialecte");
    }
    const auto* ceil = nt::function("CEIL");
    const auto* lerp = nt::function("LERP");
    check(ceil && ceil->inC == nt::Avail::Same && ceil->inCpp == nt::Avail::Same && lerp && lerp->inC == nt::Avail::Equivalent && lerp->inCpp == nt::Avail::Same,
          "C et C++ : ceux de la fiche (CEIL : le meme nom ; LERP : std::lerp en C++ seulement)");
    check(nt::isOwnFunction("MAINTENANT") && nt::isOwnFunction("DT_TO_DATE") && nt::isOwnFunction("RANDOM") && !nt::isOwnFunction("YEAR"),
          "les natives propres d'avant le restent ; YEAR est servie par le dialecte");
    const auto* swap = nt::function("SWAP");
    const auto* exchange = nt::function("EXCHANGE");
    const auto* table = nt::function("INTERPOLER_TABLE");
    check(swap && swap->category == "utility" && exchange && exchange->category == "utility" && table && table->category == "numeric",
          "SWAP et EXCHANGE dans std::utility, INTERPOLER_TABLE dans std::numeric");
}

} // namespace

int main() {
    std::printf("1.12.16 : les 39 natives std::\n");
    maths();
    textes();
    dates();
    listes();
    interpoler();
    ensembles();
    hasard();
    bits();
    utility();
    expressions();
    compiler();
    calculs();
    catalogue();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
