// =============================================================================
//  app/hmi/HmiCorbeillePane.cpp - 1.25.1 : Projet > Corbeille (HmiCorbeillePane.hpp)
// =============================================================================
#include "HmiCorbeillePane.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/PropertyGridHooks.hpp"   // Ouvrir dans l'explorateur (l'element dans sa corbeille)
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <cstdint>

namespace app {

namespace cb = corbeille;
using hmikit::Rows;

namespace {
constexpr int kKinds = static_cast<int>(cb::kKindCount);
// Les pictogrammes des genres (ceux de l'arbre et des volets).
ui::Icon kindIcon(cb::Kind k) {
    switch (k) {
        case cb::Kind::Recording: return ui::Icon::Play;
        case cb::Kind::Incident: return ui::Icon::Warning;
        case cb::Kind::Version: return ui::Icon::History;
        case cb::Kind::Macro: return ui::Icon::Document;
        case cb::Kind::AiConversation: return ui::Icon::Help;
        case cb::Kind::AiAttachment: return ui::Icon::Document;
        case cb::Kind::AiKnowledge: return ui::Icon::Star;
        case cb::Kind::Count: break;
    }
    return ui::Icon::Document;
}
} // namespace

std::string HmiCorbeillePane::purgeNote() {
    return "Les \xC3\xA9l\xC3\xA9ments supprim\xC3\xA9s il y a plus de " + std::to_string(cb::kKeepDays)
         + " jours partent \xC3\xA0 l'ouverture suivante (du projet, ou de l'application pour l'Assistant IA) ; les macros restent "
           "jusqu'\xC3\xA0 Supprimer d\xC3\xA9" "finitivement.";
}

HmiCorbeillePane::HmiCorbeillePane(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ARestore, HmiGlyph::Undo, "Restaurer les \xC3\xA9l\xC3\xA9ments choisis \xC3\xA0 leur place (Entr\xC3\xA9" "e) - un seul Ctrl+Z les remet dans la corbeille",
               "Restaurer");
    tools->add(APurge, HmiGlyph::Delete, "Supprimer d\xC3\xA9" "finitivement les \xC3\xA9l\xC3\xA9ments choisis (Suppr) : une question, sans retour",
               "Supprimer d\xC3\xA9" "finitivement");
    tools->add(AEmpty, HmiGlyph::Delete, "Vider la corbeille : tout ce qu'elle garde part pour de bon (une question)", "Vider la corbeille");
    tools->separator();
    tools->add(AReveal, HmiGlyph::Link, "Ouvrir dans l'explorateur : l'\xC3\xA9l\xC3\xA9ment choisi, dans sa corbeille", "Ouvrir dans l'explorateur");
    tools->add(ARefresh, HmiGlyph::Refresh, "Relire les corbeilles", "Actualiser");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(ARestore, [this] { return !chosen().empty(); });
    tools_->setEnabledWhen(APurge, [this] { return !chosen().empty(); });
    tools_->setEnabledWhen(AEmpty, [this] { return !entries_.empty(); });
    tools_->setEnabledWhen(AReveal, [this] { return chosen().size() == 1; });
    tools_->setLabelWhen(ARestore, [this] { return several::label("Restaurer", chosen().size()); });
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Chercher dans la corbeille\xE2\x80\xA6",
        "Chaque mot cherch\xC3\xA9 commence un mot du nom, du genre ou de l'origine (Nuit, incident, versions, pompe...), sans casse "
        "ni accents.")));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Nom", 360.f}, {"Supprim\xC3\xA9 le", 130.f}, {"Origine", 420.f}, {"Taille", 90.f, 50.f, true, true, true, ui::Align::End}});
    table->setSelectionMode(ui::SelectionMode::Extended);
    table->setPathColumn(2);   // le dossier ou il reviendra
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (r < lines_.size() && lines_[r].header) setKindOpen(lines_[r].kind, closed_.count(static_cast<int>(lines_[r].kind)) != 0);
        else (void)restoreChosen();
    });
    links_ += table_->expanderClicked->connect([this](ui::RowIndex r) {
        if (r < lines_.size() && lines_[r].header) setKindOpen(lines_[r].kind, closed_.count(static_cast<int>(lines_[r].kind)) != 0);
    });
    table_->setExtraContextItems([this] { return contextItems(); });
    links_ += table_->contextAction->connect([this](int a) { runAction(a); });
    table_->setKeyHandler([this](const ui::KeyDown& k) {
        const bool plain = !k.mods.ctrl && !k.mods.alt && !k.mods.shift;
        if (k.key == ui::Key::Return && plain && !chosen().empty()) { runAction(ARestore); return true; }
        if (k.key == ui::Key::Delete && plain && !chosen().empty()) { runAction(APurge); return true; }
        return false;
    });
    links_ += search_->changed->connect([this] { setSearch(search_->text()); });
    refresh();
}

