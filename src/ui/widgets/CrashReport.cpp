// =============================================================================
//  ui/widgets/CrashReport.cpp - 1.12.28 : la fenetre du plantage et du blocage
// =============================================================================
#include "CrashReport.hpp"

#include "../DialogFrame.hpp"
#include "../../menu/MenuManager.hpp"
#include "../Icons.hpp"
#include "../Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ui {

namespace crashreport {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

bool isRule(std::string_view s) {
    s = trim(s);
    return s.size() >= 3 && s.find_first_not_of("-=") == std::string_view::npos;
}

// La premiere lettre en capitale (l'ASCII, et les minuscules accentuees courantes).
std::string capitalized(std::string_view s) {
    std::string out(s);
    if (out.empty()) return out;
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    else if (out.size() >= 2 && static_cast<unsigned char>(out[0]) == 0xC3) {
        const auto c = static_cast<unsigned char>(out[1]);
        if (c >= 0xA0 && c <= 0xBE) out[1] = static_cast<char>(c - 0x20);   // a grave ... thorn
    }
    return out;
}

// « EXCEPTION_ACCESS_VIOLATION (acces memoire invalide), code ... » : le nom technique, en
// capitales, puis l'explication entre parentheses.
bool technicalName(std::string_view s, std::size_t& open, std::size_t& close) {
    open = s.find(" (");
    if (open == std::string_view::npos || open == 0) return false;
    for (std::size_t i = 0; i < open; ++i) {
        const char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    close = s.find(')', open);
    return close != std::string_view::npos;
}

void reasonOf(std::string_view raw, Summary& s) {
    raw = trim(raw);
    if (startsWith(raw, "blocage : ")) {
        s.reason = capitalized(raw.substr(10));
        return;
    }
    std::size_t open = 0, close = 0;
    if (technicalName(raw, open, close)) {
        s.reason = capitalized(raw.substr(open + 2, close - open - 2));
        s.detail = std::string(raw);
        return;
    }
    constexpr std::string_view kCxx = "exception C++ non attrap\xC3\xA9" "e";
    if (startsWith(raw, kCxx)) {
        s.reason = "Une exception C++ non attrap\xC3\xA9" "e";
        const auto at = raw.find(" : ");
        if (at != std::string_view::npos) s.detail = std::string(trim(raw.substr(at + 3)));
        return;
    }
    if (raw == "std::terminate") {
        s.reason = "Un arr\xC3\xAAt imm\xC3\xA9" "diat (std::terminate)";
        return;
    }
    s.reason = capitalized(raw);
}

// Une ligne de l'historique : « 10:57:40.123  #1 principal    menu              Aide > Journal ».
// Celle du fil principal et d'un genre que l'utilisateur reconnait : « 10:57:40  Aide > Journal ».
bool recentOf(std::string_view line, std::string& out) {
    if (line.size() < 16 || line[2] != ':' || line[5] != ':') return false;
    const auto thread = line.find("  #");
    if (thread == std::string_view::npos) return false;
    const auto name = line.find(' ', thread + 3);
    if (name == std::string_view::npos) return false;
    std::string_view rest = trim(line.substr(name));
    constexpr std::string_view kMain = "principal ";
    if (!startsWith(rest, kMain)) return false;
    rest = trim(rest.substr(kMain.size()));
    static constexpr std::string_view kKinds[] = {
        "action", "menu", "fen\xC3\xAAtre", "document", "fichier", "simulation", "script", "compiler"};
    for (const auto kind : kKinds) {
        if (!startsWith(rest, kind) || rest.size() <= kind.size() || rest[kind.size()] != ' ') continue;
        const auto text = trim(rest.substr(kind.size()));
        if (text.empty()) return false;
        out = std::string(line.substr(0, 8)) + "  " + std::string(text);
        return true;
    }
    return false;
}

} // namespace

Summary summarize(std::string_view report) {
    Summary s;
    if (startsWith(report, "\xEF\xBB\xBF")) report.remove_prefix(3);
    std::vector<std::string_view> lines;
    for (std::size_t from = 0; from <= report.size();) {
        const auto nl = report.find('\n', from);
        auto l = report.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.push_back(l);
        if (nl == std::string_view::npos) break;
        from = nl + 1;
    }
    enum class Part { Head, Stack, Scopes, Documents, Trail, Other } part = Part::Head;
    bool mainThread = false;
    std::vector<std::string> recent;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto l = lines[i];
        // Un titre de partie : souligne par la ligne d'apres.
        if (i + 1 < lines.size() && !trim(l).empty() && isRule(lines[i + 1]) && !isRule(l)) {
            if (i == 0) { ++i; continue; }    // le titre du rapport
            if (startsWith(l, "Pile d'appels")) part = Part::Stack;
            else if (startsWith(l, "Port\xC3\xA9" "es ouvertes")) part = Part::Scopes;
            else if (startsWith(l, "Documents ouverts")) part = Part::Documents;
            else if (l.find("historique interne") != std::string_view::npos) part = Part::Trail;
            else part = Part::Other;
            ++i;
            continue;
        }
        switch (part) {
            case Part::Head: {
                const auto at = l.find(" : ");
                if (at == std::string_view::npos) break;
                const auto key = trim(l.substr(0, at));
                const auto value = trim(l.substr(at + 3));
                if (key == "Genre") s.kind = std::string(value);
                else if (key == "Version") s.version = std::string(value);
                else if (key == "Syst\xC3\xA8me") s.system = std::string(value);
                else if (key == "Heure") s.when = std::string(value);
                else if (key == "Raison") reasonOf(value, s);
                break;
            }
            case Part::Scopes: {
                if (startsWith(l, "Fil #")) {
                    const auto colon = l.find(" : ");
                    const auto who = l.substr(0, colon);
                    mainThread = who.size() >= 10 && who.substr(who.size() - 10) == " principal";
                    break;
                }
                const auto t = trim(l);
                if (!mainThread || t.empty() || startsWith(t, "...") || t.front() == '(') break;
                const auto since = t.find("   depuis ");
                if (since == std::string_view::npos) s.doing.emplace_back(t);
                else s.doing.push_back(std::string(trim(t.substr(0, since))) + "  (depuis "
                                       + std::string(trim(t.substr(since + 10))) + ")");
                break;
            }
            case Part::Documents: {
                const auto t = trim(l);
                if (!t.empty() && t.front() != '(') s.documents.emplace_back(t);
                break;
            }
            case Part::Trail: {
                std::string r;
                if (recentOf(l, r)) recent.push_back(std::move(r));
                break;
            }
            default: break;
        }
    }
    // Les plus recentes d'abord, sans repetition, quatre au plus.
    for (auto it = recent.rbegin(); it != recent.rend() && s.recent.size() < 4; ++it) {
        const auto text = std::string_view(*it).substr(std::min<std::size_t>(10, it->size()));
        const bool seen = std::any_of(s.recent.begin(), s.recent.end(), [&](const std::string& o) {
            return std::string_view(o).substr(std::min<std::size_t>(10, o.size())) == text;
        });
        if (!seen) s.recent.push_back(*it);
    }
    return s;
}

