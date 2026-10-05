// =============================================================================
//  app/hmi/HmiTemplateGallery.hpp - Nouvelle vue : choisir un modele (lot 20)
// -----------------------------------------------------------------------------
//  LE DIALOGUE DE "NOUVELLE VUE", en galerie (maquette M17) :
//
//    +-----------------+------------------------------------+-----------------+
//    | Proposes par    | [Chercher un modele]               |   [ apercu ]    |
//    |  l'application 6| [vignette] [vignette] [vignette]   | Nom  (categorie)|
//    | Mes modeles    4| [vignette]                         | description     |
//    | Modeles du      |                                    | Emporte : ...   |
//    |  projet        2| PROPOSES PAR L'APPLICATION          | Variables : ... |
//    |                 | [vignette] [vignette] [vignette]   | Nom  [      ]   |
//    | Importer...     |                                    | Role [Vue v]    |
//    | Gerer...        |                                    | Annuler  Creer  |
//    +-----------------+------------------------------------+-----------------+
//
//  Trois sources : les modeles de l'application (vide, synoptique, tableau de
//  bord...), MES MODELES (ma bibliotheque : tous mes projets) et LES MODELES
//  DU PROJET (ils voyagent avec lui). Chaque vignette est la vue du modele,
//  dessinee. Chercher filtre les trois. L'ecran fournit les entrees (il sait
//  ou elles vivent) et fait la vue ; le dialogue ne fait que choisir.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiTemplateGallery final : public menu::WidgetMenu {
public:
    struct Entry {
        std::string key;            // "app:synoptique", "lib:<fichier>", "prj:<id>"
        int         source{0};      // 0 : l'application, 1 : mes modeles, 2 : le projet
        std::string name, category, description;
        std::string origin;         // "Enregistre le 24/09 depuis Armoire_Gaz"
        std::string contents;       // "Emporte : 1 symbole, 2 images, 1 style"
        std::string variables;      // "Variables : 12 - toutes presentes"
        bool        variablesOk{true};
        std::string role{"vue"};    // le role propose en le choisissant
        // 1.10.2 : les roles pour lesquels il est propose (vide : tous, comme Vide).
        // 1.10.3 : la galerie ne montre que ceux du role choisi (plus d'AUTRES MODELES).
        std::vector<std::string> suits;
        [[nodiscard]] bool fits(const std::string& r) const {
            return suits.empty() || std::find(suits.begin(), suits.end(), r) != suits.end();
        }
        int         width{0}, height{0};     // la taille de la vue creee (0 : celle du projet)
        std::string viewDescription;         // la description de la vue creee
        // La vignette : la vue du modele dans son petit projet.
        std::shared_ptr<const hmi::Project> preview;
        hmi::Id     previewView{hmi::kNoId};
    };
    struct Spec {
        std::vector<Entry> entries;
        std::string        name;           // le nom propose
        std::string        role{"vue"};
        int                source{0};      // la categorie ouverte
        std::string        selected;       // la cle choisie
        // Un nom deja pris (le bouton reste gris, et le dit).
        std::function<bool(const std::string&)> nameTaken;
        // 1.10.2 : la liste Role met tout a jour. Le nom propose pour un role
        // (Vue_3, Popup_2...) et les modeles de l'application refaits pour lui
        // (leur taille, leur vignette) ; vides, le role ne change que le titre.
        std::function<std::string(const std::string& role)>        nameFor;
        std::function<std::vector<Entry>(const std::string& role)> appEntriesFor;
    };
    // Ce que le dialogue rend (DialogResult::payload, voir parse) : l'entree
    // choisie, le nom, le role ; ou une demande (importer, gerer).
    struct Answer {
        std::string key, name, role;
        int         width{0}, height{0};
        std::string description;
        std::string request;        // "" : creer ; "importer", "gerer"
    };

    explicit HmiTemplateGallery(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    [[nodiscard]] static Answer parse(const std::string& payload);

    // ---- pour les scripts et les tests ----
    void chooseSource(int source);
    void chooseRole(const std::string& role);           // la liste Role (1.10.2)
    [[nodiscard]] const std::string& role() const noexcept { return spec_.role; }
    // Les champs, pour les essais : le nom, la taille, la description.
    [[nodiscard]] std::string fieldText(const std::string& which) const;   // "nom", "largeur", "hauteur", "description"
    void typeField(const std::string& which, const std::string& text);    // comme une saisie a la main
    [[nodiscard]] int  templateListIndex() const;       // la ligne choisie de la liste Modele
    [[nodiscard]] const std::vector<std::string>& templateListKeys() const noexcept { return templateKeys_; }
    [[nodiscard]] int  scrollOffset() const;            // le defilement des vignettes
    bool choose(const std::string& nameOrKey);          // par le nom de la vignette, ou sa cle
    void setSearch(const std::string& text);
    [[nodiscard]] const std::string& selectedKey() const noexcept { return spec_.selected; }
    [[nodiscard]] int  source() const noexcept { return spec_.source; }
    // Les vignettes montrees (celles de la categorie, puis celles de l'application), du role choisi.
    [[nodiscard]] std::vector<const Entry*> shown() const;
    [[nodiscard]] gfx::Rect cardRect(const std::string& nameOrKey) const;
    [[nodiscard]] gfx::Rect sourceRect(int source) const;
    void confirm();                                      // Creer la vue

protected:
    core::Status buildUi() override;

private:
    friend class GalleryBody;
    [[nodiscard]] const Entry* find(const std::string& nameOrKey) const;
    // 0 : en tete ; 1 : PROPOSES PAR L'APPLICATION (1.10.3 : plus d'AUTRES MODELES).
    [[nodiscard]] int section(const Entry& e) const;
    void applyRole(const std::string& role, bool fromModel);
    void applyModel(const std::string& key, bool forceSize);
    void rebuildTemplateList();
    void reveal(const std::string& key);
    [[nodiscard]] std::string payload(const std::string& request) const;
    void finish(bool ok, const std::string& request = {});
    void sync();

    Spec                 spec_;
    std::string          search_;
    // LES CHAMPS, DANS CET ORDRE (les sessions d'avant les remplissent par leur
    // rang : champ 1 le nom, 2 et 3 la taille, 4 la description ; liste 1 le
    // role, liste 2 le modele) ; Chercher en dernier.
    ui::InputText*       nameBox_{nullptr};
    ui::DropDown*        roleBox_{nullptr};
    ui::DropDown*        templateBox_{nullptr};
    ui::InputText*       widthBox_{nullptr};
    ui::InputText*       heightBox_{nullptr};
    ui::InputText*       descBox_{nullptr};
    ui::InputText*       searchBox_{nullptr};
    std::vector<std::string> templateKeys_;     // la liste Modele : une cle par ligne
    bool                 syncing_{false};
    // Ce que la galerie a rempli elle-meme : une valeur differente a ete tapee.
    std::string          autoName_, autoWidth_, autoHeight_, autoDesc_;
    ui::Button*          ok_{nullptr};
    ui::Widget*          body_{nullptr};
    bool                 done_{false};
    core::ConnectionScope links_;
};

} // namespace app
