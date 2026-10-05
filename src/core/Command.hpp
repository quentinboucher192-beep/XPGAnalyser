// =============================================================================
//  core/Command.hpp — Command pattern + undo/redo stack + action dispatch
// -----------------------------------------------------------------------------
//  Every user-visible mutation (rename a filter, reorder columns, collapse a
//  tree branch, change theme) is a Command. Menu items, toolbar buttons and
//  keyboard shortcuts all resolve to the same ActionId, so the toolbar and the
//  menu bar can never drift out of sync.
// =============================================================================
#pragma once

#include "Result.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual Status      execute() = 0;
    virtual Status      undo()    = 0;
    [[nodiscard]] virtual std::string label() const = 0;   // shown in Edit > Undo <label>
    [[nodiscard]] virtual bool        mergeableWith(const ICommand&) const { return false; }
    virtual void                      mergeFrom(const ICommand&) {}
};
using CommandPtr = std::unique_ptr<ICommand>;

// ---------------------------------------------------------------------------
//  LOT 19 : CE QUE L'HISTORIQUE MONTRE D'UNE COMMANDE.
//
//  Une commande sait se faire et se defaire ; elle ne sait ni QUAND ni OU on
//  l'a faite. La pile le note au moment ou elle l'empile : l'heure (celle du
//  PC, pour l'afficher), l'endroit ("IHM > Vues > Vue_Equipements") et une cle
//  pour y retourner, le cote du projet (API, IHM). Une commande fusionnee (la
//  frappe d'une rafale, un glisser) garde sa premiere heure, prend la
//  derniere, et compte combien de gestes elle porte.
// ---------------------------------------------------------------------------
struct CommandInfo {
    std::string   label;          // ce que la liste affiche (le libelle a l'empilage)
    std::string   place;          // l'endroit, en clair
    std::string   placeKey;       // pour y retourner ("hmi:vue:17", "doc:3"...)
    int           area{0};        // 0 : ailleurs, 1 : API, 2 : IHM
    std::int64_t  firstMs{0};     // l'heure du PC (ms depuis 1970) du premier geste
    std::int64_t  lastMs{0};      // et du dernier
    int           merged{1};      // combien de gestes elle porte
    std::uint64_t serial{0};      // son identite dans la liste (ne revient jamais)
};

// ---------------------------------------------------------------------------
//  LOT 20 : UN GESTE, UN PAS - MEME QUAND IL FAIT CENT CHOSES.
//
//  Coller douze lignes d'Excel, importer trois vues et leur symbole : chaque
//  ligne passe par les actions habituelles (et leurs controles), qui empilent
//  chacune leur commande ; mais Ctrl+Z doit tout reprendre d'un coup, et
//  l'historique ne montrer qu'une ligne. Entre CommandStack::beginGroup et
//  endGroup, les commandes s'executent et s'entassent ici ; endGroup empile le
//  tout comme UN pas (rien, si aucune n'a ete faite). Annuler defait les
//  parties de la derniere a la premiere, retablir les refait dans l'ordre.
// ---------------------------------------------------------------------------
class GroupCommand final : public ICommand {
public:
    explicit GroupCommand(std::string label) : label_(std::move(label)) {}
    Status execute() override {
        for (auto& c : parts_)
            if (auto r = c->execute(); !r) return r;
        return ok();
    }
    Status undo() override {
        Status last = ok();
        for (auto it = parts_.rbegin(); it != parts_.rend(); ++it)
            if (auto r = (*it)->undo(); !r) last = r;
        return last;
    }
    [[nodiscard]] std::string label() const override { return label_; }
    void setLabel(std::string l) { label_ = std::move(l); }
    void add(CommandPtr c) { parts_.push_back(std::move(c)); }
    [[nodiscard]] const std::vector<CommandPtr>& parts() const noexcept { return parts_; }
    [[nodiscard]] std::size_t size() const noexcept { return parts_.size(); }
    [[nodiscard]] bool empty() const noexcept { return parts_.empty(); }
    // La derniere partie (une frappe se fusionne dans la precedente).
    [[nodiscard]] ICommand* back() noexcept { return parts_.empty() ? nullptr : parts_.back().get(); }
    // Un groupe d'une seule commande s'empile comme elle : on la reprend.
    [[nodiscard]] CommandPtr takeFirst() {
        if (parts_.empty()) return nullptr;
        CommandPtr first = std::move(parts_.front());
        parts_.erase(parts_.begin());
        return first;
    }
private:
    std::string             label_;
    std::vector<CommandPtr> parts_;
};

