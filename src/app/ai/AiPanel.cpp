// =============================================================================
//  app/ai/AiPanel.cpp - voir AiPanel.hpp
// =============================================================================
#include "AiPanel.hpp"

#include "AiPreviewDialog.hpp"
#include "AiSettingsDialog.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../ui/FileKinds.hpp"
#include "../../ui/Icons.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace app::ai {

namespace {

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
const gfx::FontId kHead{16};
constexpr float kPad = 10.f;
constexpr float kHeaderH = 38.f;
constexpr float kStatusH = 20.f;
constexpr float kModeH = 34.f;
constexpr float kListHeadH = 26.f;
constexpr float kRowH = 28.f;
constexpr int   kMaxRows = 4;
constexpr float kBottomH = 40.f;
constexpr float kPendingH = 30.f;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const float dw = r.measure(dots, f).width;
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - dw));
    return text.substr(0, n) + dots;
}

// Un texte trace deux fois a un pixel : le gras du logiciel (il n'a pas de face grasse).
void bold(gfx::IRenderer& r, gfx::Point at, std::string_view t, gfx::FontId f, gfx::Color c) {
    r.drawText(at, t, f, c);
    r.drawText({at.x + 0.8f, at.y}, t, f, c);
}

struct FileLook {
    ui::Icon   icon;
    gfx::Color tint;
    std::string type;
};
FileLook lookOf(const ui::Theme& t, const std::string& name) {
    const auto ext = ui::files::extensionOf(name);
    if (ext == "xihm" || ext == "xapi" || ext == "xpgproj")
        return {ui::Icon::Project, ui::files::tintColor(t, ui::files::Tint::Orange),
                ext == "xihm" ? "Projet IHM (.xihm)" : ext == "xapi" ? "Projet API (.xapi)" : "Projet"};
    const auto fam = ui::files::familyOf(name, false);
    return {ui::files::iconOf(fam), ui::files::tintColor(t, ui::files::tintOf(fam)), ui::files::typeLabel(name, false)};
}

// Le cache des mises en page (un texte a une largeur) : la reponse s'ecrit mot a mot,
// chaque image ne refait que le message qui change.
std::unordered_map<std::string, std::vector<AiChatView::Line>>& layoutCache() {
    static std::unordered_map<std::string, std::vector<AiChatView::Line>> cache;
    if (cache.size() > 400) cache.clear();
    return cache;
}

const std::string& welcomeText() {
    static const std::string k = "Je propose des modifications de **la vue ouverte**, avec les vrais objets du logiciel. "
                                 "D\xC3\xA9monstration : je reconnais les demandes ci-dessous.";
    return k;
}

} // namespace

// =============================================================================
//  La saisie
// =============================================================================
AiPromptEdit::AiPromptEdit(std::string id) : ui::MultiLineText(std::move(id)) {
    setShowLineNumbers(false);
    setWordWrap(true);
    setLanguage(ui::Language::PlainText);
    setCommandKeys(false);
    setZoomSupport(ui::ZoomSupport::None);
}

ui::EventResult AiPromptEdit::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && k->key == ui::Key::Return && !k->mods.alt) {
        if (k->mods.shift) {
            insertNewline();          // Maj+Entree : une ligne de plus
            return ui::EventResult::Consumed;
        }
        submitted->emit();            // Entree (ou Ctrl+Entree) : envoyer
        return ui::EventResult::Consumed;
    }
    return ui::MultiLineText::onEvent(ev);
}

// =============================================================================
//  Le panneau
// =============================================================================
AiPanel::AiPanel(std::string id, Assistant::Host core, UiHost uiHost)
    : ui::Widget(std::move(id)), assistant_(std::make_unique<Assistant>(std::move(core))), ui_(std::move(uiHost)) {
    setFocusPolicy(true);
    setZoomSupport(ui::ZoomSupport::Scaled);
    const std::string base = this->id();
    const auto iconButton = [&](const char* label, ui::Icon icon, const char* idSuffix, const char* tip) {
        auto b = std::make_unique<ui::Button>(label, base + idSuffix);
        b->setIcon(icon);
        b->setCompact(true);
        b->setStyle(ui::Button::Style::Flat);
        b->setTooltip(tip);
        return &static_cast<ui::Button&>(addChild(std::move(b)));
    };
    newBtn_ = iconButton("Nouvelle conversation", ui::Icon::Plus, ".nouvelle", "Nouvelle conversation");
    settingsBtn_ = iconButton("Param\xC3\xA8tres de l'Assistant IA", ui::Icon::Settings, ".parametres",
                              "Param\xC3\xA8tres de l'Assistant IA : fournisseur, mod\xC3\xA8le, cl\xC3\xA9, confidentialit\xC3\xA9, historique");
    closeBtn_ = iconButton("Replier l'Assistant", ui::Icon::Close, ".replier", "Replier l'Assistant (Ctrl+Maj+I le rouvre)");
    modePreview_ = &static_cast<ui::ToggleButton&>(addChild(std::make_unique<ui::ToggleButton>("Pr\xC3\xA9visualisation", base + ".mode.apercu")));
    modePreview_->setTooltip("Chaque proposition passe par l'aper\xC3\xA7u : rien ne change tant que vous n'avez pas dit Appliquer.");
    modeDirect_ = &static_cast<ui::ToggleButton&>(addChild(std::make_unique<ui::ToggleButton>("Direct", base + ".mode.direct")));
    modeDirect_->setTooltip("Chaque proposition s'applique d\xC3\xA8s la r\xC3\xA9ponse (une suppression multiple demande d'abord) ; Ctrl+Z la reprend.");
    auto rename = std::make_unique<ui::InputText>(base + ".renommer");
    rename->setVisibility(ui::Visibility::Hidden);
    rename_ = &static_cast<ui::InputText&>(addChild(std::move(rename)));
    chat_ = &static_cast<AiChatView&>(addChild(std::make_unique<AiChatView>(base + ".discussion", *this)));
    prompt_ = &static_cast<AiPromptEdit&>(addChild(std::make_unique<AiPromptEdit>(base + ".saisie")));
    prompt_->setTooltip("Votre demande. Entr\xC3\xA9" "e : envoyer ; Maj+Entr\xC3\xA9" "e : nouvelle ligne ; un fichier l\xC3\xA2" "ch\xC3\xA9 ici se joint.");
    attachBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Joindre\xE2\x80\xA6", base + ".joindre")));
    attachBtn_->setIcon(ui::Icon::Link);
    attachBtn_->setCompact(true);   // l'icone seule (le libelle : l'infobulle, les seances de script)
    attachBtn_->setStyle(ui::Button::Style::Flat);
    attachBtn_->setTooltip("Joindre un fichier : image (PNG, JPG), PDF, texte, CSV, Excel, XML, JSON, projet (.xihm, .xapi). Il est copi\xC3\xA9 dans le dossier de l'Assistant.");
    sendBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Envoyer", base + ".envoyer")));
    sendBtn_->setStyle(ui::Button::Style::Primary);
    auto menu = std::make_unique<ui::PopupMenu>(base + ".menu");
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::move(menu)));

    links_ += newBtn_->clicked->connect([this] { newConversation(); });
    links_ += settingsBtn_->clicked->connect([this] { openSettings(); });
    links_ += closeBtn_->clicked->connect([this] { if (ui_.close) ui_.close(); });
    links_ += modePreview_->toggled->connect([this](bool) { if (!syncing_) setDirect(false); });
    links_ += modeDirect_->toggled->connect([this](bool) { if (!syncing_) setDirect(true); });
    links_ += rename_->editingDone->connect([this](const std::string& t) { if (renaming()) (void)commitRename(t); });
    links_ += prompt_->submitted->connect([this] { (void)send(); });
    links_ += attachBtn_->clicked->connect([this] { askAttach(); });
    links_ += sendBtn_->clicked->connect([this] {
        if (assistant_->generating()) stop();
        else (void)send();
    });
    links_ += menu_->itemChosen->connect([this](int item) { menuChosen(item); });
    refreshButtons();
}

AiPanel::~AiPanel() = default;

void AiPanel::say(const std::string& text) {
    if (text.empty()) return;
    lastStatus_ = text;
    if (ui_.status) ui_.status(text);
}

