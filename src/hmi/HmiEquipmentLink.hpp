// =============================================================================
//  hmi/HmiEquipmentLink.hpp - 1.12.11 (lot G de l'audit des objets) : LA
//                             LIAISON EQUIPEMENT ET LE MODE ETAT DES SYMBOLES
//                             DU SYNOPTIQUE
// -----------------------------------------------------------------------------
//  (Rien a voir avec les equipements du reseau du lot 15 : ici, « l'equipement »
//  est la structure que represente un symbole - une pompe P1 : T_Pompe.)
//
//  LA LIAISON EQUIPEMENT. Une propriete « Equipement » (`equipment`) prend un
//  chemin de structure : P1, Pompes[2], $Pompe$, le parametre d'un symbole ou
//  d'une popup. Ses membres se lient PAR LEUR NOM aux roles du symbole :
//    Valeur (marche)   <- Marche, EnMarche, Running, Etat
//    Mesure / Niveau   <- Mesure, Niveau, Valeur, Pv      (cuve, silo... ISA)
//    Position          <- Position, Voie, Etat            (vanne 3 voies)
//    Defaut            <- Defaut, Fault, Alarme
//    Ouverture         <- Ouverture, Position             (vanne)
//    En mouvement      <- EnMouvement, Mouvement, Moving   (vannes)
//    Mode              <- Mode                             (un badge : Auto, Manu)
//    Libelle           <- Nom, Repere, Tag                 (un texte a trous)
//  1.12.30, le variateur de vitesse en plus :
//    Pret              <- Pret, Ready, PretMarche
//    Sens (arriere)    <- Arriere, Sens, Reverse, Direction
//    Frequence         <- Frequence, Freq, Frequency, Hz
//    Consigne          <- Consigne, ConsigneFreq, Setpoint, Sp
//    Courant           <- Courant, Intensite, Current, Amperes
//    Vitesse           <- Vitesse, Speed, VitessePct
//    Local / distance  <- Local, EnLocal, Loc
//  Le tableau de correspondance (role -> membre) se choisit PAR TYPE DE
//  STRUCTURE (Project::equipmentLinks) : toutes les T_Pompe se lient pareil ;
//  ce qui n'est pas choisi suit les noms proposes. CE QUI EST REGLE A LA MAIN
//  SUR L'OBJET GAGNE (une formule, une variable, une valeur autre que celle
//  d'un objet neuf).
//
//  OU ELLE SE FAIT. apply() rend l'objet tel que la liaison le remplit (des
//  formules P1.Marche, P1.Defaut ; le libelle {P1.Nom}) : le moteur la fait sur
//  la vue composee (Runtime::viewOf), les alarmes des objets sur leur copie ;
//  l'objet enregistre ne change pas (Dupliquer, le renommage et Generer lisent
//  le chemin `equipment`).
//
//  LE MODE ETAT. `stateAspects` : un petit tableau valeur -> aspect (arret,
//  marche, defaut, maintenance), avec une couleur et « anime » ou non :
//      0 = arret; 1 = marche | anime; 2 = defaut; 3 = maintenance | #4F8FD9
//  La valeur se lit alors comme un etat (son nombre, ou le nom d'une valeur
//  d'enumeration) : un moteur en DEFAUT (2) se dessine en defaut, plus « en
//  marche » parce que 2 n'est pas 0. Rempli d'office pour ETAT_MOTEUR (et un
//  entier), et pour une enumeration du projet d'apres les noms de ses valeurs.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi { struct Issue; }
namespace hmi::params { struct PlcTypes; }

