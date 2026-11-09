// =============================================================================
//  app/SessionDialogs.cpp - 1.43.0 : voir SessionDialogs.hpp
//  1.44.0 : Demarrer et Rejoindre ont leur fenetre (FlowBody) : Ou (le reseau du
//  bureau / Internet, chiffre de bout en bout), le code par Internet montre et
//  copiable ; Rejoindre : << Sur le reseau du bureau >> (la liste, Actualiser, le
//  code seulement si la session le demande, une autre adresse repliee) ou << Par
//  Internet >> (le code seul). La 1.43 (un formulaire) montrait toujours l'adresse
//  et le code : l'utilisateur croyait devoir les remplir.
//  1.45.0 : la fenetre de la session - << Retirer de la session... >> pour chaque
//  equipier (l'hote, une question d'abord) ; la colonne de droite a deux vues :
//  la Discussion et le Journal (qui a change quoi ; l'hote l'exporte en CSV).
// =============================================================================
#include "SessionDialogs.hpp"

#include "App.hpp"
#include "SessionService.hpp"
#include "screens/Screens.hpp"

#include "../menu/MenuManager.hpp"
#include "../session/SessionRelay.hpp"   // 1.44.0 : le code par Internet
#include "../ui/DialogFrame.hpp"
#include "../ui/Shapes.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

float buttonWidth(const std::string& text) { return std::max(88.f, ui::measureWidth(text, kButton) + 32.f); }

std::vector<std::string> wrapWords(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : s) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else word.push_back(c);
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

std::string pcText(const session::Member& m) {
    std::string t = m.machine;
    if (!m.machineId.empty()) t += (t.empty() ? "" : " \xC2\xB7 ") + m.machineId;
    return t;
}

