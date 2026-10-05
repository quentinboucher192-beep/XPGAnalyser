// =============================================================================
//  help/TryBench.hpp — le petit banc d'essai de l'aide
// -----------------------------------------------------------------------------
//  CE QUI N'ALLAIT PAS AVEC « ESSAYER ».
//
//  Le bouton montait un projet de poche, lancait UN cycle, et affichait un
//  releve fige. Trois choses manquaient, et ce sont les trois qui font qu'on
//  comprend un bloc :
//
//    ON NE VOYAIT RIEN BOUGER. Un automate est une machine a etats : un front
//    montant, une temporisation, un anti-rebond ne se comprennent pas sur une
//    photo. Il faut que ca tourne.
//
//    ON NE POUVAIT PAS AGIR. Comprendre un bloc, c'est forcer une entree et
//    regarder ce que ca change. Sans table de forcage, on lit du code.
//
//    ON NE POUVAIT PAS EN SORTIR. Le releve remplacait l'article et rien ne
//    disait comment revenir. Un ecran dont on ne sait pas sortir passe pour
//    plante, et c'est le genre de chose qu'on ne signale meme pas.
//
//  CE FICHIER EST LE MOTEUR, ET RIEN QUE LUI. La marche/arret, le cycle, les
//  lignes de la table, le forcage, la courbe : tout se calcule ici, sans
//  widget, donc tout se verifie sans ecran. Le panneau qui l'affiche ne fait
//  que lire ce qu'il rend.
//
//  LE TEMPS VIENT DU DEHORS. `tick(dt)` recoit le temps de l'image courante et
//  decide combien de cycles lancer ; le banc n'a pas d'horloge a lui. C'est ce
//  qui permet a un test de faire passer douze secondes en trois appels, et
//  c'est la meme discipline que SimulationHost.
// =============================================================================
#pragma once

#include "HelpTryIt.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace help {

// ------------------------------------------------------------- une ligne ----
// Ce que la table d'animation montre. `changed` sert a mettre en evidence ce
// qui vient de bouger : dans trente lignes, la seule qui a change est celle
// qu'on cherche, et elle est invisible sans ca.
struct WatchRow {
    std::string name;
    std::string value;
    std::string type;        // "BOOL", "INT", ...
    bool        forced{false};
    bool        changed{false};
    bool        boolean{false};
    double      number{0.0};
};

// Un point de la courbe. On garde la valeur numerique : un BOOL vaut 0 ou 1,
// ce qui se trace comme un creneau et se lit comme un chronogramme.
struct TrendSample { std::uint32_t scan{0}; double value{0.0}; };

class TryBench {
public:
    enum class State : std::uint8_t { Stopped, Running, Paused };

    // Le banc prend la session deja construite : c'est la page d'aide qui sait
    // quel bloc est ouvert, et `TrySession::build` dit deja pourquoi quand elle
    // echoue.
    explicit TryBench(std::shared_ptr<TrySession> session);

    // --- la transport ---------------------------------------------------------
    void play();
    void pause();
    void stop();          // remet tout a zero, y compris les forcages
    void step();          // un cycle, quel que soit l'etat ; passe en Pause

    // Le temps de l'image. Rend le nombre de cycles reellement lances - zero la
    // plupart du temps, parce qu'un cycle dure 20 ms et une image 16.
    std::uint32_t tick(std::int64_t deltaMs);

    [[nodiscard]] State         state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t scans() const noexcept { return scans_; }
    [[nodiscard]] std::int64_t  elapsedMs() const noexcept { return elapsedMs_; }

    // La periode d'un cycle, en ms. C'est le CycleMs des blocs : le changer
    // change ce que les temporisations comptent, donc c'est un reglage visible
    // et pas une constante cachee.
    void setPeriodMs(std::int64_t ms);
    [[nodiscard]] std::int64_t periodMs() const noexcept { return periodMs_; }

