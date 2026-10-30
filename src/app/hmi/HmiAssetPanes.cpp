#include "HmiAssetPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"            // 1.14.0 (M4) : prop, tf, yes (les enregistrements)
#include "HmiSound.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <map>
#include <cctype>
#include <cmath>
#include <cstdlib>              // lot API 8 : std::system (ouvrir un document)
#include <filesystem>
#include <initializer_list>

namespace app {

using hmi::Id;
using hmi::kNoId;
using hmi::MediaKind;

namespace {

class RowsModel final : public ui::ITableModel {
public:
    RowsModel(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows,
              std::vector<ui::CellStyle> styles = {}, std::vector<ui::CellStyle> lastStyles = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(styles)), last_(std::move(lastStyles)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        if (c == 0 && r < styles_.size()) return styles_[r];
        if (c + 1 == headers_.size() && r < last_.size()) return last_[r];
        return {};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        return cellText(a, c) < cellText(b, c);
    }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<ui::CellStyle> styles_, last_;
};

// Ce que dit une erreur, sans son code ("invalid argument: ...") : la phrase.
std::string reason(const core::Error& e) { return e.context.empty() ? e.message() : e.context; }

// ---- Lot API 8 : glisser de fichiers, 2e partie ----
//  Ouvrir un fichier avec le programme que le systeme lui associe (comme l'aide :
//  le chemin entre guillemets ; un chemin qui en contient est refuse). Vide : ouvert.
std::string openWithSystem(const std::string& path) {
    if (path.find('"') != std::string::npos) return "chemin refus\xC3\xA9 : " + path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec)) return "fichier absent : " + path;
#if defined(_WIN32)
    const std::string command = "start \"\" \"" + path + "\"";
#elif defined(__APPLE__)
    const std::string command = "open \"" + path + "\"";
#else
    const std::string command = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
#endif
    if (std::system(command.c_str()) != 0) return "aucun programme n'a pu ouvrir " + path;
    return {};
}
// ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

std::string dimensionsOf(const hmi::Resource& r) {
    switch (r.kind()) {
        case MediaKind::Image:
            // Lot 16 : un GIF anime dit aussi la duree d'un tour.
            if (r.format == "GIF" && r.seconds > 0 && r.width > 0)
                return std::to_string(r.width) + " x " + std::to_string(r.height) + ", " + hmi::formatDuration(r.seconds) + " (anim\xC3\xA9)";
            return r.width > 0 ? std::to_string(r.width) + " x " + std::to_string(r.height) : "-";
        case MediaKind::Video:
            return (r.width > 0 ? std::to_string(r.width) + " x " + std::to_string(r.height) + ", " : std::string{})
                 + hmi::formatDuration(r.seconds);
        case MediaKind::Sound:
            return hmi::formatDuration(r.seconds);
        default:
            return "-";
    }
}

ui::Icon iconOf(MediaKind k) {
    switch (k) {
        case MediaKind::Image: return ui::Icon::Image;
        case MediaKind::Sound: return ui::Icon::Play;
        case MediaKind::Video: return ui::Icon::Screen;
        case MediaKind::Font:  return ui::Icon::Document;
        default:               return ui::Icon::Document;
    }
}

// Les couleurs de la scene d'apercu : sombres dans un theme sombre, claires
// dans un theme clair (une police ecrite a la couleur du texte doit s'y lire).
struct Stage { gfx::Color back, check1, check2, axis, overlay; };
Stage stageOf(const ui::Theme& t) {
    const auto p = t.color.panelBg;
    const bool darkTheme = (p.r * 299 + p.g * 587 + p.b * 114) / 1000 < 128;
    if (darkTheme) return {{24, 27, 32, 255}, {58, 62, 70, 255}, {78, 83, 92, 255}, {70, 76, 86, 255}, {230, 236, 244, 220}};
    return {{236, 239, 243, 255}, {204, 208, 214, 255}, {228, 231, 235, 255}, {176, 182, 192, 255}, {70, 80, 96, 200}};
}

void checker(gfx::IRenderer& r, const gfx::Rect& b, const Stage& st) {
    // Le damier des logiciels de dessin : la transparence se voit.
    const float s = 10.f;
    r.pushClip(b);
    for (float y = b.y; y < b.y + b.h; y += s)
        for (float x = b.x; x < b.x + b.w; x += s) {
            const bool first = (static_cast<int>((x - b.x) / s) + static_cast<int>((y - b.y) / s)) % 2 == 0;
            r.fillRect({x, y, s, s}, first ? st.check1 : st.check2);
        }
    r.popClip();
}

} // namespace

void hmiSelectModelRow(ui::TableView& table, std::size_t modelRow) {
    table.selectModelRows({static_cast<ui::RowIndex>(modelRow)});
}