void AiPanel::refreshButtons() {
    const auto& a = *assistant_;
    syncing_ = true;
    modePreview_->setChecked(!a.directMode());
    modeDirect_->setChecked(a.directMode());
    syncing_ = false;
    const bool gen = a.generating();
    sendBtn_->setText(gen ? "Arr\xC3\xAAter" : "Envoyer");
    sendBtn_->setIcon(gen ? ui::Icon::Stop : ui::Icon::None);
    sendBtn_->setStyle(gen ? ui::Button::Style::Default : ui::Button::Style::Primary);
    sendBtn_->setTooltip(gen ? "Arr\xC3\xAAter la g\xC3\xA9n\xC3\xA9ration : la r\xC3\xA9ponse est gard\xC3\xA9" "e telle quelle, sans proposition."
                             : "Envoyer la demande (Entr\xC3\xA9" "e)");
    sendBtn_->setEnabled(gen || (a.settings().enabled && !a.processing()));
    attachBtn_->setEnabled(!gen);
    prompt_->setReadOnly(!a.settings().enabled);
}

void AiPanel::tick(double now) {
    (void)assistant_->tick(now);
    sync();
}

void AiPanel::ensureLoaded() {
    if (loaded_) return;
    loaded_ = true;
    std::vector<std::string> problems;
    assistant_->load(&problems);
    for (const auto& p : problems) say("Assistant IA : conversation illisible, laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9 (" + p + ")");
    seen_ = 0;
    sync();
    chat_->scrollToBottom();
}

void AiPanel::sync() {
    if (assistant_->revision() != seen_) {
        seen_ = assistant_->revision();
        refreshButtons();
        chat_->rebuild();
        invalidateLayout();
        invalidate();
    }
    if (const auto n = assistant_->takeNotice(); !n.empty()) say(n);
    // Le mode Direct et une suppression multiple : on demande d'abord.
    if (const int m = assistant_->takeDirectConfirm(); m >= 0) (void)applyProposal(m);
}

// ---- les conversations ------------------------------------------------------------------
void AiPanel::newConversation() {
    cancelRename();
    assistant_->newConversation();
    seen_ = 0;
    sync();
    prompt_->setText({});
    prompt_->takeFocus();
    say("Nouvelle conversation : d\xC3\xA9" "crivez votre demande, ou choisissez une suggestion.");
}

bool AiPanel::selectConversation(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return false;
    cancelRename();
    const bool ok = assistant_->select(list[static_cast<std::size_t>(index)].id);
    chat_->rebuild();
    chat_->scrollToBottom();
    invalidateLayout();
    return ok;
}

void AiPanel::startRename(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    renameIndex_ = index;
    rename_->setText(list[static_cast<std::size_t>(index)].title);
    rename_->setVisibility(ui::Visibility::Visible);
    invalidateLayout();
    layout();
    (void)rename_->focusAndSelectAll();
}

bool AiPanel::commitRename(const std::string& title) {
    if (!renaming()) return false;
    const auto& list = assistant_->conversations();
    const int i = renameIndex_;
    renameIndex_ = -1;
    rename_->setVisibility(ui::Visibility::Hidden);
    invalidateLayout();
    if (i < 0 || i >= static_cast<int>(list.size())) return false;
    const std::string id = list[static_cast<std::size_t>(i)].id;
    const bool ok = assistant_->rename(id, title);
    if (ok) say("Conversation renomm\xC3\xA9" "e : " + title);
    takeFocus();
    return ok;
}

void AiPanel::cancelRename() {
    if (!renaming()) return;
    renameIndex_ = -1;
    rename_->setVisibility(ui::Visibility::Hidden);
    invalidateLayout();
}

void AiPanel::askDelete(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Conversation& c = list[static_cast<std::size_t>(index)];
    const std::string id = c.id, title = c.title;
    std::size_t files = 0;
    for (const auto& m : c.messages) files += m.attachments.size();
    const auto run = [this, id, title] {
        if (assistant_->remove(id)) say("Conversation supprim\xC3\xA9" "e : " + title);
        chat_->rebuild();
        invalidateLayout();
    };
    if (!ui_.showDialog) {   // sans hote (un essai) : on supprime
        run();
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.ai.supprimer";
    spec.title = "Supprimer la conversation";
    spec.text = "Supprimer \xC2\xAB " + title + " \xC2\xBB et ses " + std::to_string(c.messages.size()) + " messages"
                + (files ? " (et la copie de ses " + std::to_string(files) + " pi\xC3\xA8" "ces jointes)" : std::string{})
                + " ?\nLe projet n'est pas touch\xC3\xA9 : seule la conversation part du dossier de l'Assistant.";
    spec.confirm = "Supprimer";
    spec.danger = true;
    spec.width = 520.f;
    ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [run](const menu::DialogResult& r) {
        if (r.accepted()) run();
    });
}

void AiPanel::setConversationsOpen(bool open) {
    listOpen_ = open;
    invalidateLayout();
}

void AiPanel::openRowMenu(int index, gfx::Point at) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    menuRow_ = index;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = list[static_cast<std::size_t>(index)].title;
    items.push_back(head);
    ui::PopupMenu::Item open;
    open.label = "Ouvrir";
    open.icon = ui::Icon::Open;
    open.id = kMenuOpen;
    items.push_back(open);
    ui::PopupMenu::Item ren;
    ren.label = "Renommer\xE2\x80\xA6";
    ren.icon = ui::Icon::Rename;
    ren.shortcut = "F2";
    ren.id = kMenuRename;
    items.push_back(ren);
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    ui::PopupMenu::Item del;
    del.label = "Supprimer\xE2\x80\xA6";
    del.icon = ui::Icon::Delete;
    del.shortcut = "Suppr";
    del.id = kMenuDelete;
    items.push_back(del);
    items.push_back(sep);
    ui::PopupMenu::Item fresh;
    fresh.label = "Nouvelle conversation";
    fresh.icon = ui::Icon::Plus;
    fresh.id = kMenuNew;
    items.push_back(fresh);
    menu_->setItems(std::move(items));
    menu_->openAtSurface(toSurface(at));
}

void AiPanel::menuChosen(int id) {
    const int row = menuRow_;
    menuRow_ = -1;
    switch (id) {
        case kMenuOpen: (void)selectConversation(row); break;
        case kMenuRename: startRename(row); break;
        case kMenuDelete: askDelete(row); break;
        case kMenuNew: newConversation(); break;
        default: break;
    }
}

// ---- envoyer --------------------------------------------------------------------------------
bool AiPanel::send() {
    const std::string text = prompt_->text();
    if (!sendText(text)) return false;
    prompt_->setText({});
    return true;
}

bool AiPanel::sendText(const std::string& text) {
    cancelRename();
    std::string why;
    if (!assistant_->send(text, &why)) {
        say(why);
        return false;
    }
    seen_ = 0;
    sync();
    chat_->scrollToBottom();
    return true;
}

void AiPanel::stop() {
    assistant_->stop();
    sync();
}

bool AiPanel::clickSuggestion(std::size_t index) {
    const auto& s = demoSuggestions();
    if (index >= s.size()) return false;
    return sendText(s[index]);
}

void AiPanel::askAttach() {
    if (assistant_->generating()) {
        say("Attendez la fin de la r\xC3\xA9ponse pour joindre un fichier.");
        return;
    }
    ui::FilePick pick;
    pick.title = "Joindre un fichier \xC3\xA0 la demande";
    pick.filters = {{"Fichiers pris en charge", attachmentFilter()}, {"Images", "png;jpg;jpeg"}, {"Documents", "pdf;txt;csv;xlsx;xml;json"},
                    {"Projets", "xihm;xapi;xpgproj"}};
    if (!ui::pickFile(pick, [this](std::string path) {
            if (!path.empty()) (void)attachFile(path);
        }))
        say("Pas de s\xC3\xA9lecteur de fichiers ici : glissez le fichier sur le panneau.");
}

bool AiPanel::attachFile(const std::string& path) {
    std::string why;
    if (!assistant_->attach(path, &why)) {
        say(why);
        return false;
    }
    const auto& a = assistant_->pending().back().attachment;
    say("Pi\xC3\xA8" "ce jointe : " + a.name + " (" + formatBytes(a.bytes) + ")" + (a.file.empty() ? std::string{} : ", copi\xC3\xA9" "e dans le dossier de l'Assistant"));
    sync();
    return true;
}

void AiPanel::removePending(std::size_t index) {
    assistant_->detach(index);
    sync();
}

// ---- les propositions ----------------------------------------------------------------------
void AiPanel::openPreview(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return;
    if (p->state != "proposee") {
        say(p->state == "appliquee" ? "Cette proposition est d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9" "e (Ctrl+Z la reprend)."
                                    : "Cette proposition a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e.");
        return;
    }
    if (!ui_.showDialog) return;
    ui_.showDialog(std::make_unique<AiPreviewDialog>(*this, message), [](const menu::DialogResult&) {});
}

