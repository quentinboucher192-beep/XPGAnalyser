// =============================================================================
//  hmi/HmiForms.hpp - la geometrie des objets a saisie (lot 8)
// -----------------------------------------------------------------------------
//  LE DESSIN ET LE CLIC LISENT LA MEME GEOMETRIE, comme le gestionnaire de
//  recettes du lot 6 : un champ se dessine ou il se clique. Tout est dans le
//  repere de l'objet (0,0 en haut a gauche, w x h), en pixels de vue.
//
//    Connexion              titre, Utilisateur (un champ, ou une liste qu'on
//                           fait defiler avec < >), Mot de passe (ou Code),
//                           le bouton, une ligne de message.
//    Changer le mot de passe  titre, Ancien, Nouveau, Confirmation, le bouton,
//                           une ligne de message.
//    Gestion des utilisateurs  la barre de boutons et le tableau, comme le
//                           gestionnaire de recettes (recipeManagerLayout).
//
//  LE CLAVIER VIRTUEL (un champ qui le demande, en marche) : ses touches, dans
//  un panneau de w x h, pour le dessin et le clic.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct FormField {
    std::string name;       // "utilisateur", "motdepasse", "ancien"...
    std::string label;      // "Utilisateur", "Mot de passe"...
    Box         labelBox;
    Box         box;
    bool        secret{false};
};
struct FormLayout {
    double                 scale{1};    // la taille du texte / 15
    Box                    title;
    std::vector<FormField> fields;
    Box                    userBox;     // connexion en liste : le nom choisi
    Box                    prev, next;  // ... et ses fleches
    Box                    button;
    Box                    message;
    [[nodiscard]] const FormField* field(std::string_view name) const noexcept;
};
// `codeLabel` : l'utilisateur choisi se connecte par un code dynamique.
[[nodiscard]] FormLayout loginLayout(const Object&, double w, double h, bool codeLabel = false);
[[nodiscard]] FormLayout passwordLayout(const Object&, double w, double h);
// Ce qu'un clic en (x, y) touche : "champ:motdepasse", "precedent", "suivant",
// "bouton", "champ" (un champ de saisie), "" (rien).
[[nodiscard]] std::string formHit(const Object&, double w, double h, double x, double y);

// Gestion des utilisateurs : les boutons par defaut, et leur geometrie (celle
// du gestionnaire de recettes).
inline constexpr std::string_view kUserButtons[] = {"Ajouter", "Modifier", "Supprimer", "Activer", "Mot de passe"};
inline constexpr std::string_view kUserColumns[] = {"Identifiant", "Nom", "Groupe", "Protection", "\xC3\x89tat"};
[[nodiscard]] std::string userManagerHit(const Object&, double w, double h, double x, double y, std::size_t users);

// Le texte d'un champ secret : un point par caractere.
[[nodiscard]] std::string maskedText(std::string_view text);
// "Utilisateur connecte" : le gabarit rempli. {login} {nom} {groupe} {niveau}
// {depuis} (duree de la connexion) {reste} (avant la deconnexion automatique).
struct UserInfoValues {
    std::string login, name, group;
    int         level{0};
    double      since{-1};       // secondes ; negatif : inconnu
    double      remaining{-1};   // secondes ; negatif : pas de minuterie
};
[[nodiscard]] std::string fillUserInfo(std::string_view pattern, const UserInfoValues&);
// "2 min 05 s", "1 h 03 min"
[[nodiscard]] std::string shortDuration(double seconds);

// ---- le clavier virtuel ------------------------------------------------------
struct KeyCap {
    std::string label;      // ce qu'on lit sur la touche
    std::string text;       // ce qu'elle tape (vide : une touche de commande)
    std::string command;    // "retour", "entree", "echap", "maj", "gauche", "droite" ; vide : un caractere
    Box         box;        // dans le panneau
};
// mode : "numerique" ou "complet" ; `shift` : les majuscules.
[[nodiscard]] std::vector<KeyCap> keyboardLayout(std::string_view mode, double w, double h, bool shift);
// La taille conseillee du panneau (pixels ecran) pour un mode.
[[nodiscard]] Box keyboardSize(std::string_view mode);
// La touche sous (x, y) ; -1 : aucune.
[[nodiscard]] int keyboardHit(const std::vector<KeyCap>&, double x, double y);

} // namespace hmi