// =============================================================== apercu ==
void HmiResourcePreview::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    const hmi::Resource* res = project_ ? project_->resource(resource_) : nullptr;
    if (!res) {
        ctx.r.drawText({b.x + 16, b.y + 16}, "Choisis une ressource pour la voir.", ctx.theme.font.ui, c.textMuted);
        return;
    }
    ctx.r.pushClip(b);
    // La zone d'apercu en haut, les renseignements dessous.
    const float infoH = 190.f;
    const gfx::Rect area{b.x + 12, b.y + 12, b.w - 24, std::max(60.f, b.h - infoH - 24)};
    const Stage stage = stageOf(ctx.theme);
    ctx.r.fillRect(area, stage.back);
    auto& cache = HmiImageCache::instance();
    switch (res->kind()) {
        case MediaKind::Image: {
            // Lot 16 : un GIF anime se joue dans l'apercu, a son rythme ; le coin dit
            // l'image montree et le temps du tour.
            if (res->format == "GIF") {
                if (const auto* gif = cache.gif(ctx.r, *res); gif && gif->valid() && gif->frames.size() > 1) {
                    const auto& img = gif->looping(ctx.time);
                    const float scale = std::min({area.w / static_cast<float>(img.width), area.h / static_cast<float>(img.height), 4.f});
                    const float w = static_cast<float>(img.width) * scale, h = static_cast<float>(img.height) * scale;
                    const gfx::Rect dst{area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h};
                    checker(ctx.r, dst, stage);
                    ctx.r.drawImage(img.tex, dst);
                    const double total = std::max(1, gif->timing.totalMs) / 1000.0;
                    const double at = std::fmod(ctx.time, total);
                    const auto frame = gif->timing.frameAt(at * 1000.0);
                    const std::string tag = "GIF \xC2\xB7 image " + std::to_string(frame + 1) + " / " + std::to_string(gif->frames.size()) + " \xC2\xB7 "
                                            + hmi::formatDuration(at) + " / " + hmi::formatDuration(total);
                    ctx.r.drawText({area.x + 8, area.y + area.h - 22}, tag, ctx.theme.font.smallUi, c.textMuted);
                    invalidate();          // l'apercu joue : redessiner
                    break;
                }
            }
            const auto img = cache.resource(ctx.r, *res);
            if (img.valid()) {
                const float scale = std::min({area.w / static_cast<float>(img.width), area.h / static_cast<float>(img.height), 4.f});
                const float w = static_cast<float>(img.width) * scale, h = static_cast<float>(img.height) * scale;
                const gfx::Rect dst{area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h};
                checker(ctx.r, dst, stage);
                ctx.r.drawImage(img.tex, dst);
            } else {
                ctx.r.drawText({area.x + 12, area.y + 12}, res->data ? "Image illisible" : "Fichier absent",
                               ctx.theme.font.ui, c.warning);
            }
            break;
        }
        case MediaKind::Sound: {
            const int buckets = std::max(8, static_cast<int>(area.w / 3));
            const auto& env = cache.envelope(*res, buckets);
            const float mid = area.y + area.h / 2;
            ctx.r.line({area.x, mid}, {area.x + area.w, mid}, stage.axis, 1);
            for (int k = 0; k < buckets && static_cast<std::size_t>(k * 2 + 1) < env.size(); ++k) {
                const float x = area.x + static_cast<float>(k) * area.w / static_cast<float>(buckets);
                const float lo = env[static_cast<std::size_t>(k * 2)], hi = env[static_cast<std::size_t>(k * 2 + 1)];
                ctx.r.fillRect({x, mid - hi * area.h / 2, std::max(1.f, area.w / static_cast<float>(buckets) + 0.5f),
                                std::max(1.f, (hi - lo) * area.h / 2)},
                               c.accent);
            }
            // En lecture : le curseur avance avec l'horloge de l'application.
            auto& player = HmiSoundPlayer::instance();
            if (player.current() == res->data.get() && player.playing(ctx.time) && player.duration() > 0) {
                const float x = area.x + static_cast<float>(player.position(ctx.time) / player.duration()) * area.w;
                ctx.r.fillRect({x - 1, area.y, 2, area.h}, c.warning);
                ctx.r.drawText({x + 6, area.y + 6}, hmi::formatDuration(player.position(ctx.time)), ctx.theme.font.smallUi,
                               c.warning);
                invalidate();
            }
            // L'axe du temps : le debut, le milieu, la fin.
            const float ty = area.y + area.h - 20;
            ctx.r.drawText({area.x + 6, ty}, "0 s", ctx.theme.font.smallUi, c.textMuted);
            const auto half = hmi::formatDuration(res->seconds / 2), end = hmi::formatDuration(res->seconds);
            ctx.r.drawText({area.x + area.w / 2 - ctx.r.measure(half, ctx.theme.font.smallUi).width / 2, ty}, half,
                           ctx.theme.font.smallUi, c.textMuted);
            ctx.r.drawText({area.x + area.w - 6 - ctx.r.measure(end, ctx.theme.font.smallUi).width, ty}, end,
                           ctx.theme.font.smallUi, c.textMuted);
            break;
        }
        case MediaKind::Font: {
            float y = area.y + 14;
            for (const auto& [line, px] : std::initializer_list<std::pair<const char*, float>>{
                     {"Aa Bb Cc 0123456789", 36.f}, {"Temp\xC3\xA9rature : 21,5 \xC2\xB0" "C", 26.f},
                     {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 18.f}, {"abcdefghijklmnopqrstuvwxyz \xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7", 18.f}}) {
                const auto img = cache.fontText(ctx.r, *res, line, px);
                if (img.valid())
                    ctx.r.drawImage(img.tex, {area.x + 14, y, static_cast<float>(img.width), static_cast<float>(img.height)}, 0.f,
                                    false, false, c.text);
                y += px * 1.45f;
            }
            break;
        }
        case MediaKind::Video: {
            // 1.12.12 (lot H) : sa premiere image quand elle se decode ici (le Motion-JPEG en AVI partout ; MP4, WMV,
            // MOV sous Windows). Sinon l'affiche, si une image de meme nom existe (presentation.png pour
            // presentation.mp4) ; sinon un ecran et le triangle de lecture.
            std::string why;
            auto img = cache.videoFrame(ctx.r, *res, hmi::kNoId, 0.0, &why);
            const bool decoded = img.valid();
            if (!decoded) {
                const auto stem = res->name.substr(0, res->name.rfind('.'));
                const hmi::Resource* poster = nullptr;
                for (const auto& r : project_->assets.resources)
                    if (r.kind() == MediaKind::Image && r.name.rfind(stem + ".", 0) == 0) poster = &r;
                img = poster ? cache.resource(ctx.r, *poster) : HmiImageCache::Image{};
            }
            if (img.valid()) {
                const float scale = std::min(area.w / static_cast<float>(img.width), area.h / static_cast<float>(img.height));
                const float w = static_cast<float>(img.width) * scale, h = static_cast<float>(img.height) * scale;
                ctx.r.drawImage(img.tex, {area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h});
            }
            const float s = std::min(area.w, area.h) * 0.22f;
            const gfx::Point m{area.x + area.w / 2, area.y + area.h / 2};
            ui::shapes::fillPolygon(ctx.r, {{m.x - s / 2, m.y - s / 2}, {m.x + s / 2, m.y}, {m.x - s / 2, m.y + s / 2}},
                                    stage.overlay);
            if (!decoded) {
                if (why.empty()) why = hmi::video::decodeAdvice(res->format, res->detail);
                ctx.r.drawText({area.x + 8, area.y + area.h - 22},
                               why.empty() ? std::string("Non d\xC3\xA9" "cod\xC3\xA9" "e : conteneur lu (dur\xC3\xA9" "e, dimensions, codec)") : why,
                               ctx.theme.font.smallUi, c.textMuted);
            }
            break;
        }
        case MediaKind::Document: {
            // Lot API 8 : un document garde tel quel - son format en grand.
            const std::string fmt = res->format.empty() ? std::string("?") : res->format;
            const auto sz = ctx.r.measure(fmt, ctx.theme.font.title);
            ctx.r.drawText({area.x + (area.w - sz.width) / 2, area.y + area.h / 2 - 20}, fmt, ctx.theme.font.title, c.text);
            const std::string said = "Document gard\xC3\xA9 tel quel : une copie qui voyage avec le projet";
            const auto sw = ctx.r.measure(said, ctx.theme.font.smallUi);
            ctx.r.drawText({area.x + std::max(8.f, (area.w - sw.width) / 2), area.y + area.h / 2 + 12}, said, ctx.theme.font.smallUi,
                           c.textMuted);
            break;
        }
        default:
            break;
    }
    // Les renseignements.
    float y = area.y + area.h + 12;
    const auto line = [&](const std::string& label, const std::string& value, gfx::Color col) {
        ctx.r.drawText({b.x + 16, y}, label, ctx.theme.font.smallUi, c.textMuted);
        ctx.r.drawText({b.x + 130, y}, value, ctx.theme.font.ui, col);
        y += ctx.r.lineHeight(ctx.theme.font.ui) + 3;
    };
    line("Nom", res->name, c.text);
    line("Genre", std::string(hmi::mediaKindLabel(res->kind())) + (res->format.empty() ? "" : " \xC2\xB7 " + res->format), c.text);
    line("Dimensions", dimensionsOf(*res), c.text);
    line("Poids", hmi::formatBytes(res->bytes), c.text);
    if (!res->detail.empty()) line("D\xC3\xA9tail", res->detail, res->detail.find("ERREUR") != std::string::npos ? c.error : c.text);
    const auto uses = hmi::citations(*project_, res->name);
    const bool document = res->kind() == MediaKind::Document;           // lot API 8 : il accompagne le projet
    line("Utilisations", uses.empty() ? (document ? std::string("un document accompagne le projet : il voyage avec lui")
                                                  : std::string("aucune : inutilis\xC3\xA9" "e"))
                                      : std::to_string(uses.size()),
         uses.empty() ? (document ? c.textMuted : c.warning) : c.text);
    for (std::size_t i = 0; i < uses.size() && i < 4; ++i)
        line("", uses[i].where + (uses[i].expression ? "  (expression)" : ""), c.textMuted);
    ctx.r.popClip();
}

// ============================================================ Ressources ==
namespace {
enum ResourceAction : int { RImport = 1, RReplace, RRename, RDelete, RUnused, RPurge, RPlay,
                            RFolder };        // lot 21 : un dossier de ressources
}

HmiResourcesPane::HmiResourcesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    // 1.12.12 (lot H) : la liste des extensions acceptees (AVI, MOV, WMV y sont ; avant, recopiee a la main).
    tools->add(RImport, HmiGlyph::Plus, "Importer une ressource (" + std::string(hmi::acceptedExtensions()) + " ; "
                                        "tout autre fichier - PDF, DOCX, ZIP... - : un document, gard\xC3\xA9 tel quel)", "Importer");
    tools->add(RReplace, HmiGlyph::Paste, "Remplacer le contenu (les objets qui la citent suivent)", "Remplacer");
    tools->add(RRename, HmiGlyph::Text, "Renommer (les objets et les expressions qui la citent suivent)", "Renommer");
    tools->add(RDelete, HmiGlyph::Delete, "Supprimer la ressource, ou le dossier choisi (ses ressources remontent d'un cran) - Ctrl+Z la rend",
               "Supprimer");
    tools->add(RFolder, HmiGlyph::Plus, "Nouveau dossier de ressources (sans effet sur les noms) : glisse des ressources dessus", "Dossier");
    tools->add(RPlay, HmiGlyph::Play, "\xC3\x89" "couter le son (encore une fois : l'arr\xC3\xAAter)", "\xC3\x89" "couter");
    tools->separator();
    tools->add(RUnused, HmiGlyph::Search, "Montrer seulement les ressources inutilis\xC3\xA9" "es", "Inutilis\xC3\xA9" "es");
    tools->add(RPurge, HmiGlyph::Delete, "Retirer toutes les ressources inutilis\xC3\xA9" "es", "Retirer les inutilis\xC3\xA9" "es");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(RReplace, [this] { return selectedResource() != kNoId; });
    tools_->setEnabledWhen(RRename, [this] { return selectedResource() != kNoId; });
    tools_->setEnabledWhen(RDelete, [this] { return selectedResource() != kNoId || (folders_ && !folders_->selectedFolder().empty()); });
    tools_->setCheckedWhen(RUnused, [this] { return unusedOnly_; });
    tools_->setEnabledWhen(RPlay, [this] {
        const auto* r = doc_->project.resource(selectedResource());
        return r && r->kind() == MediaKind::Sound && r->data;
    });
    tools_->setCheckedWhen(RPlay, [this] {
        const auto* r = doc_->project.resource(selectedResource());
        return r && r->data && HmiSoundPlayer::instance().current() == r->data.get();
    });
    tools_->setEnabledWhen(RPurge, [this] { return !hmi::unusedResources(doc_->project).empty(); });

    auto kind = std::make_unique<ui::DropDown>(base + ".kind");
    kind->setItems({{"Toutes", "", {}, true}, {"Images", "", {}, true}, {"Sons", "", {}, true},
                    {"Vid\xC3\xA9os", "", {}, true}, {"Polices", "", {}, true},
                    {"Documents", "", {}, true}});                  // lot API 8 : en dernier (les rangs restent)
    kind->setSelectedIndex(0);
    kind_ = &static_cast<ui::DropDown&>(addChild(std::move(kind)));
    auto search = std::make_unique<ui::InputText>(base + ".search");
    search->setPlaceholder("Rechercher une ressource...");
    search_ = &static_cast<ui::InputText&>(addChild(std::move(search)));

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto table = std::make_unique<ui::TableView>(base + ".table");
    // Lot 21 : Nom et Genre plus larges (les dossiers : "Images", "4 ressources").
    table->setColumns({{"Nom", 210.f}, {"Genre", 105.f}, {"Format", 80.f}, {"Dimensions / dur\xC3\xA9" "e", 160.f},
                       {"Poids", 84.f, 60.f, true, true, true, ui::Align::End}, {"Utilisations", 110.f, 60.f, true, true, true, ui::Align::End},
                       {"D\xC3\xA9tail", 240.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(split->addPane(std::move(table), 0.60f, 300.f));
    // 1.12.27 : Suppr, Ctrl+D, Inser dans la liste - comme les boutons de la barre
    tools_->bindListKeys(*table_, {RDelete});
    // Lot 21 : les ressources rangees en dossiers ; glisser une ou plusieurs lignes.
    folders_ = std::make_unique<HmiFolderTable>(*table_, doc_, apply_);
    preview_ = &static_cast<HmiResourcePreview&>(split->addPane(std::make_unique<HmiResourcePreview>(base + ".preview"), 0.40f, 240.f));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedResource();
        switch (a) {
            case RImport: if (hosts_.importFile) hosts_.importFile(); break;
            case RReplace: if (sel && hosts_.replace) hosts_.replace(sel); break;
            case RRename: if (sel && hosts_.rename) hosts_.rename(sel); break;
            case RFolder: (void)folders_->newFolder(); break;
            case RDelete:
                if (!sel) {
                    if (const auto f = folders_->selectedFolder(); !f.empty()) (void)folders_->deleteFolder(f);
                    break;
                }
                if (hosts_.remove) hosts_.remove(sel);
                else (void)deleteSelected();
                break;
            case RUnused: setUnusedOnly(!unusedOnly_); break;
            case RPurge: (void)removeUnused(); break;
            case RPlay: (void)togglePlay(); break;
            default: break;
        }
    });
    links_ += kind_->selectionChanged->connect([this](int i) { kindFilter_ = i; refresh(); });
    links_ += search_->textChanged->connect([this](const std::string& t) { searchText_ = t; refresh(); });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        preview_->show(&doc_->project, selectedResource());
        invalidate();
    });
    // Une commande est passee (ici, ou Annuler ailleurs) : le message de
    // l'action precedente ne dit plus vrai. L'action d'ici redit le sien apres.
    links_ += folders_->message->connect([this](const std::string& text) { say(text); });
    links_ += folders_->relayout->connect([this] { refresh(); });
    links_ += doc_->changed->connect([this](Id) {
        status_->dismissTransient();
        refresh();
    });
    refresh();
}