    // --- ce qu'on regarde ------------------------------------------------------
    [[nodiscard]] const std::vector<WatchRow>& rows() const noexcept { return rows_; }
    [[nodiscard]] const std::vector<std::string>& notes() const;
    [[nodiscard]] const std::vector<std::string>& declared() const;
    [[nodiscard]] const std::string& example() const;
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }
    [[nodiscard]] bool ran() const noexcept { return ran_; }

    // --- agir -------------------------------------------------------------------
    // Rend false quand le nom est inconnu : une faute de frappe ne doit pas
    // passer pour un forcage sans effet.
    bool force(const std::string& name, const std::string& value);
    bool unforce(const std::string& name);
    // Bascule un BOOL et relance un cycle. C'est LE geste de la table
    // d'animation - double-clic sur une ligne - et il ne veut rien dire sur
    // autre chose qu'un booleen.
    bool toggle(const std::string& name);

    // --- la courbe ---------------------------------------------------------------
    // Ce qui est trace. Vide au depart : on choisit ce qu'on regarde, sinon la
    // courbe porte trente signaux et n'en montre aucun.
    void addToTrend(const std::string& name);
    void removeFromTrend(const std::string& name);
    [[nodiscard]] bool inTrend(const std::string& name) const;
    [[nodiscard]] const std::vector<std::string>& trendNames() const noexcept {
        return trendNames_;
    }
    [[nodiscard]] const std::deque<TrendSample>& trend(const std::string& name) const;

    // Les bornes de l'axe, sur tout ce qui est trace. Rendues ensemble parce
    // qu'une courbe par signal avec chacune son echelle ne se compare pas.
    struct Bounds { double low{0.0}, high{1.0}; };
    [[nodiscard]] Bounds trendBounds() const;

    // Combien de cycles la courbe garde. Au-dela, les plus vieux tombent : une
    // courbe qui garde tout finit par tenir la memoire d'un automate entier.
    static constexpr std::size_t kTrendDepth = 600;

private:
    void sample(bool first);

    std::shared_ptr<TrySession> session_;
    State                       state_{State::Stopped};
    std::uint32_t               scans_{0};
    std::int64_t                elapsedMs_{0};
    std::int64_t                periodMs_{20};
    std::int64_t                carryMs_{0};     // le reste du temps non consomme
    bool                        ran_{false};
    std::string                 failure_;

    std::vector<WatchRow>       rows_;
    std::vector<std::string>    trendNames_;
    std::vector<std::deque<TrendSample>> trends_;
    static const std::deque<TrendSample> kEmptyTrend;
};

// ---- le bloc, vu comme un bloc FBD ---------------------------------------------
//
//  Le banc dessine le bloc essaye avec ses broches, et sur chaque broche ce qui
//  y est branche et ce qui y passe. Deux choses a savoir pour ca, que l'exemple
//  dit sans les ecrire : QUELLE variable est l'instance, et QUOI est branche
//  sur chaque parametre.

// L'instance d'un type dans les declarations d'une session :
// "MOT_Station : DFB_EQ_MOTOR" -> "MOT_Station". Vide s'il n'y en a pas - un
// DDT, ou un exemple qui n'appelle pas le bloc.
[[nodiscard]] std::string instanceOf(const std::vector<std::string>& declared,
                                     std::string_view type);

// Les arguments du premier appel de `instance` dans l'exemple :
//     MOT_Station(Count := 6, CycleMs := 20,
//                 Eq := Moteurs);          (* sur plusieurs lignes *)
// -> {Count, 6}, {CycleMs, 20}, {Eq, Moteurs}. Une sortie ecrite "Q => x" a
// `output` vrai. Les commentaires sont retires, les blancs recompactes.
struct CallArgument {
    std::string param;
    std::string argument;
    bool        output{false};
};
[[nodiscard]] std::vector<CallArgument> callArguments(std::string_view example,
                                                      std::string_view instance);

} // namespace help
