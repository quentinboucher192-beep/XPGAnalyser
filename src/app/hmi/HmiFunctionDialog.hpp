// =============================================================================
//  app/hmi/HmiFunctionDialog.hpp - 1.12.24 : "Nouvelle fonction"
// -----------------------------------------------------------------------------
//  Une fonction IHM du projet, ou d'une vue, d'une popup, d'un symbole. Comme le
//  createur de variable : LA CASE RETOUR OUVRE LE SELECTEUR DE TYPES (un seul
//  endroit qui cherche, filtre et compose - MAP[STRING] OF VECTOR OF T_Cuve,
//  les generiques T et K d'un symbole), plus de liste a rallonge ni de Forme /
//  Bornes / Type ecrit.
//
//    +-- Nouvelle fonction de Vue_Accueil ----------------------------------+
//    |  Une fonction de la vue : par son nom dans la vue ... ; publique,    |
//    |  par Vue_Accueil.Nom(...) ailleurs.                                  |
//    |  Nom          [Fonction________________________________]            |
//    |               lettres, chiffres, _                                   |
//    |  Retour       [Aucun                         Choisir un type...]     |
//    |               une procedure : appelee seule sur sa ligne             |
//    |  Portee       (o) Publique   ( ) Privee                              |
//    |  Description  [________________________________________]            |
//    |  +-- Elle s'ecrira ---------------------------------------------+   |
//    |  | FUNCTION Fonction                                            |   |
//    |  | dans la vue : Fonction()  -  ailleurs : Vue_Accueil.Fonction()|   |
//    |  +--------------------------------------------------------------+   |
//    |  Ses parametres, locales et constantes : dans ses onglets.           |
//    |                                             [Annuler] [Creer]        |
//    +----------------------------------------------------------------------+
//
//  Portee : une vue ou une popup (publique : Vue.Nom(...) ailleurs) ; Virtuelle :
//  un symbole (une instance peut la redefinir). Un nom deja pris est une
//  surcharge : dit sous le nom, il ne bloque pas. La reponse (DialogResult::
//  payload, parse) : nom, retour (vide : aucun), description, portee, virtuelle ;
//  l'appelant la pose en une commande (un Ctrl+Z).
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>

namespace app {

class HmiNewFunctionDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string      id{"dialog.hmiNewFunction"};
        hmi::DocumentPtr doc;
        hmi::Id          owner{hmi::kNoId};   // la vue, la popup ou le symbole ; kNoId : une fonction IHM du projet
        std::string      name;                // le nom propose (vide : Fonction, Fonction2...)
        std::string      returnType;          // vide : Aucun
    };
    struct Answer {
        std::string name, returnType, description;   // returnType vide : une procedure
        bool        pub{true};                        // une vue ou une popup : publique
        bool        isVirtual{false};                 // un symbole : virtuelle
    };
    enum class Owner : std::uint8_t { Project, View, Popup, Symbol };

    explicit HmiNewFunctionDialog(Spec spec);
    ~HmiNewFunctionDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) --------------------------
    void setName(const std::string& name);
    void setDescription(const std::string& text);
    void setReturnType(const std::string& type);    // comme choisi dans le selecteur ("Aucun" ou vide : aucun)
    void setPublic(bool on);
    void setVirtual(bool on);
    void chooseReturn();                            // ouvre le selecteur de types
    [[nodiscard]] Owner owner() const noexcept { return owner_; }
    [[nodiscard]] const Answer& answer() const noexcept { return answer_; }
    [[nodiscard]] std::string nameProblem() const;  // vide : le nom va
    [[nodiscard]] std::string nameNote() const;     // sous le nom : la regle, ou la surcharge
    [[nodiscard]] std::string returnNote() const;   // sous le retour : ce qu'elle rend
    [[nodiscard]] std::string declaration() const;  // FUNCTION Fonction : REAL
    [[nodiscard]] std::string calls() const;        // dans la vue : Fonction()  -  ailleurs : Vue.Fonction()
    [[nodiscard]] std::string explanation() const;
    [[nodiscard]] bool canConfirm() const { return nameProblem().empty(); }
    bool confirm();
    void cancel();

    [[nodiscard]] static std::string encode(const Answer&);
    static bool parse(const std::string& payload, Answer& out);

protected:
    core::Status buildUi() override;

private:
    class Body;
    class TypeField;
    void refresh();
    void finish(bool ok);
    [[nodiscard]] int sameName() const;             // les fonctions deja nommees ainsi (une surcharge)

    Spec        spec_;
    Owner       owner_{Owner::Project};
    std::string ownerName_;
    Answer      answer_;
    bool        done_{false};
    bool        syncing_{false};
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    Body*            body_{nullptr};
    ui::InputText*   nameField_{nullptr};
    ui::InputText*   descField_{nullptr};
    TypeField*       returnField_{nullptr};
    ui::RadioButton* publicRadio_{nullptr};
    ui::RadioButton* privateRadio_{nullptr};
    ui::Checkbox*    virtualBox_{nullptr};
    ui::Button*      ok_{nullptr};
    std::shared_ptr<ui::RadioGroup> scope_;
    core::ConnectionScope links_;
};

} // namespace app
