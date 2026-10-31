// app/hmi/HmiScanDialog.cpp - 1.21.0 : confirmer une decouverte reseau.
#include "HmiScanDialog.hpp"
#include "../../ui/DialogFrame.hpp"   // la fenetre se deplace et s'agrandit

#include "../../hmi/HmiEquipment.hpp"
#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace app {

namespace disc = hmi::discovery;

namespace {

const gfx::FontId kSmall{13};
constexpr float   kRowH = 28.f;
constexpr float   kLabelW = 96.f;
constexpr char    kSignatureKey[] = "empreinte=";

std::string rateText(double rate) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", rate);
    return b;
}

} // namespace

// ------------------------------------------------------------------ Body ---
class HmiScanDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiScanDialog& d) : d_(d) {}
    std::vector<ui::Button*> buttons;
    gfx::Rect panel{};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(780.f, r.w - 40.f), h = std::min(700.f, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        const float left = panel.x + 16.f, inner = panel.w - 32.f;
        float y = panel.y + 44.f;
        introY_ = y;
        y += d_.spec_.note.empty() ? 40.f : 58.f;
        rowY_[0] = y;
        if (d_.interfaceBox_) d_.interfaceBox_->setBounds({left + kLabelW, y, inner - kLabelW, kRowH});
        y += kRowH + 8.f;
        rowY_[1] = y;
        // Debut | Fin | Masque : trois champs, la place de leur libelle entre eux.
        constexpr float gap = 80.f;
        const float fw = std::max(110.f, (inner - kLabelW - 2.f * gap) / 3.f);
        if (d_.startField_) d_.startField_->setBounds({left + kLabelW, y, fw, kRowH});
        if (d_.endField_) d_.endField_->setBounds({left + kLabelW + fw + gap, y, fw, kRowH});
        if (d_.maskField_) d_.maskField_->setBounds({left + kLabelW + 2.f * (fw + gap), y, std::max(80.f, std::min(fw, inner - kLabelW - 2.f * (fw + gap))), kRowH});
        endX_ = left + kLabelW + fw;
        maskX_ = left + kLabelW + 2.f * fw + gap;
        y += kRowH + 10.f;
        rowY_[2] = y;
        y += 22.f;
        const float colW = inner / 3.f;
        for (std::size_t i = 0; i < d_.protocolBoxes_.size(); ++i) {
            const float cx = left + colW * static_cast<float>(i % 3);
            const float cy = y + 26.f * static_cast<float>(i / 3);
            d_.protocolBoxes_[i]->setBounds({cx, cy, colW - 8.f, 24.f});
        }
        y += 26.f * static_cast<float>((d_.protocolBoxes_.size() + 2) / 3) + 8.f;
        rowY_[3] = y;
        if (d_.extraField_) d_.extraField_->setBounds({left + kLabelW, y, 180.f, kRowH});
        rateX_ = left + kLabelW + 196.f;
        if (d_.rateField_) d_.rateField_->setBounds({rateX_ + 70.f, y, 70.f, kRowH});
        y += kRowH + 8.f;
        rowY_[4] = y;
        if (d_.namesBox_) d_.namesBox_->setBounds({left, y, inner * 0.4f, 24.f});
        if (d_.modbusBox_) d_.modbusBox_->setBounds({left + inner * 0.4f, y, inner * 0.6f, 24.f});
        y += 34.f;
        summaryY_ = y;
        float bx = panel.x + panel.w - 16.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(110.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.y + panel.h - 44.f, bw, 30.f});
            bx -= 10.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const float left = panel.x + 16.f;
        ctx.r.pushClip({panel.x + 8.f, panel.y + th, panel.w - 16.f, panel.h - th});
        // Ce que fait le scan, et ce qu'il ne fait jamais.
        float y = introY_;
        ctx.r.drawText({left, y}, "Observation seulement : ping, connexions TCP referm\xC3\xA9" "es aussit\xC3\xB4t, lecture de l'identification Modbus (fonction 43).",
                       kSmall, c.text);
        y += 18.f;
        ctx.r.drawText({left, y}, "Aucune \xC3\xA9" "criture, aucune commande, aucun red\xC3\xA9marrage. Rien ne part avant Lancer ; Arr\xC3\xAAter coupe tout.", kSmall,
                       c.textMuted);
        if (!d_.spec_.note.empty()) {
            y += 18.f;
            ctx.r.drawText({left, y}, d_.spec_.note, kSmall, ctx.theme.color.accent);
        }
        const auto label = [&](float ly, const char* text) { ctx.r.drawText({left, ly + 6.f}, text, ctx.theme.font.ui, c.text); };
        label(rowY_[0], "Interface");
        label(rowY_[1], "D\xC3\xA9" "but");
        ctx.r.drawText({endX_ + 12.f, rowY_[1] + 6.f}, "Fin", ctx.theme.font.ui, c.text);
        ctx.r.drawText({maskX_ + 12.f, rowY_[1] + 6.f}, "Masque", ctx.theme.font.ui, c.text);
        ctx.r.drawText({left, rowY_[2]}, "Protocoles (une connexion TCP ; seul Modbus est interrog\xC3\xA9, en lecture)", ctx.theme.font.uiBold, c.textMuted);
        label(rowY_[3], "Ports en plus");
        ctx.r.drawText({rateX_, rowY_[3] + 6.f}, "Cadence", ctx.theme.font.ui, c.text);
        ctx.r.drawText({rateX_ + 148.f, rowY_[3] + 7.f}, "interrogations par seconde au plus (1 \xC3\xA0 100)", kSmall, c.textMuted);
        // Le resume : ce qui partira.
        const float bottom = panel.y + panel.h - 56.f;
        const gfx::Rect box{panel.x + 12.f, summaryY_, panel.w - 24.f, std::max(40.f, bottom - summaryY_ - 6.f)};
        ctx.r.fillRect(box, c.headerBg);
        ctx.r.strokeRect(box, c.borderStrong, 1.f);
        float sy = box.y + 8.f;
        const float lh = 19.f;
        if (d_.plan_.ok()) {
            for (const auto& [k, v] : disc::summary(d_.plan_)) {
                if (sy + lh > box.y + box.h) break;
                ctx.r.drawText({box.x + 10.f, sy}, k, kSmall, c.textMuted);
                ctx.r.drawText({box.x + 130.f, sy}, v, kSmall, c.text);
                sy += lh;
            }
            for (const auto& n : d_.plan_.notes) {
                if (sy + lh > box.y + box.h) break;
                ctx.r.drawText({box.x + 10.f, sy}, "\xE2\x9A\xA0 " + n, kSmall, ctx.theme.color.warning);
                sy += lh;
            }
        } else {
            ctx.r.drawText({box.x + 10.f, sy}, "Refus\xC3\xA9 : " + d_.plan_.error, ctx.theme.font.uiBold, ctx.theme.color.error);
            sy += lh + 4.f;
            ctx.r.drawText({box.x + 10.f, sy}, "Rien ne partira tant que la demande est refus\xC3\xA9" "e.", kSmall, c.textMuted);
        }
        ctx.r.drawText({left, panel.y + panel.h - 36.f}, "Le r\xC3\xA9sultat se compare au projet ; rien n'y est ajout\xC3\xA9 sans votre accord (Ctrl+Z).",
                       kSmall, c.textMuted);
        ctx.r.popClip();
    }

