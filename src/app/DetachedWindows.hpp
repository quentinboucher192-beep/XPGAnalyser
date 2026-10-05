// =============================================================================
//  app/DetachedWindows.hpp - les onglets detaches dans des fenetres a eux
// -----------------------------------------------------------------------------
//  Lot API 7 : un onglet de l'ecran principal peut partir dans sa propre fenetre
//  du systeme (un deuxieme ecran, typiquement) puis revenir. Chaque fenetre a son
//  SDL_Window, son renderer (les textures, donc l'atlas des polices et les
//  images, appartiennent a un SDL_Renderer) et une racine de widgets a elle
//  (ui::WidgetHost : survol, focus, listes ouvertes, infobulles) : la page y vit
//  comme dans l'onglet, sous une bande "Ramener dans la fenetre principale".
//
//  La fenetre POSSEDE la page tant qu'elle est detachee ; la rendre (sa croix,
//  son bouton, giveBack, giveBackAll) rappelle `giveBack(page)` du proprietaire,
//  qui la remet en onglet. Elle revient d'elle-meme quand le projet se ferme,
//  quand l'ecran qui l'avait quitte la pile (juste avant d'etre detruit), quand
//  le poste d'exploitation s'ouvre, et a la sortie de l'application.
//
//  Lot API 8 : un dialogue demande par un geste fait DANS la fenetre (un clic,
//  une touche, un outil de sa page) s'ouvre dans cette fenetre, centre sur
//  elle, avec son voile ; il reste modal pour toute l'application (la fenetre
//  principale attend et le dit). Ce qui reste a la fenetre principale : les
//  ecrans pousses sur le MenuManager, et les dialogues demandes ailleurs (voir
//  DetachedWindows.cpp). Sans SDL (XPG_HAVE_SDL3 = 0), detach() rend faux.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace app {

class App;

// Les onglets detaches dans des fenetres a eux (lot API 7).
class DetachedWindows {
public:
    explicit DetachedWindows(App& app);
    ~DetachedWindows();
    DetachedWindows(const DetachedWindows&) = delete;
    DetachedWindows& operator=(const DetachedWindows&) = delete;

