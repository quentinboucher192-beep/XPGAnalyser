// =============================================================================
//  app/ai/AiKeysDialog.cpp - voir AiKeysDialog.hpp
// =============================================================================
#include "AiKeysDialog.hpp"

#include "AiMemory.hpp"
#include "AiPanel.hpp"
#include "AiSecret.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>

namespace app::ai {

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kText{14};
const gfx::FontId kSmall{13};
constexpr float kRow = 32.f;

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto push = [&] {
        if (word.empty()) return;
        const std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(t, f) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = t;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') push();
        else word += c;
    }
    push();
    if (!line.empty()) out.push_back(line);
    return out;
}

const char* const kProviders[][2] = {{"Anthropic", "anthropic"}, {"Compatible OpenAI", "openai"}};

} // namespace

// ------------------------------------------------------------------ la liste ---
class AiKeysDialog::Model final : public ui::ITableModel {
public:
    std::vector<keys::Entry> rows;
    std::string              anthropic, openai;   // les actives

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 6; }
    [[nodiscard]] std::string headerText(std::size_t col) const override {
        static const char* const h[] = {"Nom", "Fournisseur", "Cl\xC3\xA9", "Ajout\xC3\xA9" "e", "Dernier essai", "Utilis\xC3\xA9" "e par"};
        return col < 6 ? h[col] : std::string{};
    }
    [[nodiscard]] bool active(const keys::Entry& e) const { return e.id == (e.provider == "anthropic" ? anthropic : openai); }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t col) const override {
        if (row >= rows.size()) return {};
        const auto& e = rows[row];
        switch (col) {
            case 0: return (active(e) ? "\xE2\x98\x85 " : "") + e.name;
            case 1: return keys::providerLabel(e.provider);
            case 2: return keys::masked(e);
            case 3: return memory::displayDate(e.added);
            case 4:
                if (e.tested == 0) return "jamais essay\xC3\xA9" "e";
                return std::string(e.tested > 0 ? "\xE2\x9C\x93 accept\xC3\xA9" "e" : "\xE2\x9C\x97 refus\xC3\xA9" "e")
                       + (e.testNote.empty() ? std::string{} : " (" + e.testNote + ")") + " \xC2\xB7 " + memory::displayDate(e.testedAt);
            case 5:
                return active(e) ? std::string("l'Assistant (active)")
                                 : e.stored ? std::string("\xE2\x80\x93") : std::string("\xE2\x80\x93 (pour cette s\xC3\xA9" "ance)");
            default: return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t col) const override {
        ui::CellStyle s;
        if (row >= rows.size()) return s;
        const auto& e = rows[row];
        if (col == 0 && active(e)) s.bold = true;
        if (col == 2) s.monospace = true;
        if (col == 4) s.fgTone = e.tested > 0 ? ui::Tone::Ok : e.tested < 0 ? ui::Tone::Error : ui::Tone::Muted;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t col) const override { return cellText(a, col) < cellText(b, col); }
    [[nodiscard]] int find(std::string_view id) const {
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id) return static_cast<int>(i);
        return -1;
    }
    void reset() { modelReset->emit(); }
};