private:
    HmiScanDialog& d_;
    float introY_{0}, summaryY_{0}, endX_{0}, maskX_{0}, rateX_{0};
    float rowY_[5]{};
};

// ======================================================================= ======
HmiScanDialog::HmiScanDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    request_ = spec_.request;
    interfaces_ = disc::interfaces(spec_.adapters);
    rateText_ = rateText(request_.rate);
    // Une demande sans plage : le reseau de l'interface (celle proposee d'office sinon).
    if (request_.start.empty() && request_.end.empty()) {
        std::string name = request_.adapter.empty() ? disc::defaultInterface(spec_.adapters) : request_.adapter;
        if (!name.empty()) (void)disc::useInterface(request_, spec_.adapters, name);
    }
    plan_ = disc::plan(request_, spec_.adapters);
}

menu::MenuTraits HmiScanDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

void HmiScanDialog::recompute() {
    plan_ = disc::plan(request_, spec_.adapters);
    if (ok_) {
        const std::string label = confirmLabel();
        if (label != ok_->text()) {
            ok_->setText(label);
            if (body_) body_->invalidateLayout();
        }
        ok_->setEnabled(canConfirm());
    }
    if (body_) body_->invalidate();
}

void HmiScanDialog::refresh() {
    syncing_ = true;
    if (startField_ && startField_->text() != request_.start) startField_->setText(request_.start);
    if (endField_ && endField_->text() != request_.end) endField_->setText(request_.end);
    if (maskField_ && maskField_->text() != request_.mask) maskField_->setText(request_.mask);
    if (extraField_ && extraField_->text() != request_.extraPorts) extraField_->setText(request_.extraPorts);
    if (rateField_ && rateField_->text() != rateText_) rateField_->setText(rateText_);
    if (interfaceBox_) {
        int at = static_cast<int>(interfaces_.size());   // la derniere ligne : aucune
        for (std::size_t i = 0; i < interfaces_.size(); ++i)
            if (interfaces_[i].name == request_.adapter) at = static_cast<int>(i);
        if (interfaceBox_->selectedIndex() != at) interfaceBox_->setSelectedIndex(at);
    }
    const auto& list = disc::protocols();
    for (std::size_t i = 0; i < protocolBoxes_.size() && i < list.size(); ++i) {
        const bool on = std::find(request_.protocols.begin(), request_.protocols.end(), list[i].key) != request_.protocols.end();
        protocolBoxes_[i]->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    }
    if (namesBox_) namesBox_->setState(request_.names ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    if (modbusBox_) modbusBox_->setState(request_.modbusId ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    syncing_ = false;
    recompute();
}

bool HmiScanDialog::setInterface(const std::string& name, std::string* why) {
    if (name.empty()) {
        request_.adapter.clear();
        refresh();
        return true;
    }
    if (!disc::useInterface(request_, spec_.adapters, name, why)) {
        recompute();
        return false;
    }
    refresh();
    return true;
}

void HmiScanDialog::setStart(std::string text) {
    request_.start = std::move(text);
    recompute();
}

void HmiScanDialog::setEnd(std::string text) {
    request_.end = std::move(text);
    recompute();
}

void HmiScanDialog::setMask(std::string text) {
    request_.mask = std::move(text);
    recompute();
}

bool HmiScanDialog::applyMask(std::string* why) {
    if (!disc::applyMask(request_, why)) {
        recompute();
        return false;
    }
    refresh();
    return true;
}

void HmiScanDialog::setProtocol(const std::string& key, bool on) {
    auto& list = request_.protocols;
    const auto it = std::find(list.begin(), list.end(), key);
    if (on && it == list.end()) {
        // Dans l'ordre du tableau des protocoles : le resume et les ports restent stables.
        std::vector<std::string> next;
        for (const auto& p : disc::protocols())
            if (p.key == key || std::find(list.begin(), list.end(), p.key) != list.end()) next.push_back(p.key);
        list = std::move(next);
    } else if (!on && it != list.end()) {
        list.erase(it);
    }
    if (!syncing_) refresh();
}

void HmiScanDialog::setExtraPorts(std::string text) {
    request_.extraPorts = std::move(text);
    recompute();
}

void HmiScanDialog::setRate(std::string text) {
    rateText_ = std::move(text);
    char* end = nullptr;
    const double v = std::strtod(rateText_.c_str(), &end);
    request_.rate = (rateText_.empty() || (end && *end && *end != ' ')) ? 0.0 : v;   // illisible : 0, le plan refuse
    recompute();
}

void HmiScanDialog::setNames(bool on) {
    request_.names = on;
    recompute();
}

void HmiScanDialog::setModbusId(bool on) {
    request_.modbusId = on;
    recompute();
}

std::string HmiScanDialog::confirmLabel() const {
    if (!plan_.ok()) return "Lancer le scan";
    return "Lancer le scan (" + std::to_string(plan_.addresses) + (plan_.addresses > 1 ? " adresses)" : " adresse)");
}

bool HmiScanDialog::confirm() {
    if (!canConfirm()) return false;
    finish(true);
    return true;
}

void HmiScanDialog::cancel() { finish(false); }

std::string HmiScanDialog::encode(const disc::Request& request, const disc::Plan& plan) {
    return disc::encodeRequest(request) + kSignatureKey + hmi::ipscan::signature(plan.options) + "\n";
}

bool HmiScanDialog::parse(const std::string& payload, disc::Request& request, std::string& signature) {
    signature.clear();
    const std::string key = kSignatureKey;
    if (const auto at = payload.find("\n" + key); at != std::string::npos) {
        const auto from = at + 1 + key.size();
        const auto to = payload.find('\n', from);
        signature = payload.substr(from, to == std::string::npos ? std::string::npos : to - from);
    }
    return disc::decodeRequest(payload, request);
}

void HmiScanDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? encode(request_, plan_) : std::string{}});
}