void HmiResourcesPane::refresh() {
    const Id keep = selectedResource();
    const auto& p = doc_->project;
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles, lastStyles;
    order_.clear();
    const ui::SearchQuery query(searchText_);        // lot recherche : mots, "phrase", -exclus
    std::uint64_t total = 0;
    std::size_t unused = 0;
    std::map<Id, std::vector<std::string>> cells;
    std::map<Id, std::pair<ui::CellStyle, ui::CellStyle>> looks;
    for (const auto& r : p.assets.resources) {
        total += r.bytes;
        const auto uses = hmi::citations(p, r.name).size();
        const bool document = r.kind() == MediaKind::Document;       // lot API 8 : jamais "inutilise"
        unused += uses == 0 && !document;
        const MediaKind wanted[] = {MediaKind::Unknown, MediaKind::Image, MediaKind::Sound, MediaKind::Video, MediaKind::Font,
                                    MediaKind::Document};
        if (kindFilter_ > 0 && kindFilter_ < 6 && r.kind() != wanted[kindFilter_]) continue;
        if (unusedOnly_ && (uses > 0 || document)) continue;
        // Lot recherche : le nom, le format, le detail (codec, police...), le fichier
        // d'origine, le dossier.
        if (!query.matches({r.name, r.format, r.detail, r.origin, r.folder, std::string(hmi::mediaKindLabel(r.kind()))})) continue;
        order_.push_back(r.id);
        // Lot API 8 : corrections des captures - un document sans citation disait
        // "-" (illisible) : "aucune", le compte (il n'est pas "inutilisee" : il
        // voyage avec le projet, Retirer les inutilisees le garde).
        cells[r.id] = {r.name, std::string(hmi::mediaKindLabel(r.kind())), r.format, dimensionsOf(r), hmi::formatBytes(r.bytes),
                       uses ? std::to_string(uses) : document ? std::string("aucune") : std::string("inutilis\xC3\xA9" "e"),
                       r.data ? r.detail : "FICHIER ABSENT"};
        ui::CellStyle s;
        s.icon = iconOf(r.kind());
        if (!r.data) s.iconTone = ui::Tone::Error;
        ui::CellStyle last;
        if (!r.data || r.detail.find("ERREUR") != std::string::npos) last.fgTone = ui::Tone::Error;
        looks[r.id] = {s, last};
    }
    (void)rows;
    (void)styles;
    (void)lastStyles;
    // Lot 21 : la liste rangee en dossiers (filtree par genre ou par recherche,
    // chaque ressource montree reste sous le sien).
    folders_->rebuild(hmi::fold::List::Resources, order_,
                      {"Nom", "Genre", "Format", "Dimensions / dur\xC3\xA9" "e", "Poids", "Utilisations", "D\xC3\xA9tail"},
                      [&cells](Id id) { return cells[id]; },
                      [&looks](Id id, std::size_t c) { return c == 0 ? looks[id].first : c == 6 ? looks[id].second : ui::CellStyle{}; });
    if (keep) selectResource(keep);
    preview_->show(&doc_->project, selectedResource());
    std::string msg = std::to_string(p.assets.resources.size()) + " ressource(s), " + hmi::formatBytes(total);
    if (unused) msg += " \xC2\xB7 " + std::to_string(unused) + " inutilis\xC3\xA9" "e(s)";
    if (order_.size() != p.assets.resources.size()) msg += " \xC2\xB7 " + std::to_string(order_.size()) + " montr\xC3\xA9" "e(s)";
    status_->setMessage(msg);
    invalidate();
}

Id HmiResourcesPane::selectedResource() const { return folders_ ? folders_->selectedItem() : kNoId; }

void HmiResourcesPane::selectResource(Id id) {
    folders_->selectItem(id);
    preview_->show(&doc_->project, selectedResource());
}

void HmiResourcesPane::setKindFilter(int index) {
    kind_->setSelectedIndex(index);
    kindFilter_ = index;
    refresh();
}
void HmiResourcesPane::setSearch(std::string text) {
    search_->setText(text);
    searchText_ = std::move(text);
    refresh();
}
void HmiResourcesPane::setUnusedOnly(bool on) {
    unusedOnly_ = on;
    refresh();
}

void HmiResourcesPane::say(std::string text, bool warning) {
    if (warning) ui::PropertyGrid::refuse(text);   // 1.12.19 (la regle des cases) : un refus se dit sous la case
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiResourcesPane::importFile(const std::string& path, const std::string& name, std::string* why) {
    auto res = hmi::readResource(doc_->project, path, name);
    if (!res) {
        if (why) *why = reason(res.error());
        say("Import impossible : " + reason(res.error()), true);
        return false;
    }
    const Id made = res->id;
    const std::string label = res->name;
    auto cmd = hmi::changeProject(doc_, "Importer " + label, [&](hmi::Project& p) { p.assets.resources.push_back(*res); });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectResource(made);
    const auto* r = doc_->project.resource(made);
    say("Import\xC3\xA9" "e : " + label + (r ? " (" + std::string(hmi::mediaKindLabel(r->kind())) + ", " + hmi::formatBytes(r->bytes) + ")" : ""));
    return true;
}

bool HmiResourcesPane::replaceResource(Id sel, const std::string& path, std::string* why) {
    const auto* cur = doc_->project.resource(sel);
    if (!cur) { if (why) *why = "aucune ressource choisie"; return false; }
    hmi::Project scratch;
    scratch.nextId = doc_->project.nextId;
    auto res = hmi::readResource(scratch, path, cur->name);
    if (!res) {
        if (why) *why = reason(res.error());
        say("Remplacement impossible : " + reason(res.error()), true);
        return false;
    }
    if (res->kind() != cur->kind()) {
        const std::string msg = "une ressource " + std::string(hmi::mediaKindLabel(cur->kind())) + " se remplace par une ressource du m\xC3\xAAme genre";
        if (why) *why = msg;
        say(msg, true);
        return false;
    }
    const std::string name = cur->name;
    auto cmd = hmi::changeProject(doc_, "Remplacer " + name, [&](hmi::Project& p) {
        if (auto* r = p.resource(sel)) {
            r->data = res->data;
            r->origin = res->origin;
            r->added = hmi::nowStamp();
            hmi::describeResource(*r);
        }
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Remplac\xC3\xA9" "e : " + name + " (les objets qui la citent montrent le nouveau contenu)");
    return true;
}

bool HmiResourcesPane::renameResource(Id sel, const std::string& name, std::string* why) {
    if (!doc_->project.resource(sel)) { if (why) *why = "aucune ressource choisie"; return false; }
    std::string error;
    std::size_t updated = 0;
    bool ok = true;
    auto cmd = hmi::changeProject(doc_, "Renommer la ressource", [&](hmi::Project& p) {
        ok = hmi::renameResource(p, sel, name, &updated, &error);
    });
    if (!ok) {
        if (why) *why = error;
        say("Renommage refus\xC3\xA9 : " + error, true);
        return false;
    }
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectResource(sel);
    say("Renomm\xC3\xA9" "e en " + name + " : " + std::to_string(updated) + " r\xC3\xA9" "f\xC3\xA9rence(s) mise(s) \xC3\xA0 jour");
    return true;
}

bool HmiResourcesPane::deleteResource(Id sel) {
    const auto* r = doc_->project.resource(sel);
    if (!r) return false;
    const std::string name = r->name;
    const auto uses = hmi::citations(doc_->project, name).size();
    auto cmd = hmi::changeProject(doc_, "Supprimer " + name, [&](hmi::Project& p) {
        auto& v = p.assets.resources;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::Resource& x) { return x.id == sel; }), v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Supprim\xC3\xA9" "e : " + name + (uses ? " - " + std::to_string(uses) + " objet(s) la citaient encore (G\xC3\xA9n\xC3\xA9rer le dira)" : ""),
        uses > 0);
    return true;
}

std::size_t HmiResourcesPane::removeUnused() {
    std::vector<Id> gone;
    for (const auto* r : hmi::unusedResources(doc_->project)) gone.push_back(r->id);
    if (gone.empty()) return 0;
    auto cmd = hmi::changeProject(doc_, "Retirer les ressources inutilis\xC3\xA9" "es", [&](hmi::Project& p) {
        auto& v = p.assets.resources;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const hmi::Resource& x) { return std::find(gone.begin(), gone.end(), x.id) != gone.end(); }),
                v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say(std::to_string(gone.size()) + " ressource(s) inutilis\xC3\xA9" "e(s) retir\xC3\xA9" "e(s) - Ctrl+Z les rend");
    return gone.size();
}

bool HmiResourcesPane::togglePlay() {
    auto& player = HmiSoundPlayer::instance();
    const auto* r = doc_->project.resource(selectedResource());
    if (r && r->data && player.current() == r->data.get()) {
        player.stop();
        say("Arr\xC3\xAAt\xC3\xA9 : " + r->name);
        return true;
    }
    if (!r || r->kind() != MediaKind::Sound) { say("Choisis un son pour l'\xC3\xA9" "couter.", true); return false; }
    std::string why;
    if (!player.play(*r, &why)) { say("Lecture impossible : " + why, true); return false; }
    say("Lecture : " + r->name + " (" + hmi::formatDuration(player.duration()) + ")");
    invalidate();
    return true;
}

void HmiResourcesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    kind_->setBounds({b.x + 8, b.y + 42, 170, 28});
    search_->setBounds({b.x + 186, b.y + 42, std::min(420.f, b.w - 194), 28});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 76, b.w, std::max(0.f, b.h - 100)});
}

void HmiResourcesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

ui::EventResult HmiResourcesPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        (void)importFile(drop->path);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ======================================================= fichiers externes ==
HmiFilePreview::HmiFilePreview(std::string id) : ui::Widget(std::move(id)) {
    auto grid = std::make_unique<ui::TableView>(this->id() + ".grid");
    grid->setSelectionMode(ui::SelectionMode::Single);
    grid_ = &static_cast<ui::TableView&>(addChild(std::move(grid)));
    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    open_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ouvrir avec le programme du syst\xC3\xA8me",
                                                                           this->id() + ".ouvrir")));
    open_->setTooltip("Le programme que le syst\xC3\xA8me associe \xC3\xA0 ce genre de fichier (un lecteur PDF, Word...)");
    open_->setVisibility(ui::Visibility::Collapsed);
    links_ += open_->clicked->connect([this] { (void)openDocument(); });
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
}

// ---- Lot API 8 : glisser de fichiers, 2e partie ----
bool HmiFilePreview::openDocument(std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (openPath_.empty()) return fail("aucun document choisi (ou le fichier est absent)");
    const std::string error = openWithSystem(openPath_);
    if (!error.empty()) return fail(error);
    return true;
}
// ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

void HmiFilePreview::show(const hmi::ExternalFile* file) {
    openPath_.clear();                                   // lot API 8
    if (!file) {
        data_.reset();
        title_.clear();
        grid_->setModel(nullptr);
        invalidateLayout();
        invalidate();
        return;
    }
    data_ = hmiExternalData(*file, 200);
    title_ = file->name + "  \xC2\xB7  " + std::string(hmi::externalKindKey(file->kind));
    // Lot API 8 : un document present s'ouvre avec le programme du systeme.
    if (file->kind == hmi::ExternalKind::Document && data_ && data_->ok())
        openPath_ = hmi::externalState(*file, hmiProjectFolder()).resolvedPath;
    // La largeur d'une colonne : son titre ou ses 50 premieres valeurs.
    std::vector<ui::TableView::Column> cols;
    for (std::size_t k = 0; k < data_->headers.size(); ++k) {
        std::size_t widest = data_->headers[k].size();
        for (std::size_t i = 0; i < data_->rows.size() && i < 50; ++i)
            if (k < data_->rows[i].size()) widest = std::max(widest, data_->rows[i][k].size());
        cols.push_back({data_->headers[k], std::clamp(8.f * static_cast<float>(widest) + 28.f, 70.f, 380.f)});
    }
    grid_->setColumns(cols);
    grid_->setModel(std::make_shared<RowsModel>(data_->headers, data_->rows));
    invalidateLayout();
    invalidate();
}

void HmiFilePreview::onLayout() {
    const auto b = bounds();
    const float head = 96.f;
    grid_->setBounds({b.x + 8, b.y + head, b.w - 16, std::max(0.f, b.h - head - 8)});
    grid_->setVisibility(data_ && !data_->headers.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    // Lot API 8 : le bouton d'un document, sous son resume.
    if (open_) {
        open_->setBounds({b.x + 12, b.y + head - 30.f, std::min(b.w - 24.f, 300.f), 28.f});
        open_->setVisibility(openPath_.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    }
}

void HmiFilePreview::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    if (!data_) {
        ctx.r.drawText({b.x + 16, b.y + 16}, "Choisis un fichier pour voir son contenu.", ctx.theme.font.ui, c.textMuted);
        return;
    }
    ctx.r.pushClip(b);
    float y = b.y + 10;
    ctx.r.drawText({b.x + 12, y}, title_, ctx.theme.font.title, c.text);
    y += ctx.r.lineHeight(ctx.theme.font.title) + 6;
    ctx.r.drawText({b.x + 12, y}, data_->ok() ? data_->summary : data_->error, ctx.theme.font.ui, data_->ok() ? c.textMuted : c.error);
    y += ctx.r.lineHeight(ctx.theme.font.ui) + 4;
    if (!data_->parts.empty()) {
        std::string parts;
        for (const auto& p : data_->parts) parts += (parts.empty() ? "" : "   ") + (p == data_->partUsed ? "[" + p + "]" : p);
        ctx.r.drawText({b.x + 12, y}, parts, ctx.theme.font.smallUi, c.accent);
    }
    // Pas de colonnes : le texte lui-meme.
    if (data_->headers.empty()) {
        y = b.y + 96;
        for (const auto& l : data_->lines) {
            if (y > b.y + b.h - 16) break;
            ctx.r.drawText({b.x + 12, y}, l, ctx.theme.font.mono, c.text);
            y += ctx.r.lineHeight(ctx.theme.font.mono) + 2;
        }
    }
    ctx.r.popClip();
}

namespace {
enum FileAction : int { FLink = 1, FDatabase, FRelink, FPart, FRemove };
}

HmiFilesPane::HmiFilesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(FLink, HmiGlyph::Plus, "Lier un fichier : Excel, CSV, TXT, JSON, XML, SQLite ; tout autre fichier (PDF, DOCX, ZIP...) : un document",
               "Lier un fichier");
    tools->add(FDatabase, HmiGlyph::Layer, "Lier une base externe (cha\xC3\xAEne de connexion)", "Base externe");
    tools->add(FRelink, HmiGlyph::Redo, "Relier : prendre acte d'un fichier modifi\xC3\xA9 (nouvelle taille, nouvelle date)", "Relier");
    tools->add(FPart, HmiGlyph::View, "Choisir l'onglet (Excel) ou la table (SQLite)", "Onglet / table");
    tools->add(FRemove, HmiGlyph::Delete, "Retirer le lien (le fichier n'est pas touch\xC3\xA9)", "Retirer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(FRelink, [this] { return selectedFile() != kNoId; });
    tools_->setEnabledWhen(FRemove, [this] { return selectedFile() != kNoId; });
    tools_->setEnabledWhen(FPart, [this] {
        const auto* f = doc_->project.externalFile(selectedFile());
        return f && (f->kind == hmi::ExternalKind::Excel || f->kind == hmi::ExternalKind::Sqlite);
    });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".split");
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Nom", 170.f}, {"Type", 150.f}, {"Chemin", 380.f}, {"Date", 170.f},
                       {"Taille", 90.f, 60.f, true, true, true, ui::Align::End}, {"\xC3\x89tat", 130.f}, {"Utilis\xC3\xA9 par", 260.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(split->addPane(std::move(table), 0.42f, 120.f));
    // 1.12.27 : Suppr, Ctrl+D, Inser dans la liste - comme les boutons de la barre
    tools_->bindListKeys(*table_, {FRemove});
    preview_ = &static_cast<HmiFilePreview&>(split->addPane(std::make_unique<HmiFilePreview>(base + ".preview"), 0.58f, 160.f));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedFile();
        switch (a) {
            case FLink: if (hosts_.linkFile) hosts_.linkFile(); break;
            case FDatabase: if (hosts_.linkDatabase) hosts_.linkDatabase(); break;
            case FRelink: (void)relinkSelected(); break;
            case FPart: if (sel && hosts_.choosePart) hosts_.choosePart(sel); break;
            case FRemove:
                if (!sel) break;
                if (hosts_.remove) hosts_.remove(sel);
                else (void)removeSelected();
                break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        preview_->show(doc_->project.externalFile(selectedFile()));
        invalidate();
    });
    links_ += doc_->changed->connect([this](Id) {
        status_->dismissTransient();
        refresh();
    });
    refresh();
}

