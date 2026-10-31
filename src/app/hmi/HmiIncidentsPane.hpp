// =============================================================================
//  app/hmi/HmiIncidentsPane.hpp - 1.20.0 : IHM > Ressources > Incidents
// -----------------------------------------------------------------------------
//  LE VOLET DES INCIDENTS. Les dossiers d'incident que la surveillance
//  (IncidentHost, HmiIncidentHost.hpp) cree quand une alarme apparait : deux
//  onglets, En attente et Valides ; chaque ligne, une MINIATURE de la capture
//  (decodee une fois, sur un fil a part, puis gardee en texture), le nom du
//  dossier, la date, l'alarme, l'equipement, la vue affichee, la synthese, la
//  certitude, l'etat de l'analyse et celui de la validation.
//
//  A droite, LA FICHE de l'incident choisi :
//    - la capture en grand (une image fixe : jamais dite video) et la video
//      Motion-JPEG (lue ici par le decodeur des objets Video, ou ouverte avec le
//      lecteur du systeme) ;
//    - la fiche de diagnostic en dix sections depliables (hmi::incident::diagnosis,
//      decoupee sur ses titres "### n.") ;
//    - les dependances (snapshot.chain) : un clic ouvre l'element dans le projet
//      s'il existe encore ; sinon la fiche le dit et garde son nom d'origine ;
//    - les analyses (l'initiale et les suivantes ; Comparer : l'initiale et la
//      derniere cote a cote), les commentaires, la validation, l'integrite.
//
//  LES GESTES passent par l'hote de la surveillance (en arriere-plan) : le
//  volet ne lit le disque que par IncidentHost::refresh (Actualiser, et a
//  l'ouverture) et se refait quand la liste change (sa revision). Les dialogues
//  (commentaire, validation, suppression, reglages) sont a l'ecran (Hosts) ;
//  sans hote, les methodes publiques font le geste directement (les essais).
//
//  Rien de lourd au dessin : la fiche se met en page quand l'incident, la
//  largeur ou un depli change ; les images se decodent une fois, hors du fil
//  de l'interface ; les textures se font sur lui.
// =============================================================================
#pragma once

#include "HmiIncidentHost.hpp"
#include "HmiPanels.hpp"
#include "../ai/AiIncident.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiIncident.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace app {

class HmiIncidentList;
class HmiIncidentSheet;

// ---- ce qui se teste sans ecran ------------------------------------------------------------------------------------
namespace incidentsui {

// La fiche de diagnostic decoupee : son titre ("Incident : X") et ses sections ("1. Resume" et son texte).
struct Section {
    std::string title, body;
};
// Les sections "### n." dans l'ordre (1 a 10) ; une analyse jointe (section 7) qui ecrit elle-meme des titres "### 8."
// ne coupe pas la fiche : les sections 8 a 10 sont cherchees depuis la fin.
[[nodiscard]] std::vector<Section> splitDiagnosis(std::string_view text, std::string* title = nullptr);

// Une liste de noms (alarmes surveillees, ignorees) <-> le texte d'un champ ("POMPE_*, NIVEAU_HAUT").
[[nodiscard]] std::string              listText(const std::vector<std::string>& names);
[[nodiscard]] std::vector<std::string> listOf(std::string_view text);

// LES REGLAGES DE LA SURVEILLANCE, champ par champ, dans l'ordre du dialogue : Surveillance (Activee / Desactivee),
// Alarmes surveillees, Alarmes ignorees, Defauts de communication (Oui / Non), Secondes avant, Secondes apres, Images
// par seconde, Maintien apres (s), Anti-rebond (s).
inline constexpr std::size_t kSettingsFields = 9;
[[nodiscard]] std::vector<std::string> settingsLabels();
[[nodiscard]] std::vector<std::string> settingsValues(const hmi::incident::Settings&);
// Faux et `why` (et `field`, le champ fautif) : une valeur refusee ; `io` n'est pas touche.
bool settingsFrom(const std::vector<std::string>& values, hmi::incident::Settings& io, std::string* why = nullptr,
                  std::size_t* field = nullptr);

// "vid\xC3\xA9o Motion-JPEG de 30 images (15 s)" ; sans video : "pas de vid\xC3\xA9o (la raison)".
[[nodiscard]] std::string videoLabel(const ai::incidents::Item&);
// "intact : 7 fichiers v\xC3\xA9rifi\xC3\xA9s" ; sinon les fichiers manquants, modifies, en trop (une ligne chacun).
[[nodiscard]] std::vector<std::string> integrityLines(const ai::incidents::Integrity&);
// "En attente", "Valid\xC3\xA9 le 31/10/2026 17:52 par Quentin".
[[nodiscard]] std::string validationLabel(const ai::incidents::Item&);
// "2026-10-31 17:40:12.350" -> "31/10/2026 17:40:12" (illisible : tel quel).
[[nodiscard]] std::string dateText(std::string_view stamp);

// Un maillon de la chaine -> l'element a ouvrir (son genre, son nom, sa vue) et s'il existe encore dans `project`.
struct Target {
    std::string kind, name, view;
    bool        exists{false};
};
[[nodiscard]] Target targetOf(const hmi::Project* project, const hmi::incident::Link& link);

} // namespace incidentsui

// ---------------------------------------------------------------------------------------------------------- le volet --
class HmiIncidentsPane final : public ui::Widget {
public:
    HmiIncidentsPane(std::string id, hmi::DocumentPtr doc, IncidentHost* host);
    ~HmiIncidentsPane() override;

