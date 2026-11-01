// =============================================================================
//  app/hmi/HmiProjectEdits.hpp - 1.22.6 : les modifications du projet COMMUNES
//  aux ecrans et a l'Assistant IA
// -----------------------------------------------------------------------------
//  Ce que les ecrans font, sorti de leurs volets pour que l'Assistant IA fasse
//  EXACTEMENT la meme chose (memes controles, memes messages) :
//
//    la securite        Configuration > Utilisateurs (HmiUsersPane) : un reglage
//                       de la securite, un utilisateur, un groupe, par le nom de
//                       leur champ (celui de la fiche) ;
//    une vue            l'inspecteur d'une vue (HmiEditor) : son ecran modele, son
//                       en-tete et son pied (et leurs modeles), sa vue parente -
//                       pas de boucle, le bon role ;
//    une variable IHM   Variables IHM (HmiVariablesPane) : la valeur initiale
//                       (lue comme une case), les membres internes.
//
//  Chaque fonction travaille sur un hmi::Project (celui d'une commande, ou une
//  copie) : faux et la raison - rien n'a change ; vrai : c'est fait.
//
//  LES MOTS DE PASSE NE PASSENT PAS ICI : ils se donnent dans la fiche d'un
//  utilisateur (empreinte salee, HmiPolicy), jamais par un champ texte.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace app::projedit {

// ---- la securite (Configuration > Utilisateurs) ------------------------------------------------
// Un reglage de la securite, par le nom de son champ : active, depart, deconnexion, menu_comptes, menu_acces, menu_journal,
// periode, chiffres, mdp_longueur, mdp_chiffre, mdp_lettre, mdp_casse, mdp_special, mdp_duree, mdp_historique, mdp_premiere,
// verrou_essais, verrou_minutes, avertir, badge.
bool setSecurityField(hmi::Project&, const std::string& field, const std::string& value, std::string& err);
[[nodiscard]] const std::vector<std::string>& securityFields();
// Un utilisateur neuf : un login libre (lettres, chiffres, _), son groupe par son nom (vide : le premier groupe). Son id ;
// kNoId et `err`. Aucun mot de passe : << sans mot de passe >> tant qu'on ne lui en donne pas.
hmi::Id addUser(hmi::Project&, const std::string& login, const std::string& group, std::string& err);
// Un champ d'un utilisateur : login, nom, groupe, protection, expression, actif, description, changer. Un login renomme : les
// actions << Changer d'utilisateur >> qui le visaient suivent (`followed` : combien).
bool setUserField(hmi::Project&, hmi::Id user, const std::string& field, const std::string& value, std::string& err,
                  std::size_t* followed = nullptr);
// Un groupe neuf : un nom libre, un niveau de 1 a 9. Son id ; kNoId et `err`.
hmi::Id addGroup(hmi::Project&, const std::string& name, int level, std::string& err);
// Un champ d'un groupe : nom, niveau, roles (des roles existants, separes par , ou ;), description, vue_demarrage.
bool setGroupField(hmi::Project&, hmi::Id group, const std::string& field, const std::string& value, std::string& err);
// Les comptes ACTIFS qui peuvent administrer (la permission Administrer, par leur groupe) - `canLogIn` : et qui peuvent se
// connecter (un mot de passe, un secret, une expression, selon leur protection). Leurs logins.
[[nodiscard]] std::vector<std::string> administrators(const hmi::Project&, bool canLogIn = false);
// Les groupes qui ont la permission Administrer (par leurs roles).
[[nodiscard]] std::vector<std::string> adminGroups(const hmi::Project&);

// ---- une vue (l'inspecteur d'une vue) ------------------------------------------------------------
// modele, entete, pied, entete_modele, pied_modele, vue_parente.
[[nodiscard]] bool isViewLinkField(std::string_view field) noexcept;
// Le champ `field` de la vue `vv` (une copie de travail ; `p` : le projet, pour trouver les autres vues). Faux et `why` :
// refuse (une vue introuvable, un modele qui n'a pas le role, une boucle de parentes ou d'heritage).
bool setViewLink(const hmi::Project& p, hmi::View& vv, const std::string& field, const std::string& value, std::string& why);

// ---- une variable IHM (Variables IHM) ------------------------------------------------------------
// La valeur initiale `initial` (deja lue par hmi::typedInitial) convient-elle au type ? Faux et `why`.
bool validInitial(const hmi::Project&, const std::string& type, const std::string& initial, std::string* why);
// Les membres internes de `v` apres << Rendre interne >> (`internal`) ou << Attribuer a l'equipement >> des chemins `relPaths`
// (relatifs a la variable : "[0].NOM", "Defaut" ; `allElements` : le membre de toutes les cases, [*]). Faux et `why` : un
// chemin qui n'est pas un membre, une variable sans equipement ou sans membres.
bool membersInternal(const hmi::Project&, const hmi::Variable& v, const std::vector<std::string>& relPaths, bool internal, bool allElements,
                     std::vector<std::string>& next, std::string* why);

} // namespace app::projedit