void HmiFilesPane::refresh() {
    const Id keep = selectedFile();
    const auto& p = doc_->project;
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles, states;
    order_.clear();
    std::size_t missing = 0, modified = 0;
    for (const auto& f : p.assets.files) {
        const auto st = hmi::externalState(f, hmiProjectFolder());
        order_.push_back(f.id);
        const auto uses = hmi::externalCitations(p, f.name);
        std::string usedBy = uses.empty() ? std::string("-") : uses.front().where;
        if (uses.size() > 1) usedBy += "  (+" + std::to_string(uses.size() - 1) + ")";
        const bool db = f.kind == hmi::ExternalKind::Database;
        rows.push_back({f.name, std::string(hmi::externalKindKey(f.kind)) + (f.part.empty() ? "" : " \xC2\xB7 " + f.part),
                        f.path, db ? std::string("-") : (st.modified.empty() ? f.modified : st.modified),
                        db ? std::string("-") : hmi::formatBytes(st.status == hmi::ExternalStatus::Missing ? f.bytes : st.bytes),
                        std::string(hmi::externalStatusLabel(st.status)), usedBy});
        ui::CellStyle s;
        s.icon = db ? ui::Icon::Library : ui::Icon::Document;
        styles.push_back(s);
        ui::CellStyle state;
        switch (st.status) {
            case hmi::ExternalStatus::Present:      state.fgTone = ui::Tone::Ok; break;
            case hmi::ExternalStatus::Modified:     state.fgTone = ui::Tone::Warning; ++modified; break;
            case hmi::ExternalStatus::Missing:      state.fgTone = ui::Tone::Error; ++missing; break;
            case hmi::ExternalStatus::Unverifiable: state.fgTone = ui::Tone::Muted; break;
        }
        states.push_back(state);
    }
    // La colonne Etat est l'avant-derniere : on la colore par son propre style.
    struct Model final : public ui::ITableModel {
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
        std::vector<ui::CellStyle> first, state;
        [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return headers.size(); }
        [[nodiscard]] std::string headerText(std::size_t c) const override { return headers[c]; }
        [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override { return rows[r][c]; }
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
            if (c == 0) return first[r];
            if (c == 5) return state[r];
            return {};
        }
        [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return rows[a][c] < rows[b][c]; }
    };
    auto m = std::make_shared<Model>();
    m->headers = {"Nom", "Type", "Chemin", "Date", "Taille", "\xC3\x89tat", "Utilis\xC3\xA9 par"};
    m->rows = std::move(rows);
    m->first = std::move(styles);
    m->state = std::move(states);
    model_ = m;
    table_->setModel(model_);
    if (keep) selectFile(keep);
    preview_->show(p.externalFile(selectedFile()));
    std::string msg = std::to_string(p.assets.files.size()) + " fichier(s) externe(s)";
    if (missing) msg += " \xC2\xB7 " + std::to_string(missing) + " absent(s)";
    if (modified) msg += " \xC2\xB7 " + std::to_string(modified) + " modifi\xC3\xA9(s) depuis le lien";
    status_->setMessage(msg);
    invalidate();
}

Id HmiFilesPane::selectedFile() const {
    const auto rows = table_->selectedModelRows();
    if (rows.empty() || rows.front() >= order_.size()) return kNoId;
    return order_[rows.front()];
}

void HmiFilesPane::selectFile(Id id) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == id) { hmiSelectModelRow(*table_, i); break; }
    preview_->show(doc_->project.externalFile(selectedFile()));
}

