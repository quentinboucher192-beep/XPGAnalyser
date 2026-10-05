// =============================================================================
//  hmi/HmiEquipment.hpp - les equipements du reseau (lot 15)
// -----------------------------------------------------------------------------
//  UN EQUIPEMENT, EN PLUS DE L'AUTOMATE DU PROJET : une centrale de mesure, un
//  variateur, un analyseur (Modbus TCP/IP), ou un appareil IP sans Modbus que
//  l'IHM surveille (Ethernet TCP/IP). Chacun a sa liaison - la meme que celle
//  de l'automate (comm::Link : son fil, ses blocs, la qualite) - et son plan :
//  les variables IHM qui s'y lient, chacune a son adresse.
//
//  LES ADRESSES. Un equipement d'un autre fabricant donne ses registres a la
//  facon Modicon (40101 : le registre de maintien 101, a partir de 1), un
//  automate Schneider a la sienne (%MW100 : le mot 100, a partir de 0). Les
//  deux se lisent, ramenees a la forme Schneider :
//
//      %MW100  %MF20  %MD20  %M5  %MW10.3  %IW4  %I7    a partir de 0
//      40101  400101  4x0101  4x101                     un registre de maintien (fonctions 3, 6, 16)
//      30001  3x0001                                    un registre d'entree (fonction 4), lecture seule
//      00017  0x0017                                    une bobine (fonctions 1, 5, 15)
//      10005  1x0005                                    une entree TOR (fonction 2), lecture seule
//      HR100  IR100  CO100  DI100                       les memes, a partir de 0
//      40101.3                                          le bit 3 d'un registre
//
//  LA MISE A L'ECHELLE : le registre brut [brut min, brut max] devient la
//  valeur [echelle min, echelle max] (0..27648 -> 0..10 bar), et l'inverse a
//  l'ecriture.
//
//  LES RESEAUX : IPv4, le masque en /24 ou en 255.255.255.0 ; un equipement
//  est joignable par un port du PC s'il est dans son reseau.
// =============================================================================
#pragma once

#include "HmiComm.hpp"
#include "HmiModel.hpp"
#include "../sim/Value.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::equip {

// ------------------------------------------------------------------ adresses ---
// La forme Schneider d'une adresse ("%MW100") ; vide si elle est illisible (why).
[[nodiscard]] std::string canonicalAddress(std::string_view address, std::string* why = nullptr);
// Une place d'un equipement : comme comm::placeAddress, pour toutes les formes ci-dessus.
bool placeEquipmentAddress(std::string_view address, sim::Type type, comm::Point& out, std::string* why = nullptr);
// "registre 40101", "registres 40101 a 40102", "bobine 17", "entree TOR 5",
// "registre d'entree 30001", "registre 40011, bit 3" : la place en Modicon (a partir de 1).
[[nodiscard]] std::string modiconText(const comm::Point&);

// Le type d'un nom ("INT", "REAL"...) ; inconnu : INT.
[[nodiscard]] sim::Type typeOfName(std::string_view typeName) noexcept;
// Le type du registre d'une variable liee : celui de la variable, ou son type
// brut quand elle est mise a l'echelle (INT par defaut).
[[nodiscard]] sim::Type registerType(const Variable&) noexcept;

// Les variables IHM liees a cet equipement, dans l'ordre du projet.
[[nodiscard]] std::vector<const Variable*> boundVariables(const Project&, const Equipment&);
// Le plan de l'equipement : ses variables liees, sous leur nom ; les
// illisibles dans les refusees, avec la raison.
[[nodiscard]] comm::Plan buildPlan(const Project&, const Equipment&);
// Les reglages de sa liaison. Lot 17 : `twinPort` non nul, vers son jumeau
// (127.0.0.1, le port du serveur local) ; sinon vers le vrai appareil.
[[nodiscard]] comm::Settings settingsOf(const Equipment&, int twinPort = 0);

// La premiere adresse libre de l'equipement pour une variable de ce type : le
// registre qui suit le plus haut deja pris (%MW0 s'il n'y en a pas), dans
// l'ecriture de ses autres variables (40001 ou %MW).
[[nodiscard]] std::string nextFreeAddress(const Project&, const Equipment&, sim::Type type);

// ------------------------------------------------------------ mise a l'echelle ---
[[nodiscard]] double scaleIn(const Variable&, double raw) noexcept;
[[nodiscard]] double scaleOut(const Variable&, double value) noexcept;
// Ce que vaut la variable, lu brut dans le registre ; ce qu'on ecrit dans le
// registre pour cette valeur (arrondi et borne pour un registre entier).
[[nodiscard]] sim::Value fromRegister(const Variable&, const sim::Value& raw);
[[nodiscard]] sim::Value toRegister(const Variable&, const sim::Value& value);

// ------------------------------------------------------------------- IPv4 ---
bool parseIpv4(std::string_view text, std::uint32_t& out) noexcept;
[[nodiscard]] std::string ipv4Text(std::uint32_t ip);
// "255.255.255.0", "/24", "24" -> 24 ; faux : illisible, ou pas un masque (des 1 puis des 0).
bool parseMask(std::string_view text, int& prefix) noexcept;
[[nodiscard]] std::uint32_t maskOf(int prefix) noexcept;
[[nodiscard]] std::string   maskText(int prefix);
[[nodiscard]] std::uint32_t networkOf(std::uint32_t ip, int prefix) noexcept;
[[nodiscard]] bool          sameNetwork(std::uint32_t a, std::uint32_t b, int prefix) noexcept;
[[nodiscard]] std::string   networkText(std::uint32_t ip, int prefix);        // "192.168.1.0 / 24"
// Une adresse d'hote de ce reseau : ni celle du reseau, ni la diffusion, ni
// 0.0.0.0, 127.x, 169.254.x (sauf demande), multicast.
bool validHost(std::uint32_t ip, int prefix, std::string* why = nullptr);
// Un reglage de port : adresse, masque, passerelle (vide : aucune) - la
// passerelle dans le meme reseau, differente de l'adresse.
bool validPortSettings(std::string_view ip, std::string_view mask, std::string_view gateway, std::string* why = nullptr);
// Une adresse libre a proposer dans le reseau de `anyIp` / prefix : .100, puis
// .101... ; aucune (0) si tout est pris.
[[nodiscard]] std::uint32_t suggestAddress(std::uint32_t anyIp, int prefix, const std::vector<std::uint32_t>& taken);

// Un reseau du PC : l'adresse d'un port et son masque.
struct Network {
    std::uint32_t ip{0};
    int           prefix{24};
    int           adapter{-1};      // le rang du port (PcNetwork, HmiNetInfo)
};
// Le reseau du PC qui contient cette adresse ; -1 : aucun ("hors reseau").
[[nodiscard]] int networkFor(std::uint32_t host, const std::vector<Network>& networks) noexcept;

} // namespace hmi::equip
