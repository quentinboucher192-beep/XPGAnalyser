// =============================================================================
//  hmi/HmiPrompt.hpp - 1.11.7 : le champ de saisie de l'action Clavier virtuel
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : « ajouter -> ouvrir un clavier virtuel champ de
//  saisie (et on met des parametres) ». L'action Clavier virtuel (Operation::Keyboard)
//  ouvre, par-dessus la vue en marche, un petit panneau : son titre, la valeur en
//  cours (choisie : la premiere frappe la remplace), les limites et l'unite, un
//  message ; Valider, Annuler ; le clavier virtuel dessous (numerique pour un nombre,
//  complet pour un texte, ou celui des reglages). Entree valide, Echap annule. Il est
//  modal : les clics ailleurs ne font rien.
//
//  La geometrie (celle du dessin et du clic), dans une zone w x h de l'ecran de l'IHM.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>

namespace hmi {

struct PromptLayout {
    Box    panel, title, close, field, limits, message, ok, cancel;
    double rowH{34}, fontSize{16};
};
[[nodiscard]] PromptLayout promptLayout(double w, double h);
// "bouton:valider", "bouton:annuler", "fermer", "champ", "dehors" ; "" : dans le panneau, rien.
[[nodiscard]] std::string promptHit(const PromptLayout&, double x, double y);
[[nodiscard]] bool        promptPartBox(const PromptLayout&, std::string_view part, Box& out);

} // namespace hmi
