// =============================================================================
//  hmi/HmiDossier.hpp - le dossier de l'IHM (lot 13) : Word et PDF
// -----------------------------------------------------------------------------
//  LA DOCUMENTATION DU PROJET, FAITE PAR LE PROJET. Un document qu'on remet
//  avec l'IHM : la page de garde (nom, version, auteur, dates), la presentation
//  (resolution, vue de demarrage, les chiffres), chaque vue avec sa VIGNETTE,
//  sa taille, ses objets et ou elle mene, les variables IHM, les alarmes, les
//  recettes (elements et jeux), les utilisateurs et la securite (groupes,
//  roles, comptes, politique - jamais un mot de passe ni une empreinte), les
//  scripts et les fonctions (leur code), les historiques, les essais de
//  reception, les ressources et les styles.
//
//  DEUX TEMPS. buildDossier() fait le CONTENU (des blocs : titres,
//  paragraphes, tableaux, images, code) ; dossierDocx() et dossierPdf() le
//  METTENT EN PAGE. Les vignettes sont des JPEG que l'ecran dessine (le module
//  IHM ne dessine pas) ; sans elles, le dossier est complet, sans images.
//
//  SANS BIBLIOTHEQUE : le .docx est un zip de XML (WordprocessingML : styles
//  Titre, Titre 1, Titre 2, tableaux a en-tete repete, pied de page numerote) ;
//  le PDF est ecrit a la main (A4 portrait, Helvetica et Courier, les JPEG tels
//  quels, les signets des titres, "Page 2 / 14").
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hmi {

struct ScenarioReport;

struct DossierBlock {
    enum class Kind : std::uint8_t { Title, Subtitle, Heading1, Heading2, Paragraph, Note, Code, Table, Image, PageBreak };
    Kind                                  kind{Kind::Paragraph};
    std::string                           text;        // les textes ; Code : les lignes (\n)
    std::vector<std::string>              headers;     // Table
    std::vector<std::vector<std::string>> rows;
    std::vector<double>                   widths;      // Table : les proportions (vide : mesurees)
    int                                   image{-1};   // Image : le rang dans Dossier::images
};

struct DossierImage {
    std::string name;        // "Vue_Accueil"
    Bytes       jpeg;        // l'image, en JPEG
    int         width{0}, height{0};
    std::string caption;     // la legende
};

struct Dossier {
    std::string               title;      // "Dossier de l'IHM Armoire_Gaz"
    std::string               author;
    std::string               date;       // "2026-09-25 10:12"
    std::vector<DossierBlock> blocks;
    std::vector<DossierImage> images;
};

struct DossierOptions {
    bool code{true};                                        // le code des scripts et des fonctions
    const std::map<Id, ScenarioReport>* reports{nullptr};   // le dernier passage des essais (facultatif)
};

// Le contenu. `thumbnails` : la vignette de chaque vue (par son identifiant) ;
// les vues sans vignette sont decrites sans image.
[[nodiscard]] Dossier buildDossier(const Project&, const std::map<Id, DossierImage>& thumbnails, const DossierOptions& = {});
// La mise en page.
[[nodiscard]] Bytes dossierDocx(const Dossier&);
[[nodiscard]] Bytes dossierPdf(const Dossier&);
// La taille d'une image JPEG (son en-tete SOF) ; faux : ce n'est pas un JPEG lisible.
[[nodiscard]] bool jpegSize(const Bytes&, int& width, int& height);

} // namespace hmi
