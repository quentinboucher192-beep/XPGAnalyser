// =============================================================================
//  app/hmi/HmiObjectAlarmTree.hpp - le noeud "Alarmes" d'un objet (1.10, chantier O)
// -----------------------------------------------------------------------------
//  DEPLIER UN OBJET MONTRE SES ALARMES. Dans l'explorateur d'objets et dans
//  l'arbre du projet, un objet qui porte des alarmes (1.9 : un objet du
//  synoptique, une instance de symbole) a un noeud
//
//      Alarmes - Vue.Pompe_1 (3)            <- son groupe interne, le nombre
//         * Defaut                 Haute     <- la pastille de priorite
//         * Niveau_Bas             Moyenne - surchargee
//         o Niveau_Haut            Basse - decochee
//
//  Une instance de symbole montre aussi les groupes de ses objets (un objet
//  pris dans l'instance a son groupe "Vue.Instance.Objet") :
//
//      Alarmes - Vue.Carte_A (4)
//         * Surchauffe                       <- declaree par le symbole, sur l'instance
//         Vue.Carte_A.Pompe (2)              <- le groupe d'un objet de l'instance
//            * Defaut
//            ...
//
//  Un clic sur une alarme ouvre la section Alarmes de l'objet sur elle (et
//  choisit l'objet) : `object` (l'objet pose dans la vue), `path` (le chemin
//  dans le symbole) et `localName` disent laquelle.
//  Ici, seulement les donnees : les deux arbres les dessinent.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"

#include <optional>
#include <string>
#include <vector>

namespace app::alarmtree {

struct Entry {
    std::string name;             // ce que la ligne montre : le nom court (Defaut, Niveau_Bas)
    std::string full;             // le nom genere : "<groupe>.<alarme>"
    std::string localName;        // le nom dans le symbole ou la bibliotheque
    std::string path;             // le chemin dans le symbole ("" : l'objet pose lui-meme)
    hmi::Id     object{hmi::kNoId};   // l'objet pose dans la vue (l'instance)
    int         priority{3};
    bool        active{true};     // cochee
    bool        overridden{false};// un champ au moins surcharge sur l'objet
    // "Haute", "Moyenne . surchargee", "Basse . decochee" (le point median a l'ecran)
    [[nodiscard]] std::string detail() const;
};

struct Group {
    std::string        group;     // le groupe interne : "Vue.Pompe_1"
    std::string        label;     // "Alarmes . Vue.Pompe_1 (3)" ; un sous-groupe : "Vue.Carte_A.Pompe (2)"
    std::size_t        count{0};  // toutes ses alarmes, sous-groupes compris
    std::vector<Entry> alarms;    // les siennes
    std::vector<Group> groups;    // une instance : les groupes de ses objets
};

// Le noeud "Alarmes" d'un objet pose dans `view` (rien : il n'en porte pas,
// ou la vue n'en genere pas - un symbole, une popup a parametres).
[[nodiscard]] std::optional<Group> nodeOf(const hmi::Project&, const hmi::View&, const hmi::Object&);

// Les lignes a plat, dans l'ordre de l'arbre deplie : (profondeur, groupe ou
// alarme). Profondeur 0 : le noeud Alarmes ; un groupe d'objet d'instance : 1,
// ses alarmes : 2. Ce que l'explorateur d'objets range sous l'objet.
struct Line {
    int          depth{0};
    const Group* group{nullptr};  // un titre de groupe
    const Entry* alarm{nullptr};  // une alarme
};
[[nodiscard]] std::vector<Line> linesOf(const Group& root);

// La couleur de la pastille d'une priorite (1 : critique ... 4 : basse), en 0xRRGGBB.
[[nodiscard]] unsigned priorityColor(int priority) noexcept;

} // namespace app::alarmtree