    // Les dialogues et ce qui sort du volet (l'ecran). Sans hote : le geste se fait sans demander (les essais).
    struct Hosts {
        std::function<std::string()> author;                                             // l'auteur ("" : aucun)
        std::function<void(const std::string& folder, const std::string& name)> comment;  // demande le texte
        std::function<void(const std::string& folder, const std::string& name)> validate; // demande la note
        std::function<void(const std::string& folder, const std::string& name)> remove;   // demande confirmation
        std::function<void()> settings;                                                    // les reglages
        // Ouvrir un element du projet (genre : celui de plan::Ref, "vue", "objet", "variable"...) ; faux : introuvable.
        std::function<bool(const std::string& kind, const std::string& name, const std::string& view)> reveal;
        std::function<void(const std::string& folder)> openFolder;
        std::function<std::string(const std::string& file)> openFile;                     // "" : ouvert ; sinon pourquoi
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // Relire les dossiers (en arriere-plan, par l'hote) ; la liste se refait a son retour.
    void refresh();
    // Reprendre la derniere liste de l'hote si elle a change (appele au dessin ; les essais l'appellent).
    bool poll();

    // ---- la liste ----
    void showValidated(bool validated);
    [[nodiscard]] bool showingValidated() const noexcept { return validatedTab_; }
    [[nodiscard]] std::size_t rowCount() const;
    [[nodiscard]] std::size_t pendingCount() const;
    [[nodiscard]] std::size_t validatedCount() const;
    [[nodiscard]] const ai::incidents::Item* selected() const;
    [[nodiscard]] std::string selectedName() const;
    // Par le nom du dossier ou son chemin, dans l'onglet ouvert puis dans l'autre (qui s'ouvre).
    bool select(const std::string& nameOrFolder);
    // Le texte d'une ligne (les essais, les scripts) : nom, date, alarme, equipement, vue, synthese, certitude, analyse,
    // validation, separes par " | ".
    [[nodiscard]] std::string rowText(std::size_t row) const;

    // ---- les gestes (sans dialogue ; les hotes demandent d'abord) ----
    bool commentSelected(const std::string& text, std::string* why = nullptr);
    bool analyzeSelected(std::string* why = nullptr);
    bool validateSelected(const std::string& note, std::string* why = nullptr);
    bool removeSelected(std::string* why = nullptr);
    // Les gestes de la barre et du menu, avec leurs dialogues s'il y en a (Hosts).
    void askComment();
    void askValidate();
    void askRemove();
    void openSelectedFolder();
    // Verifier les empreintes de l'incident choisi (lecture directe) : les lignes du resultat.
    std::vector<std::string> verifySelected();
    // Ouvrir le maillon `index` de la chaine ; faux et `why` : il n'existe plus (la fiche garde son nom).
    bool revealLink(std::size_t index, std::string* why = nullptr);

    // ---- la fiche ----
    void consult();               // la fiche en haut, le resume deplie
    void showFullAnalysis();      // toutes les sections et la derniere analyse depliees
    void exploreDependencies();   // les dependances depliees, en vue
    [[nodiscard]] std::vector<std::string> sheetSections() const;   // les dix titres de la fiche de diagnostic
    [[nodiscard]] bool sheetSectionOpen(std::size_t index) const;
    void toggleSheetSection(std::size_t index);
    void setCompare(bool on);
    [[nodiscard]] bool comparing() const;
    [[nodiscard]] HmiIncidentSheet& sheet() noexcept { return *sheet_; }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    void say(std::string text, bool warning = false);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    friend class HmiIncidentList;
    friend class HmiIncidentSheet;
    void selectRow(int tab, int row);
    void showSelected();
    void openContextMenu(gfx::Point surface);
    [[nodiscard]] std::string author() const;
    // Une analyse demandee d'ici qui attend encore (l'hote en a une en cours ou en file) ; sinon elle se redemande.
    [[nodiscard]] bool analysisWaiting(const std::string& name) const;

    hmi::DocumentPtr                      doc_;
    IncidentHost*                         host_{nullptr};
    Hosts                                 hosts_;
    HmiToolStrip*                         tools_{nullptr};
    ui::Splitter*                         split_{nullptr};
    ui::TabControl*                       tabs_{nullptr};
    ui::ScrollablePanel*                  scrolls_[2]{nullptr, nullptr};
    HmiIncidentList*                      lists_[2]{nullptr, nullptr};
    ui::ScrollablePanel*                  sheetScroll_{nullptr};
    HmiIncidentSheet*                     sheet_{nullptr};
    ui::StatusBar*                        status_{nullptr};
    ui::PopupMenu*                        menu_{nullptr};
    std::shared_ptr<const IncidentHost::Listing> listing_;   // la derniere liste de l'hote (gardee vivante)
    const std::vector<ai::incidents::Item>* items_[2]{nullptr, nullptr};
    std::uint64_t                         revision_{~std::uint64_t{0}};
    bool                                  validatedTab_{false};
    int                                   row_[2]{-1, -1};
    std::string                           chosen_[2];        // le nom choisi dans chaque onglet (suit les relectures)
    std::set<std::string>                 requested_;        // les analyses demandees d'ici, pas encore arrivees
    std::map<std::string, std::size_t>    analysesSeen_;     // ... et le nombre d'analyses qu'elles avaient
    std::string                           message_;
    core::ConnectionScope                 links_;
};

} // namespace app
