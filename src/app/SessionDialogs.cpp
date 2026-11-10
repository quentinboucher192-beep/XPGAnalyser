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
#include "SessionWindow.hpp"   // 1.47.0 : le contenu de la fenetre de la session (le texte dans son cadre)
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

const gfx::FontId kButton{16};

float buttonWidth(const std::string& text) { return std::max(88.f, ui::measureWidth(text, kButton) + 32.f); }

std::string pcText(const session::Member& m) {
    std::string t = m.machine;
    if (!m.machineId.empty()) t += (t.empty() ? "" : " \xC2\xB7 ") + m.machineId;
    return t;
}

// ------------------------------------------------------------------------------------------- la fenetre ---
//  1.47.0 : son contenu est app/SessionWindow (sessionwin::Body) : le texte dans son cadre a toute taille. Ici : ce
//  qu'elle montre (snapshotOf, lu dans le service des sessions) et ses boutons (crees, relies).
sessionwin::Snapshot snapshotOf(const SessionService& s, bool journal) {
    sessionwin::Snapshot sn;
    sn.active = s.active();
    sn.title = s.active() ? "Session \xC2\xAB " + s.sessionName() + " \xC2\xBB" : std::string("Session");
    if (s.active()) sn.title += s.host() ? " \xE2\x80\x94 tu es l'h\xC3\xB4te" : " \xE2\x80\x94 h\xC3\xB4te : " + s.hostName();
    if (!s.active()) {
        sn.progress = s.mode() == SessionService::Mode::Joining ? s.progress() : (s.progress().empty() ? "Pas de session en cours." : s.progress());
        return sn;
    }
    sn.teamTitle = "L'\xC3\xA9quipe \xC2\xB7 " + std::to_string(s.members().size());
    if (s.host() && !s.code().empty()) {
        sn.code = true;
        // 1.44.0 : par Internet, le code de 10 caracteres (et si la session est deja ouverte sur le service XPG).
        sn.codeLabel = !s.internet() ? std::string("Le code de la session")
                     : s.relayOpen() ? std::string("Code de la session \xC2\xB7 par Internet, chiffr\xC3\xA9" "e de bout en bout")
                                     : std::string("Code de la session \xC2\xB7 ouverture sur le service XPG\xE2\x80\xA6");
        sn.codeText = s.codeText();
    }
    for (const auto& m : s.members()) {
        sessionwin::Person p;
        p.number = m.number;
        p.name = m.name;
        if (m.number == s.me()) p.tag = "(toi)";
        if (m.number == 0) p.tag += std::string(p.tag.empty() ? "" : " ") + "\xC2\xB7 h\xC3\xB4te";
        p.initials = session::initials(m.name);
        p.color = m.color;
        p.where = pcText(m) + " \xC2\xB7 " + session::roleText(m.role);
        if (const auto v = s.viewName(m.view); !v.empty()) p.where += " \xC2\xB7 " + v;
        if (!m.selection.empty()) p.where += " \xC2\xB7 " + std::to_string(m.selection.size()) + " objet" + (m.selection.size() > 1 ? "s" : "");
        sn.people.push_back(std::move(p));
    }
    for (const auto& k : s.knocks())
        sn.knocks.push_back({k.name, k.machine + (k.machineId.empty() ? std::string{} : " \xC2\xB7 " + k.machineId) + " \xC2\xB7 " + k.address});
    const auto* me = s.member(s.me());
    for (const auto& l : s.chat())
        sn.chat.push_back(sessionui::Row{l.time, l.from, l.text, l.system ? 0u : l.color, false, l.system, !l.system && me && l.from == me->name, 1});
    for (const auto& e : s.journal().entries())
        sn.journal.push_back(sessionui::Row{session::clockText(e.t), e.who, e.text, e.color, true, e.member < 0, e.member == s.me(), e.count});
    sn.showJournal = journal;
    sn.hint = s.host() ? "Tu enregistres pour tout le monde (Ctrl+S). Fermer le projet arr\xC3\xAAte la session."
                       : "C'est " + s.hostName() + " qui enregistre ; ta copie s'efface quand tu pars. Ctrl+Z annule tes gestes.";
    return sn;
}

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
            body_->setSnapshot(snapshotOf(app_.session(), journal_));   // 1.47.0 : les lignes, les noms, le code du moment
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
        auto body = std::make_unique<sessionwin::Body>("dialog.session.corps");
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
                sessionwin::Body::MemberButtons row;
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
                    sessionwin::Body::KnockButtons kr;
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
        body->setSnapshot(snapshotOf(s, journal_));
        seen_ = s.revision();
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
        // 1.47.0 : le titre garde sa place (un nom tres long y est raccourci ; il est entier dans le texte).
        manager().ShowDialog(std::make_unique<MessageDialog>("Retirer " + sessionui::elide(who, gfx::FontId{16}, 260.f) + " de la session ?",
                                                             who + " est coup\xC3\xA9 tout de suite (\xC2\xAB " + host + " t'a retir\xC3\xA9 de la session \xC2\xBB) "
                                                                   "et revient \xC3\xA0 son accueil ; la copie du projet s'efface de son PC. Les autres le lisent dans la discussion.\n\n"
                                                                   "Il ne revient que si tu le fais entrer de nouveau (m\xC3\xAAme avec le code).",
                                                             MessageDialog::Icon::Question, "Le retirer"),
                             [this, member](const menu::DialogResult& r) {
                                 if (r.accepted()) app_.session().remove(member);
                             });
    }

    App& app_;
    sessionwin::Body* body_{nullptr};
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
using sessionwin::FlowBody;   // 1.47.0 : app/SessionWindow (les essais la posent a toutes les tailles)

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
            // 1.47.0 : raccourci avec << ... >> s'il ne tient pas (l'infobulle dit le texte entier).
            const std::string shown = sessionui::elide(text_, ctx.theme.font.uiBold, b.w);
            setTooltip(shown == text_ ? std::string{} : text_);
            ctx.r.drawText({b.x, b.y + (b.h - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, shown, ctx.theme.font.uiBold, ctx.theme.color.text);
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