namespace hmi::eqlink {

inline constexpr std::string_view kEquipmentKey = "equipment";
inline constexpr std::string_view kAspectsKey = "stateAspects";
inline constexpr std::string_view kModeKey = "operatingMode";

// ---- les roles ---------------------------------------------------------------------
enum class Role : std::uint8_t {
    Running, Measure, Position, Fault, Opening, Moving, Mode, Label,
    // 1.12.30 : le variateur de vitesse
    Ready, Direction, Frequency, Setpoint, Current, Speed, Local,
};
struct RoleInfo {
    Role                            role;
    std::string_view                key;        // enregistre : "marche", "mesure"...
    std::string_view                label;      // montre : "Valeur (marche)"
    std::string_view                property;   // la cle de l'objet qu'il remplit : value, fault, opening...
    std::array<std::string_view, 4> names;      // les noms proposes, dans l'ordre
};
[[nodiscard]] const std::vector<RoleInfo>& allRoles();
[[nodiscard]] const RoleInfo* roleInfo(std::string_view key) noexcept;
// Les roles d'un genre (vide : pas un symbole du synoptique), dans l'ordre de la fiche.
[[nodiscard]] std::vector<const RoleInfo*> rolesOf(Kind);
// Le genre a-t-il un mode Etat (une valeur marche / arret : pompe, moteur, vanne...) ? Pas un
// contenant, ni l'instrument ISA (une mesure), ni la vanne 3 voies (une position).
[[nodiscard]] bool kindHasAspects(Kind) noexcept;

// ---- les types et leurs membres -------------------------------------------------------
struct Member {
    std::string name, type;
};
// Les membres d'une structure (un type IHM du projet) ; vide : pas une structure connue.
[[nodiscard]] std::vector<Member> membersOf(const Project&, std::string_view type);
// Les DDT de l'automate (API Studio), donnes par l'application quand le programme change ; nuls :
// seuls les types IHM se lient.
void setPlcTypes(std::shared_ptr<const params::PlcTypes>);
// Le type de la structure qu'un chemin designe (P1, Pompes[2], le parametre type d'une popup ou
// d'un symbole de `view`) ; vide : inconnu, ou pas une structure. Les $ des reperes ne comptent pas.
[[nodiscard]] std::string structTypeOf(const Project&, const View* view, std::string_view path);

// ---- le tableau de correspondance, par type -------------------------------------------
enum class From : std::uint8_t { None, Name, Chosen };   // aucun membre ; par son nom ; choisi pour le type
// Le membre que lit le role pour ce type : celui choisi (Project::equipmentLinks), sinon le premier
// nom propose qui est un membre ; vide : aucun.
[[nodiscard]] std::string memberFor(const Project&, std::string_view type, const RoleInfo&, const std::vector<Member>&,
                                    From* from = nullptr);
// Choisir le membre d'un role pour un type : `member` un nom (vide : aucun membre) ; nullopt : revenir
// aux noms proposes. Vrai si le projet a change.
bool choose(Project&, std::string_view type, std::string_view role, const std::optional<std::string>& member);
// Les renommages : un type, un membre d'un type (avant / apres une modification du type IHM : un seul
// nom disparu et un seul apparu, c'est un renommage). Rendent le nombre de choix suivis.
std::size_t renameType(Project&, std::string_view from, std::string_view to);
std::size_t followMembers(Project&, const HmiType& before, const HmiType& after);

// ---- l'objet --------------------------------------------------------------------------
// Le chemin de l'equipement (sa formule, ou son texte) ; vide : pas de liaison.
[[nodiscard]] std::string equipmentPath(const Object&);
// La propriete `key` est-elle reglee a la main (une formule, sa variable, une valeur autre que celle
// d'un objet neuf de ce genre) ? Elle gagne alors sur la liaison.
[[nodiscard]] bool setByHand(const Object&, std::string_view key);
// Ce que la liaison fait d'un objet, role par role.
struct Row {
    const RoleInfo* role{nullptr};
    std::string     member;          // vide : aucun membre
    From            from{From::None};
    bool            byHand{false};   // la propriete est reglee a la main : elle gagne
    std::string     source;          // ce que lit la propriete : P1.Marche, {P1.Nom}
};
struct Binding {
    std::string         path;        // P1 (tel qu'ecrit, reperes compris)
    std::string         type;        // T_Pompe ; vide : inconnu
    std::vector<Member> members;
    std::vector<Row>    rows;
    std::string         aspects;     // le mode Etat qu'elle pose d'office (vide : aucun)
};
[[nodiscard]] Binding bindingOf(const Project&, const View* view, const Object&);
// L'objet tel que la liaison le remplit (les roles libres) ; vrai si quelque chose a change.
bool apply(const Project&, const View* view, Object&);
[[nodiscard]] Object bound(const Project&, const View* view, const Object&);
// Chaque objet de la vue ; vrai si l'un a change. viewHasLinks : un objet de la vue en a une.
bool applyView(const Project&, View&);
[[nodiscard]] bool viewHasLinks(const View&) noexcept;

// ---- le mode Etat ---------------------------------------------------------------------
enum class Look : std::uint8_t { Off, On, Fault, Maintenance };
struct Aspect {
    std::string match;          // la valeur : 2, 10..20, Auto (le nom d'une valeur d'enumeration), * (sinon)
    Look        look{Look::Off};
    std::string color;          // vide : celle de l'objet (arret, marche, defaut) ; maintenance : kMaintenanceColor
    bool        animated{false};
};
inline constexpr std::string_view kMaintenanceColor = "#4F8FD9";
[[nodiscard]] std::string_view lookKey(Look) noexcept;      // "arret", "marche", "defaut", "maintenance"
[[nodiscard]] std::string_view lookLabel(Look) noexcept;    // "Arr\xC3\xAAt", "Marche", "D\xC3\xA9" "faut", "Maintenance"
[[nodiscard]] std::optional<Look> lookFrom(std::string_view) noexcept;   // une cle ou un libelle (sans casse ni accent)
// "0 = arret; 1 = marche | anime; 2 = defaut | #E5534B". `error` : la premiere ligne illisible.
[[nodiscard]] std::vector<Aspect> parseAspects(std::string_view text, std::string* error = nullptr);
[[nodiscard]] std::string formatAspects(const std::vector<Aspect>&);
// L'aspect d'une valeur (`name` : le nom d'une valeur d'enumeration) ; aucune ligne : l'arret.
[[nodiscard]] Aspect aspectOf(const std::vector<Aspect>&, std::string_view value, std::string_view name = {});
// ETAT_MOTEUR (et un entier) : 0 arret, 1 marche (anime), 2 defaut, 3 maintenance.
[[nodiscard]] std::vector<Aspect> motorStateAspects();
// Une enumeration : une ligne par valeur, par son nom - ARRET, STOP : arret ; MARCHE, RUN, AUTO :
// marche (anime) ; DEFAUT, FAULT, ALARME : defaut ; MAINT... : maintenance ; les autres : arret.
[[nodiscard]] Look lookOfName(std::string_view valueName) noexcept;
[[nodiscard]] std::vector<Aspect> aspectsFromEnum(const HmiType& enumeration);
// Le mode Etat d'un objet (sa propriete stateAspects) ; vide : la valeur se lit vrai / faux.
[[nodiscard]] bool hasAspects(const Object&) noexcept;
// Un verin « rentre / sorti » (cylinderMode) : sa Valeur se lit vrai / faux (avant, et sans ce choix : une
// position entre min et max - un BOOL vrai le sortait de 1 %).
inline constexpr std::string_view kCylinderModeKey = "cylinderMode";
[[nodiscard]] bool cylinderOnOff(const Object&) noexcept;

// ---- Generer --------------------------------------------------------------------------
// La liaison de chaque symbole : un chemin inconnu ou qui n'est pas une structure, un type sans
// aucun membre lie ; le mode Etat (une ligne illisible, une couleur) ; une enumeration lue en
// booleen sur la Valeur (le mode Etat propose) ; une structure entiere sur la Valeur (la liaison
// proposee). Des avertissements et des informations : rien ne bloque.
void check(const Project&, std::vector<Issue>& out);

} // namespace hmi::eqlink
