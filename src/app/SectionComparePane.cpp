// =============================================================================
//  app/SectionComparePane.cpp - 1.8.0 : voir SectionComparePane.hpp
// =============================================================================
#include "SectionComparePane.hpp"

#include "../project/CodeIconKeys.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "hmi/HmiPanels.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace app {

namespace cmp = project::compare;

namespace {
const gfx::FontId kSmall{13};
constexpr float kLineH = 20.f;
constexpr float kHeadH = 28.f;
constexpr float kRuler = 12.f;

gfx::Color tint(gfx::Color c, std::uint8_t a) { return c.withAlpha(a); }

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}
} // namespace

// ---- le corps : les deux sections ------------------------------------------------
class SectionCompareBody final : public ui::Widget {
public:
    explicit SectionCompareBody(SectionComparePane& pane) : ui::Widget(pane.id() + ".body"), pane_(pane) {}

    struct Visual {
        enum Kind : std::uint8_t { Row, Fold, Minus, Plus } kind{Row};
        std::size_t row{0};        // Row, Minus, Plus : la ligne de result.rows
        std::size_t count{0};      // Fold : combien de lignes identiques cachees
        std::size_t key{0};        // Fold : la premiere cachee (pour la deplier)
    };
    std::vector<Visual>   visual;
    std::set<std::size_t> unfolded;
    float                 scroll{0.f};    // en lignes

