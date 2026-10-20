// tests/cellrule_test.cpp - 1.12.19 : LA REGLE DES CASES, sans ecran.
//
//   cellrule_test
//
//  Une valeur fixe se tape telle qu'on la lit, sans « = » ni apostrophes, et se lit dans le
//  type de la case par UNE SEULE lecture (sim/ValueText) : la constante d'une case de
//  l'inspecteur (hmi::argumentLiteral), le forcage (l'API, les tables d'animation, l'IHM),
//  les valeurs initiales, les cases a cocher. Les memes mots pour un booleen, la virgule ou le
//  point, les espaces entre les milliers, les durees (T#5s, 5 s, 250 ms), les dates ; un
//  entier sans decimale et dans ses bornes (refuse, pas arrondi ni replie) ; ce qu'une case
//  montre d'une valeur se retape tel quel.
#include "../src/ui/widgets/ExprField.hpp"
#include "../src/sim/ValueText.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRoles.hpp"
#include "../src/hmi/HmiSymbols.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("  ECHEC  %s\n", what.c_str());
        ++failures;
    }
}

bool reads(std::string_view text, sim::Type type, sim::Value& out, std::string& why) {
    why.clear();
    return sim::readTyped(text, type, out, why);
}

// ------------------------------------------------------------------ les mots ---
void words() {
    std::printf("== les mots d'un booleen, partout les memes ==\n");
    for (const char* w : {"TRUE", "true", "Vrai", "OUI", "oui", "ON", "yes", "1", " VRAI "})
        check(sim::trueWord(w) && !sim::falseWord(w), std::string("vrai : ") + w);
    for (const char* w : {"FALSE", "false", "Faux", "NON", "non", "OFF", "No", "0"})
        check(sim::falseWord(w) && !sim::trueWord(w), std::string("faux : ") + w);
    for (const char* w : {"2", "X", "", "vraiment", "O"})
        check(!sim::trueWord(w) && !sim::falseWord(w), std::string("ni vrai ni faux : ") + w);
    // Les lecteurs de l'IHM disent pareil (avant : ON/OFF/YES/NO manquaient ici ou la).
    check(hmi::parseBool("ON", false) && !hmi::parseBool("OFF", true) && hmi::parseBool("yes", false) && !hmi::parseBool("No", true),
          "hmi::parseBool : ON, OFF, YES, NO");
    check(hmi::roles::readBool("'OUI'", false) && !hmi::roles::readBool("NON", true), "roles::readBool : 'OUI', NON");
}

// ---------------------------------------------------------------- les nombres ---
void numbers() {
    std::printf("== les nombres : la virgule, les milliers ==\n");
    double d = 0;
    check(sim::readDecimal("12,5", d) && d == 12.5, "12,5");
    check(sim::readDecimal("12.5", d) && d == 12.5, "12.5");
    check(sim::readDecimal("1 000", d) && d == 1000, "1 000");
    check(sim::readDecimal("1\xE2\x80\xAF" "000,25", d) && d == 1000.25, "1 000,25 (espace fine)");
    check(sim::readDecimal("1_000", d) && d == 1000, "1_000");
    check(sim::readDecimal("-3", d) && d == -3, "-3");
    check(sim::readDecimal("1e3", d) && d == 1000, "1e3");
    for (const char* bad : {"", "abc", "0x1A", "inf", "nan", "12,5,3", "1.2.3"})
        check(!sim::readDecimal(bad, d), std::string("pas un nombre : ") + bad);
    check(hmi::parseNumber("1 000,5", d) && d == 1000.5, "hmi::parseNumber : la meme lecture (1 000,5)");
}

