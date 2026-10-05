#pragma once
// =============================================================================
//  app/tutorial/UiDriver.hpp - jouer l'appli a la souris et au clavier (1.11, T1)
// -----------------------------------------------------------------------------
//  Tire de ScriptRunner : ce que les sessions rejouees et le lecteur des
//  tutoriels font tous deux.
//   - SIMULER L'ENTREE : les evenements passent par MenuManager::HandleEvent,
//     comme ceux que SDL traduit (apres la bulle des nouveautes, comme
//     App::pumpEvents) ; la souris est suivie (moveTo envoie le delta).
//   - TROUVER UNE CIBLE : locate("genre:nom") rend le rectangle a l'ecran d'une
//     cible de tutoriel (CONCEPTION-T1 section 1.4) ; vide si elle n'est pas (ou
//     pas encore) la : l'appelant reessaie a l'image suivante, comme le Retry de
//     ScriptRunner.
//  ScriptRunner garde ses commandes ; il passe par ce pilote pour envoyer.
// =============================================================================

#include "../../platform/Geometry.hpp"
#include "../../platform/InputEvent.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ui { class Widget; }

namespace app {

class App;
class HmiEditor;

class UiDriver {
public:
    explicit UiDriver(App& app) : app_(app) {}

    // --- L'entree simulee ------------------------------------------------------
    void send(const ui::InputEvent& e);
    void moveTo(gfx::Point p, ui::KeyMods m = {});
    void click(gfx::Point p, ui::MouseButton b = ui::MouseButton::Left, int clicks = 1, ui::KeyMods m = {});
    void press(gfx::Point p, ui::KeyMods m = {});            // bouton gauche enfonce (debut d'un glisser)
    void release(ui::KeyMods m = {});                         // ... relache la ou est la souris
    // Un glisser en 8 pas ; release = faux : la souris reste enfoncee.
    void drag(gfx::Point a, gfx::Point b, ui::KeyMods m = {}, bool release = true);
    // Un clic dans la case, Ctrl+A, puis le texte (vide : la case se vide).
    void typeInto(gfx::Point cell, const std::string& text);
    void key(ui::Key k, ui::KeyMods m = {});
    void text(const std::string& t);                          // des lettres, sans clic
    [[nodiscard]] gfx::Point mouse() const noexcept { return mouse_; }
    // Une session : le nombre d'evenements envoyes (ScriptRunner remet son compteur).
    [[nodiscard]] long long sent() const noexcept { return sent_; }

    // --- Ou sont les choses ----------------------------------------------------
    // Tranche 5 : la variante de la derniere cible variante: trouvee ("" apres clearVariant).
    [[nodiscard]] const std::string& lastVariant() const noexcept { return lastVariant_; }
    void clearVariant() { lastVariant_.clear(); }
    [[nodiscard]] ui::Widget* top() const;          // l'ecran du dessus (dialogue compris)
    [[nodiscard]] ui::Widget* currentPage() const;  // l'onglet courant de l'espace de travail
    [[nodiscard]] HmiEditor*  currentEditor() const;

    // La cible d'un tutoriel ("bouton:Cr\xC3\xA9" "er", "outil:Compiler", "propriete:Nom",
    // "biblio:Vanne", "vue:320,190", "ecran:640,400"...). Vide : introuvable pour
    // l'instant. *why dit pourquoi (pour le verificateur).
    [[nodiscard]] std::optional<gfx::Rect> locate(std::string_view target, std::string* why = nullptr) const;
    // Tranche 23 (R111-15) : la meme recherche, SANS RIEN CHANGER (l'encadre la refait a chaque
    // image, meme pendant l'"A toi" de l'eleve) : ni defilement (arbre, propriete, biblio), ni
    // noeud deplie (alarmes), ni la derniere tuile ou variante visee. Hors de vue : vide.
    [[nodiscard]] std::optional<gfx::Rect> follow(std::string_view target) const;
    // "Ctrl+Z", "Maj+Tab", "Return", "F8" -> la touche (les noms de ScriptRunner).
    [[nodiscard]] static bool parseKey(const std::string& combo, ui::Key& key, ui::KeyMods& mods);
    // Les genres que locate connait deja (les autres : a venir, tranche 3).
    [[nodiscard]] static bool locates(std::string_view kind);

private:
    App&       app_;
    gfx::Point mouse_{};
    long long  sent_{0};
    mutable std::string lastKind_;   // le dernier genre vise par biblio: (variante: s'y rapporte)
    mutable std::string lastVariant_;   // tranche 5 : la derniere variante: trouvee (biblio.variante)
    mutable bool quiet_ = false;        // tranche 23 : dans follow() (locate ne change rien)
};

[[nodiscard]] inline gfx::Point centreOf(const gfx::Rect& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

} // namespace app