    // La ligne visuelle ou commence un bloc de differences.
    [[nodiscard]] std::size_t visualOfBlock(int block) const {
        const auto& r = pane_.result_;
        if (block < 0 || static_cast<std::size_t>(block) >= r.blocks.size()) return 0;
        const auto first = r.blocks[static_cast<std::size_t>(block)].first;
        for (std::size_t i = 0; i < visual.size(); ++i)
            if (visual[i].kind != Visual::Fold && visual[i].row >= first) return i;
        return 0;
    }
    void reveal(int block) {
        const float rows = std::max(1.f, (bounds().h - kHeadH) / kLineH);
        const float at = static_cast<float>(visualOfBlock(block));
        if (at < scroll || at > scroll + rows - 3.f) scroll = std::max(0.f, at - 4.f);
        clampScroll();
        invalidate();
    }
    void clampScroll() {
        const float rows = std::max(1.f, (bounds().h - kHeadH) / kLineH);
        scroll = std::clamp(scroll, 0.f, std::max(0.f, static_cast<float>(visual.size()) - rows + 2.f));
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        const auto& r = pane_.result_;
        const auto& L = pane_.leftLines_;
        const auto& R = pane_.rightLines_;
        if (pane_.left_ == domain::kNoIndex || pane_.right_ == domain::kNoIndex) {
            ctx.r.drawText({b.x + 20.f, b.y + 20.f}, "Choisis deux sections en haut (ou Ctrl+clic sur deux sections, puis Comparer).", ctx.theme.font.ui, c.textMuted);
            return;
        }
        ctx.r.pushClip(b);
        const float textW = b.w - kRuler;
        const float colW = std::floor(textW / 2.f);
        const auto header = [&](gfx::Rect hr, domain::Index s) {
            ctx.r.fillRect(hr, c.headerBg);
            float x = hr.x + 8.f;
            if (const auto p = pane_.hosts_.project ? pane_.hosts_.project() : nullptr) {
                const int ic = project::codeicons::sectionIcon(*p, s);
                ui::drawIcon(ctx.r, ic >= 0 ? ui::codeIcon(ic) : ui::Icon::Section, {x, hr.y + 6.f, 16.f, 16.f}, ic >= 0 ? ui::codeIconColor(ic) : c.textMuted);
                x += 22.f;
            }
            std::string t = pane_.sectionLabel(s);
            while (t.size() > 4 && ctx.r.measure(t, ctx.theme.font.uiBold).width > hr.w - (x - hr.x) - 8.f) t = t.substr(0, t.size() - 4) + "\xE2\x80\xA6";
            ctx.r.drawText({x, hr.y + (hr.h - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, t, ctx.theme.font.uiBold, c.text);
        };
        if (!pane_.unified_) {
            header({b.x, b.y, colW - 1.f, kHeadH}, pane_.left_);
            header({b.x + colW, b.y, textW - colW, kHeadH}, pane_.right_);
        } else {
            header({b.x, b.y, textW, kHeadH}, pane_.left_);
        }
        const std::size_t first = static_cast<std::size_t>(std::max(0.f, scroll));
        float y = b.y + kHeadH + 2.f;
        const auto cur = pane_.current_ >= 0 && static_cast<std::size_t>(pane_.current_) < r.blocks.size() ? r.blocks[static_cast<std::size_t>(pane_.current_)]
                                                                                                          : std::pair<std::size_t, std::size_t>{1, 0};
        std::vector<cmp::Span> ls, rs;
        for (std::size_t i = first; i < visual.size() && y < b.bottom(); ++i, y += kLineH) {
            const auto& v = visual[i];
            if (v.kind == Visual::Fold) {
                const gfx::Rect fr{b.x, y, textW, kLineH};
                ctx.r.fillRect(fr, tint(c.textMuted, 18));
                const std::string t = "\xE2\x8B\xAF  " + std::to_string(v.count) + " lignes identiques : un clic pour les afficher  \xE2\x8B\xAF";
                ctx.r.drawText({b.x + (textW - ctx.r.measure(t, kSmall).width) * 0.5f, y + 3.f}, t, kSmall, c.accent);
                continue;
            }
            const auto& row = r.rows[v.row];
            const bool inCur = v.row >= cur.first && v.row <= cur.second;
            const std::string lt = row.left >= 0 ? L[static_cast<std::size_t>(row.left)] : std::string{};
            const std::string rt = row.right >= 0 ? R[static_cast<std::size_t>(row.right)] : std::string{};
            if (row.kind == cmp::Row::Changed) cmp::inlineDiff(lt, rt, pane_.options_, ls, rs);
            else {
                ls.clear();
                rs.clear();
            }
            const auto line = [&](float x, float w, int number, const std::string& text, const std::vector<cmp::Span>& spans, gfx::Color bg,
                                  gfx::Color strong, const char* sign) {
                if (bg.a) ctx.r.fillRect({x, y, w, kLineH}, bg);
                const std::string num = number >= 0 ? std::to_string(number + 1) : std::string{};
                ctx.r.drawText({x + 38.f - ctx.r.measure(num, kSmall).width, y + 3.f}, num, kSmall, c.textMuted);
                if (sign) ctx.r.drawText({x + 42.f, y + 2.f}, sign, ctx.theme.font.mono, c.textMuted);
                const float tx = x + (sign ? 58.f : 48.f);
                ctx.r.pushClip({x, y, w, kLineH});
                for (const auto& s : spans) {
                    if (s.begin >= text.size()) continue;
                    const float x0 = tx + ctx.r.measure(text.substr(0, s.begin), ctx.theme.font.mono).width;
                    const float x1 = tx + ctx.r.measure(text.substr(0, std::min(s.end, text.size())), ctx.theme.font.mono).width;
                    if (x1 > x0) ctx.r.fillRect({x0, y + 1.f, x1 - x0, kLineH - 2.f}, strong);
                }
                ctx.r.drawText({tx, y + 2.f}, text, ctx.theme.font.mono, c.text);
                ctx.r.popClip();
            };
            const gfx::Color none{0, 0, 0, 0};
            if (!pane_.unified_) {
                gfx::Color lb = none, rb = none;
                if (row.kind == cmp::Row::Removed || row.kind == cmp::Row::Changed) lb = tint(c.error, 46);
                if (row.kind == cmp::Row::Added || row.kind == cmp::Row::Changed) rb = tint(c.ok, 46);
                if (row.left < 0) lb = tint(c.textMuted, 14);
                if (row.right < 0) rb = tint(c.textMuted, 14);
                line(b.x, colW - 1.f, row.left, lt, ls, lb, tint(c.error, 120), nullptr);
                line(b.x + colW, textW - colW, row.right, rt, rs, rb, tint(c.ok, 120), nullptr);
            } else {
                if (v.kind == Visual::Minus) line(b.x, textW, row.left, lt, ls, tint(c.error, 46), tint(c.error, 120), "-");
                else if (v.kind == Visual::Plus) line(b.x, textW, row.right, rt, rs, tint(c.ok, 46), tint(c.ok, 120), "+");
                else line(b.x, textW, row.right, rt, {}, none, none, " ");
            }
            if (inCur) ctx.r.fillRect({b.x, y, 3.f, kLineH}, c.warning);
        }
        if (!pane_.unified_) ctx.r.line({b.x + colW, b.y}, {b.x + colW, b.bottom()}, c.border, 1.f);
        // La regle : ou sont les differences.
        const gfx::Rect ruler{b.x + textW, b.y + kHeadH, kRuler, b.h - kHeadH};
        ctx.r.fillRect(ruler, c.headerBg);
        if (!r.rows.empty())
            for (std::size_t k = 0; k < r.blocks.size(); ++k) {
                const auto [a, z] = r.blocks[k];
                const float y0 = ruler.y + ruler.h * static_cast<float>(a) / static_cast<float>(r.rows.size());
                const float h = std::max(3.f, ruler.h * static_cast<float>(z - a + 1) / static_cast<float>(r.rows.size()));
                const auto kind = r.rows[a].kind;
                const gfx::Color col = static_cast<int>(k) == pane_.current_ ? c.accent : kind == cmp::Row::Added ? c.ok : kind == cmp::Row::Removed ? c.error : c.warning;
                ctx.r.fillRect({ruler.x + 2.f, y0, ruler.w - 4.f, h}, col);
            }
        ctx.r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
            scroll -= w->dy * 3.f;
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
            const auto b = bounds();
            // La regle : un clic y va.
            if (d->pos.x >= b.x + b.w - kRuler && !pane_.result_.rows.empty()) {
                const float f = (d->pos.y - b.y - kHeadH) / std::max(1.f, b.h - kHeadH);
                const auto row = static_cast<std::size_t>(std::clamp(f, 0.f, 0.999f) * static_cast<float>(pane_.result_.rows.size()));
                for (std::size_t i = 0; i < visual.size(); ++i)
                    if (visual[i].kind != Visual::Fold && visual[i].row >= row) {
                        scroll = std::max(0.f, static_cast<float>(i) - 4.f);
                        break;
                    }
                clampScroll();
                invalidate();
                return ui::EventResult::Consumed;
            }
            const float rel = (d->pos.y - b.y - kHeadH - 2.f) / kLineH;
            if (rel < 0.f) return ui::EventResult::Ignored;
            const auto i = static_cast<std::size_t>(std::max(0.f, scroll)) + static_cast<std::size_t>(rel);
            if (i >= visual.size()) return ui::EventResult::Ignored;
            if (visual[i].kind == Visual::Fold) {
                unfolded.insert(visual[i].key);
                pane_.buildVisual();
                invalidate();
                return ui::EventResult::Consumed;
            }
            // Un double-clic : le code de cette section, a cette ligne.
            if (d->clickCount >= 2 && pane_.hosts_.openSection) {
                const auto& row = pane_.result_.rows[visual[i].row];
                const bool rightSide = !pane_.unified_ ? d->pos.x > b.x + (b.w - kRuler) / 2.f : visual[i].kind != Visual::Minus;
                if (rightSide && row.right >= 0) pane_.hosts_.openSection(pane_.right_, row.right);
                else if (row.left >= 0) pane_.hosts_.openSection(pane_.left_, row.left);
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    SectionComparePane& pane_;
};

// ---- le volet ---------------------------------------------------------------------
SectionComparePane::SectionComparePane(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(APrev, HmiGlyph::Up, "Diff\xC3\xA9rence pr\xC3\xA9" "c\xC3\xA9" "dente", "Pr\xC3\xA9" "c\xC3\xA9" "dente");
    tools->add(ANext, HmiGlyph::Down, "Diff\xC3\xA9rence suivante", "Suivante");
    tools->separator();
    tools->add(ASide, HmiGlyph::Compare, "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te : les deux sections en face", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te");
    tools->add(AUnified, HmiGlyph::List, "Unifi\xC3\xA9 : une seule colonne, - \xC3\xA0 gauche, + \xC3\xA0 droite", "Unifi\xC3\xA9");
    tools->separator();
    tools->add(ASwap, HmiGlyph::Refresh, "Inverser : la gauche passe \xC3\xA0 droite", "Inverser");
    tools->add(ACopy, HmiGlyph::Copy, "Copier le rapport : les chiffres et chaque diff\xC3\xA9rence, en texte", "Copier le rapport");
    tools->separator();
    tools->add(AOpenLeft, HmiGlyph::Code, "Ouvrir le code de la section de gauche", "Ouvrir gauche");
    tools->add(AOpenRight, HmiGlyph::Code, "Ouvrir le code de la section de droite", "Ouvrir droite");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setCheckedWhen(ASide, [this] { return !unified_; });
    tools_->setCheckedWhen(AUnified, [this] { return unified_; });
    tools_->setEnabledWhen(APrev, [this] { return !result_.blocks.empty(); });
    tools_->setEnabledWhen(ANext, [this] { return !result_.blocks.empty(); });
    tools_->setEnabledWhen(ASwap, [this] { return left_ != domain::kNoIndex && right_ != domain::kNoIndex; });
    tools_->setEnabledWhen(ACopy, [this] { return left_ != domain::kNoIndex && right_ != domain::kNoIndex; });
    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case APrev: step(-1); break;
            case ANext: step(1); break;
            case ASide: setUnified(false); break;
            case AUnified: setUnified(true); break;
            case ASwap: swapSides(); break;
            case ACopy:
                ui::setClipboardText(reportText());
                if (hosts_.status) hosts_.status("Le rapport de la comparaison est dans le presse-papiers.");
                break;
            case AOpenLeft: if (hosts_.openSection && left_ != domain::kNoIndex) hosts_.openSection(left_, 0); break;
            case AOpenRight: if (hosts_.openSection && right_ != domain::kNoIndex) hosts_.openSection(right_, 0); break;
            default: break;
        }
    });
    boxLeft_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".gauche")));
    boxRight_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".droite")));
    boxLeft_->setMaxVisibleRows(18);
    boxRight_->setMaxVisibleRows(18);
    const auto pick = [this](bool leftSide) {
        return [this, leftSide](int i) {
            if (syncing_ || i < 0 || static_cast<std::size_t>(i) >= choices_.size()) return;
            if (leftSide) setPair(choices_[static_cast<std::size_t>(i)], right_);
            else setPair(left_, choices_[static_cast<std::size_t>(i)]);
        };
    };
    links_ += boxLeft_->selectionChanged->connect(pick(true));
    links_ += boxRight_->selectionChanged->connect(pick(false));
    const auto box = [this, &base](const char* label, const char* key, bool on) {
        auto* b = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>(label, base + "." + key)));
        b->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        links_ += b->stateChanged->connect([this](ui::Checkbox::State) {
            if (!syncing_) recompute();
        });
        return b;
    };
    optNames_ = box("Rapprocher les noms", "noms", true);
    optIndex_ = box("[0] <-> [1]", "indices", false);
    optSpaces_ = box("Ignorer les espaces", "espaces", true);
    optCase_ = box("Ignorer la casse", "casse", false);
    optComments_ = box("Ignorer les commentaires", "commentaires", false);
    optNames_->setTooltip("Un nom qui finit par le suffixe de gauche d'un c\xC3\xB4t\xC3\xA9 et par celui de droite de l'autre compte pour le m\xC3\xAAme "
                          "(Trans_ManuA / Trans_ManuB) : il ne reste que les vraies diff\xC3\xA9rences.");
    optIndex_->setTooltip("armoires[0] et armoires[1] comptent pour le m\xC3\xAAme");
    extra_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".equivalences")));
    extra_->setPlaceholder("Autres \xC3\xA9quivalences : X=Y ; U=V");
    extra_->setTooltip("D'autres noms \xC3\xA0 rapprocher, s\xC3\xA9par\xC3\xA9s par des points-virgules : HCL=ND3 ; _A=_B ; %MW100=%MW200");
    links_ += extra_->textChanged->connect([this](const std::string&) {
        if (!syncing_) recompute();
    });
    body_ = &static_cast<SectionCompareBody&>(addChild(std::make_unique<SectionCompareBody>(*this)));
}

