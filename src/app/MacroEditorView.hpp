// =============================================================================
//  app/MacroEditorView.hpp - le mode Modifier d'une macro (lot API 6, A11)
// -----------------------------------------------------------------------------
//  L'onglet "Macro X" n'est plus un simple editeur de texte. Un seul fichier,
//  vu de trois facons qui restent d'accord :
//
//   * LE CODE, a gauche : l'editeur ST, avec la completion des fonctions des
//     macros, des onglets du dernier classeur ouvert et des noms du projet ;
//     les lignes de la question choisie sont eclairees.
//   * LES QUESTIONS, a droite : une carte par question, rangees par groupe
//     ("#! groupe"). Glisser une carte la deplace (dans son groupe ou dans un
//     autre) ; sa fiche, dessous, change son libelle, son genre, son groupe,
//     son aide, "avancee" ou non. Chaque geste REECRIT LES LIGNES "#!" qui le
//     disent (project/MacroSpecWriter) et rien d'autre ; une ligne "#!" tapee
//     dans le code cree sa carte au prochain releve (une demi-seconde apres la
//     frappe).
//   * L'APERCU DU FORMULAIRE : ce que verra celui qui lance la macro.
//
//  ESSAYER (F5) rejoue la macro en apercu sur le projet ouvert (MacroSession :
//  tout est defait) ; les remarques - erreurs, avertissements, ce qu'elle a
//  lu - menent a leur ligne d'un double-clic. VERIFIER relit l'en-tete et le
//  code sans rien lancer.
//
//  ENREGISTRER (Ctrl+S) propose la version suivante (1.10 -> 1.11) et demande
//  ce qui change : "#! version" et "#! changes" sont ecrits dans l'en-tete, puis
//  le fichier dans libs/Macros. Ctrl+Z dans l'editeur defait les gestes des
//  cartes et les frappes, jusqu'a la fermeture de l'onglet.
//
//  UTILISER ouvre le formulaire de la macro (l'onglet Macros), avec le code tel
//  qu'il est a l'ecran : rien n'est ecrit avant Appliquer.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/MacroSpec.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui { class MultiLineText; class TableView; class PropertyGrid; class ToggleButton; class Button; }

namespace app {

class HmiToolStrip;
class MacroFormView;

class MacroEditorView final : public ui::Widget {
public:
    enum Action : int { ASave = 1, AVersion, AUndo, ARedo, ATry, ACheck, AUse, AShow, AHelp };

    struct Hosts {
        std::function<std::shared_ptr<domain::Project>()> project;   // Essayer, la completion ; nul : pas de projet
        std::string                                        libsRoot;
        // Ecrire dans libs/Macros ; faux : l'ecran a dit pourquoi.
        std::function<bool(const std::string& name, const std::string& source)> save;
        // Utiliser : le formulaire, avec ce code.
        std::function<void(const std::string& name, const std::string& source)> use;
        std::function<void(const std::string& name)>                            showInMacros;
        std::function<void()>                                                   help;
        std::function<void(const std::string& text, bool error)>                status;
        // Enregistrer : la version et ce qui change (l'ecran pose le dialogue).
        std::function<void(const std::string& current, const std::string& next,
                           std::function<void(const std::string& version, const std::string& changes)> done)> askSave;
        // Ajouter une question : sa cle, son genre, son libelle.
        std::function<void(std::function<void(const std::string& key, const std::string& kind, const std::string& label)> done)> askQuestion;
    };

    // Une remarque de l'essai ou de la verification.
    struct Remark {
        enum class Kind : std::uint8_t { Ok, Info, Warning, Error };
        Kind        kind{Kind::Info};
        std::size_t line{0};          // 1... ; 0 : pas de ligne
        std::string text;
    };

    MacroEditorView(std::string id, std::string name, std::string text);
    ~MacroEditorView() override;
    void setHosts(Hosts hosts);

    // Chaque image : le releve differe apres une frappe.
    void tick(double now);
    void runAction(int action);

    // ---- le code --------------------------------------------------------------
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::string source() const;
    // Remplace le texte (un geste des cartes) ; `undoable` : Ctrl+Z le reprend.
    void setSource(const std::string& text, bool undoable = true);
    [[nodiscard]] bool dirty() const noexcept { return source() != saved_; }
    [[nodiscard]] const std::string& savedSource() const noexcept { return saved_; }
    void goToLine(std::size_t line);             // 1...
    [[nodiscard]] ui::MultiLineText& code() noexcept { return *code_; }
    bool undoStep();
    bool redoStep();

