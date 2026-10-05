#include "HmiHelpPane.hpp"

#include "HmiAssetPanes.hpp"
#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiExamples.hpp"
#include "../../help/Novelties.hpp"     // 1.10 (chantier P) : l'aide en orange
#include "../../ui/NoveltyMarks.hpp"    // 1.10 : la pastille NOUVEAU
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <iterator>
#include <map>

namespace app {

namespace guide = hmi::guide;

namespace {

enum ToolAction : int { TBack = 1, TTutorial, TReplay, TObjectTutorial, TOnlyNew, TMarkRead };

// A droite : l'exemple anime (la page d'un objet de la bibliotheque) au-dessus
// de l'article ; replie quand le sujet n'est pas un objet. Lot 16 : deux
// onglets pour un objet - la page, et son tutoriel (toute la place).
class HelpRight final : public ui::Widget {
public:
    explicit HelpRight(std::string id) : ui::Widget(std::move(id)) {}
    HmiExampleView*      example{nullptr};
    ui::HelpArticleView* article{nullptr};
    HmiObjectTutorial*   tutorial{nullptr};
    HmiTrailCards*       trails{nullptr};         // lot 21 : les parcours (la page du didacticiel)
    bool                 showTrails{false};
    int                  mode{0};                 // 0 : la page ; 1 : le tutoriel
    std::function<void(int)> modeRequested;
    gfx::Rect            tabs[2]{};
    [[nodiscard]] bool tabbed() const { return example && example->hasExample(); }
protected:
    void onLayout() override {
        const auto b = bounds();
        const float tabH = tabbed() ? 34.f : 0.f;
        tabs[0] = tabbed() ? gfx::Rect{b.x + 10.f, b.y + 4.f, 150.f, tabH - 6.f} : gfx::Rect{};
        tabs[1] = tabbed() ? gfx::Rect{b.x + 166.f, b.y + 4.f, 210.f, tabH - 6.f} : gfx::Rect{};
        const gfx::Rect body{b.x, b.y + tabH, b.w, std::max(0.f, b.h - tabH)};
        const bool tuto = tabbed() && mode == 1;
        if (tutorial) {
            tutorial->setVisibility(tuto ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            tutorial->setBounds(tuto ? body : gfx::Rect{});
        }
        float h = !tuto && tabbed() ? std::clamp(body.h * 0.46f, 180.f, 380.f) : 0.f;
        if (example) {
            example->setVisibility(h > 0.f ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            example->setBounds({body.x, body.y, body.w, h});
        }
        // Lot 21 : la page du didacticiel porte ses parcours au-dessus du texte.
        if (trails) {
            const bool on = showTrails && !tuto && h <= 0.f;
            const float th = on ? std::min(trails->preferredHeight(body.w), body.h * 0.74f) : 0.f;
            trails->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            trails->setBounds({body.x, body.y, body.w, th});
            if (on) h = th;
        }
        if (article) {
            article->setVisibility(tuto ? ui::Visibility::Collapsed : ui::Visibility::Visible);
            article->setBounds({body.x, body.y + h, body.w, tuto ? 0.f : std::max(0.f, body.h - h)});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        if (!tabbed()) return;
        const auto& c = ctx.theme.color;
        const auto& f = ctx.theme.font;
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 34.f}, c.windowBg);
        ctx.r.line({b.x, b.y + 33.f}, {b.right(), b.y + 33.f}, c.border, 1.f);
        const char* labels[2] = {"Page de l'objet", "Tutoriel \xC2\xB7 jou\xC3\xA9 par le moteur"};
        for (int i = 0; i < 2; ++i) {
            const auto& t = tabs[i];
            const bool on = mode == i;
            if (on) {
                ctx.r.fillRoundedRect(t, c.panelBg, 5.f);
                ctx.r.fillRect({t.x + 6.f, t.bottom() - 3.f, t.w - 12.f, 3.f}, c.accent);
            }
            drawHmiGlyph(ctx.r, i == 0 ? HmiGlyph::Help : HmiGlyph::Play, {t.x + 8.f, t.y + (t.h - 16.f) / 2.f, 16.f, 16.f}, on ? c.accent : c.textMuted);
            ctx.r.drawText({t.x + 30.f, t.y + (t.h - ctx.r.lineHeight(f.ui)) / 2.f}, labels[i], on ? f.uiBold : f.ui, on ? c.text : c.textMuted);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && tabbed())
            for (int i = 0; i < 2; ++i)
                if (tabs[i].contains(d->pos) && modeRequested) {
                    modeRequested(i);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Ignored;
    }
};

// Lot 21 : LES ICONES DE L'AIDE. Chaque chapitre a la sienne (et sa couleur),
// que ses sujets reprennent ; un objet de la bibliotheque a l'icone de sa tuile.
// Deux colonnes de marques : l'exemple anime, le tutoriel joue par le moteur.
// La legende, sous la liste, les explique (un clic sur son titre la replie).
struct ChapterLook {
    const char* name;
    HmiGlyph    glyph;
    ui::Tone    tone;
    const char* label;      // dans la legende
};
const ChapterLook kChapterLooks[] = {
    {"D\xC3\xA9marrer", HmiGlyph::Help, ui::Tone::Accent, "D\xC3\xA9marrer"},
    {"Concevoir les vues", HmiGlyph::View, ui::Tone::Family0, "Concevoir les vues"},
    {"La biblioth\xC3\xA8que d'objets", HmiGlyph::Template, ui::Tone::Family1, "Les objets"},
    {"Programmer", HmiGlyph::Code, ui::Tone::Family2, "Programmer"},
    {"Variables syst\xC3\xA8me et d'instances", HmiGlyph::SystemVars, ui::Tone::Family3, "Variables syst\xC3\xA8me"},
    {"Superviser", HmiGlyph::Bell, ui::Tone::Warning, "Superviser"},
    {"Ressources et donn\xC3\xA9" "es", HmiGlyph::Image, ui::Tone::Family4, "Ressources, donn\xC3\xA9" "es"},
    {"Tester et livrer", HmiGlyph::Check, ui::Tone::Ok, "Tester et livrer"},
    // 1.10 (H) : les sujets de l'API (le grafcet, son code) - fusion de l'aide 1.10.
    {"L'automate (API)", HmiGlyph::Gear, ui::Tone::Family5, "L'automate (API)"},
};
const ChapterLook& lookOf(const std::string& chapter) {
    static const ChapterLook other{"", HmiGlyph::Help, ui::Tone::Muted, ""};
    for (const auto& c : kChapterLooks)
        if (chapter == c.name) return c;
    return other;
}
// Un numero d'icone pour la table (0 : aucune) : le glyphe de l'IHM, plus un.
int iconOf(HmiGlyph g) { return static_cast<int>(g) + 1; }

// Le glyphe d'un sujet qui n'est pas un objet : par sa cle ; sinon celui de son chapitre.
HmiGlyph topicGlyph(const guide::Topic& t) {
    static const std::pair<const char*, HmiGlyph> map[] = {
        {"ihm", HmiGlyph::System}, {"historique", HmiGlyph::Undo}, {"onglets", HmiGlyph::TabContainer},
        {"aller-a", HmiGlyph::Search}, {"excel", HmiGlyph::Paste}, {"didacticiel", HmiGlyph::Help},
        {"versions", HmiGlyph::History}, {"comparer-versions", HmiGlyph::Compare}, {"dossiers", HmiGlyph::Group},
        {"tutoriel-symbole", HmiGlyph::Symbol},
        {"vues", HmiGlyph::View}, {"editeur", HmiGlyph::Select}, {"objets", HmiGlyph::Template},
        {"proprietes", HmiGlyph::Style}, {"actions", HmiGlyph::Link}, {"modeles", HmiGlyph::Template},
        {"popups", HmiGlyph::Popup}, {"symboles", HmiGlyph::Symbol}, {"contenu", HmiGlyph::Table},
        {"concevoir-vite", HmiGlyph::Duplicate}, {"modeles-de-vues", HmiGlyph::Template}, {"paquets-de-vues", HmiGlyph::Export},
        {"styles-nommes", HmiGlyph::Style}, {"rechercher-remplacer", HmiGlyph::Search}, {"langues", HmiGlyph::Language},
        {"unites-formats", HmiGlyph::NumericDisplay}, {"affichage", HmiGlyph::Theme}, {"communication", HmiGlyph::Network},
        {"qualite-valeurs", HmiGlyph::Diagnostic}, {"equipements", HmiGlyph::Network}, {"variables-liees", HmiGlyph::Link},
        {"zones-memoire", HmiGlyph::Table}, {"carte-memoire", HmiGlyph::Grid}, {"esclaves-virtuels", HmiGlyph::Compare},
        {"valeurs-simulees", HmiGlyph::Slider}, {"reseau-pc", HmiGlyph::Network}, {"scanner-ip", HmiGlyph::Search},
        {"outil-modbus", HmiGlyph::Diagnostic}, {"synoptique", HmiGlyph::Valve}, {"graphiques", HmiGlyph::BarChart},
        {"alarmes-objets", HmiGlyph::AlarmBanner}, {"production", HmiGlyph::ProductionCounter}, {"navigation", HmiGlyph::NavBar},
        {"navigation-marche", HmiGlyph::Breadcrumb}, {"scripts", HmiGlyph::Code}, {"variables-ihm", HmiGlyph::InstanceVars},
        {"types-ihm", HmiGlyph::Structure}, {"tableaux-ihm", HmiGlyph::Table}, {"variables-locales", HmiGlyph::Code},
        {"fonctions", HmiGlyph::Code}, {"reference", HmiGlyph::Help}, {"aide-saisie", HmiGlyph::Keyboard},
        {"variables-systeme", HmiGlyph::SystemVars}, {"variables-instances", HmiGlyph::InstanceVars}, {"alarmes", HmiGlyph::Bell},
        {"recettes", HmiGlyph::RecipeManager}, {"securite", HmiGlyph::Lock}, {"signature", HmiGlyph::Signature},
        {"audit", HmiGlyph::Audit}, {"parametres-systeme", HmiGlyph::Gear}, {"menu-connexion", HmiGlyph::LoginMenu},
        {"historiques", HmiGlyph::Trend}, {"ressources", HmiGlyph::Image}, {"fichiers", HmiGlyph::Import},
        {"actions-ressources", HmiGlyph::Link}, {"simulation", HmiGlyph::Play}, {"verifier", HmiGlyph::Check},
        {"essais", HmiGlyph::Check}, {"performances", HmiGlyph::Diagnostic}, {"poste-exploitation", HmiGlyph::Station},
        {"reprise", HmiGlyph::Refresh}, {"etats-projet", HmiGlyph::Lock}, {"notifications", HmiGlyph::Mail},
        {"rapports", HmiGlyph::Report}, {"acces-web", HmiGlyph::Web}, {"configuration", HmiGlyph::Gear},
        {"echange", HmiGlyph::Export}, {"dossier", HmiGlyph::Report}, {"raccourcis", HmiGlyph::Keyboard},
    };
    if (!t.kind.empty() && t.kind != "*")
        if (const auto kind = hmi::kindFromKey(t.kind)) return glyphFor(*kind);
    for (const auto& [key, glyph] : map)
        if (t.key == key) return glyph;
    return lookOf(t.chapter).glyph;
}

// L'objet a-t-il un exemple anime (et donc son tutoriel joue par le moteur) ?
// Construire un exemple coute : une fois par genre.
bool hasExample(const guide::Topic& t) {
    if (t.kind.empty() || t.kind == "*") return false;
    const auto kind = hmi::kindFromKey(t.kind);
    if (!kind) return false;
    static std::map<hmi::Kind, bool> known;
    const auto it = known.find(*kind);
    if (it != known.end()) return it->second;
    const bool has = hmi::examples::exampleFor(*kind).has_value();
    known.emplace(*kind, has);
    return has;
}

enum TopicColumn : std::size_t { CTopic = 0, CExample, CTutorial, CCount };

// Une ligne de la liste : un chapitre (en retrait nul, son icone) ou un sujet.
class TopicRows final : public ui::ITableModel {
public:
    struct Row {
        std::string text;
        bool        chapter{false};
        std::string hint;
        HmiGlyph    glyph{HmiGlyph::None};
        ui::Tone    tone{ui::Tone::None};
        bool        example{false}, tutorial{false}, written{false};
        bool        fresh{false};       // 1.10 : nouveau (ou change) pour le lecteur
        bool        changed{false};     // 1.10 : un sujet ancien dont des paragraphes ont change (MODIFIE)
    };
    explicit TopicRows(std::vector<Row> rows) : rows_(std::move(rows)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == CTopic ? "Sujets" : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_.size() || c != CTopic) return {};
        return rows_[r].chapter ? rows_[r].text : "  " + rows_[r].text;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows_.size()) return s;
        const Row& row = rows_[r];
        switch (c) {
            case CTopic:
                s.customIcon = row.glyph == HmiGlyph::None ? 0 : iconOf(row.glyph);
                s.iconTone = row.tone;
                if (row.chapter) {
                    s.fgTone = ui::Tone::Accent;
                    s.bold = true;
                } else {
                    s.indent = 12.f;
                }
                if (row.fresh) {
                    // 1.10 : la pastille NOUVEAU (un sujet nouveau) ou MODIFIE (un
                    // sujet ancien qui a change), a droite de la ligne (la maquette).
                    s.badge = row.changed ? "MODIFI\xC3\x89" : ui::novelty::kPillText;
                    s.badgeTone = ui::Tone::Warning;
                }
                break;
            case CExample:
                if (row.example) {
                    s.customIcon = iconOf(HmiGlyph::Eye);
                    s.iconTone = ui::Tone::Accent;
                }
                break;
            case CTutorial:
                if (row.tutorial) {
                    s.customIcon = iconOf(HmiGlyph::Play);
                    s.iconTone = row.written ? ui::Tone::Warning : ui::Tone::Ok;
                }
                break;
            default: break;
        }
        return s;
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= rows_.size() || rows_[r].chapter) return {};
        const Row& row = rows_[r];
        std::string tip = row.text;
        if (row.fresh)
            tip += row.changed ? "\nChang\xC3\xA9 depuis la derni\xC3\xA8re version que tu as vue : ce qui a chang\xC3\xA9 est encadr\xC3\xA9 en orange."
                               : "\nNouveau depuis la derni\xC3\xA8re version que tu as vue : encadr\xC3\xA9 en orange.";
        if (row.example) tip += "\nExemple anim\xC3\xA9 : l'objet en marche, en haut de sa page.";
        if (row.tutorial)
            tip += row.written ? "\nTutoriel \xC3\xA9" "crit, jou\xC3\xA9 par le moteur (onglet Tutoriel)."
                               : row.example ? "\nTutoriel jou\xC3\xA9 par le moteur, d\xC3\xA9" "duit de l'exemple (onglet Tutoriel)."
                                             : "\nLes parcours du didacticiel : des exercices guid\xC3\xA9s.";
        return tip;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
private:
    std::vector<Row> rows_;
};

// La legende de la liste : les chapitres, les marques. Un clic sur son titre la replie.
class HelpLegend final : public ui::Widget {
public:
    explicit HelpLegend(std::string id) : ui::Widget(std::move(id)) {}
    bool open{true};
    [[nodiscard]] float wanted() const {
        const std::size_t items = std::size(kChapterLooks) + 4;
        return open ? 24.f + static_cast<float>((items + 1) / 2) * 19.f + 8.f : 24.f;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto& f = ctx.theme.font;
        const auto b = bounds();
        ctx.r.fillRect(b, c.headerBg);
        ctx.r.line({b.x, b.y}, {b.right(), b.y}, c.border, 1.f);
        const float ch = ctx.r.lineHeight(f.caption);
        ctx.r.drawText({b.x + 10.f, b.y + (24.f - ch) / 2.f}, open ? "L\xC3\xA9gende" : "L\xC3\xA9gende (un clic pour la montrer)", f.caption, c.textMuted);
        // Le chevron, a droite.
        const float ax = b.right() - 16.f, ay = b.y + 12.f;
        if (open) {
            ctx.r.line({ax - 4.f, ay - 2.f}, {ax, ay + 2.f}, c.textMuted, 1.5f);
            ctx.r.line({ax, ay + 2.f}, {ax + 4.f, ay - 2.f}, c.textMuted, 1.5f);
        } else {
            ctx.r.line({ax - 4.f, ay + 2.f}, {ax, ay - 2.f}, c.textMuted, 1.5f);
            ctx.r.line({ax, ay - 2.f}, {ax + 4.f, ay + 2.f}, c.textMuted, 1.5f);
        }
        if (!open) return;
        struct Item { HmiGlyph glyph; ui::Tone tone; std::string label; };
        std::vector<Item> items;
        for (const auto& look : kChapterLooks) items.push_back({look.glyph, look.tone, look.label});
        items.push_back({HmiGlyph::Eye, ui::Tone::Accent, "exemple anim\xC3\xA9"});
        items.push_back({HmiGlyph::Play, ui::Tone::Ok, "tutoriel (de l'exemple)"});
        items.push_back({HmiGlyph::Play, ui::Tone::Warning, "tutoriel \xC3\xA9" "crit"});
        items.push_back({HmiGlyph::Button, ui::Tone::Family1, "objet : sa tuile"});
        const float colW = (b.w - 20.f) / 2.f;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const float x = b.x + 10.f + static_cast<float>(i % 2) * colW;
            const float y = b.y + 24.f + static_cast<float>(i / 2) * 19.f;
            drawHmiGlyph(ctx.r, items[i].glyph, {x, y + 1.f, 15.f, 15.f}, ctx.theme.tone(items[i].tone, c.textMuted));
            ctx.r.drawText({x + 21.f, y + (17.f - ch) / 2.f}, items[i].label, f.caption, c.text);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left
                                                             && d->pos.y < bounds().y + 24.f && bounds().contains(d->pos)) {
            open = !open;
            if (parent()) parent()->invalidateLayout();
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
};

// La liste des sujets et sa legende, l'une sous l'autre.
class TopicsBody final : public ui::Widget {
public:
    explicit TopicsBody(std::string id) : ui::Widget(std::move(id)) {}
    ui::TableView* table{nullptr};
    HelpLegend*    legend{nullptr};
protected:
    void onLayout() override {
        const auto b = bounds();
        const float lh = legend ? std::min(legend->wanted(), std::max(24.f, b.h * 0.45f)) : 0.f;
        if (table) table->setBounds({b.x, b.y, b.w, std::max(0.f, b.h - lh)});
        if (legend) legend->setBounds({b.x, b.bottom() - lh, b.w, lh});
    }
};

ui::HelpBlock block(ui::HelpBlockKind kind, std::string text) {
    ui::HelpBlock b;
    b.kind = kind;
    b.text = std::move(text);
    return b;
}

// 1.10 : la notation des exemples de code choisie par le lecteur (ST, C, C++).
std::string codeNotation() {
    const auto& n = help::news::session().helpNotation;
    return n == "C" || n == "C++" ? n : std::string("ST");
}

} // namespace

bool HmiHelpPane::isNewTopic(const guide::Topic& t) {
    return help::news::helpIsNew(help::news::session(), guide::latestChange(t));
}

std::size_t HmiHelpPane::newTopicCount() {
    return static_cast<std::size_t>(std::count_if(guide::topics().begin(), guide::topics().end(),
                                                  [](const guide::Topic& t) { return isNewTopic(t); }));
}

ui::HelpArticle HmiHelpPane::compose(const guide::Topic& t) {
    ui::HelpArticle a;
    // 1.10 : un sujet nouveau pour le lecteur est encadre tout entier ; dans un
    // sujet ancien, les blocs qui ont change (@nouveau) le sont un a un.
    const auto& news = help::news::session();
    const std::string topicLabel = help::news::helpIsNew(news, t.since) ? help::news::label(t.since) : std::string{};
    const auto labelOf = [&](const guide::Block& b) {
        if (!topicLabel.empty()) return topicLabel;
        return help::news::helpIsNew(news, b.since) ? help::news::label(b.since) : std::string{};
    };
    // L'en-tete : le fil (Aide IHM / chapitre), le titre.
    {
        ui::HelpBlock hero = block(ui::HelpBlockKind::Hero, t.title);
        hero.links.push_back({"Aide IHM", "topic:ihm", "Le sommaire de l'aide"});
        hero.links.push_back({t.chapter, "chapter:" + t.chapter, "Le premier sujet de ce chapitre"});
        hero.links.push_back({t.title, {}, {}});
        hero.tone = ui::Tone::Accent;
        hero.icon = ui::Icon::Info;
        hero.novelty = topicLabel;
        a.blocks.push_back(std::move(hero));
    }
    a.blocks.push_back(block(ui::HelpBlockKind::Lead, guide::plain(t.summary)));
    a.blocks.back().novelty = topicLabel;
    for (std::size_t bi = 0; bi < t.blocks.size(); ++bi) {
        const auto& b = t.blocks[bi];
        const std::size_t before = a.blocks.size();
        if (b.kind == guide::BlockKind::Code && !b.label.empty()) {
            // 1.10 : UN EXEMPLE A PLUSIEURS NOTATIONS (des blocs ```ST, ```C,
            // ```C++ qui se suivent) : un seul cadre, la notation choisie
            // (gardee dans les reglages : aide.notation), le selecteur en tete.
            std::size_t last = bi;
            while (last + 1 < t.blocks.size() && t.blocks[last + 1].kind == guide::BlockKind::Code && !t.blocks[last + 1].label.empty())
                ++last;
            const std::string want = codeNotation();
            std::size_t shown = bi;
            for (std::size_t k = bi; k <= last; ++k)
                if (t.blocks[k].label == want) shown = k;
            auto x = block(ui::HelpBlockKind::Code, t.blocks[shown].text);
            x.label = t.blocks[shown].label;
            std::string since;
            for (std::size_t k = bi; k <= last; ++k) {
                const auto& n = t.blocks[k];
                ui::HelpLink link{n.label, "notation:" + n.label,
                                  "L'exemple en " + n.label + " (le choix est gard\xC3\xA9 pour tous les exemples)"};
                if (k == shown) link.tone = ui::Tone::Accent;
                x.links.push_back(std::move(link));
                if (help::news::compareVersions(n.since, since) > 0) since = n.since;
            }
            x.novelty = !topicLabel.empty() ? topicLabel
                      : help::news::helpIsNew(news, since) ? help::news::label(since) : std::string{};
            a.blocks.push_back(std::move(x));
            bi = last;
            continue;
        }
        switch (b.kind) {
            case guide::BlockKind::Heading:   a.blocks.push_back(block(ui::HelpBlockKind::Heading, guide::plain(b.text))); break;
            case guide::BlockKind::Paragraph: a.blocks.push_back(block(ui::HelpBlockKind::Paragraph, guide::plain(b.text))); break;
            case guide::BlockKind::Bullet: {
                auto x = block(ui::HelpBlockKind::Bullet, guide::plain(b.text));
                x.label = "-";
                a.blocks.push_back(std::move(x));
                break;
            }
            case guide::BlockKind::Step: {
                auto x = block(ui::HelpBlockKind::Bullet, guide::plain(b.text));
                x.label = b.label;
                x.tone = ui::Tone::Accent;
                a.blocks.push_back(std::move(x));
                break;
            }
            case guide::BlockKind::Code:      a.blocks.push_back(block(ui::HelpBlockKind::Code, b.text)); break;
            case guide::BlockKind::Tip:
            case guide::BlockKind::Warning: {
                auto x = block(ui::HelpBlockKind::Callout, guide::plain(b.text));
                x.severity = b.kind == guide::BlockKind::Warning ? 1 : 0;
                x.label = b.kind == guide::BlockKind::Warning ? "Attention" : "Astuce";
                a.blocks.push_back(std::move(x));
                break;
            }
            case guide::BlockKind::Table:     a.blocks.push_back(block(ui::HelpBlockKind::Table, guide::plain(b.text))); break;
        }
        for (std::size_t i = before; i < a.blocks.size(); ++i) a.blocks[i].novelty = labelOf(b);
    }
    if (!t.see.empty()) {
        ui::HelpBlock links;
        links.novelty = topicLabel;
        links.kind = ui::HelpBlockKind::Links;
        links.label = "Voir aussi";
        for (const auto& k : t.see)
            if (const auto* o = guide::topic(k)) links.links.push_back({o->title, "topic:" + k, guide::plain(o->summary)});
        a.blocks.push_back(std::move(links));
    }
    return a;
}

HmiHelpPane::HmiHelpPane(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TBack, HmiGlyph::Undo, "Revenir au sujet pr\xC3\xA9" "c\xC3\xA9" "dent", "Pr\xC3\xA9" "c\xC3\xA9" "dent");
    tools->separator();
    tools->add(TTutorial, HmiGlyph::Play, "Didacticiel : les parcours - la visite de l'IHM, cr\xC3\xA9" "er un symbole, coller depuis Excel, l'historique et les versions...",
               "Didacticiel");
    tools->add(TReplay, HmiGlyph::Redo, "Rejouer l'exemple anim\xC3\xA9 de l'objet depuis le d\xC3\xA9" "but", "Rejouer l'exemple");
    tools->add(TObjectTutorial, HmiGlyph::Play, "Le tutoriel de l'objet : son exemple en chapitres, jou\xC3\xA9 par le vrai moteur, puis \xC3\x80 toi",
               "Tutoriel de l'objet");
    // 1.10 (chantier P) : les nouveautes de l'aide, en orange.
    tools->separator();
    tools->add(TOnlyNew, HmiGlyph::StarFilled, "Ne garder dans la liste que les sujets nouveaux ou chang\xC3\xA9s depuis la derni\xC3\xA8re version que tu as vue",
               "Nouveaut\xC3\xA9s seulement");
    tools->add(TMarkRead, HmiGlyph::Check, "Retirer l'orange de toute l'aide : les nouveaut\xC3\xA9s sont lues", "Tout marquer comme lu");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(TBack, [this] { return !history_.empty(); });
    tools_->setEnabledWhen(TTutorial, [this] { return static_cast<bool>(hosts_.replayTutorial) || static_cast<bool>(hosts_.startTrail); });
    tools_->setEnabledWhen(TReplay, [this] { return example_ && example_->hasExample(); });
    tools_->setEnabledWhen(TObjectTutorial, [this] { return example_ && example_->hasExample(); });
    tools_->setCheckedWhen(TOnlyNew, [this] { return onlyNew_; });
    tools_->setEnabledWhen(TOnlyNew, [this] { return onlyNew_ || newTopicCount() > 0; });
    tools_->setEnabledWhen(TMarkRead, [] { return newTopicCount() > 0; });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".topicsPanel", "AIDE DE L'IHM");
        auto field = std::make_unique<ui::InputText>(base + ".search");
        field->setPlaceholder("Chercher : fonction, VAR_TEMP, alarme...");
        search_ = &static_cast<ui::InputText&>(panel->addRow(std::move(field), 30.f));
        auto body = std::make_unique<TopicsBody>(base + ".topicsBody");
        auto table = std::make_unique<ui::TableView>(base + ".topics");
        // Lot 21 : les sujets et deux colonnes de marques (l'exemple, le tutoriel),
        // leurs icones dessinees par l'IHM (le glyphe d'une tuile, d'un chapitre).
        ui::TableView::Column topicCol;
        topicCol.title = "Sujets";
        topicCol.width = 318.f;
        ui::TableView::Column exampleCol;
        exampleCol.width = 28.f;
        exampleCol.minWidth = 28.f;
        exampleCol.resizable = false;
        exampleCol.sortable = false;
        exampleCol.headerIcon = iconOf(HmiGlyph::Eye);
        ui::TableView::Column tutoCol = exampleCol;
        tutoCol.headerIcon = iconOf(HmiGlyph::Play);
        table->setColumns({topicCol, exampleCol, tutoCol});
        table->setSelectionMode(ui::SelectionMode::Single);
        table->setIconPainter([](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) {
            if (icon > 0) drawHmiGlyph(r, static_cast<HmiGlyph>(icon - 1), box, color);
        });
        table->setTooltip("Les sujets de l'aide. \xC5\x92il : un exemple anim\xC3\xA9 ; triangle : un tutoriel jou\xC3\xA9 par le moteur.");
        body->table = &static_cast<ui::TableView&>(body->addChild(std::move(table)));
        body->legend = &static_cast<HelpLegend&>(body->addChild(std::make_unique<HelpLegend>(base + ".legend")));
        list_ = body->table;
        legend_ = body->legend;
        panel->setBody(std::move(body));
        split->addPane(std::move(panel), 0.26f, 220.f);
    }
    {
        auto right = std::make_unique<HelpRight>(base + ".right");
        auto example = std::make_unique<HmiExampleView>(base + ".example");
        right->example = &static_cast<HmiExampleView&>(right->addChild(std::move(example)));
        right->article = &static_cast<ui::HelpArticleView&>(right->addChild(std::make_unique<ui::HelpArticleView>(base + ".article")));
        right->tutorial = &static_cast<HmiObjectTutorial&>(right->addChild(std::make_unique<HmiObjectTutorial>(base + ".tutorial")));
        right->tutorial->setVisibility(ui::Visibility::Collapsed);
        right->trails = &static_cast<HmiTrailCards&>(right->addChild(std::make_unique<HmiTrailCards>(base + ".trails")));
        right->trails->setVisibility(ui::Visibility::Collapsed);
        trails_ = right->trails;
        right->modeRequested = [this](int mode) { (void)showTutorial(mode == 1); };
        example_ = right->example;
        view_ = right->article;
        tutorial_ = right->tutorial;
        right_ = right.get();
        split->addPane(std::move(right), 0.74f, 320.f);
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));

    links_ += tools_->triggered->connect([this](int a) {
        if (a == TBack) (void)back();
        if (a == TTutorial) {
            // Lot 21 : la page du didacticiel et ses parcours ; sans parcours, la visite.
            if (hosts_.startTrail && guide::topic("didacticiel")) show("didacticiel");
            else if (hosts_.replayTutorial) hosts_.replayTutorial();
        }
        if (a == TReplay && example_) example_->restart();
        if (a == TObjectTutorial) (void)showTutorial(!tutorialShown());
        if (a == TOnlyNew) setOnlyNew(!onlyNew_);
        if (a == TMarkRead) markAllRead();
    });
    links_ += trails_->startRequested->connect([this](const std::string& key, bool resume) {
        if (hosts_.startTrail) hosts_.startTrail(key, resume);
    });
    links_ += search_->textChanged->connect([this](const std::string& text) { setSearch(text); });
    links_ += list_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_ || rows.empty() || rows.front() >= rowKeys_.size()) return;
        std::string key = rowKeys_[rows.front()];
        if (key.empty()) {
            // Un chapitre : son premier sujet.
            for (std::size_t i = rows.front() + 1; i < rowKeys_.size() && key.empty(); ++i) key = rowKeys_[i];
        }
        if (!key.empty() && key != current_) show(key);
    });
    links_ += view_->linkActivated->connect([this](const std::string& target) {
        if (target.rfind("notation:", 0) == 0) {
            // 1.10 : le selecteur ST | C | C++ d'un exemple : le choix vaut pour
            // tous les exemples, et se garde (aide.notation). Le meme sujet,
            // dans l'autre notation : le lecteur reste ou il lisait.
            const std::string n = target.substr(9);
            if ((n == "ST" || n == "C" || n == "C++") && n != codeNotation()) {
                help::news::session().helpNotation = n;
                if (const auto* t = guide::topic(current_)) view_->replaceArticle(compose(*t));
                readChanged->emit();
            }
        } else if (target.rfind("topic:", 0) == 0) show(target.substr(6));
        else if (target.rfind("chapter:", 0) == 0) {
            const auto chapter = target.substr(8);
            for (const auto& t : guide::topics())
                if (t.chapter == chapter) { show(t.key); break; }
        }
    });
    onlyNew_ = help::news::session().helpOnlyNew && newTopicCount() > 0;   // 1.10 : le filtre retenu
    rebuildList();
    show({});
}

