// =============================================================================
//  app/SessionMarks.cpp - 1.43.0 : voir SessionMarks.hpp
// =============================================================================
#include "SessionMarks.hpp"

namespace app::sessionmarks {

namespace {
struct State {
    bool active{false};
    Held held;
    std::uint64_t revision{0};
    Mine mine;
    std::function<void(const std::string&)> notice;
};
State& state() {
    static State s;
    return s;
}
} // namespace

void setActive(bool on) {
    auto& s = state();
    if (s.active == on) return;
    s.active = on;
    if (!on) s.held.clear();
    ++s.revision;
}

bool active() { return state().active; }

void setHeld(Held held) {
    auto& s = state();
    if (held == s.held) return;
    s.held = std::move(held);
    ++s.revision;
}

const Holder* heldByOther(hmi::Id view, hmi::Id object) {
    const auto& s = state();
    if (!s.active) return nullptr;
    const auto it = s.held.find({view, object});
    return it == s.held.end() ? nullptr : &it->second;
}

std::vector<std::pair<hmi::Id, const Holder*>> heldIn(hmi::Id view) {
    std::vector<std::pair<hmi::Id, const Holder*>> out;
    const auto& s = state();
    if (!s.active) return out;
    for (auto it = s.held.lower_bound({view, 0}); it != s.held.end() && it->first.first == view; ++it)
        out.emplace_back(it->first.second, &it->second);
    return out;
}

std::uint64_t revision() { return state().revision; }

void mine(hmi::Id view, const std::vector<hmi::Id>& selection) {
    auto& m = state().mine;
    if (m.view == view && m.selection == selection) return;
    m.view = view;
    m.selection = selection;
    ++m.serial;
}

void seen(hmi::Id view) {
    auto& m = state().mine;
    if (m.view == view) return;
    m.view = view;
    m.selection.clear();   // le choix d'une autre vue ne tient plus rien ici
    ++m.serial;
}

const Mine& current() { return state().mine; }

void setRefusedNotice(std::function<void(const std::string&)> fn) { state().notice = std::move(fn); }

void refused(const std::string& text) {
    if (state().notice) state().notice(text);
}

} // namespace app::sessionmarks