void HmiFilesPane::say(std::string text, bool warning) {
    if (warning) ui::PropertyGrid::refuse(text);   // 1.12.19 (la regle des cases) : un refus se dit sous la case
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiFilesPane::link(hmi::ExternalKind kind, const std::string& path, const std::string& name, const std::string& part,
                        std::string* why) {
    if (path.empty()) {
        if (why) *why = kind == hmi::ExternalKind::Database ? "cha\xC3\xAEne de connexion vide" : "chemin vide";
        say("Lien impossible : " + (why ? *why : std::string("chemin vide")), true);
        return false;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Lier un fichier externe", [&](hmi::Project& p) {
        auto f = hmi::linkExternal(p, name, kind, path, part, hmiProjectFolder());
        made = f.id;
        p.assets.files.push_back(std::move(f));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectFile(made);
    const auto* f = doc_->project.externalFile(made);
    const auto st = f ? hmi::externalState(*f, hmiProjectFolder()) : hmi::ExternalState{};
    say("Li\xC3\xA9 : " + (f ? f->name : path) + " (" + std::string(hmi::externalStatusLabel(st.status)) + ")",
        st.status == hmi::ExternalStatus::Missing);
    return true;
}

bool HmiFilesPane::relinkFile(Id sel) {
    if (!doc_->project.externalFile(sel)) return false;
    auto cmd = hmi::changeProject(doc_, "Relier le fichier externe", [&](hmi::Project& p) {
        if (auto* f = p.externalFile(sel)) hmi::relinkExternal(*f, hmiProjectFolder());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Reli\xC3\xA9 : taille et date relev\xC3\xA9" "es \xC3\xA0 nouveau");
    return true;
}

bool HmiFilesPane::removeFile(Id sel) {
    const auto* f = doc_->project.externalFile(sel);
    if (!f) return false;
    const std::string name = f->name;
    auto cmd = hmi::changeProject(doc_, "Retirer " + name, [&](hmi::Project& p) {
        auto& v = p.assets.files;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::ExternalFile& x) { return x.id == sel; }), v.end());
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Lien retir\xC3\xA9 : " + name + " (le fichier n'est pas touch\xC3\xA9)");
    return true;
}

bool HmiFilesPane::setPart(Id sel, const std::string& part) {
    if (!doc_->project.externalFile(sel)) return false;
    auto cmd = hmi::changeProject(doc_, "Choisir l'onglet ou la table", [&](hmi::Project& p) {
        if (auto* f = p.externalFile(sel)) f->part = part;
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    return true;
}

void HmiFilesPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

void HmiFilesPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

ui::EventResult HmiFilesPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        if (const auto kind = hmi::externalKindFromPath(drop->path)) (void)link(*kind, drop->path);
        else say("Pas un fichier externe connu (Excel, CSV, TXT, JSON, XML, SQLite) : " + drop->path, true);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ======================================================= 1.14.0 (M4) : les enregistrements ==
namespace {
enum RecordingAction : int { RcImport = 1, RcExport, RcReplay, RcRename, RcPassword, RcDelete, RcFolder };
// Les chemins de l'interface sont en UTF-8 (std::filesystem::path(std::string) les lirait dans la page de code du
// systeme sous Windows).
std::filesystem::path recPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
// "2026-10-29 14:02:33" -> "29/10/2026 14:02".
std::string recDate(const std::string& iso) {
    if (iso.size() < 16 || iso[4] != '-' || iso[7] != '-') return iso;
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4) + " " + iso.substr(11, 5);
}

// 1.14.0 (relecture M4b) : RENOMMER UN ENREGISTREMENT, UN SEUL PAS - le fichier ET ce qui le cite. Ctrl+Z rend le nom
// du fichier avec les citations (avant : les citations seules, qui ne designaient plus rien) ; Ctrl+Y le refait. Le
// fichier est deja renomme quand la commande est empilee : sa premiere execution ne fait que les citations.
class RecordingRenameCommand final : public core::ICommand {
public:
    RecordingRenameCommand(std::string folder, std::string from, std::string to, core::CommandPtr citations, std::function<void()> refresh)
        : folder_(std::move(folder)), from_(std::move(from)), to_(std::move(to)), citations_(std::move(citations)), refresh_(std::move(refresh)) {}
    core::Status execute() override {
        if (!first_) {
            std::string why;
            if (!hmi::rec::renameFile(folder_, from_, to_, &why)) return core::fail(core::ErrorCode::FileUnreadable, "renommer " + from_ + " : " + why);
        }
        first_ = false;
        if (citations_)
            if (auto r = citations_->execute(); !r) return r;
        if (refresh_) refresh_();
        return core::ok();
    }
    core::Status undo() override {
        if (citations_)
            if (auto r = citations_->undo(); !r) return r;
        std::string why;
        const bool ok = hmi::rec::renameFile(folder_, to_, from_, &why);
        if (refresh_) refresh_();
        if (!ok) return core::fail(core::ErrorCode::FileUnreadable, "rendre le nom " + from_ + " : " + why);
        return core::ok();
    }
    [[nodiscard]] std::string label() const override { return "Renommer l'enregistrement " + from_; }

private:
    std::string           folder_, from_, to_;
    core::CommandPtr      citations_;
    std::function<void()> refresh_;
    bool                  first_{true};
};

// 1.14.0 (captures finales) : LE DETAIL D'UN ENREGISTREMENT - la grille, la colonne des noms a la largeur du plus long
// (avant : 45 % de la place, les valeurs coupees : "par Op...", le projet), et dessous, quand elle ne tient pas
// dans sa case, LA DESCRIPTION ENTIERE, repliee (une grille ne passe pas a la ligne).
constexpr gfx::FontId kDetailFont{16};
std::vector<std::string> wrapWords(const std::string& text, gfx::FontId f, float maxW) {
    std::vector<std::string> out;
    std::string line;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        auto sp = text.find_first_of(" \n", pos);
        if (sp == std::string::npos) sp = text.size();
        const std::string word = text.substr(pos, sp - pos);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(candidate, f) > maxW) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (sp < text.size() && text[sp] == '\n') {
            out.push_back(line);
            line.clear();
        }
        pos = sp + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

class RecordingDetail final : public ui::Widget {
public:
    explicit RecordingDetail(std::string id) : ui::Widget(std::move(id)) {
        grid = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".grille")));
    }
    ui::PropertyGrid* grid{nullptr};
    // Les noms montres (la colonne des noms les tient) ; la description (repliee dessous si sa case est trop courte).
    void setContent(const std::vector<std::string>& names, std::string description) {
        nameW_ = 0.f;
        for (const auto& n : names) nameW_ = std::max(nameW_, ui::measureWidth(n, kDetailFont));
        description_ = std::move(description);
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] const std::vector<std::string>& noteLines() const noexcept { return lines_; }
    [[nodiscard]] float valueWidth() const noexcept { return valueW_; }

protected:
    void onLayout() override {
        const auto b = bounds();
        // Le nom : depuis le retrait (4 + 12) et sa marge (6), jusqu'a 6 avant la separation ; la valeur : 6 apres elle,
        // et la marge du bout (l'ascenseur au besoin).
        const float split = std::clamp(nameW_ + 36.f, b.w * 0.18f, b.w * 0.45f);
        grid->setNameColumnRatio(b.w > 1.f ? split / b.w : 0.45f);
        valueW_ = std::max(0.f, b.w - split - 6.f - 22.f);
        lines_.clear();
        if (!description_.empty() && ui::measureWidth(description_, kDetailFont) > valueW_)
            lines_ = wrapWords(description_, kDetailFont, std::max(80.f, b.w - 24.f));
        const float lh = ui::lineHeight(kDetailFont);
        const float noteH = lines_.empty() ? 0.f : 10.f + lh + 4.f + static_cast<float>(lines_.size()) * lh + 10.f;
        const float gridH = lines_.empty() ? b.h : std::max(60.f, std::min(b.h - noteH, grid->contentHeight()));
        grid->setBounds({b.x, b.y, b.w, gridH});
        noteTop_ = b.y + gridH;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        ctx.r.fillRect(b, ctx.theme.color.panelBg);
        if (lines_.empty()) return;
        const auto& c = ctx.theme.color;
        const float lh = ctx.r.lineHeight(ctx.theme.font.ui);
        float y = noteTop_ + 10.f;
        ctx.r.drawText({b.x + 12.f, y}, "Description", ctx.theme.font.smallUi, c.textMuted);
        y += ctx.r.lineHeight(ctx.theme.font.smallUi) + 4.f;
        for (const auto& line : lines_) {
            if (y + lh > b.bottom()) break;
            ctx.r.drawText({b.x + 12.f, y}, line, ctx.theme.font.ui, c.text);
            y += lh;
        }
    }

private:
    float                    nameW_{0.f}, valueW_{0.f}, noteTop_{0.f};
    std::string              description_;
    std::vector<std::string> lines_;
};
} // namespace

HmiRecordingsPane::HmiRecordingsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(RcImport, HmiGlyph::Import, "Importer un enregistrement (.xrec) : il est copi\xC3\xA9 dans le dossier (un nom pris : \xC2\xAB Nom (2) \xC2\xBB)",
               "Importer\xE2\x80\xA6");
    tools->add(RcExport, HmiGlyph::Export, "Exporter (copier) l'enregistrement choisi : un dossier, ou un fichier .xrec", "Exporter\xE2\x80\xA6");
    tools->separator();
    tools->add(RcReplay, HmiGlyph::Play, "Rejouer : l'IHM montre ce qui a \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9, comme un film (\xC3\x89" "chap la quitte). "
                                         "Un double-clic sur la ligne aussi.", "Rejouer");
    tools->add(RcRename, HmiGlyph::Text, "Renommer : les actions, le Lecteur et les scripts qui le citent suivent (Ctrl+Z les rend)", "Renommer");
    tools->add(RcPassword, HmiGlyph::Key, "Mot de passe : prot\xC3\xA9ger, changer ou retirer (l'actuel est demand\xC3\xA9)", "Mot de passe\xE2\x80\xA6");
    tools->add(RcDelete, HmiGlyph::Delete, "Supprimer : l'enregistrement va dans la corbeille du projet (ihm/corbeille)", "Supprimer");
    tools->separator();
    tools->add(RcFolder, HmiGlyph::Link, "Ouvrir le dossier des enregistrements dans l'explorateur", "Ouvrir le dossier");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    for (const int a : {RcExport, RcReplay, RcRename, RcPassword, RcDelete})
        tools_->setEnabledWhen(a, [this] { return selectedItem() != nullptr; });
    tools_->setEnabledWhen(RcImport, [this] { return !folder().empty(); });
    tools_->setEnabledWhen(RcFolder, [this] { return !folder().empty(); });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto table = std::make_unique<ui::TableView>(base + ".table");
    // 1.14.0 (captures finales) : des largeurs qui tiennent - la date entiere ("30/10/2026 03:12"), un nom
    // d'operateur ("Op\xC3\xA9rateur de conduite") ; le Nom, la Vue de depart, Ou et la Taille rendent ce qu'il faut
    // (avant : "30/10/2026 0...", "Op\xC3\xA9rate...", le tableau ayant de la place).
    table->setColumns({{"Nom", 180.f}, {"Dur\xC3\xA9" "e", 70.f, 50.f, true, true, true, ui::Align::End}, {"Vue de d\xC3\xA9part", 126.f},
                       {"Enregistr\xC3\xA9 le", 140.f}, {"O\xC3\xB9", 92.f}, {"Par", 170.f},
                       {"Taille", 72.f, 50.f, true, true, true, ui::Align::End}, {"Prot\xC3\xA9g\xC3\xA9", 84.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table->setEnterActivates(true);
    table_ = &static_cast<ui::TableView&>(split->addPane(std::move(table), 0.64f, 320.f));
    tools_->bindListKeys(*table_, {RcDelete});
    auto box = std::make_unique<RecordingDetail>(base + ".detail");
    detail_ = box->grid;
    detailBox_ = &split->addPane(std::move(box), 0.36f, 240.f);
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const auto* it = selectedItem();
        const std::string name = it ? it->name : std::string{};
        switch (a) {
            case RcImport: if (hosts_.importFile) hosts_.importFile(); break;
            case RcExport: if (it && hosts_.exportFile) hosts_.exportFile(name); break;
            case RcReplay: replaySelected(); break;
            case RcRename: if (it && hosts_.rename) hosts_.rename(name); break;
            case RcPassword: if (it && hosts_.password) hosts_.password(name); break;
            case RcDelete:
                if (!it) break;
                if (hosts_.remove) hosts_.remove(name);
                else (void)removeSelected();
                break;
            case RcFolder:
                if (hosts_.openFolder && !folder().empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(recPath(folder()), ec);
                    hosts_.openFolder(folder());
                }
                break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { showDetail(); });
    links_ += table_->activated->connect([this](ui::RowIndex) { replaySelected(); });
    links_ += doc_->changed->connect([this](Id) { scanned_.clear(); });
    refresh();
}

void HmiRecordingsPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

std::string HmiRecordingsPane::folder() const {
    return hmi::rec::folderOf(doc_->project.recording, hosts_.projectFolder ? hosts_.projectFolder() : doc_->savedFolder);
}

void HmiRecordingsPane::say(std::string text, bool warning) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
    invalidate();
}

const hmi::rec::Item* HmiRecordingsPane::selectedItem() const {
    const auto rows = table_ ? table_->selectedModelRows() : std::vector<ui::RowIndex>{};
    return rows.size() == 1 && rows.front() < items_.size() ? &items_[rows.front()] : nullptr;
}

std::string HmiRecordingsPane::selectedName() const {
    const auto* it = selectedItem();
    return it ? it->name : std::string{};
}

bool HmiRecordingsPane::select(const std::string& name) {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].name == name) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            showDetail();
            return true;
        }
    return false;
}

