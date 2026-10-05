// =============================================================================
//  app/hmi/HmiSimVarTree.hpp - 1.11.5 : les variables IHM et les variables API
//                              de la simulation, en arbre, avec le forcage
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : « ajouter un onglet avec les variables IHM,
//  variables API avec treeview dans les 2 onglets, dans les 3 onglets ajouter une
//  recherche, garder le principe de forcage ».
//
//  UN ARBRE A TOUTE PROFONDEUR. Chaque variable (une case : Four1.Vannes[1].Position)
//  se range sous ses noeuds (Four1, Vannes, [1]) ; un noeud se replie d'un clic, il
//  dit combien de valeurs il contient. La recherche (mots ET, "phrase", -exclu, comme
//  toutes les listes) garde les valeurs trouvees et leurs noeuds, ouverts.
//
//  LE FORCAGE, COMME LES ESCLAVES SIMULES. La case Forcer tient la variable a sa
//  valeur du moment (decocher la rend libre) ; un double-clic sur la valeur ouvre un
//  champ : Entree la force a ce qu'on a tape, Echap annule. Une variable forcee se
//  voit (sa valeur en orange, « forcee »). Ce que veut dire forcer, et ce qui se
//  passe vraiment, c'est l'hote qui le dit (Hooks) : la simulation de l'automate
//  (sim::Runtime::force) pour l'onglet API, le moteur de l'IHM (forceVariable) pour
//  l'onglet IHM.
// =============================================================================
#pragma once

#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"
#include "../../core/Signal.hpp"
#include "../../sim/Value.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace app {

class HmiSimVarTree final : public ui::Widget {
public:
    struct Leaf {
        std::string path;   // Four1.Vannes[1].Position
        std::string type;   // INT
    };
    struct Hooks {
        std::function<std::optional<sim::Value>(const std::string& path)> read;
        std::function<bool(const std::string& path)>                      forced;
        // Forcer : `text` vide - a la valeur du moment ; sinon la valeur tapee.
        std::function<bool(const std::string& path, const std::string& text, std::string* why)> force;
        std::function<bool(const std::string& path)>                      unforce;
        // Le type d'un noeud (T_Four) ; vide : rien a dire.
        std::function<std::string(const std::string& path)>               nodeType;
    };
    struct Row {
        int         node{-1};
        std::string label, path, type;
        int         depth{0};
        bool        leaf{true};
        bool        open{false};
        std::size_t count{0};
    };

    HmiSimVarTree(std::string id, std::string what);
    void setHooks(Hooks h) { hooks_ = std::move(h); }
    // Les variables (refaire l'arbre : quand le projet ou la simulation change).
    void setLeaves(std::vector<Leaf> leaves);
    [[nodiscard]] std::size_t leafCount() const noexcept { return leaves_; }
    // Ce que dit la liste vide (« la simulation de l'automate n'est pas lancee »).
    void setEmptyText(std::string text) { emptyText_ = std::move(text); invalidate(); }

    // La recherche.
    void setSearch(const std::string& text);
    [[nodiscard]] ui::InputText* searchBox() noexcept { return search_; }
    // Un noeud ouvert ou replie (son chemin : Four1.Vannes).
    void setOpen(const std::string& path, bool open);
    [[nodiscard]] bool isOpen(const std::string& path) const;
    // Les lignes montrees.
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    // La valeur montree d'une variable (« - » : pas lue).
    [[nodiscard]] std::string valueText(const std::string& path) const;
    // Forcer (vide : a la valeur du moment), deforcer - comme la case et le champ.
    bool forcePath(const std::string& path, const std::string& text = {});
    bool unforcePath(const std::string& path);
    // Le champ de la valeur (un double-clic) : ouvert sur cette variable.
    bool openValueEditor(const std::string& path);
    [[nodiscard]] ui::InputText* valueEditor() noexcept;
    // Ce qu'a dit le dernier geste (« Four1.Temperature forcee a 80 »), pour la barre d'etat.
    const core::SignalPtr<const std::string&, bool> said = core::Signal<const std::string&, bool>::create();
    // Pour les scripts et les tests : ou est la ligne de ce chemin (faux : pas montree).
    [[nodiscard]] bool rowRect(const std::string& path, gfx::Rect& out) const;
    // Faire defiler jusqu'a la ligne de ce chemin (faux : pas montree, repliee ou filtree).
    bool reveal(const std::string& path);
    [[nodiscard]] gfx::Rect forceBox(const gfx::Rect& row) const;

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Node {
        std::string      label, path, type;
        int              parent{-1};
        std::vector<int> children;
        bool             leaf{false};
        int              depth{0};
        std::size_t      count{0};   // les valeurs dessous (une feuille : 1)
    };
    [[nodiscard]] gfx::Rect listArea() const;
    [[nodiscard]] int rowAt(gfx::Point p) const;
    void rebuildRows();
    void closeValueEditor(bool apply);
    void say(const std::string& text, bool error) { if (said) said->emit(text, error); }

    std::string                  what_;          // « IHM » ou « API »
    std::string                  emptyText_;
    Hooks                        hooks_;
    std::vector<Node>            nodes_;
    std::vector<int>             roots_;
    std::size_t                  leaves_{0};
    std::map<std::string, bool>  open_;          // les noeuds ouverts (ou replies) par l'utilisateur
    std::string                  query_;
    std::vector<Row>             rows_;
    float                        scroll_{0.f};
    ui::EdgeScrollBar            bar_;
    int                          hover_{-1};
    ui::InputText*               search_{nullptr};
    ui::InputText*               edit_{nullptr};
    std::string                  editPath_;
    core::ConnectionScope        links_;
};

// Un texte tape en valeur, selon le type de la valeur du moment (TRUE, 12, 1,5, 'texte', T#5s).
[[nodiscard]] std::optional<sim::Value> parseTypedValue(std::string text, const sim::Value* current);

} // namespace app
