// =============================================================================
//  hmi/HmiLoginMenu.hpp - le menu natif de connexion (lot 12)
// -----------------------------------------------------------------------------
//  Comme le menu Parametres systeme (lot 10) : un menu que l'IHM porte elle-
//  meme, par-dessus la vue, ouvert par l'action "Menu de connexion", l'objet du
//  meme nom ou IHM_MENU_CONNEXION(). Ses onglets se montrent selon qui est
//  connecte :
//
//    Connexion    a tout le monde : choisir son compte, taper son mot de passe
//                 (ou son code), se connecter, se deconnecter ;
//    Mon compte   a un utilisateur connecte : qui il est, changer son mot de
//                 passe ;
//    Comptes      des le niveau "menuLevelAccounts" (4) : les comptes (ajouter,
//                 activer, changer de groupe, donner un mot de passe, supprimer) ;
//    Acces        des le niveau "menuLevelAccess" (4) : les roles de chaque
//                 groupe, la deconnexion automatique ;
//    Journal      des le niveau "menuLevelJournal" (3) : les connexions, les
//                 deconnexions, les refus, les comptes changes.
//  La permission Administrer montre tout ; changer quelque chose la demande
//  toujours (un technicien voit les comptes, sans y toucher). Securite
//  eteinte : tout est permis, tous les onglets se montrent.
//
//  Ce qui change le projet (un compte, un role, la deconnexion automatique)
//  passe par l'ecran, en commandes annulables (Runtime::Hooks::userRequest).
//  Le dessin et le clic lisent la meme geometrie (loginMenuLayout).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

enum class LoginTab : std::uint8_t { Connexion, Compte, Comptes, Acces, Journal };
struct LoginTabSpec {
    LoginTab         tab;
    std::string_view key;     // "connexion", "compte", "comptes", "acces", "journal"
    std::string_view label;
};
inline constexpr LoginTabSpec kLoginTabs[] = {
    {LoginTab::Connexion, "connexion", "Connexion"},
    {LoginTab::Compte, "compte", "Mon compte"},
    {LoginTab::Comptes, "comptes", "Comptes"},
    {LoginTab::Acces, "acces", "Acc\xC3\xA8s"},
    {LoginTab::Journal, "journal", "Journal"},
};
[[nodiscard]] std::string_view loginTabKey(LoginTab) noexcept;
[[nodiscard]] std::string_view loginTabLabel(LoginTab) noexcept;
// "comptes", "Comptes", "acces"... ; sinon Connexion.
[[nodiscard]] LoginTab loginTabFrom(std::string_view) noexcept;
// Les onglets que voit l'utilisateur connecte (nul : personne).
[[nodiscard]] std::vector<LoginTab> visibleLoginTabs(const Project&, const User* current);

// ---- la geometrie (le dessin et le clic) -------------------------------------------------
struct LoginMenuLayout {
    Box panel, title, close, status, body;
    std::vector<std::pair<LoginTab, Box>> tabs;
    double rowH{30}, fontSize{14};
    // Lot 13 : le renouvellement (un mot de passe perime, ou a changer a la
    // premiere connexion) - l'onglet Connexion demande le nouveau (newField,
    // confirmField), "Renouveler" (login) et "Annuler" (logout).
    bool renewal{false};
    // Connexion
    Box who, prev, account, next, secret, login, logout, message;
    // Mon compte
    Box info, oldField, newField, confirmField, change;
    // Comptes : les lignes, les boutons
    Box header;
    std::vector<Box> rows;
    Box up, down;
    struct Button {
        std::string key;      // "ajouter", "motdepasse", "activer", "groupe:precedent", "groupe:suivant", "supprimer"
        Box         box;
    };
    std::vector<Button> buttons;
    // Acces : une ligne par groupe, une case par role ; la deconnexion automatique
    std::vector<Box> groupRows;
    std::vector<Box> roleColumns;
    Box logoutRow, logoutMinus, logoutPlus;
};
//  Dans une zone w x h (pixels d'ecran). `rows` : les lignes de l'onglet
//  (comptes, journal) ; `groups`, `roles` : l'onglet Acces.
[[nodiscard]] LoginMenuLayout loginMenuLayout(double w, double h, const std::vector<LoginTab>& tabs, LoginTab tab,
                                              std::size_t rows, std::size_t groups, std::size_t roles, bool renewal = false);
// "fermer", "dehors", "onglet:comptes", "precedent", "suivant", "champ:secret",
// "bouton:connexion", "bouton:deconnexion", "champ:ancien", "champ:nouveau",
// "champ:confirmation", "bouton:changer", "ligne:2", "defiler:-1", "defiler:1",
// "bouton:ajouter", "bouton:motdepasse", "bouton:activer", "bouton:groupe:precedent",
// "bouton:groupe:suivant", "bouton:supprimer", "role:1,2" (groupe 1, role 2),
// "deconnexion:moins", "deconnexion:plus" ; lot 13, le renouvellement : "champ:nouveau",
// "champ:confirmation", "bouton:renouveler", "bouton:annuler" ; "" : rien.
[[nodiscard]] std::string loginMenuHit(const LoginMenuLayout&, LoginTab tab, double x, double y, std::size_t scroll = 0,
                                       std::size_t rows = 0);
[[nodiscard]] bool loginMenuPartBox(const LoginMenuLayout&, LoginTab tab, std::string_view part, Box& out);
// Les pas de la deconnexion automatique (minutes), dans l'ordre.
[[nodiscard]] const std::vector<int>& autoLogoutSteps();

} // namespace hmi
