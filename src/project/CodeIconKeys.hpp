// =============================================================================
//  project/CodeIconKeys.hpp - 1.8.0 : l'icone choisie de chaque element du projet
// -----------------------------------------------------------------------------
//  Le catalogue est dans core/CodeIcons.hpp ; ici, ce qui le relie au projet :
//  la cle d'une section, d'une unite, d'un bloc, d'un type ; l'icone choisie
//  (sans la casse : le ST ne la distingue pas) ; la commande qui la change (un
//  seul Ctrl+Z pour plusieurs elements choisis) ; et le renommage, que
//  RenameCommand appelle pour que l'icone suive le nom.
// =============================================================================
#pragma once

#include "../core/CodeIcons.hpp"
#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace project::codeicons {

using core::codeicons::Kind;

// "section:MAST/Init", "section:Logigrammes_A/Init", "dfbsection:CAPTEUR/Code".
[[nodiscard]] std::string keyForSection(const domain::Project&, domain::Index section);
// "unit:Logigrammes_A" (une unite de programme), "dfb:CAPTEUR" (un type DFB) ; vide sinon.
[[nodiscard]] std::string keyForPou(const domain::Project&, domain::Index pou);
[[nodiscard]] std::string keyForType(const domain::Project&, domain::Index derivedType);
[[nodiscard]] Kind        kindOfSection(const domain::Project&, domain::Index section);

// L'icone choisie (l'indice du catalogue), sans la casse ; -1 : aucune.
[[nodiscard]] int iconOf(const domain::Project&, std::string_view key);
[[nodiscard]] int sectionIcon(const domain::Project&, domain::Index section);
[[nodiscard]] int pouIcon(const domain::Project&, domain::Index pou);
[[nodiscard]] int typeIcon(const domain::Project&, domain::Index derivedType);

// Un element renomme : sa cle (exacte), ou toutes celles qui commencent par
// `fromPrefix` (le proprietaire : "section:Logigrammes_A/").
void renameKey(domain::Project&, std::string_view from, std::string_view to);
void renamePrefix(domain::Project&, std::string_view fromPrefix, std::string_view toPrefix);

// Choisir (ou retirer : icone vide) l'icone de plusieurs elements, en une commande.
class SetCodeIconsCommand final : public core::ICommand {
public:
    // (cle, cle du catalogue ; vide : retirer)
    SetCodeIconsCommand(std::shared_ptr<domain::Project> p, std::vector<std::pair<std::string, std::string>> changes, std::string label);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override { return label_; }

private:
    std::shared_ptr<domain::Project>                    project_;
    std::vector<std::pair<std::string, std::string>>    changes_;
    std::vector<std::pair<std::string, std::string>>    before_;   // (cle trouvee, ancienne valeur ; vide : absente)
    std::string                                         label_;
};

} // namespace project::codeicons
