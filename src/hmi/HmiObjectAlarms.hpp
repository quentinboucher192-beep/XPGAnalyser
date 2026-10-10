// =============================================================================
//  hmi/HmiObjectAlarms.hpp - les alarmes des objets (1.9)
// -----------------------------------------------------------------------------
//  DEUX SOURCES, UNE FORME.
//    - LA BIBLIOTHEQUE : chaque objet du synoptique pose (pompe, vanne, cuve...)
//      a des alarmes par defaut - Defaut (sa propriete "fault" reliee), les
//      quatre niveaux d'un contenant (ses seuils), la course trop longue d'une
//      vanne ("moving" relie). Chacune n'existe que si sa source est reglee
//      (sinon : "sans objet", avec la raison). Actives des qu'on pose l'objet.
//    - LES SYMBOLES : une vue de role "symbole" declare des alarmes
//      (View::alarms) dont la condition et les textes citent ses parametres ;
//      chaque instance les recoit, developpees avec ses arguments.
//
//  LE GROUPE INTERNE. Chaque objet qui porte des alarmes a son groupe : son
//  chemin "Vue.Objet" ; un objet pris dans une instance : "Vue.Instance.Objet"
//  (le groupe de l'instance englobe ceux de ses objets). Une alarme generee
//  s'appelle "<groupe>.<alarme>" et garde aussi le groupe declare dans sa
//  definition (une zone) : elle appartient aux deux.
//
//  LA SURCHARGE. L'objet pose garde, par alarme (et par chemin dans le symbole),
//  les champs changes chez lui (Object::alarmOverrides) ; les autres suivent le
//  symbole ou la bibliotheque. Une condition (un texte) surchargee s'ecrit dans
//  les termes du symbole qui declare l'alarme (Moteur.Courant > 50.0) et se
//  developpe comme elle (Pompes[3].Courant > 50.0 : ObjectAlarm::def).
//
//  QUI GENERE. objectAlarms(projet) : chaque vue (pas les symboles eux-memes ;
//  un ecran modele, un en-tete, un pied : une fois, depuis leur vue), chaque
//  objet du synoptique, chaque instance (ses objets et ses instances
//  imbriquees). Les objets d'une popup a parametres ne generent rien : leurs
//  arguments ne sont connus qu'a l'ouverture. Le moteur et l'editeur s'en
//  servent tous deux.
// =============================================================================
#pragma once
#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// D'ou vient une alarme d'objet.
enum class ObjectAlarmSource : std::uint8_t { Library, Symbol };
[[nodiscard]] std::string_view objectAlarmSourceLabel(ObjectAlarmSource) noexcept;   // "biblioth\xC3\xA8que", "symbole"

// ---- la bibliotheque -------------------------------------------------------------
// Une alarme par defaut d'un objet du synoptique : sa definition (le nom court :
// Defaut, Niveau_Bas... ; la condition ecrite sur l'objet), et si elle s'applique
// (faux : "sans objet", `why` dit pourquoi : "fault n'est pas reli\xC3\xA9"...).
struct LibraryAlarm {
    AlarmDef    def;
    bool        applicable{true};
    std::string why;
};
// Les alarmes par defaut de l'objet, selon son genre et ses proprietes (vide :
// un objet hors du synoptique). Dans l'ordre du tableau de l'aide.
[[nodiscard]] std::vector<LibraryAlarm> libraryAlarms(const Object&);
// Le libelle d'un objet dans un message : sa propriete "label", sinon son nom.
[[nodiscard]] std::string objectCaption(const Object&);
// Une propriete reliee : son expression, sinon sa valeur quand ce n'est pas une
// constante (TRUE, FALSE, un nombre, une chaine) ; vide : pas reliee.
[[nodiscard]] std::string linkedSource(const Object&, std::string_view key);

// ---- les surcharges ------------------------------------------------------------------
// Les noms des champs surchargeables, dans l'ordre de la fiche : active, condition,
// message, priorite, categorie, groupe, delai, acquittement, consigne, description.
[[nodiscard]] const std::vector<std::string>& overrideFields();
// Les champs surcharges d'une surcharge, dans cet ordre.
[[nodiscard]] std::vector<std::string> overriddenFields(const AlarmOverride&);
// La surcharge d'une alarme sur un objet pose (nul : aucune).
[[nodiscard]] const AlarmOverride* findOverride(const Object&, std::string_view path, std::string_view alarm) noexcept;
// La surcharge, creee au besoin (a remplir).
AlarmOverride& overrideOf(Object&, std::string_view path, std::string_view alarm);
// Revenir a la valeur du symbole (ou de la bibliotheque) : un champ ; vide :
// tous (la surcharge disparait). Vrai si quelque chose a change.
bool resetOverride(Object&, std::string_view path, std::string_view alarm, std::string_view field = {});
// La definition surchargee (et `active`, vrai sans surcharge).
[[nodiscard]] AlarmDef applyOverride(const AlarmDef& base, const AlarmOverride*, bool* active = nullptr);