// ------------------------------------------------------------------ le corps ---
class AiKeysDialog::Body final : public ui::Widget {
public:
    explicit Body(AiKeysDialog& d) : ui::Widget("ai.cles.corps"), d_(d) {}

protected:
    struct Label { gfx::Point at; std::string text; bool heading; float w; };

    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1040.f, std::max(600.f, r.w - 40.f));
        const float h = std::min(700.f, std::max(460.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        labels_.clear();
        const float x = panel_.x + 20.f, cw = panel_.w - 40.f;
        float y = panel_.y + 60.f;
        labels_.push_back({{x, y}, "MES CL\xC3\x89S", true, cw});
        y += 24.f;
        // La liste : ce qui reste au-dessus du detail (au moins trois lignes).
        const float detailH = 6.f * kRow + 120.f;
        const float listH = std::max(110.f, panel_.bottom() - 60.f - detailH - kRow - 40.f - y);
        d_.table_->setBounds({x, y, cw, listH});
        y += listH + 8.f;
        // Les gestes
        float bx = x;
        for (ui::Button* b : {d_.add_, d_.use_, d_.test_, d_.replace_, d_.remove_}) {
            const float bw = ui::measureWidth(b->text(), gfx::FontId{16}) + 28.f;
            b->setBounds({bx, y, bw, kRow - 4.f});
            bx += bw + 8.f;
        }
        y += kRow + 6.f;
        labels_.push_back({{x, y}, d_.selected_.empty() ? std::string("UNE CL\xC3\x89 NEUVE") : std::string("LA CL\xC3\x89 CHOISIE"), true, cw});
        y += 24.f;
        const float labelW = 150.f;
        const float fw = std::min(560.f, cw - labelW);
        const auto row = [&](const char* label, ui::Widget* wgt, float width = 0.f) {
            labels_.push_back({{x, y + 6.f}, label, false, labelW - 8.f});
            wgt->setBounds({x + labelW, y, width > 0.f ? width : fw, kRow - 4.f});
            y += kRow;
        };
        row("Nom", d_.name_);
        row("Fournisseur", d_.provider_, 260.f);
        row("Adresse", d_.endpoint_);
        row("Mod\xC3\xA8le pr\xC3\xA9" "f\xC3\xA9r\xC3\xA9", d_.modelName_);
        {
            const float sw = ui::measureWidth("Afficher", gfx::FontId{16}) + 24.f;
            labels_.push_back({{x, y + 6.f}, "Cl\xC3\xA9", false, labelW - 8.f});
            d_.key_->setBounds({x + labelW, y, fw - sw - 6.f, kRow - 4.f});
            d_.show_->setBounds({x + labelW + fw - sw, y, sw, kRow - 4.f});
            y += kRow;
        }
        d_.remember_->setBounds({x, y, cw * 0.55f, kRow - 6.f});
        d_.sessionOnly_->setBounds({x + cw * 0.55f, y, cw * 0.45f, kRow - 6.f});
        y += kRow - 2.f;
        {
            const float sw = ui::measureWidth(d_.save_->text(), gfx::FontId{16}) + 34.f;
            d_.save_->setBounds({x + labelW, y, sw, kRow - 2.f});
            messageAt_ = {x + labelW + sw + 12.f, y + 6.f, cw - labelW - sw - 12.f, kRow};
            y += kRow + 4.f;
        }
        noteAt_ = {x, y, cw, 40.f};
        const float cbw = std::max(110.f, ui::measureWidth(d_.close_->text(), gfx::FontId{16}) + 34.f);
        d_.close_->setBounds({panel_.right() - 20.f - cbw, panel_.bottom() - 46.f, cbw, 30.f});
        footerW_ = panel_.w - cbw - 60.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 20.f;
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {x, panel_.y + 13.f, 20.f, 20.f}, c.accent);
        ctx.r.drawText({x + 28.f, panel_.y + 10.f}, "Cl\xC3\xA9s API", kTitleFont, c.text);
        {
            const float tx = x + 28.f + ctx.r.measure("Cl\xC3\xA9s API", kTitleFont).width + 14.f;
            const std::string sub = keyProtectionAvailable()
                                        ? "Assistant IA \xC2\xB7 gard\xC3\xA9" "es sur ce PC, prot\xC3\xA9g\xC3\xA9" "es par Windows (DPAPI) \xC2\xB7 une r\xC3\xA9installation ne les efface pas"
                                        : "Assistant IA \xC2\xB7 sous Linux, gard\xC3\xA9" "es pour cette s\xC3\xA9" "ance seulement (jamais \xC3\xA9" "crites)";
            ctx.r.drawText({tx, panel_.y + 17.f}, elide(ctx.r, sub, kSmall, panel_.right() - 20.f - tx), kSmall, c.textMuted);
        }
        for (const auto& l : labels_) {
            if (l.heading) {
                ctx.r.drawText(l.at, l.text, kSmall, c.textMuted);
                ctx.r.fillRect({l.at.x, l.at.y + ctx.r.lineHeight(kSmall) + 3.f, l.w, 1.f}, c.border);
            } else {
                ctx.r.drawText(l.at, elide(ctx.r, l.text, kText, l.w), kText, c.text);
            }
        }
        if (!d_.message_.empty())
            ctx.r.drawText({messageAt_.x, messageAt_.y}, elide(ctx.r, d_.message_, kSmall, messageAt_.w), kSmall,
                           d_.testing() ? c.textMuted : d_.messageOk_ ? c.ok : c.error);
        {
            const std::string note = "Un autre PC, une autre session Windows : la cl\xC3\xA9 se ressaisit (elle ne voyage jamais, m\xC3\xAAme chiffr\xC3\xA9" "e). "
                                     "Jamais en clair : ni fichier, ni journal, ni projet.";
            float y = noteAt_.y;
            for (const auto& line : wrap(note, kSmall, noteAt_.w)) {
                ctx.r.drawText({noteAt_.x, y}, line, kSmall, c.textMuted);
                y += ctx.r.lineHeight(kSmall) + 3.f;
            }
        }
        const std::string dir = keys::folder().empty() ? std::string("Dossier : aucun (rien n'est \xC3\xA9" "crit)") : "Dossier : " + keys::folder();
        ctx.r.drawText({x, panel_.bottom() - 38.f}, elide(ctx.r, dir, kSmall, footerW_), kSmall, c.textMuted);
    }

