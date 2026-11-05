// =============================================================================
//  app/FileViewer.hpp - 1.21.0 : L'APERCU DE TOUS LES FICHIERS, A L'ECRAN
// -----------------------------------------------------------------------------
//  UN SEUL APERCU, PARTAGE : IHM > Ressources (la colonne de droite), IHM >
//  Fichiers externes (un document), l'objet Explorateur de fichiers de l'IHM en
//  marche (dans l'objet quand il est assez large, sinon une fenetre d'apercu).
//  Ce que montre chaque genre vient de app/FileView (sans ecran) :
//
//    +---------------------------------------------------------------+
//    | mesures.csv                                                   |  le nom
//    | Tableau CSV  .  12,4 Ko  .  30/10/2026 14:02                  |  le type, la taille, la date
//    | [Ouvrir avec le programme du systeme] [Ouvrir le dossier]     |
//    | Separateur : point-virgule  .  Encodage : UTF-8  .  Lignes... |  les faits
//    |  ...la grille, le texte numerote, l'image ajustee, les pages, |
//    |     l'onde et Lire, les onglets, la liste du zip...           |
//    +---------------------------------------------------------------+
//
//  CE QUI EST LENT SE FAIT SUR UN FIL DE FOND (un fichier du disque, une entree
//  de zip, un son, un classeur) : l'apercu dit "Chargement..." et l'ecran ne
//  bloque jamais ; un autre fichier choisi abandonne le precedent. Un PDF se lit
//  et se rend sur le fil des documents (hmi::doc), une video sur celui de son
//  decodeur. Des octets deja en memoire et petits (une ressource) : tout de suite.
//
//  LES TEXTURES : une par image montree, rendue au renderer quand l'apercu change
//  (au dessin suivant) ou quand il disparait. FileViewer ne depend d'aucun widget :
//  le dessin et les gestes prennent leur rectangle (le volet FileViewPane, la
//  partie droite de l'objet Explorateur dans la vue).
// =============================================================================
#pragma once

#include "FileView.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ui { struct Theme; }
namespace hmi { class Runtime; struct Project; }

namespace app {

class DetachedWindows;

class FileViewer {
public:
    // Les couleurs : celles du theme (l'appli), ou celles d'un objet IHM.
    struct Palette {
        gfx::Color back, surface, text, muted, accent, onAccent, error, warning, border;
    };
    struct Fonts {
        gfx::FontId ui{14}, small{12}, mono{14}, title{18};
    };
    [[nodiscard]] static Palette paletteOf(const ui::Theme&);
    [[nodiscard]] static Fonts   fontsOf(const ui::Theme&);
    [[nodiscard]] static Fonts   fontsAt(double px);   // un objet IHM : sa taille de texte, a l'ecran

    FileViewer();
    ~FileViewer();
    FileViewer(const FileViewer&) = delete;
    FileViewer& operator=(const FileViewer&) = delete;

    // Montrer une source ; la meme (meme cle : le meme fichier, a la meme date) ne relit rien. Vide : rien.
    void show(fileview::Source source);
    // Suivre un choix fait ailleurs (la ligne d'un Explorateur) : `hint` change, la source de `make` est montree ; le meme
    // `hint` ne refait rien (pas meme la date du fichier, lue par la cle).
    void follow(const std::string& hint, const std::function<fileview::Source()>& make);
    void clear();
    void reload();
    [[nodiscard]] const fileview::Source& source() const noexcept;
    [[nodiscard]] bool loading() const;                      // le fil de fond lit encore
    [[nodiscard]] const fileview::Preview* current() const;  // nul : rien, ou en chargement
    // Attendre la fin du chargement (les essais) ; vrai : fini.
    bool waitLoaded(int ms);

    // L'en-tete (le nom, le type, la taille, la date) : Ressources le dit dessous, a sa facon.
    void setShowHeader(bool on) noexcept;
    // Les boutons : Ouvrir avec le programme du systeme, Ouvrir le dossier (un objet IHM : selon sa propriete).
    void setActions(bool openWithSystem, bool openFolder) noexcept;
    void setEmptyText(std::string text);

