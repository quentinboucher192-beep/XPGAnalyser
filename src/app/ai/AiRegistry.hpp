// =============================================================================
//  app/ai/AiRegistry.hpp - l'Assistant IA : le registre des genres d'objets
// -----------------------------------------------------------------------------
//  LE CAIObjectRegistry DE LA DEMANDE (4.5), TIRE DU CODE, RIEN D'INVENTE. Pour
//  chacun des 116 genres (hmi::kKindInfos), une fois au premier appel :
//
//    - sa cle enregistree et son libelle (kKindInfos) ;
//    - sa famille dans la bibliotheque de l'editeur (hmiPaletteFamily) et ses
//      mots-cles de recherche (hmiPaletteWords : le sujet d'aide, @mots) ;
//    - une description courte : la premiere phrase de son sujet d'aide (F1) ;
//    - ses capacites : ce que porte sa valeur (hmi::roles::valueRole), s'il
//      montre une valeur, ecrit sa variable, est un symbole du synoptique, a un
//      niveau, tient des enfants (les familles en ligne de HmiModel.hpp) ;
//    - SES PROPRIETES, celles que la creation par defaut lui donne
//      (hmi::makeObject) : la cle, la valeur par defaut, le libelle de
//      l'inspecteur (hmiPropertyLabel), le role et le type (hmi::roles::roleOf),
//      les mots proposes pour un choix (la liste de l'inspecteur,
//      hmiPropertyCategories : gardee seulement si elle contient la valeur par
//      defaut - une liste de libelles n'est pas une liste de valeurs), et si la
//      propriete porte une variable (une formule lue par le moteur).
//
//  UN TEXTE COMPACT POUR LE MODELE. Tout le registre est trop gros pour chaque
//  demande : la liste de tous les genres part toujours (cle | libelle |
//  famille), les proprietes communes une fois, et le detail d'un genre (une
//  ligne : ses proprietes en abrege) seulement pour les genres presents dans la
//  vue et ceux que la demande evoque (son libelle, sa cle, les mots-cles de la
//  bibliotheque : "pompe", "cuve", "vanne"...).
//
//  Le centre de memoire (AiKnowledge.hpp) tire du meme registre la connaissance
//  "Genres d'objets IHM" et son compte de genres connus (116/116).
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiRoles.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

// Une propriete d'un genre, telle que le logiciel la definit.
struct PropertyInfo {
    std::string              key;          // "fillColor"
    std::string              label;        // le libelle de l'inspecteur ("Couleur du liquide")
    std::string              value;        // la valeur par defaut (hmi::makeObject)
    hmi::roles::Role         role{hmi::roles::Role::Text};   // hmi::roles::roleOf
    hmi::roles::Role         item{hmi::roles::Role::Text};   // une liste : ses elements ; une formule : sa valeur
    std::string              type;         // le type de Vue.Objet.Cle (REAL, INT, BOOL) ; vide : lu dans la valeur
    std::vector<std::string> choices;      // un choix : les mots que l'inspecteur propose (vide : libre)
    bool                     variable{false};   // porte une variable : une formule lue par le moteur (roles::readsAsFormula)
    bool                     common{false};     // commune a tous les genres de la bibliotheque (x, y, w, h, visible...)
};

// Un genre d'objet et ce que le logiciel en sait.
struct KindEntry {
    hmi::Kind                 kind{hmi::Kind::Rectangle};
    std::string               key, label;       // hmi::kKindInfos ("Tank", "Cuve")
    std::string               family;           // la famille de la bibliotheque ("Synoptique") ; vide : hors bibliotheque
    std::string               description;      // la premiere phrase de son sujet d'aide ; vide : aucun sujet
    std::vector<std::string>  words;            // les mots-cles de recherche de la bibliotheque
    hmi::roles::Role          valueRole{hmi::roles::Role::Text};
    bool                      placeable{false};  // se pose depuis la bibliotheque (hmi::kPlaceableKinds)
    bool                      shows{false};      // montre une valeur (kindShowsValue, un symbole, un niveau)
    bool                      writes{false};     // une commande : ecrit sa variable (kindWritesVariable)
    bool                      synoptic{false};   // un symbole du synoptique (kindIsSynoptic)
    bool                      level{false};      // une valeur de niveau entre min et max (kindHasLevel)
    bool                      holds{false};      // tient d'autres objets (kindHoldsChildren)
    double                    w{0}, h{0};        // la taille par defaut
    std::vector<PropertyInfo> props;             // dans l'ordre de hmi::makeObject
    [[nodiscard]] const PropertyInfo* prop(std::string_view key) const noexcept;
};

class KindRegistry {
public:
    // Le registre, construit une fois (sur le fil de l'interface : il lit les tables de l'editeur).
    [[nodiscard]] static const KindRegistry& get();

    [[nodiscard]] const std::vector<KindEntry>& kinds() const noexcept { return kinds_; }
    [[nodiscard]] const KindEntry* find(hmi::Kind) const noexcept;
    [[nodiscard]] const KindEntry* find(std::string_view key) const noexcept;   // la cle exacte ("Tank")
    // Les proprietes communes a tous les genres de la bibliotheque, dans l'ordre de la creation.
    [[nodiscard]] const std::vector<PropertyInfo>& common() const noexcept { return common_; }

    // ---- les textes envoyes au modele -----------------------------------------------------
    // Tous les genres, une ligne chacun : "Tank | Cuve | Synoptique" (la connaissance "Genres
    // d'objets IHM" du centre de memoire : son en-tete et ses lignes).
    [[nodiscard]] std::string kindsText() const;
    // Le role d'une propriete en abrege ("n", "c", "f:b", "dessous|dessus|aucun").
    [[nodiscard]] static std::string roleCode(const PropertyInfo&);
    // La legende des abreviations et les proprietes communes.
    [[nodiscard]] std::string commonText() const;
    // Le detail d'un genre en une ligne : taille, capacites, proprietes propres en abrege.
    [[nodiscard]] std::string detailLine(const KindEntry&) const;
    // Toutes les proprietes de tous les genres, une ligne par genre, avec les libelles (la
    // connaissance "Proprietes de chaque genre" du centre de memoire).
    [[nodiscard]] std::string propertiesText() const;

    // Les genres qu'une demande evoque : sa cle, son libelle, le premier mot de son libelle,
    // et (`useWords`) les mots-cles de la bibliotheque ; un pluriel en -s ou -x compte. Sans
    // casse ni accents, dans l'ordre ou la demande les cite ; au plus `max`.
    [[nodiscard]] std::vector<hmi::Kind> evoked(std::string_view request, bool useWords = true, std::size_t max = 24) const;
    // Les genres les plus proches d'un nom inconnu ("Pompe", "pump", "TankLevel") : par la
    // cle, le libelle, les mots-cles. Pour dire au modele ce qu'il voulait sans doute.
    [[nodiscard]] std::vector<std::string> nearKinds(std::string_view name, std::size_t max = 3) const;

private:
    KindRegistry();
    std::vector<KindEntry>    kinds_;
    std::vector<PropertyInfo> common_;
};

// Raccourci : KindRegistry::get().
[[nodiscard]] inline const KindRegistry& registry() { return KindRegistry::get(); }

} // namespace app::ai