double stalledSeconds(std::string_view reason) {
    // « ... depuis 8,0 s »
    const auto at = reason.rfind("depuis ");
    if (at == std::string_view::npos) return 0;
    std::string number(reason.substr(at + 7));
    for (auto& c : number) if (c == ',') c = '.';
    char* end = nullptr;
    const double v = std::strtod(number.c_str(), &end);
    return end != number.c_str() && v > 0 ? v : 0;
}

} // namespace crashreport

// =================================================================== la vue ==
namespace {

constexpr float kPad = 22.f;
constexpr float kHeader = 92.f;
constexpr float kButtonH = 32.f;
constexpr float kFooter = 64.f;
constexpr double kNoteSeconds = 8.0;
constexpr double kCloseAfter = 2.6;     // la reprise dite, puis la fenetre se ferme

std::string seconds(double s) {
    char buf[32];
    if (s < 60) std::snprintf(buf, sizeof buf, "%.0f s", std::floor(s));
    else std::snprintf(buf, sizeof buf, "%d min %02d s", static_cast<int>(s) / 60, static_cast<int>(s) % 60);
    return buf;
}

std::string fileName(std::string_view path) {
    const auto cut = path.find_last_of("/\\");
    return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
}

std::string folderOf(std::string_view path) {
    const auto cut = path.find_last_of("/\\");
    return cut == std::string_view::npos ? std::string{} : std::string(path.substr(0, cut));
}

gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (static_cast<float>(y) - x) * t));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
}

