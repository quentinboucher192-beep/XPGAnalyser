// =============================================================================
//  help/CenterPages.hpp - 1.11 (chantier T2, tranche 3) : ce que dessinent
//  les pages Raccourcis et Notes de version du centre d'aide
// -----------------------------------------------------------------------------
//  Comme help/CenterView pour l'arbre : tout ce que ces deux pages decident
//  est ici, pur et essayable ; l'ecran (app/help/HelpCenterScreen) ne fait que
//  le dessiner. La page Signaler a deja sa partie pure : help/ProblemReport.
//
//  - Raccourcis (maquette, scene 6) : les groupes par contexte, la recherche,
//    les touches a dessiner (une liste de touches par variante), les reperes
//    "1.10" / "1.11", le compte "n raccourcis sur N".
//  - Notes de version (scene 7) : les versions en pastilles, l'en-tete de la
//    version (date, etat, resume par genre), le filtre par domaine, les lignes
//    N / M / C, et pour chacune ce que font "Me montrer" et "Tutoriel".
// =============================================================================
#pragma once

#include "CenterIndex.hpp"
#include "ReleaseNotes.hpp"
#include "Shortcuts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace help::center {

// ---- la page Raccourcis --------------------------------------------------------
struct KeyRow {
    std::vector<std::vector<std::string>> caps;   // une liste de touches par variante : {{"Ctrl","Y"},{"Ctrl","Maj","Z"}}
    std::string text;                             // ce qu'elle fait
    std::string since;                            // "", "1.10", "1.11" : le repere
    const keys::Shortcut* row{nullptr};
};
struct KeyGroup {
    keys::Context       context{keys::Context::General};
    std::string         label;                    // "G\xC3\xA9n\xC3\xA9ral"
    std::vector<KeyRow> rows;
};
struct KeysPage {
    std::vector<KeyGroup> groups;                 // les contextes qui ont au moins une ligne, dans l'ordre de la table
    std::size_t           shown{0}, total{0};
    std::string           status;                 // "61 raccourcis", "2 raccourcis sur 61", "Aucun raccourci pour \xC2\xAB ... \xC2\xBB"
};
// term : la recherche de la page (vide : tout) ; only : la pastille d'un seul
// contexte (aucune : tous).
[[nodiscard]] KeysPage keysPage(std::string_view term, std::optional<keys::Context> only = std::nullopt);

// ---- la page Notes de version ------------------------------------------------------
// "Me montrer" : a l'etape du tutoriel (en pause) si la ligne en nomme une,
// sinon a l'endroit (go, et le widget a encadrer), sinon la page du sujet.
enum class ShowKind : std::uint8_t { TutorialStep, Place, Topic };
struct ShowAction {
    ShowKind    kind{ShowKind::Topic};
    std::string go, widget;      // Place
    std::string topic;           // la cle du sujet (Topic, et le tutoriel de TutorialStep)
    std::size_t step{0};         // TutorialStep : l'index de l'etape (0 = la premiere ; la table ecrit "3" pour la 3e)
};
[[nodiscard]] ShowAction showAction(const notes::Note& n);
// La demande que "Me montrer" (TutorialStep) ou "Tutoriel" passe au lanceur de
// T1 ; returnTopic : la page des notes de la version, ou l'on revient.
[[nodiscard]] TutorialRequest showRequest(const notes::Note& n);
[[nodiscard]] TutorialRequest tutorialRequest(const notes::Note& n);

struct NoteRow {
    std::string letter;          // "N", "M", "C"
    std::string label;           // "nouveau", "modifi\xC3\xA9", "corrig\xC3\xA9"
    std::string title;           // le titre de la carte ; vide : une ligne sans carte
    std::string text;
    bool        news{false};     // aussi une carte de la fenetre Nouveautes
    ShowAction  show;
    std::string tutorialTopic;   // la cle du sujet dont "Tutoriel" joue le tutoriel ; vide : pas de bouton
    const notes::Note* note{nullptr};
};
struct NoteSection {
    std::string          domain;
    std::vector<NoteRow> rows;
};
struct NotesPage {
    std::vector<std::string> versions;     // les pastilles, de la plus recente a la plus ancienne
    std::string              version, date, state, summary;   // l'en-tete de la version
    std::vector<std::string> domains;      // le filtre : les domaines de la version, dans l'ordre de la page
    std::vector<NoteSection> sections;     // les domaines retenus (un seul si le filtre en nomme un)
    std::size_t              rows{0};
};
// version : "1.10" ou "1.10.0" ; vide ou inconnue : la plus recente.
// domain : le filtre ; vide : tous.
[[nodiscard]] NotesPage notesPage(std::string_view version, std::string_view domain = {});
// La cle de la page des notes d'une version dans le centre : "notes-1.10.0".
[[nodiscard]] std::string notesKey(std::string_view version);

// ---- la fiche A4 des raccourcis, a imprimer (tranche 8) ---------------------------
// L'appli n'a pas d'impression : "Imprimer..." ecrit cette page HTML (A4, @page)
// et l'ouvre dans le navigateur du systeme, qui l'imprime ou l'enregistre en PDF.
// Les memes lignes que keysPage(term, only), les touches en <kbd>, le texte echappe.
[[nodiscard]] std::string keysSheetHtml(std::string_view version, std::string_view term = {},
                                        std::optional<keys::Context> only = std::nullopt);

} // namespace help::center
