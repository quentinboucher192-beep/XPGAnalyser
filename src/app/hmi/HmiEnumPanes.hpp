// =============================================================================
//  app/hmi/HmiEnumPanes.hpp - l'interface du type enumeration IHM (1.10,
//  decision 15, chantier U ; la maquette : scene 14)
// -----------------------------------------------------------------------------
//  LES VALEURS D'UNE ENUMERATION (HmiEnumValuesPane), a la place de la table des
//  membres quand le type choisi dans Types IHM est une enumeration :
//
//    +-- Ajouter  Dupliquer  Supprimer  Monter  Descendre | Coller | toString --+
//    | # | Nom      | Valeur | Texte affiche | Description   |  Declaration    |
//    | 1 | Arret    |      0 | A l'arret     | ...           |  [ST][C][C++]   |
//    | 2 | Auto     |      1 | Automatique   |               |  TYPE T_MODE :( |
//    |   ! valeur en double : 1 est deja celle de Defaut      |    Arret := 0,  |
//    +-- Valeurs valides : l'ordre est celui de FOR EACH v IN T_MODE ----------+
//
//  Chaque case s'edite sur place (double-clic, F2, Entree, Tab, Echap) ; les
//  controles de hmi::enumIssues en direct (la ligne en rouge, sa raison
//  dessous) ; chaque changement est UNE commande (hmi::changeProject, Ctrl+Z).
//  Renommer une valeur employee (T_MODE#Manu dans un script) ou la supprimer
//  ouvre une fenetre qui montre ses emplois : Renommer partout (une commande,
//  hmi::renameEnumValue) / La valeur seulement / Supprimer quand meme.
//  Coller depuis Excel (Nom, Valeur, Texte affiche, Description ; une ligne de
//  titres reconnue) : un apercu - ajoutee, remplacee (meme nom), refusee et
//  pourquoi - puis Appliquer (une commande).
//
//  LA CREATION (HmiEnumCreatePanel) : la fenetre << Nouvelle enumeration >> de
//  << Nouveau type v > Enumeration... >> - le nom (controle en direct), les
//  premieres valeurs (une par ligne : Nom [= valeur] ['texte affiche']),
//  l'apercu de la declaration en ST | C | C++, les deux conversions annoncees.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// ---- les aides (sans interface : les essais s'en servent) ----------------------
// Un emploi du litteral T_MODE#Manu dans un texte ST du projet.
struct EnumUse {
    std::string where;      // "Script Gestion_Mode", "toString (op\xC3\xA9rateur)", "Vue_A : action"...
    int         line{0};    // 1 = la premiere
    std::string text;       // la ligne, sans les espaces autour
};
[[nodiscard]] std::vector<EnumUse> enumValueUses(const hmi::Project&, std::string_view type, std::string_view value);
// Les textes (where) distincts de ces emplois : les << scripts >> qui l'emploient.
[[nodiscard]] std::size_t enumUseScripts(const std::vector<EnumUse>&);

// La declaration de l'enumeration : lang 0 ST (TYPE ... END_TYPE), 1 C (typedef
// enum + to_string / from_string), 2 C++ (enum class + to_string / from_string).
[[nodiscard]] std::string enumDeclaration(std::string_view name, const std::vector<hmi::HmiEnumValue>&, int lang);

// Les premieres valeurs tapees, une par ligne : Nom [= valeur] ['texte affiche']
// (texte entre ' ou " ; sans valeur : la suivante de la plus grande).
[[nodiscard]] std::vector<hmi::HmiEnumValue> parseEnumLines(std::string_view text);

// La raison d'une valeur en defaut (le premier constat de hmi::enumIssues pour
// cette ligne), ou vide.
[[nodiscard]] std::string enumRowIssue(const hmi::HmiType&, std::size_t index);

