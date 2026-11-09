// =============================================================================
//  app/SessionDialogs.cpp - 1.43.0 : voir SessionDialogs.hpp
// =============================================================================
#include "SessionDialogs.hpp"

#include "App.hpp"
#include "SessionService.hpp"
#include "screens/Screens.hpp"

#include "../menu/MenuManager.hpp"
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
    struct Row { ui::Button* follow{nullptr}; ui::Button* role{nullptr}; int member{0}; };
    struct KnockRow { ui::Button* yes{nullptr}; ui::Button* no{nullptr}; int conn{0}; };
    std::vector<Row> rows;
    std::vector<KnockRow> knockRows;
    ui::Button* copy{nullptr};
    ui::InputText* field{nullptr};
    ui::Button* send{nullptr};
    ui::Button* stop{nullptr};
    ui::Button* close{nullptr};

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
            for (auto* b : {row.follow, row.role}) {
                if (!b) continue;
                const float bw = std::min(160.f, buttonWidth(b->text()) - 10.f);
                bx -= bw;
                b->setBounds({bx, y + 12.f, bw, 26.f});
                bx -= 6.f;
            }
            textRight_.push_back(bx - 6.f);   // le texte de la ligne s'arrete avant ses boutons
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
            r.drawText({box.x + 10.f, box.y + 3.f}, "Le code de la session", kSmall, c.textMuted);
            r.drawText({box.x + 10.f, box.y + 18.f}, session::codeShown(s.code()), ctx.theme.font.mono, c.text);
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
        // La discussion.
        r.drawText({right_.x, right_.y}, "La discussion", bold, c.text);
        const gfx::Rect box{right_.x, right_.y + 24.f, right_.w, right_.h - 24.f - 40.f};
        r.fillRoundedRect(box, c.inputBg, 6.f);
        std::vector<std::pair<std::string, gfx::Color>> lines;
        for (const auto& l : s.chat()) {
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
                                          : "C'est " + s.hostName() + " qui enregistre. Ctrl+Z annule tes gestes, pas ceux des autres.";
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
        std::string k = std::to_string(static_cast<int>(s.mode())) + "|" + (s.code().empty() ? "" : "c");
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
                    ui::setClipboardText(app_.session().code());
                    app_.events().publish(StatusNotice{"Le code est copi\xC3\xA9 : donne-le \xC3\xA0 tes \xC3\xA9quipiers (il ne sert que pendant la session).", 5.0});
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

    App& app_;
    PanelBody* body_{nullptr};
    ui::InputText* field_{nullptr};
    core::ConnectionScope links_;
    std::string built_;
    std::uint64_t seen_{~0ull};
};

// Ce que le dialogue Rejoindre propose.
struct JoinState {
    std::vector<session::Found> found;
    std::string pendingAddress, pendingCode;
};
JoinState& joinState() {
    static JoinState s;
    return s;
}