class CommandStack {
public:
    struct Entry {
        CommandPtr                            command;
        CommandInfo                           info;
        std::chrono::steady_clock::time_point last{};   // le dernier geste (fusion)
    };
    // Complete ce que la pile sait d'une commande qu'on empile (l'endroit, le
    // cote) : c'est l'application qui le sait, pas la commande.
    using Describer = std::function<void(const ICommand&, CommandInfo&)>;

    explicit CommandStack(std::size_t depth = 256) : depth_(depth) {}

    Status push(CommandPtr cmd) { return push(std::move(cmd), CommandInfo{}); }
    Status push(CommandPtr cmd, CommandInfo info) {
        if (auto r = cmd->execute(); !r) return r;
        // Lot 20 : un groupe ouvert garde la commande ; il sera UN pas.
        if (group_) {
            if (auto* last = group_->back(); last && last->mergeableWith(*cmd)) last->mergeFrom(*cmd);
            else {
                if (group_->empty()) groupInfo_ = std::move(info);
                group_->add(std::move(cmd));
            }
            return ok();
        }
        return pushExecuted(std::move(cmd), std::move(info));
    }

    // ---- lot 20 : les groupes ------------------------------------------------
    //  beginGroup ouvre un groupe (un deuxieme beginGroup dans le premier ne
    //  fait que compter : le groupe du dehors l'emporte) ; endGroup le ferme et
    //  l'empile comme un seul pas. Rend le nombre de commandes qu'il porte
    //  (0 : rien n'a ete fait, rien n'est empile).
    void beginGroup(std::string label) {
        if (groupDepth_++ == 0) {
            group_ = std::make_unique<GroupCommand>(std::move(label));
            groupInfo_ = CommandInfo{};
            ++groupSerial_;
        }
    }
    std::size_t endGroup() {
        if (groupDepth_ == 0) return 0;
        if (--groupDepth_ > 0) return group_ ? group_->size() : 0;
        auto g = std::move(group_);
        if (!g || g->empty()) return 0;
        const std::size_t n = g->size();
        // Une seule commande : elle-meme (l'historique garde son libelle et sa
        // fusion) ; plusieurs : le groupe, sous le libelle du geste.
        CommandInfo info = std::move(groupInfo_);
        if (n == 1) {
            (void)pushExecuted(g->takeFirst(), std::move(info));
        } else {
            info.label.clear();
            (void)pushExecuted(std::move(g), std::move(info));
        }
        return n;
    }
    [[nodiscard]] bool grouping() const noexcept { return groupDepth_ > 0; }
    // Le libelle du groupe ouvert, connu seulement a la fin du geste ("Coller
    // depuis Excel : 12 variables").
    void setGroupLabel(std::string label) { if (group_) group_->setLabel(std::move(label)); }
    [[nodiscard]] std::size_t groupSize() const noexcept { return group_ ? group_->size() : 0; }
    // Change a chaque groupe ouvert : ce qui ne doit se dire qu'une fois par geste.
    [[nodiscard]] std::uint64_t groupSerial() const noexcept { return groupSerial_; }

private:
    // Empile une commande DEJA faite (push, ou un groupe qui se ferme).
    Status pushExecuted(CommandPtr cmd, CommandInfo info) {
        const auto now = std::chrono::steady_clock::now();
        // LA FUSION A UNE FENETRE (lot 19). Deux frappes dans le meme script
        // fusionnaient pour toujours : une matinee de saisie devenait UN pas,
        // et Ctrl+Z effacait tout. Passe la fenetre, une nouvelle rafale.
        // Et jamais juste apres une annulation, un retablissement ou un
        // enregistrement : fusionner dans le pas enregistre laissait la taille
        // de la pile inchangee, donc "rien n'a change" alors que si.
        const bool inWindow = mergeWindow_.count() <= 0
                           || (!done_.empty() && now - done_.back().last <= mergeWindow_);
        if (!sealed_ && inWindow && !done_.empty() && done_.back().command->mergeableWith(*cmd)) {
            done_.back().command->mergeFrom(*cmd);
            done_.back().info.lastMs = wallMs();
            done_.back().last = now;
            ++done_.back().info.merged;
        } else {
            if (info.label.empty()) info.label = cmd->label();
            if (describe_) describe_(*cmd, info);
            info.firstMs = info.lastMs = wallMs();
            info.merged = 1;
            info.serial = ++serial_;
            done_.push_back(Entry{std::move(cmd), std::move(info), now});
            if (done_.size() > depth_) {
                done_.pop_front();
                // LA MARQUE SUIT LA PILE QUI GLISSE. Quand la plus ancienne
                // commande tombe, le point de sauvegarde recule d'un cran ; s'il
                // tombe a son tour, on ne peut PLUS savoir si l'etat courant est
                // celui qui a ete enregistre, et isModified doit alors repondre
                // oui. Dire "rien n'a change" quand on l'ignore est la seule
                // reponse dont on ne se remet pas.
                if (savedDepth_ >= 0) --savedDepth_;
                ++dropped_;
            }
        }
        sealed_ = false;
        undone_.clear();
        ++revision_;
        return ok();
    }

public:
    Status undo() {
        if (done_.empty()) return fail(ErrorCode::OutOfRange, "nothing to undo");
        auto e = std::move(done_.back()); done_.pop_back();
        auto r = e.command->undo();
        undone_.push_back(std::move(e));
        sealed_ = true;
        ++revision_;
        return r;
    }
    Status redo() {
        if (undone_.empty()) return fail(ErrorCode::OutOfRange, "nothing to redo");
        auto e = std::move(undone_.back()); undone_.pop_back();
        auto r = e.command->execute();
        done_.push_back(std::move(e));
        sealed_ = true;
        ++revision_;
        return r;
    }
    [[nodiscard]] bool canUndo() const noexcept { return !done_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !undone_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return done_.empty() ? "" : done_.back().info.label; }
    [[nodiscard]] std::string redoLabel() const { return undone_.empty() ? "" : undone_.back().info.label; }
    void clear() noexcept {
        done_.clear(); undone_.clear(); savedDepth_ = 0; sealed_ = true; dropped_ = 0; ++revision_;
        group_.reset(); groupDepth_ = 0;   // lot 20 : un groupe ouvert part avec le projet
    }

