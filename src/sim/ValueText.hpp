// =============================================================================
//  sim/ValueText.hpp - 1.12.19 : LA LECTURE D'UNE VALEUR TAPEE, partout la meme
// -----------------------------------------------------------------------------
//  La regle des cases : une valeur fixe se tape telle qu'on la lit, sans « = » ni
//  apostrophes, et se lit dans le type de la case. Ces lecteurs sont les seuls : la
//  constante d'une case (hmi::argumentLiteral), le forcage (Simulation > Automate, les
//  tables d'animation, les variables simulees de l'IHM, le simulateur de l'automate dans
//  l'IHM), les valeurs initiales. Avant, six lectures differentes du meme texte : T#5s
//  valait 5 ms dans deux d'entre elles, OUI etait refuse dans l'API, 12.7 s'arrondissait
//  en silence dans un INT, 40000 s'y repliait en -25536.
//
//    booleen   TRUE VRAI OUI ON YES 1 / FALSE FAUX NON OFF NO 0, sans casse
//    nombre    12 ; -3 ; 12,5 ou 12.5 ; 1 000 ou 1_000 ; 1e3 ; une base 16#FF 8#17 2#1010 ;
//              un prefixe de type INT#5, REAL#1.5
//    entier    sans decimale (12.7 dans un INT : refuse, pas arrondi), dans ses bornes
//              (40000 dans un INT : refuse, pas replie)
//    duree     T#1h2m3s4ms, TIME#1.5s, 5s, 5 s, 1m30s, 250 ms, 2 min, 1 h 30, 00:05 ; un nombre seul : des ms
//    date      2026-10-09 ou D#2026-10-09 ; 14:30 ou TOD#14:30 ; 2026-10-09-14:30 ou DT#...
//    texte     tel quel ; 'entre apostrophes' ou "entre guillemets" aussi (elles partent)
// =============================================================================
#pragma once

#include "Value.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace sim {

// Les mots d'un booleen (sans casse ; les blancs autour ne comptent pas).
[[nodiscard]] bool trueWord(std::string_view text) noexcept;
[[nodiscard]] bool falseWord(std::string_view text) noexcept;

// Un nombre tape : la virgule ou le point, des espaces (fines ou insecables) ou des _
// entre les chiffres, un signe, un exposant. Pas de base (16#FF : readTyped).
[[nodiscard]] bool readDecimal(std::string_view text, double& out) noexcept;
// Un entier tape, les memes ecritures sans decimale : 12, 1 000, -3, 1e3. Faux : illisible ou decimal
// (12.7 ne s'arrondit pas).
[[nodiscard]] bool readInteger(std::string_view text, long long& out) noexcept;

// Une duree tapee : T#1h2m3s4ms, TIME#1.5s, 5s, 5 s, 1,5 s, 1m30s, 250 ms, 2 min, 1 h 30, 00:05
// (mm:ss), 01:30:00 ; un nombre seul dans l'unite `bareMs` (des millisecondes ; 1000 : des secondes,
// pour une case « (s) ») - apres T#, toujours des millisecondes. Faux : illisible. La seule lecture
// d'une duree (hmi::roles::parseDuration la prend).
[[nodiscard]] bool readDuration(std::string_view text, std::int64_t& ms, double bareMs = 1.0) noexcept;

// Les bornes d'un entier (faux : pas un entier). ULINT et LWORD s'arretent a 2^63-1.
[[nodiscard]] bool integerBounds(Type type, long long& lo, long long& hi) noexcept;
// « -32 768 à 32 767 » (des espaces fines entre les milliers).
[[nodiscard]] std::string boundsText(Type type);
// 32767 -> « 32 767 ».
[[nodiscard]] std::string thousands(long long n);

// LA VALEUR D'UNE CASE DE TYPE `type`, ECRITE COMME UNE VALEUR FIXE (pas une
// expression). Unknown : devinee (une chaine entre apostrophes, un booleen, une duree,
// un nombre). Faux : `why` dit pourquoi, en une phrase (« 12.7 » n'est pas un nombre
// entier (INT) ; 40000 est hors des bornes d'un INT (-32 768 à 32 767)).
bool readTyped(std::string_view text, Type type, Value& out, std::string& why);

// Ce que la case montre d'une valeur, et qu'on peut retaper tel quel : TRUE, 12, 12.5,
// T#1s500ms, 2026-10-09, le texte sans apostrophes.
[[nodiscard]] std::string fixedText(const Value& value);

} // namespace sim
