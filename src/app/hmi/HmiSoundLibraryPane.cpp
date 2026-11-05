// =============================================================================
//  app/hmi/HmiSoundLibraryPane.cpp - 1.24.0 : la bibliotheque de sons (voir HmiSoundLibraryPane.hpp)
// =============================================================================
#include "HmiSoundLibraryPane.hpp"

#include "../FileViewer.hpp"        // l'apercu des fichiers : la forme d'onde, Lire
#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <filesystem>   // 1.24.1 : le fichier d'un de Mes sons est-il la (Retirer)

namespace app {

using hmi::Id;
using hmi::kNoId;
using namespace hmikit;
namespace sl = hmi::soundlib;

// ---- la fiche du son choisi : ses renseignements, puis l'apercu (la forme d'onde, Lire) ----------------------------------
class HmiSoundDetail final : public ui::Widget {
public:
    explicit HmiSoundDetail(std::string id) : ui::Widget(std::move(id)), viewer_(std::make_unique<FileViewer>()) {
        setFocusPolicy(true);
        viewer_->setShowHeader(false);
        viewer_->setActions(false, false);
        viewer_->setEmptyText("Choisis un son pour l'\xC3\xA9" "couter.");
    }
    void show(const sl::Sound* s, std::string inProject) {
        sound_ = s ? *s : sl::Sound{};
        has_ = s != nullptr;
        inProject_ = std::move(inProject);
        if (!s || !s->data) viewer_->clear();
        else viewer_->show(fileview::fromBytes(sl::resourceName(*s), s->data, s->mine() ? "Mes sons" : "Biblioth\xC3\xA8que de sons"));
        invalidate();
    }
    bool togglePlay(std::string* why) {
        const bool ok = viewer_->togglePlay(why);
        invalidate();
        return ok;
    }
    [[nodiscard]] FileViewer& viewer() noexcept { return *viewer_; }

protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && bounds().contains(m->pos)) grabFocus();
        if (std::holds_alternative<ui::KeyDown>(ev) && !focused()) return ui::EventResult::Ignored;
        if (area_.w > 0 && viewer_->event(ev, area_)) {
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.y, 1.f, b.h}, c.border);
        if (!has_) {
            area_ = {};
            ctx.r.drawText({b.x + 16, b.y + 16}, "Choisis un son pour l'\xC3\xA9" "couter.", ctx.theme.font.ui, c.textMuted);
            return;
        }
        ctx.r.pushClip(b);
        float y = b.y + 14.f;
        ctx.r.drawText({b.x + 16, y}, sound_.label, ctx.theme.font.uiBold, c.text);
        y += ctx.r.lineHeight(ctx.theme.font.uiBold) + 4.f;
        const std::string facts = sound_.category + "  \xC2\xB7  " + sl::durationText(sound_.seconds) + "  \xC2\xB7  " + sound_.format
                                + (sound_.mine() ? "  \xC2\xB7  un de tes sons" : "  \xC2\xB7  son natif (fabriqu\xC3\xA9 par le logiciel)");
        ctx.r.drawText({b.x + 16, y}, facts, ctx.theme.font.smallUi, c.textMuted);
        y += ctx.r.lineHeight(ctx.theme.font.smallUi) + 8.f;
        // La description, coupee en lignes a la largeur.
        {
            const float maxW = std::max(40.f, b.w - 32.f);
            std::string line, word;
            const auto flush = [&] {
                if (line.empty()) return;
                ctx.r.drawText({b.x + 16, y}, line, ctx.theme.font.ui, c.text);
                y += ctx.r.lineHeight(ctx.theme.font.ui) + 2.f;
                line.clear();
            };
            const std::string text = sound_.description + " ";
            for (const char ch : text) {
                if (ch != ' ') { word += ch; continue; }
                const std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && ctx.r.measure(candidate, ctx.theme.font.ui).width > maxW) {
                    flush();
                    line = word;
                } else {
                    line = candidate;
                }
                word.clear();
            }
            flush();
        }
        y += 6.f;
        ctx.r.drawText({b.x + 16, y}, inProject_.empty() ? std::string("Dans le projet : pas encore (Importer dans le projet)")
                                                         : "Dans le projet : la ressource " + inProject_,
                       ctx.theme.font.smallUi, inProject_.empty() ? c.textMuted : c.ok);
        y += ctx.r.lineHeight(ctx.theme.font.smallUi) + 10.f;
        ctx.r.popClip();
        const gfx::Rect area{b.x + 12, y, std::max(0.f, b.w - 24), std::max(60.f, b.bottom() - y - 12)};
        area_ = area;
        viewer_->paint(ctx.r, area, FileViewer::paletteOf(ctx.theme), FileViewer::fontsOf(ctx.theme));
        if (viewer_->animating()) invalidate();
    }