bool AiPanel::applyProposal(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return false;
    const auto doApply = [this, message] {
        std::string why;
        const bool ok = assistant_->apply(message, &why);
        if (!ok) say(why);
        sync();
        return ok;
    };
    const Check ck = assistant_->check(message);
    if (ck.confirmRemoval && ui_.showDialog) {
        HmiAskDialog::Spec spec;
        spec.id = "dialog.ai.confirmer";
        spec.title = "Supprimer " + std::to_string(ck.remove) + " objets";
        spec.text = "L'Assistant propose de supprimer " + std::to_string(ck.remove) + " objets de la vue \xC2\xAB " + p->view
                    + " \xC2\xBB. Une suppression multiple se confirme.\nCtrl+Z la reprend d'un coup.";
        spec.listTitle = "Objets supprim\xC3\xA9s";
        for (const auto& it : ck.items)
            if (it.what == Item::What::Remove) spec.items.push_back({it.name, std::string(hmi::kindLabel(it.kind)), true});
        spec.plainItems = true;
        spec.confirm = "Supprimer les " + std::to_string(ck.remove) + " objets";
        spec.danger = true;
        spec.width = 560.f;
        ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [doApply](const menu::DialogResult& r) {
            if (r.accepted()) (void)doApply();
        });
        return false;   // la reponse viendra
    }
    return doApply();
}

bool AiPanel::refuseProposal(int message) {
    const bool ok = assistant_->refuse(message);
    sync();
    return ok;
}

void AiPanel::editRequest(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return;
    const std::string request = p->request;
    if (p->state == "proposee") (void)assistant_->refuse(message);
    prompt_->setText(request);
    prompt_->takeFocus();
    sync();
    say("La demande est dans la saisie : modifiez-la, puis Entr\xC3\xA9" "e.");
}

void AiPanel::openSettings() {
    if (!ui_.showDialog) return;
    ui_.showDialog(std::make_unique<AiSettingsDialog>(*this), [this](const menu::DialogResult&) {
        seen_ = 0;
        sync();
    });
}

void AiPanel::setDirect(bool direct) {
    assistant_->setDirectMode(direct);
    refreshButtons();
    say(direct ? "Mode Direct : chaque proposition s'applique d\xC3\xA8s la r\xC3\xA9ponse (Ctrl+Z la reprend)."
               : "Mode Pr\xC3\xA9visualisation : chaque proposition passe par l'aper\xC3\xA7u.");
}

// ---- la mise en page --------------------------------------------------------------------------
gfx::Rect AiPanel::rowRect(int index) const {
    return index >= 0 && index < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(index)] : gfx::Rect{};
}
gfx::Rect AiPanel::promptRect() const { return prompt_->bounds(); }
gfx::Rect AiPanel::chatRect() const { return chat_->bounds(); }

void AiPanel::layoutRows() {
    const auto& list = assistant_->conversations();
    rows_.assign(list.size(), gfx::Rect{});
    if (!listOpen_) return;
    const float content = static_cast<float>(list.size()) * kRowH;
    listScroll_ = std::clamp(listScroll_, 0.f, std::max(0.f, content - list_.h));
    const float space = listBar_.space(list_, content, list_.h);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const gfx::Rect r{list_.x, list_.y + static_cast<float>(i) * kRowH - listScroll_, list_.w - space, kRowH};
        if (r.bottom() <= list_.y || r.y >= list_.bottom()) continue;
        rows_[i] = r;
    }
}

void AiPanel::onLayout() {
    const auto b = bounds();
    // Le menu du clic droit : le cadre du panneau (ferme, il ne prend aucun clic ; ouvert, il deborde).
    menu_->setBounds(b);
    const float x = b.x, w = b.w;
    header_ = {x, b.y, w, kHeaderH};
    const float bs = 28.f;
    closeBtn_->setBounds({b.right() - kPad - bs, b.y + (kHeaderH - bs) / 2.f, bs, bs});
    settingsBtn_->setBounds({closeBtn_->bounds().x - bs - 2.f, closeBtn_->bounds().y, bs, bs});
    newBtn_->setBounds({settingsBtn_->bounds().x - bs - 2.f, closeBtn_->bounds().y, bs, bs});
    statusLine_ = {x, header_.bottom(), w, kStatusH};
    modeRow_ = {x, statusLine_.bottom(), w, kModeH};
    const float pw = ui::measureWidth("Pr\xC3\xA9visualisation", gfx::FontId{16}) + 22.f;
    const float dw = ui::measureWidth("Direct", gfx::FontId{16}) + 26.f;
    modePreview_->setBounds({x + kPad, modeRow_.y + 3.f, pw, kModeH - 8.f});
    modeDirect_->setBounds({x + kPad + pw - 1.f, modeRow_.y + 3.f, dw, kModeH - 8.f});
    // Les conversations : l'en-tete, puis au plus kMaxRows lignes.
    listHead_ = {x, modeRow_.bottom() + 4.f, w, kListHeadH};
    const auto count = static_cast<int>(assistant_->conversations().size());
    const int shown = listOpen_ ? std::clamp(count, 1, kMaxRows) : 0;
    list_ = {x, listHead_.bottom(), w, static_cast<float>(shown) * kRowH};
    layoutRows();
    if (renaming() && renameIndex_ < static_cast<int>(rows_.size()) && rows_[static_cast<std::size_t>(renameIndex_)].h > 0.f) {
        const auto r = rows_[static_cast<std::size_t>(renameIndex_)];
        rename_->setBounds({r.x + 30.f, r.y + 2.f, r.w - 40.f, r.h - 4.f});
    } else if (renaming()) {
        rename_->setBounds({list_.x + 30.f, list_.y, list_.w - 40.f, kRowH - 4.f});
    }
    // Le bas : la barre (Joindre, Envoyer), la saisie, les pieces en attente ; la discussion prend le reste.
    bottomBar_ = {x, b.bottom() - kBottomH, w, kBottomH};
    const float sw = std::max(110.f, ui::measureWidth(sendBtn_->text(), gfx::FontId{16}) + 40.f);
    sendBtn_->setBounds({bottomBar_.right() - kPad - sw, bottomBar_.y + 5.f, sw, kBottomH - 10.f});
    attachBtn_->setBounds({x + kPad - 4.f, bottomBar_.y + 5.f, 32.f, kBottomH - 10.f});
    const float lh = ui::lineHeight(gfx::FontId{16});
    const auto lines = static_cast<float>(std::clamp<std::size_t>(prompt_->lineCount(), 2, 6));
    const float ph = std::clamp(lines * lh + 14.f, 56.f, 150.f);
    promptArea_ = {x + kPad, bottomBar_.y - ph - 2.f, w - 2 * kPad, ph};
    prompt_->setBounds(promptArea_);
    const float pendH = static_cast<float>(assistant_->pending().size()) * kPendingH;
    pendingArea_ = {x + kPad, promptArea_.y - pendH - (pendH > 0 ? 6.f : 0.f), w - 2 * kPad, pendH};
    const float chatTop = (listOpen_ ? list_.bottom() : listHead_.bottom()) + 1.f;
    const float chatBottom = pendingArea_.y - 6.f;
    chat_->setBounds({x, chatTop, w, std::max(40.f, chatBottom - chatTop)});
}