SectionComparePane::~SectionComparePane() = default;

void SectionComparePane::setHosts(Hosts h) { hosts_ = std::move(h); }

std::string SectionComparePane::ownerOf(domain::Index s) const {
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (!p || s >= p->sections.size()) return {};
    const auto& sec = p->sections[s];
    if (sec.owner < p->pous.size() && (p->pous[sec.owner].kind == domain::PouKind::ProgramUnit || p->pous[sec.owner].kind == domain::PouKind::FunctionBlockType))
        return std::string(p->strings.text(p->pous[sec.owner].name));
    const auto task = std::string(p->strings.text(sec.task));
    return task.empty() ? std::string("MAST") : task;
}

std::string SectionComparePane::sectionLabel(domain::Index s) const {
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (!p || s >= p->sections.size()) return {};
    const auto& sec = p->sections[s];
    return std::string(p->strings.text(sec.name)) + "  \xC2\xB7  " + ownerOf(s) + "  \xC2\xB7  " + std::to_string(sec.lineCount) + " lignes";
}

std::string SectionComparePane::title() const {
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (!p || left_ >= p->sections.size() || right_ >= p->sections.size()) return "Comparer des sections";
    return "Comparer " + std::string(p->strings.text(p->sections[left_].name)) + " \xE2\x86\x94 " + std::string(p->strings.text(p->sections[right_].name));
}

