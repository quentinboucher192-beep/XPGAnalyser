// =============================================================================
//  export/HtmlPage.hpp - 1.12.30 : une appli en page web, un seul fichier .html
// -----------------------------------------------------------------------------
//  LA PAGE QUI S'OUVRE PARTOUT : un seul fichier, aucun lien vers Internet (le
//  style, les scripts, les images sont dedans), qui s'ouvre dans n'importe quel
//  navigateur, se lit sur un telephone, s'imprime (chaque page a la suite).
//
//  UNE PAGE A LA FOIS, COMME DANS L'APPLI : a gauche le menu (les vues, les
//  popups, les listes du projet), une recherche qui le filtre ; au centre la page
//  choisie. L'adresse suit (#vue), donc Precedent du navigateur revient. Un
//  objet qui ouvre une vue ou une popup est cliquable (data-go, data-popup) ; une
//  popup s'ouvre par-dessus.
//
//  L'ARCHIVE DU PROJET EST DEDANS (base64, project/ProjectArchive.hpp) : le
//  bouton du haut la telecharge (.xihm, .xapi), et IHM Studio ou API Studio
//  reconstruit le projet a l'identique depuis la page.
//
//  TOUT TEXTE ECRIT PASSE PAR escape() : un nom avec < ou & ne casse pas la
//  page, et rien de ce que contient le projet ne s'y execute.
// =============================================================================
#pragma once

#include "../project/ProjectArchive.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace exporter::html {

// & < > " ' : le texte et les attributs.
[[nodiscard]] std::string escape(std::string_view);
// Un identifiant d'ancre sur : lettres, chiffres, - et _ (le reste devient -).
[[nodiscard]] std::string anchor(std::string_view);

struct NavLink {
    std::string target;      // l'identifiant de la section
    std::string label;
    std::string badge;       // "1280 x 800", "140"
};
struct NavGroup {
    std::string          title;
    std::vector<NavLink> links;
};

struct Section {
    std::string id;
    std::string title;
    std::string subtitle;    // a cote du titre, en petit
    std::string html;        // le contenu, deja echappe
    std::string css{"page"}; // "page", "view", "popup"
    std::string attributes;  // d'autres attributs de la section, deja echappes (data-width="1280")
};

struct Page {
    project::archive::Kind kind{project::archive::Kind::Ihm};
    std::string            application;   // "IHM Studio 1.12.30"
    std::string            title;         // le nom du projet
    std::string            subtitle;      // "version 1.0.0 - Q.BOUCHER - exportee le ..."
    std::vector<NavGroup>  nav;
    std::vector<Section>   sections;
    std::string            first;         // la section montree a l'ouverture
    std::string            defs;          // des <defs> SVG communs (les images des vues)
    std::string            extraCss;
    // L'archive du projet, portee par la page (nul : pas d'archive).
    const std::vector<std::uint8_t>* archive{nullptr};
    std::string            archiveName;   // "Ligne_gaz.xihm"
};
[[nodiscard]] std::string render(const Page&);

// ---- les morceaux -------------------------------------------------------------
struct Cell {
    std::string text;        // echappe a l'ecriture
    std::string css;         // une classe ("num", "mono", "warn"...)
    std::string html;        // non vide : du HTML deja echappe (un lien), a la place du texte
};
// Un tableau ; `filter` : un champ au-dessus qui filtre ses lignes.
[[nodiscard]] std::string table(const std::vector<std::string>& headers, const std::vector<std::vector<Cell>>& rows,
                                bool filter = true, std::string_view emptyText = "Aucune ligne.");
// La fiche : des lignes libelle / valeur.
[[nodiscard]] std::string card(const std::vector<std::pair<std::string, std::string>>& rows);
// Du code, colore (le ST : mots-cles, types, commentaires, chaines, nombres, adresses), numerote.
[[nodiscard]] std::string code(const std::vector<std::string>& lines, bool numbers = true);
// Des chiffres en tuiles : "20 vues", "1 438 objets".
[[nodiscard]] std::string tiles(const std::vector<std::pair<std::string, std::string>>& valueAndLabel);

} // namespace exporter::html
