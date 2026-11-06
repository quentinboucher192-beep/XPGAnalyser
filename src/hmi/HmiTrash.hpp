// =============================================================================
//  hmi/HmiTrash.hpp - 1.26.1 : LA CORBEILLE DU PROJET IHM
// -----------------------------------------------------------------------------
//  Le client (06/11) : << la corbeille ne fonctionne pas je supprime des choses
//  ca n'apparait pas dedans (j'ai supprime une ressource, et une vue) >>.
//
//  CE QUI PART DU PROJET ENTRE DANS SA CORBEILLE (Project::trash) : une vue (de
//  tout role ; un symbole emporte les popups parties avec lui), une ressource et
//  ses octets, un fichier externe, un script general, une fonction IHM, une
//  variable IHM, un type IHM, une alarme, une recette, un style nomme. Un objet
//  d'une vue n'y entre pas : Ctrl+Z suffit.
//
//  DANS LE PROJET, PAS A COTE : la commande de la suppression (deleteFromProject,
//  HmiCommands.hpp) l'y met - Ctrl+Z l'en ressort et rend l'element, Ctrl+Y l'y
//  remet ; un projet non enregistre n'a jamais une corbeille qui le contredit.
//  Enregistrer l'ecrit dans ihm/supprimes/ (HmiStore.cpp) : chaque element, un
//  petit projet a lui (ihm.txt, vues/, ressources/...), relu a l'ouverture.
//
//  RESTAURER le remet a la fin de sa liste, dans son dossier ; un nom deja pris
//  entretemps : le premier libre (Vue_Accueil_2, logo_2.png - les noms que
//  l'application donne, valides partout). Rien d'autre ne le cite : il ne laisse
//  aucune reference cassee (une popup dont le symbole n'est plus la redevient
//  une popup ordinaire).
//
//  LA PURGE : ce qui y est depuis plus de 7 jours part a l'ouverture du projet ;
//  Supprimer definitivement, Vider : sans retour (et sur le disque, tout de suite).
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::bin {

// Les genres, et leur cle (TrashItem::kind, ecrite dans ihm/supprimes/corbeille.txt).
enum class Kind : std::uint8_t { View, Resource, External, Script, Function, Variable, Type, Alarm, Recipe, Style, Count };
[[nodiscard]] std::string_view   kindKey(Kind) noexcept;            // "vue", "ressource", "externe", "script"...
[[nodiscard]] std::optional<Kind> kindOf(std::string_view key) noexcept;

// Maintenant : les secondes depuis 1970 (temps universel).
[[nodiscard]] std::int64_t now() noexcept;

// CE QUI EST PARTI : ce que `before` avait et que `after` n'a plus (par identifiant), chacun dans after.trash
// (un element par vue - avec les popups de son symbole parties en meme temps -, par ressource, par script...).
// `now` : 0, l'horloge. Rend combien d'elements sont entres.
std::size_t capture(const Project& before, Project& after, std::int64_t now = 0);

// RESTAURER l'element `item` : ce qu'il garde revient dans `p` (a la fin de sa liste ; un nom pris : le premier libre),
// l'element quitte la corbeille. `shown` : le nom qu'il a maintenant ("Vue_Accueil", ou "Vue_Accueil -> Vue_Accueil_2"
// renomme). Faux et `why` : introuvable, illisible.
bool restore(Project& p, Id item, std::string* shown = nullptr, std::string* why = nullptr);
// Pour de bon (en memoire) ; faux : il n'y etait pas.
bool forget(Project& p, Id item);
// Ceux qui y sont depuis plus de `days` jours (`now` : 0, l'horloge).
[[nodiscard]] std::vector<Id> older(const Project& p, int days, std::int64_t now = 0);
[[nodiscard]] const TrashItem* find(const Project& p, Id item) noexcept;

// La taille d'un contenu : ce que l'enregistrement en ecrirait (une ressource : ses octets).
[[nodiscard]] std::uint64_t sizeOf(const Project& content);

} // namespace hmi::bin
