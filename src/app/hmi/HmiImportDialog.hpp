// =============================================================================
//  app/hmi/HmiImportDialog.hpp - Importer des vues : comparer avant d'agir (lot 20)
// -----------------------------------------------------------------------------
//  1.11.2 (scene 5 de la maquette de MQ5 ; decisions 162, 174, 177) :
//    Fichier               son nom ; "Le fichier emporte : ..." et d'ou il vient
//    un tableau            Nom / Sorte / Dans ce projet : "nouveau",
//                          "identique : garde(e)", "existe deja : a trancher plus bas"
//    N NOMS EN CONFLIT     pour chacun : "Renommer en X_2", "Remplacer celui du
//                          projet" (et "Garder celui du projet") ; un style garde
//                          par defaut celui du projet (decision 177)
//    VARIABLES ABSENTES    les creer (avec leur type), ou non
//  "Importer N elements" fait tout en une commande : Ctrl+Z annule l'import entier.
//  1.11.2 (decision 188) : un paquet lache sur l'appli ouvre cette fenetre (tout
//  genre, .xpgvues compris : takePackages, load) ; "Parcourir..." dans la fenetre
//  meme en lit un autre, comme un paquet lache sur elle (la fenetre se refait) ;
//  un fichier illisible, plus recent ou un modele : la bande le dit, Importer est
//  grise ; "Annuler" (ou Echap) ne fait rien.
// =============================================================================
#pragma once

#include "../../hmi/HmiPackage.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/PathBrowse.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiImportDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string        fileName;      // "Armoires_Sud.xpgvues"
        hmi::pkg::Manifest manifest;
        std::string        contents;      // "3 vues, 1 symbole, 4 images..."
        hmi::pkg::Plan     plan;
        std::string        title{"Importer des vues"};
        std::string        confirm{"Importer"};
        // 1.11.2 (decision 188) : le chemin du fichier ; le paquet lu (nul : rien a
        // importer, `error` dit pourquoi) ; ou s'ouvre Parcourir...
        std::string                              path;
        std::shared_ptr<const hmi::pkg::Package> package;
        std::string                              error;
        ui::PathBrowse                           browse{};
    };
    // 1.11.2 : lire un autre fichier (Parcourir..., un paquet lache sur la fenetre).
    using Loader = std::function<Spec(const std::string& path)>;
    explicit HmiImportDialog(Spec spec, Loader loader = {});
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_->title; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    void            Update(const menu::FrameContext& fc) override;

    // 1.11.2 (decision 188) : la fenetre pour le fichier `path`, compare au projet :
    // son titre (ce qu'il apporte), ce qu'il emporte, le plan ; un fichier
    // illisible, d'une version plus recente, ou un modele : `error` le dit.
    [[nodiscard]] static Spec load(const hmi::Project& target, const std::string& path);
    // Un paquet : .xpgvues, .xpgsymboles, .xpgtypes, .xpgfonctions, .xpgscripts (sans casse).
    [[nodiscard]] static bool isPackageFile(const std::string& path);
    // Les paquets d'un depot, dans leur ordre ; ils sortent de `paths`.
    [[nodiscard]] static std::vector<std::string> takePackages(std::vector<std::string>& paths);
    // Ce que la fenetre montre (le fichier choisi dedans compris) : la reponse
    // s'importe depuis lui (son paquet, son plan).
    [[nodiscard]] std::shared_ptr<const Spec> state() const { return spec_; }
    // Lire un autre fichier tout de suite (scripts, essais ; l'ecran : a l'image suivante).
    void loadFile(const std::string& path);

    // La reponse : un choix par element du plan (dans l'ordre du plan).
    [[nodiscard]] static std::vector<hmi::pkg::Choice> parse(const std::string& payload);
    // Le plan avec ces choix.
    static void applyChoices(hmi::pkg::Plan& plan, const std::vector<hmi::pkg::Choice>& choices);

    // ---- pour les scripts et les tests ----
    [[nodiscard]] const hmi::pkg::Plan& plan() const noexcept { return spec_->plan; }
    bool setChoice(const std::string& name, hmi::pkg::Choice choice);
    void setCreateVariables(bool on);
    void confirm();

protected:
    core::Status buildUi() override;

private:
    friend class ImportBody;
    [[nodiscard]] std::string payload() const;
    void finish(bool ok);

    std::shared_ptr<Spec>         spec_;
    Loader                        loader_;
    std::string                   pending_;     // un fichier a lire a la prochaine image
    std::shared_ptr<char>         alive_{std::make_shared<char>(0)};   // la reponse de Parcourir... n'en garde qu'une reference faible
    void                          browse();
    [[nodiscard]] bool            ready() const;   // de quoi importer (sinon Importer est grise)
    std::unique_ptr<ui::Widget>   makeBody();
    // 1.11.2 : un groupe de choix par element en conflit (nul : pas de choix) ; sa valeur : le Choice.
    std::vector<std::shared_ptr<ui::RadioGroup>> groups_;
    [[nodiscard]] hmi::pkg::Choice effective(std::size_t item) const;
    [[nodiscard]] std::string confirmText() const;   // "Importer 8 \xC3\xA9l\xC3\xA9ments"
    ui::DropDown*                 variables_{nullptr};
    ui::Button*                   ok_{nullptr};
    bool                          done_{false};
    core::ConnectionScope         links_;
};

} // namespace app