// Le texte replie a `width` (les fins de ligne gardees) : les morceaux, sans espace au bout.
std::vector<std::string_view> wrap(const gfx::IRenderer& r, std::string_view text, gfx::FontId font, float width) {
    std::vector<std::string_view> out;
    for (std::size_t from = 0; from <= text.size();) {
        const auto nl = text.find('\n', from);
        auto para = text.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from);
        if (para.empty()) out.push_back(para);
        while (!para.empty()) {
            std::size_t take = std::max<std::size_t>(1, r.fitCharacters(para, font, width));
            if (take < para.size()) {
                const auto space = para.substr(0, take).find_last_of(' ');
                if (space != std::string_view::npos && space > 0) take = space + 1;
            }
            auto piece = para.substr(0, take);
            while (!piece.empty() && piece.back() == ' ') piece.remove_suffix(1);
            out.push_back(piece);
            para.remove_prefix(std::min(take, para.size()));
        }
        if (nl == std::string_view::npos) break;
        from = nl + 1;
    }
    return out;
}

} // namespace

CrashReportView::CrashReportView(CrashReportSetup setup, bool framed)
    : Widget("rapport.vue"), setup_(std::move(setup)), framed_(framed) {
    using crashreport::Mode;
    summary_ = crashreport::summarize(setup_.text);
    if (setup_.when.empty()) setup_.when = summary_.when;
    if (setup_.mode == Mode::Hang && setup_.stalledSeconds <= 0)
        setup_.stalledSeconds = crashreport::stalledSeconds(summary_.reason);
    const auto tell = [this](const std::function<std::string()>& f) {
        return [this, f] { if (f) say(f()); };
    };
    // A gauche, les outils ; a droite, la decision (la principale en dernier, en couleur).
    if (setup_.openFolder) left_.push_back(add("Ouvrir le dossier", "rapport.dossier", Button::Style::Default, tell(setup_.openFolder)));
    if (setup_.copyReport) left_.push_back(add("Copier le rapport", "rapport.copier", Button::Style::Default, tell(setup_.copyReport)));
    if (setup_.signal) left_.push_back(add("Signaler le probl\xC3\xA8me", "rapport.signaler", Button::Style::Default, tell(setup_.signal)));
    switch (setup_.mode) {
        case Mode::Hang:
            right_.push_back(add("Fermer " + setup_.product, "rapport.fermer-appli", Button::Style::Danger, [this] {
                if (setup_.closeApp) setup_.closeApp();
            }));
            primary_ = add("Attendre", "rapport.attendre", Button::Style::Primary, [this] {
                if (setup_.wait) setup_.wait();
                else if (setup_.close) setup_.close();
            });
            break;
        case Mode::Crash:
            right_.push_back(add("Fermer", "rapport.fermer", Button::Style::Default, [this] { if (setup_.close) setup_.close(); }));
            if (setup_.restart)
                primary_ = add("Relancer " + setup_.product, "rapport.relancer", Button::Style::Primary, [this] { setup_.restart(); });
            break;
        case Mode::Pending:
            primary_ = add("Fermer", "rapport.fermer", Button::Style::Primary, [this] { if (setup_.close) setup_.close(); });
            break;
    }
    if (primary_) right_.push_back(primary_);
}