void SectionComparePane::setSections(std::vector<domain::Index> chosen) {
    chosen_ = std::move(chosen);
    rebuildChoices();
    pairs_.clear();
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (p && chosen_.size() >= 3) {
        // Qui ressemble a qui : chaque paire, avec l'equivalence de ses noms.
        for (std::size_t i = 0; i < chosen_.size(); ++i)
            for (std::size_t j = i + 1; j < chosen_.size(); ++j) {
                const auto a = chosen_[i], b = chosen_[j];
                if (a >= p->sections.size() || b >= p->sections.size()) continue;
                cmp::Options o;
                if (auto e = cmp::equivalenceFromNames(p->strings.text(p->sections[a].name), p->strings.text(p->sections[b].name), ownerOf(a), ownerOf(b)))
                    o.equivalences.push_back(*e);
                pairs_.push_back({a, b, cmp::similarity(cmp::splitLines(p->sections[a].body), cmp::splitLines(p->sections[b].body), o)});
            }
        std::sort(pairs_.begin(), pairs_.end(), [](const Pair& x, const Pair& y) { return x.similarity > y.similarity; });
    }
    if (!pairs_.empty()) setPair(pairs_.front().a, pairs_.front().b);
    else if (chosen_.size() >= 2) setPair(chosen_[0], chosen_[1]);
    else if (chosen_.size() == 1) setPair(chosen_[0], domain::kNoIndex);
}

