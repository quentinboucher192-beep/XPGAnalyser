// =============================================================================
//  license_admin/gui/PageRequests.cpp - 1.34.0 : LA PAGE << Demandes en ligne >>
//  (1.35.0 : par le service des licences ; plus aucun GitHub)
// -----------------------------------------------------------------------------
//  Les demandes MANUELLES que les clients envoient depuis leur logiciel (au
//  service : POST /v1/demandes, scellees pour la cle de l'editeur) : relevees
//  par << Relever les demandes >> (lent : runSlow), rangees dans le dossier de
//  la cle (demandes-en-ligne/) - aucun fichier a importer. La table : date,
//  client, PC, logiciel, licence demandee, etat, message. Pour une demande en
//  attente :
//    - Accepter (licence liee)... ouvre l'assistant de licence avec elle (une
//      licence existante demandee : en ajout de PC) ; signee, la licence part
//      d'elle-meme a TOUS ses PC (publishToService : l'etat << poste >> de
//      chacun, la licence scellee pour lui) et la demande est marquee traitee.
//      Une demande d'AJOUT a une licence existante n'est pas une preuve
//      (n'importe qui peut taper un identifiant et un nom de client) : la table
//      la marque << a verifier >>, la page le dit en entier quand elle est
//      choisie, et l'assistant demande une confirmation avant de signer ;
//    - Accepter en ligne... : l'assistant d'une licence EN LIGNE (aucun PC, un
//      code d'activation montre une fois, a donner au client) ; le PC voit
//      << acceptee >> (sans le code) ;
//    - Refuser... demande la raison (le client la lit ; scellee pour son PC).
//  Le service pas regle, jamais releve, rien a montrer : la page dit quoi
//  faire. Rien n'est simule : hors ligne, c'est dit.
//
//  Ici aussi : publishToService, refreshOnlineLicences, depositOnlineLicence
//  (Page.hpp).
// =============================================================================
#include "Page.hpp"

#include "../../deploy/Crypto.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Definis dans LicenceWizard.cpp.
menu::MenuPtr makeReasonDialog(std::string id, std::string title, std::string text, std::string placeholder, std::string okLabel,
                               std::function<void(const std::string&)> onOk);
std::string fingerprintOfKey(const std::string& hex);
} // namespace licdlg

// ============================================================================================ l'exterieur
bool publishToService(const ShellHost& host, std::function<void(const OpResult&)> done) {
    if (!host.workspace || !host.runSlow) return false;
    Workspace& w = host.workspace();
    if (!w.hasServiceJob()) return false;
    if (host.busy && host.busy()) return false;   // il attend dans le Workspace : la coquille le lance ensuite
    std::optional<ServiceJob> job = w.takeServiceJob();
    if (!job) return false;
    const Workspace* wp = &w;
    const ServiceJob copy = *job;
    const bool started = host.runSlow(copy.label, [wp, copy] { return wp->serviceRun(copy); }, std::move(done));
    if (!started) w.queueServiceJob(std::move(*job));
    return started;
}

bool refreshOnlineLicences(const ShellHost& host, std::function<void(const OpResult&)> done) {
    if (!host.workspace || !host.runSlow) return false;
    const Workspace* wp = &host.workspace();
    auto snap = std::make_shared<online::Snapshot>();
    ShellHost h = host;
    return host.runSlow("Service : relev\xC3\xA9 des licences en ligne", [wp, snap] { return wp->serviceListLicences(*snap); },
                        [h, snap, done](const OpResult& r) {
                            if (r.ok) h.workspace().adoptSnapshot(std::move(*snap));
                            if (done) done(r);
                        });
}

bool depositOnlineLicence(const ShellHost& host, ServiceJob job, std::string code, std::function<void(const OpResult&)> done) {
    if (!host.workspace || !host.runSlow) {
        deploy::crypto::wipe(code.data(), code.size());
        return false;
    }
    const Workspace* wp = &host.workspace();
    // Le code reste en memoire (ici seulement) jusqu'a la reponse du service : montre si le depot a reussi, oublie sinon.
    auto secret = std::make_shared<std::string>(std::move(code));
    const std::string id = job.licenseId;
    ShellHost h = host;
    // 1.37.0 : le code garde chiffre sur ce poste, et scelle pour le PC d'une demande (serviceRunOnline).
    const bool started = host.runSlow(job.label, [wp, job, secret] { return wp->serviceRunOnline(job, *secret); }, [h, secret, id, done](const OpResult& r) {
        if (r.ok && !secret->empty() && h.openDialog) h.openDialog(makeCodeDialog(id, *secret));
        deploy::crypto::wipe(secret->data(), secret->size());
        secret->clear();
        if (done) done(r);
    });
    if (!started) {
        deploy::crypto::wipe(secret->data(), secret->size());
        secret->clear();
    }
    return started;
}