Button* CrashReportView::add(std::string text, std::string id, Button::Style style, std::function<void()> act) {
    auto& b = static_cast<Button&>(addChild(std::make_unique<Button>(std::move(text), std::move(id))));
    b.setStyle(style);
    links_ += b.clicked->connect(std::move(act));
    buttons_.push_back(&b);
    return &b;
}

Button* CrashReportView::button(std::string_view id) const {
    for (auto* b : buttons_) if (b->id() == id) return b;
    return nullptr;
}

void CrashReportView::say(std::string note) {
    note_ = std::move(note);
    noteAt_ = now_;
    invalidate();
}

void CrashReportView::primary() {
    if (primary_ && primary_->enabled()) primary_->clicked->emit();
}

std::string CrashReportView::headline() const {
    using crashreport::Mode;
    const bool hang = setup_.mode == Mode::Hang;
    if (hang && gone_) return setup_.product + " s'est ferm\xC3\xA9";
    if (hang && recovered_) return setup_.product + " est reparti";
    switch (setup_.mode) {
        case Mode::Hang:  return setup_.product + " ne r\xC3\xA9pond plus";
        case Mode::Crash: return setup_.product + " s'est arr\xC3\xAAt\xC3\xA9";
        case Mode::Pending:
            return summary_.kind == "blocage" ? "Un blocage s'est produit" : "Un plantage s'est produit";
    }
    return {};
}

std::string CrashReportView::subline() const {
    using crashreport::Mode;
    switch (setup_.mode) {
        case Mode::Hang: {
            if (gone_) return "La fen\xC3\xAAtre se ferme.";
            if (recovered_) return "Apr\xC3\xA8s " + seconds(recoveredAfter_) + " d'arr\xC3\xAAt. La fen\xC3\xAAtre se ferme.";
            const double elapsed = start_ < 0 ? 0 : now_ - start_;
            return "Depuis " + seconds(setup_.stalledSeconds + elapsed)
                 + ". Tu peux attendre qu'il reparte, ou le fermer.";
        }
        case Mode::Crash:
            return "Un probl\xC3\xA8me l'a forc\xC3\xA9 \xC3\xA0 se fermer"
                 + (setup_.when.empty() ? std::string(".") : ", le " + setup_.when + ".");
        case Mode::Pending:
            return (setup_.when.empty() ? std::string("Lors de la derni\xC3\xA8re s\xC3\xA9" "ance")
                                        : "Le " + setup_.when)
                 + (summary_.kind == "blocage" ? ", " + setup_.product + " a cess\xC3\xA9 de r\xC3\xA9pondre."
                                               : ", " + setup_.product + " s'est ferm\xC3\xA9 sur un probl\xC3\xA8me.");
    }
    return {};
}

void CrashReportView::tick(double now) {
    now_ = now;
    if (start_ < 0) start_ = now;
    if (noteAt_ >= 0 && now - noteAt_ > kNoteSeconds) {
        note_.clear();
        noteAt_ = -1;
    }
    if (endAt_ >= 0 && now >= endAt_ && !closed_) {
        closed_ = true;
        if (setup_.close) setup_.close();
    }
    invalidate();   // le compteur, le sablier
}

void CrashReportView::setRecovered(double s) {
    if (recovered_ || gone_) return;
    recovered_ = true;
    recoveredAfter_ = s;
    for (auto* b : right_) b->setEnabled(false);
    endAt_ = now_ + kCloseAfter;
    invalidate();
}

void CrashReportView::setGone() {
    if (gone_ || recovered_) return;
    gone_ = true;
    for (auto* b : right_) b->setEnabled(false);
    endAt_ = now_ + kCloseAfter;
    invalidate();
}