void SectionComparePane::rebuildChoices() {
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    choices_.clear();
    std::vector<ui::DropDown::Item> items;
    if (p) {
        for (const auto s : chosen_)
            if (s < p->sections.size()) choices_.push_back(s);
        for (domain::Index s = 0; s < p->sections.size(); ++s)
            if (std::find(choices_.begin(), choices_.end(), s) == choices_.end()) choices_.push_back(s);
        for (const auto s : choices_) items.push_back({std::string(p->strings.text(p->sections[s].name)) + "  (" + ownerOf(s) + ")", std::to_string(s)});
    }
    syncing_ = true;
    boxLeft_->setItems(items);
    boxRight_->setItems(items);
    syncing_ = false;
}

void SectionComparePane::setPair(domain::Index l, domain::Index r) {
    // Les listes deroulantes d'abord (une paire posee sans setSections : l'arbre, un script).
    if (choices_.empty() || std::find(choices_.begin(), choices_.end(), l) == choices_.end()) {
        if (chosen_.empty()) chosen_ = {l, r};
        rebuildChoices();
    }
    left_ = l;
    right_ = r;
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    // L'equivalence deduite des noms change avec la paire.
    namesPair_.clear();
    if (p && l < p->sections.size() && r < p->sections.size())
        if (auto e = cmp::equivalenceFromNames(p->strings.text(p->sections[l].name), p->strings.text(p->sections[r].name), ownerOf(l), ownerOf(r)))
            namesPair_ = e->left + " <-> " + e->right;
    syncing_ = true;
    optNames_->setLabel(namesPair_.empty() ? std::string("Rapprocher les noms (rien \xC3\xA0 d\xC3\xA9" "duire)") : "Rapprocher " + namesPair_ + " (des noms)");
    optNames_->setEnabled(!namesPair_.empty());
    const auto indexOf = [this](domain::Index s) {
        const auto it = std::find(choices_.begin(), choices_.end(), s);
        return it == choices_.end() ? -1 : static_cast<int>(it - choices_.begin());
    };
    boxLeft_->setSelectedIndex(indexOf(l));
    boxRight_->setSelectedIndex(indexOf(r));
    syncing_ = false;
    body_->unfolded.clear();
    body_->scroll = 0.f;
    current_ = 0;
    recompute();
    if (hosts_.retitle) hosts_.retitle(title());
    invalidateLayout();
}

