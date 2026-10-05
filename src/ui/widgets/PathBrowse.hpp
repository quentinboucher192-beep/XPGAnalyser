// =============================================================================
//  ui/widgets/PathBrowse.hpp - le bouton ... d'un champ de chemin
// -----------------------------------------------------------------------------
//  PARTOUT OU L'ON TAPE LE CHEMIN d'un fichier ou d'un dossier a importer ou a
//  exporter, un petit bouton ... a sa droite ouvre l'explorateur de fichiers du
//  systeme (ui::pickFile : SDL3 - ouvrir, enregistrer sous, choisir un dossier).
//  Le chemin choisi remplit le champ, qui reste un champ : taper ou coller un
//  chemin, puis Entree, marche comme avant.
//
//  UNE SEULE FACON DE LE FAIRE :
//    - FormDialog::setFieldBrowse(index, spec)      un champ d'un formulaire ;
//    - HmiAskDialog::Option::browse                  le champ sous un choix ;
//    - BrowseButton(champ, spec)                     a cote de n'importe quel
//                                                    InputText (meme parent) ;
//    - browsePath(spec, texte, fait)                 l'explorateur seul, sans
//                                                    bouton (une commande).
//
//  ASYNCHRONE. SDL repond plus tard (App::frame le rend sur le fil de
//  l'interface). Le dialogue qui portait le champ a pu etre ferme entretemps
//  (Echap, Annuler) : le bouton garde un jeton de vie et la reponse n'en tient
//  qu'une reference faible - bouton detruit, reponse ignoree.
//
//  OU S'OUVRE L'EXPLORATEUR : la ou pointe le champ s'il est rempli (son
//  dossier, ou le fichier lui-meme), sinon `start` - le dossier du projet, son
//  exports/, son simulation/... Un nom seul se lit dans ce dossier.
//
//  SCRIPTS. `explorateur "C:/x/y.csv"` range la reponse de la prochaine
//  ouverture (ui::queueFilePick : l'explorateur ne s'ouvre pas) ;
//  `parcourir "Libelle du champ"` clique le bouton ... de ce champ.
// =============================================================================
#pragma once

#include "Controls.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {

    // Ce que le bouton ... ouvre.
    enum class BrowseMode : std::uint8_t { None, OpenFile, SaveFile, Folder };

    struct PathBrowse {
        BrowseMode  mode{BrowseMode::None};
        // Les filtres : "*.csv" ; "*.xpg;*.xhw" ; avec un nom :
        // "Exports Control Expert|*.xpg;*.xhw" ; plusieurs groupes :
        // "Images|*.png;*.jpg|Sons|*.wav;*.mp3". "Tous les fichiers" suit
        // toujours. Ignores pour un dossier. Pour un enregistrement, la
        // premiere extension est ajoutee a un nom qui n'en a pas.
        std::string filters;
        // Le dossier de depart (le dossier du projet, exports/...) ; pour un
        // enregistrement, ce peut etre le fichier propose. Vide : au choix du
        // systeme. Le texte du champ l'emporte quand il pointe quelque part.
        std::string start;
        // Le titre de la fenetre de l'explorateur ; vide : le libelle du champ.
        std::string title;
        // Folder : le champ nomme un dossier A CREER (nouveau projet, copie,
        // version extraite). L'explorateur s'ouvre sur son parent et choisit OU
        // le mettre ; le nom du champ est garde (D:\Affaires + "Pompe" ->
        // D:\Affaires\Pompe). Un dossier vide, ou du meme nom, est pris tel quel.
        bool        newFolder{false};
        [[nodiscard]] bool active() const noexcept { return mode != BrowseMode::None; }
    };

    // Les raccourcis des appels.
    [[nodiscard]] PathBrowse openFile(std::string filters, std::string start = {}, std::string title = {});
    [[nodiscard]] PathBrowse saveFile(std::string filters, std::string start = {}, std::string title = {});
    [[nodiscard]] PathBrowse chooseFolder(std::string start = {}, std::string title = {});
    [[nodiscard]] PathBrowse newFolder(std::string start = {}, std::string title = {});
    // Un chemin sous un dossier (UTF-8) : pathIn(dossierDuProjet, "exports").
    // Un dossier vide rend "" : l'explorateur s'ouvre ou le systeme veut.
    [[nodiscard]] std::string pathIn(const std::string& folder, std::string_view name);

    // La demande a l'explorateur pour un champ dont le texte est `current`.
    [[nodiscard]] FilePick filePickFor(const PathBrowse& spec, std::string_view current);
    // Ce que devient la reponse de l'explorateur : "" (annule) reste "" ; un
    // enregistrement sans extension recoit la premiere des filtres ; un dossier
    // a creer garde le nom de `current`.
    [[nodiscard]] std::string finishPick(const PathBrowse& spec, std::string chosen, std::string_view current);
    // L'explorateur seul. `done` recoit le chemin choisi - jamais vide : un
    // explorateur annule ne rappelle pas. Faux : pas d'explorateur ici (essais,
    // console). L'appelant qui peut disparaitre avant la reponse la garde lui-
    // meme (un jeton de vie, comme BrowseButton).
    bool browsePath(const PathBrowse& spec, std::string_view current, std::function<void(std::string)> done);

    // ------------------------------------------------------------ BrowseButton ---
    //  Le bouton ... d'un champ. Le champ vit au moins aussi longtemps que lui :
    //  le meme dialogue, la meme rangee (le bouton est son voisin, pas son enfant
    //  - "champ N" des scripts compte toujours les memes champs).
    class BrowseButton final : public Button {
    public:
        BrowseButton(InputText& target, PathBrowse spec, std::string id = {}, std::string text = {});

        void setSpec(PathBrowse spec) { spec_ = std::move(spec); }
        [[nodiscard]] const PathBrowse& spec() const noexcept { return spec_; }
        [[nodiscard]] InputText& target() const noexcept { return *target_; }
        // Le libelle du champ : le titre de l'explorateur, l'infobulle, et ce que
        // `parcourir "Libelle"` cherche.
        void setFieldLabel(std::string label);
        [[nodiscard]] const std::string& fieldLabel() const noexcept { return fieldLabel_; }

        // Ce que fait un clic. Faux : pas d'explorateur, ou il est deja ouvert.
        bool browse();
        [[nodiscard]] bool pending() const noexcept { return pending_; }

        [[nodiscard]] SizeHint sizeHint() const override;

        // Apres que le champ a recu le chemin choisi (setText : son textChanged
        // est deja parti).
        const core::SignalPtr<const std::string&> chosen = core::Signal<const std::string&>::create();

    private:
        InputText*            target_;
        PathBrowse            spec_;
        std::string           fieldLabel_;
        bool                  pending_{false};
        std::shared_ptr<char> alive_;       // la reponse n'en garde qu'une reference faible
        core::ConnectionScope links_;
    };

} // namespace ui
