// =============================================================================
//  project/RenameCommands.hpp - lot API 5 : renommer, et tout ce qui suit
// -----------------------------------------------------------------------------
//  Renommer dans Control Expert oblige a chercher soi-meme ce qui portait
//  l'ancien nom. Ici, UNE commande renomme et reecrit ce qui en depend :
//
//   * une VARIABLE : le code qui la nomme (les sections ou elle est visible :
//     toutes pour une globale, celles de son unite ou de son DFB sinon), les
//     lignes des tables d'animation ;
//   * un CHAMP de DDT : les acces « .champ » du code et des tables - refuse
//     quand un autre type (DDT ou DFB) a un membre du meme nom, car le texte
//     seul ne dit pas lequel est vise ;
//   * un TYPE DERIVE ou un BLOC DFB : le type des variables, des champs, des
//     parametres (« ARRAY[0..1] OF armoire » compris) ;
//   * une UNITE DE PROGRAMME : l'unite des tables d'animation qu'elle porte ;
//   * une SECTION (ou une sous-routine) : les appels « SR_Nom(); » du code.
//
//  Le nom est verifie (un identifiant Control Expert, libre la ou il vit). La
//  commande garde le projet d'avant : un Ctrl+Z le rend tel quel.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace project {

class RenameCommand final : public core::ICommand {
public:
    enum class What : std::uint8_t { Variable, Field, DerivedType, Block, Unit, Section };
    RenameCommand(std::shared_ptr<domain::Project> p, What what, domain::Index index, std::string newName);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
    // Ce que la commande a reecrit : les sections touchees (pour le message).
    [[nodiscard]] std::size_t sectionsRewritten() const noexcept { return sections_; }
private:
    std::shared_ptr<domain::Project>   project_;
    What                               what_;
    domain::Index                      index_;
    std::string                        newName_, oldName_;
    std::optional<domain::Project>     before_;
    std::size_t                        sections_{0};
};

// La raison du refus d'un nouveau nom ("" : accepte), sans rien changer.
[[nodiscard]] std::string renameProblem(const domain::Project&, RenameCommand::What, domain::Index, std::string_view newName);

// Remplace dans du code ST l'identifiant `from` par `to` (sans la casse),
// hors commentaires et chaines ; `member` : seulement apres un point
// (« x.champ »), sinon seulement en racine. Rend le nombre de remplacements.
std::size_t renameInCode(std::string& code, std::string_view from, std::string_view to, bool member);

} // namespace project