// ------------------------------------------------------------------------------------------- la fenetre ---
class PanelBody final : public ui::Widget {
public:
    explicit PanelBody(App& app) : app_(app) {}
    struct Row { ui::Button* follow{nullptr}; ui::Button* role{nullptr}; ui::Button* remove{nullptr}; int member{0}; };
    struct KnockRow { ui::Button* yes{nullptr}; ui::Button* no{nullptr}; int conn{0}; };
    std::vector<Row> rows;
    std::vector<KnockRow> knockRows;
    ui::Button* copy{nullptr};
    ui::InputText* field{nullptr};
    ui::Button* send{nullptr};
    ui::Button* stop{nullptr};
    ui::Button* close{nullptr};
    // 1.45.0 : la colonne de droite - la Discussion ou le Journal ; Exporter le journal (l'hote).
    ui::Button* chatView{nullptr};
    ui::Button* journalView{nullptr};
    ui::Button* exportJournal{nullptr};
    bool showJournal{false};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(900.f, std::max(560.f, r.w - 60.f));
        const float h = std::min(620.f, std::max(420.f, r.h - 60.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        left_ = {panel_.x + 16.f, panel_.y + 34.f + 12.f, std::floor(w * 0.5f) - 24.f, panel_.h - 34.f - 12.f - 60.f};
        right_ = {left_.right() + 16.f, left_.y, panel_.right() - 16.f - (left_.right() + 16.f), left_.h};
        auto& s = app_.session();
        float y = left_.y + 24.f;
        // Le code, sous le titre de l'equipe (l'hote, avec le code).
        codeAt_ = -1.f;
        if (s.host() && !s.code().empty()) {
            codeAt_ = y;
            if (copy) copy->setBounds({left_.right() - buttonWidth(copy->text()), y + 6.f, buttonWidth(copy->text()), 28.f});
            y += 44.f;
        }
        rowsAt_ = y;
        textRight_.clear();
        for (auto& row : rows) {
            float bx = left_.right();
            // 1.45.0 : avec << Retirer de la session... >>, deux lignes de boutons (Suivre et le role dessus).
            const bool two = row.remove != nullptr;
            for (auto* b : {row.follow, row.role}) {
                if (!b) continue;
                const float bw = std::min(160.f, buttonWidth(b->text()) - 10.f);
                bx -= bw;
                b->setBounds({bx, two ? y + 2.f : y + 12.f, bw, two ? 23.f : 26.f});
                bx -= 6.f;
            }
            float left = bx;
            if (row.remove) {
                const float bw = std::min(200.f, buttonWidth(row.remove->text()) - 10.f);
                row.remove->setBounds({left_.right() - bw, y + 27.f, bw, 23.f});
                left = std::min(left, left_.right() - bw - 6.f);
            }
            textRight_.push_back(left - 6.f);   // le texte de la ligne s'arrete avant ses boutons
            y += 52.f;
        }
        knocksAt_ = y + 8.f;
        y = knocksAt_ + 22.f;
        for (auto& k : knockRows) {
            float bx = left_.right();
            for (auto* b : {k.no, k.yes}) {
                const float bw = buttonWidth(b->text()) - 10.f;
                bx -= bw;
                b->setBounds({bx, y + 10.f, bw, 26.f});
                bx -= 6.f;
            }
            y += 48.f;
        }
        // 1.45.0 : les deux vues de la colonne de droite (en tete), Exporter le journal.
        {
            float hx = right_.right();
            for (auto* b : {exportJournal, journalView, chatView}) {
                if (!b) continue;
                const float bw = buttonWidth(b->text()) - 16.f;
                hx -= bw;
                b->setBounds({hx, right_.y - 4.f, bw, 24.f});
                hx -= 4.f;
            }
        }
        // La discussion : le champ et Envoyer en bas de la colonne de droite.
        const float fy = right_.bottom() - 32.f;
        if (send) send->setBounds({right_.right() - 96.f, fy, 96.f, 30.f});
        if (field) field->setBounds({right_.x, fy, right_.w - 104.f, 30.f});
        const float by = panel_.bottom() - 44.f;
        float bx = panel_.right() - 16.f;
        for (auto* b : {close, stop}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 10.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        auto& s = app_.session();
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = 34.f;
        r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold, ui = ctx.theme.font.ui;
        std::string title = s.active() ? "Session \xC2\xAB " + s.sessionName() + " \xC2\xBB" : std::string("Session");
        if (s.active()) title += s.host() ? " \xE2\x80\x94 tu es l'h\xC3\xB4te" : " \xE2\x80\x94 h\xC3\xB4te : " + s.hostName();
        r.drawText({panel_.x + 12.f, panel_.y + (titleH - r.lineHeight(bold)) * 0.5f}, title, bold, c.text);

        if (!s.active()) {
            float y = left_.y + 10.f;
            const std::string text = s.mode() == SessionService::Mode::Joining ? s.progress() : (s.progress().empty() ? "Pas de session en cours." : s.progress());
            for (const auto& l : wrapWords(text, ui, panel_.w - 32.f)) {
                r.drawText({panel_.x + 16.f, y}, l, ui, c.text);
                y += 22.f;
            }
            return;
        }
        // L'equipe.
        r.drawText({left_.x, left_.y}, "L'\xC3\xA9quipe \xC2\xB7 " + std::to_string(s.members().size()), bold, c.text);
        if (codeAt_ >= 0.f) {
            const gfx::Rect box{left_.x, codeAt_, left_.w, 38.f};
            r.fillRoundedRect(box, c.inputBg, 6.f);
            // 1.44.0 : par Internet, le code de 10 caracteres (et si la session est deja ouverte sur le service XPG).
            const std::string what = !s.internet() ? std::string("Le code de la session")
                                   : s.relayOpen() ? std::string("Code de la session \xC2\xB7 par Internet, chiffr\xC3\xA9" "e de bout en bout")
                                                   : std::string("Code de la session \xC2\xB7 ouverture sur le service XPG\xE2\x80\xA6");
            r.drawText({box.x + 10.f, box.y + 3.f}, what, kSmall, c.textMuted);
            r.drawText({box.x + 10.f, box.y + 18.f}, s.codeText(), ctx.theme.font.mono, c.text);
        }
        float y = rowsAt_;
        std::size_t rowIndex = 0;
        for (const auto& m : s.members()) {
            const float textRight = rowIndex < textRight_.size() ? textRight_[rowIndex] : left_.right();
            ++rowIndex;
            const auto col = gfx::Color::rgb(m.color);
            const gfx::Rect dot{left_.x, y + 8.f, 32.f, 32.f};
            r.fillRoundedRect(dot, col, 16.f);
            const std::string ini = session::initials(m.name);
            r.drawText({dot.x + (32.f - r.measure(ini, kSmall).width) * 0.5f, dot.y + (32.f - r.lineHeight(kSmall)) * 0.5f}, ini, kSmall,
                       gfx::Color::rgb(0xFFFFFF));
            std::string who = m.name;
            if (m.number == s.me()) who += " (toi)";
            if (m.number == 0) who += " \xC2\xB7 h\xC3\xB4te";
            {
                const auto nw = r.fitCharacters(who, ui, std::max(60.f, textRight - (left_.x + 42.f)));
                r.drawText({left_.x + 42.f, y + 6.f}, nw >= who.size() ? who : who.substr(0, nw) + "\xE2\x80\xA6", ui, c.text);
            }
            std::string where = pcText(m) + " \xC2\xB7 " + session::roleText(m.role);
            if (const auto v = s.viewName(m.view); !v.empty()) where += " \xC2\xB7 " + v;
            if (!m.selection.empty()) where += " \xC2\xB7 " + std::to_string(m.selection.size()) + " objet" + (m.selection.size() > 1 ? "s" : "");
            const float maxW = textRight - (left_.x + 42.f);
            const auto n = r.fitCharacters(where, kSmall, std::max(60.f, maxW - 12.f));
            r.drawText({left_.x + 42.f, y + 27.f}, n >= where.size() ? where : where.substr(0, n) + "\xE2\x80\xA6", kSmall, c.textMuted);
            y += 52.f;
        }
        if (!s.knocks().empty()) {
            r.drawText({left_.x, knocksAt_}, "Ils veulent entrer", bold, c.warning);
            float ky = knocksAt_ + 22.f;
            for (const auto& k : s.knocks()) {
                r.drawText({left_.x, ky + 6.f}, k.name, ui, c.text);
                r.drawText({left_.x, ky + 26.f}, k.machine + (k.machineId.empty() ? std::string{} : " \xC2\xB7 " + k.machineId) + " \xC2\xB7 " + k.address, kSmall,
                           c.textMuted);
                ky += 48.f;
            }
        }
        // La discussion (1.45.0 : ou le journal).
        r.drawText({right_.x, right_.y}, showJournal ? "Le journal" : "La discussion", bold, c.text);
        const gfx::Rect box{right_.x, right_.y + 24.f, right_.w, right_.h - 24.f - 40.f};
        r.fillRoundedRect(box, c.inputBg, 6.f);
        std::vector<std::pair<std::string, gfx::Color>> lines;
        if (showJournal) {
            const auto& entries = s.journal().entries();
            if (entries.empty())
                lines.emplace_back("Aucun changement encore : chaque changement de la session s'inscrit ici (qui, quand, quoi).", c.textMuted);
            for (const auto& e : entries) {
                bool first = true;
                for (const auto& w : wrapWords(session::lineOf(e), kSmall, box.w - 20.f)) {
                    lines.emplace_back(w, first && e.color ? gfx::Color::rgb(e.color) : c.text);
                    first = false;
                }
            }
        }
        for (const auto& l : showJournal ? std::vector<SessionService::ChatLine>{} : s.chat()) {
            const std::string text = l.system ? l.text : l.from + " : " + l.text;
            const auto col = l.system ? c.textMuted : c.text;
            bool first = true;
            for (const auto& w : wrapWords((l.time.empty() ? std::string{} : l.time + "  ") + text, kSmall, box.w - 20.f)) {
                lines.emplace_back(w, first && !l.system && l.color ? gfx::Color::rgb(l.color) : col);
                first = false;
            }
        }
        const float lh = 18.f;
        const std::size_t fit = static_cast<std::size_t>(std::max(1.f, std::floor((box.h - 12.f) / lh)));
        const std::size_t from = lines.size() > fit ? lines.size() - fit : 0;
        float ly = box.y + 6.f;
        for (std::size_t i = from; i < lines.size(); ++i) {
            r.drawText({box.x + 10.f, ly}, lines[i].first, kSmall, lines[i].second);
            ly += lh;
        }
        // Le rappel en bas a gauche.
        const std::string hint = s.host() ? "Tu enregistres pour tout le monde (Ctrl+S). Fermer le projet arr\xC3\xAAte la session."
                                          : "C'est " + s.hostName() + " qui enregistre ; ta copie s'efface quand tu pars. Ctrl+Z annule tes gestes.";
        r.drawText({panel_.x + 16.f, panel_.bottom() - 36.f}, hint, kSmall, c.textMuted);
    }

private:
    App& app_;
    gfx::Rect panel_{}, left_{}, right_{};
    float codeAt_{-1.f}, rowsAt_{0.f}, knocksAt_{0.f};
    std::vector<float> textRight_;
};

class SessionPanel final : public menu::WidgetMenu {
public:
    explicit SessionPanel(App& app) : menu::WidgetMenu("dialog.session"), app_(app) {}
    [[nodiscard]] menu::MenuTraits traits() const override {
        menu::MenuTraits t;
        t.kind = menu::MenuKind::Dialog;
        t.rendersBelow = true;
        t.updatesBelow = true;   // le reseau et l'editeur continuent dessous
        t.blocksInput = true;
        t.dimsBelow = true;
        return t;
    }
    [[nodiscard]] std::string title() const override { return "Session"; }

    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (structure() != built_) rebuild();
        if (app_.session().revision() != seen_ && body_) {
            seen_ = app_.session().revision();
            body_->invalidate();
        }
    }

    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Escape) {
                manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Return && field_ && !field_->text().empty()) {
                sendLine();
                return ui::EventResult::Consumed;
            }
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }

protected:
    core::Status buildUi() override {
        rebuild();
        return core::ok();
    }

private:
    // Ce qui change les boutons : les membres (leur role, s'ils sont dans une vue), ceux qui frappent, le mode.
    std::string structure() const {
        const auto& s = app_.session();
        std::string k = std::to_string(static_cast<int>(s.mode())) + "|" + (s.code().empty() ? "" : "c") + (s.relayOpen() ? "o" : "")
                      + (journal_ ? "j" : "") + std::to_string(s.journal().entries().size());   // 1.45.0 : la vue de droite, le compte du journal
        for (const auto& m : s.members()) k += "|" + std::to_string(m.number) + session::roleKey(m.role) + (m.view ? "v" : "");
        for (const auto& kn : s.knocks()) k += "|k" + std::to_string(kn.conn);
        return k;
    }

    void rebuild() {
        built_ = structure();
        const std::string typed = field_ ? field_->text() : std::string{};
        links_.clear();
        auto body = std::make_unique<PanelBody>(app_);
        auto& s = app_.session();
        const auto button = [&](const std::string& text, const std::string& id, ui::Button::Style style) {
            auto* b = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, "dialog.session." + id)));
            b->setStyle(style);
            return b;
        };
        if (s.active()) {
            if (s.host() && !s.code().empty()) {
                body->copy = button("Copier le code", "copier", ui::Button::Style::Flat);
                links_ += body->copy->clicked->connect([this] {
                    const auto& ss = app_.session();
                    ui::setClipboardText(ss.internet() ? ss.codeText() : ss.code());   // 1.44.0 : "K7Q4-M2XP-T9" par Internet
                    app_.events().publish(StatusNotice{std::string("Le code est copi\xC3\xA9 : donne-le \xC3\xA0 tes \xC3\xA9quipiers")
                                                           + (ss.internet() ? " (Rejoindre une session \xE2\x80\xBA Par Internet)" : "") + " ; il ne sert que pendant la session.",
                                                       5.0});
                });
            }
            for (const auto& m : s.members()) {
                PanelBody::Row row;
                row.member = m.number;
                if (m.number != s.me() && m.view != hmi::kNoId) {
                    row.follow = button("Suivre", "suivre." + std::to_string(m.number), ui::Button::Style::Default);
                    const int n = m.number;
                    links_ += row.follow->clicked->connect([this, n] {
                        app_.session().follow(n);
                        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
                    });
                }
                if (s.host() && m.number != 0) {
                    const bool watching = m.role == session::Role::Watch;
                    row.role = button(watching ? "Laisser modifier" : "Regarder seulement", "role." + std::to_string(m.number), ui::Button::Style::Default);
                    const int n = m.number;
                    links_ += row.role->clicked->connect([this, n, watching] {
                        app_.session().setRole(n, watching ? session::Role::Edit : session::Role::Watch);
                    });
                    // 1.45.0 : RETIRER DE LA SESSION (une question d'abord).
                    row.remove = button("Retirer de la session\xE2\x80\xA6", "retirer." + std::to_string(m.number), ui::Button::Style::Danger);
                    const std::string who = m.name;
                    links_ += row.remove->clicked->connect([this, n, who] { askRemove(n, who); });
                }
                body->rows.push_back(row);
            }
            if (s.host())
                for (const auto& k : s.knocks()) {
                    PanelBody::KnockRow kr;
                    kr.conn = k.conn;
                    kr.yes = button("Faire entrer", "entrer." + std::to_string(k.conn), ui::Button::Style::Primary);
                    kr.no = button("Refuser", "refuser." + std::to_string(k.conn), ui::Button::Style::Default);
                    const int conn = k.conn;
                    links_ += kr.yes->clicked->connect([this, conn] { app_.session().accept(conn); });
                    links_ += kr.no->clicked->connect([this, conn] { app_.session().refuse(conn, app_.session().hostName() + " ne t'a pas fait entrer"); });
                    body->knockRows.push_back(kr);
                }
            // 1.45.0 : la Discussion ou le Journal ; Exporter le journal (l'hote).
            body->chatView = button("Discussion", "vue.discussion", journal_ ? ui::Button::Style::Flat : ui::Button::Style::Primary);
            body->journalView = button("Journal \xC2\xB7 " + std::to_string(s.journal().entries().size()), "vue.journal",
                                       journal_ ? ui::Button::Style::Primary : ui::Button::Style::Flat);
            body->showJournal = journal_;
            links_ += body->chatView->clicked->connect([this] { journal_ = false; });
            links_ += body->journalView->clicked->connect([this] { journal_ = true; });
            if (s.host() && journal_) {
                body->exportJournal = button("Exporter (CSV)", "journal.exporter", ui::Button::Style::Flat);
                links_ += body->exportJournal->clicked->connect([this] {
                    std::string why;
                    const std::string path = app_.session().exportJournal(&why);
                    app_.events().publish(StatusNotice{path.empty() ? "Le journal ne s'exporte pas : " + why + "." : "Journal export\xC3\xA9 : " + path, 8.0});
                });
            }
            auto f = std::make_unique<ui::InputText>("dialog.session.message");
            f->setPlaceholder("\xC3\x89" "cris \xC3\xA0 l'\xC3\xA9quipe\xE2\x80\xA6 (Entr\xC3\xA9" "e)");
            f->setText(typed);
            field_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
            body->field = field_;
            body->send = button("Envoyer", "envoyer", ui::Button::Style::Primary);
            links_ += body->send->clicked->connect([this] { sendLine(); });
            body->stop = button(s.host() ? "Arr\xC3\xAAter la session" : "Quitter la session", "arreter", ui::Button::Style::Danger);
            links_ += body->stop->clicked->connect([this] {
                app_.session().stop();
                manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
            });
        } else {
            field_ = nullptr;
            if (s.mode() == SessionService::Mode::Joining) {
                body->stop = button("Annuler", "annuler", ui::Button::Style::Default);
                links_ += body->stop->clicked->connect([this] { app_.session().stop(); });
            }
        }
        body->close = button("Fermer", "fermer", ui::Button::Style::Default);
        links_ += body->close->clicked->connect([this] { manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}}); });
        body_ = body.get();
        setRoot(std::move(body));
        if (field_) {
            focus().clear();
            focus().focus(field_);
        }
    }

    void sendLine() {
        if (!field_) return;
        const std::string t = field_->text();
        if (t.empty()) return;
        app_.session().say(t);
        field_->setText({});
    }

    // 1.45.0 : retirer un equipier - la question, puis le retrait.
    void askRemove(int member, const std::string& who) {
        const std::string host = app_.session().hostName();
        manager().ShowDialog(std::make_unique<MessageDialog>("Retirer " + who + " de la session ?",
                                                             who + " est coup\xC3\xA9 tout de suite (\xC2\xAB " + host + " t'a retir\xC3\xA9 de la session \xC2\xBB) "
                                                                   "et revient \xC3\xA0 son accueil ; la copie du projet s'efface de son PC. Les autres le lisent dans la discussion.\n\n"
                                                                   "Il ne revient que si tu le fais entrer de nouveau (m\xC3\xAAme avec le code).",
                                                             MessageDialog::Icon::Question, "Le retirer"),
                             [this, member](const menu::DialogResult& r) {
                                 if (r.accepted()) app_.session().remove(member);
                             });
    }

    App& app_;
    PanelBody* body_{nullptr};
    ui::InputText* field_{nullptr};
    core::ConnectionScope links_;
    std::string built_;
    std::uint64_t seen_{~0ull};
    bool journal_{false};   // 1.45.0 : la vue de droite (le Journal)
};

