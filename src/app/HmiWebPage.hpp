// =============================================================================
//  app/HmiWebPage.hpp - 1.12.30 : le projet IHM en page web, un seul .html
// -----------------------------------------------------------------------------
//  CHAQUE VUE ET CHAQUE POPUP DESSINEE PAR LE VRAI DESSIN DE L'APPLI : paintHmiView
//  (celui de l'editeur, de la simulation, des vignettes du dossier) ecrit dans
//  app/SvgRenderer - du SVG, net a tous les zooms, ses textes cherchables. La vue
//  composee : son ecran modele, son en-tete et son pied, comme en marche.
//
//  LA NAVIGATION : un objet dont une action ouvre une vue ou une popup (au clic,
//  au double-clic, a l'appui long) devient une zone cliquable, comme les boutons
//  d'une barre de navigation ; Precedent et Fermer la popup aussi. Le menu de
//  gauche liste les vues et les popups ; la vue de demarrage s'ouvre d'abord.
//
//  ET LE RESTE, EN LECTURE : les alarmes, les variables IHM, les scripts et les
//  fonctions (leur code colore), la fiche du projet. L'archive .xihm complete est
//  dans la page : Projet > Importer depuis une page web... reconstruit le projet.
//
//  Rien d'ecrit ici n'est lu tel quel par le navigateur : chaque texte du projet
//  passe par exporter::html::escape.
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"
#include "../project/ProjectStore.hpp"
#include "../ui/Theme.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace app {

struct WebPageInfo {
    std::string                      application;     // "IHM Studio 1.12.30"
    std::string                      exportedAt;      // "23/10/2026 \xC3\xA0 19:40"
    project::Manifest                manifest;        // la fiche du dossier (etat, version, auteur...)
    bool                             hasManifest{false};
    const std::vector<std::uint8_t>* archive{nullptr};   // l'archive du projet, portee par la page
    std::string                      archiveName;     // "Ligne_gaz.xihm"
};

struct WebPageStats {
    std::size_t views{0}, popups{0}, hotspots{0}, images{0}, elements{0};
    std::size_t svgBytes{0}, pageBytes{0};
};

// LA PAGE EN DEUX TEMPS - la mise a jour a chaque enregistrement ne doit pas figer l'appli :
// draw() dessine les vues une a une, sur le fil de l'interface (le dessin des vues y vit), dans
// le temps donne ; page() compose la page (du texte : sur n'importe quel fil). Le projet est
// copie : il peut changer pendant ce temps.
class HmiWebPageBuilder {
public:
    HmiWebPageBuilder(hmi::Project project, ui::Theme theme);
    ~HmiWebPageBuilder();
    HmiWebPageBuilder(const HmiWebPageBuilder&) = delete;
    HmiWebPageBuilder& operator=(const HmiWebPageBuilder&) = delete;
    // Des vues pendant `budgetMs` au plus (une au moins) ; vrai : toutes sont dessinees.
    bool draw(double budgetMs);
    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] std::string page(const WebPageInfo&, WebPageStats* stats = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Les deux temps a la suite (les essais, l'export a la demande).
[[nodiscard]] std::string hmiWebPage(const hmi::Project&, const ui::Theme&, const WebPageInfo&, WebPageStats* stats = nullptr);

} // namespace app