    // ---- les questions ----------------------------------------------------------
    [[nodiscard]] const project::macro::MacroSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::vector<std::string> questionKeys() const;       // dans l'ordre des cartes
    void selectQuestion(const std::string& key);
    [[nodiscard]] const std::string& selectedQuestion() const noexcept { return selected_; }
    bool moveQuestion(const std::string& key, const std::string& group, std::size_t index);
    bool setQuestionLabel(const std::string& key, const std::string& label);
    bool setQuestionKind(const std::string& key, const std::string& kind);
    bool setQuestionOptional(const std::string& key, bool optional);
    bool setQuestionGroup(const std::string& key, const std::string& group);
    bool setQuestionAdvanced(const std::string& key, bool advanced);
    bool setQuestionHelp(const std::string& key, const std::string& help);
    bool addQuestion(const std::string& key, const std::string& kind, const std::string& label, const std::string& preset = {});
    // 0 : les questions ; 1 : l'apercu du formulaire.
    void setSideTab(int tab);
    [[nodiscard]] int sideTab() const noexcept { return sideTab_; }
    [[nodiscard]] ui::TableView&    cards() noexcept { return *cards_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *props_; }
    [[nodiscard]] MacroFormView&    form() noexcept { return *form_; }

    // ---- essayer, verifier ------------------------------------------------------
    void tryRun();
    void check();
    [[nodiscard]] const std::vector<Remark>& remarks() const noexcept { return remarks_; }
    [[nodiscard]] ui::TableView& remarkTable() noexcept { return *remarkTable_; }
    [[nodiscard]] const std::string& trialTitle() const noexcept { return trialTitle_; }

    // ---- enregistrer ------------------------------------------------------------
    // La version du fichier et celle que proposera Enregistrer.
    [[nodiscard]] std::string version() const { return spec_.version; }
    [[nodiscard]] std::string nextVersion() const;
    void askSave();
    // Ecrit "#! version" et "#! changes", puis le fichier (hosts.save).
    bool save(const std::string& version, const std::string& changes);

    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class CardsModel;
    class RemarksModel;
    friend class CardsModel;
    friend class RemarksModel;

    struct Card {
        bool        header{false};
        std::string group;        // "" : sans groupe
        std::string key;          // vide pour un titre
        std::size_t count{0};     // un titre : ses questions
    };

    void reparse();                                  // le code -> spec, cartes, apercu
    void rebuildCards();
    void rebuildProperties();
    void rebuildPreview();
    void markLines();
    void remember(const std::string& before);        // une entree d'annulation
    bool rewrite(const std::string& next, const std::string& what);
    void setRemarks(std::vector<Remark> remarks, std::string title);
    [[nodiscard]] std::string champValue(const std::string& key) const;
    [[nodiscard]] std::vector<std::string> groupNames() const;
    void complete(std::string_view prefix, std::vector<std::pair<std::string, std::string>>& out) const;

    std::string                       name_;
    std::string                       saved_;         // ce qui est dans libs/ (au dernier enregistrement)
    Hosts                             hosts_;
    project::macro::MacroSpec         spec_;
    std::vector<Card>                 cardRows_;
    std::string                       selected_;
    int                               sideTab_{0};
    std::vector<std::string>          undo_, redo_;
    std::string                       lastSnapshot_;  // le texte au dernier releve
    bool                              typing_{false};
    double                            now_{0.0}, reparseAt_{-1.0};
    bool                              settingText_{false};
    std::vector<Remark>               remarks_;
    std::string                       trialTitle_;

    HmiToolStrip*                     tools_{nullptr};
    ui::MultiLineText*                code_{nullptr};
    ui::TableView*                    remarkTable_{nullptr};
    std::shared_ptr<RemarksModel>     remarksModel_;
    ui::ToggleButton*                 tabQuestions_{nullptr};
    ui::ToggleButton*                 tabPreview_{nullptr};
    ui::TableView*                    cards_{nullptr};
    std::shared_ptr<CardsModel>       cardsModel_;
    ui::Button*                       addButton_{nullptr};
    ui::PropertyGrid*                 props_{nullptr};
    MacroFormView*                    form_{nullptr};
    gfx::Rect                         codeTitle_{}, trialTitleRect_{}, sideRect_{};
    bool                              syncing_{false};
    core::ConnectionScope             links_;
};

} // namespace app