// ----------------------------------------------------------------- les durees ---
void durations() {
    std::printf("== les durees ==\n");
    std::int64_t ms = 0;
    const struct { const char* text; std::int64_t ms; } good[] = {
        {"T#5s", 5000}, {"t#5s", 5000}, {"T#1s500ms", 1500}, {"TIME#5s", 5000}, {"TIME#1.5s", 1500}, {"5s", 5000},
        {"5 s", 5000}, {"1m30s", 90000}, {"250 ms", 250}, {"2 min", 120000}, {"1h", 3600000}, {"500", 500}, {"T#0ms", 0},
        {"1,5 s", 1500}, {"1 h 30", 5400000}, {"2 min 30", 150000}, {"00:05", 5000}, {"01:30:00", 5400000}, {"-2s", -2000}};
    for (const auto& g : good) check(sim::readDuration(g.text, ms) && ms == g.ms, std::string("duree : ") + g.text + " = " + std::to_string(g.ms) + " ms");
    for (const char* bad : {"", "5x", "T#", "s", "5 s 3", "1:2:3:4", "a:05"})
        check(!sim::readDuration(bad, ms), std::string("pas une duree : ") + bad);
    // Une case « (s) » : un nombre seul en secondes ; apres T#, toujours des millisecondes (le ST).
    check(sim::readDuration("5", ms, 1000.0) && ms == 5000 && sim::readDuration("T#500", ms, 1000.0) && ms == 500
              && sim::readDuration("250 ms", ms, 1000.0) && ms == 250,
          "une case (s) : 5 = 5 s ; T#500 = 500 ms ; 250 ms");
    check(hmi::roles::parseDuration("90", ms) && ms == 90000 && hmi::roles::parseDuration("90", ms, hmi::roles::Unit::Milliseconds) && ms == 90,
          "roles::parseDuration est la meme lecture (un nombre nu dans son unite)");
}

// ----------------------------------------------- la valeur d'une case typee ---
void typed() {
    std::printf("== la valeur d'une case typee (le forcage, la constante) ==\n");
    sim::Value v;
    std::string why;
    check(reads("12", sim::Type::Int, v, why) && v.asInteger() == 12, "INT : 12");
    check(reads("1 000", sim::Type::Int, v, why) && v.asInteger() == 1000, "INT : 1 000");
    check(reads("INT#5", sim::Type::Int, v, why) && v.asInteger() == 5, "INT : INT#5");
    check(reads("16#FF", sim::Type::Int, v, why) && v.asInteger() == 255, "INT : 16#FF");
    check(!reads("12.7", sim::Type::Int, v, why) && why.find("n'est pas un nombre entier (INT)") != std::string::npos,
          "INT : 12.7 refuse, pas arrondi (" + why + ")");
    check(!reads("40000", sim::Type::Int, v, why) && why.find("hors des bornes d'un INT (-32\xE2\x80\xAF" "768 \xC3\xA0 32\xE2\x80\xAF" "767)") != std::string::npos,
          "INT : 40000 refuse, pas replie (" + why + ")");
    check(!reads("-1", sim::Type::UInt, v, why) && why.find("hors des bornes d'un UINT") != std::string::npos, "UINT : -1 refuse");
    check(reads("12,5", sim::Type::Real, v, why) && v.asReal() == 12.5, "REAL : 12,5");
    check(reads("1 000,5", sim::Type::Real, v, why) && v.asReal() == 1000.5, "REAL : 1 000,5");
    check(!reads("abc", sim::Type::Real, v, why) && why.find("n'est pas un nombre (REAL)") != std::string::npos, "REAL : abc refuse (" + why + ")");
    check(reads("OUI", sim::Type::Bool, v, why) && v.isTruthy(), "BOOL : OUI");
    check(reads("non", sim::Type::Bool, v, why) && !v.isTruthy(), "BOOL : non");
    check(reads("1", sim::Type::Bool, v, why) && v.isTruthy(), "BOOL : 1");
    check(!reads("2", sim::Type::Bool, v, why) && why.find("n'est pas un bool") != std::string::npos, "BOOL : 2 refuse (" + why + ")");
    check(reads("T#5s", sim::Type::Time, v, why) && v.asInteger() == 5000, "TIME : T#5s = 5000 ms (avant : 5 ms dans deux forcages)");
    check(reads("T#1s500ms", sim::Type::Time, v, why) && v.asInteger() == 1500, "TIME : T#1s500ms = 1500 ms (avant : 1 ms)");
    check(reads("TIME#5s", sim::Type::Time, v, why) && v.asInteger() == 5000, "TIME : TIME#5s (avant : refuse)");
    check(reads("5 s", sim::Type::Time, v, why) && v.asInteger() == 5000, "TIME : 5 s");
    check(reads("500", sim::Type::Time, v, why) && v.asInteger() == 500, "TIME : 500 (des ms)");
    check(reads("abc", sim::Type::String, v, why) && v.asString() == "abc", "STRING : abc, tel quel");
    check(reads("'abc'", sim::Type::String, v, why) && v.asString() == "abc", "STRING : 'abc' (les apostrophes partent)");
    check(reads("'L$'eau'", sim::Type::String, v, why) && v.asString() == "L'eau", "STRING : 'L$'eau' -> L'eau");
    check(reads("l'eau", sim::Type::String, v, why) && v.asString() == "l'eau", "STRING : l'eau (une apostrophe au milieu reste)");
    check(reads("ON", sim::Type::String, v, why) && v.asString() == "ON", "STRING : ON reste un texte (avant : 'TRUE' dans deux forcages)");
    check(reads("2026-10-09", sim::Type::Date, v, why) && sim::dateText(sim::Type::Date, v.asInteger()) == "2026-10-09", "DATE : 2026-10-09");
    check(reads("D#2026-10-09", sim::Type::Date, v, why) && sim::dateText(sim::Type::Date, v.asInteger()) == "2026-10-09", "DATE : D#2026-10-09");
    check(!reads("2026-13-01", sim::Type::Date, v, why) && why.find("n'est pas une date") != std::string::npos, "DATE : un mois 13 refuse");
    check(reads("14:30", sim::Type::Tod, v, why) && v.type() == sim::Type::Tod, "TOD : 14:30");
    check(reads("2026-10-09 14:30", sim::Type::Dt, v, why) && v.type() == sim::Type::Dt, "DT : 2026-10-09 14:30 (l'espace pour le tiret)");
    check(!reads("", sim::Type::Int, v, why) && why == "rien n'est tap\xC3\xA9", "vide : rien n'est tape");
    // Sans type connu : devinee.
    check(reads("TRUE", sim::Type::Unknown, v, why) && v.type() == sim::Type::Bool, "? : TRUE est un booleen");
    check(reads("'x'", sim::Type::Unknown, v, why) && v.type() == sim::Type::String, "? : 'x' est un texte");
    check(reads("12.5", sim::Type::Unknown, v, why) && v.type() == sim::Type::Real, "? : 12.5 est un REAL");
    check(reads("12", sim::Type::Unknown, v, why) && v.type() == sim::Type::DInt, "? : 12 est un DINT");
    check(reads("T#2s", sim::Type::Unknown, v, why) && v.type() == sim::Type::Time && v.asInteger() == 2000, "? : T#2s est un TIME");
}