void CrashReportView::onLayout() {
    const auto r = bounds();
    if (framed_) {
        const float w = std::floor(std::clamp(r.w - 40.f, 560.f, 760.f));
        const float h = std::floor(std::clamp(r.h - 40.f, 400.f, 540.f));
        panel_ = dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
    } else {
        panel_ = r;
    }
    // La rangee du bas : les boutons a leur taille. S'ils ne tiennent pas sur une ligne, les
    // outils passent au-dessus de la decision.
    const auto width = [](const Button* b) {
        return std::max(104.f, std::ceil(measureWidth(b->text(), gfx::FontId{16})) + 32.f);
    };
    float leftW = 0, rightW = 0;
    for (auto* b : left_) leftW += width(b) + 8.f;
    for (auto* b : right_) rightW += width(b) + 8.f;
    const float avail = panel_.w - 2 * kPad;
    const bool twoRows = leftW + rightW + 24.f > avail;
    const float y = panel_.bottom() - kPad - kButtonH;
    float x = panel_.right() - kPad;
    for (auto it = right_.rbegin(); it != right_.rend(); ++it) {
        const float bw = width(*it);
        x -= bw;
        (*it)->setBounds({x, y, bw, kButtonH});
        x -= 8.f;
    }
    // Les outils : sur la ligne de la decision s'ils y tiennent ; sinon au-dessus, sur autant de
    // lignes qu'il faut (une fenetre etroite).
    if (!twoRows) {
        x = panel_.x + kPad;
        for (auto* b : left_) {
            const float bw = width(b);
            b->setBounds({x, y, bw, kButtonH});
            x += bw + 8.f;
        }
        return;
    }
    std::vector<std::vector<Button*>> rows(1);
    float used = 0;
    for (auto* b : left_) {
        const float bw = width(b);
        if (!rows.back().empty() && used + bw > avail) {
            rows.emplace_back();
            used = 0;
        }
        rows.back().push_back(b);
        used += bw + 8.f;
    }
    float ly = y - static_cast<float>(rows.size()) * (kButtonH + 8.f);
    for (const auto& row : rows) {
        x = panel_.x + kPad;
        for (auto* b : row) {
            const float bw = std::min(width(b), avail);
            b->setBounds({x, ly, bw, kButtonH});
            x += bw + 8.f;
        }
        ly += kButtonH + 8.f;
    }
}