    // Ouvre `page` dans une fenetre a elle (titre, icone de l'appli), a la taille w x h
    // (0 : celle que la page occupait), decalee de la fenetre principale (ou sur le
    // deuxieme ecran s'il y en a un). La fenetre possede la page ; quand on la ferme
    // (sa croix, giveBack, giveBackAll), `giveBack(page)` la rend a l'appelant, qui la
    // remet en onglet. Faux : pas de fenetre possible (`page` n'est pas deplace).
    bool detach(const std::string& title, ui::WidgetPtr& page, std::function<void(ui::WidgetPtr)> giveBack, int w = 0, int h = 0);
    [[nodiscard]] bool isDetached(const ui::Widget* page) const;
    void raise(const ui::Widget* page);
    bool giveBack(const ui::Widget* page);
    void giveBackAll();
    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] std::vector<const ui::Widget*> pages() const;
    // Pour App : les evenements d'une fenetre detachee (vrai : elle les a pris),
    // et le dessin de chaque fenetre a chaque image.
    bool routeEvent(std::uint32_t windowId, const ui::InputEvent& e);
    void frame();
    const core::SignalPtr<> changed = core::Signal<>::create();

    // --- au-dela de l'interface du lot : App et les scripts ----------------------
    //  Rendre une page (giveBack, giveBackAll, la croix) pendant un evenement ou
    //  un dessin d'une fenetre detachee - son bouton "Ramener", une page qui se
    //  rend elle-meme - se fait a la sortie de cet evenement : detruire l'arbre
    //  qu'on est en train de parcourir ne pardonne pas. Ailleurs, tout de suite.
    //
    // La fenetre principale (App::run, avant la premiere image) : ou poser les
    // nouvelles fenetres, et laquelle montrer quand une question y attend.
    void setMainWindow(SDL_Window* window) noexcept;
    // Le dessin a l'heure de l'appli (App::frame) : carets, animations et attente
    // des infobulles suivent l'horloge de la fenetre principale - l'horloge fixe
    // d'une session rejouee comprise. frame() prend celle du poste.
    void frame(double totalSeconds);
    // Vrai quand `windowId` est une fenetre detachee : App lui passe alors TOUS
    // ses evenements, et aucun ne va a l'ecran principal.
    [[nodiscard]] bool ownsWindow(std::uint32_t windowId) const;
    // Sa croix (ou Alt+F4) : la page revient. Faux : pas une fenetre detachee.
    bool closeRequested(std::uint32_t windowId);
    // La souris a quitte la fenetre : plus de survol, plus d'infobulle.
    void mouseLeft(std::uint32_t windowId);
    // Le titre donne a detach (ou a setTitle), "" si `page` n'est pas detachee.
    [[nodiscard]] std::string titleOf(const ui::Widget* page) const;
    // L'onglet a change de nom : la bande et le titre de la fenetre suivent.
    void setTitle(const ui::Widget* page, std::string title);
    // Les titres, dans l'ordre d'ouverture.
    [[nodiscard]] std::vector<std::string> titles() const;
    // La page detachee de titre `title` : le titre exact, sinon sans la casse,
    // sinon son debut (comme la commande "onglet" des scripts) ; nulle sinon.
    [[nodiscard]] const ui::Widget* findByTitle(std::string_view title) const;
    // Un evenement pour la fenetre de `page`, comme s'il venait d'elle (scripts).
    bool sendEvent(const ui::Widget* page, const ui::InputEvent& e);
    // L'image de la fenetre de `page` en PNG : elle est redessinee et relue avant
    // d'etre presentee. Faux, et `error` dit pourquoi : pas detachee, fenetre
    // reduite, ecriture impossible.
    bool capturePng(const ui::Widget* page, const std::string& path, std::string* error = nullptr);

    // ---- Lot API 8 : les dialogues dans la fenetre detachee -----------------
    //  Chaque fenetre a un numero (celui de SDL), qui est aussi celui de ses
    //  couches dans le MenuManager (MenuManager::WindowId ; 0 : la principale).
    //  Le temps d'un de ses evenements, elle est la fenetre "en cours" : un
    //  dialogue demande alors s'ouvre chez elle. Fermee (ou l'onglet rendu)
    //  pendant qu'un dialogue y est : il revient dans la principale, intact.
    //
    // Le numero de la fenetre de `page` (0 : pas detachee).
    [[nodiscard]] std::uint32_t windowIdOf(const ui::Widget* page) const;
    // Le titre de l'onglet de la fenetre `windowId` ("" : pas une fenetre detachee).
    [[nodiscard]] std::string titleOfWindow(std::uint32_t windowId) const;
    // La page detachee de titre `title` (comme findByTitle), pour y chercher un
    // outil ou une ligne (les scripts) ; nulle sinon.
    [[nodiscard]] ui::Widget* pageByTitle(std::string_view title) const;
    // Montrer la fenetre `windowId` (0 : la principale) : restauree, devant.
    void raiseWindow(std::uint32_t windowId);
    // Un evenement de la fenetre PRINCIPALE pendant que la question attend dans
    // une fenetre detachee : il n'y fait rien ; un clic ou une touche amene cette
    // fenetre devant. Vrai : il est pris (App ne le passe pas aux ecrans).
    bool holdMainEvent(const ui::InputEvent& e);
    // La fenetre principale quand la question attend dans une fenetre detachee :
    // son voile, et ou elle attend (App::frame, apres le dessin des ecrans).
    void paintOverMain(gfx::IRenderer& r, gfx::Size surface);
    // La fenetre du systeme `windowId` (0 ou inconnue : nulle) : le parent de
    // l'explorateur de fichiers ouvert depuis un dialogue de cette fenetre.
    [[nodiscard]] SDL_Window* systemWindow(std::uint32_t windowId) const;

private:
    struct Window;
    struct Impl;

    Window* byPage(const ui::Widget* page) const;
    Window* byId(std::uint32_t windowId) const;
    // Un evenement dans une fenetre : sa page, puis les raccourcis de l'appli
    // (Ctrl+Z, Ctrl+Y, Ctrl+S, Ctrl+H) qu'elle n'a pas pris ; F11, F12 pour elle.
    void deliver(Window& w, const ui::InputEvent& e);
    // Dessine une fenetre : sa page, ses dialogues (lot API 8), le voile quand la
    // question attend ailleurs ; `capture` : la relire avant de la presenter.
    bool renderWindow(Window& w, const std::string* capture, std::string* error);
    // Les titres (celui de la fenetre principale suit le projet, son " *") et
    // l'icone (celle du projet quand il en a une).
    void refreshDecorations();
    // Ce qui a ete demande pendant un evenement ou un dessin, fait des qu'on en
    // est sorti : rendre une page, fermer une orpheline.
    void settle();
    // Retire la fenetre `index` ; `callOwner` : rendre la page par giveBack
    // (sinon elle est detruite avec la fenetre - son proprietaire n'est plus).
    void returnWindow(std::size_t index, bool callOwner);
    // L'ecran qui avait les pages `ownerId` quitte la pile : elles rentrent
    // avant qu'il soit detruit.
    void ownerLeaving(const std::string& ownerId);

    std::unique_ptr<Impl> impl_;
};

} // namespace app