private:
    std::unique_ptr<FileViewer> viewer_;
    sl::Sound                   sound_;
    bool                        has_{false};
    std::string                 inProject_;
    gfx::Rect                   area_{};
};

// =============================================================================================================== le volet ===
HmiSoundLibraryPane::HmiSoundLibraryPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActPlay, HmiGlyph::Play, "\xC3\x89" "couter le son choisi, ou l'arr\xC3\xAA" "ter (Espace, double-clic)", "\xC3\x89" "couter");
    tools->add(ActImport, HmiGlyph::Import,
               "Importer dans le projet (Entr\xC3\xA9" "e) : le son devient une ressource, \xC3\xA0 choisir dans une alarme, l'action Jouer un "
               "son ou un script (Ctrl+Z la retire)",
               "Importer dans le projet");
    // 1.25.0 : dans la palette de l'editeur de vues seulement (setCompact) - le son choisi, joue au clic de l'objet choisi.
    tools->add(ActAttach, HmiGlyph::Button,
               "Jouer au clic de l'objet choisi : le son est import\xC3\xA9 s'il ne l'est pas, puis l'objet choisi dans la vue le "
               "joue quand on clique dessus (une action Jouer un son ; Ctrl+Z la retire)",
               "Au clic");
    tools->separator();
    tools->add(ActAdd, HmiGlyph::Plus, "Ajouter un fichier WAV ou MP3 \xC3\xA0 Mes sons (gard\xC3\xA9s hors du projet, pour tous tes projets)",
               "Ajouter \xC3\xA0 Mes sons");
    tools->add(ActRemove, HmiGlyph::Delete, "Retirer le son choisi de Mes sons (Suppr) - un son natif se garde", "Retirer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // 1.24.1 : plusieurs sons choisis - Retirer les retire tous de Mes sons (les natifs se gardent, le compte rendu le
    // dit ; un seul Ctrl+Z les rend) ; Ecouter, Importer : un a la fois.
    tools_->setEnabledWhen(ActPlay, [this] { return selectedSound() != nullptr && selectedIds().size() <= 1; });
    tools_->setEnabledWhen(ActImport, [this] { return selectedSound() != nullptr && selectedIds().size() <= 1; });
    tools_->setEnabledWhen(ActRemove, [this] { const auto* s = selectedSound(); return (s && s->mine()) || selectedIds().size() > 1; });
    tools_->setLabelWhen(ActRemove, [this] { return several::label("Retirer", selectedIds().size()); });
    tools_->setVisibleWhen(ActAttach, [this] { return compact_; });
    tools_->setEnabledWhen(ActAttach, [this] {
        if (!selectedSound() || selectedIds().size() > 1 || !hosts_.chosenObjects) return false;
        const auto chosen = hosts_.chosenObjects();
        return chosen.first != kNoId && !chosen.second.empty();
    });
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Chercher un son\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 dans le nom du son, sa cat\xC3\xA9gorie, sa description et ses mots-cl\xC3\xA9s (sir\xC3\xA8ne, "
        "acquittement, porte...), sans casse ni accents.")));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Son", 260.f}, {"Dur\xC3\xA9" "e", 70.f}, {"Dans le projet", 170.f}, {"Description", 420.f}});
    table->setSelectionMode(ui::SelectionMode::Extended);   // 1.24.1 : plusieurs sons a retirer d'un coup
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    tools_->bindListKeys(*table_, {ActRemove, 0, 0});
    detail_ = &static_cast<HmiSoundDetail&>(addChild(std::make_unique<HmiSoundDetail>(base + ".detail")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) showDetail();
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (r < lines_.size() && lines_[r].header) setCategoryOpen(lines_[r].category, closed_.count(lines_[r].category) != 0);
        else (void)togglePlay();
    });
    links_ += table_->expanderClicked->connect([this](ui::RowIndex r) {
        if (r < lines_.size() && lines_[r].header) setCategoryOpen(lines_[r].category, closed_.count(lines_[r].category) != 0);
    });
    table_->setExtraContextItems([this] { return contextItems(); });
    table_->setExtraContextOnly(true);
    links_ += table_->contextAction->connect([this](int a) { runAction(a); });
    const auto keys = table_->keyHandler();
    table_->setKeyHandler([this, keys](const ui::KeyDown& k) {
        const bool plain = !k.mods.ctrl && !k.mods.alt && !k.mods.shift;
        if (k.key == ui::Key::Space && plain && selectedSound()) { runAction(ActPlay); return true; }
        if (k.key == ui::Key::Return && plain && selectedSound()) { runAction(ActImport); return true; }
        if (k.key == ui::Key::C && k.mods.ctrl && !k.mods.alt && !k.mods.shift && selectedSound()) { runAction(ActCopy); return true; }
        return keys ? keys(k) : false;
    });
    links_ += search_->changed->connect([this] { setSearch(search_->text()); });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