void CrashReportView::onPaint(const PaintContext& ctx) {
    using crashreport::Mode;
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    auto& r = ctx.r;
    const bool hang = setup_.mode == Mode::Hang;
    // La couleur du moment : ambre (bloque), rouge (arrete), bleu (au lancement), vert (reparti).
    gfx::Color tone = setup_.mode == Mode::Crash ? c.error : hang ? c.warning : c.info;
    if (recovered_) tone = c.ok;
    if (gone_) tone = c.textMuted;

    r.fillRect(panel_, c.panelBg);
    if (framed_) r.strokeRect(panel_, c.borderStrong, 1.f);
    // L'en-tete : une bande teintee, la pastille, le titre et ce qui suit.
    const gfx::Rect head{panel_.x, panel_.y, panel_.w, kHeader};
    r.fillRect(head, mix(c.panelBg, tone, 0.14f));
    r.fillRect({head.x, head.bottom() - 2.f, head.w, 2.f}, tone);
    const float disc = 52.f;
    const gfx::Rect circle{head.x + kPad, head.y + (kHeader - disc) * 0.5f, disc, disc};
    r.fillRoundedRect(circle, tone, disc * 0.5f);
    const Icon icon = recovered_ ? Icon::Ok : setup_.mode == Mode::Crash ? Icon::Error : hang ? Icon::History : Icon::Warning;
    drawIcon(r, icon, {circle.x + 14.f, circle.y + 14.f, 24.f, 24.f}, c.textInverted);
    if (hang && !recovered_ && !gone_) {
        // Le sablier qui tourne autour de la pastille : la fenetre attend, l'appli aussi.
        const float cx = circle.x + disc * 0.5f, cy = circle.y + disc * 0.5f, rad = disc * 0.5f + 6.f;
        const double turn = ctx.time * 1.6;
        for (int i = 0; i < 12; ++i) {
            const double a = turn + i * (6.2831853 / 12.0);
            const float alpha = static_cast<float>(i + 1) / 12.f;
            const gfx::Color dot = mix(mix(c.panelBg, tone, 0.14f), tone, alpha);
            const gfx::Point p{cx + static_cast<float>(std::cos(a)) * rad, cy + static_cast<float>(std::sin(a)) * rad};
            r.fillRoundedRect({p.x - 2.5f, p.y - 2.5f, 5.f, 5.f}, dot, 2.5f);
        }
    }
    const float tx = circle.right() + 18.f;
    const float tw = head.right() - kPad - tx;
    const float titleH = r.lineHeight(f.title);
    const float subH = r.lineHeight(f.ui);
    const float top = head.y + (kHeader - titleH - 6.f - subH) * 0.5f;
    r.drawText({tx, top}, headline(), f.title, c.text);
    const auto sub = subline();
    const auto subLines = wrap(r, sub, f.ui, tw);
    if (!subLines.empty()) r.drawText({tx, top + titleH + 6.f}, subLines.front(), f.ui, c.textMuted);

    // Le corps : trois parties, chacune son intertitre.
    float y = head.bottom() + 16.f;
    const float x = panel_.x + kPad, w = panel_.w - 2 * kPad;
    float bottom = panel_.bottom() - kFooter;
    for (auto* b : left_) bottom = std::min(bottom, b->bounds().y - 12.f);
    const float lh = r.lineHeight(f.ui) + 3.f;
    const auto heading = [&](std::string_view text) {
        if (y > bottom) return;
        r.drawText({x, y}, text, f.caption, gone_ ? c.textMuted : mix(c.text, tone, 0.55f));
        y += r.lineHeight(f.caption) + 6.f;
    };
    const auto paragraph = [&](std::string_view text, gfx::FontId font, gfx::Color color, float indent = 0.f) {
        for (const auto piece : wrap(r, text, font, w - indent)) {
            if (y + lh > bottom) return;
            r.drawText({x + indent, y}, piece, font, color);
            y += lh;
        }
    };
    const auto bullets = [&](const std::vector<std::string>& items, std::size_t max, gfx::FontId font) {
        std::size_t shown = 0;
        for (const auto& it : items) {
            if (shown == max || y + lh > bottom) break;
            r.fillRoundedRect({x + 4.f, y + lh * 0.5f - 3.f, 5.f, 5.f}, tone, 2.5f);
            paragraph(it, font, c.text, 18.f);
            ++shown;
        }
        if (items.size() > shown && y + lh <= bottom) {
            char more[64];
            std::snprintf(more, sizeof more, "\xE2\x80\xA6 et %zu de plus (dans le rapport)", items.size() - shown);
            paragraph(more, f.ui, c.textMuted, 18.f);
        }
    };

    heading("CE QUI S'EST PASS\xC3\x89");
    paragraph(summary_.reason.empty() ? std::string("Le rapport ne le dit pas.") : summary_.reason + ".", f.ui, c.text);
    if (!summary_.detail.empty()) paragraph(summary_.detail, f.caption, c.textMuted);
    y += 10.f;

    heading("CE QUE L'APPLICATION FAISAIT");
    if (!summary_.doing.empty()) {
        // La plus fine d'abord : c'est la que la boucle etait.
        std::vector<std::string> deepest(summary_.doing.rbegin(), summary_.doing.rend());
        bullets(deepest, 3, f.mono);
    } else if (!summary_.recent.empty()) {
        paragraph("Ses derni\xC3\xA8res actions :", f.ui, c.textMuted);
        bullets(summary_.recent, 3, f.ui);
    } else {
        paragraph("Rien de suivi n'\xC3\xA9tait en cours.", f.ui, c.textMuted);
    }
    y += 10.f;

    heading("LE RAPPORT");
    if (y + lh * 2 <= bottom) {
        drawIcon(r, Icon::Document, {x, y + 1.f, 16.f, 16.f}, c.textMuted);
        r.drawText({x + 22.f, y}, setup_.path.empty() ? std::string("(pas de rapport)") : fileName(setup_.path), f.uiBold, c.text);
        y += lh;
        const auto folder = folderOf(setup_.path);
        if (!folder.empty()) paragraph(folder, f.caption, c.textMuted, 22.f);
    }
    paragraph("Il contient la version, le syst\xC3\xA8me, la pile d'appels et l'historique interne"
              " (les noms des documents ouverts, jamais leurs donn\xC3\xA9" "es).", f.caption, c.textMuted);

    // La note du dernier geste (« Le rapport est copie »), au-dessus des boutons.
    if (!note_.empty()) {
        const float ny = bottom + 4.f;
        const auto lines = wrap(r, note_, f.ui, w - 24.f);
        const float nh = std::min<float>(static_cast<float>(lines.size()), 2.f) * lh + 8.f;
        r.fillRoundedRect({x, ny, w, nh}, mix(c.panelBg, c.accent, 0.12f), 6.f);
        float ly = ny + 4.f;
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i, ly += lh) r.drawText({x + 12.f, ly}, lines[i], f.ui, c.text);
    }
}

