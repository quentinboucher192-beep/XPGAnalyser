// =============================================================================
//  hmi/HmiAlarmViews.hpp - les objets des alarmes du lot 11 : ce qu'ils
//                          montrent et ou ils se cliquent
// -----------------------------------------------------------------------------
//  BANDEAU D'ALARME     une ligne en haut ou en bas de l'ecran : l'alarme la
//                       plus grave (ou la plus recente, ou chacune a son tour),
//                       son heure, son message ; "+3" les autres ; Acquitter.
//  COMPTEUR D'ALARMES   un nombre (a acquitter, actives, en cours, mises de
//                       cote) sur la couleur de la priorite la plus forte.
//  RESUME PAR ZONE      une tuile par zone (groupe d'alarmes) : combien, la
//                       couleur de la plus grave ; un clic choisit la zone.
//  CONSIGNE D'ALARME    ce que fait l'operateur pour l'alarme choisie.
//  STATISTIQUES         les alarmes les plus frequentes (ou les plus longues),
//                       sur une periode : combien de fois, combien de temps.
//
//  Le dessin et le clic lisent la meme geometrie (comme le gestionnaire de
//  recettes) ; le moteur tient les alarmes (Runtime::alarms, shelvedAlarms).
// =============================================================================
#pragma once

#include "HmiHistory.hpp"
#include "HmiRuntime.hpp"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// "2026-09-22 07:40:12.350" -> secondes (jours civils, sans fuseau : seules les
// differences comptent) ; -1 : ce n'est pas une date.
[[nodiscard]] double stampToSeconds(std::string_view);
// Le groupe d'une alarme passe-t-il le filtre (vide ou "*" : tous) ?
[[nodiscard]] bool inGroup(std::string_view alarmGroup, std::string_view filter) noexcept;
// 1.9 : une alarme passe-t-elle le filtre d'un objet d'alarmes (son groupe declare,
// son groupe interne d'objet, ses symboles ; voir alarmGroupMatches, HmiObjectAlarms.hpp) ?
[[nodiscard]] bool inGroup(const LiveAlarm&, std::string_view filter) noexcept;
[[nodiscard]] bool inGroup(const ShelvedAlarm&, std::string_view filter) noexcept;
[[nodiscard]] bool inGroup(const AlarmOccurrence&, std::string_view filter) noexcept;

// ---- le bandeau -------------------------------------------------------------------------
//  Parmi `alarms` (deja classees : les plus graves d'abord, puis les plus
//  recentes), celle que montre le bandeau : son indice, -1 : aucune.
//  mode : "la plus grave" (la premiere a acquitter, sinon la premiere),
//  "la plus recente" (la derniere apparue), "defilement" (chacune `periodMs`).
//  `step` : les "suivante" demandees depuis le lancement.
[[nodiscard]] int bannerPick(const std::vector<LiveAlarm>&, std::string_view group, std::string_view mode, double seconds,
                             int periodMs, int step);
[[nodiscard]] std::size_t bannerCount(const std::vector<LiveAlarm>&, std::string_view group);
struct BannerLayout {
    Box    stripe, time, message, count, ack;   // count et ack : vides quand ils ne sont pas montres
    double fontSize{15};
};
[[nodiscard]] BannerLayout bannerLayout(const Object&, double w, double h);
// "acquitter", "suivante", ou "" : le reste du bandeau (son alarme est choisie, ses actions partent).
[[nodiscard]] std::string bannerHit(const Object&, double w, double h, double x, double y);

// ---- le compteur ---------------------------------------------------------------------------
//  count : "a acquitter", "actives", "en cours", "mises de cote".
[[nodiscard]] std::size_t alarmCount(const std::vector<LiveAlarm>&, const std::vector<ShelvedAlarm>&, std::string_view group,
                                     std::string_view count);