std::string label(const session::Found& f) {
    const auto& a = f.announce;
    return a.session + " \xE2\x80\x94 h\xC3\xB4te : " + a.host;
}

std::string detail(const session::Found& f) {
    const auto& a = f.announce;
    return a.machine + " \xC2\xB7 " + f.address + " \xC2\xB7 " + a.app + " " + a.version + " \xC2\xB7 " + std::to_string(a.members) + " dedans"
         + (a.needsCode ? " \xC2\xB7 il faut le code" : "");
}

SessionService::JoinTarget targetOf(const session::Found& f) {
    SessionService::JoinTarget t;
    t.address = f.address;
    t.port = f.announce.port;
    t.fingerprint = f.announce.fingerprint;
    t.sessionId = f.announce.sessionId;
    t.sessionName = f.announce.session;
    t.hostName = f.announce.host;
    t.machine = f.announce.machine;
    t.needsCode = f.announce.needsCode;
    return t;
}

// ------------------------------------------------------------------------- 1.44.0 : une fenetre en lignes ---
//  Les fenetres Demarrer et Rejoindre : des lignes posees de haut en bas (un titre, une phrase, un widget, des
//  widgets cote a cote, le code dans sa boite), puis les boutons en bas a droite. La fenetre les recompose quand
//  un choix change (Ou, Sur le reseau du bureau / Par Internet...).
class FlowBody final : public ui::Widget {
public:
    struct Item {
        enum class Kind { Title, Text, Muted, Warning, Widget, Row, Gap, Code };
        Kind kind{Kind::Text};
        std::string text;
        std::vector<ui::Widget*> widgets;
        float height{0};   // Widget, Row : la hauteur ; Gap : l'espace
        float width{0};    // Widget : 0, toute la largeur
        float indent{0};
    };
    std::string title;
    std::vector<Item> items;
    std::vector<ui::Button*> footer;   // en bas a droite (le dernier : le plus a droite)
    std::string error;                 // en rouge, au-dessus des boutons
    float wanted{620.f};

    void add(Item::Kind k, std::string text = {}, float indent = 0) {
        Item i;
        i.kind = k;
        i.text = std::move(text);
        i.indent = indent;
        items.push_back(std::move(i));
    }
    void widget(ui::Widget* w, float h = 30.f, float width = 0, float indent = 0) {
        Item i;
        i.kind = Item::Kind::Widget;
        i.widgets = {w};
        i.height = h;
        i.width = width;
        i.indent = indent;
        items.push_back(std::move(i));
    }
    void row(std::vector<ui::Widget*> ws, float h = 30.f, float indent = 0) {
        Item i;
        i.kind = Item::Kind::Row;
        i.widgets = std::move(ws);
        i.height = h;
        i.indent = indent;
        items.push_back(std::move(i));
    }
    void gap(float h = 10.f) {
        Item i;
        i.kind = Item::Kind::Gap;
        i.height = h;
        items.push_back(std::move(i));
    }
    void code(std::string text, ui::Button* copy) {
        Item i;
        i.kind = Item::Kind::Code;
        i.text = std::move(text);
        if (copy) i.widgets = {copy};
        i.height = 44.f;
        items.push_back(std::move(i));
    }

protected:
    static constexpr float kPad = 20.f;
    static constexpr float kSmallLine = 18.f;

    static float widthOf(ui::Widget* w) {
        if (auto* b = dynamic_cast<ui::Button*>(w)) return buttonWidth(b->text());
        if (auto* rb = dynamic_cast<ui::RadioButton*>(w)) return ui::measureWidth(rb->label(), kButton) + 34.f;
        return 0.f;
    }

