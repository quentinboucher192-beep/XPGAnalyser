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
    // 1.45.0
    Board board;
    std::uint64_t boardRevision{0};
    Hooks hooks;
    bool guest{false};
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
    if (!on) {
        s.held.clear();
        s.board = Board{};   // 1.45.0 : plus rien a montrer
        s.guest = false;
        ++s.boardRevision;
    }
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

// ---- 1.45.0 ----
void setBoard(Board board) {
    auto& s = state();
    s.board = std::move(board);
    ++s.boardRevision;
}

const Board& board() { return state().board; }

std::uint64_t boardRevision() { return state().boardRevision; }

void setHooks(Hooks hooks) { state().hooks = std::move(hooks); }

void say(const std::string& text, const std::string& replyTo) {
    if (state().hooks.say && !text.empty()) state().hooks.say(text, replyTo);
}

void follow(int member) {
    if (state().hooks.follow) state().hooks.follow(member);
}

std::string exportJournal(std::string* why) {
    if (!state().hooks.exportJournal) {
        if (why) *why = "pas de session";
        return {};
    }
    return state().hooks.exportJournal(why);
}

void setGuest(bool guest) { state().guest = guest; }

bool guest() { return state().active && state().guest; }

std::string keepRefusal() { return guest() ? std::string("En session, le projet reste chez l'h\xC3\xB4te") : std::string{}; }

std::string initialsOf(std::string_view name) {
    std::string out;
    int letters = 0;
    bool start = true;
    for (std::size_t i = 0; i < name.size() && letters < 2;) {
        const auto u = static_cast<unsigned char>(name[i]);
        const std::size_t len = u < 0x80 ? 1 : (u >> 5) == 6 ? 2 : (u >> 4) == 14 ? 3 : (u >> 3) == 30 ? 4 : 1;
        if (name[i] == ' ' || name[i] == '-' || name[i] == '_') {
            start = true;
            ++i;
            continue;
        }
        if (start) {
            std::string ch(name.substr(i, len));
            if (len == 1 && ch[0] >= 'a' && ch[0] <= 'z') ch[0] = static_cast<char>(ch[0] - 'a' + 'A');
            out += ch;
            ++letters;
            start = false;
        }
        i += len;
    }
    return out.empty() ? std::string("?") : out;
}

bool keepAction(std::string_view action) {
    return action == "project.saveAs" || action == "project.duplicate" || action == "project.exportArchive" || action == "project.exportSources";
}

} // namespace app::sessionmarks