HmiCorbeillePane::~HmiCorbeillePane() = default;

void HmiCorbeillePane::say(std::string text, bool warning) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::string HmiCorbeillePane::summary() const {
    std::uint64_t bytes = 0;
    for (const auto& e : entries_) bytes += e.bytes;
    return cb::summaryText(entries_.size(), bytes);
}

void HmiCorbeillePane::refresh() {
    seen_ = cb::revision();
    // Les choisis d'abord (leurs lignes designent les elements d'avant), puis les elements relus.
    std::vector<std::string> keep;
    for (const auto& e : chosen()) keep.push_back(e.key());
    entries_ = cb::entries();
    lines_.clear();
    rebuild(keep);
    status_->setMessage(summary());
}

void HmiCorbeillePane::setSearch(const std::string& text) {
    query_ = text;
    rebuild();
}

void HmiCorbeillePane::setKindOpen(cb::Kind kind, bool open) {
    if (open) closed_.erase(static_cast<int>(kind));
    else closed_.insert(static_cast<int>(kind));
    rebuild();
}

bool HmiCorbeillePane::kindOpen(cb::Kind kind) const { return closed_.count(static_cast<int>(kind)) == 0; }

void HmiCorbeillePane::rebuild() {
    std::vector<std::string> keep;
    for (const auto& e : chosen()) keep.push_back(e.key());
    rebuild(keep);
}

void HmiCorbeillePane::rebuild(const std::vector<std::string>& keep) {
    lines_.clear();
    const bool searching = !hmikit::trimmed(query_).empty();
    for (int k = 0; k < kKinds; ++k) {
        const auto kind = static_cast<cb::Kind>(k);
        std::vector<int> found;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].kind == kind && (!searching || cb::matches(entries_[i], query_))) found.push_back(static_cast<int>(i));
        if (found.empty()) continue;   // un genre vide (ou sans resultat) : pas de dossier
        lines_.push_back({true, kind, found.size(), -1});
        if (closed_.count(k) && !searching) continue;   // une recherche deplie ce qui trouve
        for (const int i : found) lines_.push_back({false, kind, 0, i});
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<int> expanders, kinds;
    std::vector<bool> heads;
    for (const auto& l : lines_) {
        if (l.header) {
            rows.push_back({cb::kindLabel(l.kind, true) + "  (" + std::to_string(l.count) + ")", {}, {}, {}});
            expanders.push_back(searching ? 1 : closed_.count(static_cast<int>(l.kind)) ? 0 : 1);
            heads.push_back(true);
        } else {
            const auto& e = entries_[static_cast<std::size_t>(l.entry)];
            rows.push_back({e.name, cb::dateText(e.deletedAt), e.originFolder(), cb::sizeText(e.bytes)});
            expanders.push_back(-1);
            heads.push_back(false);
        }
        kinds.push_back(static_cast<int>(l.kind));
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Nom", "Supprim\xC3\xA9 le", "Origine", "Taille"}, std::move(rows),
                                    [heads, expanders, kinds](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle st;
                                        if (r >= heads.size()) return st;
                                        if (heads[r]) {
                                            st.bold = c == 0;
                                            st.expander = c == 0 ? expanders[r] : -1;
                                            st.icon = c == 0 ? ui::Icon::Folder : ui::Icon::None;
                                            st.iconTone = ui::Tone::Warning;
                                            return st;
                                        }
                                        if (c == 0) {
                                            st.indent = 18.f;
                                            st.icon = kindIcon(static_cast<cb::Kind>(kinds[r]));
                                            st.iconTone = ui::Tone::Muted;
                                        }
                                        if (c == 1 || c == 2) st.fgTone = ui::Tone::Muted;
                                        if (c == 3) st.monospace = true;
                                        return st;
                                    });
    table_->setModel(model_);
    std::vector<ui::RowIndex> again;
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (!lines_[i].header && std::find(keep.begin(), keep.end(), entries_[static_cast<std::size_t>(lines_[i].entry)].key()) != keep.end())
            again.push_back(static_cast<ui::RowIndex>(i));
    table_->selectModelRows(again, false);
    invalidate();
}