void AiPanel::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    const auto b = bounds();
    ctx.r.fillRect(b, c.panelBg);
    ctx.r.fillRect({b.x, b.y, 1.f, b.h}, c.border);
    // ---- l'en-tete
    ctx.r.fillRect({header_.x + 1.f, header_.y, header_.w - 1.f, header_.h}, c.headerBg);
    ui::drawIcon(ctx.r, ui::Icon::Assistant, {header_.x + kPad + 2.f, header_.y + (header_.h - 20.f) / 2.f, 20.f, 20.f}, c.accent);
    const float lhUi = ctx.r.lineHeight(f.uiBold);
    bold(ctx.r, {header_.x + kPad + 30.f, header_.y + (header_.h - lhUi) / 2.f}, "Assistant IA", f.uiBold, c.text);
    // ---- l'etat de connexion
    const auto& a = *assistant_;
    const bool on = a.settings().enabled;
    const float lhCap = ctx.r.lineHeight(f.caption);
    const float dotY = statusLine_.y + statusLine_.h / 2.f;
    ctx.r.fillRoundedRect({statusLine_.x + kPad + 6.f, dotY - 4.f, 8.f, 8.f}, on ? c.warning : c.textDisabled, 4.f);
    ctx.r.drawText({statusLine_.x + kPad + 22.f, statusLine_.y + (statusLine_.h - lhCap) / 2.f},
                   elide(ctx.r, a.connectionText(), f.caption, statusLine_.w - 2 * kPad - 24.f), f.caption, c.textMuted);
    // ---- le mode et la vue visee
    {
        const auto ctx2 = a.context();
        const std::string viewText = ctx2.view ? "Vue : " + ctx2.view->name : std::string("Aucune vue ouverte");
        const float vx = modeDirect_->bounds().right() + 10.f;
        ctx.r.drawText({vx, modeRow_.y + (modeRow_.h - lhCap) / 2.f}, elide(ctx.r, viewText, f.caption, modeRow_.right() - kPad - vx),
                       f.caption, ctx2.view ? c.textMuted : c.warning);
    }
    // ---- les conversations
    ctx.r.fillRect({listHead_.x + 1.f, listHead_.y, listHead_.w - 1.f, listHead_.h}, c.rowAltBg);
    ctx.r.fillRect({listHead_.x + 1.f, listHead_.y, listHead_.w - 1.f, 1.f}, c.border);
    ui::drawIcon(ctx.r, listOpen_ ? ui::Icon::Collapse : ui::Icon::Expand, {listHead_.x + kPad, listHead_.y + 5.f, 16.f, 16.f}, c.textMuted);
    const std::string head = "CONVERSATIONS (" + std::to_string(a.conversations().size()) + ")";
    ctx.r.drawText({listHead_.x + kPad + 22.f, listHead_.y + (listHead_.h - lhCap) / 2.f}, head, f.caption, c.textMuted);
    if (listOpen_) {
        ctx.r.pushClip(list_);
        const auto& list = a.conversations();
        if (list.empty())
            ctx.r.drawText({list_.x + kPad + 22.f, list_.y + (kRowH - lhCap) / 2.f}, "Aucune conversation : Nouvelle conversation (+).", f.caption,
                           c.textMuted);
        for (std::size_t i = 0; i < list.size() && i < rows_.size(); ++i) {
            const auto r = rows_[i];
            if (r.h <= 0.f) continue;
            const bool sel = static_cast<int>(i) == a.currentIndex();
            if (sel) ctx.r.fillRect(r, c.selectionBg);
            else if (static_cast<int>(i) == hoverRow_) ctx.r.fillRect(r, ctx.theme.brand.hover);
            if (sel) ctx.r.fillRect({r.x + 1.f, r.y, 3.f, r.h}, c.accent);
            const gfx::Color ink = sel ? c.selectionText : c.text;
            ui::drawIcon(ctx.r, ui::Icon::Assistant, {r.x + kPad + 2.f, r.y + (r.h - 14.f) / 2.f, 14.f, 14.f}, sel ? ink : c.textMuted);
            if (static_cast<int>(i) == renameIndex_) continue;   // le champ est dessus
            const auto& conv = list[i];
            const std::string date = shortDate(conv.modified);
            const float dw = ctx.r.measure(date, f.caption).width;
            float right = r.right() - kPad - dw;
            ctx.r.drawText({right, r.y + (r.h - lhCap) / 2.f}, date, f.caption, sel ? ink : c.textMuted);
            if (conv.demo) {
                const float bw = ctx.r.measure("d\xC3\xA9mo", f.caption).width + 10.f;
                right -= bw + 6.f;
                const gfx::Rect chip{right, r.y + 6.f, bw, r.h - 12.f};
                ctx.r.fillRoundedRect(chip, alpha(c.info, 40), 3.f);
                ctx.r.drawText({chip.x + 5.f, chip.y + (chip.h - lhCap) / 2.f}, "d\xC3\xA9mo", f.caption, sel ? ink : c.info);
            }
            const float tx = r.x + kPad + 24.f;
            ctx.r.drawText({tx, r.y + (r.h - ctx.r.lineHeight(kBody)) / 2.f}, elide(ctx.r, conv.title, kBody, right - tx - 8.f), kBody, ink);
        }
        ctx.r.popClip();
        listBar_.paint(ctx, list_, static_cast<float>(a.conversations().size()) * kRowH, list_.h, listScroll_);
    }
    ctx.r.fillRect({b.x + 1.f, (listOpen_ ? list_.bottom() : listHead_.bottom()), b.w - 1.f, 1.f}, c.border);
    // ---- les pieces jointes en attente
    pendingRemove_.clear();
    const auto& pend = a.pending();
    for (std::size_t i = 0; i < pend.size(); ++i) {
        const gfx::Rect r{pendingArea_.x, pendingArea_.y + static_cast<float>(i) * kPendingH, pendingArea_.w, kPendingH - 4.f};
        ctx.r.fillRoundedRect(r, c.inputBg, 4.f);
        ctx.r.strokeRect(r, c.border, 1.f);
        const auto look = lookOf(ctx.theme, pend[i].attachment.name);
        ui::drawIcon(ctx.r, look.icon, {r.x + 6.f, r.y + (r.h - 16.f) / 2.f, 16.f, 16.f}, look.tint);
        const gfx::Rect x{r.right() - 24.f, r.y + (r.h - 18.f) / 2.f, 18.f, 18.f};
        pendingRemove_.push_back(x);
        ui::drawIcon(ctx.r, ui::Icon::Close, {x.x + 2.f, x.y + 2.f, 14.f, 14.f}, c.textMuted);
        const std::string size = formatBytes(pend[i].attachment.bytes);
        const bool done = pend[i].progress >= 1.f;
        const std::string state = done ? size : "traitement " + std::to_string(static_cast<int>(pend[i].progress * 100.f)) + " %";
        const float sw = ctx.r.measure(state, f.caption).width;
        ctx.r.drawText({x.x - 8.f - sw, r.y + (r.h - lhCap) / 2.f}, state, f.caption, done ? c.textMuted : c.info);
        const float nx = r.x + 28.f;
        const std::string label = pend[i].attachment.name + "  \xC2\xB7  " + look.type;
        ctx.r.drawText({nx, r.y + (r.h - lhCap) / 2.f}, elide(ctx.r, label, f.caption, x.x - 16.f - sw - nx), f.caption, c.text);
        if (!done) {
            ctx.r.fillRect({r.x + 1.f, r.bottom() - 3.f, r.w - 2.f, 2.f}, alpha(c.info, 50));
            ctx.r.fillRect({r.x + 1.f, r.bottom() - 3.f, (r.w - 2.f) * pend[i].progress, 2.f}, c.info);
        }
    }
    // ---- la barre du bas : l'aide des touches entre Joindre et Envoyer
    ctx.r.fillRect({b.x + 1.f, bottomBar_.y, b.w - 1.f, 1.f}, c.border);
    const float hx = attachBtn_->bounds().right() + 8.f;
    const float hw = sendBtn_->bounds().x - 8.f - hx;
    if (a.generating()) {
        ctx.r.drawText({hx, bottomBar_.y + (bottomBar_.h - lhCap) / 2.f}, elide(ctx.r, std::string(phaseLabel(a.phase())), f.caption, hw), f.caption,
                       c.textMuted);
    } else {
        // L'aide des touches : sur une ligne si elle tient, sinon sur deux.
        const std::string one = "Entr\xC3\xA9" "e : envoyer \xC2\xB7 Maj+Entr\xC3\xA9" "e : nouvelle ligne";
        if (ctx.r.measure(one, f.caption).width <= hw) {
            ctx.r.drawText({hx, bottomBar_.y + (bottomBar_.h - lhCap) / 2.f}, one, f.caption, c.textMuted);
        } else {
            const float y0 = bottomBar_.y + (bottomBar_.h - 2.f * lhCap - 1.f) / 2.f;
            ctx.r.drawText({hx, y0}, elide(ctx.r, "Entr\xC3\xA9" "e : envoyer", f.caption, hw), f.caption, c.textMuted);
            ctx.r.drawText({hx, y0 + lhCap + 1.f}, elide(ctx.r, "Maj+Entr\xC3\xA9" "e : nouvelle ligne", f.caption, hw), f.caption, c.textMuted);
        }
    }
}

void AiPanel::onPaintOverlay(const ui::PaintContext& ctx) {
    // Le texte d'invite de la saisie vide (le champ du logiciel n'en a pas).
    if (prompt_->text().empty() && !prompt_->focused()) {
        const auto r = prompt_->bounds();
        const std::string invite = assistant_->settings().enabled ? "D\xC3\xA9" "crivez votre demande\xE2\x80\xA6 (ex. : Ajoute une cuve avec un indicateur de niveau)"
                                                                  : "L'Assistant est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres)";
        ctx.r.pushClip(r);
        ctx.r.drawText({r.x + 8.f, r.y + 7.f}, elide(ctx.r, invite, kBody, r.w - 16.f), kBody, ctx.theme.color.textMuted);
        ctx.r.popClip();
    }
}

