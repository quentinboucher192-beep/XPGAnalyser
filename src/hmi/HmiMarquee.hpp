// =============================================================================
//  hmi/HmiMarquee.hpp - 1.12.12 (lot H) : LE TEXTE DEFILANT DEPUIS UNE SOURCE
// -----------------------------------------------------------------------------
//  « Messages depuis » (messagesFrom) : au lieu de son texte, le texte defilant
//  fait passer des messages, l'un apres l'autre, separes par son Separateur :
//   - une LIST, un VECTOR ou un tableau de textes (ARRAY OF STRING) : un message
//     par element, dans son ordre ; le nombre suit la liste en marche ;
//   - un joker du lot E : Consignes[*].Texte (un membre de chaque element d'un
//     tableau de structures), Messages[*] ;
//   - « alarmes en cours » : le message de chaque alarme active (de son Groupe
//     d'alarmes s'il en a un), dans l'ordre du bandeau ; aucune : son texte.
//  L'editeur montre les valeurs initiales (celles d'une LIST, d'un tableau de
//  textes du projet) ; une source vide fait defiler le texte de l'objet. Sans
//  source : son texte, comme avant. Generer verifie le type de la source.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::marquee {

inline constexpr std::string_view kMessagesFromKey = "messagesFrom";
inline constexpr std::string_view kAlarmsSource = "alarmes en cours";
// Le separateur d'un objet qui n'en a pas : un point median entre trois espaces.
inline constexpr std::string_view kDefaultSeparator = "   \xC2\xB7   ";

// « alarmes en cours » (sans casse ; « alarmes » aussi) : les alarmes actives.
[[nodiscard]] bool isAlarmSource(std::string_view from) noexcept;
// Les messages a la suite, separes (sans separateur apres le dernier).
[[nodiscard]] std::string join(const std::vector<std::string>& messages, std::string_view separator);
// L'APERCU DE L'EDITEUR : les valeurs initiales d'une LIST, d'un VECTOR ou d'un tableau de textes du projet
// (un joker vers un membre : le numero de chaque element), les textes d'une enumeration ; « alarmes en
// cours » : les messages des alarmes du projet (de ce groupe) ; une source inconnue : rien.
[[nodiscard]] std::vector<std::string> preview(const Project& p, std::string_view from, std::string_view group = {});
// Ce que Generer dit de la source (un avertissement) : vide si elle convient. `info` : une remarque
// (des elements qui ne sont pas des textes, une liste vide au demarrage).
[[nodiscard]] std::string sourceProblem(const Project& p, const View* view, std::string_view from, std::string* info = nullptr);

} // namespace hmi::marquee