HmiSoundLibraryPane::~HmiSoundLibraryPane() = default;

void HmiSoundLibraryPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiSoundLibraryPane::fail(std::string text, std::string* why) {
    say(text, true);
    if (why) *why = std::move(text);
    return false;
}

void HmiSoundLibraryPane::syncMine() {
    if (mineRevision_ == sl::userRevision() && !sounds_.empty()) return;
    mineRevision_ = sl::userRevision();
    sounds_ = sl::userSounds();
    for (const auto& s : sl::builtIn()) sounds_.push_back(s);
}

const sl::Sound* HmiSoundLibraryPane::byId(const std::string& id) const {
    for (const auto& s : sounds_)
        if (s.id == id) return &s;
    return nullptr;
}

std::string HmiSoundLibraryPane::selectedId() const {
    const auto rows = table_->selectedModelRows();
    if (rows.empty() || rows.front() >= lines_.size()) return {};
    const auto& l = lines_[rows.front()];
    return l.sound >= 0 && static_cast<std::size_t>(l.sound) < sounds_.size() ? sounds_[static_cast<std::size_t>(l.sound)].id : std::string{};
}

const sl::Sound* HmiSoundLibraryPane::selectedSound() const { return byId(selectedId()); }

void HmiSoundLibraryPane::setCategoryOpen(const std::string& category, bool open) {
    if (open) closed_.erase(category);
    else closed_.insert(category);
    rebuild();
}

bool HmiSoundLibraryPane::categoryOpen(const std::string& category) const { return closed_.count(category) == 0; }

void HmiSoundLibraryPane::setSearch(const std::string& text) {
    query_ = text;
    if (search_ && search_->text() != text) search_->setText(text);
    rebuild();
}

void HmiSoundLibraryPane::refresh() {
    syncMine();
    rebuild();
}