EventResult CrashReportView::onEvent(const InputEvent& e) {
    if (const auto* k = std::get_if<KeyDown>(&e)) {
        if (k->key == Key::Escape && k->mods.none()) {
            // Echap : fermer la fenetre (pendant un blocage : attendre, l'appli continue).
            if (setup_.mode == crashreport::Mode::Hang && setup_.wait && !recovered_ && !gone_) setup_.wait();
            else if (setup_.close) setup_.close();
            return EventResult::Consumed;
        }
        if (k->key == Key::Return && k->mods.none()) {
            primary();
            return EventResult::Consumed;
        }
        if (k->key == Key::C && k->mods.ctrl && !k->mods.alt && setup_.copyReport) {
            say(setup_.copyReport());
            return EventResult::Consumed;
        }
    }
    return EventResult::Ignored;
}

// ================================================================ les hotes ==
CrashReportDialog::CrashReportDialog(CrashReportSetup setup)
    : menu::WidgetMenu("dialog.crashReport"), setup_(std::move(setup)) {}

menu::MenuTraits CrashReportDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    t.closableWithEscape = true;
    return t;
}

std::string CrashReportDialog::title() const { return view_ ? view_->headline() : std::string("Rapport"); }

core::Status CrashReportDialog::buildUi() {
    auto setup = setup_;
    auto userClose = setup.close;
    setup.close = [this, userClose] {
        if (userClose) userClose();
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
    };
    auto view = std::make_unique<CrashReportView>(std::move(setup), true);
    view_ = view.get();
    setRoot(std::move(view));
    return core::ok();
}

void CrashReportDialog::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    if (view_) view_->tick(fc.totalSeconds);
}

CrashReportScreen::CrashReportScreen(CrashReportSetup setup)
    : menu::WidgetMenu("rapport.fenetre"), setup_(std::move(setup)) {}

menu::MenuTraits CrashReportScreen::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Screen;
    t.closableWithEscape = false;    // la vue prend Echap (Attendre, Fermer)
    return t;
}

std::string CrashReportScreen::title() const { return view_ ? view_->headline() : std::string("Rapport"); }

core::Status CrashReportScreen::buildUi() {
    auto view = std::make_unique<CrashReportView>(setup_, false);
    view_ = view.get();
    setRoot(std::move(view));
    return core::ok();
}

void CrashReportScreen::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    if (view_) view_->tick(fc.totalSeconds);
}

} // namespace ui
