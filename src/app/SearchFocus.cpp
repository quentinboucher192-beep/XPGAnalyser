// =============================================================================
//  app/SearchFocus.cpp - lot API 8 (finitions) : Ctrl+F dans les volets
//  (voir SearchFocus.hpp).
// =============================================================================
#include "SearchFocus.hpp"

#include "ApiListKit.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/SearchField.hpp"

#include <string>

namespace app {

namespace {

// Visible et pose (un volet range hors de l'ecran a des bords vides).
bool usable(const ui::InputText& f) {
    const auto b = f.bounds();
    return f.visible() && f.enabled() && b.w > 1.f && b.h > 1.f;
}

ui::InputText* findIn(ui::Widget& w) {
    if (!w.visible()) return nullptr;
    if (auto* bar = dynamic_cast<ApiFilterBar*>(&w)) {
        auto& f = bar->field();
        return usable(f) ? &f : nullptr;
    }
    if (auto* field = dynamic_cast<ui::SearchField*>(&w)) {
        auto& f = field->field();
        return usable(f) ? &f : nullptr;
    }
    if (auto* in = dynamic_cast<ui::InputText*>(&w))
        return usable(*in) && looksLikeSearchId(in->id()) ? in : nullptr;
    for (const auto& c : w.children())
        if (auto* found = findIn(*c)) return found;
    return nullptr;
}

} // namespace

bool looksLikeSearchId(std::string_view id) {
    const auto dot = id.rfind('.');
    std::string last(dot == std::string_view::npos ? id : id.substr(dot + 1));
    for (auto& c : last)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const char* word : {"search", "chercher", "recherche", "find", "filter", "filtre"})
        if (last.find(word) != std::string::npos) return true;
    return false;
}

ui::InputText* visibleSearchField(ui::Widget& root) {
    for (const ui::Widget* p = &root; p; p = p->parent())
        if (!p->visible()) return nullptr;
    return findIn(root);
}

bool focusSearchField(ui::Widget& root) {
    auto* field = visibleSearchField(root);
    if (!field) return false;
    // Echap l'efface : c'est une recherche (ApiFilterBar et ui::SearchField le
    // disent deja ; un champ trouve par son id, a partir de maintenant).
    field->setEscapeClears(true);
    return field->focusAndSelectAll();
}

} // namespace app
