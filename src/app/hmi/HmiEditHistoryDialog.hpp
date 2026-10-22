// =============================================================================
//  app/hmi/HmiEditHistoryDialog.hpp - 1.12.26 : LA FENETRE HISTORIQUE D'UN OBJET
// -----------------------------------------------------------------------------
//  Le clic droit sur un objet de la vue (« Historique des modifications... »),
//  sur plusieurs (« Historique des 3 objets... »), sur le fond de la vue
//  (« Historique de la vue... », les objets supprimes compris) :
//
//    +-- Historique - RG_TE3212_1 (Registre) --------------------------------------+
//    | [Tout] [Objet] [Position et taille] [Apparence] [Liaisons...] [Actions]     |
//    |                                       PC [Tous les PC v]  [Rechercher...]  |
//    +--------------------------+--------------------------------------------------+
//    | Non enregistre           | 22/10/2026 14:32 - PC-XFAB-B32 - mmartin         |
//    |   sera signe PC-.. - qb  | Modification - Apparence, Liaisons et expressions |
//    | 22/10/2026               |  Propriete         Avant      Apres               |
//    |   14:32  PC-XFAB-B32     |  Fond              #1B2028    #EEF1F5  [Revenir]  |
//    |   10:05  PC-SEATI-QB12   |  Fin de course O   (vide)     =FC_O    [Revenir]  |
//    +--------------------------+--------------------------------------------------+
//    | [Copier] [Exporter en CSV...]         [Revenir a cette version] [Fermer]   |
//    +-----------------------------------------------------------------------------+
//
//  A gauche, les enregistrements (du plus recent au plus ancien, par jour) et, en
//  tete, ce qui n'est pas encore enregistre - signe de ce PC au prochain
//  Enregistrer. A droite, ce qu'un enregistrement a change, propriete par
//  propriete. Revenir (une valeur, ou toute la version) est une modification
//  ordinaire : confirmee ici, annulee par Ctrl+Z, notee au prochain Enregistrer
//  (« retour a la version du ... »). Un objet supprime se restaure.
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiEditHistory.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

class HmiEditHistoryDialog final : public menu::WidgetMenu {
public:
    // Une modification de la vue, en une commande (Ctrl+Z) ; faux : refusee.
    using Edit = std::function<bool(const std::string& label, const std::function<bool(hmi::View&)>& change)>;
    using Label = std::function<std::string(hmi::Kind, std::string_view key)>;
    struct Spec {
        std::string          id{"dialog.hmiHistory"};
        hmi::DocumentPtr     doc;
        hmi::Id              view{hmi::kNoId};
        std::vector<hmi::Id> objects;      // vide : toute la vue (les supprimes compris)
        Edit                 edit;
        Label                label;        // le libelle d'une propriete (celui de l'inspecteur)
    };

    explicit HmiEditHistoryDialog(Spec spec);
    ~HmiEditHistoryDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que montre la fenetre (et ce que ferait la souris : scripts, essais) ----
    [[nodiscard]] std::size_t rowCount() const noexcept { return shown_.size(); }        // les enregistrements montres
    [[nodiscard]] const hmi::edits::Entry* entry(std::size_t i) const noexcept;         // 0 : le plus recent
    [[nodiscard]] bool isPending(std::size_t i) const noexcept;                          // « Non enregistre »
    [[nodiscard]] int  selected() const noexcept { return selected_; }
    void select(std::size_t i);
    void setSortFilter(std::optional<hmi::edits::Sort> s);
    [[nodiscard]] std::optional<hmi::edits::Sort> sortFilter() const noexcept { return sort_; }
    void setPcFilter(const std::string& pc);   // vide : tous
    void setSearch(const std::string& text);
    [[nodiscard]] std::vector<std::string> pcs() const;                                // les PC de l'historique
    // Le libelle d'un changement (la case Propriete), et ce que montrent Avant / Apres.
    [[nodiscard]] std::string changeLabel(const hmi::edits::Entry&, const hmi::edits::Change&) const;
    [[nodiscard]] std::string valueText(const hmi::edits::Entry&, const hmi::edits::Change&, bool before) const;
    // Une ligne par enregistrement montre : « 22/10/2026 14:32 · PC-XFAB-B32 · mmartin · 2 changements ».
    [[nodiscard]] std::string rowText(std::size_t i) const;
    [[nodiscard]] std::string selectedText() const;     // ce que Copier copie
    [[nodiscard]] std::string csv() const;              // ce que Exporter ecrit (les enregistrements montres)
    bool exportCsv(const std::string& path, std::string* why = nullptr) const;
    void copy();

    // ---- revenir (une confirmation d'abord, sauf `now`) ----------------------------
    bool revertChange(std::size_t change, bool now = false);   // la valeur Avant de ce changement
    bool revertEntry(bool now = false);                        // l'objet tel qu'apres cet enregistrement
    bool restoreDeleted(bool now = false);                     // un objet supprime, remis dans la vue
    [[nodiscard]] bool confirming() const noexcept { return confirm_.kind != Confirm::None; }
    [[nodiscard]] const std::string& confirmText() const noexcept { return confirm_.text; }
    bool confirmYes();
    void confirmNo();
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    void reload();   // relire l'historique (apres un Enregistrer, un Revenir)
    void dismiss() { finish(); }   // Fermer (scripts, essais)

protected:
    core::Status buildUi() override;

private:
    class Body;
    enum class Confirm : std::uint8_t { None, Change, Entry, Restore };
    struct Pending {
        Confirm     kind{Confirm::None};
        std::size_t change{0};
        std::string text;
    };
    void refresh();
    void finish();
    void say(std::string text, bool bad = false);
    [[nodiscard]] bool wanted(const hmi::edits::Entry&) const;
    [[nodiscard]] bool matches(const hmi::edits::Entry&) const;
    [[nodiscard]] const hmi::Object* current(hmi::Id) const;
    [[nodiscard]] std::string objectName(hmi::Id) const;
    [[nodiscard]] std::string kindName(const hmi::edits::Entry&) const;
    bool doRevertChange(std::size_t change);
    bool doRevertEntry();
    bool doRestore();

    Spec                            spec_;
    std::vector<hmi::edits::Entry>  all_;       // l'historique, du plus recent au plus ancien ; en tete, le non enregistre
    std::size_t                     pendingCount_{0};
    std::vector<std::size_t>        shown_;     // les indices montres de all_
    int                             selected_{-1};
    std::optional<hmi::edits::Sort> sort_;
    std::string                     pc_, search_;
    Pending                         confirm_;
    std::string                     message_;
    bool                            messageBad_{false};
    bool                            done_{false};
    Body*                           body_{nullptr};
    ui::InputText*                  searchField_{nullptr};
    ui::DropDown*                   pcBox_{nullptr};
    ui::Button*                     copy_{nullptr};
    ui::Button*                     export_{nullptr};
    ui::Button*                     revert_{nullptr};
    ui::Button*                     close_{nullptr};
    ui::Button*                     yes_{nullptr};
    ui::Button*                     no_{nullptr};
    std::shared_ptr<int>            alive_ = std::make_shared<int>(0);
    core::ConnectionScope           links_;
};

} // namespace app
