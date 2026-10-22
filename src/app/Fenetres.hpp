// =============================================================================
//  app/Fenetres.hpp - 1.12.27 : PLUSIEURS FENETRES D'IHM STUDIO
// -----------------------------------------------------------------------------
//  Relancer l'application ramene toujours la premiere fenetre (l'instance unique,
//  1.11.2, decision 195). Une AUTRE fenetre se demande : Projet > Nouvelle fenetre
//  (Ctrl+Maj+N), le clic droit de l'accueil (Nouvelle fenetre, Ouvrir dans une
//  nouvelle fenetre). C'est le meme programme, relance avec --nouvelle-fenetre (et
//  le projet a ouvrir) : il ne prend pas l'instance unique.
//
//  LE MEME PROJET DANS DEUX FENETRES s'ecraserait a l'enregistrement. Chaque
//  fenetre annonce le projet qu'elle ouvre : un petit fichier par projet dans le
//  dossier des fenetres (le dossier des donnees, fenetres/), avec le numero du
//  processus et l'heure. Une autre fenetre qui ouvre ce projet le voit (le
//  processus vit encore) et le demande d'abord.
//
//  Le meme dossier garde le presse-papiers partage des objets (copier dans une
//  fenetre, coller dans l'autre : app/hmi/HmiPartage.hpp).
// =============================================================================
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace app::fenetres {

// Lancer une autre fenetre (le meme programme, --nouvelle-fenetre [projet]). Faux :
// `pourquoi` dit ce qui a manque (le programme introuvable, le lancement refuse).
bool ouvrirNouvelleFenetre(const std::string& projet = {}, std::string* pourquoi = nullptr);

// Le dossier des fenetres : celui des donnees de l'application + fenetres/ ; sans
// donnees (les essais), le dossier temporaire. poserDossier le change (les essais ;
// un chemin vide : le dossier habituel).
[[nodiscard]] std::filesystem::path dossier();
void poserDossier(std::filesystem::path p);

// Le processus de cette fenetre, et s'il vit encore (un autre).
[[nodiscard]] long processusCourant() noexcept;
[[nodiscard]] bool processusVivant(long pid) noexcept;

// Ce processus a ouvert ce projet (son dossier) / l'a ferme.
void annoncer(const std::string& dossierProjet);
void retirer(const std::string& dossierProjet);

// Le projet est-il ouvert par une AUTRE fenetre encore vivante ? Son processus et
// depuis quand ("14:05"). Un fichier laisse par une fenetre disparue est retire.
struct Presence {
    long        pid{0};
    std::string depuis;
};
[[nodiscard]] std::optional<Presence> autreFenetre(const std::string& dossierProjet);

} // namespace app::fenetres
