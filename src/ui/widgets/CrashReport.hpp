// =============================================================================
//  ui/widgets/CrashReport.hpp - 1.12.28 : la fenetre du plantage et du blocage,
//  dessinee par l'appli
// -----------------------------------------------------------------------------
//  Le client (23/10) : « met pas un truc windows pour les blocages/crash fait le
//  meme principe que l'explorateur de fichiers du projet ». Avant : une boite
//  native (SDL_ShowMessageBox), grise, aux boutons coupes (« Copier le rappor »,
//  « gnaler le problèr »), ouverte avant la premiere image (la fenetre restait
//  noire derriere).
//
//  UNE FENETRE, TROIS MOMENTS :
//    - Pending, au lancement suivant : un dialogue de l'appli, dans la fenetre
//      principale, apres sa premiere image (CrashReportDialog) ;
//    - Hang, pendant un blocage, et Crash, apres un plantage : la boucle
//      principale ne dessine plus. Le chien de garde (ou le rapporteur) relance
//      l'exe en « fenetre de rapport » (app/ReportWindow) : une petite fenetre a
//      part, avec les memes widgets et le meme theme (CrashReportScreen).
//  ELLE DIT ce qui s'est passe (la raison du rapport, en clair), ce que l'appli
//  faisait (les portees ouvertes du fil principal, sinon ses dernieres actions),
//  ou est le rapport ; et les boutons du moment, a leur taille (rien de coupe).
//  Pendant un blocage, elle compte les secondes ; quand la boucle repart, elle
//  le dit et se ferme.
//
//  Pas de SDL ici : les gestes (ouvrir le dossier, copier, signaler, fermer
//  l'appli bloquee, relancer) sont donnes par l'hote.
// =============================================================================
#pragma once

#include "../Widget.hpp"
#include "../../menu/IMenu.hpp"
#include "Controls.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

namespace paths { class PathMenu; }   // 1.25.1 : le clic droit sur le chemin du rapport (PropertyGridHooks.hpp)

namespace crashreport {

enum class Mode : std::uint8_t { Pending, Hang, Crash };

// Ce que la fenetre montre d'un rapport (core/CrashGuard : son format).
struct Summary {
    std::string              kind;        // « blocage », « plantage », « manuel »
    std::string              reason;      // en clair : « La boucle principale ne bat plus depuis 8,0 s »
    std::string              detail;      // le technique, s'il y en a (le code, l'adresse)
    std::vector<std::string> doing;       // les portees ouvertes du fil principal, de la plus large a la plus fine
    std::vector<std::string> recent;      // ses dernieres actions (menus, commandes, fichiers), la plus recente d'abord
    std::vector<std::string> documents;   // les documents ouverts
    std::string              version, system, when;
};
[[nodiscard]] Summary summarize(std::string_view report);

// « 8,0 s » au bout d'une raison de blocage : 8.0 ; 0 sinon.
[[nodiscard]] double stalledSeconds(std::string_view reason);

} // namespace crashreport

struct CrashReportSetup {
    crashreport::Mode mode{crashreport::Mode::Pending};
    std::string       product{"IHM Studio"};
    std::string       path;               // le rapport (UTF-8)
    std::string       text;               // le rapport entier
    std::string       when;               // « 23/10/2026 à 10:57 »
    double            stalledSeconds{0};  // Hang : l'arret deja compte quand la fenetre s'ouvre
    // Les gestes. Ceux qui rendent un texte : la note a montrer (vide : aucune).
    std::function<std::string()> openFolder, copyReport;
    std::function<std::string()> signal;                    // vide : pas de bouton Signaler
    std::function<void()>        wait, closeApp, restart;   // Hang : Attendre, Fermer l'appli ; Crash : Relancer
    std::function<void()>        close;                     // fermer la fenetre
};

class CrashReportView final : public Widget {
public:
    // framed : un dialogue dans la fenetre de l'appli (deplacable) ; sinon toute la fenetre.
    CrashReportView(CrashReportSetup setup, bool framed);
    ~CrashReportView() override;