// --------------------------------------------- ce que montre une case, se retape ---
void shown() {
    std::printf("== ce qu'une case montre se retape tel quel ==\n");
    const sim::Value values[] = {sim::Value::boolean(true), sim::Value::integer(sim::Type::Int, -12), sim::Value::real(12.5),
                                 sim::Value::real(static_cast<double>(0.1f)), sim::Value::time(1500), sim::Value::time(0),
                                 sim::Value::time(3723004), sim::Value::text("Pompe 1"), sim::Value::integer(sim::Type::Date, 1791504000000LL)};
    for (const auto& v : values) {
        const std::string text = sim::fixedText(v);
        sim::Value back;
        std::string why;
        const bool read = sim::readTyped(text, v.type(), back, why);
        // Un REAL de l'automate est sur 32 bits : 0.1 relu donne le meme REAL (pas le meme double).
        const bool same = v.type() == sim::Type::Real ? static_cast<float>(back.asReal()) == static_cast<float>(v.asReal()) : back.equals(v);
        check(read && same, "\xC2\xAB " + text + " \xC2\xBB se relit pareil" + (why.empty() ? "" : " (" + why + ")"));
    }
    check(sim::fixedText(sim::Value::real(static_cast<double>(0.1f))) == "0.1", "un REAL de l'automate : 0.1, pas 0.100000001");
    check(sim::fixedText(sim::Value::time(1500)) == "T#1s500ms", "TIME : T#1s500ms");
    check(sim::fixedText(sim::Value::text("abc")) == "abc", "STRING : sans apostrophes");
}