// Le plan d'un collage d'Excel dans une enumeration.
struct EnumPaste {
    struct Line {
        int         kind{0};            // 1 ajoutee, 2 remplacee, 3 refusee
        std::string label, detail;      // "Manu = 2", "avant : 2 << Manuel >> -> << Manuel (local) >>"
    };
    std::vector<hmi::HmiEnumValue> result;   // les valeurs apres le collage
    std::vector<Line> lines;
    int  added{0}, replaced{0}, refused{0};
    bool titles{false};                      // la premiere ligne etait des titres
    [[nodiscard]] std::string summary() const;   // "2 ajoutee(s), 1 remplacee(s), 2 refusee(s)"
};
[[nodiscard]] EnumPaste planEnumPaste(const hmi::HmiType&, std::string_view text);

// ---- la fenetre (posee par l'hote sur tout le volet des types) : renommer,
//      supprimer, coller ------------------------------------------------------------
class HmiEnumDialog final : public ui::Widget {
public:
    struct Line {
        std::string text;
        int         tone{0};            // 0 texte, 1 ajoutee (vert), 2 remplacee (bleu), 3 refusee / erreur, 4 gris, 5 titre
        bool        mono{false};
    };
    explicit HmiEnumDialog(std::string id);
    // Le titre, les lignes, les boutons (le dernier est le principal), et un champ
    // de texte a gauche (le tableau colle) si `withText`.
    void setup(std::string title, std::vector<Line> lines, std::vector<std::string> buttons, bool withText, std::string text = {},
               bool danger = false);
    void setLines(std::vector<Line> lines);
    void setNote(std::string note);
    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return lines_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] ui::Button* button(std::size_t i) const noexcept { return i < 3 ? buttons_[i] : nullptr; }
    [[nodiscard]] ui::MultiLineText& textField() noexcept { return *text_; }
    [[nodiscard]] gfx::Rect boxRect() const;
    std::function<void(int)>                onButton;   // le rang du bouton
    std::function<void(const std::string&)> onText;     // le texte colle a change
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    std::string         title_, note_;
    std::vector<Line>   lines_;
    ui::Button*         buttons_[3]{};
    std::size_t         count_{0};
    ui::MultiLineText*  text_{nullptr};
    bool                withText_{false};
    core::ConnectionScope links_;
};

// ---- les valeurs d'une enumeration ------------------------------------------------
class HmiEnumValuesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiEnumValuesPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiEnumValuesPane() override;

    void setType(hmi::Id);                       // l'enumeration montree
    [[nodiscard]] hmi::Id type() const noexcept { return type_; }
    void refresh();

    // ---- les actions : une commande chacune (Ctrl+Z) ; faux si rien n'est fait ----
    bool addValue();                             // apres la ligne choisie : le nombre suivant, le nom a taper
    bool duplicateValue();
    bool removeValue();                          // employee : la fenetre previent (faux)
    bool moveValue(int delta);                   // l'ordre de FOR EACH
    // Une case : 0 Nom, 1 Valeur, 2 Texte affiche, 3 Description. Le nom d'une
    // valeur employee ouvre la fenetre << Renommer partout ? >> (vrai).
    bool setCell(std::size_t index, std::size_t col, const std::string& text);
    bool renameValue(std::size_t index, const std::string& to, bool everywhere);
    bool removeValueAnyway(std::size_t index);
    bool rewriteConversions();                   // toString et fromString d'apres les valeurs
    bool openPaste(std::string_view text);       // l'apercu (faux : rien a coller)
    bool applyPaste();

    // ---- la fenetre ----
    enum class Dialog : std::uint8_t { None, Rename, Delete, Paste };
    [[nodiscard]] Dialog dialog() const noexcept { return dialogKind_; }
    [[nodiscard]] const std::vector<EnumUse>& dialogUses() const noexcept { return uses_; }
    [[nodiscard]] const EnumPaste* pendingPaste() const noexcept { return dialogKind_ == Dialog::Paste ? &paste_ : nullptr; }
    [[nodiscard]] HmiEnumDialog* dialogWidget() const noexcept { return dialog_; }
    // L'hote (Types IHM) pose la fenetre sur tout son volet ; sans hote, le volet
    // en cree une a lui.
    void setDialogWidget(HmiEnumDialog* d);
    bool confirmRename(bool everywhere);         // Renommer partout / La valeur seulement
    bool confirmDelete();                        // Supprimer quand meme
    void closeDialog();

    [[nodiscard]] int  selectedValue() const noexcept { return row_; }
    void selectValue(int index);
    void setDeclarationLanguage(int lang);       // 0 ST, 1 C, 2 C++
    [[nodiscard]] int declarationLanguage() const noexcept { return lang_; }
    [[nodiscard]] std::string declarationText() const;
    [[nodiscard]] HmiToolStrip&  tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView& grid() noexcept { return *grid_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    [[nodiscard]] std::string noteText() const;  // sous la grille : valide, ou n lignes en defaut
    // La ligne de VUE de la valeur `index` (les raisons sont des lignes a part).
    [[nodiscard]] int viewRowOf(int index) const;

    const core::SignalPtr<const std::string&> message = core::Signal<const std::string&>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // Suppr : supprimer la valeur choisie

private:
    [[nodiscard]] const hmi::HmiType* current() const;
    bool change(std::string label, const std::function<bool(hmi::HmiType&)>& edit);
    void say(std::string text, bool warning = false);
    void showDialog();

    hmi::DocumentPtr doc_;
    Apply            apply_;
    hmi::Id          type_{hmi::kNoId};
    HmiToolStrip*    tools_{nullptr};
    ui::TableView*   grid_{nullptr};
    ui::Button*      langs_[3]{};
    HmiEnumDialog*   dialog_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<int> viewToValue_;               // la valeur de chaque ligne de vue ; -1 - k : la raison de la valeur k
    int              row_{-1};
    int              lang_{0};
    std::string      message_;
    bool             syncing_{false};
    // la fenetre en cours
    Dialog           dialogKind_{Dialog::None};
    std::vector<EnumUse> uses_;
    std::size_t      dialogIndex_{0};
    std::string      dialogOld_, dialogNew_;
    EnumPaste        paste_;
    std::string      pasteText_;
    core::ConnectionScope links_;
};