namespace {

using RowIndex = ui::RowIndex;
using online::Entry;
using online::RequestState;

const ui::Fonts kFonts{};
constexpr float kPad = 16.f;
constexpr float kButtonH = 32.f;
constexpr float kGap = 8.f;
constexpr float kLineH = 20.f;
constexpr const char* kBusy = "Un travail est en cours : attends qu'il finisse.";

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(RowIndex, std::size_t)>;
    explicit Rows(std::vector<std::string> headers) : headers_(std::move(headers)) {}
    void set(std::vector<std::vector<std::string>> rows, std::vector<std::string> sortKeys, Style style) {
        rows_ = std::move(rows);
        keys_ = std::move(sortKeys);
        style_ = std::move(style);
        modelReset->emit();
    }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        if (c == 0 && a < keys_.size() && b < keys_.size()) return keys_[a] < keys_[b];
        return cellText(a, c) < cellText(b, c);
    }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::string>              keys_;
    Style                                 style_;
};

template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float w, std::size_t maxLines) {
    std::vector<std::string> words;
    for (std::size_t i = 0; i <= text.size();) {
        std::size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        if (j > i) words.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    std::vector<std::string> lines;
    std::string line;
    for (const auto& word : words) {
        const std::string t = line.empty() ? word : line + " " + word;
        if (line.empty() || ui::measureWidth(t, f) <= w) {
            line = t;
            continue;
        }
        lines.push_back(line);
        line = word;
        if (lines.size() == maxLines) {
            lines.back() += "\xE2\x80\xA6";
            return lines;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string padKey(long long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%012lld", v);
    return b;
}

ui::Tone toneOf(RequestState s) {
    switch (s) {
        case RequestState::Pending: return ui::Tone::Warning;
        case RequestState::Accepted: return ui::Tone::Ok;
        case RequestState::Refused:
        case RequestState::Unreadable:
        case RequestState::Ignored: return ui::Tone::Error;
        case RequestState::Cancelled:
        case RequestState::Replaced:
        case RequestState::Handled: return ui::Tone::Muted;
    }
    return ui::Tone::None;
}

std::string oneLine(std::string s) {
    for (auto& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}

// =================================================================================== la page
class RequestsPage final : public Page {
public:
    explicit RequestsPage(ShellHost host) : Page("page.demandes", std::move(host)) {
        fetch_ = add(*this, std::make_unique<ui::Button>("Relever les demandes", "demandes.relever"));
        fetch_->setStyle(ui::Button::Style::Primary);
        fetch_->setIcon(ui::Icon::Refresh);
        fetch_->setTooltip("Lit les demandes envoy\xC3\xA9" "es par les logiciels des clients au service des licences, les ouvre avec ta cl\xC3\xA9 et les range sur ce poste.");
        accept_ = add(*this, std::make_unique<ui::Button>("Accepter\xE2\x80\xA6", "demandes.accepter"));
        accept_->setIcon(ui::Icon::Ok);
        accept_->setTooltip("L'assistant de licence, avec cette demande : une licence LI\xC3\x89" "E \xC3\xA0 ce PC ; sign\xC3\xA9" "e, elle part d'elle-m\xC3\xAAme \xC3\xA0 chacun de ses PC.");
        acceptOnline_ = add(*this, std::make_unique<ui::Button>("Accepter en ligne\xE2\x80\xA6", "demandes.accepter-en-ligne"));
        acceptOnline_->setTooltip("Une licence EN LIGNE pour ce client (aucun PC li\xC3\xA9) : son code part scell\xC3\xA9 \xC3\xA0 son PC (il s'active tout seul) et par mail ; gard\xC3\xA9 chiffr\xC3\xA9 ici.");
        refuse_ = add(*this, std::make_unique<ui::Button>("Refuser\xE2\x80\xA6", "demandes.refuser"));
        refuse_->setStyle(ui::Button::Style::Danger);
        refuse_->setTooltip("Le client voit \xC2\xAB Refus\xC3\xA9" "e \xC2\xBB et ta raison dans son logiciel (scell\xC3\xA9" "e : personne d'autre ne la lit).");
        settings_ = add(*this, std::make_unique<ui::Button>("R\xC3\xA9gler le service", "demandes.regler"));
        settings_->setIcon(ui::Icon::Settings);
        settings_->setTooltip("La page Service en ligne : l'adresse de ton service des licences.");
        filter_ = add(*this, std::make_unique<ui::DropDown>("demandes.filtre"));
        filter_->setItems({{"En attente de ta d\xC3\xA9" "cision", "attente"}, {"Toutes", ""}});
        filter_->setSelectedIndex(0);
        table_ = add(*this, std::make_unique<ui::TableView>("demandes.table"));
        table_->setColumns({{"Date", 150.f}, {"Client", 190.f}, {"PC", 170.f}, {"Logiciel", 190.f}, {"Licence demand\xC3\xA9" "e", 150.f}, {"\xC3\x89tat", 150.f},
                            {"Message", 320.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Date", "Client", "PC", "Logiciel", "Licence demand\xC3\xA9" "e", "\xC3\x89tat", "Message"});
        table_->setModel(model_);

        links_ += fetch_->clicked->connect([this] { fetch(); });
        links_ += accept_->clicked->connect([this] { accept(false); });
        links_ += acceptOnline_->clicked->connect([this] { accept(true); });
        links_ += refuse_->clicked->connect([this] { askRefuse(); });
        links_ += settings_->clicked->connect([this] {
            if (host_.navigate) host_.navigate("service");
        });
        links_ += filter_->selectionChanged->connect([this](int) { rebuild(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) { syncButtons(); });
        links_ += table_->activated->connect([this](RowIndex) { accept(false); });
        refresh();
    }
    ~RequestsPage() override {
        links_.clear();
        *life_ = false;
    }

    [[nodiscard]] std::string title() const override { return "Demandes en ligne"; }

    void refresh() override {
        lastBusy_ = busy();
        if (!lastBusy_) fetching_ = false;
        rebuild();
        invalidateLayout();
        invalidate();
    }

    // Les essais.
    [[nodiscard]] const std::vector<Entry>& shown() const noexcept { return shown_; }
    [[nodiscard]] std::string statusText() const { return status(); }
    [[nodiscard]] std::string noticeText() const { return notice(); }
    [[nodiscard]] std::string emptyText() const { return empty(); }

protected:
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        float y = b.y + kPad;
        titleY_ = y;
        y += 34.f;
        leadLines_ = wrap(kLead, kFonts.ui, w, 3);
        leadY_ = y;
        y += static_cast<float>(leadLines_.size()) * kLineH + 10.f;
        float bx = x;
        for (ui::Button* btn : {fetch_, accept_, acceptOnline_, refuse_, settings_}) {
            if (!btn->visible()) continue;
            const float bw = btn->sizeHint().preferred.w;
            btn->setBounds({bx, y, bw, kButtonH});
            bx += bw + kGap;
        }
        const float fw = 230.f;
        filter_->setBounds({std::max(bx, x + w - fw), y + 2.f, fw, 28.f});
        y += kButtonH + kGap;
        statusLines_ = wrap(status(), kFonts.ui, w, 2);
        statusY_ = y;
        y += static_cast<float>(statusLines_.size()) * kLineH + 6.f;
        const std::string n = notice();
        noticeLines_ = n.empty() ? std::vector<std::string>{} : wrap(n, kFonts.ui, w, 3);
        noticeY_ = y;
        if (!noticeLines_.empty()) y += static_cast<float>(noticeLines_.size()) * kLineH + 6.f;
        table_->setBounds({x, y, w, std::max(60.f, b.bottom() - kPad - y)});
        emptyY_ = y + 40.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        if (const bool bz = busy(); bz != lastBusy_) {
            lastBusy_ = bz;
            if (!bz) fetching_ = false;
            syncButtons();
        }
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, titleY_}, "Demandes en ligne", ctx.theme.font.title, c.text);
        for (std::size_t i = 0; i < leadLines_.size(); ++i)
            r.drawText({x, leadY_ + static_cast<float>(i) * kLineH}, fit(r, leadLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        const ui::Tone tone = statusTone();
        for (std::size_t i = 0; i < statusLines_.size(); ++i)
            r.drawText({x, statusY_ + static_cast<float>(i) * kLineH}, fit(r, statusLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui,
                       tone == ui::Tone::None ? c.text : ctx.theme.tone(tone, c.text));
        for (std::size_t i = 0; i < noticeLines_.size(); ++i)
            r.drawText({x, noticeY_ + static_cast<float>(i) * kLineH}, fit(r, noticeLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui,
                       ctx.theme.tone(ui::Tone::Warning, c.text));
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        auto& r = ctx.r;
        const gfx::Rect t = table_->bounds();
        float y = emptyY_;
        for (const auto& l : wrap(empty(), ctx.theme.font.ui, t.w - 32.f, 6)) {
            r.drawText({t.x + 16.f, y}, l, ctx.theme.font.ui, ctx.theme.color.textMuted);
            y += kLineH;
        }
    }

private:
    static constexpr const char* kLead =
        "Les demandes de licence que les clients envoient depuis leur logiciel, par le service des licences : relev\xC3\xA9" "es toutes seules (\xC3\xA0 "
        "l'ouverture, puis toutes les 2 minutes), rien \xC3\xA0 importer. Accepte-en une : l'assistant signe la licence (li\xC3\xA9" "e \xC3\xA0 ce PC, ou en ligne avec un "
        "code), elle part d'elle-m\xC3\xAAme ; le Studio du client l'installe dans la minute et le lui confirme. Une demande par PC ; le client peut l'annuler.";

    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }
    [[nodiscard]] bool ready() const { return ws().service().ready(); }

    [[nodiscard]] std::string status() const {
        const auto& g = ws().service();
        if (fetching_ && busy()) return "Relev\xC3\xA9 des demandes en cours\xE2\x80\xA6 (le r\xC3\xA9seau peut prendre quelques secondes)";
        if (!g.problem.empty()) return "Le service des licences est mal r\xC3\xA9gl\xC3\xA9 : " + g.problem;
        if (!g.configured) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9.";
        std::string t = "Service " + g.config.url + " \xC2\xB7 ";
        if (g.fetch.fetchedAt == 0) t += "jamais relev\xC3\xA9" "es sur ce poste";
        else t += "relev\xC3\xA9" "es le " + dateTimeFr(g.fetch.fetchedAt) + " : " + std::to_string(g.pending) + " en attente de ta d\xC3\xA9" "cision";
        if (!ws().key().present) t += " \xC2\xB7 il manque la cl\xC3\xA9 de l'\xC3\xA9" "diteur";
        return t;
    }
    [[nodiscard]] ui::Tone statusTone() const {
        const auto& g = ws().service();
        if (!g.problem.empty()) return ui::Tone::Error;
        if (!g.configured || !ws().key().present) return ui::Tone::Warning;
        if (g.pending > 0) return ui::Tone::Warning;
        return ui::Tone::None;
    }

    [[nodiscard]] std::string empty() const {
        const auto& g = ws().service();
        if (!g.problem.empty()) return "Corrige le r\xC3\xA9glage dans Service en ligne.";
        if (!g.configured)
            return "Le service des licences n'est pas encore r\xC3\xA9gl\xC3\xA9 : les clients ne peuvent pas demander une licence depuis leur logiciel. Mets-le en "
                   "ligne (Service en ligne : les \xC3\xA9tapes), puis donne son adresse. En attendant, les demandes arrivent par fichier (Activations \xE2\x80\xBA "
                   "Noter des demandes).";
        if (!ws().key().present) return "Il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur : les demandes sont scell\xC3\xA9" "es pour elle, et elle signe chaque appel au service.";
        if (g.fetch.fetchedAt == 0) return "Pas encore relev\xC3\xA9" "es sur ce poste : clique \xC2\xAB Relever les demandes \xC2\xBB.";
        if (!g.requests.empty() && wantWaiting())
            return "Aucune demande n'attend ta d\xC3\xA9" "cision. \xC2\xAB Toutes \xC2\xBB montre celles d\xC3\xA9j\xC3\xA0 trait\xC3\xA9" "es.";
        return "Aucune demande en ligne pour l'instant. Les clients la font depuis leur logiciel (Aide \xE2\x80\xBA Licence\xE2\x80\xA6) ; rel\xC3\xA8ve-les de "
               "temps en temps.";
    }

    // La demande choisie demande l'AJOUT de son PC a une licence existante : le dire en entier (a verifier).
    [[nodiscard]] std::string notice() const {
        const Entry* e = selected();
        if (!e || !e->waiting() || !e->joinRequest()) return {};
        const LicenceEntry* l = ws().licence(e->licenseId);
        if (!l) return {};   // une licence qui n'existe pas encore : une licence neuve, rien a quoi s'ajouter
        std::string t = online::joinWarning(*e);
        if (lowerAscii(l->licence.customer) != lowerAscii(e->customer))
            t += " La licence " + e->licenseId + " est au nom de \xC2\xAB " + l->licence.customer + " \xC2\xBB.";
        return t;
    }

    [[nodiscard]] bool wantWaiting() const {
        const auto* item = filter_->selectedItem();
        return item && item->value == "attente";
    }

    void rebuild() {
        const auto& g = ws().service();
        shown_.clear();
        for (const auto& e : g.requests)
            if (!wantWaiting() || e.waiting()) shown_.push_back(e);
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> keys;
        std::vector<RequestState> states;
        for (const auto& e : shown_) {
            std::string licence = e.licenseId.empty() ? std::string("nouvelle") : e.licenseId;
            const bool joins = !e.licenseId.empty() && ws().licence(e.licenseId);
            if (joins) licence += " (ajouter ce PC)";
            std::string state = online::requestStateLabel(e.state);
            if (joins && e.waiting()) {   // n'importe qui peut demander l'ajout a une licence : a verifier
                licence += " \xE2\x80\x94 \xC3\xA0 v\xC3\xA9rifier";
                state += " \xE2\x80\x94 \xC3\xA0 v\xC3\xA9rifier";
            }
            if (!e.problems.empty()) state += " \xE2\x80\x94 " + e.problems.front();
            const std::string pc = e.state == RequestState::Unreadable ? "poste " + e.device.substr(0, 12)
                                   : e.deviceName.empty()               ? std::string("(sans nom)")
                                                                        : e.deviceName;
            rows.push_back({dateTimeFr(e.createdAt ? e.createdAt : e.requestedAt), e.customer.empty() ? std::string("\xE2\x80\x94") : e.customer, pc,
                            e.software, e.state == RequestState::Unreadable ? std::string() : licence, state,
                            oneLine(e.message) + (e.contact.empty() ? std::string() : (e.message.empty() ? "" : " \xC2\xB7 ") + e.contact)});
            keys.push_back(padKey(e.createdAt ? e.createdAt : e.requestedAt));
            states.push_back(e.state);
        }
        model_->set(std::move(rows), std::move(keys), [states](RowIndex r, std::size_t c) {
            ui::CellStyle st;
            if (r >= states.size() || c != 5) return st;
            st.fgTone = toneOf(states[r]);
            st.bold = states[r] == RequestState::Pending;
            return st;
        });
        if (!shown_.empty() && table_->selection().empty()) table_->selectModelRows({0}, false);
        syncButtons();
        invalidateLayout();
        invalidate();
    }

    [[nodiscard]] const Entry* selected() const {
        const auto rows = table_->selectedModelRows();
        if (rows.empty() || rows.front() >= shown_.size()) return nullptr;
        return &shown_[rows.front()];
    }

    void syncButtons() {
        const bool b = busy();
        const Entry* e = selected();
        const bool on = ready();
        fetch_->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        accept_->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        acceptOnline_->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        refuse_->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        settings_->setVisibility(on ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        fetch_->setEnabled(!b && ws().key().present);
        accept_->setEnabled(!b && e && e->waiting());
        acceptOnline_->setEnabled(!b && e && e->waiting());
        refuse_->setEnabled(!b && e && e->waiting());
        invalidateLayout();
    }

    bool refuseIfBusy() {
        if (!busy()) return false;
        if (host_.report) host_.report(OpResult::fail("un travail est en cours (une publication, un relev\xC3\xA9) : attends qu'il finisse"));
        return true;
    }

    void fetch() {
        if (refuseIfBusy()) return;
        if (!ready()) {
            host_.report(OpResult::fail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)"));
            return;
        }
        const Workspace* w = &ws();
        std::weak_ptr<bool> alive = life_;
        fetching_ = true;
        const bool started = host_.runSlow && host_.runSlow(
                                                  "Service : relev\xC3\xA9 des demandes", [w] { return w->serviceFetch(); },
                                                  [this, alive](const OpResult& r) {
                                                      if (alive.expired()) return;
                                                      (void)r;
                                                      fetching_ = false;
                                                      ws().reload();   // relevees (meme en partie : la page le montre aussi)
                                                      refresh();
                                                  });
        if (!started) {
            fetching_ = false;
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
        }
        if (!alive.expired()) {
            lastBusy_ = busy();
            syncButtons();
            invalidateLayout();
        }
    }

    void accept(bool onlineLicence) {
        if (refuseIfBusy()) return;
        const Entry* e = selected();
        if (!e) {
            if (host_.flash) host_.flash("Choisis d'abord une demande dans la table.", Tone::Warning);
            return;
        }
        if (!e->waiting()) {
            host_.report(OpResult::fail("cette demande n'attend pas de d\xC3\xA9" "cision (" + online::requestStateLabel(e->state) + ")"));
            return;
        }
        std::error_code ec;
        if (e->requestPath.empty() || !std::filesystem::is_regular_file(std::filesystem::path(std::u8string(e->requestPath.begin(), e->requestPath.end())), ec)) {
            host_.report(OpResult::fail("le fichier de la demande manque sur ce poste : rel\xC3\xA8ve de nouveau les demandes"));
            return;
        }
        if (onlineLicence) {   // une licence EN LIGNE : aucun PC lie, la demande dit seulement a qui (et y repond)
            if (host_.openDialog) host_.openDialog(makeLicenceWizard(host_, {e->requestPath}, {}, e->customer, true));
            return;
        }
        std::string extend;
        if (!e->licenseId.empty())
            if (const LicenceEntry* l = ws().licence(e->licenseId)) {
                extend = e->licenseId;   // une licence partagee : ce PC s'y ajoute - a verifier (l'assistant demande de confirmer)
                if (host_.flash) {
                    std::string t = online::joinWarning(*e);
                    if (lowerAscii(l->licence.customer) != lowerAscii(e->customer))
                        t += " La licence " + extend + " est au nom de \xC2\xAB " + l->licence.customer + " \xC2\xBB.";
                    host_.flash(t, Tone::Warning);
                }
            }
        if (host_.openDialog) host_.openDialog(makeLicenceWizard(host_, {e->requestPath}, extend, extend.empty() ? e->customer : std::string()));
    }

    void askRefuse() {
        if (refuseIfBusy()) return;
        const Entry* e = selected();
        if (!e || !e->waiting()) {
            if (host_.flash) host_.flash("Choisis d'abord une demande en attente.", Tone::Warning);
            return;
        }
        const std::string ref = std::to_string(e->number);
        const std::string who = (e->customer.empty() ? std::string("ce client") : e->customer) + ", PC \xC2\xAB " + (e->deviceName.empty() ? "(sans nom)" : e->deviceName) + " \xC2\xBB";
        std::weak_ptr<bool> alive = life_;
        ShellHost host = host_;
        host_.openDialog(licdlg::makeReasonDialog(
            "demandes.refus", "Refuser la demande",
            "Refuser la demande de " + who + " ?\n\nLe client lit ta raison dans son logiciel ; elle est scell\xC3\xA9" "e pour son PC (personne d'autre ne la lit, "
            "pas m\xC3\xAAme le service).",
            "la raison, par exemple : pas de contrat pour ce poste", "Refuser",
            [this, alive, host, ref](const std::string& reason) {
                if (host.busy && host.busy()) {
                    if (host.report) host.report(OpResult::fail("un travail est en cours : attends qu'il finisse, puis refuse de nouveau"));
                    return;
                }
                const Workspace* w = &host.workspace();
                const bool started = host.runSlow && host.runSlow(
                                                         "Service : refus de la demande n\xC2\xB0 " + ref, [w, ref, reason] { return w->serviceRefuse(ref, reason); },
                                                         [this, alive](const OpResult&) {
                                                             if (alive.expired()) return;
                                                             ws().reload();
                                                             refresh();
                                                         });
                if (!started && host.flash) host.flash(kBusy, Tone::Warning);
            }));
    }

    std::shared_ptr<bool>    life_{std::make_shared<bool>(true)};
    core::ConnectionScope    links_;
    ui::Button*              fetch_{nullptr};
    ui::Button*              accept_{nullptr};
    ui::Button*              acceptOnline_{nullptr};
    ui::Button*              refuse_{nullptr};
    ui::Button*              settings_{nullptr};
    ui::DropDown*            filter_{nullptr};
    ui::TableView*           table_{nullptr};
    std::shared_ptr<Rows>    model_;
    std::vector<Entry>       shown_;
    std::vector<std::string> leadLines_, statusLines_, noticeLines_;
    bool                     fetching_{false};
    bool                     lastBusy_{false};
    float                    titleY_{0}, leadY_{0}, statusY_{0}, noticeY_{0}, emptyY_{0};
};

} // namespace

PagePtr makeRequestsPage(ShellHost host) { return std::make_unique<RequestsPage>(std::move(host)); }

} // namespace license_admin::gui
