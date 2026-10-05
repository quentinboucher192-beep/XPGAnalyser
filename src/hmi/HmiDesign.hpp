// =============================================================================
//  hmi/HmiDesign.hpp - concevoir plus vite (lot 12)
// -----------------------------------------------------------------------------
//  LES VARIABLES POSEES D'UN GESTE. Une variable glissee de la bibliotheque
//  (onglet Variables) dans la vue devient l'objet qui lui va : un BOOL un
//  voyant, un nombre un afficheur, un texte un texte a trous, une structure
//  (DDT, DFB) un bouton qui ouvre sa POPUP D'EQUIPEMENT - generee au besoin
//  depuis les membres du type, reliee par son parametre (Equipement := Pompe_1).
//
//  LA VUE D'UN TYPE. Une vue ou une popup generee depuis les membres d'un DDT
//  ou d'un DFB : une ligne par membre (son nom ou son commentaire, l'objet de
//  sa valeur), lus a travers un parametre - une popup pour toutes les instances.
//
//  RECHERCHER / REMPLACER. Dans une vue ou tout le projet (proprietes et
//  expressions des objets, actions, scripts de vue, parametres, scripts
//  generaux, alarmes), avec l'apercu de chaque changement (avant, apres).
//  DUPLIQUER EN REMPLACANT : Vue_Armoire_A -> Vue_Armoire_B, Armoires[0] ->
//  Armoires[1], en un geste.
//
//  LES STYLES NOMMES. Creer un style depuis un objet, l'appliquer (ses
//  valeurs copiees, l'objet le cite par "namedStyle"), le changer : les objets
//  qui le citent suivent - sauf une valeur que l'objet a changee lui-meme.
//
//  LES MODELES DE VUES. "Nouvelle vue" part d'un modele : vide, synoptique,
//  tableau de bord, popup d'equipement, reglages, alarmes ; 1.10.2 : accueil,
//  courbes, maintenance, communication, production (des vues), confirmation,
//  saisie d'une consigne, popup moteur, popup vanne (des popups), moteur et son
//  etat, mesure et unite (des symboles), en-tete et pied de page.
//
//  L'ESPACEMENT EGAL des guides magnetiques est dans HmiEdit (snapMove).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::design {

// ---- les variables ------------------------------------------------------------------------
enum class VarShape : std::uint8_t { Bool, Number, Text, Structure, Other };
struct VarInfo {
    std::string          name;          // le chemin : "Pompe_Marche", "Armoires[0].ana.PT1"
    std::string          type;          // "BOOL", "REAL", "T_ANA"...
    std::string          comment;
    bool                 plc{true};     // de l'automate ; faux : une variable IHM
    std::vector<VarInfo> members;       // une structure : ses membres (un niveau)
};
[[nodiscard]] VarShape shapeOf(std::string_view type, bool hasMembers = false);
[[nodiscard]] VarShape shapeOf(const VarInfo&);
[[nodiscard]] Kind     kindForVariable(const VarInfo&);   // voyant, afficheur, texte, bouton
// "Armoires[0].ana.PT1" -> "Armoires_0_ana_PT1" : un morceau de nom d'objet.
[[nodiscard]] std::string nameStem(std::string_view path);
// La popup d'equipement d'un type : "Popup_T_ANA".
[[nodiscard]] std::string equipmentPopupName(std::string_view typeName);
// Pose l'objet d'une variable en (x, y) (coin haut-gauche) dans la vue `view`
// du projet et rend son identifiant ; kNoId pour ce qui ne se montre pas (un
// tableau). Une structure : un bouton qui ouvre `popup` avec Equipement := sa
// variable (la popup est generee d'abord si elle manque et que `popup` est
// vide - une vue de plus : les references aux vues du projet ne tiennent plus,
// on les reprend par leur identifiant).
Id placeVariable(Project&, Id view, const VarInfo&, double x, double y, std::string popup = {});