// ---- la fenetre << Nouvelle enumeration >> ---------------------------------------
class HmiEnumCreatePanel final : public ui::Widget {
public:
    explicit HmiEnumCreatePanel(std::string id);
    // Ouvre la fenetre (prereglee : T_VANNE, quatre valeurs d'exemple) ; `taken`
    // dit si un nom de type est deja pris (structure ou enumeration).
    void open(const hmi::Project&);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void setName(const std::string&);
    void setValuesText(const std::string&);
    void setLanguage(int lang);
    // Le controle du nom (vide : libre) et des valeurs (vide : valides).
    [[nodiscard]] std::string nameIssue() const;
    [[nodiscard]] std::string valuesIssue() const;
    [[nodiscard]] std::vector<hmi::HmiEnumValue> values() const;
    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::string preview() const;
    [[nodiscard]] bool ready() const { return nameIssue().empty() && valuesIssue().empty(); }
    [[nodiscard]] ui::InputText&     nameField() noexcept { return *name_; }
    [[nodiscard]] ui::MultiLineText& valuesField() noexcept { return *values_; }
    [[nodiscard]] ui::Button&        createButton() noexcept { return *create_; }
    [[nodiscard]] gfx::Rect boxRect() const;

    // << Creer l'enumeration >> : le nom et les valeurs (l'hote fait la commande).
    const core::SignalPtr<> created = core::Signal<>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    ui::InputText*     name_{nullptr};
    ui::MultiLineText* values_{nullptr};
    ui::Button*        create_{nullptr};
    ui::Button*        cancel_{nullptr};
    ui::Button*        langs_[3]{};
    std::vector<std::string> taken_;
    int                lang_{0};
    bool               open_{false};
    core::ConnectionScope links_;
};

// Cree l'enumeration (une commande, Ctrl+Z) : makeEnumeration de E, puis ces
// valeurs, puis ses deux conversions reecrites d'apres elles. Rend son id.
hmi::Id createEnumeration(const hmi::DocumentPtr&, const std::function<void(core::CommandPtr)>& apply, const std::string& name,
                          const std::vector<hmi::HmiEnumValue>& values, std::string* why = nullptr);

} // namespace app