// ----------------------------------------------- la constante d'une case (IHM) ---
void constants() {
    std::printf("== la constante d'une case de l'inspecteur : les memes ecritures ==\n");
    std::string why;
    check(hmi::argumentLiteral(nullptr, "BOOL", "OUI") == "TRUE" && hmi::argumentLiteral(nullptr, "BOOL", "off") == "FALSE", "BOOL : OUI, off");
    check(hmi::argumentLiteral(nullptr, "INT", "1 000") == "1000", "INT : 1 000 -> 1000");
    why.clear();
    check(hmi::argumentLiteral(nullptr, "INT", "12.7", &why).empty() && why.find("n'est pas un nombre entier (INT)") != std::string::npos,
          "INT : 12.7 refuse (" + why + ")");
    why.clear();
    check(hmi::argumentLiteral(nullptr, "INT", "40000", &why).empty() && why.find("hors des bornes d'un INT (-32\xE2\x80\xAF" "768 \xC3\xA0 32\xE2\x80\xAF" "767)") != std::string::npos,
          "INT : 40000 refuse, les bornes dites comme au forcage (" + why + ")");
    check(hmi::argumentLiteral(nullptr, "REAL", "12,5") == "12.5" && hmi::argumentLiteral(nullptr, "REAL", "1 000,5") == "1000.5", "REAL : 12,5 ; 1 000,5");
    check(hmi::argumentLiteral(nullptr, "TIME", "5 s") == "T#5s" && hmi::argumentLiteral(nullptr, "TIME", "250 ms") == "T#250ms"
              && hmi::argumentLiteral(nullptr, "TIME", "500") == "T#500ms" && hmi::argumentLiteral(nullptr, "TIME", "T#1s500ms") == "T#1s500ms",
          "TIME : 5 s, 250 ms, 500, T#1s500ms");
    check(hmi::argumentLiteral(nullptr, "DATE", "2026-10-09") == "D#2026-10-09", "DATE : 2026-10-09 -> D#2026-10-09");
    why.clear();
    check(hmi::argumentLiteral(nullptr, "DATE", "2026-13-40", &why).empty() && why.find("n'est pas une date") != std::string::npos,
          "DATE : 2026-13-40 refuse (avant : D#2026-13-40 sans rien dire)");
    check(hmi::argumentLiteral(nullptr, "STRING", "Voiture") == "'Voiture'" && hmi::argumentLiteral(nullptr, "STRING", "'Voiture'") == "'Voiture'",
          "STRING : Voiture et 'Voiture' donnent le meme litteral");
    why.clear();
    (void)hmi::argumentLiteral(nullptr, "ARRAY[0..9] OF INT", "3", &why);
    check(why.find("\xC3\xA9" "cris =Variable") != std::string::npos && why.find("utilisez") == std::string::npos,
          "un tableau : la raison dit =Variable, et tutoie (" + why + ")");
}