std::size_t HmiHelpPane::listedTopics() const noexcept {
    return static_cast<std::size_t>(std::count_if(rowKeys_.begin(), rowKeys_.end(), [](const std::string& k) { return !k.empty(); }));
}

void HmiHelpPane::setSearch(std::string text) {
    filter_ = std::move(text);
    if (search_ && search_->text() != filter_) search_->setText(filter_);
    rebuildList();
    // Le meilleur resultat s'affiche : chercher, c'est vouloir lire.
    if (!filter_.empty()) {
        const auto hits = guide::search(filter_, 1);
        if (!hits.empty() && hits.front().key != current_) show(hits.front().key);
    }
    selectRowOf(current_);
}

void HmiHelpPane::rebuildList() {
    std::vector<TopicRows::Row> rows;
    rowKeys_.clear();
    const auto topicRow = [](const guide::Topic& t, std::string hint) {
        TopicRows::Row r;
        r.text = t.title;
        r.hint = std::move(hint);
        r.glyph = topicGlyph(t);
        r.tone = lookOf(t.chapter).tone;
        r.example = hasExample(t);
        r.written = !t.tutorial.empty();
        r.tutorial = r.example || r.written || t.key == "didacticiel" || t.key == "tutoriel-symbole";
        r.fresh = isNewTopic(t);
        r.changed = r.fresh && !help::news::helpIsNew(help::news::session(), t.since);
        return r;
    };
    // 1.10 : "Nouveautes seulement" - les sujets nouveaux ou changes.
    const bool only = onlyNew_ && newTopicCount() > 0;
    if (filter_.empty()) {
        for (const auto& chapter : guide::chapters()) {
            if (only && std::none_of(guide::topics().begin(), guide::topics().end(),
                                     [&](const guide::Topic& t) { return t.chapter == chapter && isNewTopic(t); }))
                continue;
            TopicRows::Row head;
            head.text = chapter;
            head.chapter = true;
            head.glyph = lookOf(chapter).glyph;
            head.tone = lookOf(chapter).tone;
            rows.push_back(std::move(head));
            rowKeys_.emplace_back();
            for (const auto& t : guide::topics())
                if (t.chapter == chapter && (!only || isNewTopic(t))) {
                    rows.push_back(topicRow(t, {}));
                    rowKeys_.push_back(t.key);
                }
        }
    } else {
        auto hits = guide::search(filter_);
        if (only)
            hits.erase(std::remove_if(hits.begin(), hits.end(), [](const guide::Hit& h) {
                           const auto* t = guide::topic(h.key);
                           return !t || !isNewTopic(*t);
                       }), hits.end());
        TopicRows::Row head;
        head.text = hits.empty() ? std::string("Aucun sujet") : std::to_string(hits.size()) + " sujet(s)";
        head.chapter = true;
        head.glyph = HmiGlyph::Search;
        head.tone = ui::Tone::Accent;
        rows.push_back(std::move(head));
        rowKeys_.emplace_back();
        for (const auto& h : hits)
            if (const auto* t = guide::topic(h.key)) {
                rows.push_back(topicRow(*t, h.excerpt));
                rowKeys_.push_back(t->key);
            }
    }
    model_ = std::make_shared<TopicRows>(std::move(rows));
    syncing_ = true;
    list_->setModel(model_);
    syncing_ = false;
}

