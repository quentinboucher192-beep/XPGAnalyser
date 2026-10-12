// =============================================================================
//  hmi/HmiClock.hpp - 1.12.12 (lot H) : L'HORLOGE LIEE A UNE VARIABLE
// -----------------------------------------------------------------------------
//  Sans « Heure affichee » (value), l'horloge montre l'heure de l'IHM, comme
//  avant. Avec, elle montre l'heure que porte la valeur :
//   - une TOD (TIME_OF_DAY) : une heure du jour (la date : celle de l'IHM) ;
//   - une DT (DATE_AND_TIME) : une date et heure ; une DATE : un jour, a minuit ;
//   - une STRING ISO 8601 (2026-10-12T14:30:00, 2026-10-12 14:30, 14:30:05 ;
//     terminee par Z ou +02:00 : son fuseau est dit) ou 12/10/2026 14:30 ;
//   - un REAL (ou un entier) en secondes : moins d'un jour, une heure du jour
//     (3600 : 01:00:00) ; au-dela, des secondes depuis le 01/01/1970 (un
//     horodatage Unix, en UTC) ;
//   - un TIME : la duree depuis minuit.
//  LE FUSEAU (timeZone) dit ou l'on regarde l'heure : local (le PC, comme
//  avant), UTC, ou un decalage en heures (UTC+2, UTC-5, UTC+5:30). L'heure de
//  l'IHM et une valeur qui porte son fuseau (un horodatage Unix, une chaine en Z
//  ou +02:00) y sont converties ; une heure du mur (DT, DATE, TOD, une chaine
//  sans fuseau) se montre telle quelle : elle n'a pas de fuseau.
//  LE FORMAT de la date (dateFormat) : jj/mm/aaaa (comme avant), jj/mm/aa,
//  aaaa-mm-jj, jj mmm aaaa (12 oct. 2026), jour jj mois aaaa (lundi 12 octobre
//  2026) ; l'heure (hourFormat) sur 24 h (comme avant) ou sur 12 h (02:30 PM).
//  Le style analogique et le style numerique les suivent tous les deux.
//
//  Sans ecran ni moteur : le dessin (HmiControlsPainter) lit la copie evaluee
//  (la valeur dans le role Date : son texte exact) et appelle ces fonctions.
// =============================================================================
#pragma once

#include "HmiControls.hpp"

#include <string>
#include <string_view>

namespace hmi::clock {

inline constexpr std::string_view kDateFormats[] = {"jj/mm/aaaa", "jj/mm/aa", "aaaa-mm-jj", "jj mmm aaaa", "jour jj mois aaaa"};
inline constexpr std::string_view kHourFormats[] = {"24 h", "12 h"};
inline constexpr std::string_view kZones[] = {"local", "UTC", "UTC+1", "UTC+2", "UTC+3", "UTC+4", "UTC+5", "UTC+5:30", "UTC+8",
                                              "UTC+9", "UTC+10", "UTC-3", "UTC-4", "UTC-5", "UTC-6", "UTC-7", "UTC-8"};

struct Zone {
    enum class Kind { Local, Utc, Offset };
    Kind kind{Kind::Local};
    int  minutes{0};      // Offset : le decalage sur UTC
};
// "local" (vide aussi), "UTC" (GMT, Z), "UTC+2", "UTC-5", "UTC+5:30", "+2", "-3,5", "2" ; faux : illisible
// (`out` reste local).
bool parseZone(std::string_view text, Zone& out);
// Un instant (secondes depuis le 01/01/1970, UTC) dans un fuseau.
[[nodiscard]] DateTime civilOf(double epochSeconds, const Zone& zone);

struct Reading {
    bool        ok{true};        // faux : la valeur ne se lit pas comme une heure (### a l'ecran)
    DateTime    time;            // ce que montre l'horloge
    bool        hasDate{true};   // faux : une heure seule (la date est celle de `today`)
    std::string why;
};
// LA VALEUR D'UNE HORLOGE : le texte de la copie evaluee (role Date : 2026-10-09 14:30:00, 14:30:00,
// 2026-10-09, un nombre, T#1h30m, la STRING telle quelle) ou une valeur fixe tapee (DT#..., TOD#...).
// `today` : la date de l'IHM dans le fuseau (pour une heure seule).
[[nodiscard]] Reading read(std::string_view value, const Zone& zone, const DateTime& today);

// La date selon son format ; un format inconnu : jj/mm/aaaa.
[[nodiscard]] std::string formatDate(const DateTime& d, std::string_view format);
// L'heure : 14:30:05 (24 h) ou 02:30:05 PM (12 h) ; sans les secondes : 14:30.
[[nodiscard]] std::string formatTime(const DateTime& d, bool twelve, bool seconds);
// 0 lundi ... 6 dimanche.
[[nodiscard]] int weekdayOf(const DateTime& d);

} // namespace hmi::clock