core::Status HmiScanDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    {
        auto box = std::make_unique<ui::DropDown>(base + ".interface");
        std::vector<ui::DropDown::Item> items;
        for (const auto& i : interfaces_) items.push_back({i.label, i.name, {}, i.ip != 0});
        items.push_back({interfaces_.empty() ? std::string("Aucune interface trouv\xC3\xA9" "e : une plage libre") : std::string("Aucune (une plage libre)"), {}, {}, true});
        box->setItems(std::move(items));
        interfaceBox_ = &static_cast<ui::DropDown&>(body->addChild(std::move(box)));
        links_ += interfaceBox_->selectionChanged->connect([this](int i) {
            if (syncing_ || i < 0) return;
            (void)setInterface(i < static_cast<int>(interfaces_.size()) ? interfaces_[static_cast<std::size_t>(i)].name : std::string{});
        });
    }
    const auto field = [&](const char* id, const char* placeholder) {
        auto f = std::make_unique<ui::InputText>(base + id);
        f->setPlaceholder(placeholder);
        return &static_cast<ui::InputText&>(body->addChild(std::move(f)));
    };
    startField_ = field(".debut", "192.168.1.1");
    endField_ = field(".fin", "192.168.1.254");
    maskField_ = field(".masque", "24 ou 255.255.255.0");
    links_ += startField_->textChanged->connect([this](const std::string& t) {
        if (!syncing_) setStart(t);
    });
    links_ += endField_->textChanged->connect([this](const std::string& t) {
        if (!syncing_) setEnd(t);
    });
    links_ += maskField_->textChanged->connect([this](const std::string& t) {
        if (!syncing_) setMask(t);
    });
    // Entree (ou la sortie du champ) : le masque fait de la plage tout le reseau du debut.
    links_ += maskField_->editingDone->connect([this](const std::string&) {
        if (!syncing_ && !request_.mask.empty()) (void)applyMask();
    });
    for (const auto& p : disc::protocols()) {
        auto cb = std::make_unique<ui::Checkbox>(disc::protocolText(p), base + ".protocole." + p.key);
        auto* raw = &static_cast<ui::Checkbox&>(body->addChild(std::move(cb)));
        raw->setTooltip(p.detail);
        protocolBoxes_.push_back(raw);
        const std::string key = p.key;
        links_ += raw->stateChanged->connect([this, key](ui::Checkbox::State s) {
            if (!syncing_) setProtocol(key, s == ui::Checkbox::State::Checked);
        });
    }
    extraField_ = field(".ports", "8080, 5020");
    links_ += extraField_->textChanged->connect([this](const std::string& t) {
        if (!syncing_) setExtraPorts(t);
    });
    rateField_ = field(".cadence", "20");
    links_ += rateField_->textChanged->connect([this](const std::string& t) {
        if (!syncing_) setRate(t);
    });
    namesBox_ = &static_cast<ui::Checkbox&>(body->addChild(std::make_unique<ui::Checkbox>("Noms (DNS inverse)", base + ".noms")));
    links_ += namesBox_->stateChanged->connect([this](ui::Checkbox::State s) {
        if (!syncing_) setNames(s == ui::Checkbox::State::Checked);
    });
    modbusBox_ = &static_cast<ui::Checkbox&>(body->addChild(std::make_unique<ui::Checkbox>(
        "Identification Modbus (lecture seule, fonction 43) des ports 502 ouverts", base + ".modbus")));
    links_ += modbusBox_->stateChanged->connect([this](ui::Checkbox::State s) {
        if (!syncing_) setModbusId(s == ui::Checkbox::State::Checked);
    });
    auto* cancelButton = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    body->buttons.push_back(cancelButton);
    links_ += cancelButton->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(confirmLabel(), base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->buttons.push_back(ok_);
    links_ += ok_->clicked->connect([this] { (void)confirm(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

ui::EventResult HmiScanDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Ctrl+Entree : Lancer (jamais Entree seule : elle valide un champ).
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            (void)confirm();
            return ui::EventResult::Consumed;
        }
    }
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        finish(false);
        return ui::EventResult::Consumed;
    }
    return r;
}

} // namespace app
