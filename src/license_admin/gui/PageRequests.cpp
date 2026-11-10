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
#include "AdminsKit.hpp"   // 1.47.0 : les dates (<< hier 17:20 >>)
#include "ListKit.hpp"     // 1.47.0 : la barre de filtres, le clic droit, le depliage
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
menu::MenuPtr makeOptionDialog(ShellHost host, std::string licenseId, OptionPreset preset = {});   // 1.42.0
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
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
        if (!t.empty()) t.pop_back();
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
        addOption_ = add(*this, std::make_unique<ui::Button>("Ajouter cette option\xE2\x80\xA6", "demandes.ajouter-option"));   // 1.42.0
        addOption_->setStyle(ui::Button::Style::Primary);
        addOption_->setIcon(ui::Icon::Star);
        addOption_->setTooltip("Le client demande une option pour sa licence : la fen\xC3\xAAtre \xC2\xAB Ajouter une option \xC2\xBB r\xC3\xA9gl\xC3\xA9" "e sur sa demande ; "
                               "sign\xC3\xA9" "e, la nouvelle \xC3\xA9mission part sur le service et son PC voit \xC2\xAB Accept\xC3\xA9" "e \xC2\xBB.");
        refuse_ = add(*this, std::make_unique<ui::Button>("Refuser\xE2\x80\xA6", "demandes.refuser"));
        refuse_->setStyle(ui::Button::Style::Danger);
        refuse_->setTooltip("Le client voit \xC2\xAB Refus\xC3\xA9" "e \xC2\xBB et ta raison dans son logiciel (scell\xC3\xA9" "e : personne d'autre ne la lit).");
        resend_ = add(*this, std::make_unique<ui::Button>("Renvoyer la r\xC3\xA9ponse", "demandes.renvoyer"));
        resend_->setStyle(ui::Button::Style::Primary);
        resend_->setIcon(ui::Icon::Refresh);
        resend_->setTooltip("Cette demande est d\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici, mais sa r\xC3\xA9ponse n'est pas partie : la renvoyer au PC du client (la m\xC3\xAA" "me licence, "
                            "le m\xC3\xAA" "me code ; et le mail).");
        settings_ = add(*this, std::make_unique<ui::Button>("R\xC3\xA9gler le service", "demandes.regler"));
        settings_->setIcon(ui::Icon::Settings);
        settings_->setTooltip("La page Service en ligne : l'adresse de ton service des licences.");
        table_ = add(*this, std::make_unique<ui::TableView>("demandes.table"));
        // 1.37.0 : l'edition voulue et l'etat juste apres le client (vus sans faire defiler).
        table_->setColumns({{"Date", 140.f}, {"Client", 200.f}, {"\xC3\x89" "dition voulue", 250.f}, {"\xC3\x89tat", 170.f}, {"PC", 170.f},
                            {"Logiciel", 190.f}, {"Licence demand\xC3\xA9" "e", 190.f}, {"Message", 380.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(
            std::vector<std::string>{"Date", "Client", "\xC3\x89" "dition voulue", "\xC3\x89tat", "PC", "Logiciel", "Licence demand\xC3\xA9" "e", "Message"});
        table_->setModel(model_);
        // 1.47.0 (planche 12) : LA BARRE - chercher (demandes.recherche), l'etat (d'office : en attente de ta decision),
        // le client, l'offre, le logiciel, repondue par, la periode.
        bar_ = add(*this, std::make_unique<ui::FilterBar>("demandes", barFields()));
        bar_->setNoun("demandes");
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return all_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t {
                return f == 0 && r < all_.size() ? listkit::seconds(all_[r].createdAt ? all_[r].createdAt : all_[r].requestedAt) : 0;
            };
            src.extra = [this](std::size_t r) { return r < all_.size() ? all_[r].fingerprint + " " + all_[r].email + " n\xC2\xB0 " + std::to_string(all_[r].number) : std::string(); };
            bar_->setSource(std::move(src));
        }
        bar_->linkTable(*table_);
        bar_->setMemoryKey("demandes");
        if (!bar_->state().active()) bar_->setValues(kStateField, {online::requestStateLabel(RequestState::Pending)});   // d'office (comme avant)
        links_ += bar_->changed->connect([this] { rebuild(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });   // 1.47.0 (planche 06)
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); }, 8);    // 1.47.0 (planche 07) : reperee par son numero

        links_ += fetch_->clicked->connect([this] { fetch(); });
        links_ += accept_->clicked->connect([this] { accept(false); });
        links_ += acceptOnline_->clicked->connect([this] { accept(true); });
        links_ += addOption_->clicked->connect([this] { addOption(); });
        links_ += refuse_->clicked->connect([this] { askRefuse(); });
        links_ += resend_->clicked->connect([this] { resend(); });
        links_ += settings_->clicked->connect([this] {
            if (host_.navigate) host_.navigate("service");
        });
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
        reloadAll();
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
        for (ui::Button* btn : {fetch_, resend_, addOption_, accept_, acceptOnline_, refuse_, settings_}) {
            if (!btn->visible()) continue;
            const float bw = btn->sizeHint().preferred.w;
            btn->setBounds({bx, y, bw, kButtonH});
            bx += bw + kGap;
        }
        y += kButtonH + kGap;
        const float bh = bar_->heightFor(w);   // 1.47.0 : la barre de filtres
        bar_->setBounds({x, y, w, bh});
        y += bh + kGap;
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
        if (!ws().can("demandes")) t += " \xC2\xB7 " + ws().whyNot("demandes");   // 1.44.0 : le droit << demandes >>
        return t;
    }
    [[nodiscard]] ui::Tone statusTone() const {
        const auto& g = ws().service();
        if (!g.problem.empty()) return ui::Tone::Error;
        if (!g.configured || !ws().can("demandes")) return ui::Tone::Warning;
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
        if (ws().role() == Role::None) return "Il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur : les demandes sont scell\xC3\xA9" "es pour elle, et elle signe chaque appel au service.";
        if (!ws().can("demandes")) return ws().whyNot("demandes");   // 1.44.0 : un admin sans le droit << demandes >>
        if (g.fetch.fetchedAt == 0) return "Pas encore relev\xC3\xA9" "es sur ce poste : clique \xC2\xAB Relever les demandes \xC2\xBB.";
        if (!g.requests.empty() && wantWaiting())
            return "Aucune demande n'attend ta d\xC3\xA9" "cision. \xC2\xAB Toutes \xC2\xBB montre celles d\xC3\xA9j\xC3\xA0 trait\xC3\xA9" "es.";
        return "Aucune demande en ligne pour l'instant. Les clients la font depuis leur logiciel (Aide \xE2\x80\xBA Licence\xE2\x80\xA6) ; rel\xC3\xA8ve-les de "
               "temps en temps.";
    }

    // La demande choisie demande l'AJOUT de son PC a une licence existante : le dire en entier (a verifier).
    [[nodiscard]] std::string notice() const {
        const Entry* e = selected();
        if (e && e->acceptedHere())   // 1.37.0 : une seule acceptation
            return "D\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici (licence " + e->answer + "), mais sa r\xC3\xA9ponse n'est pas partie (le service \xC3\xA9tait injoignable ?) : "
                   "\xC2\xAB Renvoyer la r\xC3\xA9ponse \xC2\xBB. Elle ne s'accepte pas une seconde fois.";
        if (!e || !e->waiting() || !e->joinRequest()) return {};
        const LicenceEntry* l = ws().licence(e->licenseId);
        if (!l) return {};   // une licence qui n'existe pas encore : une licence neuve, rien a quoi s'ajouter
        std::string t = online::joinWarning(*e);
        if (lowerAscii(l->licence.customer) != lowerAscii(e->customer))
            t += " La licence " + e->licenseId + " est au nom de \xC2\xAB " + l->licence.customer + " \xC2\xBB.";
        return t;
    }

    // 1.47.0 : la puce << Etat >> ne garde que << En attente >> (d'office).
    [[nodiscard]] bool wantWaiting() const {
        for (const auto& p : bar_->state().picks)
            if (p.field == kStateField) return p.values.size() == 1 && p.values[0] == online::requestStateLabel(RequestState::Pending);
        return false;
    }

    // 1.37.0 : l'edition que le client a choisie dans sa boutique, et son prix (le registre de ce poste).
    [[nodiscard]] std::string wantedOf(const Entry& e) const {
        if (e.optionRequest()) return "Option : " + optionText(e);   // 1.42.0
        if (e.edition.empty()) return e.state == RequestState::Unreadable ? std::string() : std::string("\xE2\x80\x94");
        const auto* ed = ws().hasRegistry() ? ws().registry().edition(e.edition) : nullptr;
        if (!ed) return e.edition + " (inconnue)";
        std::string t = ed->label.empty() ? ed->id : ed->label;
        if (!ed->price.empty()) t += " \xC2\xB7 " + ed->price;
        return t;
    }
    // 1.42.0 : << Exports avances \xC2\xB7 1 mois, des qu'elle est acceptee >>.
    [[nodiscard]] std::string optionText(const Entry& e) const {
        const auto* o = ws().hasRegistry() ? ws().registry().option(e.option) : nullptr;
        std::string t = o ? (o->label.empty() ? o->id : o->label) : e.option + " (inconnue)";
        std::string d;
        if (e.optionDays < 0) d = "du " + dateFr(e.optionFrom ? e.optionFrom : e.requestedAt) + " au " + dateFr(e.optionLastDay);
        else if (e.optionDays == 0) d = "jusqu'\xC3\xA0 la fin de la licence";
        else if (e.optionDays == 7) d = "1 semaine";
        else if (e.optionDays == 30) d = "1 mois";
        else d = std::to_string(e.optionDays) + " jours";
        if (o)
            for (const auto& x : o->durations)
                if (x.days == e.optionDays && !x.price.empty()) d += " \xC2\xB7 " + x.price;
        t += " \xC2\xB7 " + d;
        if (e.optionDays >= 0) t += e.optionFrom ? ", \xC3\xA0 partir du " + dateFr(e.optionFrom) : std::string(", d\xC3\xA8s qu'elle est accept\xC3\xA9" "e");
        return t;
    }

    // Le mot du client, son courriel (1.37.0) et son contact.
    [[nodiscard]] static std::string messageOf(const Entry& e) {
        std::string t = oneLine(e.message);
        for (const std::string* part : {&e.email, &e.contact})
            if (!part->empty() && (part != &e.contact || *part != e.email)) t += (t.empty() ? "" : " \xC2\xB7 ") + *part;
        return t;
    }

    // 1.47.0 : toutes les demandes et leurs cases (la barre les filtre) ; qui a repondu (le journal des admins).
    void reloadAll() {
        all_ = ws().service().requests;
        texts_.clear();
        who_.reset();
        for (const auto& e : all_) {
            auto cells = cellsOf(e);
            cells.push_back(std::to_string(e.number));                // 8 : le numero (la cle du depliage)
            cells.push_back(online::requestStateLabel(e.state));       // 9 : l'etat seul (la puce)
            std::string by;
            if (e.state == RequestState::Accepted || e.state == RequestState::Refused || e.state == RequestState::Handled)
                if (const auto d = who_.last({"demande_accepter", "demande_refuser"}, "demande " + std::to_string(e.number))) by = d->who;
            cells.push_back(by);                                       // 10 : repondue par
            texts_.push_back(std::move(cells));
        }
        bar_->refreshCounts();
    }

    static constexpr std::size_t kStateField = 9;

    // 1.47.0 (planche 12) : les colonnes vues par la barre (les 8 du modele, puis le numero, l'etat seul, repondue par).
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(11);
        f[0] = {.title = "Date", .kind = K::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"};
        f[1] = {.title = "Client", .chip = true, .noun = "un client"};
        f[2] = {.title = "\xC3\x89" "dition voulue", .chipLabel = "Offre", .noun = "une offre"};
        f[3] = {.title = "\xC3\x89tat (d\xC3\xA9tail)", .kind = K::Text};
        f[4] = {.title = "PC", .kind = K::Text};
        f[5] = {.title = "Logiciel", .chip = true, .noun = "un logiciel"};
        f[6] = {.title = "Licence demand\xC3\xA9" "e", .kind = K::Text};
        f[7] = {.title = "Message", .kind = K::Text};
        f[8] = {.title = "N\xC2\xB0", .kind = K::Text};
        f[9] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat"};
        for (RequestState st : {RequestState::Pending, RequestState::Accepted, RequestState::Refused, RequestState::Handled, RequestState::Cancelled,
                                RequestState::Replaced, RequestState::Unreadable, RequestState::Ignored})
            f[9].order.push_back(online::requestStateLabel(st));
        f[10] = {.title = "R\xC3\xA9pondue par", .chip = true, .noun = "un admin"};
        return f;
    }

    // Les cases d'une demande (la table).
    [[nodiscard]] std::vector<std::string> cellsOf(const Entry& e) const {
        std::string licence = e.licenseId.empty() ? std::string("nouvelle") : e.licenseId;
        const bool joins = !e.licenseId.empty() && ws().licence(e.licenseId) && !e.optionRequest();
        if (joins) licence += " (ajouter ce PC)";
        if (e.optionRequest()) licence += ws().licence(e.licenseId) ? " (une option)" : " (une option ; licence inconnue ici)";
        std::string state = e.acceptedHere() ? "Accept\xC3\xA9" "e ici \xE2\x80\x94 r\xC3\xA9ponse \xC3\xA0 renvoyer" : online::requestStateLabel(e.state);
        if (joins && e.waiting() && !e.acceptedHere()) {   // n'importe qui peut demander l'ajout a une licence : a verifier
            licence += " \xE2\x80\x94 \xC3\xA0 v\xC3\xA9rifier";
            state += " \xE2\x80\x94 \xC3\xA0 v\xC3\xA9rifier";
        }
        if (!e.problems.empty()) state += " \xE2\x80\x94 " + e.problems.front();
        const std::string pc = e.state == RequestState::Unreadable ? "poste " + e.device.substr(0, 12)
                               : e.deviceName.empty()               ? std::string("(sans nom)")
                                                                    : e.deviceName;
        return {dateTimeFr(e.createdAt ? e.createdAt : e.requestedAt), e.customer.empty() ? std::string("\xE2\x80\x94") : e.customer, wantedOf(e),
                state, pc, e.software, e.state == RequestState::Unreadable ? std::string() : licence, messageOf(e)};
    }

    void rebuild() {
        shown_.clear();
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> keys;
        std::vector<RequestState> states;
        std::vector<bool> here;
        for (std::size_t i = 0; i < all_.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            const Entry& e = all_[i];
            shown_.push_back(e);
            auto cells = texts_[i];
            cells.resize(9);   // la table montre 0..7 ; 8 : le numero (la cle du depliage)
            rows.push_back(std::move(cells));
            keys.push_back(padKey(e.createdAt ? e.createdAt : e.requestedAt));
            states.push_back(e.state);
            here.push_back(e.acceptedHere());
        }
        model_->set(std::move(rows), std::move(keys), [states, here](RowIndex r, std::size_t c) {
            ui::CellStyle st;
            if (r >= states.size()) return st;
            if (c == 2) st.bold = states[r] == RequestState::Pending;   // l'edition voulue
            if (c != 3) return st;
            st.fgTone = here[r] ? ui::Tone::Warning : toneOf(states[r]);
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
        // 1.37.0 : une seule acceptation - acceptee ici (sa reponse pas partie) : on la renvoie, on ne la decide plus.
        const bool here = e && e->acceptedHere();
        resend_->setVisibility(on && here ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        fetch_->setEnabled(!b && ws().can("demandes"));   // 1.44.0 : le droit, plus seulement la cle de l'editeur
        resend_->setEnabled(!b && here);
        // 1.42.0 : une demande d'option - << Ajouter cette option... >> a la place d'Accepter
        const bool opt = e && e->optionRequest();
        addOption_->setVisibility(on && opt && !here ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        addOption_->setEnabled(!b && opt && e->waiting() && !here && ws().licence(e->licenseId) && ws().hasRegistry() && ws().registry().option(e->option));
        accept_->setEnabled(!b && e && e->waiting() && !here && !opt);
        acceptOnline_->setEnabled(!b && e && e->waiting() && !here && !opt);
        refuse_->setEnabled(!b && e && e->waiting() && !here);
        // 1.44.0 : un admin sans le droit << demandes >> : tout est grise, chaque bouton dit pourquoi.
        if (!ws().can("demandes"))
            for (ui::Button* x : {fetch_, accept_, acceptOnline_, refuse_, addOption_, resend_}) {
                x->setEnabled(false);
                x->setTooltip(ws().whyNot("demandes"));
            }
        invalidateLayout();
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    ui::ListMenu menuFor() {
        const auto rows = table_->selectedModelRows();
        ui::ListMenu m({rows.size(), "demandes", true});
        if (rows.empty()) return m;
        m.unfold(*table_);
        const Entry* e = rows.front() < shown_.size() ? &shown_[rows.front()] : nullptr;
        if (rows.size() == 1 && e) {
            const bool here = e->acceptedHere();
            const std::string notWaiting = here ? std::string("d\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici : renvoie la r\xC3\xA9ponse")
                                                : "elle n'attend pas de d\xC3\xA9" "cision (" + online::requestStateLabel(e->state) + ")";
            const std::string noRight = ws().can("demandes") ? std::string() : ws().whyNot("demandes");
            const auto reason = [&](ui::Button* b) -> std::string {
                if (!noRight.empty()) return noRight;
                if (b->enabled()) return {};
                return busy() ? std::string(kBusy) : notWaiting;
            };
            if (e->optionRequest()) m.action({.label = "Ajouter cette option\xE2\x80\xA6", .icon = ui::Icon::Star, .why = reason(addOption_), .run = [this] { addOption(); }});
            else {
                m.action({.label = "Accepter\xE2\x80\xA6", .icon = ui::Icon::Ok, .why = reason(accept_), .run = [this] { accept(false); }});
                m.action({.label = "Accepter en ligne\xE2\x80\xA6", .icon = ui::Icon::Globe, .why = reason(acceptOnline_), .run = [this] { accept(true); }});
            }
            m.action({.label = "Refuser\xE2\x80\xA6", .icon = ui::Icon::Close, .why = reason(refuse_), .run = [this] { askRefuse(); }});
            m.action({.label = "Renvoyer la r\xC3\xA9ponse", .icon = ui::Icon::Refresh,
                      .why = !noRight.empty() ? noRight : here ? std::string() : std::string("pas de r\xC3\xA9ponse \xC3\xA0 renvoyer"), .run = [this] { resend(); }});
            const std::string lic = e->state == RequestState::Accepted && !e->answer.empty() ? e->answer : e->licenseId;
            if (!lic.empty() && ws().licence(lic))
                m.goTo({.label = "La licence " + lic, .icon = ui::Icon::Document, .run = [this, lic] {
                            requestPageFocus("licences", lic);
                            host_.navigate("licences");
                        }});
            if (!e->customer.empty()) {
                const std::string c = e->customer;
                m.goTo({.label = "Le client : " + c, .icon = ui::Icon::User, .run = [this, c] {
                            requestPageFocus("clients", "client:" + c);
                            host_.navigate("clients");
                        }});
            }
            if (const auto d = who_.last({"demande_accepter", "demande_refuser"}, "demande " + std::to_string(e->number))) {
                const std::string key = d->key;
                m.goTo({.label = "L'admin : " + d->who, .icon = ui::Icon::User, .run = [this, key] {
                            requestPageFocus("admins", "admin:" + key);
                            host_.navigate("admins");
                        }});
            }
            const std::size_t i = indexOf(*e);
            const auto on = [&](std::size_t f, const std::string& label) {
                if (i >= texts_.size() || texts_[i][f].empty() || texts_[i][f] == "\xE2\x80\x94") return;
                const std::string v = texts_[i][f];
                m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
            };
            on(1, "Ce client");
            on(2, "Cette offre");
            on(5, "Ce logiciel");
            on(kStateField, "Cet \xC3\xA9tat");
            on(10, "R\xC3\xA9pondue par");
            if (const Seconds t = e->createdAt ? e->createdAt : e->requestedAt; t > 0) {
                const std::int64_t when = listkit::seconds(t);
                m.filterOn({.label = "Ce jour : " + dateFr(t), .run = [this, when] { bar_->filterOnDay(0, when); }});
            }
            m.copy("Le num\xC3\xA9ro", std::to_string(e->number), ui::Icon::Copy);
            if (!e->fingerprint.empty()) m.copy("L'empreinte du PC", e->fingerprint, ui::Icon::Lock);
            if (!e->message.empty()) m.copy("Le message", e->message, ui::Icon::Comment);
        }
        std::string lines;
        for (const RowIndex r : rows) {
            std::string line;
            for (std::size_t c = 0; c < 8; ++c) line += (c ? "\t" : "") + model_->cellText(r, c);
            lines += line + "\n";
        }
        m.copy("La ligne", lines, ui::Icon::Copy, "Copier les %n lignes", lines);
        m.exportRows([this, rows] { listkit::exportRows(host_, *table_, rows, "demandes.csv", "demandes"); });
        return m;
    }

    [[nodiscard]] std::size_t indexOf(const Entry& e) const {
        for (std::size_t i = 0; i < all_.size(); ++i)
            if (all_[i].number == e.number && all_[i].device == e.device) return i;
        return all_.size();
    }

    // ---- 1.47.0 (planche 07) : la ligne depliee - la demande, la reponse, qui l'a donnee ----
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= shown_.size()) return nullptr;
        const Entry e = shown_[r];
        auto card = std::make_unique<ui::UnfoldCard>("demandes.depliage");
        const Seconds now = ws().now();
        ui::UnfoldCard::Box ask;
        ask.title = "LA DEMANDE";
        if (!e.fingerprint.empty()) ask.lines.push_back({.icon = ui::Icon::Lock, .text = e.fingerprint, .mono = true});
        std::string pc = e.deviceName.empty() ? std::string("(sans nom)") : e.deviceName;
        if (!e.software.empty()) pc += " \xC2\xB7 " + e.software;
        ask.lines.push_back({.icon = ui::Icon::Station, .text = pc});
        if (!e.message.empty()) ask.lines.push_back({.icon = ui::Icon::Comment, .text = "\xC2\xAB " + oneLine(e.message) + " \xC2\xBB"});
        ask.lines.push_back({.icon = ui::Icon::History, .text = "re\xC3\xA7ue " + kit::whenFr(now, e.createdAt ? e.createdAt : e.requestedAt)});
        card->addBox(std::move(ask));
        ui::UnfoldCard::Box ans;
        ans.title = "LA R\xC3\x89PONSE";
        if (e.state == RequestState::Accepted) {
            ans.lines.push_back({.text = "**accept\xC3\xA9" "e** " + kit::whenFr(now, e.answeredAt), .tone = ui::Tone::Ok, .check = true});
        } else if (e.state == RequestState::Refused) {
            ans.lines.push_back({.icon = ui::Icon::Close, .text = "**refus\xC3\xA9" "e** " + kit::whenFr(now, e.answeredAt), .tone = ui::Tone::Error});
            if (!e.answer.empty()) ans.lines.push_back({.icon = ui::Icon::Comment, .text = "\xC2\xAB " + oneLine(e.answer) + " \xC2\xBB"});
        } else {
            ans.lines.push_back({.icon = ui::Icon::Info, .text = "**" + online::requestStateLabel(e.state) + "**", .tone = toneOf(e.state)});
        }
        if (e.answeredAt > 0 && (e.createdAt || e.requestedAt)) {
            const Seconds d = e.answeredAt - (e.createdAt ? e.createdAt : e.requestedAt);
            if (d >= 0) ans.lines.push_back({.icon = ui::Icon::History, .text = "d\xC3\xA9lai : **" + (d >= 3600 ? std::to_string(d / 3600) + " h " + std::to_string(d % 3600 / 60) : std::to_string(d / 60) + " min") + "**"});
        }
        const std::string lic = e.state == RequestState::Accepted && !e.answer.empty() ? e.answer : e.licenseId;
        if (!lic.empty()) {
            ui::UnfoldCard::Line l{.icon = ui::Icon::Document, .text = "licence **" + lic + "**"};
            if (ws().licence(lic))
                l.link = [this, lic] {
                    requestPageFocus("licences", lic);
                    host_.navigate("licences");
                };
            ans.lines.push_back(std::move(l));
        }
        if (e.optionRequest()) ans.lines.push_back({.icon = ui::Icon::Star, .text = "option : " + optionText(e)});
        card->addBox(std::move(ans));
        if (const auto d = who_.last({"demande_accepter", "demande_refuser"}, "demande " + std::to_string(e.number))) {
            card->addText("r\xC3\xA9pondue par");
            (void)card->addPerson(listkit::person(host_, d->key, d->who), "demandes.depliage.admin");
        }
        if (e.acceptedHere())
            (void)card->addButton("Renvoyer la r\xC3\xA9ponse", "demandes.depliage.renvoyer", ui::Icon::Refresh, [this] { resend(); },
                                  ws().can("demandes") ? std::string() : ws().whyNot("demandes"), true);
        else if (e.waiting() && !e.optionRequest())
            (void)card->addButton("Accepter\xE2\x80\xA6", "demandes.depliage.accepter", ui::Icon::Ok, [this] { accept(false); },
                                  ws().can("demandes") ? std::string() : ws().whyNot("demandes"), true);
        return card;
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

    // 1.42.0 : la fenetre << Ajouter une option >> reglee sur la demande du client.
    void addOption() {
        if (refuseIfBusy()) return;
        const Entry* e = selected();
        if (!e || !e->optionRequest() || !e->waiting()) return;
        const LicenceEntry* l = ws().licence(e->licenseId);
        if (!l) {
            host_.report(OpResult::fail("la licence " + e->licenseId + " n'est pas dans l'archive de ce poste : r\xC3\xA9" "cup\xC3\xA8re-la d'abord"));
            return;
        }
        if (!Workspace::inForce(*l)) {
            host_.report(OpResult::fail("la licence " + e->licenseId + " n'est plus en cours (finie ou r\xC3\xA9voqu\xC3\xA9" "e) : une option ne s'y ajoute pas ; refuse la demande en le disant"));
            return;
        }
        OptionPreset p;
        p.set = true;
        p.optionId = e->option;
        p.days = e->optionDays;
        p.from = e->optionFrom;
        p.lastDay = e->optionLastDay;
        p.requestNumber = e->number;
        host_.openDialog(licdlg::makeOptionDialog(host_, e->licenseId, p));
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
        if (e->acceptedHere()) {   // 1.37.0 : une seule acceptation
            host_.report(OpResult::fail("cette demande est d\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici (licence " + e->answer +
                                        ") : sa r\xC3\xA9ponse n'est pas partie, renvoie-la (\xC2\xAB Renvoyer la r\xC3\xA9ponse \xC2\xBB)"));
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

    // 1.37.0 : renvoyer la reponse d'une demande acceptee ici (la meme licence, le meme code) - lent : runSlow.
    void resend() {
        if (refuseIfBusy()) return;
        const Entry* e = selected();
        if (!e || !e->acceptedHere()) return;
        const int n = e->number;
        const Workspace* w = &ws();
        std::weak_ptr<bool> alive = life_;
        const bool started = host_.runSlow && host_.runSlow("Service : r\xC3\xA9ponse \xC3\xA0 la demande n\xC2\xB0 " + std::to_string(n),
                                                            [w, n] { return w->serviceResendAnswer(n); }, [this, alive](const OpResult&) {
                                                                if (alive.expired()) return;
                                                                ws().reload();
                                                                refresh();
                                                            });
        if (!started && host_.flash) host_.flash(kBusy, Tone::Warning);
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
    ui::Button*              addOption_{nullptr};   // 1.42.0
    ui::Button*              refuse_{nullptr};
    ui::Button*              resend_{nullptr};
    ui::Button*              settings_{nullptr};
    ui::FilterBar*           bar_{nullptr};                          // 1.47.0
    std::vector<Entry>       all_;                                   // 1.47.0 : toutes (la barre filtre)
    std::vector<std::vector<std::string>> texts_;                    // ... leurs cases (0..7 la table, 8 n\xC2\xB0, 9 l'etat, 10 repondue par)
    listkit::WhoDid          who_{host_.workspace()};                // 1.47.0 : qui a repondu
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