int AiPanel::rowAt(gfx::Point p) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].h > 0.f && rows_[i].contains(p)) return static_cast<int>(i);
    return -1;
}

ui::EventResult AiPanel::onEvent(const ui::InputEvent& ev) {
    // La barre de la liste se tire.
    if (listOpen_ && listBar_.handle(*this, ev, list_, static_cast<float>(assistant_->conversations().size()) * kRowH, list_.h, listScroll_)) {
        layoutRows();
        return ui::EventResult::Consumed;
    }
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        if (!drop->path.empty()) (void)attachFile(drop->path);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        if (h != hoverRow_) {
            hoverRow_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos) && listOpen_) {
        listScroll_ -= w->dy * kRowH;
        layoutRows();
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        for (std::size_t i = 0; i < pendingRemove_.size(); ++i)
            if (pendingRemove_[i].contains(d->pos) && d->button == ui::MouseButton::Left) {
                removePending(i);
                return ui::EventResult::Consumed;
            }
        if (listHead_.contains(d->pos) && d->button == ui::MouseButton::Left) {
            setConversationsOpen(!listOpen_);
            return ui::EventResult::Consumed;
        }
        const int row = rowAt(d->pos);
        if (row >= 0) {
            takeFocus();
            if (d->button == ui::MouseButton::Right) {
                (void)selectConversation(row);
                openRowMenu(row, d->pos);
            } else if (d->clickCount >= 2) {
                startRename(row);
            } else {
                (void)selectConversation(row);
            }
            return ui::EventResult::Consumed;
        }
        if (header_.contains(d->pos) || statusLine_.contains(d->pos) || modeRow_.contains(d->pos)) return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape && renaming()) {
            cancelRename();
            takeFocus();
            return ui::EventResult::Consumed;
        }
        // La liste a le clavier (le panneau lui-meme) : F2, Suppr, les fleches.
        if (focused()) {
            const int cur = assistant_->currentIndex();
            if (k->key == ui::Key::F2 && cur >= 0) { startRename(cur); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Delete && cur >= 0) { askDelete(cur); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Up && cur > 0) { (void)selectConversation(cur - 1); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Down && cur + 1 < static_cast<int>(assistant_->conversations().size())) {
                (void)selectConversation(cur + 1);
                return ui::EventResult::Consumed;
            }
        }
    }
    return ui::EventResult::Ignored;
}

// =============================================================================
//  La discussion
// =============================================================================
AiChatView::AiChatView(std::string id, AiPanel& panel) : ui::Widget(std::move(id)), panel_(panel) {}

float AiChatView::viewHeight() const noexcept { return bounds().h; }
bool AiChatView::atBottom() const noexcept { return offset_ >= std::max(0.f, content_ - viewHeight()) - 2.f; }

void AiChatView::scrollToBottom() {
    stick_ = true;
    measure();
    offset_ = std::max(0.f, content_ - viewHeight());
    invalidateLayout();
}

void AiChatView::scrollBy(float dy) {
    offset_ = std::clamp(offset_ + dy, 0.f, std::max(0.f, content_ - viewHeight()));
    stick_ = atBottom();
    invalidateLayout();
}

std::vector<AiChatView::Line> AiChatView::layoutText(const std::string& md, float width) {
    const std::string key = std::to_string(static_cast<int>(width)) + "|" + md;
    auto& cache = layoutCache();
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    std::vector<Line> out;
    const float lhBody = ui::lineHeight(kBody) + 4.f;
    const float lhHead = ui::lineHeight(kHead) + 6.f;
    std::size_t from = 0;
    bool firstPara = true, prevBlank = false;
    while (from <= md.size()) {
        std::size_t nl = md.find('\n', from);
        if (nl == std::string::npos) nl = md.size();
        std::string raw = md.substr(from, nl - from);
        from = nl + 1;
        if (raw.empty()) {
            prevBlank = true;
            if (nl >= md.size()) break;
            continue;
        }
        Line proto;
        std::uint8_t base = 0;
        float indent = 0.f;
        if (raw.rfind("## ", 0) == 0 || raw.rfind("# ", 0) == 0) {
            raw = raw.substr(raw.find(' ') + 1);
            base = 3;
        } else if (raw.rfind("- ", 0) == 0 || raw.rfind("* ", 0) == 0) {
            raw = raw.substr(2);
            proto.bullet = "\xE2\x80\xA2";
            indent = 16.f;
        } else if (raw.size() > 3 && std::isdigit(static_cast<unsigned char>(raw[0])) && raw[1] == '.' && raw[2] == ' ') {
            proto.bullet = raw.substr(0, 2);
            raw = raw.substr(3);
            indent = 18.f;
        }
        // Les morceaux : **gras**, `code`, *attenue*.
        struct Tok { std::string text; std::uint8_t style; bool space; };
        std::vector<Tok> toks;
        std::uint8_t style = base;
        std::string word;
        bool pendingSpace = false;
        const auto flush = [&] {
            if (word.empty()) return;   // l'espace en attente reste (un mot en gras apres un blanc)
            toks.push_back({word, style, pendingSpace});
            word.clear();
            pendingSpace = false;
        };
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const char ch = raw[i];
            if (ch == '*' && i + 1 < raw.size() && raw[i + 1] == '*' && base != 3) {
                flush();
                style = style == 1 ? 0 : 1;
                ++i;
                continue;
            }
            if (ch == '`' && base != 3) {
                flush();
                style = style == 2 ? 0 : 2;
                continue;
            }
            if (ch == '*' && base != 3 && style != 2) {
                flush();
                style = style == 4 ? 0 : 4;
                continue;
            }
            if (ch == ' ' && style != 2) {   // dans du `code`, l'espace fait partie du morceau
                flush();
                pendingSpace = true;
                continue;
            }
            word += ch;
        }
        flush();
        // Le placement, mot a mot.
        const float lh = base == 3 ? lhHead : lhBody;
        Line line = proto;
        line.h = lh;
        line.bulletX = indent > 0.f ? 4.f : 0.f;
        line.gapBefore = base == 3 ? (firstPara ? 0.f : 8.f) : (prevBlank && !firstPara ? 6.f : 0.f);
        float x = indent;
        bool lineStart = true;
        const float space = ui::measureWidth(" ", kBody);
        for (const auto& t : toks) {
            const gfx::FontId f = t.style == 3 ? kHead : kBody;
            const float tw = ui::measureWidth(t.text, f) + (t.style == 2 ? 8.f : 0.f) + (t.style == 1 || t.style == 3 ? 1.f : 0.f);
            const float gap = (!lineStart && t.space) ? space : 0.f;
            // Une ponctuation seule (le ; d'une liste, le : francais) ne passe pas seule a la ligne.
            const bool glue = t.text == ";" || t.text == ":" || t.text == "!" || t.text == "?" || t.text == "\xC2\xBB";
            if (!lineStart && !glue && x + gap + tw > width) {
                out.push_back(std::move(line));
                line = Line{};
                line.h = lh;
                x = indent;
                lineStart = true;
            }
            const float at = x + (lineStart ? 0.f : gap);
            line.runs.push_back({t.text, at, tw, t.style});
            x = at + tw;
            lineStart = false;
        }
        out.push_back(std::move(line));
        firstPara = false;
        prevBlank = false;
        if (nl >= md.size()) break;
    }
    cache.emplace(key, out);
    return out;
}

const AiChatView::Thumb& AiChatView::thumb(int message) const {
    auto& t = thumbs_[message];
    const Proposal* p = panel_.assistant().proposal(message);
    std::string key;
    if (p) {
        key = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
        if (auto doc = panel_.assistant().host().document ? panel_.assistant().host().document() : nullptr)
            if (const auto* v = targetView(doc->project, *p)) key += "|" + fingerprint(*v);
    }
    if (t.key != key || key.empty()) {
        t.key = key;
        t.preview = panel_.assistant().previewOf(message);
        t.check = panel_.assistant().check(message);
    }
    return t;
}

