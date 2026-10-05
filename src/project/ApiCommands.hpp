// =============================================================================
//  project/ApiCommands.hpp - lots API 3 et 4 : les commandes des onglets de l'API
// -----------------------------------------------------------------------------
//  Chaque modification faite dans un onglet de l'API est UNE commande : la pile
//  la montre dans l'historique, un Ctrl+Z la reprend, un Ctrl+Y la refait.
//
//   * les tables d'animation (lot 3) : creer, renommer, changer d'unite,
//     dupliquer, supprimer une table ; ajouter, retirer, deplacer des lignes -
//     une variable de l'automate ou une variable de l'IHM ;
//   * les taches (lot 4) : cyclique / periodique, periode, chien de garde ;
//     ajouter FAST (ou AUX0..3), retirer une tache vide ;
//   * le materiel (lot 4) : importer le .XHW dans le projet ouvert (sans le
//     reimporter en entier), retirer ou remplacer un module ;
//   * le plan memoire (lot 5) : les bornes de lecture de chaque zone.
//
//  Les indices (d'une table, d'une tache) sont ceux du moment ou la commande est
//  faite : la pile defait dans l'ordre inverse, ils restent donc justes.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace project {

using ApiProjectPtr = std::shared_ptr<domain::Project>;

// ------------------------------------------------------------- les noms ----
// Un nom d'identifiant Control Expert : une lettre, puis lettres, chiffres et _
// (pas deux _ de suite, pas de _ a la fin), 32 caracteres au plus. Rend la
// raison du refus ("" : accepte).
[[nodiscard]] std::string identifierProblem(std::string_view name);
// Le nom d'une table : un identifiant, libre parmi les tables (sauf `except`).
[[nodiscard]] std::string animationTableNameProblem(const domain::Project&, std::string_view name,
                                                    std::size_t except = static_cast<std::size_t>(-1));
// Un nom libre de la forme base, base_2, base_3...
[[nodiscard]] std::string freeAnimationTableName(const domain::Project&, std::string_view base);
// L'unite de programme qui portera une nouvelle table : celle des tables deja
// la, sinon la premiere unite ; "" s'il n'y en a pas (la table reste alors dans
// le projet, l'export ne l'ecrit pas).
[[nodiscard]] std::string defaultAnimationTableOwner(const domain::Project&);

// Une ligne, dite par son texte (l'onglet, les scripts, les tests).
struct AnimationLine {
    std::string name;
    bool        hmi{false};
};

// ------------------------------------------------ les tables d'animation ----
class AddAnimationTableCommand final : public core::ICommand {
public:
    // Une table neuve (lines vide) ou la copie d'une autre ; `at` : sa place
    // dans la liste (npos : a la fin).
    AddAnimationTableCommand(ApiProjectPtr p, std::string name, std::string owner,
                             std::vector<AnimationLine> lines = {}, std::size_t at = static_cast<std::size_t>(-1),
                             bool duplicate = false);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
private:
    ApiProjectPtr              project_;
    std::string                name_, owner_;
    std::vector<AnimationLine> lines_;
    std::size_t                at_;
    std::size_t                index_{static_cast<std::size_t>(-1)};
    bool                       duplicate_{false};
};

class RemoveAnimationTableCommand final : public core::ICommand {
public:
    RemoveAnimationTableCommand(ApiProjectPtr p, std::size_t table);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr                          project_;
    std::size_t                            table_;
    std::optional<domain::AnimationTable>  removed_;
    std::string                            name_;
};

class RenameAnimationTableCommand final : public core::ICommand {
public:
    RenameAnimationTableCommand(ApiProjectPtr p, std::size_t table, std::string name);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr     project_;
    std::size_t       table_;
    std::string       name_;
    domain::SymbolId  previous_{0};
    std::string       previousText_;
};

class SetAnimationTableOwnerCommand final : public core::ICommand {
public:
    SetAnimationTableOwnerCommand(ApiProjectPtr p, std::size_t table, std::string owner);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr     project_;
    std::size_t       table_;
    std::string       owner_;
    domain::SymbolId  previous_{0};
};

class AddAnimationLinesCommand final : public core::ICommand {
public:
    // Les lignes deja dans la table (meme nom, meme source) sont ignorees ; si
    // aucune n'est nouvelle, la commande echoue (rien a annuler).
    AddAnimationLinesCommand(ApiProjectPtr p, std::size_t table, std::vector<AnimationLine> lines,
                             std::size_t at = static_cast<std::size_t>(-1));
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] std::size_t added() const noexcept { return added_; }
private:
    ApiProjectPtr              project_;
    std::size_t                table_;
    std::vector<AnimationLine> lines_;
    std::size_t                at_;
    std::size_t                first_{0}, added_{0};
};