void HmiHelpPane::selectRowOf(std::string_view key) {
    syncing_ = true;
    for (std::size_t i = 0; i < rowKeys_.size(); ++i)
        if (!key.empty() && rowKeys_[i] == key) { hmiSelectModelRow(*list_, i); break; }
    syncing_ = false;
}

void HmiHelpPane::show(std::string_view key) {
    const auto* t = guide::topic(key);
    if (!t) t = guide::topics().empty() ? nullptr : &guide::topics().front();
    if (!t) return;
    if (!current_.empty() && current_ != t->key) history_.push_back(current_);
    current_ = t->key;
    view_->setArticle(compose(*t));
    view_->scrollToTop();
    showExampleOf(*t);
    selectRowOf(current_);
    invalidate();
}

void HmiHelpPane::setOnlyNew(bool on) {
    if (onlyNew_ == on) return;
    onlyNew_ = on;
    help::news::session().helpOnlyNew = on;
    rebuildList();
    selectRowOf(current_);
    if (tools_) tools_->invalidate();
    readChanged->emit();
}

void HmiHelpPane::markAllRead() {
    help::news::markHelpRead(help::news::session(), help::news::sessionVersion());
    onlyNew_ = false;
    help::news::session().helpOnlyNew = false;
    refreshNovelty();
    readChanged->emit();
}

