// =============================================================================
//  help/Trails.cpp - 1.12.28 : les parcours du didacticiel de l'IHM, dans l'aide F1
// =============================================================================
#include "Trails.hpp"

#include <algorithm>

namespace help::trails {

const std::vector<Trail>& ihm() {
    static const std::vector<Trail> all = {
        {"decouvrir", "D\xC3\xA9" "couvrir l'IHM", "visite",
         "L'arbre, les vues, les symboles, la simulation et Pourquoi ?, l'historique, les versions, l'aide.", 16, 3, false,
         {"ihm", "onglets"}},
        {"premiere-vue", "Ma premi\xC3\xA8re vue", "interactif",
         "Une vue, un afficheur reli\xC3\xA9, un bouton et son action, puis la simulation.", 7, 6, false,
         {"vues", "editeur", "actions"}},
        {"symbole", "Cr\xC3\xA9" "er un symbole de projet", "interactif",
         "Une carte d'armoire devient un symbole, pos\xC3\xA9 deux fois, modifi\xC3\xA9 une fois.", 9, 8, false,
         {"symboles", "tutoriel-symbole", "objet-instance-de-symbole"}},
        {"modbus", "Relier un \xC3\xA9quipement Modbus", "visite",
         "Les \xC3\xA9quipements, les adresses, l'outil Modbus, l'esclave simul\xC3\xA9.", 7, 4, false,
         {"equipements", "outil-modbus", "esclaves-virtuels"}},
        {"excel", "Coller depuis Excel", "interactif",
         "Des lignes d'un tableur deviennent des variables IHM, en un seul Ctrl+Z.", 4, 3, false,
         {"excel", "variables-ihm"}},
        {"versions", "Historique et versions", "interactif",
         "Annuler, r\xC3\xA9tablir, l'historique, une version, la comparer.", 6, 4, false,
         {"historique", "versions", "comparer-versions"}},
    };
    return all;
}

const Trail* find(std::string_view key) {
    for (const auto& t : ihm())
        if (t.key == key) return &t;
    return nullptr;
}

std::string pageOf(const Trail& t) { return std::string(kPagePrefix) + std::string(t.key); }

const Trail* ofPage(std::string_view topicKey) {
    if (topicKey.substr(0, kPagePrefix.size()) != kPagePrefix) return nullptr;
    return find(topicKey.substr(kPagePrefix.size()));
}

std::vector<const Trail*> proposedOn(std::string_view topicKey) {
    std::vector<const Trail*> out;
    for (const auto& t : ihm())
        if (std::find(t.topics.begin(), t.topics.end(), topicKey) != t.topics.end()) out.push_back(&t);
    return out;
}

std::string stepKey(const Trail& t) { return "didacticiel." + std::string(t.key) + ".etape"; }
std::string doneKey(const Trail& t) { return "didacticiel." + std::string(t.key) + ".fait"; }

} // namespace help::trails