// ---------------------------------------------- ce que devient le texte tape ---
void cells() {
    std::printf("== une case de valeur : =formule, un nom connu, sinon une valeur fixe du type ==\n");
    hmi::Project p;
    hmi::Variable niveau;
    niveau.id = p.allocate();
    niveau.name = "Niveau";
    niveau.type = "REAL";
    p.programs.variables.push_back(niveau);
    hmi::HmiType mode;
    mode.id = p.allocate();
    mode.name = "T_MODE";
    mode.kind = hmi::HmiTypeKind::Enumeration;
    mode.values = {{"Arret", 0, "", ""}, {"Auto", 1, "", ""}};
    p.programs.types.push_back(mode);
    hmi::HmiType vanne;
    vanne.id = p.allocate();
    vanne.name = "T_VANNE";
    vanne.members = {{"Ouv", "REAL", "", ""}};
    p.programs.types.push_back(vanne);

    const auto is = [&](std::string_view type, std::string_view typed, std::string_view text, bool formula) {
        const auto c = hmi::cellValue(&p, type, typed);
        check(c.ok() && c.text == text && c.formula == formula,
              std::string(type) + " : " + std::string(typed) + " -> " + std::string(text) + (formula ? " (formule)" : "") + " (lu : " + c.text
                  + (c.formula ? ", formule" : "") + (c.why.empty() ? std::string{} : ", " + c.why) + ")");
    };
    const auto refused = [&](std::string_view type, std::string_view typed, std::string_view reason) {
        const auto c = hmi::cellValue(&p, type, typed);
        check(!c.ok() && c.text.empty() && c.why.find(reason) != std::string::npos && c.why.back() != '.',
              std::string(type) + " : " + std::string(typed) + " refuse, « " + std::string(reason) + " » (" + c.why + ")");
    };
    is("REAL", "=Niveau * 2", "Niveau * 2", true);
    is("REAL", "= 3", "3", true);
    refused("REAL", "=", "rien apr\xC3\xA8s le =");
    is("REAL", "2,5", "2.5", false);
    is("REAL", "1 000,5", "1000.5", false);
    is("BOOL", "VRAI", "TRUE", false);
    is("BOOL", "non", "FALSE", false);
    is("TIME", "5 s", "T#5s", false);
    is("TIME", "T#1m30s", "T#1m30s", false);
    is("STRING", "Voiture", "'Voiture'", false);
    is("STRING", "'Voiture'", "'Voiture'", false);
    is("STRING", "Seuil * 2", "'Seuil * 2'", false);
    is("CHAR", "A", "'A'", false);
    is("T_MODE", "Auto", "T_MODE#Auto", false);
    is("T_MODE", "T_MODE#Arret", "T_MODE#Arret", false);
    refused("T_MODE", "Manu", "n'est pas une valeur de T_MODE (Arret, Auto)");
    is("STRING", "Niveau", "Niveau", true);
    is("REAL", "Niveau", "Niveau", true);
    is("REAL", "Niveau * 2", "Niveau * 2", true);
    is("REAL", "TO_REAL(3)", "TO_REAL(3)", true);
    refused("INT", "12.7", "n'est pas un nombre entier (INT)");
    refused("INT", "40000", "hors des bornes d'un INT");
    refused("REAL", "Bidule", "Bidule n'est pas un nom connu");
    refused("BOOL", "peut-etre", "n'est pas un bool\xC3\xA9" "en");
    is("ARRAY[1..4] OF REAL", "2,5", "2.5", false);
    is("ARRAY[1..4] OF REAL", "[1.0, 2.0]", "[1.0, 2.0]", false);
    is("T_VANNE", "(Ouv := 1.5)", "(Ouv := 1.5)", false);
    is("ANY", "Voiture", "'Voiture'", false);
    is("ANY", "12", "12", false);
    is("ANY_NUM", "2,5", "2.5", false);
    // Un nom connu la ou est la case seulement (une constante voisine d'une grille).
    const auto c = hmi::cellValue(&p, "INT", "MAX_N + 1", [](std::string_view n) { return n == "MAX_N"; });
    check(c.ok() && c.formula && c.text == "MAX_N + 1", "INT : MAX_N + 1, MAX_N connu de la grille : une formule (" + c.text + ")");
    check(hmi::cellValue(&p, "REAL", "").text.empty() && hmi::cellValue(&p, "REAL", "  ").ok(), "vide : rien, sans faute");

    std::printf("== la valeur initiale d'une variable : la meme lecture ==\n");
    const auto initial = [&](std::string_view type, std::string typed, std::string_view want) {
        std::string why;
        const bool ok = hmi::typedInitial(p, type, typed, &why);
        check(ok && typed == want, std::string(type) + " : " + std::string(want) + " (" + typed + (why.empty() ? std::string{} : ", " + why) + ")");
    };
    initial("REAL", "2,5", "2.5");
    initial("BOOL", "Vrai", "TRUE");
    initial("STRING", "Azote", "'Azote'");
    initial("TIME", "1m30s", "T#1m30s");
    initial("T_VANNE", "=(Ouv := 1.5)", "(Ouv := 1.5)");
    initial("ARRAY[1..3] OF INT", "[1, 2, 3]", "[1, 2, 3]");
    std::string text = "12.7", why;
    check(!hmi::typedInitial(p, "INT", text, &why) && why.rfind("valeur initiale : ", 0) == 0 && why.find("n'est pas un nombre entier") != std::string::npos,
          "INT : 12.7 refuse (" + why + ")");
}

// 1.12.19 (U5) : la simulation et l'API sous la meme regle - une case qui n'attend qu'une expression accepte
// le "=" de tete (une condition de point d'arret aussi).
void expressions() {
    std::printf("== une case d'expression : le = de tete accepte ==\n");
    namespace ef = ui::exprfield;
    check(ef::withoutEquals("=x > 3") == "x > 3" && ef::withoutEquals("  = Niveau ") == "Niveau" && ef::withoutEquals("x = 3") == "x = 3",
          "=x > 3 -> x > 3 ; un = au milieu reste (une comparaison)");
    check(ef::withoutEquals("==3") == "==3", "== reste (la faute se dira)");
}

} // namespace

int main() {
    words();
    numbers();
    durations();
    typed();
    shown();
    constants();
    cells();
    expressions();
    std::printf("%d controles, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
