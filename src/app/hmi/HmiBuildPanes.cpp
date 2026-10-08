// =============================================================================
//  app/hmi/HmiBuildPanes.cpp - 1.11.13 : voir HmiBuildPanes.hpp
// =============================================================================
#include "HmiBuildPanes.hpp"

#include "../../menu/MenuManager.hpp"
#include "../../ui/Icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace app {

namespace pl = hmi::pipeline;

namespace {
const gfx::FontId kSmall{13};
constexpr float kW = 760.f;

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, s < 10.0 ? "%.2f s" : "%.1f s", s);
    std::string t(buf);
    for (auto& c : t)
        if (c == '.') c = ',';
    return t;
}
std::string clockNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}
std::string plural(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

ui::Tone severityTone(pl::Severity s) {
    switch (s) {
        case pl::Severity::Success: return ui::Tone::Ok;
        case pl::Severity::Warning: return ui::Tone::Warning;
        case pl::Severity::Error:
        case pl::Severity::Critical: return ui::Tone::Error;
        case pl::Severity::Information: break;
    }
    return ui::Tone::Info;
}
int severityIndex(pl::Severity s) { return static_cast<int>(s); }   // Information, Success, Warning, Error, Critical

// Une table de textes, une gravite par ligne (la couleur de la ligne).
class Rows final : public ui::ITableModel {
public:
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<pl::Severity> sev, std::vector<bool> rules,
         std::size_t messageColumn)
        : headers_(std::move(headers)), rows_(std::move(rows)), sev_(std::move(sev)), rules_(std::move(rules)), message_(messageColumn) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override { return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{}; }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= sev_.size()) return st;
        if (r < rules_.size() && rules_[r]) {
            st.bold = true;
            st.fgTone = ui::Tone::Accent;
            return st;
        }
        const auto s = sev_[r];
        const ui::Tone tone = severityTone(s);
        if (s == pl::Severity::Error || s == pl::Severity::Critical) st.bg = gfx::Color{229, 83, 75, 40};
        else if (s == pl::Severity::Warning) st.bg = gfx::Color{242, 153, 74, 30};
        if (c == 1) {
            st.icon = s == pl::Severity::Error || s == pl::Severity::Critical ? ui::Icon::Error : s == pl::Severity::Warning ? ui::Icon::Warning
                    : s == pl::Severity::Success ? ui::Icon::Ok : ui::Icon::Info;
            st.iconTone = tone;
            st.fgTone = tone;
            st.bold = s != pl::Severity::Information;
        } else if (c == message_ && s != pl::Severity::Information) {
            st.fgTone = tone;
        }
        if (c == 0) st.monospace = true;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<pl::Severity> sev_;
    std::vector<bool> rules_;
    std::size_t message_{0};
};
} // namespace

