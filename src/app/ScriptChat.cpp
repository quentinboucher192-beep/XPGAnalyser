// =============================================================================
//  app/ScriptChat.cpp - 1.50.1 : voir ScriptChat.hpp
// =============================================================================
#include "ScriptChat.hpp"

#include "App.hpp"
#include "ai/AiChatReply.hpp"
#include "ai/AiPanel.hpp"
#include "hmi/HmiSessionPane.hpp"
#include "screens/Screens.hpp"
#include "../session/SessionProtocol.hpp"
#include "../ui/Widget.hpp"

#include <algorithm>
#include <cstdlib>

namespace app::scriptchat {

namespace {

using Kind = Result::Kind;

Result failed(std::string why) {
    Result r;
    r.kind = Kind::Failed;
    r.why = std::move(why);
    return r;
}

std::string lowered(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// \n ecrit dans le script : a la ligne.
std::string unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += '\n';
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::size_t lineCount(const std::string& t) { return t.empty() ? 0 : static_cast<std::size_t>(std::count(t.begin(), t.end(), '\n')) + 1; }

std::string headOf(const std::string& t) { return session::chatExcerpt(t, 60); }

HmiSessionPane* findPane(ui::Widget* w) {
    if (!w) return nullptr;
    if (auto* p = dynamic_cast<HmiSessionPane*>(w)) return p;
    for (const auto& c : w->children())
        if (auto* p = findPane(c.get())) return p;
    return nullptr;
}

std::string clipboardLine(const std::string& t) {
    return "\xC2\xAB " + headOf(t) + " \xC2\xBB (" + std::to_string(t.size()) + " octets, " + std::to_string(lineCount(t)) + " ligne(s))";
}

} // namespace

// ================================================================================== l'onglet Session ===
bool isSessionCommand(const std::string& cmd) {
    static const char* const kCommands[] = {"session-ecrire",  "session-repondre", "session-cite",         "session-reduit",    "session-reduire",
                                            "session-deballer", "session-copier",  "session-presse-papiers", "session-discussion"};
    return std::any_of(std::begin(kCommands), std::end(kCommands), [&](const char* c) { return cmd == c; });
}

Result session(App& app, ui::Widget* root, const std::vector<std::string>& w, int retries) {
    const std::string& cmd = w.front();
    const auto arg = [&](std::size_t i) -> std::string { return i < w.size() ? w[i] : std::string{}; };
    auto* screen = dynamic_cast<MainAnalysisScreen*>(app.menus().top());
    if (!screen) return failed(cmd + " : pas d'\xC3\xA9" "cran du projet au-dessus");
    HmiSessionPane* pane = screen->showSessionTab() ? findPane(root) : nullptr;
    if (!pane) {
        if (retries < 60) return {Kind::Retry, {}, {}};   // l'onglet arrive a la prochaine image
        return failed(cmd + " : pas d'onglet Session (pas de session, ou pas l'\xC3\xA9" "cran du projet)");
    }
    pane->refresh();
    pane->layout();
    auto* list = pane->list();
    if (!list) return failed(cmd + " : l'onglet Session n'a pas de liste");
    list->layout();
    // Les messages (pas les lignes de la session, pas le journal) : leur rang dans la liste.
    std::vector<std::size_t> msgs;
    for (std::size_t i = 0; i < list->rows().size(); ++i)
        if (sessionui::canReply(list->rows()[i])) msgs.push_back(i);
    std::size_t row = 0;
    const auto pick = [&](const std::string& n) {
        const int k = std::atoi(n.c_str());
        if (k < 1 || static_cast<std::size_t>(k) > msgs.size()) return false;
        row = msgs[static_cast<std::size_t>(k - 1)];
        return true;
    };
    const auto state = [&](std::size_t i) {
        const std::size_t total = list->wrappedText(i).size();
        if (!list->collapsible(i)) return std::string("entier (") + std::to_string(total) + " ligne(s))";
        return list->collapsed(i) ? "r\xC3\xA9" "duit (" + std::to_string(sessionui::kCollapseLines) + " lignes sur " + std::to_string(total) + ", \xC2\xAB \xE2\x80\xA6 voir tout \xC2\xBB)"
                                  : "d\xC3\xA9" "ball\xC3\xA9 (" + std::to_string(total) + " lignes, \xC2\xAB R\xC3\xA9" "duire \xC2\xBB)";
    };
    Result r;
    const auto say = [&](std::string line) { r.lines.push_back(cmd + " " + std::move(line)); };
    if (cmd == "session-ecrire") {
        const std::string text = unescape(arg(1));
        if (!pane->send(text)) return failed("session-ecrire : rien n'est parti (pas de session, ou un texte vide)");
        say(": envoy\xC3\xA9 (" + std::to_string(text.size()) + " octets, " + std::to_string(lineCount(text)) + " ligne(s))");
        return r;
    }
    if (cmd == "session-discussion") {
        r.lines.push_back("session-discussion : " + std::to_string(msgs.size()) + " message(s)");
        for (std::size_t k = 0; k < msgs.size(); ++k) {
            const auto& x = list->rows()[msgs[k]];
            std::string s = "  " + std::to_string(k + 1) + ". " + x.time + " " + x.who + " : " + headOf(x.text) + " [" + state(msgs[k]) + "]";
            if (!x.replyId.empty() || !x.replyText.empty()) s += " [cite " + x.replyWho + " : \xC2\xAB " + x.replyText + " \xC2\xBB]";
            r.lines.push_back(std::move(s));
        }
        return r;
    }
    if (cmd == "session-presse-papiers") {
        const std::string t = ui::clipboardText();
        r.lines.push_back("session-presse-papiers : " + clipboardLine(t));
        if (!arg(1).empty() && t.find(arg(1)) == std::string::npos) return failed("session-presse-papiers : le presse-papiers ne contient pas \xC2\xAB " + arg(1) + " \xC2\xBB");
        return r;
    }
    if (cmd == "session-reduire" && lowered(arg(1)) == "tout") {
        list->collapseAll();
        say("tout : fait");
        return r;
    }
    if (!pick(arg(1))) return failed(cmd + " " + arg(1) + " : pas de tel message (" + std::to_string(msgs.size()) + " dans la discussion)");
    const sessionui::Row x = list->rows()[row];
    if (cmd == "session-repondre") {
        if (!list->reply(row)) return failed("session-repondre " + arg(1) + " : on ne r\xC3\xA9pond pas \xC3\xA0 ce message");
        auto* bar = pane->replyBar();
        if (!bar || !bar->active() || bar->replyId() != x.id) return failed("session-repondre " + arg(1) + " : la barre \xC2\xAB En r\xC3\xA9ponse \xC3\xA0 \xE2\x80\xA6 \xC2\xBB ne s'est pas montr\xC3\xA9" "e");
        say(arg(1) + " : " + bar->label());
        const std::string text = unescape(arg(2));
        if (!pane->send(text)) return failed("session-repondre " + arg(1) + " : la r\xC3\xA9ponse n'est pas partie");
        say(arg(1) + " : envoy\xC3\xA9" "e (la barre : " + std::string(bar->active() ? "toujours l\xC3\xA0" : "partie") + ")");
        return r;
    }
    if (cmd == "session-cite") {
        if (x.replyId.empty() && x.replyText.empty()) return failed("session-cite " + arg(1) + " : ce message ne cite rien");
        if (!arg(2).empty() && x.replyText.find(arg(2)) == std::string::npos)
            return failed("session-cite " + arg(1) + " : l'extrait cit\xC3\xA9 est \xC2\xAB " + x.replyText + " \xC2\xBB, pas \xC2\xAB " + arg(2) + " \xE2\x80\xA6 \xC2\xBB");
        const gfx::Rect q = list->quoteRect(row);
        if (q.w <= 0.f || q.h <= 0.f) return failed("session-cite " + arg(1) + " : l'extrait cit\xC3\xA9 n'est pas montr\xC3\xA9");
        const int target = list->indexOfId(x.replyId);
        if (target < 0 || !list->goToQuoted(row) || list->selected() != target) return failed("session-cite " + arg(1) + " : le message cit\xC3\xA9 est introuvable dans la liste");
        int rank = 0;
        for (std::size_t k = 0; k < msgs.size(); ++k)
            if (static_cast<int>(msgs[k]) == target) rank = static_cast<int>(k) + 1;
        say(arg(1) + " : \xC2\xAB " + x.replyWho + " : " + x.replyText + " \xC2\xBB montr\xC3\xA9 au-dessus du texte ; un clic choisit le message " + std::to_string(rank));
        return r;
    }
    if (cmd == "session-reduit") {
        const bool want = lowered(arg(2)) != "non";
        say(arg(1) + " : " + state(row));
        if (list->collapsed(row) != want) return failed("session-reduit " + arg(1) + " : " + state(row) + ", attendu " + (want ? "r\xC3\xA9" "duit" : "d\xC3\xA9" "ball\xC3\xA9"));
        return r;
    }
    if (cmd == "session-reduire" || cmd == "session-deballer") {
        if (!list->collapsible(row)) return failed(cmd + " " + arg(1) + " : le message se lit en entier");
        list->setExpanded(row, cmd == "session-deballer");
        say(arg(1) + " : " + state(row));
        return r;
    }
    if (cmd == "session-copier") {
        const std::string how = lowered(arg(2));
        if (how == "ctrl+c") {
            list->select(static_cast<int>(row));
            list->takeFocus();
            ui::KeyMods m;
            m.ctrl = true;
            (void)list->dispatch(ui::KeyDown{ui::Key::C, m, false});
        } else if (!list->copy(row, how == "auteur")) {
            return failed("session-copier " + arg(1) + " : rien copi\xC3\xA9");
        }
        say(arg(1) + " : copi\xC3\xA9" + (how == "auteur" ? std::string(" avec l'auteur et l'heure") : how == "ctrl+c" ? std::string(" (Ctrl+C)") : std::string{}));
        return r;
    }
    return failed("commande inconnue : " + cmd);
}

// ================================================================================== l'Assistant IA ===
bool isAiCommand(const std::string& sub) {
    static const char* const kSubs[] = {"repondre", "citation", "reduit", "reduire", "deballer", "copier", "presse-papiers", "discussion", "survoler", "menu"};
    return std::any_of(std::begin(kSubs), std::end(kSubs), [&](const char* c) { return sub == c; });
}

Result ai(ai::AiPanel& panel, const std::vector<std::string>& w) {
    const auto arg = [&](std::size_t i) -> std::string { return i < w.size() ? w[i] : std::string{}; };
    const std::string sub = lowered(arg(1));
    panel.sync();
    panel.layout();
    auto& chat = panel.chat();
    const auto* conv = panel.assistant().current();
    const std::size_t count = conv ? conv->messages.size() : 0;
    const auto state = [&](int m) {
        const std::size_t total = chat.renderedLines(m).size();
        if (!chat.collapsible(m)) return std::string("entier (") + std::to_string(total) + " ligne(s))";
        return chat.collapsed(m) ? "r\xC3\xA9" "duit (" + std::to_string(chat.shownLineCount(m)) + " lignes sur " + std::to_string(total) + ", \xC2\xAB \xE2\x80\xA6 voir tout \xC2\xBB)"
                                 : "d\xC3\xA9" "ball\xC3\xA9 (" + std::to_string(total) + " lignes, \xC2\xAB R\xC3\xA9" "duire \xC2\xBB)";
    };
    Result r;
    const auto say = [&](std::string line) { r.lines.push_back("ia " + sub + " " + std::move(line)); };
    if (sub == "presse-papiers") {
        const std::string t = ui::clipboardText();
        r.lines.push_back("ia presse-papiers : " + clipboardLine(t));
        if (!arg(2).empty() && t.find(arg(2)) == std::string::npos) return failed("ia presse-papiers : le presse-papiers ne contient pas \xC2\xAB " + arg(2) + " \xC2\xBB");
        return r;
    }
    if (sub == "discussion") {
        r.lines.push_back("ia discussion : " + std::to_string(count) + " message(s)");
        for (std::size_t i = 0; i < count; ++i) {
            const auto& m = conv->messages[i];
            std::string s = "  " + std::to_string(i + 1) + ". " + ai::chatreply::authorOf(m) + " \xC2\xB7 " + ai::chatreply::timeOf(m) + " : "
                            + ai::chatreply::excerptOf(m.text) + " [" + state(static_cast<int>(i)) + "]";
            if (const auto q = ai::chatreply::readQuote(m)) s += " [cite " + q->who + " : \xC2\xAB " + q->excerpt + " \xC2\xBB]";
            r.lines.push_back(std::move(s));
        }
        return r;
    }
    if (sub == "reduire" && lowered(arg(2)) == "tout") {
        chat.collapseAll();
        say("tout : fait");
        return r;
    }
    const int n = std::atoi(arg(2).c_str()) - 1;
    if (n < 0 || static_cast<std::size_t>(n) >= count) return failed("ia " + sub + " " + arg(2) + " : pas de tel message (" + std::to_string(count) + " dans la conversation)");
    if (sub == "repondre") {
        if (!panel.replyTo(n)) return failed("ia repondre " + arg(2) + " : " + panel.lastStatus());
        say(arg(2) + " : " + panel.replyBar().label());
        return r;
    }
    if (sub == "citation") {
        const auto q = ai::chatreply::readQuote(conv->messages[static_cast<std::size_t>(n)]);
        if (!q) return failed("ia citation " + arg(2) + " : ce message ne cite rien");
        if (!arg(3).empty() && q->excerpt.find(arg(3)) == std::string::npos)
            return failed("ia citation " + arg(2) + " : l'extrait cit\xC3\xA9 est \xC2\xAB " + q->excerpt + " \xC2\xBB, pas \xC2\xAB " + arg(3) + " \xE2\x80\xA6 \xC2\xBB");
        const std::string shown = chat.quoteText(n);
        if (shown.empty()) return failed("ia citation " + arg(2) + " : l'extrait cit\xC3\xA9 n'est pas montr\xC3\xA9");
        say(arg(2) + " : " + shown);
        return r;
    }
    if (sub == "reduit") {
        const bool want = lowered(arg(3)) != "non";
        say(arg(2) + " : " + state(n));
        if (chat.collapsed(n) != want) return failed("ia reduit " + arg(2) + " : " + state(n) + ", attendu " + (want ? "r\xC3\xA9" "duit" : "d\xC3\xA9" "ball\xC3\xA9"));
        return r;
    }
    if (sub == "reduire" || sub == "deballer") {
        if (!chat.collapsible(n)) return failed("ia " + sub + " " + arg(2) + " : le message se lit en entier");
        chat.setExpanded(n, sub == "deballer");
        say(arg(2) + " : " + state(n));
        return r;
    }
    if (sub == "copier") {
        const std::string how = lowered(arg(3));
        bool ok = true;
        if (how == "ctrl+c") {
            chat.selectMessage(n);
            chat.takeFocus();
            ui::KeyMods m;
            m.ctrl = true;
            (void)chat.dispatch(ui::KeyDown{ui::Key::C, m, false});
        } else if (how == "code") {
            ok = chat.copyCodeOf(n, 0);
        } else {
            ok = chat.copyMessage(n, how == "auteur");
        }
        if (!ok) return failed("ia copier " + arg(2) + " : rien copi\xC3\xA9 (" + panel.lastStatus() + ")");
        say(arg(2) + " : copi\xC3\xA9" + (how == "auteur" ? std::string(" avec l'auteur et l'heure") : how == "ctrl+c" ? std::string(" (Ctrl+C)") : how == "code" ? std::string(" (le code)") : std::string{}));
        return r;
    }
    if (sub == "survoler" || sub == "menu") {
        chat.revealMessage(n, false);
        panel.layout();
        if (sub == "menu") {
            if (!chat.openMessageMenu(n, -1)) return failed("ia menu " + arg(2) + " : pas de menu");
            std::string items;
            for (const auto& it : chat.messageMenuItems()) items += (items.empty() ? "" : " | ") + it;
            say(arg(2) + " : " + items);
            return r;
        }
        const gfx::Rect at = chat.messageRect(n);
        if (at.h <= 0.f) return failed("ia survoler " + arg(2) + " : le message n'est pas dans la vue");
        r.lines.push_back("ia survoler " + arg(2));
        r.move = true;   // la souris sur son en-tete : ses pictogrammes Repondre et Copier se montrent
        r.x = at.x + at.w * 0.5f;
        r.y = at.y + 6.f;
        return r;
    }
    return failed("ia " + sub + " : sous-commande inconnue");
}

} // namespace app::scriptchat