    // A chaque image : le compteur du blocage, l'animation, la note qui s'efface, la
    // fermeture d'office apres une reprise.
    void tick(double now);
    // Hang : la boucle principale repart (apres `seconds` d'arret) ; ou l'appli bloquee
    // s'est fermee. La fenetre le dit, puis se ferme toute seule.
    void setRecovered(double seconds);
    void setGone();

    [[nodiscard]] const crashreport::Summary& summary() const noexcept { return summary_; }
    [[nodiscard]] const CrashReportSetup& setup() const noexcept { return setup_; }
    [[nodiscard]] std::string headline() const;
    [[nodiscard]] std::string subline() const;
    [[nodiscard]] const std::string& note() const noexcept { return note_; }
    [[nodiscard]] bool recovered() const noexcept { return recovered_; }
    [[nodiscard]] bool gone() const noexcept { return gone_; }
    [[nodiscard]] Button* button(std::string_view id) const;      // les essais
    [[nodiscard]] const std::vector<Button*>& buttons() const noexcept { return buttons_; }
    [[nodiscard]] gfx::Rect panel() const noexcept { return panel_; }
    // 1.25.1 : LES CHEMINS MONTRES (<< corrige les limites restantes >>) - la ligne du rapport (0), celle de son dossier
    // (1), celle du minidump a cote (2, Windows : quand il existe) ; leur clic droit propose Ouvrir dans l'explorateur,
    // Aller a, Copier le chemin (ui::paths ; sans fournisseur, rien). Vide : pas dessinee. `pathOf` : le chemin d'une ligne.
    [[nodiscard]] gfx::Rect pathRow(int which) const noexcept { return which >= 0 && which < 3 ? rows_[which] : gfx::Rect{}; }
    [[nodiscard]] std::string pathOf(int which) const;
    [[nodiscard]] const std::string& dumpPath() const noexcept { return dump_; }
    [[nodiscard]] paths::PathMenu* pathMenu() const noexcept { return pathMenu_.get(); }

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    Button* add(std::string text, std::string id, Button::Style style, std::function<void()> act);
    void    say(std::string note);
    void    primary();

    CrashReportSetup     setup_;
    crashreport::Summary summary_;
    bool                 framed_;
    gfx::Rect            panel_{};
    std::vector<Button*> buttons_;
    std::vector<Button*> left_, right_;     // la rangee du bas : les outils a gauche, la decision a droite
    Button*              primary_{nullptr};
    core::ConnectionScope links_;
    std::string          note_;
    double               now_{0}, start_{-1}, noteAt_{-1}, endAt_{-1};
    double               recoveredAfter_{0};
    bool                 recovered_{false}, gone_{false}, closed_{false};
    gfx::Rect            rows_[3]{};             // 1.25.1 : le rapport, son dossier, le minidump
    std::string          dump_;                  // le minidump a cote du rapport (vide : pas encore, ou aucun)
    double               dumpCheckedAt_{-100};
    std::unique_ptr<paths::PathMenu> pathMenu_;
    void                 findDump();
};

// Au lancement suivant : le dialogue, dans la fenetre principale.
class CrashReportDialog final : public menu::WidgetMenu {
public:
    explicit CrashReportDialog(CrashReportSetup setup);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    void Update(const menu::FrameContext& fc) override;
    [[nodiscard]] CrashReportView* view() const noexcept { return view_; }
protected:
    core::Status buildUi() override;
private:
    CrashReportSetup setup_;
    CrashReportView* view_{nullptr};
};

// La fenetre a part (app/ReportWindow) : tout son contenu.
class CrashReportScreen final : public menu::WidgetMenu {
public:
    explicit CrashReportScreen(CrashReportSetup setup);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    void Update(const menu::FrameContext& fc) override;
    [[nodiscard]] CrashReportView* view() const noexcept { return view_; }
protected:
    core::Status buildUi() override;
private:
    CrashReportSetup setup_;
    CrashReportView* view_{nullptr};
};

} // namespace ui
