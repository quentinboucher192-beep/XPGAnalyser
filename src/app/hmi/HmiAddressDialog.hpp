// =============================================================================
//  app/hmi/HmiAddressDialog.hpp - « Modifier l'adresse dans toutes les cases... » (1.12.18)
// -----------------------------------------------------------------------------
//  Planche 12 de la maquette du clic droit. Ouverte sur ce qu'on a vise (un
//  membre - EXTRACTEURS[0].Retour - ou la ligne d'une case, du tableau) :
//    Ce qui change   le membre Retour de chaque case, ou les cases entieres ;
//    Comment         par une formule de i (le prefixe %MW, 1000 + 10 * i, le bit
//                    d'un BOOL range dans un mot), ou a la suite d'une adresse ;
//    Apercu          Case | Adresse | Avant | une autre variable l'a aussi.
//  Appliquer reste gris et dit pourquoi tant que l'apercu refuse (un nom
//  inconnu, deux cases a la meme place, hors des zones). La reponse
//  (DialogResult::payload, parse) : le motif et la formule ; l'appelant les pose
//  en une commande (HmiVariablesPane::applyAddressFormula : un Ctrl+Z).
//  Le moteur est hmi::addr (HmiAddressPlan.hpp), sans ecran.
// =============================================================================
#pragma once

#include "../../hmi/HmiAddressPlan.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui { class ScrollablePanel; }

namespace app {

class HmiAddressDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string      id{"dialog.hmiAddresses"};
        hmi::DocumentPtr doc;
        hmi::Id          variable{hmi::kNoId};
        std::string      member;   // le membre vise ("[0].Retour") ; vide ou une case : les cases entieres
    };

    explicit HmiAddressDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return "Modifier l'adresse dans toutes les cases"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) --------------------------
    [[nodiscard]] bool canChooseMember() const noexcept { return !memberPattern_.empty() && memberPattern_ != casePattern_; }
    void setWhole(bool wholeCases);              // Ce qui change : les cases entieres (sinon le membre)
    [[nodiscard]] bool whole() const noexcept { return whole_; }
    void setFollowing(bool following);           // Comment : a la suite d'une adresse (sinon une formule de i)
    [[nodiscard]] bool following() const noexcept { return following_; }
    void setPrefix(std::string prefix);          // "%MW", "4x", "" (un nombre seul : 40001)
    void setExpression(std::string expression);  // "1000 + 10 * i"
    void setBit(std::string bit);                // un BOOL dans un mot : "0", "i MOD 16"
    void setStart(std::string start);            // a la suite : "%MW1000"
    [[nodiscard]] const std::string& prefix() const noexcept { return prefix_; }
    [[nodiscard]] const std::string& expression() const noexcept { return expression_; }
    [[nodiscard]] const std::string& bit() const noexcept { return bit_; }
    [[nodiscard]] const std::string& start() const noexcept { return start_; }
    [[nodiscard]] const std::string& pattern() const noexcept { return whole_ ? casePattern_ : memberPattern_; }
    [[nodiscard]] const std::string& formula() const noexcept { return formula_; }
    [[nodiscard]] const hmi::addr::Plan&  plan() const noexcept { return plan_; }
    [[nodiscard]] const hmi::addr::Shape& shape() const noexcept { return shape_; }
    [[nodiscard]] std::string confirmLabel() const;   // "Appliquer aux 10 cases"
    [[nodiscard]] std::string message() const;        // ce que dit le bas de la fenetre (refus, adresses prises)
    [[nodiscard]] bool canConfirm() const { return plan_.ok(); }
    bool confirm();
    void cancel();

    // La reponse (DialogResult::payload) : le motif, puis la formule.
    [[nodiscard]] static std::string encode(const std::string& pattern, const std::string& formula);
    static bool parse(const std::string& payload, std::string& pattern, std::string& formula);

protected:
    core::Status buildUi() override;

private:
    class Body;
    class Preview;
    void prefill();     // la formule d'aujourd'hui, le depart (le motif a change)
    void recompute();   // la formule, l'apercu
    void refresh();     // les champs, le bouton
    void finish(bool ok);

    Spec                     spec_;
    hmi::Variable            var_;
    std::string              memberPattern_, casePattern_;
    bool                     whole_{false}, following_{false};
    std::string              prefix_, expression_, bit_, start_, formula_, followWhy_;
    hmi::addr::Plan          plan_;
    hmi::addr::Shape         shape_, memberShape_, caseShape_;
    std::vector<std::string> prefixes_;   // la liste du prefixe
    Body*                    body_{nullptr};
    Preview*                 preview_{nullptr};
    ui::ScrollablePanel*     previewPanel_{nullptr};
    ui::RadioButton*         memberRadio_{nullptr};
    ui::RadioButton*         wholeRadio_{nullptr};
    ui::RadioButton*         formulaRadio_{nullptr};
    ui::RadioButton*         followRadio_{nullptr};
    std::shared_ptr<ui::RadioGroup> whatGroup_, howGroup_;
    ui::DropDown*            prefixBox_{nullptr};
    ui::InputText*           expressionField_{nullptr};
    ui::InputText*           bitField_{nullptr};
    ui::InputText*           startField_{nullptr};
    ui::Button*              ok_{nullptr};
    bool                     syncing_{false};
    bool                     done_{false};
    core::ConnectionScope    links_;
};

} // namespace app