void AiChatView::measure() {
    const float w = bounds().w;
    blocks_.clear();
    suggestionRects_.clear();
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    const float inner = std::max(80.f, w - 2 * kPad - 12.f);
    float y = 10.f;
    const int generating = a.generatingMessage();
    const float meta = ui::lineHeight(kMeta) + 4.f;
    if (conv) {
        for (std::size_t i = 0; i < conv->messages.size(); ++i) {
            const auto& m = conv->messages[i];
            Block b;
            b.message = static_cast<int>(i);
            b.user = m.role == "utilisateur";
            b.y = y;
            const bool live = static_cast<int>(i) == generating;
            const std::string text = live ? a.shownText() : m.text;
            if (b.user) {
                const float maxW = inner * 0.86f - 20.f;
                b.lines = layoutText(text, maxW);
                float tw = 0.f;
                for (const auto& l : b.lines)
                    for (const auto& r : l.runs) tw = std::max(tw, r.x + r.w);
                for (const auto& at : m.attachments) tw = std::max(tw, std::min(maxW, ui::measureWidth(at.name, kMeta) + 110.f));
                b.textW = tw;
                float th = 0.f;
                for (const auto& l : b.lines) th += l.h + l.gapBefore;
                if (text.empty()) th = 0.f;
                b.attachH = static_cast<float>(m.attachments.size()) * 24.f;
                const float bw = tw + 20.f, bh = th + b.attachH + 14.f;
                b.bubble = {w - kPad - 6.f - bw, meta, bw, bh};
                b.h = meta + bh;
            } else {
                b.lines = layoutText(text, inner - 8.f);
                float th = 0.f;
                for (const auto& l : b.lines) th += l.h + l.gapBefore;
                b.h = meta + th + 4.f;
                if (live) b.h += 26.f;   // l'indicateur
                if (!live && m.proposal) {
                    // Une carte close (appliquee, refusee) n'a plus d'apercu : la vue a change depuis.
                    const bool open = m.proposal->state == "proposee";
                    const Thumb& t = thumb(static_cast<int>(i));
                    const auto lines = open ? static_cast<float>(std::min<std::size_t>(3, t.check.errors.size() + t.check.warnings.size())) : 0.f;
                    const float ch = open ? 30.f + 22.f + 104.f + lines * 18.f + 40.f : 30.f + 22.f + 26.f;
                    b.card = {kPad + 2.f, b.h + 6.f, inner - 4.f, ch};
                    b.h += ch + 10.f;
                }
            }
            y += b.h + 16.f;
            blocks_.push_back(std::move(b));
        }
    }
    // Les suggestions : une conversation vide, ou apres une demande non reconnue.
    const bool empty = !conv || conv->messages.empty();
    if (empty || a.suggestionsAfterLast()) {
        if (empty) {   // l'accueil : "Bonjour." puis son texte
            y += 6.f + 30.f;
            for (const auto& l : layoutText(welcomeText(), w - 2 * kPad - 12.f)) y += l.h;
            y += 14.f;
        }
        suggestionsTop_ = y;
        y += ui::lineHeight(kMeta) + 8.f;
        for (const auto& s : demoSuggestions()) {
            const auto lines = layoutText(s, inner - 26.f);
            const float h = std::min<std::size_t>(lines.size(), 2) * (ui::lineHeight(kBody) + 4.f) + 10.f;
            suggestionRects_.push_back({kPad + 2.f, y, inner - 4.f, h});
            y += h + 6.f;
        }
    }
    content_ = y + 8.f;
    measuredW_ = w;
}

void AiChatView::rebuild() {
    const bool wasBottom = stick_ || atBottom();
    measure();
    // Les boutons des cartes : un trio par proposition encore "proposee". Ils sont GARDES d'une
    // fois sur l'autre (caches quand ils ne servent pas) : un clic sur l'un d'eux peut refaire la
    // discussion pendant qu'il emet son signal - il ne doit pas etre detruit sous lui.
    for (auto& cb : cards_) {
        cb.message = -1;
        for (ui::Button* btn : {cb.preview, cb.apply, cb.third}) btn->setVisibility(ui::Visibility::Collapsed);
    }
    const auto& a = panel_.assistant();
    std::size_t slot = 0;
    for (const auto& b : blocks_) {
        if (b.card.h <= 0.f) continue;
        const Proposal* p = a.proposal(b.message);
        if (!p || p->state != "proposee") continue;
        const Thumb& t = thumb(b.message);
        if (slot == cards_.size()) {
            CardButtons cb;
            const std::string base = id() + ".carte" + std::to_string(slot + 1);
            auto mk = [&](const std::string& suffix, ui::Button::Style st) {
                auto btn = std::make_unique<ui::Button>(std::string{}, base + suffix);
                btn->setStyle(st);
                return &static_cast<ui::Button&>(addChild(std::move(btn)));
            };
            cb.preview = mk(".apercu", ui::Button::Style::Primary);
            cb.apply = mk(".appliquer", ui::Button::Style::Default);
            cb.third = mk(".refuser", ui::Button::Style::Flat);
            const std::size_t k = slot;
            links_ += cb.preview->clicked->connect([this, k] {
                if (k < cards_.size() && cards_[k].message >= 0) panel_.openPreview(cards_[k].message);
            });
            links_ += cb.apply->clicked->connect([this, k] {
                if (k < cards_.size() && cards_[k].message >= 0) (void)panel_.applyProposal(cards_[k].message);
            });
            links_ += cb.third->clicked->connect([this, k] {
                if (k >= cards_.size() || cards_[k].message < 0) return;
                if (cards_[k].stale) panel_.editRequest(cards_[k].message);
                else (void)panel_.refuseProposal(cards_[k].message);
            });
            cards_.push_back(cb);
        }
        CardButtons& cb = cards_[slot++];
        cb.message = b.message;
        cb.stale = t.check.stale || t.check.viewMissing;
        cb.preview->setText("Pr\xC3\xA9visualiser\xE2\x80\xA6");
        cb.preview->setTooltip("L'aper\xC3\xA7u : les objets, les propri\xC3\xA9t\xC3\xA9s avant \xE2\x86\x92 apr\xC3\xA8s, les variables, les avertissements, et la vue dessin\xC3\xA9" "e.");
        cb.apply->setText("Appliquer");
        cb.apply->setTooltip("Appliquer au projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
        cb.apply->setEnabled(t.check.ok());
        cb.third->setText(cb.stale ? "Redemander" : "Refuser");
        cb.third->setTooltip(cb.stale ? "La vue a chang\xC3\xA9 : remettre la demande dans la saisie." : "Refuser : le projet ne change pas.");
        cb.preview->setVisibility(ui::Visibility::Visible);
        cb.apply->setVisibility(cb.stale ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        cb.third->setVisibility(ui::Visibility::Visible);
    }
    if (wasBottom) offset_ = std::max(0.f, content_ - viewHeight());
    offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - viewHeight()));
    invalidateLayout();
    invalidate();
}

gfx::Rect AiChatView::cardRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message && b.card.h > 0.f)
            return {bounds().x + b.card.x, bounds().y + b.y - offset_ + b.card.y, b.card.w, b.card.h};
    return {};
}

ui::Button* AiChatView::cardButton(int message, int which) const {
    for (const auto& cb : cards_)
        if (cb.message == message && message >= 0) return which == 0 ? cb.preview : which == 1 ? cb.apply : cb.third;
    return nullptr;
}

gfx::Rect AiChatView::suggestionRect(std::size_t index) const {
    if (index >= suggestionRects_.size()) return {};
    const auto r = suggestionRects_[index];
    return {bounds().x + r.x, bounds().y + r.y - offset_, r.w, r.h};
}

std::vector<std::string> AiChatView::renderedLines(int message) const {
    std::vector<std::string> out;
    for (const auto& b : blocks_) {
        if (b.message != message) continue;
        for (const auto& l : b.lines) {
            std::string s = l.bullet.empty() ? std::string{} : l.bullet + " ";
            for (std::size_t k = 0; k < l.runs.size(); ++k) s += (k ? " " : "") + l.runs[k].text;
            out.push_back(s);
        }
    }
    return out;
}

