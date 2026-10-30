// =============================================================================
//  app/ai/AiKnowledge.hpp - les connaissances de l'IA : le registre
// -----------------------------------------------------------------------------
//  "APPRENDRE" ICI = ENRICHIR LA MEMOIRE LOCALE que l'Assistant joint a ses
//  demandes. Le modele distant n'est pas reentraine : il recoit, avec chaque
//  demande, le texte des connaissances actives. La "taille estimee" d'une
//  connaissance est donc la taille de CE texte (en octets UTF-8).
//
//  DEUX SORTES :
//    - les connaissances INTEGREES au logiciel (source "logiciel") : tirees de
//      ses VRAIES tables, au demarrage - les 116 genres d'objets (hmi::kKindInfos,
//      leur famille dans la bibliotheque, le role de leur valeur), les proprietes
//      de chaque genre (la creation par defaut hmi::makeObject et la table des
//      roles hmi::roles, le libelle de l'inspecteur), les actions (declencheurs et
//      operations), le vocabulaire et les synonymes (les mots-cles de recherche de
//      la bibliotheque d'objets), les regles de creation (l'objet pose pour une
//      variable, hmi::design::kindForVariable ; les modeles de vues). Elles
//      s'activent et se desactivent (l'etat : memoire.json) ; elles ne se
//      suppriment pas ;
//    - les connaissances AJOUTEES (par l'utilisateur, pour un projet, une regle,
//      une preference) : une par fichier, connaissances/<id>.json (le contrat :
//      format, id, nom, description, categorie, source, projet, actif, ajoute,
//      modifie, contenu ; un champ inconnu se garde). On les ajoute, consulte,
//      modifie, active, desactive, supprime.
//
//  PAR PROJET (12.7) : une connaissance porte un projet (vide : tous les
//  projets). Une connaissance d'un AUTRE projet n'est pas "active ici" : elle
//  ne part pas avec les demandes faites dans ce projet.
//
//  Chaque operation ecrit sa ligne dans activite.jsonl (AiMemory.hpp).
// =============================================================================
#pragma once

#include "AiMemoryJson.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::memory {

// Les categories (le champ "categorie"), dans l'ordre des filtres.
struct CategoryInfo {
    std::string_view key;
    std::string_view label;
};
inline constexpr CategoryInfo kCategories[] = {
    {"objets", "Genres d'objets"},
    {"proprietes", "Propri\xC3\xA9t\xC3\xA9s et actions"},
    {"vocabulaire", "Vocabulaire et synonymes"},
    {"regles", "R\xC3\xA8gles de cr\xC3\xA9" "ation"},
    {"preferences", "Pr\xC3\xA9" "f\xC3\xA9rences de conception"},
    {"projet", "Contexte de projet"},
    {"metier", "Connaissances m\xC3\xA9tier"},
};
// Les sources (le champ "source").
struct SourceInfo {
    std::string_view key;
    std::string_view label;
};
inline constexpr SourceInfo kSources[] = {
    {"logiciel", "Logiciel"},            // integree, tiree des tables d'IHM Studio
    {"utilisateur", "Utilisateur"},      // ajoutee dans cet ecran
    {"assistant", "Assistant"},          // enregistree par l'Assistant (une regle, une preference retenue)
    {"fichier", "Fichier analys\xC3\xA9"},   // tiree d'une piece jointe
};
[[nodiscard]] std::string categoryLabel(std::string_view key);   // une cle inconnue : elle-meme
[[nodiscard]] std::string sourceLabel(std::string_view key);
[[nodiscard]] bool        knownCategory(std::string_view key) noexcept;

struct Knowledge {
    std::string id, name, description, category, source, project, added, modified, content;
    bool        active{true};
    bool        builtIn{false};
    std::string file;      // ajoutee : son fichier (UTF-8) ; integree : vide
    Json        raw;       // ajoutee : le fichier tel que lu (les champs inconnus y restent)
    // La taille estimee : celle du texte qu'on enverrait a l'IA.
    [[nodiscard]] std::uintmax_t size() const noexcept { return content.size(); }
};

// L'etat d'une connaissance pour le projet ouvert.
enum class Here : std::uint8_t { Active, Inactive, OtherProject };
[[nodiscard]] Here        hereState(const Knowledge& k, std::string_view project) noexcept;
[[nodiscard]] std::string hereLabel(Here h);

// Les connaissances integrees, tirees des tables du logiciel (calculees une fois).
[[nodiscard]] const std::vector<Knowledge>& builtIns();
// L'identifiant de la connaissance des genres d'objets (le compte "genres connus").
inline constexpr std::string_view kKindsKnowledge = "logiciel.genres";

class Registry {
public:
    // Relit tout : les integrees (et leur etat, memoire.json), puis connaissances/*.json.
    void load();
    [[nodiscard]] const std::vector<Knowledge>& all() const noexcept { return items_; }
    [[nodiscard]] const Knowledge* find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<std::string>& problems() const noexcept { return problems_; }

    // AJOUTER : l'id (k-AAAAMMJJ-HHMMSS-n), les dates et la source (vide : utilisateur) sont
    // attribues ; le fichier est ecrit, l'activite aussi. `k` recoit ce qui a ete enregistre.
    bool add(Knowledge& k, std::string* why = nullptr);
    // MODIFIER une connaissance ajoutee (pas une integree) : nom, description, categorie, projet,
    // contenu, actif ; la date de mise a jour suit ; les champs inconnus du fichier restent.
    bool update(const Knowledge& k, std::string* why = nullptr);
    // ACTIVER / DESACTIVER (une integree aussi).
    bool setActive(std::string_view id, bool on, std::string* why = nullptr);
    // SUPPRIMER une connaissance ajoutee (une integree : refuse).
    bool remove(std::string_view id, std::string* why = nullptr);
    // Supprimer les connaissances propres a un projet (12.7) ; rend leur nombre.
    std::size_t removeProject(std::string_view project);

    // Les genres d'objets connus : ceux que porte la connaissance des genres quand elle est active.
    [[nodiscard]] std::size_t kindsKnown() const noexcept;
    [[nodiscard]] static std::size_t kindsTotal() noexcept;
    // Les projets cites par les connaissances (tries, sans doublon).
    [[nodiscard]] std::vector<std::string> projects() const;
    [[nodiscard]] std::size_t    count(bool activeOnly) const noexcept;
    [[nodiscard]] std::uintmax_t bytes(bool activeOnly) const noexcept;
    // Ce qui partirait avec une demande faite dans ce projet : les connaissances actives ici.
    [[nodiscard]] std::uintmax_t bytesHere(std::string_view project) const noexcept;

private:
    bool write(Knowledge& k, std::string* why);
    std::vector<Knowledge>   items_;
    std::vector<std::string> problems_;
};

} // namespace app::ai::memory