    float textHeight(const Item& i, float w) const {
        switch (i.kind) {
            case Item::Kind::Title: return 26.f;
            case Item::Kind::Text:
            case Item::Kind::Muted:
            case Item::Kind::Warning: return static_cast<float>(wrapWords(i.text, kSmall, w - i.indent).size()) * kSmallLine + 4.f;
            default: return i.height;
        }
    }

    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(wanted, std::max(420.f, r.w - 40.f));
        const float cw = w - 2 * kPad;
        float content = 0;
        for (const auto& i : items) content += textHeight(i, cw);
        const float errorH = error.empty() ? 0.f : static_cast<float>(wrapWords(error, kSmall, cw).size()) * kSmallLine + 8.f;
        const float h = std::min(std::max(240.f, r.h - 40.f), ui::dialogframe::kTitleBand + 14.f + content + errorH + 64.f);
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        const float x = panel_.x + kPad;
        float y = panel_.y + ui::dialogframe::kTitleBand + 14.f;
        at_.clear();
        for (auto& i : items) {
            at_.push_back(y);
            const float ih = textHeight(i, cw);
            if (i.kind == Item::Kind::Widget && !i.widgets.empty()) {
                const float ww = i.width > 0 ? std::min(i.width, cw - i.indent) : cw - i.indent;
                i.widgets[0]->setBounds({x + i.indent, y, ww, i.height});
            } else if (i.kind == Item::Kind::Row) {
                float bx = x + i.indent;
                // Les champs (sans largeur propre) se partagent ce qui reste.
                float fixed = 0;
                int stretch = 0;
                for (auto* wd : i.widgets) {
                    const float f = widthOf(wd);
                    if (f > 0) fixed += f;
                    else ++stretch;
                }
                const float gapW = 10.f;
                const float rest = std::max(60.f, cw - i.indent - fixed - gapW * static_cast<float>(i.widgets.size() - 1));
                for (auto* wd : i.widgets) {
                    float ww = widthOf(wd);
                    if (ww <= 0) ww = stretch ? rest / static_cast<float>(stretch) : rest;
                    wd->setBounds({bx, y, ww, i.height});
                    bx += ww + gapW;
                }
            } else if (i.kind == Item::Kind::Code && !i.widgets.empty()) {
                const float bw = widthOf(i.widgets[0]);
                i.widgets[0]->setBounds({x + cw - bw - 8.f, y + 7.f, bw, 30.f});
            }
            y += ih;
        }
        errorAt_ = panel_.bottom() - 52.f - errorH;
        float bx = panel_.right() - kPad;
        for (auto it = footer.rbegin(); it != footer.rend(); ++it) {
            const float bw = buttonWidth((*it)->text());
            bx -= bw;
            (*it)->setBounds({bx, panel_.bottom() - 46.f, bw, 30.f});
            bx -= 10.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        const float band = ui::dialogframe::kTitleBand;
        r.fillRect({panel_.x, panel_.y, panel_.w, band}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold;
        r.drawText({panel_.x + 12.f, panel_.y + (band - r.lineHeight(bold)) * 0.5f}, title, bold, c.text);
        const float x = panel_.x + kPad, cw = panel_.w - 2 * kPad;
        for (std::size_t n = 0; n < items.size() && n < at_.size(); ++n) {
            const auto& i = items[n];
            const float y = at_[n];
            switch (i.kind) {
                case Item::Kind::Title: r.drawText({x + i.indent, y + 4.f}, i.text, bold, c.text); break;
                case Item::Kind::Text:
                case Item::Kind::Muted:
                case Item::Kind::Warning: {
                    const auto col = i.kind == Item::Kind::Text ? c.text : i.kind == Item::Kind::Warning ? c.warning : c.textMuted;
                    float ly = y + 1.f;
                    for (const auto& l : wrapWords(i.text, kSmall, cw - i.indent)) {
                        r.drawText({x + i.indent, ly}, l, kSmall, col);
                        ly += kSmallLine;
                    }
                    break;
                }
                case Item::Kind::Code: {
                    const gfx::Rect box{x, y, cw, 44.f};
                    r.fillRoundedRect(box, c.inputBg, 6.f);
                    r.drawText({box.x + 12.f, box.y + (44.f - r.lineHeight(ctx.theme.font.mono)) * 0.5f}, i.text, ctx.theme.font.mono, c.text);
                    break;
                }
                default: break;
            }
        }
        if (!error.empty()) {
            float ly = errorAt_;
            for (const auto& l : wrapWords(error, kSmall, cw)) {
                r.drawText({x, ly}, l, kSmall, c.warning);
                ly += kSmallLine;
            }
        }
    }

private:
    gfx::Rect panel_{};
    std::vector<float> at_;
    float errorAt_{0};
};

// Les traits d'une fenetre de session (comme celle de l'equipe).
menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

// ------------------------------------------------------------------------------ Demarrer une session ---
class StartDialog final : public menu::WidgetMenu {
public:
    explicit StartDialog(App& app) : menu::WidgetMenu("dialog.session.demarrer"), app_(app) {
        project_ = std::filesystem::path(app.projectFolder()).filename().string();
        if (project_.empty()) project_ = app.manifest().name;
        name_ = "Mise en service " + project_;
        code_ = session::newRelayCode();
        internetOk_ = SessionService::internetAvailable(&internetWhy_);
    }
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "D\xC3\xA9marrer une session"; }

    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Escape) {
                manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Return) {
                start();
                return ui::EventResult::Consumed;
            }
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }

protected:
    core::Status buildUi() override {
        rebuild();
        return core::ok();
    }