void AiChatView::onLayout() {
    if (bounds().w != measuredW_) measure();
    offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - viewHeight()));
    if (stick_) offset_ = std::max(0.f, content_ - viewHeight());
    const auto b = bounds();
    for (auto& cb : cards_) {
        if (cb.message < 0) continue;
        gfx::Rect card{};
        for (const auto& bl : blocks_)
            if (bl.message == cb.message) card = {b.x + bl.card.x, b.y + bl.y - offset_ + bl.card.y, bl.card.w, bl.card.h};
        const float by = card.bottom() - 36.f;
        float x = card.x + 10.f;
        for (ui::Button* btn : {cb.preview, cb.apply, cb.third}) {
            if (btn->visibility() == ui::Visibility::Collapsed) continue;
            const float bw = ui::measureWidth(btn->text(), gfx::FontId{16}) + 26.f;
            btn->setBounds({x, by, bw, 28.f});
            x += bw + 6.f;
            // Hors de la discussion : cache (la decoupe le couperait, le clic le trouverait).
            const bool inside = by >= b.y && by + 28.f <= b.bottom();
            btn->setVisibility(inside ? ui::Visibility::Visible : ui::Visibility::Hidden);
        }
    }
}

void AiChatView::paintIndicator(const ui::PaintContext& ctx, gfx::Rect at) {
    const auto& c = ctx.theme.color;
    const auto phase = panel_.assistant().phase();
    // Trois points qui s'allument tour a tour, puis l'etat (en attente, reflexion, ecriture).
    for (int k = 0; k < 3; ++k) {
        const double t = std::fmod(ctx.time * 2.4 - k * 0.33, 1.0);
        const int a = t < 0.5 ? 90 + static_cast<int>(t * 300) : 240 - static_cast<int>((t - 0.5) * 300);
        ctx.r.fillRoundedRect({at.x + static_cast<float>(k) * 10.f, at.y + 8.f, 6.f, 6.f}, alpha(c.accent, a), 3.f);
    }
    ctx.r.drawText({at.x + 38.f, at.y + (22.f - ctx.r.lineHeight(kMeta)) / 2.f}, std::string(phaseLabel(phase)) + "  (Arr\xC3\xAAter : le bouton en bas)",
                   kMeta, c.textMuted);
}

void AiChatView::paintCard(const ui::PaintContext& ctx, const Block& b, gfx::Rect card) {
    const auto& c = ctx.theme.color;
    const auto& a = panel_.assistant();
    const Proposal* p = a.proposal(b.message);
    if (!p) return;
    const Thumb& t = thumb(b.message);
    const bool stale = p->state == "proposee" && (t.check.stale || t.check.viewMissing);
    ctx.r.fillRoundedRect(card, ctx.theme.brand.card.a ? ctx.theme.brand.card : c.inputBg, 6.f);
    ctx.r.strokeRect(card, c.border, 1.f);
    const float lhCap = ctx.r.lineHeight(kMeta);
    // L'en-tete : le titre, l'etat.
    float y = card.y + 8.f;
    ui::drawIcon(ctx.r, ui::Icon::Wrench, {card.x + 10.f, y, 16.f, 16.f}, c.textMuted);
    bold(ctx.r, {card.x + 32.f, y + (16.f - ctx.r.lineHeight(kBody)) / 2.f}, "Op\xC3\xA9rations propos\xC3\xA9" "es", kBody, c.text);
    std::string st;
    gfx::Color tone = c.info;
    if (p->state == "appliquee") { st = "Appliqu\xC3\xA9" "e"; tone = c.ok; }
    else if (p->state == "refusee") { st = "Refus\xC3\xA9" "e"; tone = c.textMuted; }
    else if (stale) { st = "P\xC3\xA9rim\xC3\xA9" "e"; tone = c.warning; }
    else if (!t.check.ok()) { st = "\xC3\x80 compl\xC3\xA9ter"; tone = c.error; }
    else st = "Propos\xC3\xA9" "e";
    const float sw = ctx.r.measure(st, kMeta).width + 14.f;
    const gfx::Rect chip{card.right() - 10.f - sw, y - 1.f, sw, 18.f};
    ctx.r.fillRoundedRect(chip, alpha(tone, 46), 9.f);
    ctx.r.drawText({chip.x + 7.f, chip.y + (chip.h - lhCap) / 2.f}, st, kMeta, tone);
    y += 24.f;
    // Les comptes.
    const auto count = [&](const char* label, int n, gfx::Color col, float& x) {
        const std::string s = std::string(label) + " " + std::to_string(n);
        ctx.r.fillRoundedRect({x, y + 5.f, 6.f, 6.f}, n > 0 ? col : c.textDisabled, 3.f);
        ctx.r.drawText({x + 10.f, y}, s, kMeta, n > 0 ? c.text : c.textMuted);
        x += ctx.r.measure(s, kMeta).width + 24.f;
    };
    float cx = card.x + 12.f;
    const int nc = p->state == "proposee" ? t.check.create : p->create;
    const int nm = p->state == "proposee" ? t.check.modify : p->modify;
    const int nr = p->state == "proposee" ? t.check.remove : p->remove;
    count("Cr\xC3\xA9" "er", nc, c.ok, cx);
    count("Modifier", nm, c.info, cx);
    count("Supprimer", nr, c.error, cx);
    y += 22.f;
    if (p->state != "proposee") {
        const std::string done = p->state == "appliquee" ? "Appliqu\xC3\xA9" "e au projet : Ctrl+Z la reprend d'un coup."
                                                         : "Refus\xC3\xA9" "e : le projet n'a pas chang\xC3\xA9.";
        ctx.r.drawText({card.x + 12.f, y + 2.f}, elide(ctx.r, done, kMeta, card.w - 24.f), kMeta, c.textMuted);
        return;
    }
    // L'apercu : la copie de la vue, dessinee par les painters du logiciel.
    const gfx::Rect area{card.x + 10.f, y, card.w - 20.f, 100.f};
    ctx.r.fillRect(area, c.windowBg);
    if (t.preview.ok && t.preview.view.width > 0 && t.preview.view.height > 0) {
        const auto& v = t.preview.view;
        const float z = std::min(area.w / static_cast<float>(v.width), area.h / static_cast<float>(v.height));
        const HmiViewport vp{area.x + (area.w - static_cast<float>(v.width) * z) / 2.f, area.y + (area.h - static_cast<float>(v.height) * z) / 2.f, z};
        const gfx::Rect page{vp.originX, vp.originY, static_cast<float>(v.width) * z, static_cast<float>(v.height) * z};
        ctx.r.fillRect(page, parseColor(v.background, gfx::Color::rgb(0x20242B)));
        HmiPaintOptions opt;
        const auto doc = a.host().document ? a.host().document() : nullptr;
        if (doc) {
            opt.project = &doc->project;
            opt.assets = &doc->project.assets;
        }
        ctx.r.pushClip(page);
        paintHmiView(ctx.r, v, vp, HmiPropertySource{}, ctx.theme, opt);
        if (p->state == "proposee") {
            const auto outline = [&](const std::vector<hmi::Id>& ids, gfx::Color col) {
                for (const auto id : ids)
                    for (const auto& o : v.objects)
                        if (o.id == id) {
                            const auto bx = hmi::edit::rotatedBounds(o);
                            const auto p0 = vp.toScreen(bx.x, bx.y);
                            ctx.r.strokeRect({p0.x - 1.f, p0.y - 1.f, static_cast<float>(bx.w) * z + 2.f, static_cast<float>(bx.h) * z + 2.f}, col, 1.f);
                        }
            };
            outline(t.preview.touched.created, c.ok);
            outline(t.preview.touched.modified, c.info);
            outline(t.preview.touched.removed, c.error);
        }
        ctx.r.popClip();
        ctx.r.strokeRect(page, c.border, 1.f);
    } else {
        ctx.r.drawText({area.x + 8.f, area.y + (area.h - lhCap) / 2.f},
                       elide(ctx.r, t.preview.why.empty() ? std::string("Pas d'aper\xC3\xA7u") : t.preview.why, kMeta, area.w - 16.f), kMeta, c.textMuted);
    }
    y = area.bottom() + 6.f;
    // Les erreurs puis les avertissements (trois lignes au plus ; le reste dans l'apercu).
    {
        int shown = 0;
        for (const auto& e : t.check.errors) {
            if (shown++ >= 3) break;
            ui::drawIcon(ctx.r, ui::Icon::Error, {card.x + 12.f, y + 1.f, 14.f, 14.f}, c.error);
            ctx.r.drawText({card.x + 32.f, y}, elide(ctx.r, e, kMeta, card.w - 44.f), kMeta, c.text);
            y += 18.f;
        }
        for (const auto& wv : t.check.warnings) {
            if (shown++ >= 3) break;
            ui::drawIcon(ctx.r, ui::Icon::Warning, {card.x + 12.f, y + 1.f, 14.f, 14.f}, c.warning);
            ctx.r.drawText({card.x + 32.f, y}, elide(ctx.r, wv, kMeta, card.w - 44.f), kMeta, c.textMuted);
            y += 18.f;
        }
    }
}