void HmiSoundLibraryPane::rebuild() {
    refreshing_ = true;
    const std::string keep = selectedId();
    syncMine();
    const auto& p = doc_->project;
    lines_.clear();
    std::vector<std::string> categories{std::string(sl::kMine)};
    for (const auto& c : sl::builtInCategories()) categories.push_back(c);
    const bool searching = !trimmed(query_).empty();
    for (const auto& cat : categories) {
        std::vector<int> found;
        for (std::size_t i = 0; i < sounds_.size(); ++i)
            if (sounds_[i].category == cat && (!searching || sl::matches(sounds_[i], query_))) found.push_back(static_cast<int>(i));
        // Mes sons : toujours la (une ligne pour dire comment en ajouter) ; une categorie sans resultat de recherche : cachee.
        if (found.empty() && (searching || cat != sl::kMine)) continue;
        lines_.push_back({true, cat, found.size(), -1});
        if (closed_.count(cat) && !searching) continue;   // une recherche deplie ce qui trouve
        for (const int i : found) lines_.push_back({false, cat, 0, i});
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<int> kinds;   // 0 dossier, 1 natif, 2 a toi
    std::vector<int> expanders;
    std::vector<bool> inProject;
    for (const auto& l : lines_) {
        if (l.header && compact_) {   // 1.25.0 : la palette - le dossier et son nombre
            rows.push_back({l.category + "  (" + std::to_string(l.count) + ")", {}, {}});
            kinds.push_back(0);
            expanders.push_back(searching ? 1 : closed_.count(l.category) ? 0 : 1);
            inProject.push_back(false);
            continue;
        }
        if (l.header) {
            rows.push_back({l.category + "  (" + std::to_string(l.count) + ")", {}, {},
                            l.category == sl::kMine && l.count == 0 ? std::string("Ajoute un fichier WAV ou MP3 (+), ou l\xC3\xA2" "che-le ici")
                                                                    : std::string{}});
            kinds.push_back(0);
            expanders.push_back(searching ? 1 : closed_.count(l.category) ? 0 : 1);
            inProject.push_back(false);
            continue;
        }
        const auto& s = sounds_[static_cast<std::size_t>(l.sound)];
        const hmi::Resource* r = sl::importedAs(p, s);
        if (compact_) rows.push_back({s.label, sl::durationText(s.seconds), r ? std::string("\xE2\x9C\x93") : std::string{}});   // 1.25.0
        else rows.push_back({s.label, sl::durationText(s.seconds), r ? "\xE2\x9C\x93 " + r->name : std::string("\xE2\x80\x94"), s.description});
        kinds.push_back(s.mine() ? 2 : 1);
        expanders.push_back(-1);
        inProject.push_back(r != nullptr);
    }
    auto heads = compact_ ? std::vector<std::string>{"Son", "Dur\xC3\xA9" "e", "Projet"}
                          : std::vector<std::string>{"Son", "Dur\xC3\xA9" "e", "Dans le projet", "Description"};
    model_ = std::make_shared<Rows>(std::move(heads), std::move(rows),
                                    [kinds, expanders, inProject](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle st;
                                        if (r >= kinds.size()) return st;
                                        if (kinds[r] == 0) {
                                            st.bold = c == 0;
                                            st.expander = c == 0 ? expanders[r] : -1;
                                            st.icon = c == 0 ? ui::Icon::Folder : ui::Icon::None;
                                            st.iconTone = ui::Tone::Warning;
                                            if (c == 3) st.fgTone = ui::Tone::Muted;
                                            return st;
                                        }
                                        if (c == 0) {
                                            st.indent = 18.f;
                                            st.icon = kinds[r] == 1 ? ui::Icon::Lock : ui::Icon::User;   // natif : verrouille ; a toi
                                            st.iconTone = kinds[r] == 1 ? ui::Tone::Muted : ui::Tone::Accent;
                                        }
                                        if (c == 1) st.monospace = true;
                                        if (c == 2) st.fgTone = inProject[r] ? ui::Tone::Ok : ui::Tone::Muted;
                                        if (c == 3) st.fgTone = ui::Tone::Muted;
                                        return st;
                                    });
    table_->setModel(model_);
    bool kept = false;
    for (std::size_t i = 0; i < lines_.size() && !keep.empty(); ++i)
        if (lines_[i].sound >= 0 && sounds_[static_cast<std::size_t>(lines_[i].sound)].id == keep) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            kept = true;
        }
    if (!kept)
        for (std::size_t i = 0; i < lines_.size(); ++i)
            if (lines_[i].sound >= 0) {
                table_->selectModelRows({static_cast<ui::RowIndex>(i)});
                break;
            }
    refreshing_ = false;
    showDetail();
    std::size_t mine = 0;
    for (const auto& s : sounds_) mine += s.mine() ? 1u : 0u;
    if (compact_)   // 1.25.0 : la palette - le mode d'emploi, court
        status_->setMessage(std::to_string(sl::builtIn().size() + mine) + " sons \xC2\xB7 Espace : \xC3\xA9" "couter \xC2\xB7 Entr\xC3\xA9" "e : importer"
                                " \xC2\xB7 Au clic : l'objet choisi le joue",
                            ui::StatusBar::Severity::Info);
    else
    status_->setMessage(std::to_string(sl::builtIn().size()) + " sons natifs, " + std::to_string(mine)
                            + " \xC3\xA0 toi \xC2\xB7 Espace : \xC3\xA9" "couter \xC2\xB7 Entr\xC3\xA9" "e : importer dans le projet",
                        ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiSoundLibraryPane::showDetail() {
    const sl::Sound* s = selectedSound();
    const hmi::Resource* r = s ? sl::importedAs(doc_->project, *s) : nullptr;
    detail_->show(s, r ? r->name : std::string{});
}

bool HmiSoundLibraryPane::select(const std::string& soundId) {
    syncMine();
    const sl::Sound* s = byId(soundId);
    if (!s) return false;
    if (closed_.count(s->category)) setCategoryOpen(s->category, true);
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].sound >= 0 && sounds_[static_cast<std::size_t>(lines_[i].sound)].id == soundId) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            showDetail();
            return true;
        }
    return false;
}

