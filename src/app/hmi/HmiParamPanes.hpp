// =============================================================================
//  app/hmi/HmiParamPanes.hpp - 1.9 : les parametres des popups dans l'editeur
// -----------------------------------------------------------------------------
//  La sous-section Parametres | Infos de la fiche d'une popup (ou d'un
//  symbole : meme liste, mode fige sur Reference), les arguments de l'action
//  Ouvrir une popup (un champ par parametre, type incompatible en rouge avec la
//  raison, valeur par defaut dite), le choix du parametre d'Appliquer copie sur
//  reference, le repere "modifie, pas encore applique" d'une copie en marche,
//  et les DDT du programme de l'API pour le moteur et l'aide (setPlcTypes).
//
//  Les modifications de la liste passent par des champs "param19:..." de la
//  vue, que l'editeur applique DANS UNE COMMANDE ANNULABLE (applyParamField
//  dans hmi::changeProject) : Ctrl+Z les defait. Renommer passe par
//  hmi::params::renameParam (les emplois et les arguments des appelants
//  suivent).
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiPopupParams.hpp"
#include "../../platform/Renderer.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace domain { class Project; }
namespace hmi { class Runtime; }

namespace app::hmiparams {

// Les DDT du programme de l'API (noms, membres, type d'une variable globale) :
// pour Runtime::setPlcTypes, les types proposes et l'aide a la saisie. Vides
// sans programme charge.
[[nodiscard]] hmi::params::PlcTypes plcTypesOf(const domain::Project* plc);
// Le programme de l'API pour les volets qui ne l'ont pas (l'editeur d'actions) :
// pose par HmiAssist::installProgram ; nul sans programme.
void setProgram(std::function<std::shared_ptr<const domain::Project>()> plc);
[[nodiscard]] std::shared_ptr<const domain::Project> program();

// ---- la sous-section Parametres | Infos de la fiche de la vue ----------------
// La partie montree (un etat d'ecran, par vue ; pas dans le projet).
enum class Part : std::uint8_t { List, Infos };
[[nodiscard]] Part shownPart(hmi::Id view);
void               showPart(hmi::Id view, Part);

// Les champs de la sous-section : "param19:ajouter", "param19:<n>:nom",
// ":type", ":tableau", ":mode", ":defaut", ":description", ":ordre",
// "param19:partie", "param19:aller".
inline constexpr std::string_view kFieldPrefix = "param19:";
[[nodiscard]] bool isParamField(std::string_view field) noexcept;

// Les categories de la sous-section d'une vue popup ou symbole (vide pour une
// autre vue sans parametre). `commit(field, value)` : le meme chemin que les
// autres proprietes de la vue (HmiPropertyCommits::view).
using Commit = std::function<bool(const std::string& field, const std::string& value)>;
[[nodiscard]] std::vector<ui::PropertyGrid::Category> paramCategories(const hmi::Project&, const hmi::View&,
                                                                      const domain::Project* plc, const Commit& commit);

// Applique un champ de la sous-section a la vue `view` (A APPELER DANS
// hmi::changeProject). `ok` faux : refuse, `why` dit pourquoi ; `label` : le nom
// de la commande ("Renommer le param\xC3\xA8tre Moteur").
struct FieldResult {
    bool        ok{true};
    std::string why;
    std::string label;
};
FieldResult applyParamField(hmi::Project&, hmi::Id view, std::string_view field, const std::string& value);
// Le nom de la commande d'un champ (avant de l'appliquer).
[[nodiscard]] std::string fieldLabel(std::string_view field, const std::string& value);
// "param19:aller" : la cible d'un clic dans les infos ("Vue/Objet" ; objet vide : la vue).
struct Target {
    std::string view, object;
};
[[nodiscard]] Target parseTarget(const std::string& value);
// L'hote qui ouvre une autre vue sur un objet (l'appli) ; sans lui, seul un objet
// de la vue ouverte se choisit.
void setOpenObjectHost(std::function<void(const std::string& view, const std::string& object)> host);
void openObject(const std::string& view, const std::string& object);

// ---- l'action Ouvrir une popup / Changer de popup ----------------------------
// Le cadre "Parametres de <popup>" : une ligne par parametre (nom, type,
// pastille, l'argument donne). `setArgument(param, text)` reecrit les arguments
// de l'action (vide : retire l'argument, la popup prend sa valeur par defaut).
[[nodiscard]] ui::PropertyGrid::Category argumentsCategory(const hmi::Project&, const hmi::View* caller,
                                                           const hmi::View& popup, const std::string& arguments,
                                                           const domain::Project* plc,
                                                           const std::function<bool(const std::string& param, const std::string& text)>& setArgument);
// "Moteur := Pompes[3]; Nom := 'P3'" ou l'argument `param` vaut `text` (vide :
// retire) ; les arguments gardent l'ordre des parametres de la popup, ceux
// qu'elle ne declare pas restent a la fin.
[[nodiscard]] std::string withArgument(const hmi::View& popup, const std::string& arguments, const std::string& param,
                                       const std::string& text);
// Le compteur du cadre : "4 \xC2\xB7 1 par d\xC3\xA9" "faut \xC2\xB7 1 \xC3\xA0 revoir".
[[nodiscard]] std::string argumentsSummary(const hmi::Project&, const hmi::View* caller, const hmi::View& popup,
                                           const std::string& arguments, const domain::Project* plc);
// La case d'un argument du cadre (categorie "Parametres de <popup>  (...)",
// propriete "<nom>  .  <type>  .  <PASTILLE>") : la popup et le parametre ;
// vides pour une autre case.
struct ArgumentField {
    std::string popup, param;
};
[[nodiscard]] ArgumentField argumentField(std::string_view category, std::string_view property);
// L'aide a la saisie DANS le champ d'argument (P3) : `base` (l'aide des
// expressions : variables, membres) dont les noms DU BON TYPE pour ce parametre
// (selon son mode) viennent en premier, puis les types inconnus, puis ceux qui
// ne conviennent pas (le detail le dit). `caller` : la vue qui porte l'action.
[[nodiscard]] ui::InputText::Assist argumentAssist(ui::InputText::Assist base, std::function<const hmi::Project*()> project,
                                                   hmi::Id caller, std::string popup, std::string param);
inline constexpr std::string_view kNotSuitable = "ne convient pas";

// ---- l'action Appliquer copie sur reference ------------------------------------
// Les choix de la propriete Parametre : "Tous les parametres en mode les deux"
// (enregistre "*"), puis les parametres Les deux de la popup.
inline constexpr std::string_view kAllBoth = "Tous les param\xC3\xA8tres en mode les deux";
[[nodiscard]] std::vector<std::string> applyCopyChoices(const hmi::View& popup);
[[nodiscard]] std::string              applyCopyShown(const std::string& target);       // "*" -> kAllBoth
[[nodiscard]] std::string              applyCopyTarget(std::string_view shown);         // kAllBoth -> "*"
[[nodiscard]] std::string              applyCopyWrites(const hmi::View& popup, const std::string& target);

// ---- l'aide a la saisie des scripts et des expressions ---------------------------
// La vue dont on edite les scripts et les expressions (posee par l'editeur de la
// vue a chaque reconstruction de sa fiche) ; nulle : aucune ou plus dans le projet.
void setAssistView(hmi::Id view);
[[nodiscard]] const hmi::View* assistView(const hmi::Project&);
// Les parametres de la vue (et les membres de leur type) pour ce qui precede le
// curseur : (le nom, "<type> \xC2\xB7 <PASTILLE> \xC2\xB7 param\xC3\xA8tre").
[[nodiscard]] std::vector<std::pair<std::string, std::string>> assistItems(const hmi::Project&, const hmi::View&,
                                                                          std::string_view typed,
                                                                          const domain::Project* plc);

// ---- en marche : le repere "modifie, pas encore applique" ------------------------
// La variable `path` d'un objet de la popup `view` (Moteur.Seuil_Courant) est-elle
// un membre d'une copie modifie depuis la capture (pas encore applique) ?
[[nodiscard]] bool copyModified(const hmi::Runtime&, hmi::Id view, std::string_view path);
inline constexpr std::string_view kModifiedMark = "modifi\xC3\xA9, pas encore appliqu\xC3\xA9";
// Le repere sur le champ (`field` : son cadre a l'ecran) : un lisere gauche jaune
// (#D7A824) et la pastille "modifie, pas encore applique" posee sur son bord haut.
void paintCopyMark(gfx::IRenderer&, const gfx::Rect& field, float zoom, float alpha = 1.f);

// 1.9 (chantier U) : la PASTILLE DE COULEUR d'un mode (maquette, NOTES.md) : REF
// (contour bleu), COPIE (contour cyan), LES DEUX (pleine, du bleu au cyan) - dans
// la grille (PropertyGrid::Property::pill) et les tables (CellStyle::colorLead).
[[nodiscard]] ui::ColorPill modePill(hmi::ParamMode);

// ---- en marche : l'onglet Popups de la simulation (P6) ---------------------------
// Une ligne par parametre de chaque popup ouverte : (popup, parametre, type,
// pastille, valeur, reference). Copie / Les deux : la copie (les membres
// modifies d'abord) et, quand elle differe, la valeur de la variable de
// l'appelant ("Pompes[3].Seuil_Courant = 38 (pas encore \xC3\xA9" "crit)").
[[nodiscard]] std::vector<std::vector<std::string>> popupRows(hmi::Runtime&, const hmi::Project&);
// Le nombre de copies modifiees, pas encore appliquees (toutes les popups ouvertes).
[[nodiscard]] std::size_t modifiedCopies(const hmi::Runtime&);
// La ligne de pied de l'onglet : "1 popup ouverte \xC2\xB7 1 copie modifi\xC3\xA9" "e, pas encore
// appliqu\xC3\xA9" "e" ; vide : aucune popup ouverte.
[[nodiscard]] std::string popupsSummary(const hmi::Runtime&);
// L'onglet lui-meme (un tableau dont l'identifiant finit par ".popups") et sa
// mise a jour : retrouve par cet identifiant parmi les onglets ; le modele n'est
// refait que si les lignes changent ; le badge dit le nombre de popups ouvertes.
[[nodiscard]] std::unique_ptr<ui::Widget> makePopupsTab(const std::string& id);
void updatePopupsTab(ui::TabControl& tabs, hmi::Runtime&, const hmi::Project&);

} // namespace app::hmiparams
