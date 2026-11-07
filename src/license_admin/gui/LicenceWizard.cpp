// =============================================================================
//  license_admin/gui/LicenceWizard.cpp - 1.33.0 : L'ASSISTANT << NOUVELLE LICENCE >>
//  (et les petits dialogues des pages Licences et Revocations : revoquer, confirmer)
// -----------------------------------------------------------------------------
//  Le coeur de l'application de l'editeur : signer la licence d'un client sans
//  rien configurer a la main. Un dialogue, six etapes a gauche :
//    1. Demandes des PC        les .xpgreq du client (Ajouter..., ou glisses sur la
//                              fenetre) ; chacune lue : nom du PC, empreinte,
//                              logiciel, date, licence demandee ; une demande
//                              refusee en rouge avec sa raison ; pour une licence
//                              etendue, les PC deja lies ;
//    2. Client et identifiant  le nom (les clients connus proposes), l'identifiant
//                              (le suivant de l'archive, ou celui que demandent
//                              les PC ; celui de la licence etendue, fige) ;
//    3. Edition                les editions du registre et leurs valeurs, puis le
//                              type, le nombre de PC, la duree (ou la date de
//                              fin), la grace hors ligne ;
//    4. Fonctionnalites        celles de l'edition, cochees ; a la main sinon ;
//    5. Recapitulatif          Workspace::preview : ce qui sera signe, ou le refus ;
//    6. Signer                 ou enregistrer le .xpglic, Workspace::signLicence ;
//                              puis ouvrir le dossier, publier sur GitHub (lent).
//  Sans cle ou sans registre, l'assistant ne montre pas les etapes : il dit ou
//  aller (Parametres de securite, Editions).
//
//  Tout l'etat est dans WizardBody::st_ ; chaque etape le montre (enter) et
//  l'ecrit a chaque changement ; Suivant est grise tant que l'etape dit pourquoi
//  (blocker), et la phrase est ecrite au-dessus des boutons.
//
//  LES DIALOGUES sont des WidgetMenu poses par ShellHost::openDialog (le
//  MenuManager de la coquille les attache : OnEnter). Sans gestionnaire (les
//  essais), ils se dessinent seuls et se ferment en se cachant.
// =============================================================================
#include "Page.hpp"

#include "../../deploy/Crypto.hpp"
#include "../../licensing/KeyFile.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Partages avec PageLicences.cpp et PageRevocations.cpp (declares la-bas, definis ici).
menu::MenuPtr makeRevokeDialog(ShellHost host, std::string licenseId);
menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger,
                                std::function<void()> onOk);
bool wizardTakeDrop(const std::vector<std::string>& paths);
std::string fingerprintOfKey(const std::string& hex);
std::string startFor(const std::string& fileName);
void rememberFolder(const std::string& filePath);
} // namespace licdlg

namespace {

namespace fs = std::filesystem;
using licensing::LicenseType;

constexpr gfx::FontId kBand{17};
constexpr gfx::FontId kHeading{20};
constexpr gfx::FontId kSmall{13};
constexpr Seconds kDaySeconds = 86400;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string fileName(const std::string& path) { return U(P(path).filename()); }
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string extOf(const std::string& path) { return lower(U(P(path).extension())); }
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}
std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// Un entier tape (des chiffres seulement, 7 au plus) ; -1 : illisible.
int parseCount(const std::string& text) {
    const std::string s = trim(text);
    if (s.empty() || s.size() > 7) return -1;
    for (char c : s)
        if (c < '0' || c > '9') return -1;
    return std::stoi(s);
}

// Le numero du jour (depuis le 01/01/1970, calendrier civil) : compter des jours entre deux dates.
long daysFromCivil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// "JJ/MM/AAAA" -> le numero du jour ; faux : ce n'est pas une date.
bool parseDateFr(const std::string& text, long& day) {
    int d = 0, m = 0, y = 0;
    char tail = 0;
    if (std::sscanf(trim(text).c_str(), "%d/%d/%d%c", &d, &m, &y, &tail) != 3) return false;
    if (y < 2000 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
    static const int lengths[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (d > lengths[m - 1]) return false;
    if (m == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return false;
    day = daysFromCivil(y, m, d);
    return true;
}

std::string durationLabel(int days) {
    if (days <= 0) return "sans fin";
    if (days % 365 == 0) return std::to_string(days) + " j (" + (days == 365 ? std::string("1 an") : std::to_string(days / 365) + " ans") + ")";
    return std::to_string(days) + " j";
}

std::string typeLabel(LicenseType t) {
    switch (t) {
        case LicenseType::Perpetual: return "perp\xC3\xA9tuelle";
        case LicenseType::Temporary: return "temporaire";
        case LicenseType::Evaluation: return "\xC3\xA9valuation";
        case LicenseType::Floating: return "flottante";
    }
    return {};
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

// Couper un texte en lignes de `width` pixels au plus (aux espaces ; '\n' force une ligne).
std::vector<std::string> wrapText(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::size_t a = 0;
    for (;;) {
        std::size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        const std::string para = text.substr(a, b - a);
        std::string line;
        std::size_t i = 0;
        while (i <= para.size()) {
            std::size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            const std::string word = para.substr(i, j - i);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(tried, f) > width) {
                out.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            i = j + 1;
        }
        out.push_back(line);
        if (b >= text.size()) break;
        a = b + 1;
    }
    return out;
}

float lineStep(gfx::FontId f) { return std::floor(ui::lineHeight(f) * 1.25f); }

float wrappedHeight(const std::string& text, gfx::FontId f, float width, int maxLines = 0) {
    std::size_t n = wrapText(text, f, width).size();
    if (maxLines > 0) n = std::min<std::size_t>(n, static_cast<std::size_t>(maxLines));
    return static_cast<float>(n) * lineStep(f);
}

// Dessiner un texte coupe en lignes ; rend la hauteur prise.
float drawWrapped(gfx::IRenderer& r, float x, float y, float w, const std::string& text, gfx::FontId f, gfx::Color color, int maxLines = 0) {
    auto lines = wrapText(text, f, w);
    if (maxLines > 0 && lines.size() > static_cast<std::size_t>(maxLines)) {
        lines.resize(static_cast<std::size_t>(maxLines));
        lines.back() = fit(r, lines.back() + " \xE2\x80\xA6", f, w);
    }
    const float step = lineStep(f);
    for (std::size_t i = 0; i < lines.size(); ++i) r.drawText({x, y + step * static_cast<float>(i)}, fit(r, lines[i], f, w), f, color);
    return step * static_cast<float>(lines.size());
}

gfx::Color toneColor(const ui::Theme& t, Tone tone) {
    switch (tone) {
        case Tone::Ok: return t.tone(ui::Tone::Ok, t.color.text);
        case Tone::Warning: return t.tone(ui::Tone::Warning, t.color.text);
        case Tone::Error: return t.tone(ui::Tone::Error, t.color.text);
        case Tone::Info: break;
    }
    return t.color.accent;
}

// Une bande de message : le liseret de son ton, le texte.
void drawBand(gfx::IRenderer& r, const ui::Theme& theme, const gfx::Rect& band, const std::string& text, Tone tone) {
    if (text.empty()) return;
    r.fillRect(band, theme.color.headerBg);
    r.fillRect({band.x, band.y, 3.f, band.h}, toneColor(theme, tone));
    r.drawText({band.x + 10.f, band.y + (band.h - r.lineHeight(theme.font.ui)) * 0.5f}, fit(r, text, theme.font.ui, band.w - 16.f), theme.font.ui,
               tone == Tone::Error ? toneColor(theme, Tone::Error) : theme.color.text);
}

menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

// Un tableau de textes (une ligne par entree), ses styles par case.
class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {}, std::vector<std::string> tips = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)), tips_(std::move(tips)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips_.size() ? tips_[r] : std::string(); }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    Style                                 style_;
    std::vector<std::string>              tips_;
};

// Des lignes de texte (un resultat, un recapitulatif), chacune son ton.
class Lines final : public ui::IListModel {
public:
    struct Line {
        std::string text;
        ui::Tone    tone{ui::Tone::None};
    };
    explicit Lines(std::vector<Line> lines) : lines_(std::move(lines)) {}
    [[nodiscard]] std::size_t rowCount() const override { return lines_.size(); }
    [[nodiscard]] std::string text(ui::RowIndex r) const override { return r < lines_.size() ? lines_[r].text : std::string(); }
    [[nodiscard]] ui::CellStyle style(ui::RowIndex r) const override {
        ui::CellStyle s;
        if (r < lines_.size()) s.fgTone = lines_[r].tone;
        return s;
    }

private:
    std::vector<Line> lines_;
};

std::vector<Lines::Line> linesOf(const OpResult& r) {
    std::vector<Lines::Line> out;
    if (!r.message.empty()) out.push_back({r.message, r.ok ? ui::Tone::Ok : ui::Tone::Error});
    for (const auto& d : r.details) {
        const bool warn = d.rfind("ATTENTION", 0) == 0 || d.rfind("!!!", 0) == 0;
        out.push_back({d, warn ? ui::Tone::Warning : ui::Tone::None});
    }
    return out;
}

// Le rapport de la synchro GitHub, rendu comme un resultat d'operation.
OpResult syncResult(const github::SyncReport& rep) {
    OpResult r;
    r.ok = rep.ok;
    if (rep.ok)
        r.message = rep.upToDate ? "GitHub : d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour." : "Publi\xC3\xA9 sur GitHub.";
    else
        r.message = "Refus\xC3\xA9 : " + rep.why;
    const std::string text = rep.text();
    for (std::size_t a = 0, b; a < text.size(); a = b + 1) {
        b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        const std::string line = text.substr(a, b - a);
        if (line.empty() || line == r.message || line.rfind("Refus\xC3\xA9 : ", 0) == 0) continue;
        r.details.push_back(line);
    }
    return r;
}

// ---------------------------------------------------------------------------------------- un dialogue modal
// Un WidgetMenu de dialogue : se dessine et se ferme aussi sans MenuManager (les essais).
class DialogMenu : public menu::WidgetMenu {
public:
    explicit DialogMenu(std::string id) : menu::WidgetMenu(std::move(id)) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }

    void Update(const menu::FrameContext& fc) override {
        if (hosted_) {
            menu::WidgetMenu::Update(fc);
            return;
        }
        if (ui::Widget* r = widgetRoot()) {
            r->setBounds({0.f, 0.f, fc.surface.w, fc.surface.h});
            const ui::dialogframe::KeyScope key(id());
            r->layout();
        }
    }
    void Render(gfx::IRenderer& r, const menu::FrameContext& fc) override {
        if (hosted_) {
            menu::WidgetMenu::Render(r, fc);
            return;
        }
        ui::Widget* root = widgetRoot();
        if (!root || !fc.theme) return;
        std::vector<ui::Widget*> overlays;
        const ui::PaintContext ctx{r, *fc.theme, {0.f, 0.f, fc.surface.w, fc.surface.h}, fc.totalSeconds, &overlays};
        root->render(ctx);
        for (auto* w : overlays) w->paintTopMost(ctx);
    }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        const ui::EventResult r = menu::WidgetMenu::HandleEvent(ev);
        if (r == ui::EventResult::Consumed) return r;
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            close();
            return ui::EventResult::Consumed;
        }
        return r;
    }
    void close() {
        if (closed_) return;
        closed_ = true;
        if (hosted_)
            manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        else if (ui::Widget* r = widgetRoot())
            r->setVisibility(ui::Visibility::Hidden);
    }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

protected:
    void onEnter() override { hosted_ = true; }

private:
    bool hosted_{false};
    bool closed_{false};
};