private:
    void rebuild() {
        if (nameField_) name_ = nameField_->text();
        links_.clear();
        auto body = std::make_unique<FlowBody>();
        body->title = "D\xC3\xA9marrer une session";
        body->error = error_;
        using K = FlowBody::Item::Kind;
        const auto radio = [&](const std::string& text, const std::shared_ptr<ui::RadioGroup>& g, int v, const std::string& id) {
            return &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>(text, g, v, "dialog.session.demarrer." + id)));
        };
        const auto button = [&](const std::string& text, const std::string& id, ui::Button::Style st) {
            auto* b = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, "dialog.session.demarrer." + id)));
            b->setStyle(st);
            return b;
        };
        body->add(K::Muted, "Tes \xC3\xA9quipiers ouvrent " + project_ + " avec toi et vous travaillez en m\xC3\xAA" "me temps. Tu es l'h\xC3\xB4te : ton PC garde le projet et "
                            "l'enregistre (il est enregistr\xC3\xA9 avant de d\xC3\xA9marrer).");
        body->gap(6.f);
        body->add(K::Title, "Nom de la session");
        auto f = std::make_unique<ui::InputText>("dialog.session.demarrer.nom");
        f->setText(name_);
        nameField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        body->widget(nameField_);
        body->gap();
        body->add(K::Title, "O\xC3\xB9");
        where_ = std::make_shared<ui::RadioGroup>();
        body->widget(radio("R\xC3\xA9seau du bureau", where_, 0, "ou.bureau"), 26.f);
        body->add(K::Muted, "Les PC du m\xC3\xAA" "me r\xC3\xA9seau la voient seuls dans \xC2\xAB Rejoindre une session \xC2\xBB. Rien ne sort du bureau.", 28.f);
        auto* net = radio("Internet (par le service XPG, chiffr\xC3\xA9 de bout en bout)", where_, 1, "ou.internet");
        net->setEnabled(internetOk_);
        body->widget(net, 26.f);
        if (internetOk_)
            body->add(K::Muted, "Pour un \xC3\xA9quipier \xC3\xA0 distance : il tape le code. Le service XPG ne fait que passer des donn\xC3\xA9" "es chiffr\xC3\xA9" "es ; "
                                "il ne voit ni le code, ni le projet.", 28.f);
        else body->add(K::Warning, "Pas ici : " + internetWhy_ + ".", 28.f);
        where_->setValue(internet_ && internetOk_ ? 1 : 0);
        links_ += where_->valueChanged->connect([this](int v) {
            internet_ = v == 1;
            error_.clear();
            rebuildLater_ = true;
        });
        body->gap();
        if (internet_ && internetOk_) {
            body->add(K::Title, "Le code de la session");
            auto* copy = button("Copier", "copier", ui::Button::Style::Default);
            links_ += copy->clicked->connect([this] {
                ui::setClipboardText(session::relayCodeShown(code_));
                app_.events().publish(StatusNotice{"Le code est copi\xC3\xA9 : donne-le \xC3\xA0 tes \xC3\xA9quipiers (il ne sert que pendant la session).", 5.0});
            });
            body->code("Code de la session : " + session::relayCodeShown(code_), copy);
            body->add(K::Muted, "Il faut le code pour entrer : tes \xC3\xA9quipiers le tapent dans Rejoindre une session \xE2\x80\xBA Par Internet. Il change \xC3\xA0 "
                                "chaque session et ne sert plus quand elle s'arr\xC3\xAAte.");
        } else {
            body->add(K::Title, "Qui peut entrer");
            entry_ = std::make_shared<ui::RadioGroup>();
            body->widget(radio("J'accepte chaque entr\xC3\xA9" "e", entry_, 0, "entree.accepter"), 26.f);
            body->widget(radio("Il faut le code (6 chiffres)", entry_, 1, "entree.code"), 26.f);
            entry_->setValue(code6_ ? 1 : 0);
            links_ += entry_->valueChanged->connect([this](int v) { code6_ = v == 1; });
        }
        body->gap();
        body->add(K::Title, "En entrant, ils");
        role_ = std::make_shared<ui::RadioGroup>();
        body->row({radio("Modifient", role_, 0, "role.modifier"), radio("Regardent seulement", role_, 1, "role.regarder")}, 26.f);
        role_->setValue(watch_ ? 1 : 0);
        links_ += role_->valueChanged->connect([this](int v) { watch_ = v == 1; });
        body->gap(8.f);
        body->add(K::Muted, "Jusqu'\xC3\xA0 8 personnes ; chacun a sa propre licence d'IHM Studio. Si tu fermes le projet, la session s'arr\xC3\xAAte pour tout le monde.");
        auto* cancel = button("Annuler", "annuler", ui::Button::Style::Default);
        auto* go = button("D\xC3\xA9marrer la session", "ok", ui::Button::Style::Primary);
        links_ += cancel->clicked->connect([this] { manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}}); });
        links_ += go->clicked->connect([this] { start(); });
        body->footer = {cancel, go};
        body_ = body.get();
        setRoot(std::move(body));
        focus().clear();
        focus().focus(nameField_);
    }

    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (rebuildLater_) {
            rebuildLater_ = false;
            rebuild();
        }
    }

    void start() {
        if (nameField_) name_ = nameField_->text();
        if (app_.pendingChanges() > 0) {
            if (auto saved = app_.saveProject(); !saved) {
                error_ = "Le projet ne s'enregistre pas : " + saved.error().message();
                rebuildLater_ = true;   // pas ici : le bouton qui appelle serait detruit
                return;
            }
        }
        SessionService::StartOptions o;
        o.name = name_;
        o.internet = internet_ && internetOk_;
        o.code = code_;
        o.entry = o.internet || code6_ ? session::Entry::Code : session::Entry::Accept;
        o.role = watch_ ? session::Role::Watch : session::Role::Edit;
        std::string why;
        if (!app_.session().start(o, &why)) {
            error_ = "La session ne d\xC3\xA9marre pas : " + why + ".";
            rebuildLater_ = true;
            return;
        }
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
    }

    App& app_;
    std::string project_, name_, code_, internetWhy_, error_;
    bool internetOk_{false}, internet_{false}, code6_{false}, watch_{false}, rebuildLater_{false};
    std::shared_ptr<ui::RadioGroup> where_, entry_, role_;
    ui::InputText* nameField_{nullptr};
    FlowBody* body_{nullptr};
    core::ConnectionScope links_;
};

// ------------------------------------------------------------------------------ Rejoindre une session ---
//  1.44.0 : deux choix en tete, en toutes lettres - << Sur le reseau du bureau >> (les sessions trouvees seules,
//  Actualiser ; le code SEULEMENT si la session choisie le demande ; << Une autre adresse... >> replie) et << Par
//  Internet >> (le code donne par l'hote, rien d'autre). Plus d'adresse ni de code exiges d'office (1.43).
int& lastJoinMode() {
    static int m = 0;   // 0 : le reseau du bureau ; 1 : par Internet (le dernier choix, pendant que le programme tourne)
    return m;
}

