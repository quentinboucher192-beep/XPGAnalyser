// =============================================================================
//  app/MacroFormView.hpp - le formulaire d'une macro (lot macros 1)
// -----------------------------------------------------------------------------
//  CHAQUE QUESTION A LE BON CHAMP. Le genre vient de la ligne "#! champ" de la
//  macro (ou de son Ask quand la ligne manque) :
//
//    fichier          le chemin, le bouton ... (l'explorateur de Windows),
//                     Ctrl+V d'un fichier copie dans l'Explorateur, le fichier
//                     glisse sur le champ, et les fichiers recents ;
//    tache, section,  la liste de ce que le projet (et la bibliotheque)
//    dfb, ddt...      contient, proposee pendant la frappe ; le champ dit si
//                     le nom existe ou s'il sera cree ;
//    oui-non          un interrupteur ;
//    choix, nombre    des boutons quand ils sont peu nombreux (1 Information .
//    a libelles       2 Avertissement...), une liste sinon ;
//    choix-classeur   une liste lue dans un onglet du classeur ;
//    nom              verifie (Control Expert refuserait un accent, deux _ de
//                     suite...) et suivi de son exemple (EQ_ -> EQ_Pompes) ;
//    liste            une ligne par element ;
//    cases            des cases a cocher (les elements en retard...).
//
//  LES CHAMPS SONT RANGES : ceux sans groupe d'abord, puis les groupes de la
//  macro ("#! groupe"), puis "Reglages avances" replie. L'aide courte est sous
//  le libelle ; ce qu'on voit sous le champ dit s'il est bon.
//
//  LE FORMULAIRE NE LANCE RIEN. Il dit ce qui a change (`changed`) ; le volet
//  relance l'apercu et lui redonne les champs. Un champ deja montre garde son
//  widget d'un apercu a l'autre : on ne perd ni le curseur ni le focus.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../project/MacroSpec.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/Controls.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app {

// ------------------------------------------------------------ les widgets ----
namespace macroui {

// Un interrupteur : oui / non.
class SwitchToggle final : public ui::Widget {
public:
    explicit SwitchToggle(std::string id);
    void setOn(bool on);                 // sans signal
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    void clickForTest();
    const core::SignalPtr<bool> toggled = core::Signal<bool>::create();

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    bool   on_{false};
    double animStart_{-1.0};
    bool   animating_{false};
};

// Des boutons cote a cote, un seul choisi : "1 Information | 2 Avertissement".
class Segmented final : public ui::Widget {
public:
    explicit Segmented(std::string id);
    void setOptions(std::vector<std::string> labels);
    void setSelected(int index);         // sans signal
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] const std::vector<std::string>& options() const noexcept { return labels_; }
    [[nodiscard]] float preferredWidth() const;
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    [[nodiscard]] gfx::Rect optionRect(std::size_t index) const;
    const core::SignalPtr<int> selectionChanged = core::Signal<int>::create();

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] int optionAt(gfx::Point) const;
    std::vector<std::string> labels_;
    int selected_{-1};
    int hover_{-1};
};

// Le champ d'un chemin : Ctrl+V colle aussi un fichier copie dans
// l'Explorateur, et un fichier glisse dessus le remplit.
class FileInput final : public ui::InputText {
public:
    explicit FileInput(std::string id);
    // Colle ou depose : le chemin, deja nettoye.
    const core::SignalPtr<const std::string&> fileChosen = core::Signal<const std::string&>::create();

protected:
    ui::EventResult onEvent(const ui::InputEvent&) override;
};

} // namespace macroui

// ------------------------------------------------------------- le formulaire ----
// Ce que le formulaire montre d'un champ.
struct FormField {
    enum class Origin : std::uint8_t { Proposed, Typed, Remembered };
    std::string                     key;
    project::macro::FieldSpec       spec;       // le genre, les options, l'exemple...
    std::string                     label;
    std::string                     help;
    std::string                     group;
    bool                            advanced{false};
    std::string                     value;
    std::string                     proposed;   // la valeur que la macro propose
    Origin                          origin{Origin::Proposed};
    bool                            table{false};   // un tableau CSV ("#! tableau") et non une question
};