    // ---- ou en etait-on au dernier enregistrement ------------------------
    //
    // canUndo() ne repond pas a cette question : il dit "quelque chose a ete
    // fait", pas "quelque chose est en attente". Annuler jusqu'au point de
    // depart laisse canUndo() a faux alors que le fichier est intact, et
    // enregistrer ne le remet pas a faux alors que plus rien n'est en attente.
    void markSaved() noexcept { savedDepth_ = static_cast<std::ptrdiff_t>(done_.size()); sealed_ = true; ++revision_; }
    // Lot 15 : le document ne correspond a aucun enregistrement (des
    // modifications rechargees apres un arret brutal) - a enregistrer.
    void markUnsaved() noexcept { savedDepth_ = -1; sealed_ = true; ++revision_; }

    [[nodiscard]] bool isModified() const noexcept {
        // LE TEST "savedDepth_ < 0" SERAIT REDONDANT et il a ete retire. Une
        // taille est toujours positive, donc elle ne peut jamais egaler -1 :
        // des que le point de sauvegarde est perdu, la comparaison seule
        // repond deja oui pour toujours. Une passe de mutations l'a montre -
        // la clause pouvait disparaitre sans qu'aucun test ne bronche, ce qui
        // est la definition du code mort.
        return static_cast<std::ptrdiff_t>(done_.size()) != savedDepth_;
    }

    // ---- lot 19 : l'historique ---------------------------------------------
    //  done() : du plus ancien au plus recent ; undone() : le prochain a
    //  retablir est a la FIN. La frise est done() puis undone() a l'envers.
    [[nodiscard]] const std::deque<Entry>& done() const noexcept { return done_; }
    [[nodiscard]] const std::deque<Entry>& undone() const noexcept { return undone_; }
    // Le point enregistre, compte dans la frise (0 : l'ouverture ; -1 : perdu).
    [[nodiscard]] std::ptrdiff_t savedDepth() const noexcept { return savedDepth_; }
    // Change a chaque mouvement : une liste qui le garde sait si elle est a jour.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Les pas tombes du bas de la pile (trop profonde) depuis l'ouverture.
    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }
    void setMergeWindow(std::chrono::milliseconds w) noexcept { mergeWindow_ = w; }
    void setDescriber(Describer d) { describe_ = std::move(d); }
    // Rend le libelle et l'endroit d'une entree, ou nul.
    [[nodiscard]] const Entry* find(std::uint64_t serial) const noexcept {
        for (const auto& e : done_) if (e.info.serial == serial) return &e;
        for (const auto& e : undone_) if (e.info.serial == serial) return &e;
        return nullptr;
    }
    [[nodiscard]] static std::int64_t wallMs() noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    }