// Le cadre d'un dialogue : le panneau, la bande de titre et sa croix.
struct Frame {
    gfx::Rect panel{}, closeBox{};
    void place(ui::Widget& body, float wantW, float wantH) {
        const auto b = body.bounds();
        const float w = std::min(wantW, std::max(420.f, b.w - 40.f));
        const float h = std::min(wantH, std::max(320.f, b.h - 40.f));
        panel = ui::dialogframe::place(body, {std::floor(b.x + (b.w - w) * 0.5f), std::floor(b.y + (b.h - h) * 0.5f), w, h});
        closeBox = {panel.right() - 36.f, panel.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(body, closeBox);
    }
    void paint(gfx::IRenderer& r, const ui::Theme& theme, const std::string& title) const {
        const auto& c = theme.color;
        r.fillRect(panel, c.panelBg);
        r.strokeRect(panel, c.borderStrong, 1.f);
        r.fillRect({panel.x, panel.y, panel.w, 42.f}, c.headerBg);
        r.drawText({panel.x + 16.f, panel.y + (42.f - r.lineHeight(kBand)) * 0.5f}, fit(r, title, kBand, panel.w - 70.f), kBand, c.text);
        r.drawText({closeBox.x + 7.f, closeBox.y + 2.f}, "\xC3\x97", kBand, c.textMuted);
        r.fillRect({panel.x, panel.y + 42.f, panel.w, 1.f}, c.border);
    }
    [[nodiscard]] bool hitClose(const ui::InputEvent& ev) const {
        const auto* d = std::get_if<ui::MouseDown>(&ev);
        return d && d->button == ui::MouseButton::Left && closeBox.contains(d->pos);
    }
};

template <class T>
T& adopt(ui::Widget& parent, std::unique_ptr<T> w) {
    return static_cast<T&>(parent.addChild(std::move(w)));
}

ui::Button& button(ui::Widget& parent, std::string text, std::string id, ui::Button::Style style = ui::Button::Style::Default) {
    auto& b = adopt(parent, std::make_unique<ui::Button>(std::move(text), std::move(id)));
    b.setStyle(style);
    return b;
}

float buttonWidth(ui::Button& b, float minimum = 96.f) { return std::max(minimum, b.sizeHint().preferred.w); }

void show(ui::Widget* w, bool on) {
    if (w) w->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
}

std::string folderOf(const std::string& path) { return U(P(path).parent_path()); }

std::string g_lastFolder;   // le dernier dossier ou un fichier a ete enregistre (la seance)

// ======================================================================================= L'ASSISTANT
struct Req {
    RequestPreview p;
    bool           bound{false};   // deja lie a la licence etendue (une reactivation)
    [[nodiscard]] bool ok() const { return p.ok; }
};

struct WizardState {
    bool                  extend{false};
    std::string           extendId;
    licensing::License    base;          // la licence etendue, telle qu'a l'ouverture
    std::vector<Req>      requests;
    std::string           customer, licenseId;
    bool                  idTouched{false};
    std::string           edition;
    LicenseType           type{LicenseType::Temporary};
    int                   maxDevices{1};     // -1 : illisible
    int                   days{365};         // -1 : illisible
    int                   grace{14};         // -1 : illisible
    bool                  endInvalid{false};
    bool                  raisedDevices{false};
    std::set<std::string> features;
    OpResult              preview;
    OpResult              signResult;
    bool                  signAttempted{false};
    bool                  signedOk{false};
    bool                  publishing{false};
    bool                  published{false};
    OpResult              publish;
};

class WizardMenu;
class WizardBody;
WizardBody* g_openWizard = nullptr;   // l'assistant ouvert (les fichiers glisses sur la page Licences)

class Step : public ui::Widget {
public:
    Step(std::string id, WizardBody& w) : ui::Widget(std::move(id)), w_(w) {}
    [[nodiscard]] virtual std::string heading() const = 0;
    [[nodiscard]] virtual std::string intro() const = 0;
    [[nodiscard]] virtual std::string railLabel() const { return heading(); }
    virtual void enter() {}
    [[nodiscard]] virtual std::string blocker() const { return {}; }

protected:
    WizardBody&           w_;
    core::ConnectionScope links_;
    bool                  syncing_{false};
};

class WizardBody final : public ui::Widget {
public:
    WizardBody(WizardMenu& owner, ShellHost host, std::vector<std::string> requests, std::string extendId, std::string customer);
    ~WizardBody() override;

    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    [[nodiscard]] const ShellHost& host() const { return host_; }
    [[nodiscard]] WizardState& st() { return st_; }
    [[nodiscard]] const WizardState& st() const { return st_; }
    [[nodiscard]] Seconds now() const { return ws().now(); }

    void changed();
    void say(std::string text, Tone tone);
    void addRequests(const std::vector<std::string>& paths, bool quiet = false);
    void removeRequests(std::vector<std::size_t> indexes);
    void pickRequest();
    void chooseEdition(const std::string& id);
    void switchToExtension(const std::string& licenseId);
    [[nodiscard]] int boundDevices() const { return st_.extend ? static_cast<int>(st_.base.devices.size()) : 0; }
    [[nodiscard]] int newDevices() const;
    [[nodiscard]] int neededDevices() const { return boundDevices() + newDevices(); }
    [[nodiscard]] std::set<std::string> editionFeatures(const std::string& id) const;
    [[nodiscard]] std::string editionLabel(const std::string& id) const;
    [[nodiscard]] bool customFeatures() const { return st_.features != editionFeatures(st_.edition); }
    [[nodiscard]] LicenceDraft draft() const;
    [[nodiscard]] bool takeDrop(const std::vector<std::string>& paths);
    [[nodiscard]] bool blocked() const noexcept { return !blockedWhy_.empty(); }
    [[nodiscard]] std::string windowTitle() const;
    [[nodiscard]] std::string requestedExisting() const;   // la licence de l'archive que demandent les PC (pas etendue)

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    void loadBase(const LicenceEntry& l);
    void adoptRequestedId();
    void goTo(std::size_t index);
    void next();
    void prev();
    void sign();
    void publish();
    void updateButtons();
    [[nodiscard]] bool canReach(std::size_t index) const;
    [[nodiscard]] std::size_t railIndexAt(gfx::Point p) const;

    WizardMenu&         owner_;
    ShellHost           host_;
    WizardState         st_;
    std::vector<Step*>  steps_;
    std::size_t         current_{0}, furthest_{0};
    std::string         blockedWhy_, blockedPage_, blockedLabel_;
    std::string         message_;
    Tone                messageTone_{Tone::Info};
    ui::Button*         cancel_{nullptr};
    ui::Button*         prev_{nullptr};
    ui::Button*         next_{nullptr};
    ui::Button*         sign_{nullptr};
    ui::Button*         folder_{nullptr};
    ui::Button*         github_{nullptr};
    ui::Button*         close_{nullptr};
    ui::Button*         goto_{nullptr};
    Frame               frame_;
    gfx::Rect           rail_{}, heading_{}, band_{}, area_{};
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    core::ConnectionScope links_;
};

class WizardMenu final : public DialogMenu {
public:
    WizardMenu(ShellHost host, std::vector<std::string> requests, std::string extendId, std::string customer)
        : DialogMenu("dialog.licence-assistant"), host_(std::move(host)), requests_(std::move(requests)), extendId_(std::move(extendId)),
          customer_(std::move(customer)) {}
    [[nodiscard]] std::string title() const override { return extendId_.empty() ? "Nouvelle licence" : "Ajouter des PC \xC3\xA0 " + extendId_; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<WizardBody>(*this, host_, std::move(requests_), extendId_, customer_));
        return core::ok();
    }

private:
    ShellHost                host_;
    std::vector<std::string> requests_;
    std::string              extendId_, customer_;
};

