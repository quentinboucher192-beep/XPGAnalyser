#include "HmiCommands.hpp"
#include "../core/CallTrail.hpp"   // 1.10.2 (CR) : les commandes du document, dans le journal interne

#include <algorithm>
#include <unordered_map>

namespace hmi {

namespace {

// Ce qui differe entre deux listes : l'ordre des deux cotes, et les elements
// absents d'un cote ou differents. Les elements identiques ne sont pas gardes.
template <class T>
ListDelta<T> diff(const std::vector<T>& before, const std::vector<T>& after) {
    ListDelta<T> d;
    d.orderBefore.reserve(before.size());
    d.orderAfter.reserve(after.size());
    std::unordered_map<Id, const T*> inBefore, inAfter;
    inBefore.reserve(before.size());
    inAfter.reserve(after.size());
    for (const auto& x : before) { d.orderBefore.push_back(x.id); inBefore.emplace(x.id, &x); }
    for (const auto& x : after)  { d.orderAfter.push_back(x.id);  inAfter.emplace(x.id, &x); }
    for (const auto& x : before) {
        const auto it = inAfter.find(x.id);
        if (it == inAfter.end() || !(*it->second == x)) d.before.push_back(x);
    }
    for (const auto& x : after) {
        const auto it = inBefore.find(x.id);
        if (it == inBefore.end() || !(*it->second == x)) d.after.push_back(x);
    }
    return d;
}

// La liste d'un cote, refaite depuis la liste courante : les elements gardes
// par la difference, les autres repris tels quels. Rend false si un element
// qui aurait du etre la n'y est pas - la pile a ete court-circuitee.
template <class T>
bool rebuild(const ListDelta<T>& d, bool after, const std::vector<T>& current, std::vector<T>& out) {
    const auto& order   = after ? d.orderAfter : d.orderBefore;
    const auto& changed = after ? d.after : d.before;
    std::unordered_map<Id, const T*> kept, now;
    kept.reserve(changed.size());
    now.reserve(current.size());
    for (const auto& x : changed) kept.emplace(x.id, &x);
    for (const auto& x : current) now.emplace(x.id, &x);
    std::vector<T> result;
    result.reserve(order.size());
    for (const Id id : order) {
        if (const auto k = kept.find(id); k != kept.end()) { result.push_back(*k->second); continue; }
        const auto n = now.find(id);
        if (n == now.end()) return false;
        result.push_back(*n->second);
    }
    out = std::move(result);
    return true;
}

core::Status incoherent(const std::string& what) {
    return core::fail(core::ErrorCode::OutOfRange,
                      what + " ne correspond plus \xC3\xA0 ce que la commande a modifi\xC3\xA9 : commande ignor\xC3\xA9" "e");
}

} // namespace

// ------------------------------------------------------------ ViewCommand ---
ViewCommand::ViewCommand(DocumentPtr doc, View before, View after, std::string label,
                         std::string mergeKey)
    : doc_(std::move(doc)), viewId_(before.id), label_(std::move(label)),
      mergeKey_(std::move(mergeKey)) {
    objects_ = diff(before.objects, after.objects);
    before.objects.clear();
    after.objects.clear();
    shellBefore_ = std::move(before);
    shellAfter_  = std::move(after);
}

core::Status ViewCommand::put(bool after) {
    auto* target = doc_->project.view(viewId_);
    if (!target)
        return core::fail(core::ErrorCode::OutOfRange,
                          "la vue " + std::to_string(viewId_) + " n'existe plus : commande ignor\xC3\xA9" "e");
    std::vector<Object> objects;
    if (!rebuild(objects_, after, target->objects, objects)) return incoherent("la vue " + target->name);
    *target = after ? shellAfter_ : shellBefore_;
    target->objects = std::move(objects);
    doc_->touched(viewId_);
    return core::ok();
}

std::string ViewCommand::objectsSummary() const {
    const auto& list = objects_.after.empty() ? objects_.before : objects_.after;
    if (list.empty()) return {};
    if (list.size() == 1) return list.front().name;
    return std::to_string(list.size()) + " objets";
}

core::Status ViewCommand::execute() { return put(true); }
core::Status ViewCommand::undo()    { return put(false); }

bool ViewCommand::mergeableWith(const core::ICommand& other) const {
    if (mergeKey_.empty()) return false;
    const auto* o = dynamic_cast<const ViewCommand*>(&other);
    return o && o->doc_ == doc_ && o->viewId_ == viewId_ && o->mergeKey_ == mergeKey_;
}

void ViewCommand::mergeFrom(const core::ICommand& other) {
    // Le depart reste celui de la premiere, l'arrivee devient celle de la
    // derniere : un glisser entier s'annule d'un coup. La pile appelle ceci
    // juste apres avoir execute `other` : la vue courante EST son arrivee.
    const auto& o = static_cast<const ViewCommand&>(other);
    const auto* now = doc_->project.view(viewId_);
    if (!now) return;
    std::vector<Object> middle, start;
    if (!rebuild(o.objects_, false, now->objects, middle) || !rebuild(objects_, false, middle, start))
        return;   // impossible tant que tout passe par la pile ; ne rien casser
    objects_     = diff(start, now->objects);
    shellAfter_  = o.shellAfter_;
}

// --------------------------------------------------------- ProjectCommand ---
ProjectCommand::ProjectCommand(DocumentPtr doc, Project before, Project after, std::string label, std::string mergeKey)
    : doc_(std::move(doc)), views_(diff(before.views, after.views)), nextId_(std::max(before.nextId, after.nextId)),
      label_(std::move(label)), mergeKey_(std::move(mergeKey)) {
    before.views.clear();
    after.views.clear();
    before.nextId = after.nextId = 0;      // le compteur ne compte pas : il ne recule jamais
    if (!(before == after)) {
        shellBefore_ = std::move(before);
        shellAfter_ = std::move(after);
    }
}

core::Status ProjectCommand::put(bool after) {
    std::vector<View> views;
    if (!rebuild(views_, after, doc_->project.views, views)) return incoherent("la liste des vues");
    const Id next = std::max(doc_->project.nextId, nextId_);   // jamais de reutilisation
    if (shellAfter_) doc_->project = after ? *shellAfter_ : *shellBefore_;
    doc_->project.views  = std::move(views);
    doc_->project.nextId = next;
    doc_->touched(kNoId);
    return core::ok();
}

core::Status ProjectCommand::execute() { return put(true); }
core::Status ProjectCommand::undo()    { return put(false); }

bool ProjectCommand::mergeableWith(const core::ICommand& other) const {
    if (mergeKey_.empty()) return false;
    const auto* o = dynamic_cast<const ProjectCommand*>(&other);
    // Seulement ce qui ne touche pas aux vues : une saisie dans un script.
    return o && o->doc_ == doc_ && o->mergeKey_ == mergeKey_ && views_.before.empty() && views_.after.empty()
        && o->views_.before.empty() && o->views_.after.empty() && views_.orderBefore == views_.orderAfter
        && o->views_.orderBefore == o->views_.orderAfter;
}

void ProjectCommand::mergeFrom(const core::ICommand& other) {
    // Le depart reste le notre, l'arrivee devient celle de l'autre.
    const auto& o = static_cast<const ProjectCommand&>(other);
    if (o.shellAfter_) {
        if (!shellBefore_) shellBefore_ = o.shellBefore_;
        shellAfter_ = o.shellAfter_;
    }
    nextId_ = std::max(nextId_, o.nextId_);
}

// --------------------------------------------------------------- fabriques ---
core::CommandPtr changeView(const DocumentPtr& doc, Id view, std::string label, const ViewChange& fn,
                            std::string mergeKey) {
    if (!doc) return nullptr;
    const auto* current = doc->project.view(view);
    if (!current) return nullptr;
    View before = *current;
    View after = before;
    fn(doc->project, after);
    // 1.11.18 (lot 3) : une declaration copiee (une instance, un script dupliques) recoit un identifiant neuf.
    uniqueDeclarationIds(doc->project, after);
    if (after == before) return nullptr;
    return std::make_unique<ViewCommand>(doc, std::move(before), std::move(after), std::move(label),
                                         std::move(mergeKey));
}

core::CommandPtr changeProject(const DocumentPtr& doc, std::string label, const ProjectChange& fn, std::string mergeKey) {
    if (!doc) return nullptr;
    Project before = doc->project;
    Project after = before;
    fn(after);
    // 1.11.18 (lot 3) : une declaration copiee (une vue, un script, une fonction dupliques, un
    // paquet importe) recoit un identifiant neuf : chacune garde le sien, unique.
    uniqueDeclarationIds(after);
    // Les identifiants attribues pendant la modification sont definitivement
    // pris, meme si la commande est annulee ensuite.
    doc->project.nextId = std::max(doc->project.nextId, after.nextId);
    if (after == before) return nullptr;
    XPG_TRACE(Document, "changeProject : %s", label.c_str());   // 1.10.2 (CR)
    return std::make_unique<ProjectCommand>(doc, std::move(before), std::move(after), std::move(label), std::move(mergeKey));
}

} // namespace hmi