std::string label(const session::Found& f) {
    const auto& a = f.announce;
    return a.session + " \xE2\x80\x94 " + a.host + " \xC2\xB7 " + a.machine + " \xC2\xB7 " + a.app + " " + a.version + " \xC2\xB7 " + std::to_string(a.members)
         + " dedans" + (a.needsCode ? " \xC2\xB7 avec code" : "");
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

void joinNow(App& app, const SessionService::JoinTarget& t, const std::string& code) {
    std::string why;
    if (!app.session().join(t, code, &why)) {
        app.menus().ShowDialog(std::make_unique<MessageDialog>("Rejoindre une session", "Impossible : " + why + ".", MessageDialog::Icon::Warning),
                               [](const menu::DialogResult&) {});
        return;
    }
    showSessionPanel(app);
}

void askJoin(App& app) {
    auto& st = joinState();
    st.found = app.session().found();
    std::vector<std::string> choices;
    for (const auto& f : st.found) choices.push_back(label(f));
    const std::string other = "Une autre, par l'adresse de son PC";
    choices.push_back(other);
    std::string intro = st.found.empty()
        ? "Aucune session sur le r\xC3\xA9seau du bureau pour l'instant (l'h\xC3\xB4te doit l'avoir d\xC3\xA9marr\xC3\xA9" "e). Tu peux taper l'adresse de son PC."
        : std::to_string(st.found.size()) + " session" + (st.found.size() > 1 ? "s" : "") + " sur le r\xC3\xA9seau du bureau. Choisis-la ; tape son code si elle en demande un.";
    intro += "\n\nTu entres avec ton nom (Mon profil) ; le projet arrive sur ce PC, \xC3\xA0 part de tes projets. C'est l'h\xC3\xB4te qui enregistre.";
    std::vector<FormDialog::Field> fields;
    fields.push_back(FormDialog::Field{"Session", choices.front(), "", false, choices});
    fields.push_back(FormDialog::Field{"Adresse du PC (sinon)", "", "192.168.1.20", false, {}});
    fields.push_back(FormDialog::Field{"Code (si elle le demande)", "", "6 chiffres", false, {}});
    app.menus().ShowDialog(std::make_unique<FormDialog>("dialog.session.rejoindre", "Rejoindre une session", intro, std::move(fields), "Rejoindre"),
                           [&app, other](const menu::DialogResult& r) {
                               if (!r.accepted()) return;
                               const auto v = FormDialog::split(r.payload);
                               if (v.size() < 3) return;
                               auto& st = joinState();
                               for (const auto& f : st.found)
                                   if (label(f) == v[0]) {
                                       joinNow(app, targetOf(f), v[2]);
                                       return;
                                   }
                               if (v[1].empty()) {
                                   app.events().publish(StatusNotice{"Tape l'adresse du PC de l'h\xC3\xB4te (ou choisis une session de la liste).", 6.0});
                                   return;
                               }
                               // Une adresse : la question lui est posee directement, puis on entre dans ce qu'elle annonce.
                               st.pendingAddress = v[1];
                               st.pendingCode = v[2];
                               app.events().publish(StatusNotice{"Recherche de la session sur " + v[1] + "\xE2\x80\xA6", 3.0});
                               app.session().search({v[1]});
                           });
}

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
    std::string project = std::filesystem::path(app.projectFolder()).filename().string();
    if (project.empty()) project = app.manifest().name;
    std::vector<FormDialog::Field> fields;
    fields.push_back(FormDialog::Field{"Nom de la session", "Mise en service " + project, "", false, {}});
    fields.push_back(FormDialog::Field{"Qui peut entrer", "J'accepte chaque entr\xC3\xA9" "e", "",
                                       false, {"J'accepte chaque entr\xC3\xA9" "e", "Il faut le code (6 chiffres)"}});
    fields.push_back(FormDialog::Field{"En entrant, ils", "Modifient", "", false, {"Modifient", "Regardent seulement"}});
    const std::string intro = "Tes \xC3\xA9quipiers ouvrent " + project + " avec toi et vous travaillez en m\xC3\xAA" "me temps, sur le r\xC3\xA9seau du bureau. "
                              "Tu es l'h\xC3\xB4te : ton PC garde le projet et l'enregistre (il est enregistr\xC3\xA9 avant de d\xC3\xA9marrer).\n\n"
                              "Chacun son objet : un objet choisi par l'un est pris pour lui. Ctrl+Z annule ses propres gestes. "
                              "Jusqu'\xC3\xA0 8 personnes ; chacun a sa propre licence d'IHM Studio. Si tu fermes le projet, la session s'arr\xC3\xAAte pour tout le monde.";
    app.menus().ShowDialog(std::make_unique<FormDialog>("dialog.session.demarrer", "D\xC3\xA9marrer une session", intro, std::move(fields), "D\xC3\xA9marrer la session"),
                           [&app](const menu::DialogResult& r) {
                               if (!r.accepted()) return;
                               const auto v = FormDialog::split(r.payload);
                               if (v.size() < 3) return;
                               if (app.pendingChanges() > 0) {
                                   if (auto saved = app.saveProject(); !saved) {
                                       app.menus().ShowDialog(std::make_unique<MessageDialog>("D\xC3\xA9marrer une session",
                                                                                              "Le projet ne s'enregistre pas : " + saved.error().message(),
                                                                                              MessageDialog::Icon::Error),
                                                              [](const menu::DialogResult&) {});
                                       return;
                                   }
                               }
                               SessionService::StartOptions o;
                               o.name = v[0];
                               o.entry = v[1].rfind("Il faut", 0) == 0 ? session::Entry::Code : session::Entry::Accept;
                               o.role = v[2] == "Regardent seulement" ? session::Role::Watch : session::Role::Edit;
                               std::string why;
                               if (!app.session().start(o, &why)) {
                                   app.menus().ShowDialog(std::make_unique<MessageDialog>("D\xC3\xA9marrer une session", "La session ne d\xC3\xA9marre pas : " + why + ".",
                                                                                          MessageDialog::Icon::Warning),
                                                          [](const menu::DialogResult&) {});
                                   return;
                               }
                               showSessionPanel(app);
                           });
}

void showSessionJoin(App& app) {
    auto& s = app.session();
    if (s.active() || s.mode() == SessionService::Mode::Joining) {
        showSessionPanel(app);
        return;
    }
    joinState() = JoinState{};
    app.events().publish(StatusNotice{"Recherche des sessions sur le r\xC3\xA9seau du bureau\xE2\x80\xA6", 2.0});
    s.onSearchDone = [&app] {
        auto& st = joinState();
        if (!st.pendingAddress.empty()) {
            const std::string address = st.pendingAddress, code = st.pendingCode;
            st.pendingAddress.clear();
            const auto& found = app.session().found();
            if (found.empty()) {
                app.menus().ShowDialog(std::make_unique<MessageDialog>("Rejoindre une session",
                                                                       "Aucune session ne r\xC3\xA9pond sur " + address + ". V\xC3\xA9rifie l'adresse, et que l'h\xC3\xB4te a d\xC3\xA9marr\xC3\xA9 la session.",
                                                                       MessageDialog::Icon::Warning),
                                       [](const menu::DialogResult&) {});
                return;
            }
            joinNow(app, targetOf(found.front()), code);
            return;
        }
        askJoin(app);
    };
    s.search();
}

} // namespace app