private:
    AiKeysDialog&      d_;
    gfx::Rect          panel_{}, messageAt_{}, noteAt_{};
    std::vector<Label> labels_;
    float              footerW_{0.f};
};

// ------------------------------------------------------------------ la fenetre ---
AiKeysDialog::AiKeysDialog(AiPanel& panel) : menu::WidgetMenu("dialog.ai.cles"), panel_(panel) {}
AiKeysDialog::~AiKeysDialog() = default;

menu::MenuTraits AiKeysDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status AiKeysDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<Body>(*this);
    auto* b = body.get();
    model_ = std::make_shared<Model>();
    {
        auto t = std::make_unique<ui::TableView>(base + ".liste");
        t->setSelectionMode(ui::SelectionMode::Single);
        t->setAlternatingRowColors(true);
        const auto col = [](const char* title, float w) {
            ui::TableView::Column c;
            c.title = title;
            c.width = w;
            return c;
        };
        t->setColumns({col("Nom", 200.f), col("Fournisseur", 150.f), col("Cl\xC3\xA9", 170.f), col("Ajout\xC3\xA9" "e", 130.f),
                       col("Dernier essai", 220.f), col("Utilis\xC3\xA9" "e par", 150.f)});
        t->setModel(model_);
        table_ = &static_cast<ui::TableView&>(b->addChild(std::move(t)));
    }
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_ || rows.empty() || rows.front() >= model_->rows.size()) return;
        (void)select(model_->rows[rows.front()].id);
    });
    const auto button = [&](const char* text, const char* suffix) {
        return &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>(text, base + suffix)));
    };
    const auto input = [&](const char* suffix, const char* placeholder) {
        auto in = std::make_unique<ui::InputText>(base + suffix);
        in->setPlaceholder(placeholder);
        return &static_cast<ui::InputText&>(b->addChild(std::move(in)));
    };
    add_ = button("+ Ajouter une cl\xC3\xA9\xE2\x80\xA6", ".ajouter");
    add_->setStyle(ui::Button::Style::Primary);
    use_ = button("\xE2\x98\x85 Utiliser", ".utiliser");
    use_->setTooltip("La cl\xC3\xA9 choisie devient celle de l'Assistant pour son fournisseur.");
    test_ = button("Tester", ".tester");
    test_->setTooltip("Demande la liste des mod\xC3\xA8les au fournisseur avec cette cl\xC3\xA9 (gratuit, sans consommer de jetons).");
    replace_ = button("Remplacer la cl\xC3\xA9", ".remplacer");
    replace_->setTooltip("La cl\xC3\xA9 saisie remplace celle-ci (m\xC3\xAAme nom, m\xC3\xAAme place).");
    remove_ = button("Supprimer", ".supprimer");
    remove_->setStyle(ui::Button::Style::Danger);
    links_ += add_->clicked->connect([this] { (void)beginNew(); });
    links_ += use_->clicked->connect([this] { (void)use(); });
    links_ += test_->clicked->connect([this] { (void)test(); });
    links_ += replace_->clicked->connect([this] { (void)replaceKey(); });
    links_ += remove_->clicked->connect([this] { askRemove(); });
    name_ = input(".nom", "ex. Anthropic - perso");
    provider_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".fournisseur")));
    {
        std::vector<ui::DropDown::Item> items;
        for (const auto& p : kProviders) items.push_back({p[0], p[1]});
        provider_->setItems(std::move(items));
        const std::string cur = keys::providerOf(panel_.assistant().settings().provider);
        provider_->setSelectedIndex(cur == "openai" ? 1 : 0);
    }
    endpoint_ = input(".adresse", "vide : celle des Param\xC3\xA8tres (ou le relais de l'entreprise)");
    modelName_ = input(".modele", "vide : celui des Param\xC3\xA8tres");
    key_ = input(".cle", "la cl\xC3\xA9 du fournisseur");
    key_->setMasked(true);
    show_ = &static_cast<ui::ToggleButton&>(b->addChild(std::make_unique<ui::ToggleButton>("Afficher", base + ".afficher")));
    links_ += show_->toggled->connect([this](bool on) { key_->setMasked(!on); });
    remember_ = &static_cast<ui::Checkbox&>(b->addChild(std::make_unique<ui::Checkbox>(
        "M\xC3\xA9moriser sur ce PC (prot\xC3\xA9g\xC3\xA9" "e par Windows)", base + ".memoriser")));
    sessionOnly_ = &static_cast<ui::Checkbox&>(b->addChild(std::make_unique<ui::Checkbox>(
        "Pour cette s\xC3\xA9" "ance seulement (rien n'est \xC3\xA9" "crit)", base + ".seance")));
    remember_->setEnabled(keyProtectionAvailable());
    links_ += remember_->stateChanged->connect([this](ui::Checkbox::State st) {
        if (syncing_) return;
        syncing_ = true;
        sessionOnly_->setState(st == ui::Checkbox::State::Checked ? ui::Checkbox::State::Unchecked : ui::Checkbox::State::Checked);
        syncing_ = false;
    });
    links_ += sessionOnly_->stateChanged->connect([this](ui::Checkbox::State st) {
        if (syncing_ || !keyProtectionAvailable()) return;
        syncing_ = true;
        remember_->setState(st == ui::Checkbox::State::Checked ? ui::Checkbox::State::Unchecked : ui::Checkbox::State::Checked);
        syncing_ = false;
    });
    save_ = button("Enregistrer la cl\xC3\xA9", ".enregistrer");
    save_->setStyle(ui::Button::Style::Primary);
    links_ += save_->clicked->connect([this] { (void)saveDetail(); });
    close_ = button("Fermer", ".fermer");
    links_ += close_->clicked->connect([this] { close(); });
    setRoot(std::move(body));
    refresh();
    const std::string active = keys::activeId(keys::providerOf(panel_.assistant().settings().provider));
    if (!active.empty()) (void)select(active);
    else if (!model_->rows.empty()) (void)select(model_->rows.front().id);
    else (void)beginNew();
    return core::ok();
}