class RemoveAnimationLinesCommand final : public core::ICommand {
public:
    RemoveAnimationLinesCommand(ApiProjectPtr p, std::size_t table, std::vector<std::size_t> rows);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr                                           project_;
    std::size_t                                             table_;
    std::vector<std::size_t>                                rows_;
    std::vector<std::pair<std::size_t, domain::AnimationEntry>> removed_;   // (rang, ligne), croissant
};

class MoveAnimationLineCommand final : public core::ICommand {
public:
    // La ligne `from` va au rang `to` (compte apres son retrait).
    MoveAnimationLineCommand(ApiProjectPtr p, std::size_t table, std::size_t from, std::size_t to);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr project_;
    std::size_t   table_, from_, to_;
};

// ------------------------------------------------------------- les taches ----
[[nodiscard]] std::string taskKindLabel(const domain::Task&);    // "cyclique", "periodique, 5 ms"

class SetTaskCommand final : public core::ICommand {
public:
    SetTaskCommand(ApiProjectPtr p, std::size_t task, std::string type, std::uint32_t period, std::uint32_t watchdog);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr project_;
    std::size_t   task_;
    std::string   type_, oldType_;
    std::uint32_t period_, watchdog_, oldPeriod_{0}, oldWatchdog_{0};
};

// Lot API 5 : les bornes de lecture d'une zone du plan memoire (%M, %MW, %KW).
// `window.set == false` : toute la zone.
class SetMemoryWindowCommand final : public core::ICommand {
public:
    SetMemoryWindowCommand(ApiProjectPtr p, domain::MemoryZone zone, domain::MemoryWindow window);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr        project_;
    domain::MemoryZone   zone_;
    domain::MemoryWindow window_, old_{};
};

// Lot API 6 : une case d'une variable - son type, son adresse, sa valeur
// initiale, son commentaire (coller depuis Excel met a jour une variable qui
// existe). Le type se lit comme dans une declaration : "ARRAY[0..9] OF INT",
// un DDT, un DFB ; une adresse vide delocalise la variable. `Effective` : la
// variable du projet que recoit un parametre d'unite (l'attribut
// EffectiveParameter de l'export) ; vide : le parametre n'est plus relie.
class SetVariableFieldCommand final : public core::ICommand {
public:
    enum class Field : std::uint8_t { Type, Address, InitValue, Comment, Effective };
    SetVariableFieldCommand(ApiProjectPtr p, domain::Index variable, Field field, std::string value);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr     project_;
    domain::Index     variable_;
    Field             field_;
    std::string       value_;
    domain::Variable  old_{};
    std::string       name_;
};

// Lot API 6 : l'icone du projet (vide : le logo de l'application revient).
class SetProjectIconCommand final : public core::ICommand {
public:
    SetProjectIconCommand(ApiProjectPtr p, domain::ProjectIcon icon, std::string label = {});
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr       project_;
    domain::ProjectIcon icon_, old_;
    std::string         label_;
};

class AddTaskCommand final : public core::ICommand {
public:
    // FAST (periodique, 5 ms, chien de garde 100 ms), AUX0..AUX3 (periodique,
    // 100 ms, 500 ms). Le nom dit la tache : Control Expert n'en connait pas d'autres.
    AddTaskCommand(ApiProjectPtr p, std::string name);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr project_;
    std::string   name_;
    std::size_t   index_{static_cast<std::size_t>(-1)};
};

class RemoveTaskCommand final : public core::ICommand {
public:
    RemoveTaskCommand(ApiProjectPtr p, std::size_t task);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr                project_;
    std::size_t                  task_;
    std::optional<domain::Task>  removed_;
};

// ----------------------------------------------------------- le materiel ----
// Remplace la configuration materielle (racks, modules, memoire) par celle d'un
// .XHW lu a part ; l'identite du processeur (famille, reference, systeme,
// ressource) est gardee quand le projet la porte deja. Un Ctrl+Z rend l'ancienne.
class ReplaceHardwareCommand final : public core::ICommand {
public:
    ReplaceHardwareCommand(ApiProjectPtr p, domain::HardwareConfig next, std::string source);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr                          project_;
    domain::HardwareConfig                 next_;
    std::optional<domain::HardwareConfig>  previous_;
    std::string                            source_;
};

class RemoveModuleCommand final : public core::ICommand {
public:
    RemoveModuleCommand(ApiProjectPtr p, std::uint16_t rack, std::int16_t slot);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr                  project_;
    std::uint16_t                  rack_;
    std::int16_t                   slot_;
    std::optional<domain::Module>  removed_;
    std::size_t                    position_{0};
};

class ReplaceModuleCommand final : public core::ICommand {
public:
    ReplaceModuleCommand(ApiProjectPtr p, std::uint16_t rack, std::int16_t slot, std::string reference);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    ApiProjectPtr                  project_;
    std::uint16_t                  rack_;
    std::int16_t                   slot_;
    std::string                    reference_;
    std::optional<domain::Module>  previous_;
    std::size_t                    position_{0};
};

} // namespace project
