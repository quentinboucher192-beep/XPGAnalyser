// =============================================================================
//  hmi/HmiBuildState.hpp - 1.11 (C4) : ce qui compile, ce qui part avec Generer
// -----------------------------------------------------------------------------
//  Les icones a droite de l'arbre du projet (maquette 1.11, scene 9) :
//    ✓  Compile        : ST, sans erreur ; le simulateur l'execute.
//    ✕n En erreur      : la premiere erreur dans l'infobulle ; la simulation saute.
//    ⊘  Non compilable : LD, FBD, IL ou SFC (section, DFB) ; C ou C++ (script).
//    ⇩  Genere         : le script part avec Generer.
//    ⇩̸  Non genere     : un script C ou C++, ou en erreur.
//
//  CE QUI COMPILE VRAIMENT, comme le simulateur (sim/Runtime.cpp, prepare) :
//  seul le ST est analyse ; une section d'un autre langage est sautee avec un
//  avertissement, un DFB au corps non ST « store their inputs but do nothing ».
//  Un script de l'IHM : seul le ST s'execute (HmiRuntime::runScript) ; C et C++
//  sont relus (checkScript), jamais executes ; un script ST en erreur ne part
//  pas avec Generer.
//
//  SANS ECRAN. L'arbre lit un CACHE (pas de recalcul a chaque image) : il est
//  refait au chargement et a Compiler, et un script l'est a la revision de son
//  document (`updateScript` ne recalcule que si le texte ou le langage change).
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiScript.hpp"
#include "../domain/ProjectModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hmi::build {

enum class Compile  : std::uint8_t { Ok, Error, NotCompilable };
enum class Generate : std::uint8_t { Yes, No, None };   // None : une section (le programme part entier)

struct State {
    Compile     compile{Compile::Ok};
    Generate    generate{Generate::None};
    int         errors{0};          // En erreur : le nombre (le simulateur s'arrete a la premiere)
    int         firstLine{0};       // la ligne de la premiere erreur (0 : inconnue)
    std::string reason;             // l'infobulle : la raison en une phrase, en francais
    std::string simMessage;         // le message du simulateur, cite tel quel (vide : aucun)
    [[nodiscard]] bool compiles() const noexcept { return compile == Compile::Ok; }
    [[nodiscard]] bool generated() const noexcept { return generate != Generate::No; }
};

// Une section de l'automate (ou une sous-routine).
[[nodiscard]] State ofSection(const domain::Project&, const domain::Section&);
// Une section du corps d'un DFB (le message du simulateur est celui d'un DFB).
[[nodiscard]] State ofDfbSection(const domain::Project&, const domain::Pou& dfb, const domain::Section&);
// Un type DFB : le pire de ses sections (un corps non ST : non compilable).
[[nodiscard]] State ofDfb(const domain::Project&, const domain::Pou& dfb);
// Un script de l'IHM. `knownType` : les types IHM du projet (comme Compiler).
[[nodiscard]] State ofScript(const Script&, const TypeKnown& knownType = {});
[[nodiscard]] TypeKnown knownTypesOf(const Project&);

// Les glyphes et leur legende (la meme table pour l'arbre, la legende, l'essai).
[[nodiscard]] std::string_view compileGlyph(Compile) noexcept;      // "✓" "✕" "⊘"
[[nodiscard]] std::string_view generateGlyph(Generate) noexcept;    // "⇩" (Yes, No : barre par l'arbre) ; "" (None)
[[nodiscard]] std::string tooltip(const State&);                    // la raison, et Generer pour un script
struct LegendRow { std::string_view glyph; std::string_view label; std::string_view meaning; bool struck{false}; };   // struck : le glyphe est barre a l'ecran
[[nodiscard]] const std::vector<LegendRow>& legend();               // les cinq icones

// ---- Le cache que l'arbre lit ----------------------------------------------
class Cache {
public:
    // Tout, au chargement et a Compiler (F7). L'un ou l'autre projet peut manquer.
    void rebuild(const domain::Project* plc, const Project* hmi);
    void clear();
    // Un script, a la revision de son document : rien n'est recalcule si son
    // texte et son langage n'ont pas change.
    void updateScript(const Script&, const TypeKnown& knownType = {});
    void removeScript(Id);
    // Les scripts du projet (generaux et de vue) : chacun par updateScript, et
    // ceux qui ont disparu sont retires (le « (n) » des filtres ne les compte plus).
    void syncScripts(const Project&);

    [[nodiscard]] const State* section(domain::Index) const noexcept;   // sections et sous-routines
    [[nodiscard]] const State* dfb(domain::Index pou) const noexcept;
    [[nodiscard]] const State* dfbSection(domain::Index section) const noexcept;
    [[nodiscard]] const State* script(Id) const noexcept;

    // Les deux filtres : « Ce qui ne compile pas (n) » (✕ et ⊘) et « Ce qui
    // n'est pas genere (n) » (les scripts).
    [[nodiscard]] int notCompiling() const noexcept;
    [[nodiscard]] int notGenerated() const noexcept;
    // 1.11.23 : ceux de l'automate seulement (sections, corps des DFB) - les puces de l'explorateur.
    [[nodiscard]] int notCompilingApi() const noexcept;
    // Le titre d'un dossier : « 2 ✕/⊘ · 2 non generes » (vide : tout va bien).
    [[nodiscard]] static std::string folderSummary(int notCompiling, int notGenerated);

    // Pour l'essai : combien d'etats ont ete CALCULES (pas lus) depuis le debut,
    // et un numero qui change a chaque changement (l'arbre se redessine).
    [[nodiscard]] std::uint64_t computations() const noexcept { return computations_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
    struct ScriptEntry { State state; ScriptLang lang{ScriptLang::ST}; std::size_t bodyHash{0}; };
    std::unordered_map<domain::Index, State> sections_, dfbs_, dfbSections_;
    std::unordered_map<Id, ScriptEntry>      scripts_;
    std::uint64_t computations_{0};
    std::uint64_t generation_{0};
};

} // namespace hmi::build