// ------------------------------------------------------------------------------ etape 1 : les demandes
class StepRequests final : public Step {
public:
    explicit StepRequests(WizardBody& w) : Step("wizard.etape.demandes", w) {
        add_ = &button(*this, "Ajouter\xE2\x80\xA6", "wizard.demandes.ajouter");
        add_->setTooltip("Choisir une demande d'activation (.xpgreq) envoy\xC3\xA9" "e par le client");
        remove_ = &button(*this, "Retirer", "wizard.demandes.retirer");
        table_ = &adopt(*this, std::make_unique<ui::TableView>("wizard.demandes"));
        table_->setSelectionMode(ui::SelectionMode::Extended);
        setColumns(false);
        existing_ = &adopt(*this, std::make_unique<ui::TableView>("wizard.pc.existants"));
        existing_->setColumns({{"PC", 170.f}, {"Empreinte", 250.f}, {"Activ\xC3\xA9 le", 110.f}, {"\xC3\x89tat", 280.f}});
        existing_->setSelectionMode(ui::SelectionMode::None);
        links_ += add_->clicked->connect([this] { w_.pickRequest(); });
        links_ += remove_->clicked->connect([this] {
            std::vector<std::size_t> rows;
            for (auto r : table_->selectedModelRows()) rows.push_back(r);
            w_.removeRequests(std::move(rows));
        });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { remove_->setEnabled(!table_->selection().empty()); });
    }
    [[nodiscard]] std::string heading() const override { return "Demandes des PC"; }
    [[nodiscard]] std::string intro() const override {
        const auto& s = w_.st();
        if (s.extend)
            return "Les demandes (.xpgreq) des PC \xC3\xA0 ajouter \xC3\xA0 la licence " + s.extendId
                   + ". Les PC d\xC3\xA9j\xC3\xA0 li\xC3\xA9s restent ; sans nouvelle demande, la licence est r\xC3\xA9-\xC3\xA9mise avec les m\xC3\xAAmes PC (pour changer sa dur\xC3\xA9" "e ou ses fonctionnalit\xC3\xA9s).";
        return "Les fichiers .xpgreq que le client t'a envoy\xC3\xA9s, un par PC \xC3\xA0 activer : Ajouter\xE2\x80\xA6 pour les choisir, ou glisse-les sur la fen\xC3\xAAtre.";
    }
    void enter() override {
        const auto& s = w_.st();
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> tips;
        std::vector<int> kinds;   // 0 bonne, 1 deja liee, 2 refusee
        for (const auto& r : s.requests) {
            const auto& rec = r.p.record;
            if (!r.ok()) {
                rows.push_back({fileName(r.p.path), "refus\xC3\xA9" "e : " + r.p.why, "", "", "", ""});
                tips.push_back(r.p.path + "\nRefus\xC3\xA9" "e : " + r.p.why);
                kinds.push_back(2);
                continue;
            }
            rows.push_back({rec.deviceName.empty() ? std::string("(sans nom)") : rec.deviceName,
                            r.bound ? std::string("d\xC3\xA9j\xC3\xA0 sur la licence") : std::string("valide"), rec.fingerprint, rec.software,
                            dateFr(rec.requestedAt), rec.licenseId.empty() ? std::string("\xE2\x80\x94") : rec.licenseId});
            tips.push_back(r.p.path + (r.bound ? "\nCe PC est d\xC3\xA9j\xC3\xA0 li\xC3\xA9 \xC3\xA0 la licence : une r\xC3\xA9" "activation." : ""));
            kinds.push_back(r.bound ? 1 : 0);
        }
        model_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "\xC3\x89tat", "Empreinte", "Logiciel", "Demand\xC3\xA9" "e le", "Licence demand\xC3\xA9" "e"},
                                        std::move(rows),
                                        [kinds](ui::RowIndex r, std::size_t c) {
                                            ui::CellStyle st;
                                            if (r >= kinds.size()) return st;
                                            if (c == 2) st.monospace = true;
                                            if (kinds[r] == 2) {
                                                st.fgTone = ui::Tone::Error;
                                                if (c == 0) {
                                                    st.icon = ui::Icon::Error;
                                                    st.iconTone = ui::Tone::Error;
                                                }
                                            } else if (c == 1) {
                                                st.fgTone = kinds[r] == 1 ? ui::Tone::Warning : ui::Tone::Ok;
                                                st.icon = kinds[r] == 1 ? ui::Icon::Info : ui::Icon::Ok;
                                                st.iconTone = st.fgTone;
                                            }
                                            return st;
                                        },
                                        std::move(tips));
        bool named = false;
        for (const auto& r : s.requests) named = named || (r.ok() && !r.p.record.licenseId.empty());
        setColumns(named);
        table_->setModel(model_);
        remove_->setEnabled(false);

        show(existing_, s.extend);
        if (s.extend) {
            std::vector<std::vector<std::string>> ex;
            std::vector<bool> revoked;
            for (const auto& d : s.base.devices) {
                bool rev = false;
                for (const auto& x : w_.ws().revocations().revoked)
                    if (x.licenseId == s.base.licenseId && x.device == d.key && (x.issuedBefore == 0 || s.base.issuedAt < x.issuedBefore)) rev = true;
                ex.push_back({d.name.empty() ? std::string("(sans nom)") : d.name, licdlg::fingerprintOfKey(d.key), dateFr(d.activatedAt),
                              rev ? std::string("r\xC3\xA9voqu\xC3\xA9 (la r\xC3\xA9-\xC3\xA9mission sera refus\xC3\xA9" "e)") : std::string("li\xC3\xA9, gard\xC3\xA9")});
                revoked.push_back(rev);
            }
            existingModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Empreinte", "Activ\xC3\xA9 le", "\xC3\x89tat"}, std::move(ex),
                                                    [revoked](ui::RowIndex r, std::size_t c) {
                                                        ui::CellStyle st;
                                                        if (c == 1) st.monospace = true;
                                                        if (r < revoked.size() && revoked[r]) st.fgTone = ui::Tone::Error;
                                                        return st;
                                                    });
            existing_->setModel(existingModel_);
        }
        invalidateLayout();
    }
    [[nodiscard]] std::string blocker() const override {
        const auto& s = w_.st();
        int good = 0, bad = 0;
        for (const auto& r : s.requests) (r.ok() ? good : bad)++;
        if (bad > 0) {
            for (const auto& r : s.requests)
                if (!r.ok())
                    return "Retire les demandes refus\xC3\xA9" "es (en rouge) pour continuer \xE2\x80\x94 \xC2\xAB " + fileName(r.p.path) + " \xC2\xBB : " + r.p.why;
        }
        if (!s.extend && good == 0) return "Ajoute au moins une demande d'activation (.xpgreq) du client.";
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        add_->setBounds({r.x, r.y, buttonWidth(*add_, 110.f), 30.f});
        remove_->setBounds({add_->bounds().right() + 8.f, r.y, buttonWidth(*remove_), 30.f});
        const float top = r.y + 40.f;
        const bool ext = w_.st().extend;
        const float avail = std::max(60.f, r.bottom() - top);
        const float tableH = ext ? std::floor(avail * 0.55f) : avail;
        table_->setBounds({r.x, top, r.w, tableH});
        if (ext) {
            existingCaption_ = top + tableH + 8.f;
            existing_->setBounds({r.x, existingCaption_ + 24.f, r.w, std::max(40.f, r.bottom() - existingCaption_ - 24.f)});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const float x = remove_->bounds().right() + 16.f;
        const auto& s = w_.st();
        int good = 0, bad = 0;
        for (const auto& q : s.requests) (q.ok() ? good : bad)++;
        std::string line = "ou glisse les fichiers .xpgreq sur la fen\xC3\xAAtre";
        if (!s.requests.empty())
            line = plural(good, "demande valide", "demandes valides") + (bad ? ", " + plural(bad, "refus\xC3\xA9" "e", "refus\xC3\xA9" "es") : std::string()) + " \xE2\x80\x94 " + line;
        r.drawText({x, bounds().y + 7.f}, fit(r, line, ctx.theme.font.ui, bounds().right() - x), ctx.theme.font.ui, c.textMuted);
        if (s.extend)
            r.drawText({bounds().x, existingCaption_}, fit(r, "PC d\xC3\xA9j\xC3\xA0 sur la licence " + s.extendId + " (" + std::to_string(s.base.devices.size()) + ")",
                                                          ctx.theme.font.ui, bounds().w),
                       ctx.theme.font.ui, c.text);
    }
    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!w_.st().requests.empty()) return;
        const auto t = table_->bounds();
        const std::string empty = "Aucune demande pour l'instant. Ajouter\xE2\x80\xA6 (ou glisse les .xpgreq ici).";
        ctx.r.drawText({t.x + 16.f, t.y + 44.f}, fit(ctx.r, empty, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    void setColumns(bool withLicence) {
        std::vector<ui::TableView::Column> cols{{"PC", 150.f}, {"\xC3\x89tat", 120.f}, {"Empreinte", 245.f}, {"Logiciel", 225.f},
                                                {"Demand\xC3\xA9" "e le", 105.f}, {"Licence demand\xC3\xA9" "e", 150.f}};
        cols[5].visible = withLicence;
        table_->setColumns(std::move(cols));
    }

    ui::Button*                add_{nullptr};
    ui::Button*                remove_{nullptr};
    ui::TableView*             table_{nullptr};
    ui::TableView*             existing_{nullptr};
    std::shared_ptr<Rows>      model_, existingModel_;
    float                      existingCaption_{0.f};
};

// --------------------------------------------------------------------- etape 2 : le client, l'identifiant
class StepClient final : public Step {
public:
    explicit StepClient(WizardBody& w) : Step("wizard.etape.client", w) {
        customer_ = &adopt(*this, std::make_unique<ui::InputText>("wizard.client"));
        customer_->setPlaceholder("Le nom du client, tel qu'il figurera dans la licence");
        customer_->setMaxLength(200);
        customer_->setAssist([this](std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
            from = 0;
            const std::string typed = lower(trim(std::string(before)));
            if (typed.empty()) return;
            for (const auto& name : names_) {
                const std::string l = lower(name);
                if (l == typed || l.find(typed) == std::string::npos) continue;
                ui::InputText::Suggestion s;
                s.text = name;
                s.detail = "client connu";
                out.push_back(std::move(s));
                if (out.size() >= 8) break;
            }
        });
        known_ = &adopt(*this, std::make_unique<ui::DropDown>("wizard.client.connus"));
        id_ = &adopt(*this, std::make_unique<ui::InputText>("wizard.identifiant"));
        id_->setMonospace(true);
        id_->setMaxLength(64);
        propose_ = &button(*this, "Proposer le suivant", "wizard.identifiant.proposer");
        propose_->setTooltip("Le num\xC3\xA9ro suivant de l'archive pour cette ann\xC3\xA9" "e");
        extendTo_ = &button(*this, "", "wizard.etendre");
        links_ += customer_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            w_.st().customer = t;
            w_.changed();
        });
        links_ += known_->selectionChanged->connect([this](int i) {
            if (syncing_ || i < 0 || static_cast<std::size_t>(i) >= names_.size()) return;
            customer_->setText(names_[static_cast<std::size_t>(i)]);
        });
        links_ += id_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            w_.st().licenseId = t;
            w_.st().idTouched = true;
            w_.changed();
        });
        links_ += propose_->clicked->connect([this] {
            w_.st().licenseId = w_.ws().suggestLicenseId();
            w_.st().idTouched = true;
            syncing_ = true;
            id_->setText(w_.st().licenseId);
            syncing_ = false;
            w_.changed();
        });
        links_ += extendTo_->clicked->connect([this] {
            const std::string target = w_.requestedExisting();
            if (!target.empty()) w_.switchToExtension(target);
        });
    }
    [[nodiscard]] std::string heading() const override { return "Client et identifiant"; }
    [[nodiscard]] std::string railLabel() const override { return "Client, identifiant"; }
    [[nodiscard]] std::string intro() const override {
        if (w_.st().extend) return "La licence " + w_.st().extendId + " garde son identifiant ; le nom du client peut \xC3\xAAtre corrig\xC3\xA9.";
        return "Le nom du client tel qu'il figurera dans la licence, et l'identifiant de la licence (\xC3\xA0 lui donner pour ses prochaines demandes).";
    }
    void enter() override {
        const auto& s = w_.st();
        syncing_ = true;
        customer_->setText(s.customer);
        id_->setText(s.licenseId);
        id_->setReadOnly(s.extend);
        names_.clear();
        for (const auto& c : w_.ws().clients()) names_.push_back(c.name);
        std::vector<ui::DropDown::Item> items;
        int selected = -1;
        for (std::size_t i = 0; i < names_.size(); ++i) {
            items.push_back({names_[i], names_[i]});
            if (lower(names_[i]) == lower(trim(s.customer))) selected = static_cast<int>(i);
        }
        known_->setItems(std::move(items));
        known_->setSelectedIndex(selected);
        syncing_ = false;
        show(known_, !names_.empty());
        show(propose_, !s.extend);
        const std::string target = w_.requestedExisting();
        show(extendTo_, !target.empty());
        if (!target.empty()) extendTo_->setText("Ajouter ces PC \xC3\xA0 " + target);
        invalidateLayout();
    }
    [[nodiscard]] std::string blocker() const override {
        const auto& s = w_.st();
        const std::string id = trim(s.licenseId);
        if (trim(s.customer).empty()) return "Donne le nom du client.";
        if (!licensing::isLicenseId(id)) return "L'identifiant n'est pas valide : lettres, chiffres, point, tiret, soulign\xC3\xA9 (64 au plus).";
        if (!s.extend && w_.ws().licence(id))
            return "La licence " + id + " existe d\xC3\xA9j\xC3\xA0 dans l'archive : choisis un autre identifiant (ou ajoute ces PC \xC3\xA0 cette licence).";
        for (const auto& r : s.requests)
            if (r.ok() && !r.p.record.licenseId.empty() && r.p.record.licenseId != id)
                return "Le PC \xC2\xAB " + (r.p.record.deviceName.empty() ? fileName(r.p.path) : r.p.record.deviceName) + " \xC2\xBB demande la licence "
                       + r.p.record.licenseId + ", pas " + id + " : corrige l'identifiant ou retire sa demande.";
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float fieldW = std::min(420.f, r.w - (names_.empty() ? 0.f : 240.f));
        customerY_ = r.y;
        customer_->setBounds({r.x, r.y + 22.f, fieldW, 30.f});
        known_->setBounds({r.x + fieldW + 16.f, r.y + 22.f, std::max(120.f, std::min(240.f, r.w - fieldW - 16.f)), 30.f});
        idY_ = r.y + 96.f;
        id_->setBounds({r.x, idY_ + 22.f, std::min(280.f, r.w * 0.5f), 30.f});
        propose_->setBounds({id_->bounds().right() + 8.f, idY_ + 22.f, buttonWidth(*propose_), 30.f});
        extendY_ = idY_ + 100.f;
        extendTo_->setBounds({r.x, extendY_ + 26.f, buttonWidth(*extendTo_, 160.f), 30.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const auto& s = w_.st();
        r.drawText({b.x, customerY_}, "Client", ctx.theme.font.ui, c.text);
        if (!names_.empty()) r.drawText({known_->bounds().x, customerY_}, "ou un client connu", kSmall, c.textMuted);
        r.drawText({b.x, customerY_ + 58.f}, fit(r, "Il figure dans la licence et dans le registre des activations ; les clients connus sont propos\xC3\xA9s pendant la frappe.", kSmall, b.w),
                   kSmall, c.textMuted);
        r.drawText({b.x, idY_}, "Identifiant de la licence", ctx.theme.font.ui, c.text);
        r.drawText({b.x, idY_ + 58.f},
                   fit(r, s.extend ? std::string("C'est la licence \xC3\xA9tendue : son identifiant ne change pas.")
                                   : std::string("Le suivant de l'archive pour cette ann\xC3\xA9" "e ; tu peux en donner un autre (lettres, chiffres, . _ -)."),
                       kSmall, b.w),
                   kSmall, c.textMuted);
        const std::string target = w_.requestedExisting();
        if (!target.empty()) {
            const LicenceEntry* l = w_.ws().licence(target);
            const std::string who = l ? " (client " + l->licence.customer + ")" : std::string();
            r.drawText({b.x, extendY_}, fit(r, "Ces PC demandent la licence " + target + who + ", d\xC3\xA9j\xC3\xA0 dans l'archive :", ctx.theme.font.ui, b.w),
                       ctx.theme.font.ui, toneColor(ctx.theme, Tone::Warning));
        }
    }

private:
    ui::InputText*           customer_{nullptr};
    ui::DropDown*            known_{nullptr};
    ui::InputText*           id_{nullptr};
    ui::Button*              propose_{nullptr};
    ui::Button*              extendTo_{nullptr};
    std::vector<std::string> names_;
    float                    customerY_{0}, idY_{0}, extendY_{0};
};

// ---------------------------------------------------------------------- etape 3 : l'edition et ses valeurs
class StepEdition final : public Step {
public:
    explicit StepEdition(WizardBody& w) : Step("wizard.etape.edition", w) {
        editions_ = &adopt(*this, std::make_unique<ui::TableView>("wizard.editions"));
        editions_->setSelectionMode(ui::SelectionMode::Single);
        editions_->setColumns({{"\xC3\x89" "dition", 170.f}, {"Type", 110.f}, {"PC", 50.f}, {"Dur\xC3\xA9" "e", 110.f}, {"Gr\xC3\xA2" "ce", 70.f},
                               {"Fonctionnalit\xC3\xA9s", 110.f}});
        typeGroup_ = std::make_shared<ui::RadioGroup>();
        const char* typeIds[4] = {"wizard.type.perpetuelle", "wizard.type.temporaire", "wizard.type.evaluation", "wizard.type.flottante"};
        for (int i = 0; i < 4; ++i)
            types_[i] = &adopt(*this, std::make_unique<ui::RadioButton>(typeLabel(static_cast<LicenseType>(i)), typeGroup_, i, typeIds[i]));
        types_[3]->setEnabled(false);
        types_[3]->setTooltip("Une licence flottante demande le service de licences en ligne, qui n'existe pas encore.");
        minus_ = &button(*this, "\xE2\x88\x92", "wizard.pc.moins");
        pcs_ = &adopt(*this, std::make_unique<ui::InputText>("wizard.pc"));
        pcs_->setMaxLength(6);
        pcs_->setValidator([](std::string_view t) { return std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
        plus_ = &button(*this, "+", "wizard.pc.plus");
        const int presetDays[5] = {30, 90, 365, 730, 1095};
        const char* presetText[5] = {"30 j", "90 j", "1 an", "2 ans", "3 ans"};
        for (int i = 0; i < 5; ++i) {
            auto& b = button(*this, presetText[i], "wizard.duree." + std::to_string(presetDays[i]));
            const int d = presetDays[i];
            links_ += b.clicked->connect([this, d] { setDays(d); });
            presets_.push_back(&b);
        }
        forever_ = &button(*this, "Sans fin", "wizard.duree.sansfin");
        forever_->setTooltip("Une licence perp\xC3\xA9tuelle : pas de date de fin");
        days_ = &adopt(*this, std::make_unique<ui::InputText>("wizard.duree"));
        days_->setMaxLength(5);
        days_->setValidator([](std::string_view t) { return std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
        end_ = &adopt(*this, std::make_unique<ui::InputText>("wizard.fin"));
        end_->setPlaceholder("JJ/MM/AAAA");
        end_->setMaxLength(10);
        end_->setValidator([](std::string_view t) { return std::all_of(t.begin(), t.end(), [](char ch) { return (ch >= '0' && ch <= '9') || ch == '/'; }); });
        grace_ = &adopt(*this, std::make_unique<ui::InputText>("wizard.grace"));
        grace_->setMaxLength(4);
        grace_->setValidator([](std::string_view t) { return std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
        defaults_ = &button(*this, "Valeurs de l'\xC3\xA9" "dition", "wizard.edition.defauts");
        defaults_->setTooltip("Remettre le type, les PC, la dur\xC3\xA9" "e, la gr\xC3\xA2" "ce et les fonctionnalit\xC3\xA9s de l'\xC3\xA9" "dition choisie");

        links_ += editions_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            if (syncing_ || sel.empty() || sel.front() >= ids_.size()) return;
            w_.chooseEdition(ids_[sel.front()]);
            loadForm();
        });
        links_ += typeGroup_->valueChanged->connect([this](int v) {
            if (syncing_ || v < 0 || v > 2) return;
            w_.st().type = static_cast<LicenseType>(v);
            loadForm();
            w_.changed();
        });
        links_ += pcs_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            w_.st().maxDevices = parseCount(t);
            w_.changed();
        });
        links_ += minus_->clicked->connect([this] { setDevices(std::max(1, w_.st().maxDevices - 1)); });
        links_ += plus_->clicked->connect([this] { setDevices(std::max(1, w_.st().maxDevices + 1)); });
        links_ += forever_->clicked->connect([this] {
            w_.st().type = LicenseType::Perpetual;
            loadForm();
            w_.changed();
        });
        links_ += days_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            w_.st().days = parseCount(t);
            w_.st().endInvalid = false;
            syncEnd();
            w_.changed();
        });
        links_ += end_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            long day = 0;
            auto& s = w_.st();
            if (trim(t).empty()) {
                s.endInvalid = false;
            } else if (parseDateFr(t, day)) {
                long today = 0;
                (void)parseDateFr(dateFr(w_.now()), today);
                s.days = static_cast<int>(day - today);
                s.endInvalid = false;
                syncing_ = true;
                days_->setText(s.days > 0 ? std::to_string(s.days) : std::string());
                syncing_ = false;
            } else {
                s.endInvalid = true;
            }
            w_.changed();
        });
        links_ += grace_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            w_.st().grace = parseCount(t);
            w_.changed();
        });
        links_ += defaults_->clicked->connect([this] {
            if (w_.st().edition.empty()) return;
            w_.chooseEdition(w_.st().edition);
            loadForm();
            w_.say("Valeurs de l'\xC3\xA9" "dition " + w_.editionLabel(w_.st().edition) + " remises (fonctionnalit\xC3\xA9s comprises).", Tone::Info);
        });
    }
    [[nodiscard]] std::string heading() const override { return "\xC3\x89" "dition et dur\xC3\xA9" "e"; }
    [[nodiscard]] std::string railLabel() const override { return "\xC3\x89" "dition"; }
    [[nodiscard]] std::string intro() const override {
        return "L'\xC3\xA9" "dition donne les valeurs par d\xC3\xA9" "faut ; ajuste pour ce client le type, le nombre de PC, la dur\xC3\xA9" "e et la gr\xC3\xA2" "ce hors ligne.";
    }
    void enter() override {
        auto& s = w_.st();
        // Les PC : au moins ceux deja lies et les demandes.
        s.raisedDevices = false;
        if (s.maxDevices >= 0 && s.maxDevices < w_.neededDevices()) {
            s.maxDevices = w_.neededDevices();
            s.raisedDevices = true;
        }
        ids_.clear();
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> unlicensed;
        const auto& reg = w_.ws().registry();
        for (const auto& e : reg.editions) {
            if (e.retired && e.id != s.edition) continue;
            ids_.push_back(e.id);
            const std::string label = (e.label.empty() ? e.id : e.label) + (e.retired ? " (retir\xC3\xA9" "e)" : "");
            rows.push_back({label, typeLabel(e.type), std::to_string(e.maxDevices),
                            e.type == LicenseType::Perpetual ? std::string("sans fin") : durationLabel(e.durationDays),
                            e.type == LicenseType::Temporary ? std::to_string(e.offlineGraceDays) + " j" : std::string("\xE2\x80\x94"),
                            std::to_string(e.features.size())});
            unlicensed.push_back(e.id == reg.unlicensedEdition);
        }
        model_ = std::make_shared<Rows>(std::vector<std::string>{"\xC3\x89" "dition", "Type", "PC", "Dur\xC3\xA9" "e", "Gr\xC3\xA2" "ce", "Fonctionnalit\xC3\xA9s"},
                                        std::move(rows), [unlicensed](ui::RowIndex r, std::size_t c) {
                                            ui::CellStyle st;
                                            if (c == 0) st.bold = true;
                                            if (r < unlicensed.size() && unlicensed[r] && c == 0) {
                                                st.badge = "sans licence";
                                                st.badgeTone = ui::Tone::Muted;
                                            }
                                            return st;
                                        });
        syncing_ = true;
        editions_->setModel(model_);
        for (std::size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == s.edition) editions_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
        syncing_ = false;
        loadForm();
    }
    [[nodiscard]] std::string blocker() const override {
        const auto& s = w_.st();
        const auto* e = w_.ws().registry().edition(s.edition);
        if (s.edition.empty() || !e) return "Choisis une \xC3\xA9" "dition.";
        if (e->retired) return "L'\xC3\xA9" "dition " + s.edition + " est retir\xC3\xA9" "e du registre : choisis-en une autre.";
        if (s.maxDevices < 1) return "Donne le nombre de PC (un entier, 1 au moins).";
        const int need = w_.neededDevices();
        if (s.maxDevices < need) {
            if (w_.boundDevices() > 0)
                return "Il faut au moins " + std::to_string(need) + " PC : " + std::to_string(w_.boundDevices()) + " d\xC3\xA9j\xC3\xA0 li\xC3\xA9s et "
                       + plural(w_.newDevices(), "nouvelle demande.", "nouvelles demandes.");
            return "Il faut au moins " + std::to_string(need) + " PC (les demandes).";
        }
        if (s.maxDevices > 100000) return "100000 PC au plus.";
        if (s.type != LicenseType::Perpetual) {
            if (s.endInvalid) return "La date de fin s'\xC3\xA9" "crit JJ/MM/AAAA (07/11/2027).";
            if (s.days < 1) return "Donne une dur\xC3\xA9" "e en jours, ou une date de fin apr\xC3\xA8s aujourd'hui.";
            if (s.days > 36600) return "36600 jours au plus (100 ans).";
        }
        if (s.type == LicenseType::Temporary) {
            if (s.grace < 0) return "Donne la gr\xC3\xA2" "ce hors ligne en jours (0 : aucune).";
            if (s.grace > 3660) return "3660 jours de gr\xC3\xA2" "ce au plus.";
        }
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float tableH = std::min(28.f + 24.f * static_cast<float>(std::max<std::size_t>(ids_.size(), 3)) + 6.f, std::floor(r.h * 0.4f));
        editions_->setBounds({r.x, r.y, r.w, tableH});
        const float lx = r.x, cx = r.x + 150.f;
        float y = r.y + tableH + 14.f;
        const float rowH = 36.f;
        rowY_[0] = y;
        float x = cx;
        for (auto* t : types_) {
            const float w = ui::measureWidth(t->label(), gfx::FontId{16}) + 34.f;
            t->setBounds({x, y, w, 28.f});
            x += w + 10.f;
        }
        y += rowH;
        rowY_[1] = y;
        minus_->setBounds({cx, y, 30.f, 28.f});
        pcs_->setBounds({cx + 34.f, y, 70.f, 28.f});
        plus_->setBounds({cx + 108.f, y, 30.f, 28.f});
        y += rowH;
        rowY_[2] = y;
        x = cx;
        for (auto* b : presets_) {
            const float w = buttonWidth(*b, 56.f);
            b->setBounds({x, y, w, 28.f});
            x += w + 6.f;
        }
        forever_->setBounds({x + 6.f, y, buttonWidth(*forever_, 80.f), 28.f});
        y += rowH;
        rowY_[3] = y;
        days_->setBounds({cx, y, 70.f, 28.f});
        end_->setBounds({cx + 70.f + 120.f, y, 120.f, 28.f});
        y += rowH;
        rowY_[4] = y;
        grace_->setBounds({cx, y, 70.f, 28.f});
        y += rowH;
        defaults_->setBounds({lx, std::min(y + 4.f, r.bottom() - 30.f), buttonWidth(*defaults_, 140.f), 28.f});
        (void)lx;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const auto& s = w_.st();
        const gfx::FontId f = ctx.theme.font.ui;
        const float ty = (28.f - r.lineHeight(f)) * 0.5f;
        const auto label = [&](int row, const char* text) { r.drawText({b.x, rowY_[row] + ty}, text, f, c.text); };
        const auto note = [&](float x, int row, const std::string& text, Tone tone = Tone::Info) {
            r.drawText({x, rowY_[row] + ty}, fit(r, text, f, b.right() - x), f, tone == Tone::Info ? c.textMuted : toneColor(ctx.theme, tone));
        };
        label(0, "Type");
        label(1, "Nombre de PC");
        label(2, "Dur\xC3\xA9" "e");
        label(4, "Gr\xC3\xA2" "ce hors ligne");
        const int need = w_.neededDevices();
        std::string pcNote = "au moins " + std::to_string(need) + (w_.boundDevices() > 0 ? " : les PC li\xC3\xA9s et les demandes" : " : les demandes");
        if (s.raisedDevices) pcNote += " (relev\xC3\xA9 pour les accueillir)";
        note(plus_->bounds().right() + 14.f, 1, pcNote);
        if (s.type == LicenseType::Perpetual) {
            note(days_->bounds().x, 3, "sans fin : une licence perp\xC3\xA9tuelle n'a pas de date de fin.");
        } else {
            r.drawText({days_->bounds().right() + 8.f, rowY_[3] + ty}, "jours, fin le", f, c.textMuted);
            if (s.days > 0 && !s.endInvalid)
                note(end_->bounds().right() + 12.f, 3, "(" + durationLabel(s.days) + ")");
        }
        if (s.type == LicenseType::Temporary)
            note(grace_->bounds().right() + 12.f, 4, "jours : apr\xC3\xA8s la fin, elle reste ouverte le temps d'importer le renouvellement.");
        else
            note(grace_->bounds().right() + 12.f, 4, "sans objet pour une licence " + typeLabel(s.type) + ".");
        if (!types_[3]->enabled())
            r.drawText({types_[3]->bounds().right() + 6.f, rowY_[0] + ty}, fit(r, "(demande le service en ligne)", kSmall, b.right() - types_[3]->bounds().right() - 6.f),
                       kSmall, c.textMuted);
    }

