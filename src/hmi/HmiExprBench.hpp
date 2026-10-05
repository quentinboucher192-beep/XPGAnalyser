// =============================================================================
//  hmi/HmiExprBench.hpp - 1.11 (chantier T3, D5) : le champ d'essai de la page
//                         des expressions, sans ecran
// -----------------------------------------------------------------------------
//  LE VRAI MOTEUR, PAS UN SECOND. Le banc evalue ce que l'utilisateur tape avec
//  hmi::Expression (compile / evaluate) et le juge avec hmi::exprcheck (le meme
//  jugement que l'inspecteur). Il le fait :
//   - sur un petit projet IHM de demonstration, construit ici : T_VANNE (Pos,
//     Defaut, Bouge, Cmd), T_MODE (Arret, Auto, Manu, Defaut), V : ARRAY[0..3]
//     OF T_VANNE, Pression, Mode, Pompe_Marche, Debit_P3, Niveau_Cuve,
//     Vue_Demandee, SYS.UserLevel, et quatre vues ;
//   - ou sur le projet ouvert (onProject), en lecture seule : une ecriture est
//     refusee, un bloc n'est pas appele (la regle de TrialPlc).
//  Le champ d'essai ne touche jamais au projet.
//
//  CE QU'IL REND : le resultat et son type, une pastille pour une couleur ;
//  ou l'erreur soulignee, sa raison (toujours une, meme quand le moteur n'en
//  donne pas) et, quand elle est sure, la correction ("Remplacer par").
//
//  Tranche 1 : l'interface. Tranche 2 : le projet de demonstration, les regles
//  et l'essai expressions111().
// =============================================================================
#pragma once

#include "HmiExprGuide.hpp"

#include "../sim/Value.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace sim { class Environment; }

namespace hmi {
struct Project;
}

namespace hmi::exprguide {

struct Outcome {
    bool          ok{false};
    std::string   value;          // "'#E53935'", "42.0", "TRUE", "T#5s", "Ouverture : 42 %"
    std::string   type;           // "STRING", "REAL", "T_MODE"
    std::string   fits;           // "convient a une case couleur" (UTF-8)
    std::uint32_t swatch{0};      // 0xRRGGBBAA pour la pastille ; 0 : aucune
    std::size_t   errBegin{0};    // le soulignement, en octets du champ
    std::size_t   errEnd{0};
    std::string   reason;         // tutoie, dit quoi faire
    std::string   replacement;    // le champ entier corrige ; vide : pas de correction sure
};

class Bench {
public:
    Bench();                                   // les variables d'exemple
    ~Bench();
    Bench(const Bench&) = delete;
    Bench& operator=(const Bench&) = delete;

    // Le projet ouvert, en lecture seule ; `live` : la simulation si elle tourne
    // (nul : un simulateur vide, pour les fonctions standard).
    // 1.11 (tranche 8, decision du chef) : AUCUN EFFET DE BORD. Sans `live`, l'IHM
    // du projet n'est PAS demarree (Runtime::prime, pas start) : les variables IHM
    // valent leur valeur initiale, aucun script Demarrage ne tourne, aucune vue ne
    // s'ouvre (=Compteur_Clics + 1 vaut 1 si le compteur part de 0).
    void onProject(const Project& project, sim::Environment* live);
    // Revenir aux variables d'exemple.
    void onSamples();
    [[nodiscard]] bool usesSamples() const noexcept;
    // Sur quoi le champ juge, a dire a cote du resultat ; vide : les variables d'exemple.
    [[nodiscard]] std::string sourceNote() const;

    // Les glissieres, la liste, les cases : "V[0].Pos", "Pression", "Mode",
    // "Pompe_Marche", "V[0].Defaut". Faux : pas une variable d'exemple.
    bool setSample(std::string_view name, const sim::Value& value);
    [[nodiscard]] sim::Value sample(std::string_view name) const;

    // Le champ tel qu'il est tape (avec son "=", sauf texte a trous), juge pour
    // une case du type `as`.
    [[nodiscard]] Outcome evaluate(std::string_view field, const TypeEntry& as) const;

private:
    struct State;
    std::unique_ptr<State> s_;
};

} // namespace hmi::exprguide