bool HmiSoundLibraryPane::togglePlay(std::string* why) {
    const sl::Sound* s = selectedSound();
    if (!s) return fail("Choisis un son pour l'\xC3\xA9" "couter.", why);
    std::string error;
    if (!detail_->togglePlay(&error)) return fail("Lecture impossible : " + (error.empty() ? std::string("pas de sortie son") : error), why);
    say("\xC3\x89" "coute : " + s->label + " (" + sl::durationText(s->seconds) + ")");
    return true;
}

Id HmiSoundLibraryPane::importSound(const std::string& soundId, std::string* why) {
    syncMine();
    const sl::Sound* s = byId(soundId);
    if (!s) {
        (void)fail("Choisis un son \xC3\xA0 importer.", why);
        return kNoId;
    }
    const sl::Sound copy = *s;
    if (const hmi::Resource* r = sl::importedAs(doc_->project, copy)) {
        say(copy.label + " est d\xC3\xA9j\xC3\xA0 dans le projet : la ressource " + r->name + ".");
        return r->id;
    }
    sl::Imported made;
    auto cmd = hmi::changeProject(doc_, "Importer le son " + copy.label, [&](hmi::Project& p) { made = sl::importSound(p, copy); });
    if (!cmd) {
        (void)fail("non import\xC3\xA9", why);
        return kNoId;
    }
    apply_(std::move(cmd));
    refresh();
    (void)select(copy.id);
    say(copy.label + " import\xC3\xA9 : la ressource " + made.name
        + " - choisis-la dans une alarme, l'action Jouer un son ou un script (Ctrl+Z la retire).");
    return made.id;
}

bool HmiSoundLibraryPane::addFile(const std::string& path, std::string* why) {
    std::string stored, error;
    bool existed = false;
    if (!sl::addUserFile(path, &stored, &existed, &error)) return fail("Mes sons : " + error, why);
    refresh();
    (void)select("mes/" + stored);
    say(existed ? "D\xC3\xA9j\xC3\xA0 dans Mes sons : " + stored : "Ajout\xC3\xA9 \xC3\xA0 Mes sons : " + stored + " (pour tous tes projets).");
    return true;
}

namespace {
// 1.24.1 : RETIRER UN SON DE MES SONS, CTRL+Z LE REND. La commande garde son contenu : l'annuler le rend a Mes sons sous
// son nom (le nom pris entretemps par un autre contenu : "nom (2).wav", la regle de Mes sons). Plusieurs retires d'un
// coup font un groupe : un seul Ctrl+Z les rend.
class MySoundRemoveCommand final : public core::ICommand {
public:
    MySoundRemoveCommand(sl::Sound sound, std::function<void()> refresh) : sound_(std::move(sound)), refresh_(std::move(refresh)) {}
    core::Status execute() override {
        std::string why;
        if (!sl::removeUserSound(sound_, &why)) return core::fail(core::ErrorCode::FileUnreadable, why);
        if (refresh_) refresh_();
        return core::ok();
    }
    core::Status undo() override {
        const auto slash = sound_.file.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? sound_.file : sound_.file.substr(slash + 1);
        std::string stored, why;
        if (!sound_.data || !sl::addUserSound(name, *sound_.data, &stored, nullptr, &why))
            return core::fail(core::ErrorCode::FileUnreadable, "rendre " + sound_.label + " : " + (why.empty() ? std::string("contenu perdu") : why));
        // Son fichier, pour le refaire (Ctrl+Y).
        if (slash != std::string::npos) sound_.file = sound_.file.substr(0, slash + 1) + stored;
        if (refresh_) refresh_();
        return core::ok();
    }
    [[nodiscard]] std::string label() const override { return "Retirer " + sound_.label + " de Mes sons"; }

private:
    sl::Sound             sound_;
    std::function<void()> refresh_;
};
} // namespace

std::vector<std::string> HmiSoundLibraryPane::selectedIds() const {
    std::vector<std::string> out;
    auto rows = table_->selectedModelRows();
    std::sort(rows.begin(), rows.end());
    for (const auto r : rows)
        if (r < lines_.size() && lines_[r].sound >= 0 && static_cast<std::size_t>(lines_[r].sound) < sounds_.size())
            out.push_back(sounds_[static_cast<std::size_t>(lines_[r].sound)].id);
    return out;
}