private:
    void setDevices(int n) {
        w_.st().maxDevices = n;
        syncing_ = true;
        pcs_->setText(std::to_string(n));
        syncing_ = false;
        w_.changed();
    }
    void setDays(int d) {
        auto& s = w_.st();
        if (s.type == LicenseType::Perpetual) s.type = LicenseType::Temporary;
        s.days = d;
        s.endInvalid = false;
        loadForm();
        w_.changed();
    }
    void syncEnd() {
        const auto& s = w_.st();
        syncing_ = true;
        end_->setText(s.type != LicenseType::Perpetual && s.days > 0 ? dateFr(w_.now() + static_cast<Seconds>(s.days) * kDaySeconds) : std::string());
        syncing_ = false;
    }
    // L'etat -> les champs.
    void loadForm() {
        const auto& s = w_.st();
        syncing_ = true;
        typeGroup_->setValue(static_cast<int>(s.type));
        pcs_->setText(s.maxDevices >= 0 ? std::to_string(s.maxDevices) : pcs_->text());
        const bool timed = s.type != LicenseType::Perpetual;
        days_->setText(timed && s.days > 0 ? std::to_string(s.days) : std::string());
        grace_->setText(s.grace >= 0 ? std::to_string(s.grace) : grace_->text());
        syncing_ = false;
        if (!s.endInvalid) syncEnd();
        days_->setEnabled(timed);
        end_->setEnabled(timed);
        grace_->setEnabled(s.type == LicenseType::Temporary);
        defaults_->setEnabled(!s.edition.empty());
        invalidate();
    }

    ui::TableView*                   editions_{nullptr};
    std::shared_ptr<Rows>            model_;
    std::vector<std::string>         ids_;
    std::shared_ptr<ui::RadioGroup>  typeGroup_;
    ui::RadioButton*                 types_[4]{};
    ui::Button*                      minus_{nullptr};
    ui::InputText*                   pcs_{nullptr};
    ui::Button*                      plus_{nullptr};
    std::vector<ui::Button*>         presets_;
    ui::Button*                      forever_{nullptr};
    ui::InputText*                   days_{nullptr};
    ui::InputText*                   end_{nullptr};
    ui::InputText*                   grace_{nullptr};
    ui::Button*                      defaults_{nullptr};
    float                            rowY_[5]{};
};

