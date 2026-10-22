// =============================================================================
//  hmi/HmiPidLoop.hpp - 1.12.24 : le Reglage PID en marche
// -----------------------------------------------------------------------------
//  CE QUE LE MOTEUR GARDE DE CHAQUE OBJET Reglage PID : la boucle qu'il lit
//  (ses formules resolues, ses echelles, la forme et les unites du bloc), ce
//  qu'il a lu a la derniere periode, ce qu'il apprend (pid::Learner, garde
//  dans l'historique : History::pid) et les reglages qu'il a ecrits (le
//  Journal, Revenir). Et sa fenetre, une a la fois, par-dessus la vue :
//  Statistiques, Ce qu'il a appris, Reglage propose, Journal.
//
//  Le moteur la tient (HmiRuntimePid.cpp) ; la carte et la fenetre se
//  dessinent dans app/hmi/HmiPidPainter.cpp, qui lit ceci sans rien changer.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiPid.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// La boucle telle qu'il la lit : les formules (vides : absentes), les echelles, le bloc.
struct PidBinding {
    std::string sp, pv, out, autoMode, kp, ti, td;
    std::string title;               // le titre de la carte : le Titre, sinon la boucle, sinon le nom de l'objet
    std::string unit;                // l'unite de la mesure (vide : aucune)
    double      pvMin{0}, pvMax{0};  // l'etendue de la mesure (egales : inconnue)
    pid::Form   form{pid::Form::Standard};
    double      timeSeconds{1};      // l'unite de Ti et Td : 1 s, 60 (min), 0,001 (ms), 0,1 (dixiemes)
    bool        normalized{false};   // Kp du bloc normalise sur les etendues
    double      kpScale{1};          // le bloc garde Kp x kpScale
    bool        writes{true};        // Appliquer ecrit (apres confirmation)
    std::string signature;           // ce qui a servi a la resoudre (une formule changee : on recommence)
    // Les unites du bloc (pid::BlockUnits) : Ti et Td, et le Kp du bloc en Kp physique.
    [[nodiscard]] pid::BlockUnits units(double outSpan) const noexcept {
        pid::BlockUnits u;
        u.timeSeconds = timeSeconds > 0 ? timeSeconds : 1.0;
        double k = kpScale > 0 ? kpScale : 1.0;
        if (normalized && pvMax > pvMin && outSpan > 0) k *= (pvMax - pvMin) / outSpan;
        u.kpScale = k;
        return u;
    }
};

// Un reglage ecrit par l'objet (le Journal le garde : Revenir le remet).
struct PidApplied {
    double      t{0};                // horloge murale (s)
    pid::Gains  before, after;       // standard, secondes
    std::string goal;                // "Equilibre", "Revenir", "copie"...
    std::string user;
    double      predictedOvershoot{-1};
    bool        reverted{false};
};

struct PidLoop {
    Id          view{kNoId}, object{kNoId};
    std::string key;                 // Vue.Objet : le nom du fichier de sa memoire
    PidBinding  bind;
    pid::Learner learner;
    double      nextAt{-1};          // la prochaine lecture (horloge du moteur)
    double      wall{0};             // l'heure murale de la derniere lecture
    bool        read{false};         // la derniere lecture a donne SP, PV et OUT
    std::string error;               // ce qui manque, dit sur la carte
    double      sp{0}, pv{0}, out{0};
    bool        autoMode{true};
    std::optional<pid::Gains>       gains;   // le reglage lu dans le bloc (standard, secondes)
    std::optional<pid::BlockValues> block;   // les trois nombres du bloc, tels quels
    std::vector<PidApplied> applied;
    double      keptAt{-1};          // la derniere copie de la memoire dans l'historique
    bool        loaded{false};       // la memoire reprise de l'historique
};

// La fenetre de l'objet. `tab` : 0 Statistiques, 1 Ce qu'il a appris, 2 Reglage propose, 3 Journal ;
// `range` : 0 30 min, 1 8 h, 2 24 h, 3 7 jours.
struct PidWindow {
    bool        shown{false};
    Id          view{kNoId}, object{kNoId};
    int         tab{0};
    int         range{0};
    int         goal{-1};            // l'objectif choisi dans Reglage propose (-1 : celui de l'objet)
    bool        confirm{false};      // Appliquer : la confirmation demandee
    bool        forgetAsk{false};    // Oublier et reapprendre : la confirmation demandee
    int         revertAsk{-1};       // Revenir : le reglage du Journal a remettre (sa place)
    std::string message;             // ce qui vient de se passer (ecrit, refuse, copie)
    bool        messageBad{false};
    double      messageUntil{0};
    double      hoverT{-1};          // l'instant survole sur la courbe (la bulle) ; < 0 : aucun
    int         scroll{0};           // Journal : les lignes passees
    std::string clipboard;           // Copier les valeurs : le texte copie (l'ecran le pose dans le presse-papiers)
};

// La fenetre des statistiques : sa duree (s) et son nom.
[[nodiscard]] double           pidRangeSeconds(int range) noexcept;
[[nodiscard]] std::string_view pidRangeName(int range) noexcept;   // "30 min", "8 h", "24 h", "7 jours"
// L'objectif d'un objet : sa case Objectif ("Robuste", "Equilibre", "Rapide" ; accents ou non).
[[nodiscard]] pid::Goal        pidGoalOf(std::string_view text) noexcept;
[[nodiscard]] pid::Derivative  pidDerivativeOf(std::string_view text) noexcept;
// L'etat de la boucle, en mots et en ton (0 neutre, 1 bon, 2 a surveiller, 3 mauvais) : la pastille de la carte.
struct PidBadge {
    std::string text;
    int         tone{0};
};
[[nodiscard]] PidBadge pidBadge(const PidLoop&);
// Les nombres a ecrire dans le bloc pour un reglage (standard, secondes), dans sa forme et ses unites.
[[nodiscard]] pid::BlockValues pidBlockOf(const PidLoop&, const pid::Gains&);
// Un nombre du bloc tel qu'on l'ecrit : "1,74", "109 s", "0 s" (Ti et Td dans leur unite).
[[nodiscard]] std::string pidNumber(double v, int decimals = 2);
[[nodiscard]] std::string pidTime(double seconds);            // "4 min 08 s", "109 s", "0 s"
[[nodiscard]] std::string pidTimeUnitName(double timeSeconds);   // "s", "min", "ms", "0,1 s"
// Une ligne de la boucle par minute sur la fenetre (Exporter (CSV)).
[[nodiscard]] std::string pidCsv(const PidLoop&, double window);

} // namespace hmi