private:
    std::deque<Entry>         done_, undone_;
    std::size_t               depth_;
    std::ptrdiff_t            savedDepth_{0};   // -1 : le point de sauvegarde est perdu
    std::chrono::milliseconds mergeWindow_{0};  // 0 : sans limite (le comportement d'avant)
    Describer                 describe_;
    std::uint64_t             serial_{0};
    std::uint64_t             revision_{0};
    std::size_t               dropped_{0};
    bool                      sealed_{false};   // la prochaine commande ne fusionne pas
    // Lot 20 : le groupe ouvert (beginGroup), sa profondeur, l'info de sa premiere commande.
    std::unique_ptr<GroupCommand> group_;
    int                           groupDepth_{0};
    CommandInfo                   groupInfo_;
    std::uint64_t                 groupSerial_{0};
};

// ---------------------------------------------------------------------------
//  LOT 20 : OUVRIR UN GROUPE SANS CONNAITRE LA PILE. Un volet n'a que sa
//  fonction Apply ; l'application enregistre sa pile a sa creation, et le
//  volet ouvre une portee le temps du geste. Sans pile enregistree (un essai
//  qui empile lui-meme), la portee ne fait rien.
// ---------------------------------------------------------------------------
class CommandGroupScope {
public:
    explicit CommandGroupScope(std::string label) : stack_(registered()) {
        if (stack_) stack_->beginGroup(std::move(label));
    }
    ~CommandGroupScope() { close(); }
    CommandGroupScope(const CommandGroupScope&) = delete;
    CommandGroupScope& operator=(const CommandGroupScope&) = delete;
    // Ferme le groupe avant la fin de la portee ; rend ce qu'il porte.
    std::size_t close() {
        if (!stack_) return 0;
        CommandStack* s = stack_;
        stack_ = nullptr;
        return s->endGroup();
    }
    void setLabel(std::string label) { if (stack_) stack_->setGroupLabel(std::move(label)); }
    static void registerStack(CommandStack* s) noexcept { registered() = s; }
    [[nodiscard]] static CommandStack* stack() noexcept { return registered(); }
private:
    static CommandStack*& registered() noexcept {
        static CommandStack* s = nullptr;
        return s;
    }
    CommandStack* stack_;
};

// --- Action table ----------------------------------------------------------
using ActionId = std::string_view;   // "file.open", "view.theme.dark", "analyze.run"

struct Action {
    ActionId                        id;
    std::string                     label;
    std::string                     shortcut;      // "Ctrl+O"
    std::function<bool()>           enabled  = [] { return true; };
    std::function<bool()>           checked  = [] { return false; };
    std::function<CommandPtr()>     make;          // null => fire-and-forget
    std::function<void()>           invoke;        // used when make == nullptr
};

class ActionRegistry {
public:
    void add(Action a) { actions_.emplace(a.id, std::move(a)); }
    [[nodiscard]] const Action* find(ActionId id) const {
        auto it = actions_.find(id);
        return it == actions_.end() ? nullptr : &it->second;
    }
    Status trigger(ActionId id, CommandStack& stack) const {
        const auto* a = find(id);
        if (!a) return fail(ErrorCode::InvalidArgument, std::string("unknown action ") + std::string(id));
        // `enabled` may be an empty std::function when the Action was
        // aggregate-initialised with {}; an absent predicate means "enabled".
        if (a->enabled && !a->enabled()) return fail(ErrorCode::Cancelled, "action disabled");
        if (a->make) return stack.push(a->make());
        if (a->invoke) { a->invoke(); return ok(); }
        return fail(ErrorCode::NotImplemented, std::string(id));
    }
private:
    std::unordered_map<ActionId, Action> actions_;
};

} // namespace core