class JoinDialog final : public menu::WidgetMenu {
public:
    explicit JoinDialog(App& app) : menu::WidgetMenu("dialog.session.rejoindre"), app_(app) {
        internetOk_ = SessionService::internetAvailable(&internetWhy_);
        mode_ = internetOk_ ? lastJoinMode() : 0;
    }
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Rejoindre une session"; }

    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Escape) {
                manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Return) {
                if (addressField_ && addressField_->focused()) searchAddress();
                else join();
                return ui::EventResult::Consumed;
            }
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }

    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        auto& s = app_.session();
        const bool searching = s.searching();
        if (searching != wasSearching_) {
            wasSearching_ = searching;
            if (!searching) {
                found_ = s.found();
                // Une adresse tapee : sa session est choisie d'office.
                if (!askedAddress_.empty())
                    for (std::size_t i = 0; i < found_.size(); ++i)
                        if (found_[i].address == askedAddress_ || found_[i].announce.machine == askedAddress_) chosen_ = keyOf(found_[i]);
                if (!askedAddress_.empty() && chosen_.empty())
                    error_ = "Aucune session ne r\xC3\xA9pond sur " + askedAddress_ + " : v\xC3\xA9rifie l'adresse, et que l'h\xC3\xB4te a d\xC3\xA9marr\xC3\xA9 la session.";
                askedAddress_.clear();
                if (chosen_.empty() && found_.size() == 1) chosen_ = keyOf(found_.front());
            }
            rebuildLater_ = true;
        }
        if (rebuildLater_) {
            rebuildLater_ = false;
            rebuild();
        }
    }

protected:
    core::Status buildUi() override {
        if (!app_.session().searching()) app_.session().search();
        wasSearching_ = app_.session().searching();
        rebuild();
        return core::ok();
    }

private:
    static std::string keyOf(const session::Found& f) { return f.announce.sessionId + "@" + f.address; }
    const session::Found* chosen() const {
        for (const auto& f : found_)
            if (keyOf(f) == chosen_) return &f;
        return nullptr;
    }

    void keepTyped() {
        if (lanCode_) lanCodeText_ = lanCode_->text();
        if (netCode_) netCodeText_ = netCode_->text();
        if (addressField_) addressText_ = addressField_->text();
    }

    void rebuild() {
        keepTyped();
        lanCode_ = netCode_ = addressField_ = nullptr;
        links_.clear();
        auto body = std::make_unique<FlowBody>();
        body->title = "Rejoindre une session";
        body->error = error_;
        body->wanted = 660.f;
        using K = FlowBody::Item::Kind;
        const auto radio = [&](const std::string& text, const std::shared_ptr<ui::RadioGroup>& g, int v, const std::string& id) {
            return &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>(text, g, v, "dialog.session.rejoindre." + id)));
        };
        const auto button = [&](const std::string& text, const std::string& id, ui::Button::Style st) {
            auto* b = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, "dialog.session.rejoindre." + id)));
            b->setStyle(st);
            return b;
        };
        const auto field = [&](const std::string& id, const std::string& text, const std::string& placeholder) {
            auto f = std::make_unique<ui::InputText>("dialog.session.rejoindre." + id);
            f->setText(text);
            f->setPlaceholder(placeholder);
            return &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        };
        body->add(K::Muted, "Ouvre le projet d'un \xC3\xA9quipier, avec lui, en m\xC3\xAA" "me temps. Comment le rejoins-tu ?");
        modeGroup_ = std::make_shared<ui::RadioGroup>();
        auto* lan = radio("Sur le r\xC3\xA9seau du bureau", modeGroup_, 0, "bureau");
        auto* net = radio("Par Internet", modeGroup_, 1, "internet");
        net->setEnabled(internetOk_);
        body->row({lan, net}, 28.f);
        modeGroup_->setValue(mode_);
        links_ += modeGroup_->valueChanged->connect([this](int v) {
            mode_ = v;
            lastJoinMode() = v;
            error_.clear();
            rebuildLater_ = true;
        });
        body->gap(8.f);
        ui::Widget* focusOn = nullptr;
        if (mode_ == 0) {
            auto& s = app_.session();
            const bool searching = s.searching();
            auto* refresh = button(searching ? "Recherche\xE2\x80\xA6" : "Actualiser", "actualiser", ui::Button::Style::Default);
            refresh->setEnabled(!searching);
            links_ += refresh->clicked->connect([this] {
                error_.clear();
                app_.session().search();
                rebuildLater_ = true;
            });
            auto* heading = &static_cast<ui::Widget&>(body->addChild(std::make_unique<HeadingLabel>(
                searching ? std::string("Les sessions du r\xC3\xA9seau du bureau \xC2\xB7 recherche\xE2\x80\xA6")
                          : "Les sessions du r\xC3\xA9seau du bureau \xC2\xB7 " + std::to_string(found_.size()))));
            body->row({heading, refresh}, 30.f);
            if (found_.empty()) {
                body->add(K::Muted, searching ? std::string("On cherche les sessions d\xC3\xA9marr\xC3\xA9" "es sur le r\xC3\xA9seau du bureau (et sur ce PC)\xE2\x80\xA6")
                                              : std::string("Aucune session sur le r\xC3\xA9seau du bureau pour l'instant : l'h\xC3\xB4te doit l'avoir d\xC3\xA9marr\xC3\xA9" "e. "
                                                            "\xC2\xAB Actualiser \xC2\xBB cherche de nouveau."));
            } else {
                sessions_ = std::make_shared<ui::RadioGroup>();
                int index = 0, chosenIndex = -1;
                for (const auto& f : found_) {
                    if (index >= 8) break;
                    body->widget(radio(label(f), sessions_, index, "session." + std::to_string(index + 1)), 26.f);
                    body->add(K::Muted, detail(f), 28.f);
                    if (keyOf(f) == chosen_) chosenIndex = index;
                    ++index;
                }
                sessions_->setValue(chosenIndex);
                links_ += sessions_->valueChanged->connect([this](int v) {
                    if (v >= 0 && static_cast<std::size_t>(v) < found_.size()) chosen_ = keyOf(found_[static_cast<std::size_t>(v)]);
                    error_.clear();
                    rebuildLater_ = true;
                });
            }
            if (const auto* c = chosen(); c && c->announce.needsCode) {
                body->gap(6.f);
                body->add(K::Title, "Le code de la session (6 chiffres, donn\xC3\xA9 par l'h\xC3\xB4te)");
                lanCode_ = field("code", lanCodeText_, "482 913");
                body->widget(lanCode_, 30.f, 220.f);
                focusOn = lanCode_;
            }
            body->gap(6.f);
            auto* other = button(otherOpen_ ? "Une autre adresse \xE2\x96\xB4" : "Une autre adresse\xE2\x80\xA6", "autre", ui::Button::Style::Flat);
            links_ += other->clicked->connect([this] {
                otherOpen_ = !otherOpen_;
                rebuildLater_ = true;
            });
            body->widget(other, 28.f, buttonWidth(other->text()));
            if (otherOpen_) {
                body->add(K::Muted, "Si la session n'appara\xC3\xAEt pas seule (un autre r\xC3\xA9seau, un VPN) : le nom ou l'adresse du PC de l'h\xC3\xB4te.", 0.f);
                addressField_ = field("adresse", addressText_, "192.168.1.20");
                auto* look = button("Chercher", "chercher", ui::Button::Style::Default);
                links_ += look->clicked->connect([this] { searchAddress(); });
                body->row({addressField_, look}, 30.f);
                if (!focusOn) focusOn = addressField_;
            }
        } else {
            body->add(K::Title, "Le code de la session (donn\xC3\xA9 par l'h\xC3\xB4te)");
            netCode_ = field("code-internet", netCodeText_, "K7Q4-M2XP-T9");
            netCode_->setMaxLength(24);
            body->widget(netCode_, 32.f, 260.f);
            body->add(K::Muted, "Le code ne quitte pas ce PC : il trouve la session sur le service XPG et chiffre tout, de bout en bout. Il ne sert que "
                                "tant que la session est ouverte.");
            if (!internetOk_) body->add(K::Warning, "Pas ici : " + internetWhy_ + ".");
            focusOn = netCode_;
        }
        body->gap(8.f);
        body->add(K::Muted, "Tu entres avec ton nom (Mon profil) ; le projet arrive sur ce PC, \xC3\xA0 part de tes projets. C'est l'h\xC3\xB4te qui enregistre.");
        auto* cancel = button("Annuler", "annuler", ui::Button::Style::Default);
        auto* go = button("Rejoindre", "ok", ui::Button::Style::Primary);
        go->setEnabled(mode_ == 1 ? internetOk_ : chosen() != nullptr);
        links_ += cancel->clicked->connect([this] { manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}}); });
        links_ += go->clicked->connect([this] { join(); });
        body->footer = {cancel, go};
        setRoot(std::move(body));
        focus().clear();
        if (focusOn) focus().focus(focusOn);
    }

    // Un titre dans une ligne (a cote d'Actualiser).
    class HeadingLabel final : public ui::Widget {
    public:
        explicit HeadingLabel(std::string text) : text_(std::move(text)) {}

    protected:
        void onPaint(const ui::PaintContext& ctx) override {
            const auto b = bounds();
            ctx.r.drawText({b.x, b.y + (b.h - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, text_, ctx.theme.font.uiBold, ctx.theme.color.text);
        }

    private:
        std::string text_;
    };

    void searchAddress() {
        keepTyped();
        std::string a = addressText_;
        while (!a.empty() && a.back() == ' ') a.pop_back();
        while (!a.empty() && a.front() == ' ') a.erase(a.begin());
        if (a.empty()) {
            error_ = "Tape le nom ou l'adresse du PC de l'h\xC3\xB4te.";
            rebuildLater_ = true;
            return;
        }
        auto& s = app_.session();
        if (s.searching()) return;
        error_.clear();
        askedAddress_ = a;
        s.search({a});
        rebuildLater_ = true;
    }

    void join() {
        keepTyped();
        SessionService::JoinTarget t;
        std::string code;
        if (mode_ == 1) {
            std::string norm;
            if (!session::relayCodeTyped(netCodeText_, norm)) {
                error_ = "Tape le code donn\xC3\xA9 par l'h\xC3\xB4te : 10 caract\xC3\xA8res, comme K7Q4-M2XP-T9.";
                rebuildLater_ = true;
                return;
            }
            t.internet = true;
            t.sessionName = "Session par Internet";
            t.machine = "Internet";
            code = norm;
        } else {
            const auto* c = chosen();
            if (!c) {
                error_ = "Choisis une session de la liste.";
                rebuildLater_ = true;
                return;
            }
            t = targetOf(*c);
            if (t.needsCode) {
                code = session::codeTyped(lanCodeText_);
                if (code.size() != static_cast<std::size_t>(session::kCodeDigits)) {
                    error_ = "Cette session demande son code : les 6 chiffres donn\xC3\xA9s par l'h\xC3\xB4te.";
                    rebuildLater_ = true;
                    return;
                }
            }
        }
        std::string why;
        if (!app_.session().join(t, code, &why)) {
            error_ = "Impossible : " + why + ".";
            rebuildLater_ = true;
            return;
        }
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
    }

    App& app_;
    int mode_{0};
    bool internetOk_{false}, otherOpen_{false}, wasSearching_{false}, rebuildLater_{false};
    std::string internetWhy_, error_, chosen_, askedAddress_;
    std::string lanCodeText_, netCodeText_, addressText_;
    std::vector<session::Found> found_;
    std::shared_ptr<ui::RadioGroup> modeGroup_, sessions_;
    ui::InputText* lanCode_{nullptr};
    ui::InputText* netCode_{nullptr};
    ui::InputText* addressField_{nullptr};
    core::ConnectionScope links_;
};

} // namespace