void AiKeysDialog::refresh() {
    model_->rows = keys::list();
    model_->anthropic = keys::activeId("anthropic");
    model_->openai = keys::activeId("openai");
    model_->reset();
    syncing_ = true;
    const int r = model_->find(selected_);
    if (r >= 0) table_->selectModelRows({static_cast<ui::RowIndex>(r)});
    syncing_ = false;
}

std::size_t AiKeysDialog::rowCount() const { return model_ ? model_->rows.size() : 0; }

void AiKeysDialog::say(std::string text, bool ok) {
    message_ = std::move(text);
    messageOk_ = ok;
}

void AiKeysDialog::fillDetail() {
    syncing_ = true;
    const auto e = selected_.empty() ? std::nullopt : keys::find(selected_);
    name_->setText(e ? e->name : std::string{});
    provider_->setSelectedIndex(e && e->provider == "openai" ? 1 : e ? 0 : provider_->selectedIndex());
    endpoint_->setText(e ? e->endpoint : std::string{});
    modelName_->setText(e ? e->model : std::string{});
    std::string why;
    const auto secret = e ? keys::secret(e->id, &why) : std::nullopt;
    key_->setText(secret ? *secret : std::string{});
    key_->setMasked(true);
    show_->setChecked(false);
    const bool remember = e ? e->stored : keyProtectionAvailable();   // une cle neuve : memorisee par defaut
    remember_->setState(remember ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    sessionOnly_->setState(remember ? ui::Checkbox::State::Unchecked : ui::Checkbox::State::Checked);
    use_->setEnabled(e.has_value());
    test_->setEnabled(e.has_value());
    replace_->setEnabled(e.has_value());
    remove_->setEnabled(e.has_value());
    save_->setText(e ? "Enregistrer" : "Ajouter la cl\xC3\xA9");
    if (e && !secret && e->stored)
        say("Cette cl\xC3\xA9 ne se relit pas sur ce poste (une autre session Windows, un autre PC) : saisis-la et Remplacer la cl\xC3\xA9.", false);
    syncing_ = false;
    if (auto* body = widgetRoot()) body->layout();
}

bool AiKeysDialog::beginNew() {
    selected_.clear();
    confirmRemove_ = false;
    table_->selectModelRows({});
    say("Une cl\xC3\xA9 neuve : son nom, son fournisseur, la cl\xC3\xA9, puis Ajouter la cl\xC3\xA9.", true);
    fillDetail();
    return true;
}

bool AiKeysDialog::select(const std::string& id) {
    if (!keys::find(id)) return false;
    selected_ = id;
    confirmRemove_ = false;
    message_.clear();
    syncing_ = true;
    const int r = model_->find(id);
    if (r >= 0) table_->selectModelRows({static_cast<ui::RowIndex>(r)});
    syncing_ = false;
    fillDetail();
    return true;
}

bool AiKeysDialog::saveDetail() {
    keys::Entry e;
    if (!selected_.empty())
        if (auto cur = keys::find(selected_)) e = *cur;
    e.name = name_->text();
    e.provider = provider_->selectedItem() ? provider_->selectedItem()->value : std::string("anthropic");
    e.endpoint = endpoint_->text();
    e.model = modelName_->text();
    std::string why;
    const bool remember = remember_->isChecked() && keyProtectionAvailable();
    if (selected_.empty()) {
        const std::string id = keys::add(e, key_->text(), remember, &why);
        if (id.empty()) {
            say(why, false);
            return false;
        }
        selected_ = id;
        say("Cl\xC3\xA9 \xC2\xAB " + e.name + " \xC2\xBB ajout\xC3\xA9" "e" + std::string(remember ? " (m\xC3\xA9moris\xC3\xA9" "e, prot\xC3\xA9g\xC3\xA9" "e par Windows)." : " (pour cette s\xC3\xA9" "ance)."), true);
    } else {
        if (!keys::update(e, &why)) {
            say(why, false);
            return false;
        }
        // Le secret, ou sa memorisation, a change : la cle est remplacee (le meme nom, la meme place).
        const auto old = keys::secret(selected_);
        const auto cur = keys::find(selected_);
        if (!key_->text().empty() && ((old && *old != key_->text()) || !old || (cur && cur->stored != remember))) {
            if (!keys::replace(selected_, key_->text(), remember, &why)) {
                say(why, false);
                return false;
            }
            for (const auto& x : keys::list())
                if (x.name == e.name) selected_ = x.id;
        }
        say("Enregistr\xC3\xA9.", true);
    }
    refresh();
    fillDetail();
    panel_.assistant().reloadKey();
    return true;
}

bool AiKeysDialog::use() {
    if (selected_.empty() || !keys::setActive(selected_)) return false;
    const auto e = keys::find(selected_);
    refresh();
    panel_.assistant().reloadKey();
    say("\xC2\xAB " + (e ? e->name : selected_) + " \xC2\xBB est la cl\xC3\xA9 de l'Assistant pour " + (e ? keys::providerLabel(e->provider) : std::string{}) + ".", true);
    return true;
}

bool AiKeysDialog::replaceKey() {
    if (selected_.empty()) return false;
    const auto e = keys::find(selected_);
    if (!e) return false;
    std::string why;
    if (!keys::replace(selected_, key_->text(), remember_->isChecked() && keyProtectionAvailable(), &why)) {
        say(why, false);
        return false;
    }
    for (const auto& x : keys::list())
        if (x.name == e->name) selected_ = x.id;
    refresh();
    fillDetail();
    panel_.assistant().reloadKey();
    say("Cl\xC3\xA9 \xC2\xAB " + e->name + " \xC2\xBB remplac\xC3\xA9" "e.", true);
    return true;
}

void AiKeysDialog::askRemove() {
    if (selected_.empty()) return;
    const auto e = keys::find(selected_);
    if (!e) return;
    if (!confirmRemove_) {
        confirmRemove_ = true;
        remove_->setText("Confirmer la suppression");
        say("Supprimer \xC2\xAB " + e->name + " \xC2\xBB ? Elle s'efface de ce PC. Clique encore sur Confirmer la suppression.", false);
        if (auto* body = widgetRoot()) body->layout();
        return;
    }
    (void)removeSelected();
}

bool AiKeysDialog::removeSelected() {
    if (selected_.empty()) return false;
    const auto e = keys::find(selected_);
    const bool ok = keys::remove(selected_);
    confirmRemove_ = false;
    remove_->setText("Supprimer");
    selected_.clear();
    refresh();
    panel_.assistant().reloadKey();
    if (!model_->rows.empty()) (void)select(model_->rows.front().id);
    else (void)beginNew();
    say(ok ? "Cl\xC3\xA9 \xC2\xAB " + (e ? e->name : std::string{}) + " \xC2\xBB supprim\xC3\xA9" "e." : std::string("La cl\xC3\xA9 ne se supprime pas."), ok);
    return ok;
}

bool AiKeysDialog::test() {
    if (selected_.empty()) return false;
    const auto e = keys::find(selected_);
    if (!e) return false;
    Settings s = panel_.assistant().settings();
    s.provider = e->provider;
    if (!e->endpoint.empty()) s.endpoint = e->endpoint;
    else if (keys::providerOf(panel_.assistant().settings().provider) != e->provider) s.endpoint.clear();
    std::string why;
    const std::string secret = key_->text();
    if (!panel_.assistant().queryModels(s, secret, &why)) {
        say(why, false);
        return false;
    }
    testing_ = selected_;
    seenModels_ = panel_.assistant().modelsRevision();
    say("Essai de la cl\xC3\xA9\xE2\x80\xA6", true);
    return true;
}

bool AiKeysDialog::testing() const { return !testing_.empty() && panel_.assistant().modelsBusy(); }

bool AiKeysDialog::pollTest() {
    auto& as = panel_.assistant();
    (void)as.pollModels();
    if (testing_.empty() || as.modelsRevision() == seenModels_ || !as.models()) return false;
    seenModels_ = as.modelsRevision();
    const ModelsResult& r = *as.models();
    const std::string id = testing_;
    testing_.clear();
    const std::string note = r.ok ? std::to_string(r.models.size()) + (r.models.size() > 1 ? " mod\xC3\xA8les" : " mod\xC3\xA8le") : r.error;
    keys::noteTest(id, r.ok, r.ok ? note : std::string(note.substr(0, 60)));
    refresh();
    say(r.ok ? "Cl\xC3\xA9 accept\xC3\xA9" "e : " + note + "." : "Cl\xC3\xA9 refus\xC3\xA9" "e : " + note, r.ok);
    return true;
}

void AiKeysDialog::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    (void)pollTest();
}

void AiKeysDialog::close() {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
}

ui::Widget* AiKeysDialog::field(const std::string& name) const {
    if (name == "nom") return name_;
    if (name == "fournisseur") return provider_;
    if (name == "adresse") return endpoint_;
    if (name == "modele") return modelName_;
    if (name == "cle") return key_;
    if (name == "memoriser") return remember_;
    if (name == "seance") return sessionOnly_;
    return nullptr;
}

ui::EventResult AiKeysDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        if (!(provider_ && provider_->isOpen())) {
            close();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