void SectionComparePane::readOptions() {
    options_ = {};
    options_.ignoreSpaces = optSpaces_->isChecked();
    options_.ignoreCase = optCase_->isChecked();
    options_.ignoreComments = optComments_->isChecked();
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (optNames_->isChecked() && p && left_ < p->sections.size() && right_ < p->sections.size())
        if (auto e = cmp::equivalenceFromNames(p->strings.text(p->sections[left_].name), p->strings.text(p->sections[right_].name), ownerOf(left_), ownerOf(right_)))
            options_.equivalences.push_back(*e);
    if (optIndex_->isChecked()) options_.equivalences.push_back({"[0]", "[1]", true, false});
    // X=Y ; U=V
    std::string text = extra_->text();
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto semi = text.find(';', from);
        const auto part = trim(text.substr(from, (semi == std::string::npos ? text.size() : semi) - from));
        if (const auto eq = part.find('='); eq != std::string::npos) {
            const auto a = trim(part.substr(0, eq)), b = trim(part.substr(eq + 1));
            if (!a.empty() && !b.empty()) options_.equivalences.push_back({a, b, true, false});
        }
        if (semi == std::string::npos) break;
        from = semi + 1;
    }
}

void SectionComparePane::recompute() {
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    leftLines_.clear();
    rightLines_.clear();
    readOptions();
    if (p && left_ < p->sections.size()) leftLines_ = cmp::splitLines(p->sections[left_].body);
    if (p && right_ < p->sections.size()) rightLines_ = cmp::splitLines(p->sections[right_].body);
    // Les tabulations comme dans l'editeur (4) : l'alignement se lit.
    for (auto* v : {&leftLines_, &rightLines_})
        for (auto& l : *v) {
            std::string o;
            for (const char ch : l) {
                if (ch == '\t') o.append(4 - (o.size() % 4), ' ');
                else o += ch;
            }
            l = std::move(o);
        }
    result_ = (left_ != domain::kNoIndex && right_ != domain::kNoIndex) ? cmp::compare(leftLines_, rightLines_, options_) : cmp::Result{};
    if (current_ >= static_cast<int>(result_.blocks.size())) current_ = 0;
    buildVisual();
    invalidate();
}