std::vector<std::pair<std::string, std::size_t>> HmiCorbeillePane::shownGroups() const {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const auto& l : lines_)
        if (l.header) out.emplace_back(cb::kindLabel(l.kind, true), l.count);
    return out;
}

std::vector<std::string> HmiCorbeillePane::shownNames() const {
    std::vector<std::string> out;
    for (const auto& l : lines_)
        if (!l.header) out.push_back(entries_[static_cast<std::size_t>(l.entry)].name);
    return out;
}

std::vector<cb::Entry> HmiCorbeillePane::chosen() const {
    std::vector<cb::Entry> out;
    if (!table_) return out;
    auto rows = table_->selectedModelRows();
    std::sort(rows.begin(), rows.end());
    for (const auto r : rows)
        if (r < lines_.size() && !lines_[r].header && lines_[r].entry >= 0 && static_cast<std::size_t>(lines_[r].entry) < entries_.size())
            out.push_back(entries_[static_cast<std::size_t>(lines_[r].entry)]);
    return out;
}

bool HmiCorbeillePane::choose(const std::vector<std::string>& names) {
    std::vector<ui::RowIndex> rows;
    std::size_t found = 0;
    for (const auto& n : names) {
        bool any = false;
        for (std::size_t i = 0; i < lines_.size(); ++i)
            if (!lines_[i].header && entries_[static_cast<std::size_t>(lines_[i].entry)].name == n) {
                rows.push_back(static_cast<ui::RowIndex>(i));
                any = true;
            }
        found += any ? 1 : 0;
    }
    table_->selectModelRows(rows);
    return found == names.size();
}

std::size_t HmiCorbeillePane::restoreChosen() {
    const auto items = chosen();
    if (items.empty()) return 0;
    std::vector<std::pair<std::string, std::string>> refused;
    const std::size_t n = cb::restore(items, &refused);
    refresh();
    std::vector<std::string> names;
    for (const auto& e : items)
        if (std::none_of(refused.begin(), refused.end(), [&](const auto& r) { return r.first == e.name; })) names.push_back(e.name);
    std::string text = n ? std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments restaur\xC3\xA9s : " : " \xC3\xA9l\xC3\xA9ment restaur\xC3\xA9 : ") + several::names(names)
                         : std::string("Rien de restaur\xC3\xA9");
    if (!refused.empty()) text += " ; " + std::to_string(refused.size()) + " refus\xC3\xA9" + (refused.size() > 1 ? "s" : "") + " : " + several::refusals(refused);
    if (n) text += n > 1 ? " - un seul Ctrl+Z les remet dans la corbeille" : " - Ctrl+Z le remet dans la corbeille";
    say(text + ".", !refused.empty());
    return n;
}

std::size_t HmiCorbeillePane::purgeChosen() {
    const auto items = chosen();
    if (items.empty()) return 0;
    std::vector<std::pair<std::string, std::string>> refused;
    const std::size_t n = cb::purge(items, &refused);
    refresh();
    std::string text = std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments supprim\xC3\xA9s d\xC3\xA9" "finitivement" : " \xC3\xA9l\xC3\xA9ment supprim\xC3\xA9 d\xC3\xA9" "finitivement");
    if (!refused.empty()) text += " ; " + std::to_string(refused.size()) + " refus\xC3\xA9" + (refused.size() > 1 ? "s" : "") + " : " + several::refusals(refused);
    say(text + ".", !refused.empty());
    return n;
}

void HmiCorbeillePane::askPurgeChosen() {
    const auto items = chosen();
    if (items.empty()) return;
    std::vector<std::string> names;
    for (const auto& e : items) names.push_back(e.name);
    const std::size_t n = items.size();
    const std::weak_ptr<const void> alive = table_->selectionChanged;
    several::confirm(n > 1 ? "Supprimer d\xC3\xA9" "finitivement les " + std::to_string(n) + " \xC3\xA9l\xC3\xA9ments ?"
                           : std::string("Supprimer d\xC3\xA9" "finitivement cet \xC3\xA9l\xC3\xA9ment ?"),
                     several::names(names) + (n > 1 ? " quittent" : " quitte") + " la corbeille pour de bon : ni Restaurer ni Ctrl+Z ne "
                         + (n > 1 ? "les rendront." : "le rendront."),
                     several::label("Supprimer", n), [this, alive] {
                         if (!alive.expired()) (void)purgeChosen();
                     });
}