bool HmiSoundLibraryPane::selectIds(const std::vector<std::string>& ids) {
    syncMine();
    std::vector<ui::RowIndex> rows;
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].sound >= 0 && std::find(ids.begin(), ids.end(), sounds_[static_cast<std::size_t>(lines_[i].sound)].id) != ids.end())
            rows.push_back(static_cast<ui::RowIndex>(i));
    table_->selectModelRows(rows);
    showDetail();
    return rows.size() == ids.size();
}

// 1.24.1 : PLUSIEURS D'UN COUP - les sons choisis ; un natif se garde (le compte rendu le dit), les tiens partent ; un
// seul Ctrl+Z les rend (comme un seul, sans question : le contenu est garde par la commande).
bool HmiSoundLibraryPane::removeSelected(std::string* why) {
    const auto ids = selectedIds();
    if (ids.size() > 1) {
        several::Batch b;
        b.verb = "Retirer";
        b.noun = "son";
        b.nouns = "sons";
        for (const auto& id : ids) {
            const sl::Sound* s = byId(id);
            b.names.push_back(s ? s->label : id);
        }
        b.remove = [this, ids](std::size_t i, std::string* no) {
            const sl::Sound* s = byId(ids[i]);
            if (!s) {
                if (no) *no = "introuvable";
                return false;
            }
            if (!s->mine()) {
                if (no) *no = "son natif : il se garde";
                return false;
            }
            return removeOne(*s, no);
        };
        b.after = [this] { refresh(); };
        b.report = [this](const std::string& text, bool warning) {
            say(text + " (les projets qui les ont import\xC3\xA9s gardent leur ressource)", warning);
        };
        b.alive = table_->selectionChanged;
        several::run(std::move(b));
        return true;
    }
    const sl::Sound* s = selectedSound();
    if (!s) return fail("Choisis un de tes sons.", why);
    if (!s->mine()) return fail(s->label + " est un son natif : il se garde (seuls Mes sons se retirent).", why);
    const std::string label = s->label;
    std::string error;
    if (!removeOne(*s, &error)) return fail(error, why);
    refresh();
    say(label + " retir\xC3\xA9 de Mes sons (les projets qui l'ont import\xC3\xA9 gardent leur ressource ; Ctrl+Z le rend).");
    return true;
}

