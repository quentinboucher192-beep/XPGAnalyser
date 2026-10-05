#include "HmiTemplates.hpp"

#include <algorithm>

namespace hmi {

namespace {

const View* firstWithRole(const Project& p, std::string_view role) {
    for (const auto& v : p.views) if (v.role == role) return &v;
    return nullptr;
}

const View* band(const Project& p, const View& v, bool show, Id chosen, std::string_view role) {
    if (!show || v.role == role) return nullptr;             // un en-tete n'a pas d'en-tete
    const View* b = chosen != kNoId ? p.view(chosen) : firstWithRole(p, role);
    return b && b->role == role && b->id != v.id ? b : nullptr;
}

// Emprunter les calques, les objets, les scripts et les actions de vue de
// `from` ; les objets descendus de `dy`.
void borrow(View& into, const View& from, double dy) {
    into.layers.insert(into.layers.end(), from.layers.begin(), from.layers.end());
    for (auto o : from.objects) {
        if (dy != 0) o.setNumber("y", o.number("y") + dy);   // coordonnees absolues : groupes compris
        into.objects.push_back(std::move(o));
    }
}

} // namespace

// viewRoleLabel, viewRoleFromLabel, isTemplateRole : en ligne dans l'en-tete (lot 9).

std::vector<const View*> templateChain(const Project& p, const View& v, std::string* broken) {
    std::vector<const View*> chain;
    std::vector<Id> seen{v.id};
    Id next = v.templateView;
    while (next != kNoId) {
        const View* t = p.view(next);
        if (!t) {
            if (broken) *broken = "\xC3\xA9" "cran mod\xC3\xA8le " + std::to_string(next) + " introuvable";
            break;
        }
        if (std::find(seen.begin(), seen.end(), t->id) != seen.end()) {
            if (broken) *broken = "h\xC3\xA9ritage circulaire : " + t->name;
            break;
        }
        if (t->role != "modele") {
            if (broken) *broken = t->name + " n'est pas un \xC3\xA9" "cran mod\xC3\xA8le";
            break;
        }
        seen.push_back(t->id);
        chain.push_back(t);
        next = t->templateView;
    }
    std::reverse(chain.begin(), chain.end());           // le plus lointain d'abord
    return chain;
}

const View* headerOf(const Project& p, const View& v) { return band(p, v, v.showHeader, v.header, "entete"); }
const View* footerOf(const Project& p, const View& v) { return band(p, v, v.showFooter, v.footer, "pied"); }

bool inherits(const Project& p, const View& v) {
    return !templateChain(p, v).empty() || headerOf(p, v) || footerOf(p, v);
}

View compose(const Project& p, const View& v) {
    View out = v;
    const auto chain = templateChain(p, v);
    const View* head = headerOf(p, v);
    const View* foot = footerOf(p, v);
    if (chain.empty() && !head && !foot) return out;

    out.layers.clear();
    out.objects.clear();
    out.scripts.clear();
    out.actions.clear();
    // 1. Les modeles, du plus lointain au plus proche : dessous, et en premier.
    for (const View* t : chain) {
        borrow(out, *t, 0);
        out.scripts.insert(out.scripts.end(), t->scripts.begin(), t->scripts.end());
        out.actions.insert(out.actions.end(), t->actions.begin(), t->actions.end());
    }
    // 2. La vue.
    out.layers.insert(out.layers.end(), v.layers.begin(), v.layers.end());
    out.objects.insert(out.objects.end(), v.objects.begin(), v.objects.end());
    // 3. L'en-tete et le pied : par-dessus le contenu (des calques ajoutes a la
    //    fin), leurs scripts avant ceux de la vue.
    if (head) {
        borrow(out, *head, 0);
        out.scripts.insert(out.scripts.end(), head->scripts.begin(), head->scripts.end());
        out.actions.insert(out.actions.end(), head->actions.begin(), head->actions.end());
    }
    if (foot) {
        borrow(out, *foot, static_cast<double>(v.height - foot->height));
        out.scripts.insert(out.scripts.end(), foot->scripts.begin(), foot->scripts.end());
        out.actions.insert(out.actions.end(), foot->actions.begin(), foot->actions.end());
    }
    out.scripts.insert(out.scripts.end(), v.scripts.begin(), v.scripts.end());
    out.actions.insert(out.actions.end(), v.actions.begin(), v.actions.end());
    return out;
}

std::vector<Id> actionOrigins(const Project& p, const View& v) {
    std::vector<Id> out;
    const auto add = [&](const View& from) { out.insert(out.end(), from.actions.size(), from.id); };
    for (const View* t : templateChain(p, v)) add(*t);
    if (const View* head = headerOf(p, v)) add(*head);
    if (const View* foot = footerOf(p, v)) add(*foot);
    add(v);
    return out;
}

std::vector<const View*> viewsUsing(const Project& p, Id templ) {
    std::vector<const View*> out;
    for (const auto& v : p.views) {
        if (v.id == templ) continue;
        bool uses = false;
        for (const View* t : templateChain(p, v)) uses = uses || t->id == templ;
        if (const auto* h = headerOf(p, v); h && h->id == templ) uses = true;
        if (const auto* f = footerOf(p, v); f && f->id == templ) uses = true;
        if (uses) out.push_back(&v);
    }
    return out;
}

} // namespace hmi
