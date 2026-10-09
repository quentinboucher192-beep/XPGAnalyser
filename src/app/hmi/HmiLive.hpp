// =============================================================================
//  app/hmi/HmiLive.hpp - 1.11.21 : LES DIAGNOSTICS EN DIRECT
// -----------------------------------------------------------------------------
//  Les volets de code (scripts generaux et de vue, fonctions IHM et de symbole,
//  operateurs) n'ont plus de bandeau Diagnostics sous l'editeur : ce qu'ils
//  trouvent pendant la frappe (les memes controles que Compiler, sans
//  l'attendre) va au panneau du bas, onglet Diagnostics, en tete (l'etape
//  "Saisie"). Les fautes restent soulignees dans le code, et la barre du volet
//  les compte.
//
//  L'ecran, a chaque image, cherche le volet de code montre (HmiLiveSource) :
//  s'il a change (un autre document, une frappe : liveRevision), le panneau
//  reprend ses diagnostics. Un clic sur l'une de ces lignes ramene le volet a sa
//  place (goToLive) - le curseur sur la faute, ou sa declaration dans son onglet.
// =============================================================================
#pragma once

#include "../../hmi/HmiPipeline.hpp"
#include "../../hmi/HmiScript.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace app {

class HmiLiveSource {
public:
    virtual ~HmiLiveSource() = default;
    // Croit a chaque calcul (une frappe, un autre document) : l'ecran ne relit que si elle change.
    [[nodiscard]] virtual std::uint64_t liveRevision() const noexcept = 0;
    // La cle de build du document montre ("script:12", "fonction:7", "symbole:3"...) ; vide : aucun.
    [[nodiscard]] virtual std::string liveElement() const = 0;
    // Ses diagnostics, prets pour le panneau (l'etape "Saisie", de quoi y revenir).
    [[nodiscard]] virtual std::vector<hmi::pipeline::Diagnostic> liveDiagnostics() const = 0;
    // Un clic sur l'un d'eux : le curseur a sa place (ou sa declaration, dans son onglet).
    virtual void goToLive(const hmi::pipeline::Diagnostic&) = 0;
};

// L'etape des diagnostics en direct (la colonne Etape du panneau).
inline constexpr const char* kLiveStep = "Saisie";

// Un diagnostic d'un volet pour le panneau : sa gravite, son message, sa ligne et sa colonne ;
// `element` (sa cle de build), `path` (ce que montre la colonne Element), `category` ("Script",
// "Fonction", "Op\xC3\xA9rateur"), `suggestion` (une correction proposee : vide, aucune).
[[nodiscard]] inline hmi::pipeline::Diagnostic liveDiagnostic(const hmi::ScriptDiagnostic& d, const std::string& element,
                                                             const std::string& path, const std::string& category,
                                                             const std::string& suggestion = {}) {
    hmi::pipeline::Diagnostic x;
    x.severity = d.severity == hmi::ScriptDiagnostic::Severity::Error     ? hmi::pipeline::Severity::Error
               : d.severity == hmi::ScriptDiagnostic::Severity::Warning ? hmi::pipeline::Severity::Warning
                                                                         : hmi::pipeline::Severity::Information;
    x.code = category;
    x.category = category;
    x.message = d.message;
    x.element = element;
    x.path = path;
    x.line = d.line;
    x.column = d.column;
    x.length = d.length;
    x.step = kLiveStep;
    x.suggestion = suggestion;
    return x;
}

} // namespace app