void HmiRecordingsPane::refresh() {
    const std::string keep = selectedName();
    const std::string dir = folder();
    // Le dossier natif existe toujours (un projet enregistre) : il ne se supprime pas d'ici.
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(recPath(dir), ec);
    }
    std::vector<std::string> notes;
    items_ = dir.empty() ? std::vector<hmi::rec::Item>{} : hmi::rec::listFolder(dir, &notes);
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles, lockStyles;
    std::uint64_t total = 0;
    std::string sig = dir;
    for (const auto& it : items_) {
        total += it.bytes;
        const auto& h = it.header;
        rows.push_back({it.name, it.problem.empty() ? hmi::rec::clock(h.duration) : std::string("?"), h.startView, recDate(h.date), h.where,
                        h.by.empty() ? std::string("\xE2\x80\x94") : h.by, hmi::rec::sizeText(it.bytes), h.locked() ? "oui" : ""});
        ui::CellStyle st;
        st.icon = ui::Icon::Play;
        if (!it.problem.empty()) st.iconTone = ui::Tone::Error;
        styles.push_back(st);
        ui::CellStyle lock;                    // le cadenas d'un enregistrement protege
        if (h.locked()) lock.icon = ui::Icon::Lock;
        lockStyles.push_back(lock);
        sig += "|" + it.name + ":" + std::to_string(it.bytes) + ":" + std::to_string(it.modified);
    }
    scanned_ = sig;
    model_ = std::make_shared<RowsModel>(std::vector<std::string>{"Nom", "Dur\xC3\xA9" "e", "Vue de d\xC3\xA9part", "Enregistr\xC3\xA9 le", "O\xC3\xB9", "Par",
                                                                  "Taille", "Prot\xC3\xA9g\xC3\xA9"},
                                         std::move(rows), std::move(styles), std::move(lockStyles));
    table_->setModel(model_);
    if (!keep.empty()) (void)select(keep);
    showDetail();
    std::string msg = dir.empty() ? std::string("Le projet n'est pas encore enregistr\xC3\xA9 : pas de dossier des enregistrements.")
                                  : std::to_string(items_.size()) + " enregistrement(s), " + hmi::rec::sizeText(total) + " \xC2\xB7 " + dir;
    status_->setMessage(msg);
    for (const auto& n : notes) say(n, true);
    invalidate();
}