    // Dessiner dans `area` ; les gestes dans le meme rectangle. Vrai : pris (a redessiner).
    void paint(gfx::IRenderer&, const gfx::Rect& area, const Palette&, const Fonts&);
    bool event(const ui::InputEvent&, const gfx::Rect& area);
    // A redessiner sans geste : un chargement, un son ou une video qui joue, un GIF anime, une page de PDF qui arrive.
    [[nodiscard]] bool animating() const;

    // ---- les gestes, sans souris (les scripts, les essais) ----
    bool openWithSystem(std::string* why = nullptr);
    bool openFolder(std::string* why = nullptr);
    void zoomBy(double factor);
    void zoomFit();
    void zoomActual();
    bool setSheet(std::size_t index);
    bool goPage(int delta);
    bool togglePlay(std::string* why = nullptr);
    bool toggleFold(std::size_t line);
    void scrollBy(double lines, double pixelsX = 0);
    [[nodiscard]] std::size_t sheet() const noexcept;
    [[nodiscard]] int page() const noexcept;           // 0..
    [[nodiscard]] double zoom() const noexcept;        // 1 = 100 % ; ajuste : le zoom du dernier dessin
    [[nodiscard]] bool fitted() const noexcept;
    // Ce que montre l'apercu, une ligne par information ; "Chargement..." pendant la lecture.
    [[nodiscard]] std::vector<std::string> lines() const;
    // La reponse du dernier geste (Ouvert..., la raison d'un refus) ; vide : aucune.
    [[nodiscard]] const std::string& message() const noexcept;
    // Les textures gardees (les essais : rendues quand l'apercu change).
    [[nodiscard]] std::size_t textureCount() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Le volet : un FileViewer aux couleurs du theme, qui prend le clavier au clic (les fleches, Page haut / bas defilent).
class FileViewPane final : public ui::Widget {
public:
    explicit FileViewPane(std::string id);
    [[nodiscard]] FileViewer& viewer() noexcept { return viewer_; }
    void show(fileview::Source source);

protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    FileViewer viewer_;
    // 1.24.1 : l'en-tete montre le fichier (son nom, son chemin) - son clic droit propose Ouvrir dans l'explorateur, Aller
    // a, Copier le chemin (ui::paths) ; le menu est un enfant du volet.
    ui::Widget* pathMenu_{nullptr};
    std::string pathMenuFile_;
};

// ---- l'Explorateur de fichiers de l'IHM en marche : l'apercu dans l'objet ----
//  Un par moteur et par objet ; oublie (ses textures rendues) quand il n'est plus dessine depuis 30 s.
[[nodiscard]] FileViewer& explorerViewer(const void* runtime, std::uint32_t object);
[[nodiscard]] FileViewer* findExplorerViewer(const void* runtime, std::uint32_t object);
// Le rectangle a l'ecran ou il a ete dessine la derniere fois (les gestes le reprennent).
void setExplorerViewerRect(const void* runtime, std::uint32_t object, const gfx::Rect& screen);
[[nodiscard]] bool explorerViewerRect(const void* runtime, std::uint32_t object, gfx::Rect& out);
[[nodiscard]] bool explorerViewersAnimating();
// La source de la ligne choisie d'un Explorateur en marche : un fichier du disque, une entree de zip (lue a la demande,
// sur le fil de fond), une ressource du projet (le projet pas encore enregistre) ; un dossier : son resume. Vide : rien.
[[nodiscard]] fileview::Source explorerSelection(const hmi::Runtime&, std::uint32_t object, const hmi::Project* project);
// Ce qui change quand la ligne choisie change, ou que la source est relue (Rafraichir).
[[nodiscard]] std::string explorerSelectionHint(const hmi::Runtime&, std::uint32_t object);

// ---- LA FENETRE D'APERCU (un Explorateur de fichiers trop etroit) ----
//  Une seule, reprise : ouverte la premiere fois (une fenetre a elle, deplacable, redimensionnable), puis mise a jour
//  et ramenee devant a chaque fichier choisi. Fermee (sa croix) : la suivante se rouvre. Faux : pas de fenetre ici.
bool showPreviewWindow(DetachedWindows& windows, const fileview::Source& source, bool allowOpen);

} // namespace app