// ======================================================== la progression ======
class HmiBuildProgressBody final : public ui::Widget {
public:
    explicit HmiBuildProgressBody(HmiBuildProgressDialog& d) : ui::Widget("hmiBuildProgress.body"), d_(d) {}

protected:
    void onLayout() override {
        const auto r = bounds();
        const float h = height();
        panel_ = {std::floor((r.w - kW) * 0.5f), std::floor(std::max(10.f, (r.h - h) * 0.5f)), kW, h};
        float bx = panel_.x + panel_.w - 16.f;
        for (auto* b : {d_.cancel_, d_.bg_, d_.outputs_}) {
            if (!b || !b->visible()) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 32.f);
            bx -= bw;
            b->setBounds({bx, panel_.y + panel_.h - 42.f, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto& p = d_.progress_;
        const auto* rep = d_.report_.get();
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = 36.f;
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        ui::drawIcon(ctx.r, ui::Icon::Analyze, {panel_.x + 12.f, panel_.y + 10.f, 16.f, 16.f}, c.text);
        const std::string title = d_.title_ + "  \xC2\xB7  " + plural(static_cast<long long>(p.total), "t\xC3\xA2" "che", "t\xC3\xA2" "ches");
        ctx.r.drawText({panel_.x + 36.f, panel_.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, title, ctx.theme.font.uiBold, c.text);
        const std::string el = seconds(d_.elapsed_);
        ctx.r.drawText({panel_.x + panel_.w - 14.f - ctx.r.measure(el, kSmall).width, panel_.y + (titleH - ctx.r.lineHeight(kSmall)) * 0.5f}, el, kSmall, c.textMuted);
        const float x = panel_.x + 16.f, w = panel_.w - 32.f;
        float y = panel_.y + titleH + 12.f;
        // le pourcentage, les taches
        const double fraction = p.total ? static_cast<double>(p.done) / static_cast<double>(p.total) : (d_.done_ ? 1.0 : 0.0);
        const bool failed = d_.done_ && rep && !rep->ok && !rep->cancelled;
        const bool cancelled = d_.done_ && rep && rep->cancelled;
        const gfx::Color barColor = failed ? c.error : cancelled ? c.warning : d_.done_ ? c.ok : c.accent;
        const std::string pc = std::to_string(static_cast<int>(std::lround(std::clamp(fraction, 0.0, 1.0) * 100.0))) + " %";
        ctx.r.drawText({x, y}, pc, ctx.theme.font.uiBold, d_.done_ ? barColor : c.text);
        const std::string tasks = std::to_string(p.done) + " / " + plural(static_cast<long long>(p.total), "t\xC3\xA2" "che", "t\xC3\xA2" "ches");
        ctx.r.drawText({x + w - ctx.r.measure(tasks, kSmall).width, y + 2.f}, tasks, kSmall, c.textMuted);
        y += 26.f;
        const gfx::Rect bar{x, y, w, 10.f};
        ctx.r.fillRoundedRect(bar, c.border, 5.f);
        ctx.r.fillRoundedRect({bar.x, bar.y, std::max(10.f, bar.w * static_cast<float>(std::clamp(fraction, 0.0, 1.0))), bar.h}, barColor, 5.f);
        y += 20.f;
        const auto line = [&](const std::string& label, std::string value, gfx::Color col) {
            ctx.r.drawText({x, y}, label, ctx.theme.font.ui, c.textMuted);
            const float lw = ctx.r.measure(label, ctx.theme.font.ui).width + 8.f;
            while (value.size() > 8 && ctx.r.measure(value, ctx.theme.font.uiBold).width > w - lw) value = value.substr(0, value.size() - 4) + "\xE2\x80\xA6";
            ctx.r.drawText({x + lw, y}, value, ctx.theme.font.uiBold, col);
            y += 22.f;
        };
        line("\xC3\x89tape :", d_.done_ ? (failed ? std::string("termin\xC3\xA9 avec des erreurs") : cancelled ? std::string("annul\xC3\xA9") : std::string("termin\xC3\xA9")) : p.step, c.text);
        line("\xC3\x89l\xC3\xA9ment :", d_.done_ ? std::string("\xE2\x80\x94") : (p.element.empty() ? std::string("\xE2\x80\x94") : p.element), c.text);
        {
            const std::string t = "Temps \xC3\xA9" "coul\xC3\xA9 : " + seconds(d_.elapsed_);
            ctx.r.drawText({x, y}, t, ctx.theme.font.ui, c.textMuted);
            float cx = x + ctx.r.measure(t, ctx.theme.font.ui).width + 24.f;
            const int warns = rep ? rep->warnings : p.warnings, errs = rep ? rep->errors : p.errors;
            const std::string wt = "\xE2\x9A\xA0 " + plural(warns, "avertissement", "avertissements");
            ctx.r.drawText({cx, y}, wt, ctx.theme.font.ui, warns ? c.warning : c.textMuted);
            cx += ctx.r.measure(wt, ctx.theme.font.ui).width + 24.f;
            ctx.r.drawText({cx, y}, "\xE2\x9C\x95 " + plural(errs, "erreur", "erreurs"), ctx.theme.font.ui, errs ? c.error : c.textMuted);
            y += 28.f;
        }
        // les phases, et sous B et C leurs sous-etapes
        const gfx::Rect box{x, y, w, phasesHeight()};
        ctx.r.fillRect(box, c.windowBg);
        ctx.r.strokeRect(box, c.border, 1.f);
        float py = y + 6.f;
        const char* letters = "ABCDEFG";
        for (int ph = 0; ph < static_cast<int>(pl::Phase::Count); ++ph) {
            const auto st = p.phases[ph];
            const bool running = st == pl::PhaseState::Running && !d_.done_;
            std::string glyph = "\xE2\x80\x93";   // –
            gfx::Color gc = c.textMuted;
            switch (st) {
                case pl::PhaseState::Done: glyph = "\xE2\x9C\x93"; gc = c.ok; break;
                case pl::PhaseState::Skipped: glyph = "\xE2\x9C\x93"; gc = c.textMuted; break;
                case pl::PhaseState::Failed: glyph = "\xE2\x9C\x95"; gc = c.error; break;
                case pl::PhaseState::Cancelled: glyph = "\xE2\x8A\x98"; gc = c.warning; break;
                case pl::PhaseState::Running: glyph = d_.done_ ? std::string("\xE2\x8A\x98") : std::string("\xE2\x86\xBB"); gc = d_.done_ ? c.warning : c.accent; break;
                case pl::PhaseState::NotAsked: glyph = "\xE2\x80\x93"; gc = c.textMuted; break;
                case pl::PhaseState::Todo: glyph = "\xE2\x97\x8B"; gc = c.textMuted; break;
            }
            ctx.r.drawText({x + 10.f, py}, std::string(1, letters[ph]), kSmall, c.textMuted);
            ctx.r.drawText({x + 26.f, py}, glyph, ctx.theme.font.ui, gc);
            const std::string label = std::string(pl::phaseLabel(static_cast<pl::Phase>(ph)));
            ctx.r.drawText({x + 48.f, py}, label, running ? ctx.theme.font.uiBold : ctx.theme.font.ui, st == pl::PhaseState::NotAsked ? c.textMuted : c.text);
            std::string note = p.phaseNotes[ph];
            if (note.empty()) note = st == pl::PhaseState::NotAsked ? "non demand\xC3\xA9" : st == pl::PhaseState::Todo ? "\xC3\xA0 venir" : running ? "en cours\xE2\x80\xA6" : std::string{};
            ctx.r.drawText({x + w - 10.f - ctx.r.measure(note, kSmall).width, py + 2.f}, note, kSmall, c.textMuted);
            py += 21.f;
            if (ph == static_cast<int>(pl::Phase::Api) || ph == static_cast<int>(pl::Phase::Ihm)) {
                const bool api = ph == static_cast<int>(pl::Phase::Api);
                const int n = api ? pl::kApiSteps : pl::kIhmSteps;
                const float colW = (w - 60.f) / 2.f;
                for (int k = 0; k < n; ++k) {
                    const auto& sub = api ? p.api[k] : p.ihm[k];
                    const float sx = x + 48.f + static_cast<float>(k % 2) * (colW + 12.f);
                    const float sy = py + static_cast<float>(k / 2) * 18.f;
                    const bool current = !api && p.currentSub == k && running;
                    std::string g = "\xE2\x9C\x93";
                    gfx::Color gcol = c.ok;
                    std::string what = "\xC3\xA0 jour";
                    if (sub.failed > 0) { g = "\xE2\x9C\x95"; gcol = c.error; what = std::to_string(sub.failed) + " en \xC3\xA9" "chec"; }
                    else if (sub.todo > 0 && sub.done < sub.todo) { g = current ? "\xE2\x86\xBB" : "\xE2\x97\x8B"; gcol = current ? c.accent : c.textMuted; what = std::to_string(sub.done) + "/" + std::to_string(sub.todo); }
                    else if (sub.todo > 0) what = std::to_string(sub.done) + "/" + std::to_string(sub.todo);
                    if (st == pl::PhaseState::Todo || (st == pl::PhaseState::Running && sub.todo == 0 && !d_.done_)) { if (sub.todo == 0) { g = "\xE2\x9C\x93"; gcol = c.textMuted; } }
                    ctx.r.drawText({sx, sy}, g, kSmall, gcol);
                    std::string name = std::to_string(k + 1) + ". " + std::string(pl::stepName(api ? pl::Area::Api : pl::Area::Ihm, k));
                    ctx.r.drawText({sx + 16.f, sy}, name, kSmall, current ? c.text : c.textMuted);
                    ctx.r.drawText({sx + colW - ctx.r.measure(what, kSmall).width, sy}, what, kSmall, c.textMuted);
                }
                py += static_cast<float>((n + 1) / 2) * 18.f + 4.f;
            }
        }
        y += box.h + 12.f;
        // l'etat final
        if (d_.done_ && rep) {
            std::string fin, detail;
            gfx::Color col = c.ok;
            if (rep->locked) { fin = "Build refus\xC3\xA9 : un autre build tourne sur ce projet."; col = c.error; }
            else if (rep->cancelled) { fin = "Build annul\xC3\xA9 : les artefacts valides pr\xC3\xA9" "c\xC3\xA9" "dents sont conserv\xC3\xA9s."; col = c.warning; }
            else if (!rep->ok) {
                fin = "Build \xC3\xA9" "chou\xC3\xA9 : " + plural(rep->errors, "erreur", "erreurs") + ", " + plural(rep->warnings, "avertissement", "avertissements")
                    + (d_.startsSimulation_ ? " \xE2\x80\x94 la simulation ne d\xC3\xA9marre pas." : ".");
                col = c.error;
            } else if (rep->upToDate) fin = "Projet \xC3\xA0 jour : rien \xC3\xA0 g\xC3\xA9n\xC3\xA9rer ni \xC3\xA0 compiler.";
            else {
                // deux lignes : le verdict, puis les comptes (sur une seule, il sortait du cadre)
                fin = "Build r\xC3\xA9ussi : " + plural(rep->errors, "erreur", "erreurs") + ", " + plural(rep->warnings, "avertissement", "avertissements")
                    + (d_.startsSimulation_ ? " \xE2\x80\x94 la simulation d\xC3\xA9marre." : ".");
                detail = std::to_string(rep->generated) + " g\xC3\xA9n\xC3\xA9r\xC3\xA9(s), " + std::to_string(rep->compiled) + " compil\xC3\xA9(s), "
                       + std::to_string(rep->reused) + " r\xC3\xA9utilis\xC3\xA9(s) depuis le build pr\xC3\xA9" "c\xC3\xA9" "dent";
            }
            const gfx::Rect fr{x, y, w, detail.empty() ? 30.f : 46.f};
            ctx.r.fillRoundedRect(fr, gfx::Color{col.r, col.g, col.b, 36}, 5.f);
            ctx.r.drawText({x + 12.f, y + 6.f}, fin, ctx.theme.font.uiBold, col);
            if (!detail.empty()) ctx.r.drawText({x + 12.f, y + 26.f}, detail, kSmall, c.textMuted);
        } else {
            ctx.r.drawText({x, y + 6.f}, "L'application reste utilisable : le build tourne \xC3\xA0 c\xC3\xB4t\xC3\xA9, sur une copie du projet.", kSmall, c.textMuted);
        }
    }

private:
    [[nodiscard]] float phasesHeight() const {
        const int rows = static_cast<int>(pl::Phase::Count);
        return 12.f + static_cast<float>(rows) * 21.f + static_cast<float>((pl::kApiSteps + 1) / 2 + (pl::kIhmSteps + 1) / 2) * 18.f + 8.f;
    }
    [[nodiscard]] float height() const { return 36.f + 12.f + 26.f + 20.f + 22.f * 2.f + 28.f + phasesHeight() + 12.f + 46.f + 16.f + 50.f; }
    HmiBuildProgressDialog& d_;
    gfx::Rect panel_{};
};

HmiBuildProgressDialog::HmiBuildProgressDialog(std::shared_ptr<HmiBuildManager> build, std::string title, bool startsSimulation)
    : menu::WidgetMenu("dialog.hmiBuildProgress"), build_(std::move(build)), title_(std::move(title)), startsSimulation_(startsSimulation) {
    if (build_) {
        progress_ = build_->progress();
        before_ = build_->lastReport();
        elapsed_ = build_->elapsed();
    }
}

menu::MenuTraits HmiBuildProgressDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;   // l'application continue (et fait avancer le build)
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status HmiBuildProgressDialog::buildUi() {
    auto body = std::make_unique<HmiBuildProgressBody>(*this);
    outputs_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Voir les sorties", "hmiBuildProgress.sorties")));
    outputs_->setTooltip("L'onglet IHM \xC2\xB7 Sorties : le journal du build et ses diagnostics (double-clic : la source)");
    bg_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Continuer en arri\xC3\xA8re-plan", "hmiBuildProgress.arrierePlan")));
    bg_->setTooltip("La fen\xC3\xAAtre se ferme ; la barre d'\xC3\xA9tat suit le build, les sorties le disent \xC3\xA0 la fin");
    cancel_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", "hmiBuildProgress.annuler")));
    cancel_->setTooltip("Le build s'arr\xC3\xAAte entre deux t\xC3\xA2" "ches : aucun artefact n'est coup\xC3\xA9 en deux, les pr\xC3\xA9" "c\xC3\xA9" "dents restent");
    links_ += outputs_->clicked->connect([this] { finish("outputs"); });
    links_ += bg_->clicked->connect([this] { background(); });
    links_ += cancel_->clicked->connect([this] {
        if (done_) finish("close");
        else cancelBuild();
    });
    setRoot(std::move(body));
    refreshButtons();
    return core::ok();
}

void HmiBuildProgressDialog::refreshButtons() {
    if (!bg_ || !cancel_ || !outputs_) return;
    bg_->setVisibility(done_ ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    outputs_->setVisibility(done_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    cancel_->setText(done_ ? "Fermer" : "Annuler");
    cancel_->setEnabled(done_ || !(build_ && build_->cancelling()));   // annule : grise jusqu'a la fin, puis Fermer
    root().invalidateLayout();
}

void HmiBuildProgressDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    now_ = f.totalSeconds;
    if (!build_ || closed_) return;
    progress_ = build_->progress();
    elapsed_ = build_->elapsed();
    root().invalidate();
    const auto rep = build_->lastReport();
    if (!done_ && !build_->building() && rep && rep != before_) {
        done_ = true;
        report_ = rep;
        progress_ = build_->progress();
        doneAt_ = now_;
        refreshButtons();
    }
    // Reussi (un Demarrer) : une seconde pour lire l'etat final, puis la simulation.
    if (done_ && report_ && report_->ok && startsSimulation_ && now_ - doneAt_ > 1.0) finish("close");
}

ui::EventResult HmiBuildProgressDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        if (done_) finish("close");
        else background();   // Echap : rien ne se perd, le build continue
        return ui::EventResult::Consumed;
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HmiBuildProgressDialog::background() {
    if (done_) {
        finish("close");
        return;
    }
    finish("background");
}

void HmiBuildProgressDialog::cancelBuild() {
    if (build_) build_->cancel();
    refreshButtons();
    // la fenetre reste : elle montre la fin (annule) ; Fermer apres
}

void HmiBuildProgressDialog::finish(const std::string& payload) {
    if (closed_) return;
    closed_ = true;
    manager().CloseDialog(menu::DialogResult{payload == "close" ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}

// ======================================================== les sorties ========
HmiBuildOutputPane::HmiBuildOutputPane(std::string id) : ui::Widget(std::move(id)) {
    auto tools = std::make_unique<HmiToolStrip>(this->id() + ".tools");
    tools->add(10, HmiGlyph::Check, "Les informations (le d\xC3\xA9roulement du build)", "Informations");
    tools->add(11, HmiGlyph::Check, "Les succ\xC3\xA8s (build termin\xC3\xA9, projet \xC3\xA0 jour)", "Succ\xC3\xA8s");
    tools->add(12, HmiGlyph::Bell, "Les avertissements", "Avertissements");
    tools->add(13, HmiGlyph::Stop, "Les erreurs (et les erreurs critiques)", "Erreurs");
    tools->separator();
    tools->add(1, HmiGlyph::Delete, "Effacer les sorties (les diagnostics du dernier build restent)", "Effacer");
    tools->add(2, HmiGlyph::Copy, "Copier les lignes montr\xC3\xA9" "es (et les diagnostics) dans le presse-papiers", "Copier");
    for (int k = 0; k < 4; ++k) tools->setCheckedWhen(10 + k, [this, k] { return levels_[k == 3 ? 3 : k]; });
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    auto search = std::make_unique<ui::InputText>(this->id() + ".search");
    search->setPlaceholder("Rechercher dans les sorties et les diagnostics\xE2\x80\xA6");
    search_ = &static_cast<ui::InputText&>(addChild(std::move(search)));
    auto tabs = std::make_unique<ui::TabControl>(this->id() + ".tabs");
    auto out = std::make_unique<ui::TableView>(this->id() + ".sorties");
    out->setColumns({{"Heure", 90.f}, {"Niveau", 150.f}, {"Cat\xC3\xA9gorie", 130.f}, {"Message", 900.f}});
    out->setSelectionMode(ui::SelectionMode::Single);
    out_ = out.get();
    auto diag = std::make_unique<ui::TableView>(this->id() + ".diagnostics");
    diag->setColumns({{"Code", 80.f}, {"Gravit\xC3\xA9", 140.f}, {"Message", 520.f}, {"\xC3\x89l\xC3\xA9ment", 320.f}, {"Fichier", 200.f},
                      {"Ligne", 60.f}, {"Col.", 50.f}, {"\xC3\x89tape", 110.f}, {"Suggestion", 300.f}});
    diag->setSelectionMode(ui::SelectionMode::Single);
    diagTable_ = diag.get();
    tabs->addTab({"Sorties", ui::Icon::Document, false, false}, std::move(out));
    tabs->addTab({"Diagnostics", ui::Icon::Warning, false, false}, std::move(diag));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(this->id() + ".status")));
    links_ += tools_->triggered->connect([this](int a) {
        if (a >= 10 && a <= 13) {
            const int k = a - 10;
            levels_[k] = !levels_[k];
            if (k == 3) levels_[4] = levels_[3];   // Erreurs : avec les critiques
            rebuild();
            return;
        }
        if (a == 1) clear();
        if (a == 2) {
            ui::setClipboardText(copyText());
            status_->setTransientMessage("Sorties copi\xC3\xA9" "es dans le presse-papiers.", 4.0, ui::StatusBar::Severity::Success);
        }
    });
    links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
    links_ += out_->activated->connect([this](ui::RowIndex r) {
        if (r >= outRows_.size()) return;
        const auto& l = lines_[outRows_[r]];
        if (!l.element.empty()) elementActivated->emit(l.element);
    });
    links_ += diagTable_->activated->connect([this](ui::RowIndex r) {
        if (r < diagRows_.size()) diagnosticActivated->emit(diags_[diagRows_[r]]);
    });
    summary_ = "Aucun build depuis l'ouverture : D\xC3\xA9marrer (ou G\xC3\xA9n\xC3\xA9rer dans l'arbre) en lance un.";
    rebuild();
}

void HmiBuildOutputPane::onLayout() {
    const auto b = bounds();
    const float searchW = std::min(360.f, std::max(160.f, b.w * 0.3f));
    tools_->setBounds({b.x, b.y, b.w - searchW - 8.f, 38});
    search_->setBounds({b.x + b.w - searchW - 4.f, b.y + 5.f, searchW, 28.f});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    tabs_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

bool HmiBuildOutputPane::shown(const Line& l) const {
    if (!l.rule && !levels_[severityIndex(l.severity)]) return false;
    const std::string& q = search_ ? search_->text() : std::string{};
    if (q.empty()) return true;
    const auto lower = [](std::string s) { for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; };
    const std::string lq = lower(q);
    return lower(l.message).find(lq) != std::string::npos || lower(l.category).find(lq) != std::string::npos || lower(l.element).find(lq) != std::string::npos;
}

void HmiBuildOutputPane::rebuild() {
    std::vector<std::vector<std::string>> rows;
    std::vector<pl::Severity> sev;
    std::vector<bool> rules;
    outRows_.clear();
    int warns = 0, errs = 0;
    for (std::size_t k = 0; k < lines_.size(); ++k) {
        const auto& l = lines_[k];
        if (l.severity == pl::Severity::Warning) ++warns;
        if (l.severity == pl::Severity::Error || l.severity == pl::Severity::Critical) ++errs;
        if (!shown(l)) continue;
        rows.push_back({l.time, l.rule ? std::string{} : std::string(pl::severityLabel(l.severity)), l.category, l.message});
        sev.push_back(l.severity);
        rules.push_back(l.rule);
        outRows_.push_back(k);
    }
    outModel_ = std::make_shared<Rows>(std::vector<std::string>{"Heure", "Niveau", "Cat\xC3\xA9gorie", "Message"}, std::move(rows), std::move(sev),
                                      std::move(rules), 3);
    out_->setModel(outModel_);
    std::vector<std::vector<std::string>> drows;
    std::vector<pl::Severity> dsev;
    diagRows_.clear();
    int blocking = 0;
    for (std::size_t k = 0; k < diags_.size(); ++k) {
        const auto& d = diags_[k];
        if (d.blocking()) ++blocking;
        Line probe;
        probe.severity = d.severity;
        probe.category = d.category;
        probe.message = d.message;
        probe.element = d.path;
        if (!shown(probe)) continue;
        drows.push_back({d.code, std::string(pl::severityLabel(d.severity)), d.message, d.path, d.file, d.line ? std::to_string(d.line) : std::string{},
                         d.column ? std::to_string(d.column) : std::string{}, d.step, d.suggestion});
        dsev.push_back(d.severity);
        diagRows_.push_back(k);
    }
    diagModel_ = std::make_shared<Rows>(std::vector<std::string>{"Code", "Gravit\xC3\xA9", "Message", "\xC3\x89l\xC3\xA9ment", "Fichier", "Ligne", "Col.", "\xC3\x89tape", "Suggestion"},
                                       std::move(drows), std::move(dsev), std::vector<bool>{}, 2);
    diagTable_->setModel(diagModel_);
    tabs_->setTabBadge(0, lines_.empty() ? std::string{} : std::to_string(lines_.size()), errs ? ui::Tone::Error : warns ? ui::Tone::Warning : ui::Tone::None);
    tabs_->setTabBadge(1, diags_.empty() ? std::string{} : std::to_string(diags_.size()), blocking ? ui::Tone::Error : ui::Tone::Warning);
    status_->setMessage(summary_, blocking ? ui::StatusBar::Severity::Error : ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiBuildOutputPane::addReport(const pl::Report& report, const pl::Request& request, double secs) {
    const std::string t = clockNow();
    Line head;
    head.time = t;
    head.rule = true;
    head.category = "Build";
    head.message = std::string(pl::modeLabel(request.mode)) + (request.scope.empty() ? std::string(" \xE2\x80\x94 tout le projet") : " \xE2\x80\x94 " + request.scope.front()
                   + (request.scope.size() > 1 ? " (+" + std::to_string(request.scope.size() - 1) + ")" : std::string{}));
    lines_.push_back(head);
    for (const auto& l : report.log) {
        Line x;
        x.time = t;
        x.severity = l.severity;
        x.category = l.category;
        x.message = l.message;
        x.element = l.element;
        lines_.push_back(std::move(x));
    }
    constexpr std::size_t kKeep = 2000;
    if (lines_.size() > kKeep) lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kKeep));
    diags_ = report.diagnostics;
    std::stable_sort(diags_.begin(), diags_.end(), [](const pl::Diagnostic& a, const pl::Diagnostic& b) { return a.blocking() && !b.blocking(); });
    for (std::size_t k = 0; k < diags_.size(); ++k) diags_[k].id = "D" + std::to_string(k + 1);
    summary_ = report.locked ? std::string("Build refus\xC3\xA9 : un autre build tourne sur ce projet.")
             : report.cancelled ? "Build annul\xC3\xA9 (" + seconds(secs) + ")."
             : report.upToDate ? "Projet \xC3\xA0 jour (" + seconds(secs) + ")."
                               : std::string(report.ok ? "Build r\xC3\xA9ussi : " : "Build \xC3\xA9" "chou\xC3\xA9 : ") + plural(report.errors, "erreur", "erreurs") + ", "
                                     + plural(report.warnings, "avertissement", "avertissements") + " (" + seconds(secs) + ") \xE2\x80\x94 double-clic sur un diagnostic : sa source.";
    rebuild();
}

void HmiBuildOutputPane::say(pl::Severity severity, std::string category, std::string message) {
    Line x;
    x.time = clockNow();
    x.severity = severity;
    x.category = std::move(category);
    x.message = std::move(message);
    lines_.push_back(std::move(x));
    rebuild();
}

void HmiBuildOutputPane::clear() {
    lines_.clear();
    rebuild();
}

std::string HmiBuildOutputPane::copyText() const {
    std::string out;
    for (const auto k : outRows_) {
        const auto& l = lines_[k];
        out += l.time + "\t" + (l.rule ? std::string("----") : std::string(pl::severityLabel(l.severity))) + "\t" + l.category + "\t" + l.message + "\n";
    }
    if (!diags_.empty()) {
        out += "\nDiagnostics\n";
        for (const auto k : diagRows_) {
            const auto& d = diags_[k];
            out += d.code + "\t" + std::string(pl::severityLabel(d.severity)) + "\t" + d.message + "\t" + d.path
                 + (d.line ? "\tligne " + std::to_string(d.line) + (d.column ? ", colonne " + std::to_string(d.column) : std::string{}) : std::string{}) + "\n";
        }
    }
    return out;
}

void HmiBuildOutputPane::showTab(std::size_t tab) {
    if (tabs_ && tab < tabs_->tabCount()) tabs_->setCurrentIndex(tab);
}

} // namespace app