// ----------------------------------------------------------------------------- etape 4 : les fonctionnalites
class FeatureGrid final : public ui::Widget {
public:
    struct Group {
        std::string                  product;
        std::vector<ui::Checkbox*>   boxes;
        float                        x{0}, y{0};
    };
    FeatureGrid() : ui::Widget("wizard.fonctionnalites.grille") {}
    std::vector<Group> groups;

    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        const float w = parent() ? std::max(200.f, parent()->bounds().w - 14.f) : 600.f;
        h.preferred = {w, arrange(w, false)};
        return h;
    }

protected:
    void onLayout() override { (void)arrange(bounds().w, true); }
    void onPaint(const ui::PaintContext& ctx) override {
        for (const auto& g : groups) {
            ctx.r.drawText({g.x, g.y}, g.product, ctx.theme.font.ui, ctx.theme.color.text);
            ctx.r.fillRect({g.x, g.y + 20.f, colW_ - 16.f, 1.f}, ctx.theme.color.border);
        }
    }

private:
    // Les groupes en colonnes (chacun dans la plus courte) ; rend la hauteur.
    float arrange(float width, bool place) const {
        const int cols = std::max(1, std::min(3, static_cast<int>(width / 230.f)));
        const float colW = width / static_cast<float>(cols);
        std::vector<float> height(static_cast<std::size_t>(cols), 0.f);
        const float x0 = bounds().x, y0 = bounds().y;
        for (const auto& g0 : groups) {
            auto& g = const_cast<Group&>(g0);
            const auto col = static_cast<std::size_t>(std::min_element(height.begin(), height.end()) - height.begin());
            const float gx = x0 + colW * static_cast<float>(col), gy = y0 + height[col];
            if (place) {
                g.x = gx;
                g.y = gy;
            }
            float y = gy + 26.f;
            for (auto* b : g.boxes) {
                if (place) b->setBounds({gx, y, colW - 16.f, 26.f});
                y += 26.f;
            }
            height[col] = y - y0 + 12.f;
        }
        if (place) colW_ = colW;
        return *std::max_element(height.begin(), height.end());
    }
    mutable float colW_{230.f};
};