void SectionComparePane::buildVisual() {
    auto& vis = body_->visual;
    vis.clear();
    const auto& rows = result_.rows;
    std::size_t i = 0;
    while (i < rows.size()) {
        if (rows[i].kind != cmp::Row::Same) {
            if (unified_ && rows[i].kind == cmp::Row::Changed) {
                vis.push_back({SectionCompareBody::Visual::Minus, i, 0, 0});
                vis.push_back({SectionCompareBody::Visual::Plus, i, 0, 0});
            } else if (unified_) {
                vis.push_back({rows[i].kind == cmp::Row::Added ? SectionCompareBody::Visual::Plus : SectionCompareBody::Visual::Minus, i, 0, 0});
            } else {
                vis.push_back({SectionCompareBody::Visual::Row, i, 0, 0});
            }
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < rows.size() && rows[j].kind == cmp::Row::Same) ++j;
        // Trois lignes de contexte autour des differences ; le reste replie.
        const std::size_t before = i == 0 ? 0 : 3, after = j == rows.size() ? 0 : 3;
        if (j - i > before + after + 4 && !body_->unfolded.count(i + before)) {
            for (std::size_t k = i; k < i + before; ++k) vis.push_back({SectionCompareBody::Visual::Row, k, 0, 0});
            vis.push_back({SectionCompareBody::Visual::Fold, 0, j - i - before - after, i + before});
            for (std::size_t k = j - after; k < j; ++k) vis.push_back({SectionCompareBody::Visual::Row, k, 0, 0});
        } else {
            for (std::size_t k = i; k < j; ++k) vis.push_back({SectionCompareBody::Visual::Row, k, 0, 0});
        }
        i = j;
    }
    body_->clampScroll();
}

void SectionComparePane::step(int delta) {
    if (result_.blocks.empty()) return;
    const int n = static_cast<int>(result_.blocks.size());
    current_ = ((current_ + delta) % n + n) % n;
    // Un bloc dans une partie repliee ne se cache jamais (les replis evitent les differences).
    body_->reveal(current_);
    invalidate();
}

void SectionComparePane::setUnified(bool on) {
    if (unified_ == on) return;
    unified_ = on;
    buildVisual();
    body_->reveal(current_);
    invalidate();
}

void SectionComparePane::swapSides() { setPair(right_, left_); }

std::string SectionComparePane::reportText() const {
    return cmp::report(sectionLabel(left_), sectionLabel(right_), leftLines_, rightLines_, options_, result_);
}

void SectionComparePane::setOption(const std::string& name, bool on) {
    ui::Checkbox* b = name == "noms" ? optNames_ : name == "indices" ? optIndex_ : name == "espaces" ? optSpaces_ : name == "casse" ? optCase_
                    : name == "commentaires" ? optComments_ : nullptr;
    if (b) b->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
}

void SectionComparePane::setExtraEquivalences(const std::string& text) { extra_->setText(text); }

void SectionComparePane::onLayout() {
    const auto b = bounds();
    const float th = tools_->sizeHint().preferred.h;
    tools_->setBounds({b.x, b.y, b.w, th});
    float y = b.y + th + 6.f;
    const float bw = std::min(340.f, (b.w - 60.f) / 2.f);
    boxLeft_->setBounds({b.x + 70.f, y, bw, 28.f});
    boxRight_->setBounds({b.x + 70.f + bw + 70.f, y, bw, 28.f});
    y += 34.f;
    float x = b.x + 10.f;
    for (auto* cbx : {optNames_, optIndex_, optSpaces_, optCase_, optComments_}) {
        const float w = ui::measureWidth(cbx->label(), gfx::FontId{16}) + 36.f;
        if (x + w > b.x + b.w - 10.f) {
            x = b.x + 10.f;
            y += 30.f;
        }
        cbx->setBounds({x, y, w, 26.f});
        x += w + 12.f;
    }
    const float ew = std::max(200.f, std::min(360.f, b.x + b.w - 10.f - x));
    if (x + ew > b.x + b.w - 10.f) {
        x = b.x + 10.f;
        y += 30.f;
    }
    extra_->setBounds({x, y - 1.f, ew, 28.f});
    y += 34.f;
    summary_ = {b.x, y, b.w, pairs_.empty() ? 30.f : 58.f};
    y += summary_.h;
    body_->setBounds({b.x, y, b.w, std::max(40.f, b.bottom() - y)});
    body_->clampScroll();
}

