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
//  CODE (l'identifiant du script ou de la fonction), pas son texte : taper,
//  supprimer, joindre des lignes au-dessus, Annuler, Retablir, Migrer ce code le
//  gardent sur sa ligne (shiftLines). Le projet se reconnait a son document (pas
//  a son adresse) : un autre document, meme a la meme place, vide la liste.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"     // hmi::DocumentPtr
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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
    // simulation le donnent ; il sert a refuser un script C ou C++ et a nommer un code. 1.13.0 : reconnu par son
    // document (un lien faible), pas par son adresse - un projet ferme puis un autre ouvert a la meme place : ils partent.
    void bindProject(const hmi::DocumentPtr& doc);
    [[nodiscard]] const hmi::Project* project() const noexcept;
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
    // Le code a change : les lignes 1..prefix sont restees, celles a partir de `from` (1 = la premiere, comptee dans
    // l'ancien texte) aussi, decalees de `delta` ; entre les deux, la partie changee (supprimee, remplacee, jointe).
    // Un point du bas glisse avec sa ligne ; un point de la partie changee reste dans ce qu'il en reste (sa premiere
    // ligne si elle a disparu) ; deux points sur une meme ligne n'en font plus qu'un.
    void shiftLines(HmiDebugTarget code, std::uint32_t prefix, std::uint32_t from, int delta);
    // Le meme calcul, d'un texte a l'autre (la frappe, Annuler, Retablir, Migrer ce code).
    void followText(HmiDebugTarget code, const std::string& before, const std::string& after);
    // Ce que l'utilisateur a change (le moteur relit) ; ce qui a change tout court (les marges suivent).
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::uint64_t stateRevision() const noexcept { return state_; }

    // ---- la simulation qui tourne (le moteur de l'IHM ; nul : aucune) ----
    void setRuntime(hmi::Runtime* rt);
    [[nodiscard]] hmi::Runtime* runtime() const noexcept { return runtime_; }
    // Les passages, les lignes, les notes du moteur ; la pause. 1.13.0 : une NOUVELLE pause (apres un pas, Continuer)
    // revient au niveau 0 - la ligne d'arret, pas le niveau choisi dans la pile d'avant.
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
    std::weak_ptr<const hmi::Document> owner_;
    bool                              bound_{false};
    hmi::Runtime*                     runtime_{nullptr};
    std::size_t                       level_{0};
    std::uint64_t                     pauses_{0};     // les pauses du moteur deja vues (Runtime::pauseCount)
    std::map<std::pair<hmi::Id, hmi::Id>, std::string> followed_;   // le dernier texte suivi de chaque code (followText)
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
    // Le code montre a ete recharge : un autre code, la reference repart d'ici ; le meme (Annuler, Retablir, Migrer
    // ce code), ses points suivent leurs lignes d'un texte a l'autre, comme a la frappe.
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

// Le dialogue de la condition d'un point (pose au besoin) : le champ (lu a la frappe : une condition qui ne se lit
// pas se dit), l'apercu de la condition d'avant ; apres Valider, l'apercu de la nouvelle (« Maintenant, c'est vrai
// (P = 3.2) », « Sans condition : il s'arretera a chaque passage »).
void askBreakpointCondition(HmiDebugTarget code, std::uint32_t line, const std::function<void(const std::string&, bool)>& say,
                            const std::string& lineText = {});
// L'apercu de la condition d'un point de `code` dans la simulation qui tourne : en pause dans ce code (la ligne
// d'arret), sa valeur ; ailleurs, ses noms juges dans ce code (ses VAR, ses parametres) - elle sera essayee au passage.
// Une condition qui ne rend pas un BOOL le dit (le point se desarmera).
[[nodiscard]] std::string conditionPreview(HmiDebugTarget code, const std::string& condition);

} // namespace app