void AiChatView::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto bb = bounds();
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    ctx.r.pushClip(bb);
    const float meta = ctx.r.lineHeight(kMeta);
    const int generating = a.generatingMessage();
    for (const auto& b : blocks_) {
        const float top = bb.y + b.y - offset_;
        if (top > bb.bottom() || top + b.h < bb.y) continue;
        const auto& m = conv->messages[static_cast<std::size_t>(b.message)];
        const bool live = b.message == generating;
        // L'en-tete : qui, quand.
        const std::string when = shortDate(m.date, true);
        if (b.user) {
            const std::string who = "Vous \xC2\xB7 " + when;
            const float w = ctx.r.measure(who, kMeta).width;
            ctx.r.drawText({bb.right() - kPad - 6.f - w, top}, who, kMeta, c.textMuted);
            const gfx::Rect bub{bb.x + b.bubble.x, top + b.bubble.y, b.bubble.w, b.bubble.h};
            ctx.r.fillRoundedRect(bub, alpha(c.accent, 34), 6.f);
            ctx.r.strokeRect(bub, alpha(c.accent, 80), 1.f);
            float ly = bub.y + 7.f;
            if (!m.text.empty())
                for (const auto& l : b.lines) {
                    ly += l.gapBefore;
                    for (const auto& r : l.runs) ctx.r.drawText({bub.x + 10.f + r.x, ly}, r.text, kBody, c.text);
                    ly += l.h;
                }
            for (const auto& at : m.attachments) {
                const auto look = lookOf(ctx.theme, at.name);
                ui::drawIcon(ctx.r, look.icon, {bub.x + 10.f, ly + 3.f, 16.f, 16.f}, look.tint);
                const std::string s = at.name + "  \xC2\xB7  " + formatBytes(at.bytes);
                ctx.r.drawText({bub.x + 32.f, ly + 3.f}, elide(ctx.r, s, kMeta, bub.w - 42.f), kMeta, c.text);
                ly += 24.f;
            }
            continue;
        }
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {bb.x + kPad + 2.f, top + 1.f, 14.f, 14.f}, c.accent);
        std::string who = "Assistant \xC2\xB7 " + (live ? std::string("maintenant") : when);
        if (m.state == "arrete") who += " \xC2\xB7 arr\xC3\xAAt\xC3\xA9" "e";
        if (m.state == "erreur") who += " \xC2\xB7 erreur";
        ctx.r.drawText({bb.x + kPad + 22.f, top}, who, kMeta, m.state == "erreur" ? c.error : c.textMuted);
        float ly = top + meta + 4.f;
        const float tx = bb.x + kPad + 4.f;
        for (const auto& l : b.lines) {
            ly += l.gapBefore;
            if (!l.bullet.empty()) ctx.r.drawText({tx + l.bulletX, ly}, l.bullet, kBody, c.textMuted);
            for (const auto& r : l.runs) {
                const gfx::Point at{tx + r.x, ly + (r.style == 3 ? 0.f : 0.f)};
                switch (r.style) {
                    case 1: bold(ctx.r, at, r.text, kBody, c.text); break;
                    case 2: {
                        const float ch = ctx.r.lineHeight(kBody);
                        ctx.r.fillRoundedRect({at.x, at.y - 1.f, r.w, ch + 2.f}, ctx.theme.brand.codeBg.a ? ctx.theme.brand.codeBg : c.inputBg, 3.f);
                        ctx.r.drawText({at.x + 4.f, at.y}, r.text, kBody, c.syntaxString.a ? c.syntaxString : c.text);
                        break;
                    }
                    case 3: bold(ctx.r, at, r.text, kHead, c.text); break;
                    case 4: ctx.r.drawText(at, r.text, kBody, c.textMuted); break;
                    default: ctx.r.drawText(at, r.text, kBody, c.text); break;
                }
            }
            ly += l.h;
        }
        if (live) {
            // Le curseur d'ecriture au bout du texte, puis l'indicateur.
            if (a.phase() == Assistant::Phase::Writing && std::fmod(ctx.time, 1.0) < 0.6) {
                float endX = tx;
                if (!b.lines.empty() && !b.lines.back().runs.empty()) endX = tx + b.lines.back().runs.back().x + b.lines.back().runs.back().w + 2.f;
                ctx.r.fillRect({endX, ly - b.lines.back().h + 2.f, 7.f, ctx.r.lineHeight(kBody) - 2.f}, alpha(c.accent, 160));
            }
            paintIndicator(ctx, {tx, ly + 2.f, bb.w - 2 * kPad, 22.f});
        }
        if (b.card.h > 0.f) paintCard(ctx, b, {bb.x + b.card.x, top + b.card.y, b.card.w, b.card.h});
    }
    // L'accueil et les suggestions.
    const bool empty = !conv || conv->messages.empty();
    if (!suggestionRects_.empty()) {
        if (empty) {
            float y = bb.y + 16.f - offset_;
            ui::drawIcon(ctx.r, ui::Icon::Assistant, {bb.x + kPad + 2.f, y, 22.f, 22.f}, c.accent);   // le haut : 10 + 6 (measure)
            bold(ctx.r, {bb.x + kPad + 32.f, y + 2.f}, "Bonjour.", kHead, c.text);
            y += 30.f;
            for (const auto& l : layoutText(welcomeText(), bb.w - 2 * kPad - 12.f)) {
                for (const auto& r : l.runs) {
                    if (r.style == 1) bold(ctx.r, {bb.x + kPad + 4.f + r.x, y}, r.text, kBody, c.textMuted);
                    else ctx.r.drawText({bb.x + kPad + 4.f + r.x, y}, r.text, kBody, c.textMuted);
                }
                y += l.h;
            }
        }
        ctx.r.drawText({bb.x + kPad + 4.f, bb.y + suggestionsTop_ - offset_}, "ESSAYEZ", kMeta, c.textMuted);
        const auto& s = demoSuggestions();
        for (std::size_t i = 0; i < suggestionRects_.size() && i < s.size(); ++i) {
            const auto r0 = suggestionRects_[i];
            const gfx::Rect r{bb.x + r0.x, bb.y + r0.y - offset_, r0.w, r0.h};
            if (r.y > bb.bottom() || r.bottom() < bb.y) continue;
            ctx.r.fillRoundedRect(r, static_cast<int>(i) == hoverSuggestion_ ? ctx.theme.brand.hover : c.inputBg, 6.f);
            ctx.r.strokeRect(r, static_cast<int>(i) == hoverSuggestion_ ? c.accent : c.border, 1.f);
            ui::drawIcon(ctx.r, ui::Icon::Play, {r.x + 7.f, r.y + 7.f, 12.f, 12.f}, c.accent);
            const auto lines = layoutText(s[i], r.w - 26.f);
            float ly = r.y + 5.f;
            for (std::size_t k = 0; k < lines.size() && k < 2; ++k) {
                std::string t;
                for (std::size_t q = 0; q < lines[k].runs.size(); ++q) t += (q ? " " : "") + lines[k].runs[q].text;
                if (k == 1 && lines.size() > 2) t = elide(ctx.r, t + " \xE2\x80\xA6", kBody, r.w - 30.f);
                ctx.r.drawText({r.x + 24.f, ly}, t, kBody, c.text);
                ly += ui::lineHeight(kBody) + 4.f;
            }
        }
    }
    ctx.r.popClip();
    bar_.paint(ctx, bb, content_, viewHeight(), offset_);
}

ui::EventResult AiChatView::onEvent(const ui::InputEvent& ev) {
    if (bar_.handle(*this, ev, bounds(), content_, viewHeight(), offset_)) {
        stick_ = atBottom();
        invalidateLayout();
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos) && !w->mods.ctrl) {
        scrollBy(-w->dy * 48.f);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        int h = -1;
        for (std::size_t i = 0; i < suggestionRects_.size(); ++i)
            if (suggestionRect(i).contains(m->pos)) h = static_cast<int>(i);
        if (h != hoverSuggestion_) {
            hoverSuggestion_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        for (std::size_t i = 0; i < suggestionRects_.size(); ++i)
            if (suggestionRect(i).contains(d->pos)) {
                (void)panel_.clickSuggestion(i);
                return ui::EventResult::Consumed;
            }
        // Un clic sur une carte (hors de ses boutons) : l'apercu.
        for (const auto& b : blocks_)
            if (b.card.h > 0.f && cardRect(b.message).contains(d->pos) && d->clickCount >= 2) {
                panel_.openPreview(b.message);
                return ui::EventResult::Consumed;
            }
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