void SectionComparePane::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    ctx.r.fillRect(b, c.panelBg);
    // Les libelles des listes.
    const float ly = boxLeft_->bounds().y + 5.f;
    ctx.r.drawText({b.x + 10.f, ly}, "Gauche", kSmall, c.textMuted);
    ctx.r.drawText({boxLeft_->bounds().right() + 14.f, ly}, "Droite", kSmall, c.textMuted);
    // Les chiffres.
    const auto& s = summary_;
    ctx.r.fillRect(s, c.headerBg);
    float x = s.x + 12.f;
    const float y = s.y + 6.f;
    if (left_ != domain::kNoIndex && right_ != domain::kNoIndex) {
        const std::string pct = std::to_string(result_.similarity) + " % semblables";
        ctx.r.drawText({x, y}, pct, ctx.theme.font.uiBold, c.text);
        x += ctx.r.measure(pct, ctx.theme.font.uiBold).width + 10.f;
        const gfx::Rect bar{x, y + 6.f, 110.f, 7.f};
        ctx.r.fillRoundedRect(bar, tint(c.error, 110), 3.f);
        ctx.r.fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(result_.similarity) / 100.f, bar.h}, c.ok, 3.f);
        x += bar.w + 16.f;
        const auto part = [&](const std::string& t, gfx::Color col) {
            ctx.r.drawText({x, y + 1.f}, t, kSmall, col);
            x += ctx.r.measure(t, kSmall).width + 16.f;
        };
        part(std::to_string(result_.leftLines) + " \xE2\x86\x94 " + std::to_string(result_.rightLines) + " lignes", c.text);
        part(std::to_string(result_.same) + " identiques", c.text);
        part(std::to_string(result_.changed) + (result_.changed > 1 ? " modifi\xC3\xA9" "es" : " modifi\xC3\xA9" "e"), c.warning);
        part(std::to_string(result_.added) + (result_.added > 1 ? " ajout\xC3\xA9" "es" : " ajout\xC3\xA9" "e"), c.ok);
        part(std::to_string(result_.removed) + (result_.removed > 1 ? " retir\xC3\xA9" "es" : " retir\xC3\xA9" "e"), c.error);
        const auto n = result_.blocks.size();
        part(n == 0 ? std::string("aucune diff\xC3\xA9rence") : std::to_string(n) + (n > 1 ? " diff\xC3\xA9rences" : " diff\xC3\xA9rence") + " \xC2\xB7 " + std::to_string(current_ + 1) + " / " + std::to_string(n),
             c.textMuted);
    }
    if (!pairs_.empty()) {
        float px = s.x + 12.f;
        const float py = s.y + 32.f;
        ctx.r.drawText({px, py + 2.f}, "Qui ressemble \xC3\xA0 qui :", kSmall, c.textMuted);
        px += ctx.r.measure("Qui ressemble \xC3\xA0 qui :", kSmall).width + 10.f;
        const auto p = hosts_.project ? hosts_.project() : nullptr;
        for (const auto& pr : pairs_) {
            if (!p) break;
            const std::string t = std::string(p->strings.text(p->sections[pr.a].name)) + " \xE2\x86\x94 " + std::string(p->strings.text(p->sections[pr.b].name)) + "  "
                                + std::to_string(pr.similarity) + " %";
            const float w = ctx.r.measure(t, kSmall).width + 16.f;
            if (px + w > s.x + s.w - 10.f) break;
            pr.rect = {px, py - 1.f, w, 20.f};
            const bool on = (pr.a == left_ && pr.b == right_) || (pr.a == right_ && pr.b == left_);
            ctx.r.fillRoundedRect(pr.rect, on ? c.selectionBg : c.panelBg, 9.f);
            ctx.r.strokeRect(pr.rect, on ? c.accent : c.border, 1.f);
            ctx.r.drawText({px + 8.f, py + 2.f}, t, kSmall, c.text);
            px += w + 6.f;
        }
    }
}

ui::EventResult SectionComparePane::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && summary_.contains(d->pos))
        for (const auto& pr : pairs_)
            if (pr.rect.contains(d->pos)) {
                setPair(pr.a, pr.b);
                return ui::EventResult::Consumed;
            }
    return ui::EventResult::Ignored;
}

} // namespace app