class StepFeatures final : public Step {
public:
    explicit StepFeatures(WizardBody& w) : Step("wizard.etape.fonctionnalites", w) {
        reset_ = &button(*this, "Revenir \xC3\xA0 l'\xC3\xA9" "dition", "wizard.fonctionnalites.edition");
        scroll_ = &adopt(*this, std::make_unique<ui::ScrollablePanel>("wizard.fonctionnalites"));
        scroll_->setScrollPolicy(false, true);
        auto grid = std::make_unique<FeatureGrid>();
        grid_ = grid.get();
        const auto& reg = w_.ws().registry();
        for (const auto& f : reg.features) {
            if (f.retired && !w_.st().features.count(f.id)) continue;
            const std::string product = f.product.empty() ? std::string("Autres") : f.product;
            auto it = std::find_if(grid_->groups.begin(), grid_->groups.end(), [&](const FeatureGrid::Group& g) { return g.product == product; });
            if (it == grid_->groups.end()) {
                grid_->groups.push_back({product, {}, 0.f, 0.f});
                it = grid_->groups.end() - 1;
            }
            std::string label = f.label.empty() ? f.id : f.label;
            if (f.retired) label += " (retir\xC3\xA9" "e)";
            auto& box = adopt(*grid_, std::make_unique<ui::Checkbox>(label, "wizard.f." + f.id));
            box.setTooltip(f.id + (f.limits.empty() ? std::string() : " \xE2\x80\x94 avec des limites (celles de l'\xC3\xA9" "dition)"));
            it->boxes.push_back(&box);
            boxes_.emplace_back(f.id, &box);
            const std::string id = f.id;
            links_ += box.stateChanged->connect([this, id](ui::Checkbox::State st) {
                if (syncing_) return;
                if (st == ui::Checkbox::State::Checked) w_.st().features.insert(id);
                else w_.st().features.erase(id);
                w_.changed();
            });
        }
        scroll_->setContent(std::move(grid));
        links_ += reset_->clicked->connect([this] {
            w_.st().features = w_.editionFeatures(w_.st().edition);
            enter();
            w_.changed();
        });
    }
    [[nodiscard]] std::string heading() const override { return "Fonctionnalit\xC3\xA9s"; }
    [[nodiscard]] std::string intro() const override {
        return "Ce que la licence ouvre. Par d\xC3\xA9" "faut, celles de l'\xC3\xA9" "dition ; coche ou d\xC3\xA9" "coche pour ce client.";
    }
    void enter() override {
        syncing_ = true;
        for (auto& [id, box] : boxes_) box->setState(w_.st().features.count(id) ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        syncing_ = false;
        reset_->setEnabled(w_.customFeatures());
        invalidateLayout();
    }
    [[nodiscard]] std::string blocker() const override {
        if (w_.customFeatures() && w_.st().features.empty()) return "Coche au moins une fonctionnalit\xC3\xA9 (une liste vide voudrait dire : celles de l'\xC3\xA9" "dition).";
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        reset_->setBounds({r.right() - buttonWidth(*reset_, 150.f), r.y, buttonWidth(*reset_, 150.f), 28.f});
        scroll_->setBounds({r.x, r.y + 38.f, r.w, std::max(40.f, r.h - 38.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        reset_->setEnabled(w_.customFeatures());
        const auto& s = w_.st();
        const auto base = w_.editionFeatures(s.edition);
        int more = 0, less = 0;
        for (const auto& f : s.features)
            if (!base.count(f)) ++more;
        for (const auto& f : base)
            if (!s.features.count(f)) ++less;
        std::string line = plural(static_cast<int>(s.features.size()), "coch\xC3\xA9" "e", "coch\xC3\xA9" "es");
        const std::string ed = w_.editionLabel(s.edition);
        if (more == 0 && less == 0)
            line += " \xE2\x80\x94 celles de l'\xC3\xA9" "dition " + ed + ".";
        else
            line += " \xE2\x80\x94 personnalis\xC3\xA9" "es : " + std::to_string(more) + " de plus, " + std::to_string(less) + " de moins que " + ed + ".";
        const auto b = bounds();
        ctx.r.drawText({b.x, b.y + 5.f}, fit(ctx.r, line, ctx.theme.font.ui, reset_->bounds().x - b.x - 12.f), ctx.theme.font.ui,
                       more || less ? toneColor(ctx.theme, Tone::Warning) : ctx.theme.color.textMuted);
    }

private:
    ui::Button*                                          reset_{nullptr};
    ui::ScrollablePanel*                                 scroll_{nullptr};
    FeatureGrid*                                         grid_{nullptr};
    std::vector<std::pair<std::string, ui::Checkbox*>>   boxes_;
};

// --------------------------------------------------------------------------------- etape 5 : le recapitulatif
class StepRecap final : public Step {
public:
    explicit StepRecap(WizardBody& w) : Step("wizard.etape.recapitulatif", w) {
        lines_ = &adopt(*this, std::make_unique<ui::ListView>("wizard.recap.lignes"));
        lines_->setSelectionMode(ui::SelectionMode::None);
    }
    [[nodiscard]] std::string heading() const override { return "R\xC3\xA9" "capitulatif"; }
    [[nodiscard]] std::string intro() const override { return "Ce qui sera sign\xC3\xA9, v\xC3\xA9rifi\xC3\xA9 sans rien \xC3\xA9" "crire. Pr\xC3\xA9" "c\xC3\xA9" "dent pour corriger."; }
    void enter() override {
        auto& s = w_.st();
        s.preview = w_.ws().preview(w_.draft());
        model_ = std::make_shared<Lines>(linesOf(s.preview));
        lines_->setModel(model_);
        summary_ = summaryRows();
        invalidateLayout();
    }
    [[nodiscard]] std::string blocker() const override {
        const auto& p = w_.st().preview;
        return p.ok ? std::string() : p.message;
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        summaryH_ = static_cast<float>(summary_.size()) * 22.f + 10.f;
        lines_->setBounds({r.x, r.y + summaryH_ + 24.f, r.w, std::max(48.f, r.h - summaryH_ - 24.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const float colW = b.w;
        for (std::size_t i = 0; i < summary_.size(); ++i) {
            const float x = b.x;
            const float y = b.y + 22.f * static_cast<float>(i);
            r.drawText({x, y}, summary_[i].first, kSmall, c.textMuted);
            const bool warn = summary_[i].second.find("ESSAI") != std::string::npos;
            r.drawText({x + 140.f, y - 1.f}, fit(r, summary_[i].second, ctx.theme.font.ui, colW - 140.f), ctx.theme.font.ui,
                       warn ? toneColor(ctx.theme, Tone::Error) : c.text);
        }
        r.drawText({b.x, b.y + summaryH_ + 2.f}, "Ce que dit la signature (sans rien \xC3\xA9" "crire) :", ctx.theme.font.ui, c.textMuted);
    }

private:
    std::vector<std::pair<std::string, std::string>> summaryRows() const {
        const auto& s = w_.st();
        std::vector<std::pair<std::string, std::string>> out;
        out.emplace_back("Client", trim(s.customer));
        out.emplace_back("Licence", trim(s.licenseId) + (s.extend ? " (\xC3\xA9tendue)" : ""));
        out.emplace_back("\xC3\x89" "dition", w_.editionLabel(s.edition));
        out.emplace_back("Type", typeLabel(s.type));
        out.emplace_back("PC", std::to_string(s.maxDevices) + " autoris\xC3\xA9s : " + (w_.boundDevices() ? std::to_string(w_.boundDevices()) + " li\xC3\xA9s + " : std::string())
                                   + plural(w_.newDevices(), "nouveau", "nouveaux"));
        if (s.type == LicenseType::Perpetual)
            out.emplace_back("Validit\xC3\xA9", "sans fin");
        else
            out.emplace_back("Validit\xC3\xA9", durationLabel(s.days) + ", jusqu'au " + dateFr(w_.now() + static_cast<Seconds>(std::max(0, s.days)) * kDaySeconds));
        out.emplace_back("Gr\xC3\xA2" "ce hors ligne", s.type == LicenseType::Temporary ? std::to_string(s.grace) + " j" : std::string("sans objet"));
        out.emplace_back("Fonctionnalit\xC3\xA9s", std::to_string(s.features.size()) + (w_.customFeatures() ? " (personnalis\xC3\xA9" "es)" : " (celles de l'\xC3\xA9" "dition)"));
        const auto& k = w_.ws().key();
        out.emplace_back("Cl\xC3\xA9", k.test ? "CL\xC3\x89 D'ESSAI, jamais pour un client \xE2\x80\x94 " + k.name : k.name);
        return out;
    }

    ui::ListView*                                     lines_{nullptr};
    std::shared_ptr<Lines>                            model_;
    std::vector<std::pair<std::string, std::string>>  summary_;
    float                                             summaryH_{0.f};
};

// ------------------------------------------------------------------------------------ etape 6 : signer
class StepSign final : public Step {
public:
    explicit StepSign(WizardBody& w) : Step("wizard.etape.signer", w) {
        result_ = &adopt(*this, std::make_unique<ui::ListView>("wizard.resultat"));
        result_->setSelectionMode(ui::SelectionMode::None);
    }
    [[nodiscard]] std::string heading() const override { return w_.st().signedOk ? "Licence sign\xC3\xA9" "e" : "Signer"; }
    [[nodiscard]] std::string railLabel() const override { return "Signer"; }
    [[nodiscard]] std::string intro() const override {
        if (w_.st().signedOk) return "Envoie le fichier au client : il l'importe par Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA Activer par fichier de licence.";
        return "Signe la licence avec ta cl\xC3\xA9 et enregistre le fichier .xpglic \xC3\xA0 envoyer au client ; elle est aussi rang\xC3\xA9" "e dans l'archive.";
    }
    void enter() override {
        const auto& s = w_.st();
        std::vector<Lines::Line> lines;
        if (s.signAttempted) lines = linesOf(s.signResult);
        if (s.published || s.publishing) {
            lines.push_back({"", ui::Tone::None});
            if (s.publishing)
                lines.push_back({"Publication sur GitHub en cours\xE2\x80\xA6", ui::Tone::Info});
            else
                for (auto& l : linesOf(s.publish)) lines.push_back(std::move(l));
        }
        model_ = std::make_shared<Lines>(std::move(lines));
        result_->setModel(model_);
        show(result_, s.signAttempted);
        invalidateLayout();
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float top = w_.st().signedOk ? 0.f : 120.f;
        result_->setBounds({r.x, r.y + top, r.w, std::max(48.f, r.h - top)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& s = w_.st();
        if (s.signedOk) return;
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const auto& k = w_.ws().key();
        const std::string id = trim(s.licenseId);
        float y = b.y;
        y += drawWrapped(r, b.x, y, b.w, "Signer et enregistrer\xE2\x80\xA6 : choisis o\xC3\xB9 \xC3\xA9" "crire " + id + ".xpglic, le fichier \xC3\xA0 envoyer au client "
                                            + trim(s.customer) + ". La licence est ensuite rang\xC3\xA9" "e dans l'archive et chaque PC not\xC3\xA9 dans le registre des activations.",
                         ctx.theme.font.ui, c.text, 3);
        y += 6.f;
        r.drawText({b.x, y}, fit(r, "Cl\xC3\xA9 de signature : " + k.name + " (" + k.fingerprint + ")", ctx.theme.font.ui, b.w), ctx.theme.font.ui, c.textMuted);
        y += lineStep(ctx.theme.font.ui);
        if (k.test)
            r.drawText({b.x, y}, fit(r, "CL\xC3\x89 D'ESSAI : une licence sign\xC3\xA9" "e avec elle n'est jamais pour un client.", ctx.theme.font.ui, b.w), ctx.theme.font.ui,
                       toneColor(ctx.theme, Tone::Error));
    }

private:
    ui::ListView*           result_{nullptr};
    std::shared_ptr<Lines>  model_;
};

// =========================================================================================== WizardBody
WizardBody::WizardBody(WizardMenu& owner, ShellHost host, std::vector<std::string> requests, std::string extendId, std::string customer)
    : ui::Widget("dialog.licence-assistant.corps"), owner_(owner), host_(std::move(host)) {
    const Workspace& w = ws();
    const LicenceEntry* ext = extendId.empty() ? nullptr : w.licence(extendId);
    if (!w.key().present) {
        blockedWhy_ = "Il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur pour signer une licence" + (w.key().problem.empty() ? std::string() : " : " + w.key().problem)
                      + ".\n\nCr\xC3\xA9" "e-la, ou restaure ta sauvegarde, dans Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 ; puis reviens ici.";
        blockedPage_ = "securite";
        blockedLabel_ = "Aller aux Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9";
    } else if (!w.hasRegistry()) {
        blockedWhy_ = "Pas encore de registre sign\xC3\xA9 dans l'archive : une licence en a besoin (les \xC3\xA9" "ditions et leurs fonctionnalit\xC3\xA9s).\n\n"
                      "Signe-le dans \xC3\x89" "ditions ; puis reviens ici.";
        blockedPage_ = "editions";
        blockedLabel_ = "Aller aux \xC3\x89" "ditions";
    } else if (!extendId.empty() && !ext) {
        blockedWhy_ = "La licence " + extendId + " n'est pas dans l'archive : impossible d'y ajouter des PC.";
    }

    cancel_ = &button(*this, "Annuler", "wizard.annuler");
    prev_ = &button(*this, "Pr\xC3\xA9" "c\xC3\xA9" "dent", "wizard.precedent");
    next_ = &button(*this, "Suivant", "wizard.suivant", ui::Button::Style::Primary);
    sign_ = &button(*this, "Signer et enregistrer\xE2\x80\xA6", "wizard.signer", ui::Button::Style::Primary);
    folder_ = &button(*this, "Ouvrir le dossier", "wizard.dossier");
    github_ = &button(*this, "Publier sur GitHub", "wizard.github");
    close_ = &button(*this, "Fermer", "wizard.fermer", ui::Button::Style::Primary);
    goto_ = &button(*this, blockedLabel_.empty() ? std::string("Aller") : blockedLabel_, "wizard.aller", ui::Button::Style::Primary);
    links_ += cancel_->clicked->connect([this] { owner_.close(); });
    links_ += close_->clicked->connect([this] { owner_.close(); });
    links_ += prev_->clicked->connect([this] { prev(); });
    links_ += next_->clicked->connect([this] { next(); });
    links_ += sign_->clicked->connect([this] { sign(); });
    links_ += github_->clicked->connect([this] { publish(); });
    links_ += folder_->clicked->connect([this] {
        if (!st_.signResult.output.empty() && host_.openFolder) host_.openFolder(folderOf(st_.signResult.output));
    });
    links_ += goto_->clicked->connect([this] {
        if (!blockedPage_.empty() && host_.navigate) host_.navigate(blockedPage_);
        owner_.close();
    });

    if (blocked()) {
        updateButtons();
        return;
    }

    if (ext) loadBase(*ext);
    else {
        st_.customer = std::move(customer);
        st_.licenseId = w.suggestLicenseId();
    }
    addRequests(requests, true);

    steps_.push_back(&adopt(*this, std::make_unique<StepRequests>(*this)));
    steps_.push_back(&adopt(*this, std::make_unique<StepClient>(*this)));
    steps_.push_back(&adopt(*this, std::make_unique<StepEdition>(*this)));
    steps_.push_back(&adopt(*this, std::make_unique<StepFeatures>(*this)));
    steps_.push_back(&adopt(*this, std::make_unique<StepRecap>(*this)));
    steps_.push_back(&adopt(*this, std::make_unique<StepSign>(*this)));
    g_openWizard = this;   // le dernier ouvert prend les fichiers glisses
    goTo(0);
}

WizardBody::~WizardBody() {
    *alive_ = false;
    if (g_openWizard == this) g_openWizard = nullptr;
}

void WizardBody::loadBase(const LicenceEntry& l) {
    st_.extend = true;
    st_.extendId = l.licence.licenseId;
    st_.base = l.licence;
    st_.licenseId = l.licence.licenseId;
    st_.customer = l.licence.customer;
    st_.edition = l.licence.edition;
    st_.type = l.licence.type == LicenseType::Floating ? LicenseType::Temporary : l.licence.type;
    st_.maxDevices = l.licence.maxDevices;
    st_.days = 365;
    if (l.licence.expiresAt > now())
        st_.days = static_cast<int>((l.licence.expiresAt - now() + kDaySeconds - 1) / kDaySeconds);   // la meme fin, a un jour pres
    st_.grace = l.licence.offlineGraceDays;
    st_.features.clear();
    for (const auto& g : l.licence.features) st_.features.insert(g.id);
    for (auto& r : st_.requests) r.bound = r.ok() && st_.base.boundTo(r.p.record.deviceKey);
}

int WizardBody::newDevices() const {
    int n = 0;
    for (const auto& r : st_.requests)
        if (r.ok() && !r.bound) ++n;
    return n;
}

std::set<std::string> WizardBody::editionFeatures(const std::string& id) const {
    std::set<std::string> out;
    if (const auto* e = ws().registry().edition(id))
        for (const auto& g : e->features) out.insert(g.id);
    return out;
}

std::string WizardBody::editionLabel(const std::string& id) const {
    if (id.empty()) return "\xE2\x80\x94";
    const auto* e = ws().registry().edition(id);
    return e && !e->label.empty() ? e->label : id;
}

std::string WizardBody::requestedExisting() const {
    if (st_.extend) return {};
    std::string asked;
    for (const auto& r : st_.requests) {
        if (!r.ok() || r.p.record.licenseId.empty()) continue;
        if (!asked.empty() && asked != r.p.record.licenseId) return {};
        asked = r.p.record.licenseId;
    }
    return !asked.empty() && ws().licence(asked) ? asked : std::string();
}

std::string WizardBody::windowTitle() const {
    return st_.extend ? "Ajouter des PC \xC3\xA0 la licence " + st_.extendId : std::string("Nouvelle licence");
}

LicenceDraft WizardBody::draft() const {
    LicenceDraft d;
    for (const auto& r : st_.requests)
        if (r.ok()) d.requestPaths.push_back(r.p.path);
    d.licenseId = trim(st_.licenseId);
    d.customer = trim(st_.customer);
    d.edition = st_.edition;
    d.type = st_.type;
    d.maxDevices = std::max(1, st_.maxDevices);
    d.durationDays = st_.type == LicenseType::Perpetual ? 0 : st_.days;
    d.offlineGraceDays = st_.type == LicenseType::Temporary ? std::max(0, st_.grace) : 0;
    if (customFeatures()) d.features.assign(st_.features.begin(), st_.features.end());
    d.extend = st_.extend;
    return d;
}

void WizardBody::adoptRequestedId() {
    if (st_.extend || st_.idTouched) return;
    std::string asked;
    for (const auto& r : st_.requests) {
        if (!r.ok() || r.p.record.licenseId.empty()) continue;
        if (!asked.empty() && asked != r.p.record.licenseId) return;
        asked = r.p.record.licenseId;
    }
    if (!asked.empty() && licensing::isLicenseId(asked) && !ws().licence(asked)) st_.licenseId = asked;
}

void WizardBody::addRequests(const std::vector<std::string>& paths, bool quiet) {
    int added = 0, twice = 0;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        const bool samePath = std::any_of(st_.requests.begin(), st_.requests.end(), [&](const Req& r) { return r.p.path == path; });
        if (samePath) {
            ++twice;
            continue;
        }
        Req r;
        r.p = ws().readRequest(path);
        if (r.ok()) {
            const bool sameKey = std::any_of(st_.requests.begin(), st_.requests.end(),
                                             [&](const Req& q) { return q.ok() && q.p.record.deviceKey == r.p.record.deviceKey; });
            if (sameKey) {
                ++twice;
                continue;
            }
            r.bound = st_.extend && st_.base.boundTo(r.p.record.deviceKey);
        }
        st_.requests.push_back(std::move(r));
        ++added;
        g_lastFolder = g_lastFolder.empty() ? folderOf(path) : g_lastFolder;
    }
    adoptRequestedId();
    if (!steps_.empty()) steps_[0]->enter();
    changed();
    if (quiet) return;
    if (twice > 0)
        say(plural(twice, "demande d\xC3\xA9j\xC3\xA0 dans la liste (le m\xC3\xAAme fichier ou le m\xC3\xAAme PC) : ignor\xC3\xA9" "e.",
                   "demandes d\xC3\xA9j\xC3\xA0 dans la liste (les m\xC3\xAAmes fichiers ou les m\xC3\xAAmes PC) : ignor\xC3\xA9" "es."),
            Tone::Warning);
    else if (added > 0 && current_ != 0)
        say(plural(added, "demande ajout\xC3\xA9" "e (\xC3\xA9tape 1).", "demandes ajout\xC3\xA9" "es (\xC3\xA9tape 1)."), Tone::Info);
}

void WizardBody::removeRequests(std::vector<std::size_t> indexes) {
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it)
        if (*it < st_.requests.size()) st_.requests.erase(st_.requests.begin() + static_cast<std::ptrdiff_t>(*it));
    if (!steps_.empty()) steps_[0]->enter();
    changed();
}

void WizardBody::pickRequest() {
    if (blocked() || st_.signedOk || !host_.pickFile) return;
    ui::FilePick pick;
    pick.title = "Choisis une demande d'activation du client (.xpgreq)";
    pick.filters = {{"Demandes d'activation (.xpgreq)", "xpgreq"}, {"Tous les fichiers", "*"}};
    pick.start = g_lastFolder;
    auto alive = alive_;
    host_.pickFile(pick, [this, alive](const std::string& path) {
        if (!*alive || path.empty()) return;
        g_lastFolder = folderOf(path);
        addRequests({path});
    });
}

void WizardBody::chooseEdition(const std::string& id) {
    const auto* e = ws().registry().edition(id);
    if (!e) return;
    st_.edition = id;
    st_.type = e->type == LicenseType::Floating ? LicenseType::Temporary : e->type;
    st_.maxDevices = std::max(e->maxDevices, neededDevices());
    st_.raisedDevices = e->maxDevices < neededDevices();
    st_.days = e->durationDays > 0 ? e->durationDays : 365;
    st_.grace = e->offlineGraceDays;
    st_.endInvalid = false;
    st_.features = editionFeatures(id);
    changed();
}

void WizardBody::switchToExtension(const std::string& licenseId) {
    const LicenceEntry* l = ws().licence(licenseId);
    if (!l || st_.signedOk) return;
    loadBase(*l);
    steps_[0]->enter();
    steps_[current_]->enter();
    changed();
    say("Les PC seront ajout\xC3\xA9s \xC3\xA0 la licence " + licenseId + " (client " + l->licence.customer + ") ; ses PC li\xC3\xA9s sont gard\xC3\xA9s.", Tone::Info);
}

bool WizardBody::takeDrop(const std::vector<std::string>& paths) {
    if (owner_.closed() || blocked() || st_.signedOk) return false;
    addRequests(paths);
    return true;
}

void WizardBody::changed() {
    message_.clear();
    updateButtons();
    invalidate();
}

void WizardBody::say(std::string text, Tone tone) {
    message_ = std::move(text);
    messageTone_ = tone;
    invalidate();
}

bool WizardBody::canReach(std::size_t index) const {
    if (blocked() || st_.signedOk || index >= steps_.size()) return false;
    if (index <= current_) return true;
    if (index > furthest_) return false;
    for (std::size_t i = current_; i < index; ++i)
        if (!steps_[i]->blocker().empty()) return false;
    return true;
}

void WizardBody::goTo(std::size_t index) {
    if (index >= steps_.size()) return;
    for (std::size_t k = 0; k < steps_.size(); ++k) show(steps_[k], k == index);
    current_ = index;
    furthest_ = std::max(furthest_, index);
    steps_[index]->enter();
    message_.clear();
    updateButtons();
    invalidateLayout();
}

void WizardBody::next() {
    if (blocked() || st_.signedOk || current_ + 1 >= steps_.size()) return;
    const std::string why = steps_[current_]->blocker();
    if (!why.empty()) {
        say(why, Tone::Warning);
        return;
    }
    goTo(current_ + 1);
}

void WizardBody::prev() {
    if (blocked() || st_.signedOk || current_ == 0) return;
    goTo(current_ - 1);
}

void WizardBody::sign() {
    if (blocked() || st_.signedOk || current_ + 1 != steps_.size()) return;
    if (host_.busy && host_.busy()) {
        say("Un travail est en cours : attends qu'il finisse pour signer.", Tone::Warning);
        return;
    }
    // Ce qui sera signe doit encore tenir (l'archive a pu changer depuis le recapitulatif).
    const LicenceDraft d = draft();
    const OpResult check = ws().preview(d);
    if (!check.ok) {
        st_.signAttempted = true;
        st_.signResult = check;
        steps_.back()->enter();
        say(check.message, Tone::Error);
        return;
    }
    ui::FilePick pick;
    pick.title = "Enregistrer la licence \xC3\xA0 envoyer au client";
    pick.filters = {{"Licences XPG (.xpglic)", "xpglic"}};
    pick.save = true;
    pick.start = licdlg::startFor(d.licenseId + ".xpglic");
    auto alive = alive_;
    host_.pickFile(pick, [this, alive, d](const std::string& chosen) {
        if (!*alive || st_.signedOk) return;
        if (chosen.empty()) {
            say("Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 sign\xC3\xA9.", Tone::Info);
            return;
        }
        if (host_.busy && host_.busy()) {
            say("Un travail est en cours : attends qu'il finisse pour signer.", Tone::Warning);
            return;
        }
        std::string path = chosen;
        if (extOf(path) != ".xpglic") path += ".xpglic";
        OpResult r = ws().signLicence(d, path);
        st_.signAttempted = true;
        st_.signResult = r;
        st_.signedOk = r.ok;
        if (r.ok) licdlg::rememberFolder(path);
        if (host_.report) host_.report(r);
        if (!*alive) return;
        steps_.back()->enter();
        updateButtons();
        invalidateLayout();
        if (!r.ok) say(r.message, Tone::Error);
    });
}

void WizardBody::publish() {
    if (!st_.signedOk || st_.publishing || !ws().github().configured || !host_.runSlow) return;
    if (host_.busy && host_.busy()) {
        say("Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse pour publier.", Tone::Warning);
        return;
    }
    Workspace* w = &ws();
    auto alive = alive_;
    st_.publishing = true;
    steps_.back()->enter();
    updateButtons();
    const bool started = host_.runSlow(
        "Publication sur GitHub", [w] { return syncResult(w->githubSync(false, false)); },
        [this, alive](const OpResult& r) {
            if (!*alive) return;
            st_.publishing = false;
            st_.published = true;
            st_.publish = r;
            steps_.back()->enter();
            updateButtons();
        });
    if (!started && *alive && st_.publishing) {
        st_.publishing = false;
        steps_.back()->enter();
        updateButtons();
        say("Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse pour publier.", Tone::Warning);
    }
}

void WizardBody::updateButtons() {
    const bool isBlocked = blocked();
    const bool done = st_.signedOk;
    const bool last = !steps_.empty() && current_ + 1 == steps_.size();
    show(cancel_, !isBlocked && !done);
    show(prev_, !isBlocked && !done);
    show(next_, !isBlocked && !done && !last);
    show(sign_, !isBlocked && !done && last);
    show(folder_, done);
    show(github_, done && ws().github().configured);
    show(close_, isBlocked || done);
    show(goto_, isBlocked && !blockedPage_.empty());
    if (!isBlocked && !done && !steps_.empty()) {
        prev_->setEnabled(current_ > 0);
        next_->setEnabled(steps_[current_]->blocker().empty());
    }
    github_->setEnabled(!st_.publishing && !(st_.published && st_.publish.ok));
    invalidateLayout();
}

std::size_t WizardBody::railIndexAt(gfx::Point p) const {
    if (!rail_.contains(p)) return steps_.size();
    const float rel = p.y - (rail_.y + 16.f);
    if (rel < 0.f) return steps_.size();
    return std::min(steps_.size(), static_cast<std::size_t>(rel / 46.f));
}

void WizardBody::onLayout() {
    frame_.place(*this, 1040.f, 660.f);
    const auto& panel = frame_.panel;
    rail_ = {panel.x, panel.y + 43.f, 224.f, panel.h - 43.f - 57.f};
    const float cx = rail_.right() + 20.f, cw = panel.right() - 20.f - cx;
    const float introH = blocked() || steps_.empty() ? 0.f : wrappedHeight(steps_[current_]->intro(), gfx::FontId{16}, cw, 3);
    heading_ = {cx, panel.y + 43.f + 14.f, cw, 28.f + introH + 12.f};
    band_ = {cx, panel.bottom() - 56.f - 34.f, cw, 28.f};
    area_ = {cx, heading_.bottom(), cw, std::max(60.f, band_.y - 8.f - heading_.bottom())};
    for (auto* s : steps_) s->setBounds(area_);

    const float by = panel.bottom() - 44.f;
    float left = panel.x + 16.f;
    for (auto* b : {cancel_, folder_, github_}) {
        if (!b->visible()) continue;
        const float w = buttonWidth(*b, 110.f);
        b->setBounds({left, by, w, 32.f});
        left += w + 8.f;
    }
    float right = panel.right() - 16.f;
    for (auto* b : {next_, sign_, close_, goto_, prev_}) {
        if (!b->visible()) continue;
        const float w = buttonWidth(*b, 110.f);
        right -= w;
        b->setBounds({right, by, w, 32.f});
        right -= 8.f;
    }
}

void WizardBody::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& panel = frame_.panel;
    frame_.paint(r, ctx.theme, windowTitle());
    // Les etapes, a gauche.
    r.fillRect(rail_, c.headerBg);
    r.fillRect({rail_.right(), rail_.y, 1.f, rail_.h}, c.border);
    if (!steps_.empty()) {
        for (std::size_t i = 0; i < steps_.size(); ++i) {
            const float y = rail_.y + 16.f + 46.f * static_cast<float>(i);
            const bool cur = i == current_ && !st_.signedOk;
            const bool done = st_.signedOk || i < current_ || (i <= furthest_ && i != current_ && steps_[i]->blocker().empty());
            const gfx::Rect dot{rail_.x + 18.f, y + 4.f, 26.f, 26.f};
            const gfx::Color dotColor = cur ? c.accent : done ? toneColor(ctx.theme, Tone::Ok) : c.border;
            if (cur) r.fillRect({rail_.x, y, 3.f, 34.f}, c.accent);
            r.fillRoundedRect(dot, dotColor, 13.f);
            const std::string mark = done && !cur ? std::string("\xE2\x9C\x93") : std::to_string(i + 1);
            const float mw = r.measure(mark, ctx.theme.font.ui).width;
            r.drawText({dot.x + (dot.w - mw) * 0.5f, dot.y + (dot.h - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, mark, ctx.theme.font.ui,
                       cur || done ? c.textInverted : c.textMuted);
            r.drawText({rail_.x + 54.f, y + 4.f + (26.f - r.lineHeight(ctx.theme.font.ui)) * 0.5f},
                       fit(r, steps_[i]->railLabel(), ctx.theme.font.ui, rail_.w - 60.f), ctx.theme.font.ui,
                       cur ? c.text : (canReach(i) || done ? c.text : c.textMuted));
        }
    }
    // Le contenu : l'etape, ou ce qui manque pour signer.
    if (blocked()) {
        const float x = rail_.right() + 20.f, w = panel.right() - 20.f - x;
        r.drawText({x, panel.y + 60.f}, "Impossible de signer une licence ici pour l'instant", kHeading, toneColor(ctx.theme, Tone::Warning));
        (void)drawWrapped(r, x, panel.y + 100.f, w, blockedWhy_, ctx.theme.font.ui, c.text);
    } else if (!steps_.empty()) {
        const Step& s = *steps_[current_];
        r.drawText({heading_.x, heading_.y}, fit(r, std::to_string(current_ + 1) + ". " + s.heading(), kHeading, heading_.w), kHeading, c.text);
        (void)drawWrapped(r, heading_.x, heading_.y + 30.f, heading_.w, s.intro(), ctx.theme.font.ui, c.textMuted, 3);
        std::string line = message_;
        Tone tone = messageTone_;
        if (line.empty() && !st_.signedOk) {
            line = s.blocker();
            tone = Tone::Warning;
        }
        if (line.empty() && st_.signedOk && st_.published) {
            line = st_.publish.message;
            tone = st_.publish.ok ? Tone::Ok : Tone::Error;
        }
        drawBand(r, ctx.theme, band_, line, tone);
    }
    r.fillRect({panel.x, panel.bottom() - 57.f, panel.w, 1.f}, c.border);
}

ui::EventResult WizardBody::onEvent(const ui::InputEvent& ev) {
    if (frame_.hitClose(ev)) {
        owner_.close();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const std::size_t i = railIndexAt(d->pos);
        if (i < steps_.size()) {
            if (i != current_ && canReach(i)) goTo(i);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// =================================================================================== revoquer (un dialogue)
class RevokeBody final : public ui::Widget {
public:
    RevokeBody(DialogMenu& owner, ShellHost host, std::string licenseId)
        : ui::Widget("dialog.revoquer.corps"), owner_(owner), host_(std::move(host)), fixed_(std::move(licenseId)) {
        const Workspace& w = host_.workspace();
        if (fixed_.empty()) {
            licence_ = &adopt(*this, std::make_unique<ui::DropDown>("revoquer.licence"));
            std::vector<ui::DropDown::Item> items;
            for (const auto& l : w.licences()) {
                ids_.push_back(l.licence.licenseId);
                ui::DropDown::Item it{l.licence.licenseId + " \xE2\x80\x94 " + l.licence.customer, l.licence.licenseId};
                it.note = l.stateLabel();
                it.keywords = l.licence.customer + " " + l.licence.edition;
                items.push_back(std::move(it));
            }
            licence_->setItems(std::move(items));
            links_ += licence_->selectionChanged->connect([this](int) { reloadPcs(); });
        }
        group_ = std::make_shared<ui::RadioGroup>();
        whole_ = &adopt(*this, std::make_unique<ui::RadioButton>("Toute la licence (tous ses PC)", group_, 0, "revoquer.toute"));
        one_ = &adopt(*this, std::make_unique<ui::RadioButton>("Un seul PC :", group_, 1, "revoquer.unpc"));
        pc_ = &adopt(*this, std::make_unique<ui::DropDown>("revoquer.pc"));
        reason_ = &adopt(*this, std::make_unique<ui::InputText>("revoquer.raison"));
        reason_->setPlaceholder("Fin de contrat, PC remplac\xC3\xA9, impay\xC3\xA9\xE2\x80\xA6");
        reason_->setMaxLength(300);
        onlyIssued_ = &adopt(*this, std::make_unique<ui::Checkbox>("Seulement les \xC3\xA9missions d\xC3\xA9j\xC3\xA0 sign\xC3\xA9" "es", "revoquer.emissions"));
        onlyIssued_->setTooltip("Une \xC3\xA9mission sign\xC3\xA9" "e apr\xC3\xA8s (licence renouvel\xC3\xA9" "e, PC ajout\xC3\xA9) reste valable.");
        cancel_ = &button(*this, "Annuler", "revoquer.annuler");
        ok_ = &button(*this, "R\xC3\xA9voquer", "revoquer.ok", ui::Button::Style::Danger);
        group_->setValue(0);
        links_ += group_->valueChanged->connect([this](int v) {
            pc_->setEnabled(v == 1);
            invalidate();
        });
        links_ += pc_->selectionChanged->connect([this](int i) {
            if (i >= 0) group_->setValue(1);
        });
        links_ += reason_->textChanged->connect([this](const std::string&) {
            error_.clear();
            invalidate();
        });
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] { confirm(); });
        reloadPcs();
    }

protected:
    void onLayout() override {
        frame_.place(*this, 640.f, 520.f);
        const auto& p = frame_.panel;
        const float x = p.x + 20.f, w = p.w - 40.f, cx = x + 150.f, cw = p.right() - 20.f - cx;
        float y = p.y + 60.f;
        licenceY_ = y;
        if (licence_) licence_->setBounds({cx, y, cw, 30.f});
        y += 48.f;
        whatY_ = y;
        whole_->setBounds({cx, y, cw, 26.f});
        y += 32.f;
        one_->setBounds({cx, y, 120.f, 28.f});
        pc_->setBounds({cx + 124.f, y, cw - 124.f, 28.f});
        y += 46.f;
        reasonY_ = y;
        reason_->setBounds({cx, y, cw, 30.f});
        y += 48.f;
        onlyIssued_->setBounds({cx, y, cw, 26.f});
        noteY_ = y + 30.f;
        const float by = p.bottom() - 44.f;
        const float okW = buttonWidth(*ok_, 120.f), caW = buttonWidth(*cancel_, 110.f);
        ok_->setBounds({p.right() - 16.f - okW, by, okW, 32.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - caW, by, caW, 32.f});
        (void)w;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto& p = frame_.panel;
        frame_.paint(r, ctx.theme, fixed_.empty() ? std::string("R\xC3\xA9voquer une licence") : "R\xC3\xA9voquer \xE2\x80\x94 " + fixed_);
        const float x = p.x + 20.f, cx = x + 150.f, cw = p.right() - 20.f - cx;
        const gfx::FontId f = ctx.theme.font.ui;
        const float ty = (28.f - r.lineHeight(f)) * 0.5f;
        r.drawText({x, licenceY_ + ty}, "Licence", f, c.text);
        if (!licence_) {
            const LicenceEntry* l = host_.workspace().licence(fixed_);
            const std::string what = l ? fixed_ + " \xE2\x80\x94 " + l->licence.customer + " (" + l->licence.edition + ", " + l->stateLabel() + ")"
                                       : fixed_ + " (pas dans l'archive)";
            r.drawText({cx, licenceY_ + ty}, fit(r, what, f, cw), f, c.text);
        } else if (ids_.empty()) {
            r.drawText({cx, licenceY_ + 34.f}, "Aucune licence dans l'archive.", kSmall, c.textMuted);
        }
        r.drawText({x, whatY_ + 3.f}, "R\xC3\xA9voquer", f, c.text);
        r.drawText({x, reasonY_ + ty}, "Raison", f, c.text);
        float y = noteY_;
        y += drawWrapped(r, cx, y, cw,
                         "Pour retirer une ancienne \xC3\xA9mission remplac\xC3\xA9" "e par une plus r\xC3\xA9" "cente : une licence sign\xC3\xA9" "e ensuite reste valable.",
                         kSmall, c.textMuted, 2);
        y += 10.f;
        y += drawWrapped(r, x, y, p.w - 40.f,
                         "Sign\xC3\xA9" "e dans une nouvelle liste, elle n'arrive chez le client qu'avec un fichier qu'il importe : sa prochaine licence, ou la "
                         "liste export\xC3\xA9" "e (R\xC3\xA9vocations \xE2\x80\xBA Exporter la liste\xE2\x80\xA6). Pas de service en ligne.",
                         f, c.textMuted, 4);
        if (!error_.empty()) drawBand(r, ctx.theme, {x, p.bottom() - 56.f - 34.f, p.w - 40.f, 28.f}, error_, Tone::Error);
        r.fillRect({p.x, p.bottom() - 57.f, p.w, 1.f}, c.border);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    [[nodiscard]] std::string chosen() const {
        if (!fixed_.empty()) return fixed_;
        const int i = licence_ ? licence_->selectedIndex() : -1;
        return i >= 0 && static_cast<std::size_t>(i) < ids_.size() ? ids_[static_cast<std::size_t>(i)] : std::string();
    }
    void reloadPcs() {
        keys_.clear();
        std::vector<ui::DropDown::Item> items;
        const Workspace& w = host_.workspace();
        if (const LicenceEntry* l = w.licence(chosen())) {
            for (const auto& d : l->licence.devices) {
                bool rev = false;
                for (const auto& x : w.revocations().revoked)
                    if (x.licenseId == l->licence.licenseId && x.device == d.key && (x.issuedBefore == 0 || l->licence.issuedAt < x.issuedBefore)) rev = true;
                ui::DropDown::Item it{(d.name.empty() ? std::string("(sans nom)") : d.name) + " \xE2\x80\x94 " + licdlg::fingerprintOfKey(d.key), d.key};
                if (rev) {
                    it.enabled = false;
                    it.note = "d\xC3\xA9j\xC3\xA0 r\xC3\xA9voqu\xC3\xA9";
                }
                items.push_back(std::move(it));
                keys_.push_back(d.key);
            }
        }
        pc_->setItems(std::move(items));
        pc_->setSelectedIndex(-1);
        one_->setEnabled(!keys_.empty());
        if (keys_.empty()) group_->setValue(0);
        pc_->setEnabled(group_->value() == 1);
        invalidate();
    }
    void confirm() {
        error_.clear();
        if (host_.busy && host_.busy()) {
            error_ = "Un travail est en cours : attends qu'il finisse pour r\xC3\xA9voquer.";
            invalidate();
            return;
        }
        const std::string id = chosen();
        if (id.empty()) {
            error_ = "Choisis la licence \xC3\xA0 r\xC3\xA9voquer.";
            invalidate();
            return;
        }
        std::string key;
        if (group_->value() == 1) {
            const int i = pc_->selectedIndex();
            if (i < 0 || static_cast<std::size_t>(i) >= keys_.size()) {
                error_ = "Choisis le PC \xC3\xA0 r\xC3\xA9voquer.";
                invalidate();
                return;
            }
            key = keys_[static_cast<std::size_t>(i)];
        }
        const std::string reason = trim(reason_->text());
        if (reason.empty()) {
            error_ = "Donne une raison (\xC2\xAB Fin de contrat \xC2\xBB, \xC2\xAB PC remplac\xC3\xA9 \xC2\xBB\xE2\x80\xA6).";
            invalidate();
            return;
        }
        const OpResult r = host_.workspace().revoke(id, key, reason, onlyIssued_->isChecked());
        if (host_.report) host_.report(r);
        if (r.ok) owner_.close();
        else error_ = r.message;
        invalidate();
    }

    DialogMenu&                     owner_;
    ShellHost                       host_;
    std::string                     fixed_;
    std::vector<std::string>        ids_, keys_;
    ui::DropDown*                   licence_{nullptr};
    std::shared_ptr<ui::RadioGroup> group_;
    ui::RadioButton*                whole_{nullptr};
    ui::RadioButton*                one_{nullptr};
    ui::DropDown*                   pc_{nullptr};
    ui::InputText*                  reason_{nullptr};
    ui::Checkbox*                   onlyIssued_{nullptr};
    ui::Button*                     cancel_{nullptr};
    ui::Button*                     ok_{nullptr};
    std::string                     error_;
    Frame                           frame_;
    float                           licenceY_{0}, whatY_{0}, reasonY_{0}, noteY_{0};
    core::ConnectionScope           links_;
};

class RevokeMenu final : public DialogMenu {
public:
    RevokeMenu(ShellHost host, std::string licenseId) : DialogMenu("dialog.revoquer"), host_(std::move(host)), id_(std::move(licenseId)) {}
    [[nodiscard]] std::string title() const override { return "R\xC3\xA9voquer"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<RevokeBody>(*this, host_, id_));
        return core::ok();
    }

private:
    ShellHost   host_;
    std::string id_;
};

// ================================================================================= confirmer (un dialogue)
class ConfirmBody final : public ui::Widget {
public:
    ConfirmBody(DialogMenu& owner, std::string title, std::string text, std::string okLabel, bool danger, std::function<void()> onOk)
        : ui::Widget("dialog.confirmer.corps"), owner_(owner), title_(std::move(title)), text_(std::move(text)), onOk_(std::move(onOk)) {
        cancel_ = &button(*this, "Annuler", "confirmer.annuler");
        ok_ = &button(*this, std::move(okLabel), "confirmer.ok", danger ? ui::Button::Style::Danger : ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] {
            if (done_) return;
            done_ = true;
            auto run = onOk_;
            owner_.close();
            if (run) run();
        });
    }

protected:
    void onLayout() override {
        const float h = 43.f + 24.f + wrappedHeight(text_, gfx::FontId{16}, 520.f - 40.f) + 24.f + 57.f;
        frame_.place(*this, 520.f, std::max(200.f, h));
        const auto& p = frame_.panel;
        const float by = p.bottom() - 44.f;
        const float okW = buttonWidth(*ok_, 120.f), caW = buttonWidth(*cancel_, 110.f);
        ok_->setBounds({p.right() - 16.f - okW, by, okW, 32.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - caW, by, caW, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& p = frame_.panel;
        frame_.paint(ctx.r, ctx.theme, title_);
        (void)drawWrapped(ctx.r, p.x + 20.f, p.y + 43.f + 20.f, p.w - 40.f, text_, ctx.theme.font.ui, ctx.theme.color.text);
        ctx.r.fillRect({p.x, p.bottom() - 57.f, p.w, 1.f}, ctx.theme.color.border);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    DialogMenu&           owner_;
    std::string           title_, text_;
    std::function<void()> onOk_;
    bool                  done_{false};
    ui::Button*           cancel_{nullptr};
    ui::Button*           ok_{nullptr};
    Frame                 frame_;
    core::ConnectionScope links_;
};

class ConfirmMenu final : public DialogMenu {
public:
    ConfirmMenu(std::string id, std::string title, std::string text, std::string okLabel, bool danger, std::function<void()> onOk)
        : DialogMenu("dialog." + std::move(id)), title_(std::move(title)), text_(std::move(text)), ok_(std::move(okLabel)), danger_(danger),
          onOk_(std::move(onOk)) {}
    [[nodiscard]] std::string title() const override { return title_; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<ConfirmBody>(*this, title_, text_, ok_, danger_, onOk_));
        return core::ok();
    }

private:
    std::string           title_, text_, ok_;
    bool                  danger_{false};
    std::function<void()> onOk_;
};

} // namespace

// ============================================================================================ l'exterieur
menu::MenuPtr makeLicenceWizard(ShellHost host, std::vector<std::string> requests, std::string extendLicenseId, std::string customer) {
    return std::make_unique<WizardMenu>(std::move(host), std::move(requests), std::move(extendLicenseId), std::move(customer));
}

namespace licdlg {

menu::MenuPtr makeRevokeDialog(ShellHost host, std::string licenseId) { return std::make_unique<RevokeMenu>(std::move(host), std::move(licenseId)); }

menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger, std::function<void()> onOk) {
    return std::make_unique<ConfirmMenu>(std::move(id), std::move(title), std::move(text), std::move(okLabel), danger, std::move(onOk));
}

bool wizardTakeDrop(const std::vector<std::string>& paths) { return g_openWizard && g_openWizard->takeDrop(paths); }

std::string fingerprintOfKey(const std::string& hex) {
    deploy::crypto::Bytes raw;
    deploy::crypto::PublicKey pub{};
    if (!deploy::crypto::unhex(hex, raw, 32) || raw.size() != pub.size()) return hex.size() > 16 ? hex.substr(0, 16) + "\xE2\x80\xA6" : hex;
    std::copy(raw.begin(), raw.end(), pub.begin());
    return licensing::publicKeyFingerprint(pub);
}

std::string startFor(const std::string& name) { return g_lastFolder.empty() ? name : U(P(g_lastFolder) / P(name)); }

void rememberFolder(const std::string& filePath) {
    const std::string f = folderOf(filePath);
    if (!f.empty()) g_lastFolder = f;
}

} // namespace licdlg

} // namespace license_admin::gui