// ---- la vue d'un type -----------------------------------------------------------------------
struct TypeViewOptions {
    std::string viewName;                 // vide : Popup_<Type> (ou Vue_<Type>)
    std::string typeName;
    std::string parameter{"Equipement"};
    std::string sample;                   // la valeur par defaut du parametre (une instance)
    bool        popup{true};
};
// Rend l'identifiant de la vue creee (kNoId : aucun membre a montrer).
Id generateTypeView(Project&, const TypeViewOptions&, const std::vector<VarInfo>& members);
// La popup d'equipement du type si elle existe deja (une popup qui a le parametre).
[[nodiscard]] const View* existingEquipmentPopup(const Project&, std::string_view typeName, std::string_view parameter = "Equipement");

// ---- rechercher / remplacer ---------------------------------------------------------------------
struct FindOptions {
    bool matchCase{false};
    bool wholeWord{false};
    Id   view{kNoId};            // kNoId : tout le projet (vues, scripts generaux, alarmes)
};
struct FindHit {
    Id          view{kNoId}, object{kNoId};
    std::string where;           // "Vue_Armoire_A . Voyant_PT1 . value (expression)" (points centres)
    std::string before, after;   // le champ entier, avant et apres
    std::size_t count{0};        // les occurrences dans le champ
    std::size_t first{0};        // ou commence la premiere (dans before comme dans after)
};
// `s` avec chaque occurrence remplacee (sans casse, mot entier selon les options).
[[nodiscard]] std::string replaced(std::string_view s, std::string_view find, std::string_view replacement, const FindOptions&,
                                   std::size_t* count = nullptr);
[[nodiscard]] std::vector<FindHit> find(const Project&, std::string_view text, std::string_view replacement, const FindOptions&);
// Remplace partout ; rend le nombre de champs changes.
std::size_t replaceAll(Project&, std::string_view text, std::string_view replacement, const FindOptions&);
// Copie la vue (identifiants neufs, nom `newName`) en remplacant `text` dans
// tout ce qu'elle porte. Rend l'identifiant de la copie (kNoId : vue absente).
Id duplicateViewReplacing(Project&, Id view, const std::string& newName, std::string_view text, std::string_view replacement,
                          const FindOptions& options = {}, std::size_t* replacedFields = nullptr);

// ---- les styles nommes ----------------------------------------------------------------------------
// Les proprietes d'apparence qu'un style porte (celles de Copier le style).
[[nodiscard]] const std::vector<std::string>& styleKeys();
// Un style fait des valeurs d'apparence de l'objet (identifiant attribue, nom unique).
[[nodiscard]] Style styleFromObject(Project&, const Object&, const std::string& name);
// Copie les valeurs du style dans l'objet (celles qu'il a) et le lui fait citer.
void applyStyle(Object&, const Style&);
// Le style change (ou renomme) : les objets qui le citent suivent, sauf une
// valeur qu'ils ont changee eux-memes. Rend le nombre d'objets changes.
std::size_t propagateStyle(Project&, const Style& before, const Style& after);
// Les objets qui le citent, dans toutes les vues.
[[nodiscard]] std::size_t styleUsers(const Project&, std::string_view name);
// Retirer un style : les objets gardent leurs valeurs, ne le citent plus.
std::size_t forgetStyle(Project&, std::string_view name);

// ---- les modeles de vues -----------------------------------------------------------------------------
struct ViewTemplate {
    std::string_view key, label, description;
    // 1.10.2 : le role que le modele impose ("" : celui qu'on a choisi ; une
    // popup d'equipement : "popup") et les roles pour lesquels la galerie le
    // propose en tete (vide : tous ; les autres, sous AUTRES MODELES).
    std::string_view              role{};
    std::vector<std::string_view> suits{};
    // Sa taille (0 : celle du projet pour le role ; bornee par celle du projet).
    int width{0}, height{0};
    [[nodiscard]] bool suitsRole(std::string_view r) const {
        if (suits.empty()) return true;
        for (const auto s : suits)
            if (s == r) return true;
        return false;
    }
};
[[nodiscard]] const std::vector<ViewTemplate>& viewTemplates();
[[nodiscard]] const ViewTemplate* viewTemplate(std::string_view keyOrLabel);
// Remplit la vue (neuve) d'apres le modele ; "vide" ne pose rien.
void fillFromTemplate(Project&, View&, std::string_view key);

} // namespace hmi::design
