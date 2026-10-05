// =============================================================================
//  hmi/HmiSignature.hpp - la signature electronique d'une commande (lot 13)
// -----------------------------------------------------------------------------
//  UNE COMMANDE SENSIBLE N'AGIT QU'APRES LA SIGNATURE. L'objet dit laquelle
//  (propriete "signature") :
//
//    simple   le signataire (l'utilisateur connecte, sinon un compte choisi aux
//             fleches) retape son mot de passe (ou son code) et donne un motif ;
//    double   et un second compte, d'un niveau au moins "signatureLevel",
//             appose son visa avec son propre mot de passe.
//
//  Le motif : l'un de ceux de l'objet ("signatureReasons" : Reglage;Essai),
//  choisi aux fleches, sinon tape. Le geste attend (le clic, le choix, le
//  glisser, la saisie) ; signe, il agit, et le journal d'audit garde qui a
//  signe, pourquoi, et ce que le geste a change. Un mot de passe faux compte
//  comme un echec de connexion (le verrouillage le voit) ; Annuler : rien n'agit.
//
//  Le panneau est natif (dessine par l'IHM, par-dessus la vue) ; le dessin et
//  le clic lisent la meme geometrie (signatureLayout).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct SignatureRequest {
    enum class Gesture : std::uint8_t { Click, Part, Drag, Input };
    Gesture                  gesture{Gesture::Click};
    Id                       view{kNoId}, object{kNoId};
    std::string              part;          // Part : la partie cliquee
    double                   fraction{0};   // Drag : la course
    std::string              text;          // Input : la valeur tapee
    std::string              source;        // "Vue_Pompes/Btn_Marche"
    std::string              what;          // ce qui est signe : "Marche_Pompe : FAUX -> VRAI"
    bool                     twoSigners{false};
    std::vector<std::string> reasons;       // les motifs proposes (vide : a taper)
    int                      visaLevel{3};  // double : le niveau du second signataire
    double                   since{0};
    std::string              signer;        // le compte choisi aux fleches (personne n'est connecte)
    std::string              visa;          // double : le second compte
    std::size_t              reason{0};     // le motif choisi
};

// Les motifs d'un objet : "Reglage; Essai" -> {"Reglage", "Essai"}.
[[nodiscard]] std::vector<std::string> signatureReasons(const Object&);
// "simple", "double" ou "aucune" (la propriete "signature" de l'objet).
[[nodiscard]] std::string signatureMode(const Object&);
// Cette partie cliquee ecrit-elle la valeur (une position, une option, un choix
// de la liste, Valider, une case du programmateur) ? Ouvrir une liste, regler
// une date aux fleches : non - la signature attend le geste qui ecrit.
[[nodiscard]] bool signatureCommits(const Object&, std::string_view part);

// ---- la geometrie ------------------------------------------------------------------------------
struct SignatureLayout {
    Box panel, title, close, what;
    Box signerLabel, signerPrev, signer, signerNext, password;
    Box reasonLabel, reasonPrev, reason, reasonNext, reasonField;
    Box visaTitle, visaLabel, visaPrev, visa, visaNext, visaPassword;
    Box sign, cancel, message, status;
    double rowH{30}, fontSize{14};
    bool   twoSigners{false}, reasonList{false}, chooseSigner{false};
};
//  Dans une zone w x h (pixels de l'ecran de l'IHM). `chooseSigner` : personne
//  n'est connecte, le signataire se choisit aux fleches.
[[nodiscard]] SignatureLayout signatureLayout(double w, double h, bool twoSigners, bool reasonList, bool chooseSigner);
// "fermer", "dehors", "signataire:precedent", "signataire:suivant", "champ:motdepasse",
// "motif:precedent", "motif:suivant", "champ:motif", "visa:precedent", "visa:suivant",
// "champ:visa", "bouton:signer", "bouton:annuler" ; "" : rien.
[[nodiscard]] std::string signatureHit(const SignatureLayout&, double x, double y);
[[nodiscard]] bool        signaturePartBox(const SignatureLayout&, std::string_view part, Box& out);

} // namespace hmi