// La priorite la plus forte (1 = critique ; 0 : aucune) des alarmes du groupe,
// a acquitter seulement ou toutes celles en cours.
[[nodiscard]] int strongestPriority(const std::vector<LiveAlarm>&, std::string_view group, bool unackedOnly);
// 1.11 (R111) : l'alarme la plus forte du groupe (la priorite la plus forte, puis une a
// acquitter, puis la premiere) - celle qui donne au compteur sa couleur (celle de son
// groupe d'alarmes s'il en a une, app::alarmColor). nullptr : aucune.
[[nodiscard]] const LiveAlarm* strongestAlarm(const std::vector<LiveAlarm>&, std::string_view group, bool unackedOnly);

// ---- le resume par zone ----------------------------------------------------------------------
struct ZoneSummary {
    std::string name;
    std::size_t active{0}, unacked{0}, current{0}, shelved{0};
    int         highest{0};          // la priorite la plus forte des alarmes en cours (0 : aucune)
};
// Les zones : `groups` (a;b;c), sinon les groupes des alarmes du projet, dans leur ordre.
[[nodiscard]] std::vector<std::string> zonesOf(const Project&, std::string_view groups);
[[nodiscard]] std::vector<ZoneSummary> zoneSummaries(const Project&, const std::vector<LiveAlarm>&,
                                                     const std::vector<ShelvedAlarm>&, std::string_view groups);
// 1.11 (R111) : l'alarme la plus forte d'une zone (comme zoneSummaries la compte) - celle qui
// donne a sa tuile sa couleur (celle de son groupe d'alarmes s'il en a une). nullptr : aucune.
[[nodiscard]] const LiveAlarm* strongestZoneAlarm(const Project&, const std::vector<LiveAlarm>&, std::string_view zone);
// Les tuiles, `perRow` par ligne ("perRow" de l'objet), dans le repere de l'objet.
[[nodiscard]] std::vector<Box> summaryTiles(const Object&, double w, double h, std::size_t zones);
[[nodiscard]] std::string summaryHit(const Object&, double w, double h, double x, double y, std::size_t zones);   // "zone:2"

// ---- les lignes d'une liste d'alarmes (objet Historique) -----------------------------------------
//  Source "alarmes" ou "acquittees" : les indices des lignes dans `alarms` ;
//  source "mises de cote" : dans `shelved`. Filtrees sur le groupe.
[[nodiscard]] std::vector<std::size_t> historyAlarmRows(const std::vector<LiveAlarm>&, std::string_view source, std::string_view group);
[[nodiscard]] std::vector<std::size_t> historyShelvedRows(const std::vector<ShelvedAlarm>&, std::string_view group);
inline constexpr double kHistoryHeaderH = 26, kHistoryRowH = 22;
// "ligne:3" (0 = la premiere), ou "" (l'en-tete, sous la derniere ligne).
[[nodiscard]] std::string historyHit(double w, double h, double x, double y, std::size_t rows);

// ---- la consigne -------------------------------------------------------------------------------
//  L'alarme dont la consigne se montre : `alarmProp` (un nom) ; vide : la
//  choisie (`selected`), sinon la premiere a acquitter, sinon la premiere en cours.
[[nodiscard]] const LiveAlarm* instructionAlarm(const std::vector<LiveAlarm>&, std::string_view alarmProp, std::string_view selected);

// ---- les statistiques ------------------------------------------------------------------------------
struct AlarmStatRow {
    std::string name, group;
    int         priority{3};
    std::size_t count{0};
    double      seconds{0};          // le temps passe active
    bool        current{false};      // en cours en ce moment
};
inline constexpr std::string_view kStatRanges[] = {"depuis le lancement", "24 h", "7 jours", "tout l'historique"};
//  range : "depuis le lancement" (les terminees du moteur, et celles en cours),
//  "24 h", "7 jours", "tout l'historique" (l'historique garde, et celles en cours).
//  sort : "nombre" ou "duree". `kept` : l'historique garde (nul : celui du moteur).
[[nodiscard]] std::vector<AlarmStatRow> alarmStatistics(const std::deque<AlarmOccurrence>& sinceStart,
                                                        const std::vector<AlarmOccurrence>* kept,
                                                        const std::vector<LiveAlarm>& live, std::string_view range,
                                                        std::string_view group, std::string_view sort, std::size_t top,
                                                        std::string_view nowStamp);

} // namespace hmi