// Les renommages (1.9) : une alarme renommee dans le symbole `symbol`, un objet
// renomme dans le symbole -> les surcharges des objets poses (et des objets des
// symboles qui en contiennent) suivent. Rendent le nombre de surcharges changees.
// A appeler APRES le renommage (la generation dit ce qui existe).
std::size_t renameSymbolAlarmOverrides(Project&, std::string_view symbol, std::string_view from, std::string_view to);
std::size_t renameOverridePaths(Project&, std::string_view symbol, std::string_view from, std::string_view to);

// ---- la generation --------------------------------------------------------------------
struct ObjectAlarm {
    AlarmDef    def;              // effective : nom "<groupe>.<alarme>", textes developpes, surcharges appliquees
    AlarmDef    base;             // la valeur du symbole (ou de la bibliotheque), developpee, sans la surcharge
    std::string localName;        // le nom dans le symbole ou la bibliotheque : Defaut_Thermique, Niveau_Bas
    std::string objectGroup;      // le groupe interne : "Vue.Objet", "Vue.Instance.Objet"
    std::string view;             // la vue ou l'objet est pose
    Id          viewId{kNoId};
    Id          objectId{kNoId};  // l'objet pose dans `view` (l'instance, pour un objet qu'elle contient)
    std::string objectName;       // son nom dans la vue
    std::string path;             // le chemin dans le symbole ("" : l'objet pose lui-meme)
    std::string symbol;           // le symbole qui la declare ("" : la bibliotheque)
    std::string symbols;          // les symboles des instances qui la contiennent, "Sym_A;Sym_B" (filtre symbole:)
    ObjectAlarmSource source{ObjectAlarmSource::Library};
    Kind        kind{Kind::Rectangle};   // le genre de l'objet qui la porte
    bool        active{true};     // cochee (une surcharge peut la decocher)
    std::vector<std::string> overridden;   // les champs surcharges (overrideFields)
};
// Toutes les alarmes des objets du projet, vue par vue, dans l'ordre des objets.
// Les identifiants (def.id) restent kNoId : le moteur leur en donne.
[[nodiscard]] std::vector<ObjectAlarm> objectAlarms(const Project&);
// Celles d'un objet pose (une instance : ses objets et ses instances imbriquees
// compris) - l'apercu du groupe genere dans l'editeur.
[[nodiscard]] std::vector<ObjectAlarm> objectAlarmsOf(const Project&, const View&, const Object&);
// Le groupe interne d'un objet pose : "Vue.Objet".
[[nodiscard]] std::string objectGroupOf(const View&, const Object&);
// La vue genere-t-elle les alarmes de ses objets ? Pas un symbole (ses instances
// le font), pas une popup a parametres (ses arguments viennent a l'ouverture).
// 1.12.3 : dans une popup de symbole, une alarme dont la condition cite un
// parametre du symbole n'est pas fabriquee non plus (les autres le sont).
[[nodiscard]] bool viewGeneratesAlarms(const View&) noexcept;

// ---- les filtres des objets d'alarmes -----------------------------------------------------
// Une alarme (son groupe declare, son groupe interne, les symboles qui la
// contiennent) passe-t-elle le filtre ? Vide ou "*" : toutes. Sinon une liste
// separee par ';' ; chaque element : un nom de groupe (le groupe declare) ; un
// groupe d'objet (l'alarme est dans ce groupe interne ou dans un groupe qu'il
// contient : "Vue.Carte_A" prend "Vue.Carte_A.Pompe") ; un motif avec '*'
// ("Vue_Pompes.*", sur l'un ou l'autre groupe) ; "symbole:Sym_Pompe" (les objets
// des instances de ce symbole). Les filtres d'avant (un nom exact) gardent leur sens.
[[nodiscard]] bool alarmGroupMatches(std::string_view declared, std::string_view objectGroup, std::string_view symbols,
                                     std::string_view filter) noexcept;
// Un motif avec '*' (n'importe quels caracteres, points compris).
[[nodiscard]] bool wildcardMatch(std::string_view text, std::string_view pattern) noexcept;

// ---- l'aide de l'editeur ---------------------------------------------------------------------
// Des alarmes proposees pour un symbole depuis un parametre type (`type` : un type
// IHM du projet, ou un type de base) : une par membre BOOL (Defaut, Discordance...
// -> "<param>.<membre>", priorite 2, categorie Defaut) ; pour un membre numerique
// (INT, REAL...), deux seuils (bas et haut, sur "<param>.<membre>", a regler).
// Un parametre BOOL : une alarme sur lui-meme. Les noms ne reprennent pas ceux
// deja pris dans `symbol`.
[[nodiscard]] std::vector<AlarmDef> suggestSymbolAlarms(const Project&, const View& symbol, std::string_view param,
                                                        std::string_view type);

} // namespace hmi
