// =============================================================================
//  help/ReleaseNotes.hpp - 1.11 (chantier T2) : LA table des notes de version
// -----------------------------------------------------------------------------
//  Les notes de chaque version, dans les sources : 1.11, 1.10.3, 1.10.2,
//  1.10.1, 1.10.0, 1.9.0, 1.8.0. Une ligne : la version, le domaine (Simulation,
//  Scripts, Editeur IHM, Aide, Corrige...), le genre (N nouveau, M modifie,
//  C corrige), le texte, le sujet du centre d'aide (sa cle : "Tutoriel" joue
//  son tutoriel) et l'etape de ce tutoriel qui montre la ligne ("7" : Me
//  montrer ouvre le tutoriel a l'etape 7, en pause ; vide : Me montrer va a
//  l'endroit et l'encadre).
//
//  Sources : les LISEZ-MOI livres (1.8.0 a 1.10.1, 1.10.3), DECISIONS-0210
//  (1.10.2), la SPEC 1.11 et les journaux des chantiers T1, T2, T3 et R111
//  (1.11, premier jet du 02/10 au soir, a completer le 04/10). La page Notes
//  de version du centre d'aide, sa recherche et la fenetre Nouveautes
//  (help/Novelties) lisent cette table.
//
//  Une version de plus : sa ligne EN TETE de kReleases et ses notes EN TETE de
//  kNotes (ReleaseNotes.cpp). Pur, sans ecran.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace help::notes {

enum class Kind : std::uint8_t { New, Changed, Fixed };

struct Release {
    std::string_view version;   // "1.10.1"
    std::string_view date;      // "02/10/2026"
    std::string_view note;      // "livree le 02/10", "a venir . vers 16 h" ; vide : rien
};

struct Note {
    std::string_view version;
    std::string_view domain;
    Kind             kind{Kind::New};
    std::string_view text;
    std::string_view topic;          // la cle du sujet du centre d'aide
    std::string_view tutorialStep;   // "7" ; vide : Me montrer encadre l'endroit
    // La fenetre Nouveautes (help/Novelties) lit cette table : une ligne
    // marquee news est une de ses cartes. Les 31 cartes de la 1.10.0 et de la
    // 1.9.0 y sont, avec leurs id d'avant (les reglages nouveautes.vues).
    std::string_view id;             // "1.10.f8" ; vide : une ligne sans carte
    std::string_view title;          // le titre de la carte ; vide : aucun
    std::string_view go;             // Me montrer (la syntaxe de news::Item::go)
    std::string_view widget;         // le widget a encadrer
    std::string_view image;
    bool             news{false};
};

// Les versions, de la plus recente a la plus ancienne.
[[nodiscard]] const std::vector<Release>& releases();
[[nodiscard]] const Release* release(std::string_view version);

// Toutes les notes, version par version, domaine par domaine.
[[nodiscard]] const std::vector<Note>& all();
// Les notes d'une version ("1.10" et "1.10.0" : la meme).
[[nodiscard]] std::vector<const Note*> of(std::string_view version);
// Les domaines d'une version, dans l'ordre de la page.
[[nodiscard]] std::vector<std::string_view> domainsOf(std::string_view version);

// "N", "M", "C" ; "nouveau", "modifi\xC3\xA9", "corrig\xC3\xA9".
[[nodiscard]] std::string_view kindLetter(Kind);
[[nodiscard]] std::string_view kindLabel(Kind);

// La recherche du centre d'aide : dans le texte, le domaine et la version ;
// casse et accents ignores. Les plus recentes d'abord.
[[nodiscard]] std::vector<const Note*> search(std::string_view term);

// Le nombre de notes de chaque genre d'une version : "12 nouveaut\xC3\xA9s, 3 corrections".
[[nodiscard]] std::string summary(std::string_view version);

} // namespace help::notes