// Ce que le formulaire demande au monde : le projet, la bibliotheque, le classeur.
struct MacroFormHosts {
    // Les noms d'un genre (tache, section, dfb...) : nom et detail ("20 ms", "v1.49").
    std::function<std::vector<std::pair<std::string, std::string>>(project::macro::FieldKind)> names;
    // Les membres d'un type (les entrees d'un bloc) : nom et type.
    std::function<std::vector<std::pair<std::string, std::string>>(const std::string& type)> members;
    // Les valeurs d'une colonne d'un onglet du classeur choisi : valeur et libelle.
    std::function<std::vector<std::pair<std::string, std::string>>(const std::string& sheet, const std::string& column,
                                                                    const std::string& labelColumn)> sheetValues;
    // Les onglets du classeur choisi.
    std::function<std::vector<std::string>()> sheets;
    // Les elements en retard sur la bibliotheque : nom et "projet 1.48 -> bibliotheque 1.49".
    std::function<std::vector<std::pair<std::string, std::string>>()> outdated;
    std::function<std::vector<std::string>()> recentFiles;
    // Ce qu'on dit d'un fichier sous son champ ("412 Ko, modifie le 27/09 a 18:10 - 18 onglets").
    // Faux : introuvable (le texte dit pourquoi).
    std::function<bool(const std::string& path, std::string& text)> fileInfo;
    // Ou ira un fichier-sortie ("a cote du classeur : C:\Affaires\alarmes.csv").
    std::function<std::string(const std::string& name)> outputPath;
    // Le dernier classeur ouvert : ce que prend un champ de classeur laisse vide.
    std::function<std::string()> lastWorkbook;
};

class MacroFormView final : public ui::Widget {
public:
    explicit MacroFormView(std::string id);
    ~MacroFormView() override;

    void setHosts(MacroFormHosts hosts) { hosts_ = std::move(hosts); }
    // Les groupes, dans l'ordre de la macro.
    void setGroupOrder(std::vector<std::string> groups) { groupOrder_ = std::move(groups); }
    // Lot API 6 : l'ordre des questions DANS chaque groupe - celui de sa ligne
    // "#! groupe" (une carte deplacee dans l'editeur le change).
    void setGroupKeys(std::vector<project::macro::GroupSpec> groups) { groupKeys_ = std::move(groups); }
    // Montre ces champs. Un champ deja montre (meme cle, meme genre) garde son
    // widget ; sa valeur n'est remise que s'il n'a pas le focus.
    void show(const std::vector<FormField>& fields, double now);
    void setMessage(std::string text, ui::Tone tone) { message_ = std::move(text); messageTone_ = tone; invalidate(); }

    void setAdvancedOpen(bool open);
    [[nodiscard]] bool advancedOpen() const noexcept { return advancedOpen_; }

    // cle, valeur, tout de suite (un interrupteur) ou pas (une frappe).
    const core::SignalPtr<const std::string&, const std::string&, bool> changed =
        core::Signal<const std::string&, const std::string&, bool>::create();

    // Pour les scripts et les tests : les cles montrees, le widget d'une cle,
    // et poser une valeur comme le ferait la souris (le signal part).
    [[nodiscard]] std::vector<std::string> shownKeys() const;
    [[nodiscard]] ui::Widget* controlOf(const std::string& key) const;
    bool setValueForTest(const std::string& key, const std::string& value);
    [[nodiscard]] std::string valueOf(const std::string& key) const;
    [[nodiscard]] std::string hintOf(const std::string& key) const;
    // Le rectangle d'un champ a l'ecran (le rendre visible d'abord).
    [[nodiscard]] gfx::Rect rowRect(const std::string& key);
    void ensureVisible(const std::string& key);
    // Un fichier depose : dans le champ de fichier sous `at`, sinon le premier
    // qui accepte son extension. Faux : aucun.
    bool dropFile(const std::string& path, gfx::Point at);
    // Le bouton ... d'un champ de fichier, comme un clic.
    bool browse(const std::string& key);

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Row;
    Row* rowOf(const std::string& key) const;
    void buildControls(Row& row);
    void updateControls(Row& row, bool force);
    void computeHint(Row& row);
    void emitChange(Row& row, std::string value, bool immediate);
    [[nodiscard]] std::vector<Row*> ordered() const;
    [[nodiscard]] float contentHeight() const;
    void clampScroll();

    MacroFormHosts                     hosts_;
    std::vector<std::string>           groupOrder_;
    std::vector<project::macro::GroupSpec> groupKeys_;
    std::vector<std::unique_ptr<Row>>  rows_;
    bool                               advancedOpen_{false};
    float                              scroll_{0.f};
    mutable float                      contentH_{0.f};
    gfx::Rect                          advancedHeader_{};
    std::string                        message_;
    ui::Tone                           messageTone_{ui::Tone::None};
    double                             now_{0.0};
    bool                               filling_{false};
    core::ConnectionScope              links_;
    // La reponse de l'explorateur (le bouton ...) arrive plus tard : elle n'en
    // garde qu'une reference faible - formulaire ferme, reponse ignoree.
    std::shared_ptr<char>              alive_{std::make_shared<char>('\0')};
};

} // namespace app
