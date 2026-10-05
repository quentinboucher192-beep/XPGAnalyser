// =============================================================================
//  app/RenameDialog.hpp - renommer, en montrant d'abord tout ce qui change
// -----------------------------------------------------------------------------
//  UN MODAL, TROIS ETAGES :
//    * le nouveau nom, verifie en direct : "libre" en vert, "deja pris : ..."
//      ou "nom invalide : ..." en rouge ; le plan se refait 250 ms apres la
//      derniere lettre ;
//    * ce qui change, en arbre (endroit > lignes), par onglets : Tout, API,
//      IHM, Tables - chacun avec son nombre. Une ligne montre le texte, l'ancien
//      nom barre sur fond rouge, le nouveau sur fond vert ; avant la premiere
//      lettre, les endroits ou l'ancien nom est cite (sur fond ambre). En bas,
//      la ligne choisie en entier, avant et apres ;
//    * le pied : "n changements dans m endroits - un seul Ctrl+Z", Annuler,
//      Confirmer (grise tant que le nom est refuse ou inchange).
//  Une variable liee de l'autre cote (IHM <-> API) : une case "Aussi
//  renommer ... X -> Y".
//
//  Entree : Confirmer (quand il est permis) ; Echap : Annuler. Le panneau se
//  redimensionne par son coin bas droit.
//
//  LA MOITIE IHM DU PLAN est ici aussi (makeHmiRenameSide) : elle lit et
//  reecrit le document de l'IHM avec les outils de la bibliotheque IHM
//  (hmi::rewriteNames, hmi::replacePath...), que project/ ne peut pas lier.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../menu/IMenu.hpp"
#include "../project/RenamePlan.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hmi { class Document; }
namespace ui { class InputText; class Button; class Checkbox; class TabControl; }

namespace app {

class RenameDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string id{"dialog.rename"};
        project::rename::Kind kind{project::rename::Kind::Variable};
        std::string           oldName;          // comme demande : "Logigrammes_A.compteur"
        std::string           initial;          // le nouveau nom de depart ("" : l'ancien, tout choisi)
        // Le plan pour un nouveau nom ; `withLinks` : la case des liees cochee.
        std::function<project::rename::Plan(const std::string& newName, bool withLinks)> plan;
        // Le verdict seul, tout de suite a chaque lettre (le plan attend 250 ms).
        std::function<std::string(const std::string& newName, project::rename::Verdict* verdict)> check;
        // Faire le plan (une commande) ; faux (et why) : refuse, le dialogue reste.
        std::function<bool(const project::rename::Plan&, std::string* why)> apply;
        std::string           note;             // en bas, en gris ("" : rien)
        float                 width{900.f}, height{620.f};
    };

    explicit RenameDialog(Spec spec);
    ~RenameDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- pour les scripts et les essais ----------------------------------------
    // Taper le nouveau nom (le plan se refait tout de suite, sans attendre).
    void typeName(const std::string& text);
    // Un onglet par le debut de son titre (sans casse) : "Tout", "API", "IHM", "Tables".
    bool showTab(std::string_view title);
    // Confirmer comme le bouton ; faux (et why) : il est grise, ou le plan est refuse.
    bool confirm(std::string* why = nullptr);
    void cancel();
    // La case des variables liees (faux : il n'y en a pas).
    bool setWithLinks(bool on);
    // Choisir la premiere ligne de l'onglet dont le texte contient `text` (le detail en bas).
    bool selectRow(std::string_view text);
    [[nodiscard]] const project::rename::Plan& plan() const noexcept { return plan_; }
    [[nodiscard]] std::string newName() const;
    [[nodiscard]] bool canConfirm() const noexcept;

protected:
    core::Status buildUi() override;

private:
    class Body;
    class Tree;
    class Detail;
    void onEnter() override;
    void recompute();
    void quickCheck();
    void refreshTabs();
    void refreshTexts();
    void syncDetail();
    void finish(bool ok);
    [[nodiscard]] Tree* currentTree() const;

    Spec                          spec_;
    project::rename::Plan         plan_;
    Body*                         body_{nullptr};
    ui::InputText*                name_{nullptr};
    ui::Checkbox*                 links_{nullptr};
    ui::TabControl*               tabs_{nullptr};
    Detail*                       detail_{nullptr};
    ui::Button*                   ok_{nullptr};
    std::vector<Tree*>            trees_;
    double                        now_{0.0};
    double                        dueAt_{-1.0};    // le plan a refaire a cette heure (-1 : a jour)
    project::rename::Verdict      quick_{project::rename::Verdict::Unchanged};   // le verdict de la derniere lettre
    std::string                   quickWhy_;
    bool                          done_{false};
    bool                          focused_{false};
    std::string                   error_;          // le refus de la derniere confirmation
    core::ConnectionScope         wires_;
};

// La moitie IHM du plan, sur ce document (nul : pas d'IHM - rien a verifier ni
// a reecrire de ce cote).
[[nodiscard]] std::unique_ptr<project::rename::HmiSide> makeHmiRenameSide(std::shared_ptr<hmi::Document> doc);

// ---------------------------------------------------------------------------
//  DEMANDER UN RENOMMAGE DEPUIS UN VOLET. Les volets (types, blocs, unites,
//  variables, tables, variables IHM) ne connaissent pas l'ecran d'analyse ;
//  celui-ci pose ce crochet (screens/RenameWorkspace.cpp). Vrai : le dialogue
//  s'ouvre (ou la barre d'etat dit pourquoi il ne s'ouvre pas) ; faux :
//  personne pour l'ouvrir (un essai sans ecran, un autre ecran au-dessus) - le
//  volet fait alors comme avant. `newName` : deja tape (la case Nom d'une
//  grille de proprietes) ; vide : l'ancien, tout choisi. En ligne : les essais
//  qui compilent les volets sans l'ecran n'ont rien a lier de plus.
// ---------------------------------------------------------------------------
using RenameRequestHook = std::function<bool(const std::string& kind, const std::string& name, const std::string& newName)>;
inline RenameRequestHook& renameRequestHook() {
    static RenameRequestHook hook;
    return hook;
}
inline bool requestRename(const std::string& kind, const std::string& name, const std::string& newName = {}) {
    const auto& hook = renameRequestHook();
    return hook && hook(kind, name, newName);
}

} // namespace app