// Un de Mes sons, par une commande (Ctrl+Z le rend) ; sans pile (un essai sans hote) : tout de suite.
bool HmiSoundLibraryPane::removeOne(const sl::Sound& s, std::string* why) {
    const std::weak_ptr<const void> alive = table_->selectionChanged;
    auto cmd = std::make_unique<MySoundRemoveCommand>(s, [this, alive] {
        if (!alive.expired()) refresh();
    });
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(s.file.begin(), s.file.end())), ec)) {
        if (why) *why = "fichier introuvable : " + s.file;
        return false;
    }
    if (apply_) {
        apply_(std::move(cmd));
        return true;
    }
    if (const auto st = cmd->execute(); !st) {
        if (why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, std::size_t>> HmiSoundLibraryPane::shownCategories() const {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const auto& l : lines_)
        if (l.header) out.emplace_back(l.category, l.count);
    return out;
}

std::vector<std::string> HmiSoundLibraryPane::shownIds() const {
    std::vector<std::string> out;
    for (const auto& l : lines_)
        if (l.sound >= 0) out.push_back(sounds_[static_cast<std::size_t>(l.sound)].id);
    return out;
}

std::vector<ui::PopupMenu::Item> HmiSoundLibraryPane::contextItems() const {
    std::vector<ui::PopupMenu::Item> items;
    const sl::Sound* s = selectedSound();
    const hmi::Resource* r = s ? sl::importedAs(doc_->project, *s) : nullptr;
    const auto add = [&](int action, std::string label, std::string key, ui::Icon icon, bool enabled, std::string whyNot = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.shortcut = std::move(key);
        it.icon = icon;
        it.enabled = enabled;
        it.disabledReason = std::move(whyNot);
        it.id = action;
        items.push_back(std::move(it));
    };
    const auto sep = [&] {
        ui::PopupMenu::Item it;
        it.separator = true;
        items.push_back(it);
    };
    // 1.24.1 : plusieurs sons choisis - Retirer les N ; le reste, un a la fois.
    const auto chosen = selectedIds();
    const bool many = chosen.size() > 1;
    if (many) {
        ui::PopupMenu::Item head;
        head.heading = true;
        std::size_t mine = 0;
        for (const auto& id : chosen)
            if (const auto* x = byId(id); x && x->mine()) ++mine;
        head.label = std::to_string(chosen.size()) + " sons";
        head.shortcut = std::to_string(mine) + " \xC3\xA0 toi";
        items.push_back(std::move(head));
    } else if (s) {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = s->label;
        head.shortcut = s->mine() ? "un de tes sons" : "son natif";
        items.push_back(std::move(head));
    }
    const std::string none = many ? several::kOneAtATime : std::string("choisis un son");
    add(ActPlay, "\xC3\x89" "couter", "Espace", ui::Icon::Play, s != nullptr && !many, none);
    add(ActImport, r && !many ? "D\xC3\xA9j\xC3\xA0 dans le projet (" + r->name + ")" : std::string("Importer dans le projet"), "Entr\xC3\xA9" "e",
        ui::Icon::Import, s != nullptr && !r && !many, r && !many ? std::string("le m\xC3\xAAme son est d\xC3\xA9j\xC3\xA0 une ressource") : none);
    add(ActGoTo, "Aller \xC3\xA0 la ressource", {}, ui::Icon::GoTo, r != nullptr && !many && static_cast<bool>(hosts_.openResource),
        many ? none : r ? std::string("indisponible ici") : std::string("pas encore dans le projet"));
    add(ActCopy, "Copier le nom", "Ctrl+C", ui::Icon::Copy, s != nullptr && !many, none);
    if (compact_) {   // 1.25.0 : la palette de l'editeur de vues
        bool any = false;
        if (hosts_.chosenObjects) {
            const auto chosen = hosts_.chosenObjects();
            any = chosen.first != kNoId && !chosen.second.empty();
        }
        add(ActAttach, "Jouer au clic de l'objet choisi", {}, ui::Icon::Play, s != nullptr && !many && any,
            many ? none : s ? std::string("choisis d'abord un objet dans la vue") : none);
    }
    sep();
    add(ActAdd, "Ajouter un fichier \xC3\xA0 Mes sons\xE2\x80\xA6", {}, ui::Icon::Plus, true);
    add(ActReveal, "Ouvrir dans l'explorateur", {}, ui::Icon::Open, s && s->mine() && !many,
        many ? none : s ? std::string("un son natif n'est pas un fichier : il est fabriqu\xC3\xA9 par le logiciel") : none);
    add(ActRemove, many ? several::label("Retirer", chosen.size()) + " de Mes sons" : std::string("Retirer de Mes sons"), "Suppr", ui::Icon::Delete,
        many || (s && s->mine()), s ? std::string("un son natif se garde") : none);
    return items;
}

void HmiSoundLibraryPane::runAction(int action) {
    const sl::Sound* s = selectedSound();
    // 1.24.1 : plusieurs sons choisis - Ecouter, Importer, Copier, Ouvrir, Aller a : un a la fois (Espace, Entree aussi).
    if (selectedIds().size() > 1 && (action == ActPlay || action == ActImport || action == ActCopy || action == ActReveal || action == ActGoTo)) {
        say("Plusieurs sons choisis : " + several::kOneAtATime + ".", true);
        return;
    }
    switch (action) {
        case ActPlay: (void)togglePlay(); break;
        case ActImport: (void)importSelected(); break;
        case ActAdd:
            if (hosts_.askFile) hosts_.askFile();
            else {
                ui::FilePick pick;
                pick.title = "Ajouter un son \xC3\xA0 Mes sons";
                pick.filters = {{"Sons (WAV, MP3)", "wav;mp3"}};
                if (!ui::pickFile(pick, [this](std::string path) {
                        if (!path.empty()) (void)addFile(path);
                    }))
                    say("Choisir un fichier n'est pas possible ici : l\xC3\xA2" "che un fichier WAV ou MP3 sur la liste.", true);
            }
            break;
        case ActRemove: (void)removeSelected(); break;
        case ActCopy:
            if (s) {
                ui::setClipboardText(s->mine() ? sl::resourceName(*s) : s->label);
                say("\xC2\xAB " + (s->mine() ? sl::resourceName(*s) : s->label) + " \xC2\xBB copi\xC3\xA9.");
            }
            break;
        case ActReveal:
            if (s && s->mine()) {
                const std::string why = hosts_.reveal ? hosts_.reveal(s->file) : std::string("indisponible ici");
                if (why.empty()) say("Ouvert dans l'explorateur : " + s->file);
                else say(why, true);
            }
            break;
        case ActGoTo:
            if (s)
                if (const hmi::Resource* r = sl::importedAs(doc_->project, *s); r && hosts_.openResource) hosts_.openResource(r->id);
            break;
        case ActAttach: (void)attachToChosen(); break;   // 1.25.0
        default: break;
    }
}

void HmiSoundLibraryPane::setCompact(bool on) {
    if (compact_ == on) return;
    compact_ = on;
    search_->setVisibility(on ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    detail_->setVisibility(on ? ui::Visibility::Collapsed : ui::Visibility::Visible);   // il joue quand meme (Ecouter)
    if (on) table_->setColumns({{"Son", 200.f}, {"Dur\xC3\xA9" "e", 60.f}, {"Projet", 52.f}});
    else table_->setColumns({{"Son", 260.f}, {"Dur\xC3\xA9" "e", 70.f}, {"Dans le projet", 170.f}, {"Description", 420.f}});
    rebuild();
    invalidateLayout();
}

std::size_t HmiSoundLibraryPane::attachToChosen(std::string* why) {
    syncMine();
    const sl::Sound* s = selectedSound();
    if (!s) {
        (void)fail("Choisis un son.", why);
        return 0;
    }
    if (!hosts_.chosenObjects) {
        (void)fail("Ici, pas de vue : ouvre une vue dans l'\xC3\xA9" "diteur.", why);
        return 0;
    }
    const auto [view, objects] = hosts_.chosenObjects();
    if (view == kNoId || objects.empty()) {
        (void)fail("Choisis d'abord un objet dans la vue : il jouera ce son quand on clique dessus.", why);
        return 0;
    }
    const sl::Sound copy = *s;
    std::size_t done = 0;
    std::string resource;
    auto cmd = hmi::changeProject(doc_, "Jouer le son " + copy.label + " au clic", [&](hmi::Project& p) {
        if (const hmi::Resource* r = sl::importedAs(p, copy)) resource = r->name;
        else resource = sl::importSound(p, copy).name;
        auto* v = p.view(view);
        if (!v) return;
        for (const Id id : objects)
            if (auto* o = v->object(id)) {
                hmi::Action a;
                a.trigger = hmi::Trigger::Click;
                a.operation = hmi::Operation::PlaySound;
                a.target = resource;
                o->actions.push_back(std::move(a));
                ++done;
            }
    });
    if (!cmd || done == 0) {
        (void)fail("Rien \xC3\xA0 faire : l'objet choisi n'est plus dans la vue.", why);
        return 0;
    }
    apply_(std::move(cmd));
    refresh();
    (void)select(copy.id);
    say(copy.label + " : " + (done == 1 ? std::string("l'objet choisi le joue") : std::to_string(done) + " objets le jouent")
        + " quand on clique dessus (la ressource " + resource + " ; Ctrl+Z l'annule).");
    return done;
}

void HmiSoundLibraryPane::onLayout() {
    const auto b = bounds();
    if (compact_) {   // 1.25.0 : la palette - la barre, la liste, le mode d'emploi
        const float bar = 34.f, statusH = 24.f;
        tools_->setBounds({b.x, b.y, b.w, bar});
        status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
        table_->setBounds({b.x, b.y + bar, b.w, std::max(0.f, b.h - bar - statusH)});
        return;
    }
    const float bar = 38.f, statusH = 24.f;
    const float searchW = std::clamp(b.w * 0.28f, 180.f, 320.f);
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - searchW - 12.f), bar});
    search_->setBounds({b.x + b.w - searchW - 8.f, b.y + 5.f, searchW, 28.f});
    status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    const float h = std::max(0.f, b.h - bar - statusH);
    const float detailW = std::clamp(b.w * 0.36f, 280.f, 460.f);
    table_->setBounds({b.x, b.y + bar, std::max(0.f, b.w - detailW), h});
    detail_->setBounds({b.x + b.w - detailW, b.y + bar, detailW, h});
}

void HmiSoundLibraryPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

// Un fichier WAV ou MP3 lache sur le volet rejoint Mes sons.
ui::EventResult HmiSoundLibraryPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::FileDropped>(&ev); d && bounds().contains(d->pos)) {
        (void)addFile(d->path);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
