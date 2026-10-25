// =============================================================================
//  app/hmi/HmiDebugSession.hpp - 1.13.0 (lot J2) : les points d'arret des
//  scripts IHM, pour la seance
// -----------------------------------------------------------------------------
//  OU VIVENT LES POINTS D'ARRET : ici, dans la seance de l'editeur - pas dans
//  le fichier du projet (rien a enregistrer, rien a migrer : un projet d'avant
//  s'ouvre tel quel). Fermer le sous-onglet Debogage ne les enleve pas, arreter
//  la simulation non plus ; ouvrir un autre projet, si (comme les points
//  d'arret de l'automate, SimulationHost).
//
//  LES MARGES des editeurs de code (les scripts, les fonctions) les posent et
//  les montrent (HmiBreakpointMargin) ; la simulation de l'IHM les donne a son
//  moteur (hmi::Runtime::setBreakpoints) et en rapporte ce qu'il en a compris :
//  les passages, la ligne ou il s'arrete, la note (pullFrom). Un point suit son
//  CODE (l'identifiant du script ou de la fonction), pas son texte : taper
//  au-dessus le garde a sa place (shiftLines).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace app {

// Le code d'un editeur : un script (general, de vue) ou une fonction IHM ; les deux kNoId : aucun.
struct HmiDebugTarget {
    hmi::Id script{hmi::kNoId}, function{hmi::kNoId};
    [[nodiscard]] bool empty() const noexcept { return script == hmi::kNoId && function == hmi::kNoId; }
    bool operator==(const HmiDebugTarget&) const = default;
};

class HmiDebugSession {
public:
    static HmiDebugSession& instance();

    // Le projet dont ce sont les points (un autre : ils partent). Les volets qui montrent du code et la
    // simulation le donnent ; il sert a refuser un script C ou C++ et a nommer un code.
    void bindProject(const hmi::Project* project);
    [[nodiscard]] const hmi::Project* project() const noexcept { return owner_; }
    [[nodiscard]] const std::vector<hmi::DebugBreakpoint>& breakpoints() const noexcept { return list_; }
    // Poser (faux et `why` : refuse - un script C, une ligne inconnue) ; enlever ; poser ou enlever (le clic
    // dans la marge : vrai, pose ; faux, enleve ou refuse).
    std::uint32_t add(HmiDebugTarget code, std::uint32_t line, std::string condition = {}, std::string* why = nullptr);
    bool toggle(HmiDebugTarget code, std::uint32_t line, std::string* why = nullptr);
    bool remove(std::uint32_t id);
    bool setEnabled(std::uint32_t id, bool on);
    bool setCondition(std::uint32_t id, std::string condition);
    void clear();
    void disableAll();
    // Le point de cette ligne de ce code (son id ; 0 : aucun) ; son rang dans la liste (1 = le premier).
    [[nodiscard]] std::uint32_t at(HmiDebugTarget code, std::uint32_t line) const;
    [[nodiscard]] std::size_t rankOf(std::uint32_t id) const;
    [[nodiscard]] const hmi::DebugBreakpoint* find(std::uint32_t id) const;
    // Le code a change : les points de ce code a partir de la ligne `from` (1 = la premiere) glissent de `delta`.
    void shiftLines(HmiDebugTarget code, std::uint32_t from, int delta);
    // Ce que l'utilisateur a change (le moteur relit) ; ce qui a change tout court (les marges suivent).
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::uint64_t stateRevision() const noexcept { return state_; }

    // ---- la simulation qui tourne (le moteur de l'IHM ; nul : aucune) ----
    void setRuntime(hmi::Runtime* rt);
    [[nodiscard]] hmi::Runtime* runtime() const noexcept { return runtime_; }
    // Les passages, les lignes, les notes du moteur ; la pause.
    void pullFrom(const hmi::Runtime& rt);
    // Le niveau de la pile choisi dans le volet Debogage (0 : la ligne d'arret ; 1 : celui d'en dessous...).
    void setLevel(std::size_t level);
    [[nodiscard]] std::size_t level() const noexcept { return level_; }
    // En pause : le niveau montre (le code et sa ligne, 1 = la premiere) ; faux : pas en pause.
    [[nodiscard]] bool pausedAt(HmiDebugTarget& code, std::uint32_t& line, bool* top = nullptr) const;

    // Un point, une pause, une reprise : les marges et le volet suivent.
    const core::SignalPtr<> changed = core::Signal<>::create();

private:
    void touch(bool user);
    std::vector<hmi::DebugBreakpoint> list_;
    std::uint32_t                     nextId_{1};
    std::uint64_t                     revision_{1}, state_{1};
    const hmi::Project*               owner_{nullptr};
    hmi::Runtime*                     runtime_{nullptr};
    std::size_t                       level_{0};
};

// LA MARGE D'UN EDITEUR DE CODE : la colonne des points d'arret (un clic pose ou enleve un rond rouge,
// Maj+clic l'active ou le desactive, F9 au curseur), le menu du clic droit (Point d'arret, Condition...,
// Desactiver, Retirer les points de ce code), la ligne en pause surlignee. Le volet qui la porte dit quel
// code il montre (`target`) et appelle edited() quand l'utilisateur tape (les points suivent leurs lignes).
class HmiBreakpointMargin {
public:
    using Target = std::function<HmiDebugTarget()>;
    using Say = std::function<void(const std::string&, bool warning)>;
    void attach(ui::Widget& owner, ui::MultiLineText& editor, Target target, Say say);
    // Le code montre a change (un autre script, Annuler) : la reference de la frappe repart d'ici.
    void reset();
    // L'utilisateur a tape : les points sous la ligne changee glissent avec elle.
    void edited();
    // Les ronds et la ligne en pause, refaits (rien ne change si rien n'a change).
    void refresh();
    // Le clic droit (pour les essais) : ouvrir le menu de la ligne (0 = la premiere) et choisir une entree.
    void openMenu(std::size_t line0, gfx::Point at);
    void choose(int item);
    [[nodiscard]] ui::PopupMenu* menu() const noexcept { return menu_; }
    // La condition de la ligne (1 = la premiere) : le dialogue (le point pose d'abord au besoin).
    void askCondition(std::uint32_t line);

private:
    ui::Widget*        owner_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    Target             target_;
    Say                say_;
    ui::PopupMenu*     menu_{nullptr};
    std::uint32_t      menuLine_{0};
    std::string        shown_;            // ce que la marge montre deja (une signature)
    std::string        baseline_;         // le texte d'avant la frappe
    HmiDebugTarget     baselineOf_{};
    core::ConnectionScope links_;
};

// Le dialogue de la condition d'un point (pose au besoin) : le champ, l'apercu en direct (« Maintenant, c'est
// vrai (P = 3.2) », « Sans condition : il s'arretera a chaque passage »).
void askBreakpointCondition(HmiDebugTarget code, std::uint32_t line, const std::function<void(const std::string&, bool)>& say,
                            const std::string& lineText = {});
// L'apercu d'une condition dans la simulation qui tourne (en pause : dans la portee de la ligne d'arret).
[[nodiscard]] std::string conditionPreview(const std::string& condition);

} // namespace app