void showSessionPanel(App& app) { app.menus().ShowDialog(std::make_unique<SessionPanel>(app), [](const menu::DialogResult&) {}); }

void showSessionStart(App& app) {
    auto& s = app.session();
    if (s.active() || s.mode() == SessionService::Mode::Joining) {
        showSessionPanel(app);
        return;
    }
    if (!app.hmi() || app.projectFolder().empty()) {
        app.menus().ShowDialog(std::make_unique<MessageDialog>("D\xC3\xA9marrer une session", "Ouvre d'abord le projet IHM \xC3\xA0 partager.", MessageDialog::Icon::Info),
                               [](const menu::DialogResult&) {});
        return;
    }
    // 1.44.0 : une fenetre a elle (Ou : le reseau du bureau ou Internet ; le code montre et copiable avant de demarrer).
    app.menus().ShowDialog(std::make_unique<StartDialog>(app), [&app](const menu::DialogResult& r) {
        if (r.accepted()) showSessionPanel(app);
    });
}

void showSessionJoin(App& app) {
    auto& s = app.session();
    if (s.active() || s.mode() == SessionService::Mode::Joining) {
        showSessionPanel(app);
        return;
    }
    // 1.44.0 : une fenetre a elle (Sur le reseau du bureau / Par Internet), aussi depuis l'accueil (aucun projet ouvert).
    app.menus().ShowDialog(std::make_unique<JoinDialog>(app), [&app](const menu::DialogResult& r) {
        if (r.accepted()) showSessionPanel(app);
    });
}

} // namespace app