std::size_t HmiCorbeillePane::emptyAll() {
    std::vector<std::pair<std::string, std::string>> refused;
    const std::size_t n = cb::purge(entries_, &refused);
    refresh();
    say(n ? "La corbeille est vid\xC3\xA9" "e : " + std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments partis pour de bon." : " \xC3\xA9l\xC3\xA9ment parti pour de bon.")
          : std::string("La corbeille \xC3\xA9tait vide."),
        !refused.empty());
    return n;
}

void HmiCorbeillePane::askEmpty() {
    if (entries_.empty()) {
        say("La corbeille est d\xC3\xA9j\xC3\xA0 vide.");
        return;
    }
    const std::weak_ptr<const void> alive = table_->selectionChanged;
    several::confirm("Vider la corbeille ?",
                     "Les " + std::to_string(entries_.size()) + " \xC3\xA9l\xC3\xA9ments (" + summary() + ") partent pour de bon : ni Restaurer ni Ctrl+Z ne les rendront.",
                     "Vider la corbeille", [this, alive] {
                         if (!alive.expired()) (void)emptyAll();
                     });
}

std::vector<ui::PopupMenu::Item> HmiCorbeillePane::contextItems() const {
    std::vector<ui::PopupMenu::Item> items;
    const auto picked = chosen();
    if (picked.empty()) return items;
    const auto add = [&](int action, std::string label, ui::Icon icon, bool enabled, std::string why = {}, std::string key = {}) {
        ui::PopupMenu::Item it;
        it.id = action;
        it.label = std::move(label);
        it.icon = icon;
        it.shortcut = std::move(key);
        it.enabled = enabled;
        it.disabledReason = enabled ? std::string{} : std::move(why);
        items.push_back(std::move(it));
    };
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = picked.size() == 1 ? picked.front().name : std::to_string(picked.size()) + " \xC3\xA9l\xC3\xA9ments";
    items.push_back(std::move(head));
    add(ARestore, several::label("Restaurer", picked.size()), ui::Icon::Undo, true, {}, "Entr\xC3\xA9" "e");
    add(APurge, "Supprimer d\xC3\xA9" "finitivement\xE2\x80\xA6", ui::Icon::Delete, true, {}, "Suppr");
    add(AReveal, "Ouvrir dans l'explorateur (dans la corbeille)", ui::Icon::FolderOpen, picked.size() == 1, several::kOneAtATime);
    return items;
}

void HmiCorbeillePane::runAction(int action) {
    switch (action) {
        case ARestore: (void)restoreChosen(); break;
        case APurge: askPurgeChosen(); break;
        case AEmpty: askEmpty(); break;
        case ARefresh:
            cb::touch();
            refresh();
            say("Corbeille relue : " + summary() + ".");
            break;
        case AReveal: {
            const auto picked = chosen();
            if (picked.size() != 1) break;
            const std::string why = ui::paths::run(ui::paths::PathMenu::kFirstId + ui::paths::kReveal, ui::paths::PathMenu::kFirstId, picked.front().trashed);
            if (!why.empty()) say("Ouvrir dans l'explorateur : " + why, true);
            break;
        }
        default: break;
    }
}

void HmiCorbeillePane::onLayout() {
    const auto b = bounds();
    const float bar = 38.f, statusH = 24.f, noteH = 22.f;
    const float searchW = std::clamp(b.w * 0.28f, 180.f, 320.f);
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - searchW - 12.f), bar});
    search_->setBounds({b.x + b.w - searchW - 8.f, b.y + 5.f, searchW, 28.f});
    status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    table_->setBounds({b.x, b.y + bar, b.w, std::max(0.f, b.h - bar - statusH - noteH)});
}

void HmiCorbeillePane::onPaint(const ui::PaintContext& ctx) {
    // La corbeille a change ailleurs (un geste, une suppression dans un autre volet) : relue.
    if (seen_ != cb::revision()) refresh();
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    const auto b = bounds();
    const float y = b.y + b.h - 24.f - 20.f;
    ctx.r.drawText({b.x + 10.f, y}, purgeNote(), gfx::FontId{12}, ctx.theme.color.textMuted);
}

} // namespace app