void HmiHelpPane::refreshNovelty() {
    rebuildList();
    if (const auto* t = guide::topic(current_)) view_->setArticle(compose(*t));
    selectRowOf(current_);
    if (tools_) tools_->invalidate();
    invalidate();
}

void HmiHelpPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refreshTrails();
}

void HmiHelpPane::refreshTrails() {
    auto* right = static_cast<HelpRight*>(right_);
    if (!right || !trails_) return;
    right->showTrails = current_ == "didacticiel" && static_cast<bool>(hosts_.trails);
    if (right->showTrails) trails_->setCards(hosts_.trails());
    right->invalidateLayout();
    invalidate();
}

bool HmiHelpPane::trailsShown() const noexcept {
    const auto* right = static_cast<const HelpRight*>(right_);
    return right && right->showTrails && trails_ && trails_->visible();
}

void HmiHelpPane::showExampleOf(const guide::Topic& t) {
    if (!example_) return;
    const auto kind = t.kind.empty() || t.kind == "*" ? std::nullopt : hmi::kindFromKey(t.kind);
    example_->setKind(kind);
    example_->setCaption(guide::plain(t.example));
    example_->restart();
    // Lot 16 : le tutoriel suit le sujet s'il est montre ; un sujet qui n'est pas
    // un objet revient a la page.
    if (auto* right = static_cast<HelpRight*>(right_)) {
        if (!example_->hasExample()) right->mode = 0;
        if (right->mode == 1 && tutorial_) tutorial_->setKind(kind);
    }
    if (right_) right_->invalidateLayout();
    refreshTrails();
}

bool HmiHelpPane::showTutorial(bool on) {
    auto* right = static_cast<HelpRight*>(right_);
    if (!right || !example_) return false;
    if (on && !example_->hasExample()) return false;
    right->mode = on ? 1 : 0;
    if (on && tutorial_) tutorial_->setKind(example_->kind());
    right->invalidateLayout();
    invalidate();
    return true;
}

bool HmiHelpPane::tutorialShown() const noexcept {
    const auto* right = static_cast<const HelpRight*>(right_);
    return right && right->mode == 1 && right->tabbed();
}

bool HmiHelpPane::back() {
    if (history_.empty()) return false;
    const std::string key = history_.back();
    history_.pop_back();
    const auto* t = guide::topic(key);
    if (!t) return false;
    current_ = t->key;
    view_->setArticle(compose(*t));
    view_->scrollToTop();
    showExampleOf(*t);
    selectRowOf(current_);
    return true;
}

void HmiHelpPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 38)});
}

void HmiHelpPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