void HmiRecordingsPane::showDetail() {
    using PG = ui::PropertyGrid;
    std::vector<PG::Category> cats;
    const auto* it = selectedItem();
    if (!it) {
        PG::Category c;
        c.name = "Enregistrement";
        c.properties.push_back(hmikit::prop("", items_.empty() ? "Aucun enregistrement : l'Enregistreur, le bouton Enregistrer de la simulation, "
                                                                 "une action ou IHM_ENREG_DEMARRER en font un."
                                                               : "Choisis un enregistrement.",
                                            PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        if (auto* box = dynamic_cast<RecordingDetail*>(detailBox_)) box->setContent({}, {});
        detail_->setCategories(std::move(cats));
        return;
    }
    const auto& h = it->header;
    PG::Category c;
    c.name = it->name;
    const auto ro = [&c](std::string label, std::string value) { c.properties.push_back(hmikit::prop(std::move(label), std::move(value), PG::ValueType::ReadOnly)); };
    if (!it->problem.empty()) ro("Illisible", it->problem);
    // L'ecran a 40 % de la duree : la vue (et les popups) d'alors, gardee dans l'en-tete a l'arret (1.14.0, relecture
    // M4b : avant, la piste entiere etait relue a chaque choix de ligne) ; un enregistrement d'avant : la vue de depart.
    const std::string at40 = !h.at40.empty() ? h.at40 : h.startView;
    ro("\xC3\x89" "cran \xC3\xA0 40 %", at40);
    ro("Dur\xC3\xA9" "e", hmi::rec::clock(h.duration) + (h.cut ? " (coup\xC3\xA9 net : relu jusqu'au dernier bloc)" : ""));
    ro("Images", std::to_string(h.frames) + " (" + std::to_string(h.rateHz) + " / s)");
    ro("Clics", std::to_string(h.clicks));
    ro("Alarmes", std::to_string(h.alarms));
    ro("Vues", h.viewsShown.empty() ? h.startView : h.viewsShown);
    // 1.14.0 (captures finales) : des lignes courtes, qui tiennent dans leur case (avant : "Enregistr\xC3\xA9" disait la
    // date, le poste et l'operateur d'une traite, coupes ; le chemin entier du fichier) - le dossier est dans la barre
    // du bas (et Ouvrir le dossier).
    ro("Projet", h.project);
    if (!h.projectVersion.empty()) ro("Version", h.projectVersion);
    ro("Enregistr\xC3\xA9 le", recDate(h.date));
    ro("O\xC3\xB9", h.where);
    if (!h.by.empty()) ro("Par", h.by);
    ro("Prot\xC3\xA9g\xC3\xA9", h.locked() ? "oui, mot de passe" : "non");
    {
        const auto slash = it->path.find_last_of("/\\");
        ro("Fichier", slash == std::string::npos ? it->path : it->path.substr(slash + 1));
    }
    const std::string path = it->path;
    c.properties.push_back(hmikit::prop("Description", h.description, PG::ValueType::Text,
                                        [this, path](std::string_view v) {
                                            std::string why;
                                            if (!hmi::rec::setDescription(path, std::string(v), &why)) {
                                                say("Description : " + why, true);
                                                return false;
                                            }
                                            say("Description enregistr\xC3\xA9" "e.");
                                            refresh();
                                            return true;
                                        },
                                        "Ce qu'on voit dans cet enregistrement (elle est lisible sans le mot de passe)."));
    std::vector<std::string> names;
    for (const auto& pr : c.properties) names.push_back(pr.name);
    if (auto* box = dynamic_cast<RecordingDetail*>(detailBox_)) box->setContent(names, h.description);
    cats.push_back(std::move(c));
    detail_->setCategories(std::move(cats));
}

std::vector<std::string> HmiRecordingsPane::descriptionLines() const {
    const auto* box = dynamic_cast<const RecordingDetail*>(detailBox_);
    return box ? box->noteLines() : std::vector<std::string>{};
}

float HmiRecordingsPane::detailValueWidth() const {
    const auto* box = dynamic_cast<const RecordingDetail*>(detailBox_);
    return box ? box->valueWidth() : 0.f;
}

bool HmiRecordingsPane::importPath(const std::string& path, std::string* why) {
    std::string name, reason;
    if (folder().empty()) reason = "le projet n'est pas encore enregistr\xC3\xA9";
    else if (hmi::rec::importFile(path, folder(), name, &reason)) {
        refresh();
        (void)select(name);
        say("Import\xC3\xA9 : " + name);
        return true;
    }
    say("Importer : " + reason, true);
    if (why) *why = reason;
    return false;
}

bool HmiRecordingsPane::exportTo(const std::string& destination, std::string* why, const ExportChoice& choice) {
    const auto* it = selectedItem();
    std::string written, reason = "aucun enregistrement choisi";
    // 1.14.0 (relecture M4b) : la maquette (scene 3) - un fichier deja la n'est pas ecrase sans le demander (replace),
    // et la copie d'un enregistrement protege garde son mot de passe, ou le perd si on donne le bon.
    hmi::rec::ExportOptions eo;
    eo.ifExists = choice.replace ? hmi::rec::ExportOptions::IfExists::Replace : hmi::rec::ExportOptions::IfExists::Refuse;
    eo.unprotect = choice.keepPassword ? std::string{} : choice.password;
    bool existed = false;
    hmi::rec::Status st = hmi::rec::Status::Ok;
    eo.exists = &existed;
    eo.status = &st;
    if (it && !it->header.locked() && !choice.keepPassword) eo.unprotect.clear();
    if (it && it->header.locked() && !choice.keepPassword && choice.password.empty()) {
        reason = "le mot de passe est demand\xC3\xA9 pour une copie sans protection";
        st = hmi::rec::Status::PasswordNeeded;
    } else if (it && hmi::rec::exportFile(folder(), it->name, destination, &written, &reason, eo)) {
        say("Export\xC3\xA9 : " + written + (it->header.locked() && !choice.keepPassword ? " (sans mot de passe)" : std::string{}));
        return true;
    }
    if (choice.existed) *choice.existed = existed;
    if (choice.status) *choice.status = st;
    say("Exporter : " + reason, true);
    if (why) *why = reason;
    return false;
}

bool HmiRecordingsPane::renameTo(const std::string& wanted, std::string* why) {
    const auto* it = selectedItem();
    std::string reason = "aucun enregistrement choisi";
    const std::string to = hmi::rec::cleanName(wanted);
    if (it && it->name == to) return true;
    if (it && hmi::rec::renameFile(folder(), it->name, to, &reason)) {
        const std::string from = it->name;
        // Ce qui le cite en dur suit ; le fichier et les citations font UN pas (Ctrl+Z rend les deux).
        std::size_t count = 0;
        auto citations = hmi::changeProject(doc_, "Renommer l'enregistrement " + from, [&](hmi::Project& p) { count = hmi::rec::renameReferences(p, from, to); });
        if (apply_)
            apply_(std::make_unique<RecordingRenameCommand>(folder(), from, to, count > 0 ? std::move(citations) : nullptr, [this] { refresh(); }));
        refresh();
        (void)select(to);
        say("Renomm\xC3\xA9 : " + from + " \xE2\x86\x92 " + to + (count ? " (" + std::to_string(count) + " citation(s) suivent)" : std::string{}));
        return true;
    }
    say("Renommer : " + reason, true);
    if (why) *why = reason;
    return false;
}

bool HmiRecordingsPane::setPassword(const std::string& current, const std::string& fresh, std::string* why) {
    const auto* it = selectedItem();
    std::string reason = "aucun enregistrement choisi";
    if (it) {
        const auto st = hmi::rec::setPassword(it->path, current, fresh, &reason);
        if (st == hmi::rec::Status::Ok) {
            refresh();
            say(fresh.empty() ? "Protection retir\xC3\xA9" "e." : "Prot\xC3\xA9g\xC3\xA9 par un mot de passe.");
            return true;
        }
    }
    say("Mot de passe : " + reason, true);
    if (why) *why = reason;
    return false;
}

bool HmiRecordingsPane::setDescription(const std::string& text, std::string* why) {
    const auto* it = selectedItem();
    std::string reason = "aucun enregistrement choisi";
    if (it && hmi::rec::setDescription(it->path, text, &reason)) {
        refresh();
        return true;
    }
    if (why) *why = reason;
    return false;
}

bool HmiRecordingsPane::removeSelected(std::string* why) {
    const auto* it = selectedItem();
    std::string reason = "aucun enregistrement choisi";
    const std::string pf = hosts_.projectFolder ? hosts_.projectFolder() : doc_->savedFolder;
    const std::string trash = pf.empty() ? std::string{} : pf + "/ihm/corbeille";
    if (it) {
        const std::string name = it->name;
        if (hmi::rec::removeFile(folder(), name, trash, &reason)) {
            refresh();
            say("Supprim\xC3\xA9 : " + name + (trash.empty() ? std::string{} : " (dans la corbeille du projet)"));
            return true;
        }
    }
    say("Supprimer : " + reason, true);
    if (why) *why = reason;
    return false;
}

void HmiRecordingsPane::replaySelected() {
    const auto* it = selectedItem();
    if (!it) return;
    if (!it->problem.empty()) {
        say("Rejouer : " + it->problem, true);
        return;
    }
    hmi::ReplayRequest rq;
    rq.name = it->name;
    rq.source = "Ressources \xE2\x80\xBA Enregistrements";
    if (hosts_.replay) hosts_.replay(rq);
    else say("Rejouer : pas de simulation ici", true);
}

void HmiRecordingsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

void HmiRecordingsPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    // Le dossier change sans nous (un enregistrement qui finit, un fichier copie) : relu toutes les 2 s.
    if (ctx.time - lastScan_ >= 2.0) {
        lastScan_ = ctx.time;
        std::string sig = folder();
        for (const auto& name : hmi::rec::fileNames(folder())) sig += "|" + name;
        std::string now = folder();
        for (const auto& it : items_) now += "|" + it.name;
        if (scanned_.empty() || sig != now) refresh();
    }
}

// ---- Configuration > Enregistrements -------------------------------------------------------------------------
HmiRecordingSettingsPane::HmiRecordingSettingsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiRecordingSettingsPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiRecordingSettingsPane::say(std::string text, bool error) {
    if (error) ui::PropertyGrid::refuse(text);
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiRecordingSettingsPane::setSetting(const std::string& key, const std::string& raw, std::string* why) {
    using hmikit::trimmed;
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string v = trimmed(raw);
    const auto apply = [&](const std::string& label, const std::function<void(hmi::RecordingSettings&)>& fn) {
        auto cmd = hmi::changeProject(doc_, "Enregistrements : " + label, [&](hmi::Project& p) { fn(p.recording); });
        if (!cmd) return false;
        apply_(std::move(cmd));
        refresh();
        return true;
    };
    if (key == "pointeur" || key == "sons" || key == "popups" || key == "commandes") {
        const bool on = hmikit::yes(v);
        return apply(key == "pointeur" ? "pointeur et clics" : key == "sons" ? "sons" : key == "popups" ? "popups" : "commandes d'une autre application",
                     [&](hmi::RecordingSettings& r) {
                         if (key == "pointeur") r.pointer = on;
                         else if (key == "sons") r.sounds = on;
                         else if (key == "popups") r.popups = on;
                         else r.external = on;
                     });
    }
    if (key == "dossier") {
        if (v.find_first_of("*?\"<>|") != std::string::npos) return fail("Dossier : un chemin sans * ? \" < > |");
        return apply("dossier", [&](hmi::RecordingSettings& r) { r.folder = v; });
    }
    struct Num {
        const char* key;
        long long   lo, hi;
        const char* label;
    };
    static const Num kNums[] = {{"duree", 1, 24 * 60, "Dur\xC3\xA9" "e au plus (min)"},
                                {"taille", 10, 1024 * 1024, "Taille du dossier au plus (Mo)"},
                                {"frequence", 1, 50, "Prises par seconde"}};
    for (const auto& k : kNums)
        if (key == k.key) {
            char* end = nullptr;
            const long long x = std::strtoll(v.c_str(), &end, 10);
            if (v.empty() || (end && *end) || x < k.lo || x > k.hi)
                return fail(std::string(k.label) + " : un nombre de " + std::to_string(k.lo) + " \xC3\xA0 " + std::to_string(k.hi));
            return apply(k.label, [&](hmi::RecordingSettings& r) {
                if (key == "duree") r.maxMinutes = static_cast<int>(x);
                else if (key == "taille") r.maxFolderMb = static_cast<int>(x);
                else r.rateHz = static_cast<int>(x);
            });
        }
    return fail("r\xC3\xA9glage inconnu : " + key);
}

void HmiRecordingSettingsPane::refresh() {
    using PG = ui::PropertyGrid;
    using hmikit::prop;
    using hmikit::tf;
    const auto& r = doc_->project.recording;
    const auto commit = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setSetting(key, std::string(v));
        };
    };
    const std::string pf = hosts_.projectFolder ? hosts_.projectFolder() : doc_->savedFolder;
    const std::string dir = hmi::rec::folderOf(r, pf);
    std::vector<PG::Category> cats;
    PG::Category where;
    where.name = "O\xC3\xB9";
    where.properties.push_back(prop("Dossier", r.folder, PG::ValueType::Text, commit("dossier"),
                                    "Vide : le dossier natif ressources/enregistrements du projet (Ressources \xE2\x80\xBA Enregistrements). "
                                    "Un chemin relatif part du dossier du projet. Au poste d'exploitation : le m\xC3\xAAme dossier, sur le PC du poste."));
    where.properties.push_back(prop("Chemin", dir.empty() ? std::string("(le projet n'est pas encore enregistr\xC3\xA9)") : dir, PG::ValueType::ReadOnly));
    where.properties.push_back(prop("Taille du dossier au plus (Mo)", std::to_string(r.maxFolderMb), PG::ValueType::Integer, commit("taille"),
                                    "Au-del\xC3\xA0, \xC3\xA0 la fin d'un enregistrement, les plus anciens sont retir\xC3\xA9s (le journal le dit). "
                                    "2048 : 2 Go."));
    cats.push_back(std::move(where));
    PG::Category what;
    what.name = "Ce qui est enregistr\xC3\xA9";
    what.properties.push_back(prop("Dur\xC3\xA9" "e au plus (min)", std::to_string(r.maxMinutes), PG::ValueType::Integer, commit("duree"),
                                   "Celle d'un enregistrement sans dur\xC3\xA9" "e donn\xC3\xA9" "e (l'Enregistreur, l'action, la native) : au bout, il "
                                   "s'arr\xC3\xAAte et se garde."));
    what.properties.push_back(prop("Prises par seconde", std::to_string(r.rateHz), PG::ValueType::Integer, commit("frequence"),
                                   "10 : dix fois par seconde. Seul ce qui change est \xC3\xA9" "crit ; une image compl\xC3\xA8te toutes les 5 s."));
    what.properties.push_back(prop("Pointeur et clics", tf(r.pointer), PG::ValueType::Boolean, commit("pointeur"),
                                   "La souris (ou le doigt) de l'op\xC3\xA9rateur et ses clics, rejou\xC3\xA9s en fant\xC3\xB4me."));
    what.properties.push_back(prop("Sons", tf(r.sounds), PG::ValueType::Boolean, commit("sons"),
                                   "Les sons jou\xC3\xA9s (alarmes, actions, scripts) : rejou\xC3\xA9s sur le canal Relecture."));
    what.properties.push_back(prop("Popups", tf(r.popups), PG::ValueType::Boolean, commit("popups"),
                                   "Les popups ouvertes et leurs param\xC3\xA8tres. D\xC3\xA9" "coch\xC3\xA9 : la vue seule."));
    cats.push_back(std::move(what));
    PG::Category ext;
    ext.name = "Autre application";
    ext.properties.push_back(prop("Accepter les commandes d'une autre application", tf(r.external), PG::ValueType::Boolean, commit("commandes"),
                                  "IHM-Studio.exe --enregistrer \"Nom\", --arreter-enregistrement, --rejouer \"Nom\", --quitter-relecture : "
                                  "une autre application (un banc de test, un script) pilote les enregistrements de cette IHM. "
                                  "D\xC3\xA9" "coch\xC3\xA9 (par d\xC3\xA9" "faut) : refus\xC3\xA9" "es (code 5). Les codes de sortie : 0 fait, "
                                  "2 aucune IHM en marche, 3 nom introuvable, 4 mot de passe faux, 5 refus\xC3\xA9, 6 occup\xC3\xA9" "e (d\xC3\xA9j\xC3\xA0 "
                                  "un enregistrement ou une relecture), 7 fichier (\xC3\xA9" "criture impossible, illisible)."));
    cats.push_back(std::move(ext));
    grid_->setCategories(std::move(cats));
    status_->setMessage(dir.empty() ? std::string("Pas encore de dossier : enregistre le projet.")
                                    : std::to_string(hmi::rec::fileNames(dir).size()) + " enregistrement(s) \xC2\xB7 " + dir);
}

void HmiRecordingSettingsPane::onLayout() {
    const auto b = bounds();
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    grid_->setBounds({b.x, b.y, std::min(b.w, 720.f), std::max(0.f, b.h - 24)});
}

void HmiRecordingSettingsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
