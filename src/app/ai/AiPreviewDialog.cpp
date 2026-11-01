// =============================================================================
//  app/ai/AiPreviewDialog.cpp - voir AiPreviewDialog.hpp
// =============================================================================
#include "AiPreviewDialog.hpp"

#include "AiPanel.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../../core/Perf.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace app::ai {

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kText{14};
const gfx::FontId kSmall{13};
constexpr float kMaxW = 1240.f, kMaxH = 800.f;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

// Un texte coupe aux mots pour tenir dans `width` (les avertissements et les erreurs se lisent en entier).
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto push = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') push();
        else word += c;
    }
    push();
    if (!line.empty()) out.push_back(line);
    if (out.empty()) out.emplace_back();
    return out;
}

// 1.16.1 : une fleche (un chevron) : 0 vers la droite, 1 vers le bas, 2 vers la gauche.
void chevron(gfx::IRenderer& r, gfx::Rect b, int dir, gfx::Color col) {
    const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.3f;
    if (dir == 0) {
        r.line({cx - s / 2.f, cy - s}, {cx + s / 2.f, cy}, col, 1.6f);
        r.line({cx + s / 2.f, cy}, {cx - s / 2.f, cy + s}, col, 1.6f);
    } else if (dir == 1) {
        r.line({cx - s, cy - s / 2.f}, {cx, cy + s / 2.f}, col, 1.6f);
        r.line({cx, cy + s / 2.f}, {cx + s, cy - s / 2.f}, col, 1.6f);
    } else {
        r.line({cx + s / 2.f, cy - s}, {cx - s / 2.f, cy}, col, 1.6f);
        r.line({cx - s / 2.f, cy}, {cx + s / 2.f, cy + s}, col, 1.6f);
    }
}

// 1.16.1 : la cle d'un groupe de la liste - son titre sans son compte ("OBJETS CONCERNES (3)" -> "OBJETS CONCERNES").
std::string groupKey(const std::string& heading) {
    std::size_t cut = heading.find(" (");
    const std::size_t dash = heading.find(" \xE2\x80\x94");
    if (dash != std::string::npos && (cut == std::string::npos || dash < cut)) cut = dash;
    return cut == std::string::npos ? heading : heading.substr(0, cut);
}

// Ce qui, change, refait la liste : l'etat, le choix de la variable ; 1.21.0 : les etapes retirees (une suppression
// retiree ici ou sur la bande du plan).
std::string proposalKey(const Proposal& p) {
    std::string key = p.id + "|" + p.state + "|" + p.variable.use + (p.variable.create ? "+" : "-");
    for (const auto x : p.excluded) key += "x" + std::to_string(x);
    return key + fixes::key(p);   // 1.22.1 : les corrections (un choix par variable introuvable)
}

// Une ligne de la liste de gauche.
struct Row {
    enum class Kind : std::uint8_t { Heading, Item, Change, Note, Warning, Error } kind{Kind::Note};
    std::string text, right;
    int         what{0};      // un objet : 0 creer, 1 modifier, 2 supprimer
    int         op{-1};       // 1.21.0 : une suppression (l'indice de son operation) - un clic la retire ou la remet
    bool        excluded{false};
    // 1.22.1 : une ligne qui se corrige au clic droit - 1 : une erreur, 2 : un avertissement, 3 : une correction faite ;
    // `line` : son rang (AiPreviewDialog::errorLines_, warningLines_, corrections_).
    int         list{0};
    std::size_t line{0};
    // 1.22.4 : CE QUE LA LIGNE DESIGNE DANS L'APERCU (refresh le resout une fois par proposition) - un objet (son nom) ;
    // ce qui lit des variables et des types (leurs noms) ; les objets d'operations (une etape : sinon ce qui lit les
    // variables et les types qu'elle cree) ; ceux d'un message (sinon ceux de ses operations). Rien : ce que designe la ligne
    // qui la porte (un objet, une etape). `issue` : 1 un avertissement, 2 une erreur ; `target` : ses objets
    // (AiPreviewBody::targets ; -1 : aucun).
    enum class Ref : std::uint8_t { None, Object, Uses, Ops, Message } ref{Ref::None};
    std::vector<std::string> names{}, types{};
    std::vector<std::size_t> ops{};
    int         issue{0};
    int         target{-1};
};

// ---- 1.22.4 : OU SONT LES ERREURS ------------------------------------------------------------------------------------------
gfx::Color issueColor(const ui::Theme& th, int level) { return level >= 2 ? th.color.error : th.color.warning; }

// La pastille (!) d'une erreur ou d'un avertissement : un disque de sa couleur, un point d'exclamation.
void pastille(gfx::IRenderer& r, gfx::Point center, float size, gfx::Color col, gfx::Color mark) {
    r.fillRoundedRect({center.x - size / 2.f, center.y - size / 2.f, size, size}, col, size / 2.f);
    const float tw = r.measure("!", kSmall).width;
    r.drawText({center.x - tw / 2.f, center.y - r.lineHeight(kSmall) / 2.f}, "!", kSmall, mark);
}

std::string trimCopy(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}
bool identStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool identChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
std::string upperAscii(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}
// La racine d'un chemin (Armoires[0].actif -> ARMOIRES), en majuscules : les noms de variables ne distinguent pas la casse.
std::string rootKey(std::string_view path) {
    std::size_t b = 0;
    while (b < path.size() && std::isspace(static_cast<unsigned char>(path[b]))) ++b;
    std::size_t e = b;
    while (e < path.size() && identChar(static_cast<unsigned char>(path[e]))) ++e;
    return upperAscii(path.substr(b, e - b));
}

// Les noms qu'un texte lit (une formule, du ST, un trou) : chaque identifiant qui commence un chemin - pas un membre (.Nom),
// ni un nombre, une chaine ou un commentaire -, en majuscules.
void rootsOf(std::string_view t, std::unordered_set<std::string>& out) {
    std::size_t i = 0;
    char prev = ' ';
    while (i < t.size()) {
        const auto c = static_cast<unsigned char>(t[i]);
        if (c == '\'' || c == '"') {
            const auto close = t.find(static_cast<char>(c), i + 1);
            i = close == std::string_view::npos ? t.size() : close + 1;
            prev = 'x';
            continue;
        }
        if (c == '(' && i + 1 < t.size() && t[i + 1] == '*') {
            const auto close = t.find("*)", i + 2);
            i = close == std::string_view::npos ? t.size() : close + 2;
            continue;
        }
        if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') {
            const auto close = t.find('\n', i);
            i = close == std::string_view::npos ? t.size() : close;
            continue;
        }
        if (std::isdigit(c)) {   // un nombre (16#FF, 1.5, 2s) : rien a lire
            while (i < t.size() && (identChar(static_cast<unsigned char>(t[i])) || t[i] == '#'
                                    || (t[i] == '.' && i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))))
                ++i;
            prev = 'x';
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < t.size() && identChar(static_cast<unsigned char>(t[j]))) ++j;
            if (prev != '.' && prev != '#') out.insert(upperAscii(t.substr(i, j - i)));
            prev = 'x';
            i = j;
            continue;
        }
        if (!std::isspace(c)) prev = static_cast<char>(c);
        ++i;
    }
}

// Les mots d'un message (tout identifiant, membres compris), tels qu'ecrits.
std::vector<std::string> wordsOf(std::string_view t) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < t.size()) {
        if (!identStart(static_cast<unsigned char>(t[i]))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < t.size() && identChar(static_cast<unsigned char>(t[j]))) ++j;
        out.emplace_back(t.substr(i, j - i));
        i = j;
    }
    return out;
}

void addUnique(std::vector<hmi::Id>& to, hmi::Id id) {
    if (id != hmi::kNoId && std::find(to.begin(), to.end(), id) == to.end()) to.push_back(id);
}

// OU EST CE QUE DESIGNE UNE LIGNE, dans l'apercu - construit une fois par proposition (refresh) : les objets de la vue
// montree (par nom, par id), les noms que chacun lit (sa variable, ses formules, ses textes a trous, ses actions), le type
// de chaque variable (du projet, creee par la proposition, parametre de la vue), les objets de chaque operation et de
// chaque message (Check::spots).
class Locator {
public:
    Locator(const Proposal& p, const Check& ck, const Preview& pv, const hmi::Project* project) : p_(p), ck_(ck), pv_(pv) {
        if (!pv.ok) return;
        const auto& objects = pv.view.objects;
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const auto& o = objects[i];
            index_.emplace(o.id, i);
            byName_[o.name].push_back(o.id);
            // Ce que l'objet lit : chaque texte ou un nom se lit (hmi::rewriteNames, sur une copie : rien ne change).
            hmi::Object copy = o;
            std::unordered_set<std::string> roots;
            hmi::rewriteNames(copy, [&](std::string_view text, bool) {
                rootsOf(text, roots);
                return std::string(text);
            });
            for (const auto& r : roots) readers_[r].push_back(o.id);
        }
        const auto typed = [&](const std::string& name, const std::string& type) {
            known_.insert(rootKey(name));
            for (const auto& w : wordsOf(type)) ofType_[upperAscii(w)].push_back(rootKey(name));
        };
        if (project)
            for (const auto& v : project->programs.variables) typed(v.name, v.type);
        for (const auto& v : ck.newVariables) typed(v.name, v.type);
        for (const auto& prm : pv.view.params) typed(prm.name, prm.type);
        for (std::size_t k = 0; k < ck.spots.size(); ++k) {
            const auto& s = ck.spots[k];
            if (s.message.empty()) byOp_.emplace(s.op, k);
            else byMessage_.emplace(s.message, k);
        }
    }

    [[nodiscard]] const std::unordered_map<hmi::Id, std::size_t>& index() const noexcept { return index_; }

    [[nodiscard]] hmi::Id resolve(const Check::Spot& s) const {
        if (!s.view.empty() && s.view != pv_.view.name) return hmi::kNoId;
        if (const auto it = index_.find(s.id); it != index_.end() && pv_.view.objects[it->second].name == s.object) return s.id;
        return named(s.object);
    }
    [[nodiscard]] hmi::Id named(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() || it->second.empty() ? hmi::kNoId : it->second.front();
    }
    // Les objets qui lisent ce nom (une variable : Armoire1, Armoire1.Porte, Armoires[0].actif).
    void usingName(const std::string& name, std::vector<hmi::Id>& out) const {
        if (const auto it = readers_.find(rootKey(name)); it != readers_.end())
            for (const auto id : it->second) addUnique(out, id);
    }
    // Les objets qui lisent une variable de ce type (ou un tableau de ce type), ou le type lui-meme.
    void usingType(const std::string& type, std::vector<hmi::Id>& out) const {
        const std::string key = upperAscii(type);
        if (const auto it = ofType_.find(key); it != ofType_.end())
            for (const auto& v : it->second) usingName(v, out);
        usingName(type, out);
    }
    // Les objets qu'une operation cree, modifie ou supprime ; sinon (ignoree, retiree) ceux qu'elle nomme.
    void ofOp(std::size_t op, std::vector<hmi::Id>& out) const {
        const std::size_t before = out.size();
        const auto [b, e] = byOp_.equal_range(op);
        for (auto it = b; it != e; ++it) addUnique(out, resolve(ck_.spots[it->second]));
        if (out.size() > before || op >= p_.ops.size() || projectLevel(p_.ops[op])) return;
        addUnique(out, named(p_.ops[op].name));
        for (const auto& n : p_.ops[op].objects) addUnique(out, named(n));
    }
    // Les objets d'un message : ceux que la validation a notes ; sinon ceux qu'il nomme ; une erreur : sinon ceux qui lisent
    // une variable qu'elle nomme (du projet, de la proposition, un parametre de la vue - pas un avertissement : << sera creee
    // dans le dossier... >> n'est pas un defaut des objets qui la lisent) ; sinon ceux de ses operations.
    void ofMessage(const std::string& message, const std::vector<std::size_t>& ops, bool error, std::vector<hmi::Id>& out) const {
        const std::size_t before = out.size();
        const auto [b, e] = byMessage_.equal_range(message);
        for (auto it = b; it != e; ++it) addUnique(out, resolve(ck_.spots[it->second]));
        if (out.size() > before) return;
        const auto words = wordsOf(message);
        for (const auto& w : words) addUnique(out, named(w));
        if (out.size() > before) return;
        if (error)
            for (const auto& w : words)
                if (known_.count(upperAscii(w))) usingName(w, out);
        if (out.size() > before) return;
        for (const auto op : ops) ofOp(op, out);
    }

private:
    const Proposal&                                           p_;
    const Check&                                              ck_;
    const Preview&                                            pv_;
    std::unordered_map<hmi::Id, std::size_t>                  index_;
    std::unordered_map<std::string, std::vector<hmi::Id>>     byName_;
    std::unordered_map<std::string, std::vector<hmi::Id>>     readers_;
    std::unordered_map<std::string, std::vector<std::string>> ofType_;
    std::unordered_set<std::string>                           known_;   // les variables (en majuscules)
    std::multimap<std::size_t, std::size_t>                   byOp_;
    std::multimap<std::string, std::size_t>                   byMessage_;
};

} // namespace

// Le corps : le cadre, la liste a gauche, l'apercu a droite, les boutons.
class AiPreviewBody final : public ui::Widget {
public:
    explicit AiPreviewBody(AiPreviewDialog& d) : ui::Widget("ai.apercu.corps"), d_(d) {}
    std::vector<Row> rows;
    float            scroll{0.f};
    // 1.16.1 : LE ZOOM DE L'APERCU (1 : la vue entiere, jusqu'a 10), le decalage (glisser), la liste de gauche
    // (ses groupes replies, elle-meme masquee).
    float                 zoom{1.f};
    gfx::Point            pan{0.f, 0.f};
    std::set<std::string> folded;
    bool                  listHidden{false};
    // 1.22.4 : les objets de chaque ligne (Row::target) ; le filtre (la recherche, les puces) et ce qu'il garde ; la ligne
    // choisie et ce qu'elle encadre (le cadre pulse une seconde a partir de son premier dessin) ; la ligne a faire voir.
    std::vector<std::vector<hmi::Id>> targets;
    bool                              onlyErrors{false}, onlyWarnings{false};
    bool                              filtering{false};
    std::vector<char>                 pass;
    std::size_t                       matches{0};
    int                               selected{-1};
    std::vector<hmi::Id>              highlight;
    double                            pulseStart{-1.0};
    int                               scrollToRow{-1};

    [[nodiscard]] bool hidden(std::size_t i) const {   // une ligne d'un groupe replie
        if (filtering) return i >= pass.size() || !pass[i];   // 1.22.4 : le filtre montre ce qu'il trouve, groupes replies ou non
        for (std::size_t k = i + 1; k-- > 0;)
            if (rows[k].kind == Row::Kind::Heading) return k != i && folded.count(groupKey(rows[k].text)) > 0;
        return false;
    }
    // 1.22.4 : le titre du groupe d'une ligne (-1 : aucun).
    [[nodiscard]] int headingOf(std::size_t i) const {
        for (std::size_t k = i + 1; k-- > 0;)
            if (k < rows.size() && rows[k].kind == Row::Kind::Heading) return static_cast<int>(k);
        return -1;
    }

    // 1.22.4 : LE FILTRE - la recherche (le champ de l'appli : les mots ET, sans casse ni accents) et les puces (les erreurs,
    // les avertissements, ou les deux). Une ligne repond : elle se montre, son titre aussi ; le detail d'une ligne qui repond
    // (son etat, ses changements) se montre avec elle. Rien de tape, aucune puce : la liste comme avant (les groupes replies).
    void applyFilter() {
        const ui::SearchQuery* q = d_.search_ ? &d_.search_->query() : nullptr;
        const bool text = q && !q->empty();
        filtering = text || onlyErrors || onlyWarnings;
        pass.assign(rows.size(), 0);
        matches = 0;
        if (filtering) {
            std::size_t head = rows.size(), item = rows.size();
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const Row& r = rows[i];
                if (r.kind == Row::Kind::Heading) {
                    head = i;
                    item = rows.size();
                    continue;
                }
                if (r.kind == Row::Kind::Item) item = i;
                const bool chip = (!onlyErrors && !onlyWarnings) || (onlyErrors && r.issue == 2) || (onlyWarnings && r.issue == 1);
                if (chip && (!text || q->matches({std::string_view(r.text), std::string_view(r.right)}))) {
                    pass[i] = 1;
                    ++matches;
                    if (head < rows.size()) pass[head] = 1;
                } else if (r.kind == Row::Kind::Change && item < rows.size() && pass[item]) {
                    pass[i] = 1;
                }
            }
        }
        invalidate();
    }

    // 1.22.4 : CHOISIR UNE LIGNE - ce qu'elle designe s'encadre (le cadre pulse) ; depuis la liste, l'apercu s'y recentre
    // (et zoome si besoin) ; depuis l'apercu, la liste la fait voir (son groupe se deplie).
    void select(int k, bool fromList) {
        selected = k >= 0 && static_cast<std::size_t>(k) < rows.size() ? k : -1;
        highlight.clear();
        if (selected >= 0) {
            const Row& r = rows[static_cast<std::size_t>(selected)];
            if (r.target >= 0 && static_cast<std::size_t>(r.target) < targets.size()) highlight = targets[static_cast<std::size_t>(r.target)];
        }
        pulseStart = -1.0;
        if (fromList) reveal(highlight);
        else show(selected);
        invalidate();
    }
    void show(int k) {   // la ligne k visible : son groupe deplie, la liste deroulee jusqu'a elle (au prochain dessin)
        if (k < 0) return;
        if (const int h = headingOf(static_cast<std::size_t>(k)); h >= 0 && h != k) folded.erase(groupKey(rows[static_cast<std::size_t>(h)].text));
        scrollToRow = k;
    }
    // Un clic sur un objet de l'apercu : sa ligne (celle de l'objet lui-meme d'abord - OBJETS CONCERNES -, puis une etape,
    // puis sa variable associee, puis toute ligne qui le designe) parmi celles que le filtre montre ; aucune : l'objet seul
    // s'encadre.
    void selectObject(hmi::Id id, const std::string& name) {
        int best = -1, rank = 5;
        for (std::size_t i = 0; i < rows.size() && rank > 1; ++i) {
            const Row& r = rows[i];
            if (r.kind == Row::Kind::Heading || r.target < 0 || (filtering && !pass[i])) continue;
            const auto& t = targets[static_cast<std::size_t>(r.target)];
            if (std::find(t.begin(), t.end(), id) == t.end()) continue;
            const bool itself = r.ref == Row::Ref::Object && !r.names.empty() && r.names.front() == name;
            const bool item = r.kind == Row::Kind::Item;
            const int  here = itself && item ? 1 : item ? 2 : itself ? 3 : 4;
            if (here < rank) {
                rank = here;
                best = static_cast<int>(i);
            }
        }
        select(best, false);
        highlight = {id};
    }
    // L'apercu montre ces objets : recentre sur eux ; trop petits a l'ecran (moins de 48 points), il zoome (jusqu'a 4) ;
    // trop grands, il dezoome. A zoom 1, la vue entiere est deja la.
    void reveal(const std::vector<hmi::Id>& ids) {
        const Preview& pv = d_.preview_;
        if (ids.empty() || !pv.ok || pv.view.width <= 0 || pv.view.height <= 0 || area_.w <= 1.f || area_.h <= 1.f) return;
        double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
        for (const auto id : ids) {
            const auto it = d_.objectIndex_.find(id);
            if (it == d_.objectIndex_.end() || it->second >= pv.view.objects.size()) continue;
            const auto b = hmi::edit::rotatedBounds(pv.view.objects[it->second]);
            x0 = std::min(x0, b.x);
            y0 = std::min(y0, b.y);
            x1 = std::max(x1, b.right());
            y1 = std::max(y1, b.bottom());
        }
        if (x0 > x1 || y0 > y1) return;
        const float vw = static_cast<float>(pv.view.width), vh = static_cast<float>(pv.view.height);
        const float fit = std::min(area_.w / vw, area_.h / vh);
        const float bw = static_cast<float>(std::max(8.0, x1 - x0)), bh = static_cast<float>(std::max(8.0, y1 - y0));
        const float fill = std::min(area_.w * 0.7f / (bw * fit), area_.h * 0.7f / (bh * fit));   // la selection a 70 % de l'apercu
        float z = zoom;
        if (std::max(bw, bh) * fit * z < 48.f) z = std::min(std::max(z, fill), 4.f);
        if (bw * fit * z > area_.w * 0.95f || bh * fit * z > area_.h * 0.95f) z = std::min(z, fill);
        zoom = std::clamp(z, 1.f, 10.f);
        pan = {0.f, 0.f};
        HmiViewport vp;
        gfx::Rect page;
        if (pageGeometry(vp, page)) {
            const gfx::Point c = vp.toScreen((x0 + x1) / 2.0, (y0 + y1) / 2.0);
            pan = {area_.x + area_.w / 2.f - c.x, area_.y + area_.h / 2.f - c.y};
        }
        clampPan();
        invalidate();
    }
    // La page de la vue dans l'apercu, a ce zoom et ce decalage (vide : pas d'apercu).
    [[nodiscard]] bool pageGeometry(HmiViewport& vp, gfx::Rect& page) const {
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0) return false;
        const float vw = static_cast<float>(pv.view.width), vh = static_cast<float>(pv.view.height);
        const float fit = std::min(area_.w / vw, area_.h / vh);
        const float z = fit * zoom;
        vp = HmiViewport{area_.x + (area_.w - vw * z) / 2.f + pan.x, area_.y + (area_.h - vh * z) / 2.f + pan.y, z};
        page = {vp.originX, vp.originY, vw * z, vh * z};
        return true;
    }
    void clampPan() {
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0 || zoom <= 1.f) {
            pan = {0.f, 0.f};
            return;
        }
        const float fit = std::min(area_.w / static_cast<float>(pv.view.width), area_.h / static_cast<float>(pv.view.height));
        const float pw = static_cast<float>(pv.view.width) * fit * zoom, ph = static_cast<float>(pv.view.height) * fit * zoom;
        // La page se deplace tant qu'il en reste 40 points a voir (zoomer sous la souris garde l'objet sous elle).
        const float mx = std::max(0.f, (pw + area_.w) / 2.f - 40.f), my = std::max(0.f, (ph + area_.h) / 2.f - 40.f);
        pan.x = std::clamp(pan.x, -mx, mx);
        pan.y = std::clamp(pan.y, -my, my);
    }
    // Zoomer en gardant `at` (un point de l'ecran) sous la souris ; sans point : le centre de l'apercu.
    void setZoom(float z, std::optional<gfx::Point> at = std::nullopt) {
        z = std::clamp(z, 1.f, 10.f);
        HmiViewport vp;
        gfx::Rect page;
        if (!pageGeometry(vp, page)) {
            zoom = z;
            return;
        }
        const gfx::Point s = at.value_or(gfx::Point{area_.x + area_.w / 2.f, area_.y + area_.h / 2.f});
        const float vx = (s.x - vp.originX) / vp.zoom, vy = (s.y - vp.originY) / vp.zoom;
        zoom = z;
        pan = {0.f, 0.f};
        HmiViewport centered;
        if (pageGeometry(centered, page)) pan = {s.x - vx * centered.zoom - centered.originX, s.y - vy * centered.zoom - centered.originY};
        clampPan();
        invalidate();
    }
    // L'objet de la vue sous ce point de l'ecran (le dernier dessine) ; nul : aucun.
    [[nodiscard]] const hmi::Object* objectAt(gfx::Point at) const {
        HmiViewport vp;
        gfx::Rect page;
        if (!pageGeometry(vp, page) || !area_.contains(at) || !page.contains(at)) return nullptr;
        const double x = (at.x - vp.originX) / vp.zoom, y = (at.y - vp.originY) / vp.zoom;
        const auto& objects = d_.preview_.view.objects;
        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
            const auto b = hmi::edit::rotatedBounds(*it);
            if (x >= b.x && x <= b.right() && y >= b.y && y <= b.bottom()) return &*it;
        }
        return nullptr;
    }
    [[nodiscard]] std::string objectTip(const hmi::Object& o) const {
        const Preview& pv = d_.preview_;
        const auto in = [&](const std::vector<hmi::Id>& ids) { return std::find(ids.begin(), ids.end(), o.id) != ids.end(); };
        const bool made = in(pv.touched.created), changed = in(pv.touched.modified), gone = in(pv.touched.removed);
        std::string tip = (o.name.empty() ? std::string("(sans nom)") : o.name) + " \xE2\x80\x94 " + std::string(hmi::kindLabel(o.kind));
        tip += made ? "\nNOUVEAU (cr\xC3\xA9\xC3\xA9 par la proposition)" : changed ? "\nMODIFI\xC3\x89 par la proposition"
             : gone ? "\nSUPPRIM\xC3\x89 par la proposition" : "\ninchang\xC3\xA9";
        for (const auto& it : d_.check_.items) {
            if (it.name != o.name) continue;
            std::size_t shown = 0;
            for (const auto& ch : it.changes) {
                if (made && (ch.key == "x" || ch.key == "y" || ch.key == "w" || ch.key == "h")) continue;
                if (shown++ >= 8) {
                    tip += "\n\xE2\x80\xA6";
                    break;
                }
                tip += "\n" + (made ? ch.key + " = " + ch.after
                                     : ch.key + " : " + (ch.before.empty() ? std::string("(vide)") : ch.before) + " \xE2\x86\x92 " + ch.after);
            }
            break;
        }
        const auto b = hmi::edit::rotatedBounds(o);
        tip += "\nx " + std::to_string(static_cast<long long>(std::lround(b.x))) + ", y " + std::to_string(static_cast<long long>(std::lround(b.y))) + " \xC2\xB7 "
             + std::to_string(static_cast<long long>(std::lround(b.w))) + " \xC3\x97 " + std::to_string(static_cast<long long>(std::lround(b.h)));
        // 1.22.4 : ses erreurs et ses avertissements (trois au plus)
        if (const auto it = d_.issueTexts_.find(o.id); it != d_.issueTexts_.end()) {
            for (std::size_t k = 0; k < it->second.size() && k < 3; ++k) tip += "\n" + it->second[k];
            if (it->second.size() > 3) tip += "\n\xE2\x80\xA6 et " + std::to_string(it->second.size() - 3) + " autres";
        }
        if (selectable(o.id)) tip += "\nClic : sa ligne dans la liste";
        return tip;
    }
    [[nodiscard]] bool selectable(hmi::Id id) const {   // 1.22.4 : une ligne de la liste designe cet objet
        for (const auto& r : rows)
            if (r.kind != Row::Kind::Heading && r.target >= 0 && static_cast<std::size_t>(r.target) < targets.size()) {
                const auto& t = targets[static_cast<std::size_t>(r.target)];
                if (std::find(t.begin(), t.end(), id) != t.end()) return true;
            }
        return false;
    }
    // 1.22.4 : << Erreurs (3) >>, << Avertissements (12) >> - le compte de la liste (ses titres le disent aussi).
    [[nodiscard]] std::string chipLabel(bool errors) const {
        return errors ? "Erreurs (" + std::to_string(d_.check_.errors.size()) + ")" : "Avertissements (" + std::to_string(d_.check_.warnings.size()) + ")";
    }
    [[nodiscard]] gfx::Rect chipRect(bool errors) const noexcept { return errors ? chipErr_ : chipWarn_; }
    [[nodiscard]] gfx::Rect previewArea() const noexcept { return area_; }
    [[nodiscard]] gfx::Rect toggleRect() const noexcept { return toggle_; }
    [[nodiscard]] gfx::Rect zoomInRect() const noexcept { return zoomIn_; }
    [[nodiscard]] gfx::Rect listRect() const noexcept { return list_; }
    [[nodiscard]] int headingAt(gfx::Point at) const {   // la ligne de titre sous ce point (-1 : aucune)
        for (const auto& [r, i] : hits_)
            if (r.contains(at) && i < rows.size() && rows[i].kind == Row::Kind::Heading) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] int removalAt(gfx::Point at) const {   // 1.21.0 : la ligne d'une suppression sous ce point (-1 : aucune)
        for (const auto& [r, i] : hits_)
            if (r.contains(at) && i < rows.size() && rows[i].op >= 0) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] int rowAt(gfx::Point at) const {   // 1.22.4 : une ligne (pas un titre) sous ce point (-1 : aucune)
        for (const auto& [r, i] : hits_)
            if (r.contains(at) && i < rows.size() && rows[i].kind != Row::Kind::Heading) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] int fixRowAt(gfx::Point at) const {   // 1.22.1 : une ligne qui se corrige au clic droit (-1 : aucune)
        for (const auto& [r, i] : hits_)
            if (r.contains(at) && i < rows.size() && rows[i].list > 0) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] gfx::Rect rowRect(int list, std::size_t line) const {   // 1.22.1 : sa place (vide : pas montree)
        for (const auto& [r, i] : hits_)
            if (i < rows.size() && rows[i].list == list && rows[i].line == line) return r;
        return {};
    }
    [[nodiscard]] gfx::Rect headingRect(const std::string& key) const {
        for (const auto& [r, i] : hits_)
            if (i < rows.size() && rows[i].kind == Row::Kind::Heading && groupKey(rows[i].text) == key) return r;
        return {};
    }

    bool hasTooltip() const override { return true; }
    std::string liveTooltip(gfx::Point mouse) const override {
        if (zoomOut_.contains(mouse)) return "Zoom arri\xC3\xA8re (Ctrl+moins, ou la molette sur l'aper\xC3\xA7u)";
        if (zoomFit_.contains(mouse)) return "Ajuster : la vue enti\xC3\xA8re (Ctrl+0, ou un double-clic sur l'aper\xC3\xA7u)";
        if (zoomIn_.contains(mouse)) return "Zoom avant (Ctrl+plus, ou la molette sur l'aper\xC3\xA7u)";
        if (toggle_.contains(mouse))
            return listHidden ? "Montrer la liste (ce que fait la proposition)" : "Masquer la liste : l'aper\xC3\xA7u prend toute la largeur";
        // 1.22.4 : les puces
        if (!listHidden && (chipErr_.contains(mouse) || chipWarn_.contains(mouse))) {
            const bool err = chipErr_.contains(mouse);
            return std::string(err ? "Ne montrer que les lignes li\xC3\xA9" "es \xC3\xA0 une erreur" : "Ne montrer que les lignes li\xC3\xA9" "es \xC3\xA0 un avertissement")
                   + " (les deux puces ensemble : l'un ou l'autre). \xC3\x89" "chap : tout remontrer.";
        }
        if (list_.contains(mouse))
            for (const auto& [r, i] : hits_) {
                if (!r.contains(mouse) || i >= rows.size()) continue;
                const auto& row = rows[i];
                if (row.kind == Row::Kind::Heading)
                    return folded.count(groupKey(row.text)) ? "Clic : d\xC3\xA9plier ce groupe" : "Clic : replier ce groupe";
                if (row.op >= 0)
                    return row.excluded ? "Clic : remettre cette suppression dans le plan."
                                        : "Clic : retirer cette suppression du plan (ce qui en d\xC3\xA9pend est ignor\xC3\xA9, la v\xC3\xA9rification se refait).";
                // 1.22.4 : ce que le clic encadre dans l'apercu
                const std::size_t n = row.target >= 0 && static_cast<std::size_t>(row.target) < targets.size() ? targets[static_cast<std::size_t>(row.target)].size() : 0;
                const std::string show = n == 0 ? std::string{}
                                         : n == 1 ? std::string("\nClic : l'encadrer dans l'aper\xC3\xA7u.")
                                                  : "\nClic : encadrer ses " + std::to_string(n) + " objets dans l'aper\xC3\xA7u.";
                if (row.list == 3) return row.text + " \xE2\x80\x94 " + row.right + show + "\nClic droit : revenir sur cette correction.";
                if (row.list > 0) return row.text + show + "\nClic droit : les corrections possibles.";   // 1.22.1
                return row.text + (row.right.empty() ? std::string{} : " \xE2\x80\x94 " + row.right) + show;
            }
        if (area_.contains(mouse)) {
            if (const auto* o = objectAt(mouse)) return objectTip(*o);
            return "Molette : zoomer \xC2\xB7 glisser : d\xC3\xA9placer (zoom\xC3\xA9) \xC2\xB7 double-clic : ajuster";
        }
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(kMaxW, std::max(560.f, r.w - 40.f));
        const float h = std::min(kMaxH, std::max(420.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        const float x = panel_.x + 16.f, right = panel_.right() - 16.f;
        close_ = {panel_.right() - 38.f, panel_.y + 6.f, 28.f, 24.f};
        counts_ = {x, panel_.y + 58.f, right - x, 30.f};
        const float top = counts_.bottom() + 12.f;
        const float bottom = panel_.bottom() - 56.f;
        const float leftW = listHidden ? 0.f : std::clamp((right - x) * 0.42f, 300.f, 520.f);
        float listTop = top, previewTop = top;
        const bool choice = d_.variable_ && d_.variable_->visibility() == ui::Visibility::Visible;
        if (choice) {
            // 1.16.1 : la liste masquee - le choix de la variable passe au-dessus de l'apercu.
            const float cw = listHidden ? std::min(520.f, right - x - 20.f) : leftW;
            const float cx = listHidden ? x + 20.f : x;
            varLabel_ = {cx, top, cw, 20.f};
            d_.variable_->setBounds({cx, top + 22.f, cw, 28.f});
            listTop = top + 60.f;
            if (listHidden) previewTop = top + 60.f;
        }
        // 1.22.4 : LA RECHERCHE ET LES PUCES, en haut de la liste (masquee : rien).
        searchBox_ = chips_ = chipErr_ = chipWarn_ = gfx::Rect{};
        if (!listHidden) {
            searchBox_ = {x, listTop, leftW, 28.f};
            chips_ = {x, listTop + 32.f, leftW, 22.f};
            chipErr_ = {chips_.x, chips_.y, ui::measureWidth(chipLabel(true), kSmall) + 30.f, chips_.h};
            chipWarn_ = {chipErr_.right() + 8.f, chips_.y, ui::measureWidth(chipLabel(false), kSmall) + 30.f, chips_.h};
            listTop += 60.f;
        }
        if (d_.search_) {
            d_.search_->setVisibility(listHidden ? ui::Visibility::Collapsed : ui::Visibility::Visible);
            if (!listHidden) d_.search_->setBounds(searchBox_);
        }
        list_ = {x, listTop, leftW, std::max(60.f, bottom - listTop)};
        if (listHidden) list_.w = 0.f;
        // 1.16.1 : le bouton qui masque / montre la liste (entre elle et l'apercu).
        toggle_ = listHidden ? gfx::Rect{x, previewTop, 14.f, 48.f} : gfx::Rect{x + leftW + 1.f, previewTop, 14.f, 48.f};
        const float px = listHidden ? x + 20.f : x + leftW + 16.f;
        preview_ = {px, previewTop, right - px, std::max(80.f, bottom - previewTop - 24.f)};
        legend_ = {preview_.x, preview_.bottom() + 6.f, preview_.w, 18.f};
        area_ = {preview_.x + 8.f, preview_.y + 8.f, preview_.w - 16.f, preview_.h - 16.f};
        zoomIn_ = {preview_.right() - 34.f, preview_.y + 6.f, 28.f, 24.f};
        zoomFit_ = {zoomIn_.x - 66.f, zoomIn_.y, 64.f, 24.f};
        zoomOut_ = {zoomFit_.x - 30.f, zoomIn_.y, 28.f, 24.f};
        clampPan();
        float bx = right;
        for (ui::Button* b : {d_.apply_, d_.cancel_}) {
            if (!b) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 34.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        if (d_.edit_) d_.edit_->setBounds({x, panel_.bottom() - 44.f, ui::measureWidth(d_.edit_->text(), gfx::FontId{16}) + 34.f, 30.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 16.f;
        const auto& a = d_.panel_.assistant();
        const Proposal* p = a.proposal(d_.message_);
        // Le titre, la croix.
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {x, panel_.y + 12.f, 20.f, 20.f}, c.accent);
        ctx.r.drawText({x + 28.f, panel_.y + 10.f}, "Pr\xC3\xA9visualisation des modifications", kTitleFont, c.text);
        ui::drawIcon(ctx.r, ui::Icon::Close, {close_.x + 6.f, close_.y + 4.f, 16.f, 16.f}, c.textMuted);
        if (!p) return;
        // 1.16.0 : une vue neuve, ou le projet seul (des variables, des types).
        const std::string where = createsView(*p) ? "Vue neuve \xC2\xAB " + newViewName(*p) + " \xC2\xBB"
                                : usesOpenView(*p) ? "Vue \xC2\xAB " + p->view + " \xC2\xBB"
                                                   : std::string("Le projet (variables, types IHM)");
        const std::string sub = where + " \xC2\xB7 " + (p->summary.empty() ? std::string("proposition") : p->summary)
                                + " \xC2\xB7 rien ne change avant Appliquer";
        ctx.r.drawText({x, panel_.y + 38.f}, elide(ctx.r, sub, kSmall, panel_.w - 32.f), kSmall, c.textMuted);
        // Les comptes : trois tuiles. Perimee (la vue a change, ou n'existe plus) : ceux de la
        // proposition (son texte les annonce), pas ceux d'une validation qui n'a rien pu lire.
        const Check& ck = d_.check_;
        const bool asProposed = ck.stale || ck.viewMissing;
        const struct { const char* label; int n; gfx::Color col; } tiles[] = {
            {"\xC3\x80 cr\xC3\xA9" "er", asProposed ? p->create : ck.create, c.ok},
            {"\xC3\x80 modifier", asProposed ? p->modify : ck.modify, c.info},
            {"\xC3\x80 supprimer", asProposed ? p->remove : ck.remove, c.error}};
        float tx = counts_.x;
        for (const auto& t : tiles) {
            const std::string n = std::to_string(t.n);
            const float tw = ctx.r.measure(n, kTitleFont).width + ctx.r.measure(t.label, kSmall).width + 30.f;
            const gfx::Rect tile{tx, counts_.y, tw, counts_.h};
            ctx.r.fillRoundedRect(tile, alpha(t.n > 0 ? t.col : c.textDisabled, 30), 5.f);
            ctx.r.fillRect({tile.x, tile.y + 4.f, 3.f, tile.h - 8.f}, t.n > 0 ? t.col : c.textDisabled);
            ctx.r.drawText({tile.x + 12.f, tile.y + (tile.h - ctx.r.lineHeight(kTitleFont)) / 2.f}, n, kTitleFont, c.text);
            ctx.r.drawText({tile.x + 18.f + ctx.r.measure(n, kTitleFont).width, tile.y + (tile.h - ctx.r.lineHeight(kSmall)) / 2.f}, t.label, kSmall,
                           c.textMuted);
            tx += tw + 10.f;
        }
        if (const std::string made = creationSummary(*p); !made.empty() && !(ck.stale || ck.viewMissing))
            ctx.r.drawText({tx + 8.f, counts_.y + (counts_.h - ctx.r.lineHeight(kSmall)) / 2.f},
                           elide(ctx.r, "+ " + made, kSmall, counts_.right() - tx - 8.f), kSmall, c.ok);
        if (ck.stale || ck.viewMissing) {
            ctx.r.drawText({tx + 8.f, counts_.y + (counts_.h - ctx.r.lineHeight(kSmall)) / 2.f},
                           elide(ctx.r, "P\xC3\xA9rim\xC3\xA9" "e : redemande-la (Modifier la demande).", kSmall, counts_.right() - tx - 8.f), kSmall,
                           c.warning);
        }
        if (d_.variable_ && d_.variable_->visibility() == ui::Visibility::Visible) {
            // Le nom introuvable reste dit apres le choix (la validation ne le cite plus : il est remplace).
            const std::string name = "\xC2\xAB " + d_.missingName_ + " \xC2\xBB n'existe pas";
            // 1.22.1 : le choix pour CETTE variable (une correction), sinon le choix unique d'avant.
            const VariableChoice* ch = choiceFor(*p, d_.missingName_);
            if (!ch && (!p->variable.use.empty() || p->variable.create)) ch = &p->variable;
            const bool chosen = ch != nullptr;
            std::string label;
            if (ch && !ch->create) {
                label = name + " : remplac\xC3\xA9" "e par " + ch->use + ".";
            } else if (ch) {
                const std::string made = ch == &p->variable ? d_.missingName_ : ch->use.empty() ? creatableName(d_.missingName_) : ch->use;
                const std::string type = ch->type.empty() ? std::string("BOOL") : ch->type;
                label = made == d_.missingName_ ? name + " : elle sera cr\xC3\xA9\xC3\xA9" "e (" + type + ")."
                                                : name + " : " + made + " sera cr\xC3\xA9\xC3\xA9" "e (" + type + ").";
            } else {
                label = name + " : quelle variable utiliser ?";
                if (d_.otherMissing_ > 0)
                    label += " (et " + std::to_string(d_.otherMissing_) + (d_.otherMissing_ > 1 ? " autres" : " autre") + " : clic droit sur leur erreur)";
            }
            ctx.r.drawText({varLabel_.x, varLabel_.y + 2.f}, elide(ctx.r, label, kSmall, varLabel_.w), kSmall, chosen ? c.textMuted : c.error);
        }
        paintFilter(ctx);   // 1.22.4
        paintList(ctx);
        paintPreview(ctx);
    }

    // 1.22.4 : le compte du filtre (a droite de la recherche, << 12 lignes >>) et les puces Erreurs, Avertissements.
    void paintFilter(const ui::PaintContext& ctx) {
        if (listHidden || chips_.w <= 0.f) return;
        const auto& c = ctx.theme.color;
        if (filtering && d_.search_) {
            const std::string n = matches == 0 ? std::string("aucune ligne") : std::to_string(matches) + (matches > 1 ? " lignes" : " ligne");
            const gfx::Rect fb = d_.search_->field().bounds();
            if (searchBox_.right() - (fb.right() + 10.f) >= ctx.r.measure(n, kSmall).width)
                ctx.r.drawText({fb.right() + 10.f, fb.y + (fb.h - ctx.r.lineHeight(kSmall)) / 2.f}, n, kSmall,
                               matches == 0 ? ctx.theme.onSurface(c.warning) : c.textMuted);
        }
        const auto chip = [&](const gfx::Rect& r, bool errors, bool on) {
            const std::size_t n = errors ? d_.check_.errors.size() : d_.check_.warnings.size();
            const gfx::Color col = errors ? c.error : c.warning;
            if (on) {
                ctx.r.fillRoundedRect(r, alpha(col, 70), 4.f);
                ctx.r.strokeRect(r, col, 1.5f);
            } else {
                ctx.r.fillRoundedRect(r, c.inputBg, 4.f);
                ctx.r.strokeRect(r, c.border, 1.f);
            }
            pastille(ctx.r, {r.x + 12.f, r.y + r.h / 2.f}, 13.f, n > 0 ? col : c.textDisabled, c.panelBg);
            ctx.r.drawText({r.x + 23.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) / 2.f}, chipLabel(errors), kSmall, n > 0 ? c.text : c.textDisabled);
        };
        chip(chipErr_, true, onlyErrors);
        chip(chipWarn_, false, onlyWarnings);
    }

    void paintList(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(list_, c.inputBg);
        ctx.r.strokeRect(list_, c.border, 1.f);
        const float lh = ctx.r.lineHeight(kText) + 6.f;
        const float lhSmall = ctx.r.lineHeight(kSmall) + 3.f;
        // Les hauteurs d'abord (un avertissement, une erreur : sur plusieurs lignes).
        const float w0 = list_.w - 24.f;
        std::vector<std::vector<std::string>> wrapped(rows.size());
        float content = 8.f;
        hits_.clear();
        rowTop_.assign(rows.size(), -1.f);
        if (list_.w <= 1.f) return;   // 1.16.1 : la liste masquee
        const auto heightOf = [&](std::size_t i) {
            const bool multi = rows[i].kind == Row::Kind::Warning || rows[i].kind == Row::Kind::Error;
            return multi ? std::max(lh, static_cast<float>(wrapped[i].size()) * lhSmall + 6.f) : lh;
        };
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (hidden(i)) continue;   // 1.16.1 : un groupe replie ; 1.22.4 : le filtre
            const bool multi = rows[i].kind == Row::Kind::Warning || rows[i].kind == Row::Kind::Error;
            wrapped[i] = multi ? wrap(rows[i].text, kSmall, w0 - 28.f) : std::vector<std::string>{rows[i].text};
            rowTop_[i] = content - 4.f;
            content += heightOf(i);
        }
        content_ = content;
        // 1.22.4 : la ligne choisie depuis l'apercu : la liste se deroule jusqu'a elle.
        if (scrollToRow >= 0 && static_cast<std::size_t>(scrollToRow) < rows.size() && rowTop_[static_cast<std::size_t>(scrollToRow)] >= 0.f) {
            const float top = rowTop_[static_cast<std::size_t>(scrollToRow)], h = heightOf(static_cast<std::size_t>(scrollToRow));
            if (top - scroll < 0.f) scroll = top - 4.f;
            else if (top + h - scroll > list_.h) scroll = top + h + 8.f - list_.h;
        }
        scrollToRow = -1;
        scroll = std::clamp(scroll, 0.f, std::max(0.f, content - list_.h));
        const float space = bar_.space(list_, content, list_.h);
        ctx.r.pushClip(list_);
        if (filtering && matches == 0)
            ctx.r.drawText({list_.x + 12.f, list_.y + 10.f}, elide(ctx.r, "Aucune ligne ne correspond (\xC3\x89" "chap : tout remontrer).", kSmall, list_.w - 24.f), kSmall,
                           c.textMuted);
        float y = list_.y + 4.f - scroll;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (hidden(i)) continue;
            const auto& row = rows[i];
            const float rh = heightOf(i);
            const gfx::Rect rowRect{list_.x, y, list_.w - space, rh};
            if (rowRect.bottom() > list_.y && rowRect.y < list_.bottom()) hits_.emplace_back(rowRect, i);
            if (y + rh >= list_.y && y <= list_.bottom()) {
                const float ty = y + 3.f;
                const float w = list_.w - space - 12.f;
                // 1.22.4 : la ligne choisie (ce qu'elle designe est encadre dans l'apercu) ; la couleur d'une erreur, d'un
                // avertissement (le texte, une pastille).
                if (static_cast<int>(i) == selected && row.kind != Row::Kind::Heading) {
                    ctx.r.fillRect({list_.x + 1.f, y, list_.w - space - 2.f, rh}, alpha(c.accent, 45));
                    ctx.r.fillRect({list_.x + 1.f, y, 3.f, rh}, c.accent);
                }
                const gfx::Color issueCol = row.issue > 0 ? ctx.theme.onSurface(issueColor(ctx.theme, row.issue)) : c.text;
                switch (row.kind) {
                    case Row::Kind::Heading: {
                        // 1.16.1 : un groupe se replie (sa fleche ; le clic sur son titre).
                        const bool shut = !filtering && folded.count(groupKey(row.text)) > 0;
                        ctx.r.fillRect({list_.x + 1.f, y, list_.w - 2.f, lh}, c.rowAltBg);
                        chevron(ctx.r, {list_.x + 5.f, y + (lh - 14.f) / 2.f, 14.f, 14.f}, shut ? 0 : 1, c.textMuted);
                        float tw = w - 14.f;
                        if (row.issue > 0) {   // 1.22.4 : le groupe contient une erreur, un avertissement
                            pastille(ctx.r, {list_.x + w - 6.f, y + lh / 2.f}, 13.f, issueColor(ctx.theme, row.issue), c.panelBg);
                            tw -= 18.f;
                        }
                        ctx.r.drawText({list_.x + 22.f, ty}, elide(ctx.r, row.text, kSmall, tw), kSmall, c.textMuted);
                        break;
                    }
                    case Row::Kind::Item: {
                        const gfx::Color col = row.excluded ? c.textDisabled : row.what == 0 ? c.ok : row.what == 1 ? c.info : c.error;
                        const char* tag = row.what == 0 ? "+" : row.what == 1 ? "~" : "\xE2\x88\x92";
                        ctx.r.fillRoundedRect({list_.x + 8.f, y + 4.f, 16.f, lh - 8.f}, alpha(col, 50), 3.f);
                        ctx.r.drawText({list_.x + 12.f, ty}, tag, kText, col);
                        const float rw = ctx.r.measure(row.right, kSmall).width;
                        ctx.r.drawText({list_.x + w - rw, ty + 1.f}, row.right, kSmall, c.textMuted);
                        float tw = w - rw - 34.f;
                        if (row.issue > 0) {
                            pastille(ctx.r, {list_.x + w - rw - 12.f, y + lh / 2.f}, 13.f, issueColor(ctx.theme, row.issue), c.panelBg);
                            tw -= 20.f;
                        }
                        ctx.r.drawText({list_.x + 30.f, ty}, elide(ctx.r, row.text, kText, tw), kText, issueCol);
                        break;
                    }
                    case Row::Kind::Change:
                        ctx.r.drawText({list_.x + 30.f, ty}, elide(ctx.r, row.text, kSmall, w - 24.f), kSmall, c.textMuted);
                        break;
                    case Row::Kind::Note: {
                        float tw = w;
                        if (row.issue > 0) {
                            pastille(ctx.r, {list_.x + w - 2.f, y + lh / 2.f}, 13.f, issueColor(ctx.theme, row.issue), c.panelBg);
                            tw -= 20.f;
                        }
                        ctx.r.drawText({list_.x + 12.f, ty}, elide(ctx.r, row.text, kSmall, tw), kSmall, issueCol);
                        break;
                    }
                    case Row::Kind::Warning:
                    case Row::Kind::Error: {
                        const bool err = row.kind == Row::Kind::Error;
                        ui::drawIcon(ctx.r, err ? ui::Icon::Error : ui::Icon::Warning, {list_.x + 8.f, y + 4.f, 14.f, 14.f}, err ? c.error : c.warning);
                        float ly = ty;
                        for (const auto& l : wrapped[i]) {
                            ctx.r.drawText({list_.x + 28.f, ly}, l, kSmall, issueCol);
                            ly += lhSmall;
                        }
                        break;
                    }
                }
            }
            y += rh;
        }
        ctx.r.popClip();
        bar_.paint(ctx, list_, content, list_.h, scroll);
    }

    void paintPreview(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(preview_, c.windowBg);
        ctx.r.strokeRect(preview_, c.border, 1.f);
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0) {
            ctx.r.drawText({preview_.x + 12.f, preview_.y + 12.f},
                           elide(ctx.r, pv.why.empty() ? std::string("Pas d'aper\xC3\xA7u.") : "Pas d'aper\xC3\xA7u : " + pv.why, kText, preview_.w - 24.f),
                           kText, c.textMuted);
        } else {
            const auto& v = pv.view;
            // 1.16.1 : le zoom et le decalage de l'apercu (pageGeometry) ; tout se dessine dans sa zone.
            HmiViewport vp;
            gfx::Rect page;
            (void)pageGeometry(vp, page);
            const float z = vp.zoom;
            ctx.r.pushClip(area_);
            ctx.r.fillRect(page, parseColor(v.background, gfx::Color::rgb(0x20242B)));
            HmiPaintOptions opt;
            const auto doc = d_.panel_.assistant().host().document ? d_.panel_.assistant().host().document() : nullptr;
            if (doc) {
                opt.project = &doc->project;
                opt.assets = &doc->project.assets;
            }
            ctx.r.pushClip(page);
            paintHmiView(ctx.r, v, vp, HmiPropertySource{}, ctx.theme, opt);
            // 1.22.4 : l'objet par son id (le rang, calcule une fois par proposition) - plus une boucle sur la vue par objet.
            const auto box = [&](hmi::Id id, gfx::Rect& out) {
                const auto it = d_.objectIndex_.find(id);
                if (it == d_.objectIndex_.end() || it->second >= v.objects.size()) return false;
                const auto bx = hmi::edit::rotatedBounds(v.objects[it->second]);
                const auto p0 = vp.toScreen(bx.x, bx.y);
                out = {p0.x - 2.f, p0.y - 2.f, static_cast<float>(bx.w) * z + 4.f, static_cast<float>(bx.h) * z + 4.f};
                return true;
            };
            const auto grow = [](gfx::Rect r, float by) { return gfx::Rect{r.x - by, r.y - by, r.w + 2.f * by, r.h + 2.f * by}; };
            gfx::Rect r{};
            for (const auto id : pv.touched.created)
                if (box(id, r)) ctx.r.strokeRect(r, c.ok, 2.f);
            for (const auto id : pv.touched.modified)
                if (box(id, r)) ctx.r.strokeRect(r, c.info, 2.f);
            for (const auto id : pv.touched.removed)
                if (box(id, r)) {
                    ctx.r.fillRect(r, alpha(c.error, 40));
                    ctx.r.strokeRect(r, c.error, 2.f);
                    ctx.r.line({r.x, r.y}, {r.right(), r.bottom()}, c.error, 2.f);
                    ctx.r.line({r.right(), r.y}, {r.x, r.bottom()}, c.error, 2.f);
                }
            // 1.22.4 : LES ERREURS ET LES AVERTISSEMENTS - un second cadre, plus epais, de leur couleur, et la pastille (!) ;
            // les avertissements d'abord (une erreur se dessine par-dessus).
            for (const int level : {1, 2})
                for (const auto& [id, lv] : d_.issues_)
                    if (lv == level && box(id, r)) {
                        const gfx::Rect f = grow(r, 3.f);
                        const gfx::Color col = issueColor(ctx.theme, level);
                        ctx.r.strokeRect(f, col, 3.f);
                        pastille(ctx.r, {f.right(), f.y}, 14.f, col, c.panelBg);
                    }
            // 1.22.4 : CE QUE DESIGNE LA LIGNE CHOISIE - un cadre de la couleur d'accent, plus epais ; une seconde, un halo
            // pulse autour (puis le cadre reste).
            if (!highlight.empty()) {
                if (pulseStart < 0.0) pulseStart = ctx.time;
                const double since = ctx.time - pulseStart;
                const bool pulsing = since >= 0.0 && since < 1.0;
                const float wave = pulsing ? 0.5f - 0.5f * static_cast<float>(std::cos(since * 6.2831853 * 2.0)) : 0.f;
                for (const auto id : highlight)
                    if (box(id, r)) {
                        const gfx::Rect f = grow(r, 6.f);
                        ctx.r.strokeRect(f, c.accent, 3.f);
                        if (pulsing) ctx.r.strokeRect(grow(f, 3.f + 6.f * wave), alpha(c.accent, static_cast<int>(220.f * (1.f - wave))), 2.f);
                    }
                if (pulsing) invalidate();
            }
            ctx.r.popClip();
            ctx.r.strokeRect(page, c.border, 1.f);
            ctx.r.popClip();
            // 1.16.1 : les boutons du zoom (en haut a droite de l'apercu) et le zoom en %.
            const auto button = [&](const gfx::Rect& b, const std::string& text) {
                ctx.r.fillRoundedRect(b, alpha(c.panelBg, 230), 4.f);
                ctx.r.strokeRect(b, c.border, 1.f);
                const float tw = ctx.r.measure(text, kSmall).width;
                ctx.r.drawText({b.x + (b.w - tw) / 2.f, b.y + (b.h - ctx.r.lineHeight(kSmall)) / 2.f}, text, kSmall, c.text);
            };
            button(zoomOut_, "\xE2\x88\x92");
            button(zoomFit_, "Ajuster");
            button(zoomIn_, "+");
            const std::string pct = std::to_string(static_cast<int>(std::lround(z * 100.f))) + " %";
            const float pw = ctx.r.measure(pct, kSmall).width + 12.f;
            const gfx::Rect pr{zoomOut_.x - pw - 4.f, zoomOut_.y, pw, zoomOut_.h};
            ctx.r.fillRoundedRect(pr, alpha(c.panelBg, 230), 4.f);
            ctx.r.drawText({pr.x + 6.f, pr.y + (pr.h - ctx.r.lineHeight(kSmall)) / 2.f}, pct, kSmall, c.textMuted);
        }
        // 1.16.1 : le bouton de la liste (la masquer, la montrer).
        ctx.r.fillRoundedRect(toggle_, c.rowAltBg, 3.f);
        ctx.r.strokeRect(toggle_, c.border, 1.f);
        chevron(ctx.r, {toggle_.x, toggle_.y + (toggle_.h - 14.f) / 2.f, 14.f, 14.f}, listHidden ? 0 : 2, c.textMuted);
        // La legende.
        float lx = legend_.x;
        const auto mark = [&](gfx::Color col, const char* text, bool cross) {
            ctx.r.strokeRect({lx, legend_.y + 3.f, 14.f, 11.f}, col, 2.f);
            if (cross) ctx.r.line({lx, legend_.y + 3.f}, {lx + 14.f, legend_.y + 14.f}, col, 1.5f);
            ctx.r.drawText({lx + 20.f, legend_.y}, text, kSmall, c.textMuted);
            lx += 20.f + ctx.r.measure(text, kSmall).width + 18.f;
        };
        mark(c.ok, "nouveau", false);
        mark(c.info, "modifi\xC3\xA9", false);
        mark(c.error, "supprim\xC3\xA9", true);
        // 1.22.4 : l'erreur, l'avertissement (le cadre epais et sa pastille)
        const auto issueMark = [&](int level, const char* text) {
            const gfx::Color col = issueColor(ctx.theme, level);
            ctx.r.strokeRect({lx, legend_.y + 3.f, 14.f, 11.f}, col, 3.f);
            ctx.r.fillRoundedRect({lx + 10.f, legend_.y - 1.f, 8.f, 8.f}, col, 4.f);
            ctx.r.drawText({lx + 22.f, legend_.y}, text, kSmall, c.textMuted);
            lx += 22.f + ctx.r.measure(text, kSmall).width + 18.f;
        };
        issueMark(2, "erreur");
        issueMark(1, "avertissement");
        ctx.r.drawText({lx, legend_.y}, elide(ctx.r, "Aper\xC3\xA7u : une copie de la vue \xC2\xB7 molette : zoom \xC2\xB7 survol : l'objet \xC2\xB7 clic : sa ligne", kSmall,
                                              legend_.right() - lx), kSmall, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const float lh = ui::lineHeight(kText) + 6.f;
        const float content = content_;
        if (list_.w > 1.f && bar_.handle(*this, ev, list_, content, list_.h, scroll)) return ui::EventResult::Consumed;
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos)) {
            scroll = std::clamp(scroll - w->dy * 3.f * lh, 0.f, std::max(0.f, content - list_.h));
            invalidate();
            return ui::EventResult::Consumed;
        }
        // 1.16.1 : la molette sur l'apercu zoome (autour de la souris).
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && area_.contains(w->pos) && w->dy != 0.f) {
            setZoom(zoom * (w->dy > 0.f ? 1.25f : 0.8f), w->pos);
            return ui::EventResult::Consumed;
        }
        // Le glisser de l'apercu zoome.
        if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && dragging_) {
            pan = {pan0_.x + m->pos.x - dragFrom_.x, pan0_.y + m->pos.y - dragFrom_.y};
            clampPan();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && dragging_) {
            dragging_ = false;
            releaseMouse();
            return ui::EventResult::Consumed;
        }
        // 1.22.1 : LE CLIC DROIT sur une erreur, un avertissement, une correction : le menu des corrections.
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right && list_.contains(d->pos)) {
            if (const int k = fixRowAt(d->pos); k >= 0) {
                const Row& row = rows[static_cast<std::size_t>(k)];
                (void)d_.openFixMenu(row.list, row.line, d->pos);
            }
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            if (close_.contains(d->pos)) {
                d_.close();
                return ui::EventResult::Consumed;
            }
            if (zoomOut_.contains(d->pos) || zoomIn_.contains(d->pos) || zoomFit_.contains(d->pos)) {
                if (zoomFit_.contains(d->pos)) setZoom(1.f);
                else setZoom(zoom * (zoomIn_.contains(d->pos) ? 1.25f : 0.8f));
                return ui::EventResult::Consumed;
            }
            if (toggle_.contains(d->pos)) {
                listHidden = !listHidden;
                invalidateLayout();
                invalidate();
                return ui::EventResult::Consumed;
            }
            // 1.22.4 : les puces Erreurs, Avertissements (a zero : rien) - l'une, l'autre ou les deux.
            if (chipErr_.contains(d->pos) || chipWarn_.contains(d->pos)) {
                const bool err = chipErr_.contains(d->pos);
                if ((err ? d_.check_.errors.size() : d_.check_.warnings.size()) > 0) {
                    bool& on = err ? onlyErrors : onlyWarnings;
                    on = !on;
                    applyFilter();
                }
                return ui::EventResult::Consumed;
            }
            if (const int h = headingAt(d->pos); h >= 0) {
                const std::string key = groupKey(rows[static_cast<std::size_t>(h)].text);
                if (!folded.erase(key)) folded.insert(key);
                invalidate();
                return ui::EventResult::Consumed;
            }
            // 1.21.0 : une suppression : la retirer du plan, ou l'y remettre.
            if (const int k = removalAt(d->pos); k >= 0) {
                const Row& row = rows[static_cast<std::size_t>(k)];
                (void)d_.excludeRemoval(static_cast<std::size_t>(row.op), !row.excluded);
                return ui::EventResult::Consumed;
            }
            // 1.22.4 : une ligne - ce qu'elle designe s'encadre dans l'apercu, qui s'y recentre.
            if (const int k = rowAt(d->pos); k >= 0) {
                select(k, true);
                return ui::EventResult::Consumed;
            }
            if (area_.contains(d->pos)) {
                if (d->clickCount >= 2) {
                    setZoom(1.f);
                } else {
                    if (const auto* o = objectAt(d->pos)) selectObject(o->id, o->name);   // 1.22.4 : sa ligne dans la liste
                    if (zoom > 1.f) {
                        dragging_ = true;
                        dragFrom_ = d->pos;
                        pan0_ = pan;
                        captureMouse();
                    }
                }
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    AiPreviewDialog&  d_;
    float             content_{0.f};
    gfx::Rect         panel_{}, close_{}, counts_{}, list_{}, preview_{}, legend_{}, varLabel_{};
    gfx::Rect         area_{}, toggle_{}, zoomIn_{}, zoomOut_{}, zoomFit_{};   // 1.16.1
    gfx::Rect         searchBox_{}, chips_{}, chipErr_{}, chipWarn_{};          // 1.22.4
    std::vector<float> rowTop_;                                                // 1.22.4 : le haut de chaque ligne montree (dans la liste)
    std::vector<std::pair<gfx::Rect, std::size_t>> hits_;                    // 1.16.1 : les lignes montrees de la liste
    bool              dragging_{false};
    gfx::Point        dragFrom_{}, pan0_{};
    ui::EdgeScrollBar bar_;
};

// ---- 1.16.1 : le zoom, la liste repliable, les infobulles (les gestes, pour les essais et les scripts) ----
void AiPreviewDialog::setPreviewZoom(float z) {
    if (auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot())) b->setZoom(z);
}
float AiPreviewDialog::previewZoom() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->zoom : 1.f;
}
bool AiPreviewDialog::foldGroup(const std::string& heading, bool folded) {
    auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot());
    if (!b) return false;
    const bool known = std::any_of(b->rows.begin(), b->rows.end(),
                                   [&](const Row& r) { return r.kind == Row::Kind::Heading && groupKey(r.text) == heading; });
    if (!known) return false;
    if (folded) b->folded.insert(heading);
    else b->folded.erase(heading);
    b->invalidate();
    return true;
}
bool AiPreviewDialog::groupFolded(const std::string& heading) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b && b->folded.count(heading) > 0;
}
std::size_t AiPreviewDialog::shownRows() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    if (!b) return 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < b->rows.size(); ++i)
        if (!b->hidden(i)) ++n;
    return n;
}
void AiPreviewDialog::setListHidden(bool hidden) {
    auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot());
    if (!b || b->listHidden == hidden) return;
    b->listHidden = hidden;
    b->invalidateLayout();
    b->invalidate();
}
bool AiPreviewDialog::listHidden() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b && b->listHidden;
}
std::string AiPreviewDialog::previewTooltip(gfx::Point at) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->liveTooltip(at) : std::string{};
}
gfx::Rect AiPreviewDialog::previewArea() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->previewArea() : gfx::Rect{};
}
gfx::Rect AiPreviewDialog::listArea() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->listRect() : gfx::Rect{};
}
gfx::Rect AiPreviewDialog::groupRect(const std::string& heading) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->headingRect(heading) : gfx::Rect{};
}
gfx::Point AiPreviewDialog::objectPoint(const std::string& name) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    if (!b) return {-1.f, -1.f};
    HmiViewport vp;
    gfx::Rect page;
    if (!b->pageGeometry(vp, page)) return {-1.f, -1.f};
    for (const auto& o : preview_.view.objects)
        if (o.name == name) {
            const auto bx = hmi::edit::rotatedBounds(o);
            return vp.toScreen(bx.x + bx.w / 2.0, bx.y + bx.h / 2.0);
        }
    return {-1.f, -1.f};
}

// ---- 1.22.4 : ou sont les erreurs, la recherche, la ligne choisie (les gestes, pour les essais et les scripts) ----------------
namespace {
AiPreviewBody* bodyOf(const AiPreviewDialog& d) { return dynamic_cast<AiPreviewBody*>(const_cast<AiPreviewDialog&>(d).widgetRoot()); }
} // namespace

int AiPreviewDialog::issueOf(hmi::Id id) const {
    const auto it = issues_.find(id);
    return it == issues_.end() ? 0 : it->second;
}
hmi::Id AiPreviewDialog::objectId(const std::string& name) const {
    for (const auto& o : preview_.view.objects)
        if (o.name == name) return o.id;
    return hmi::kNoId;
}
void AiPreviewDialog::setSearch(const std::string& text) {
    if (search_) search_->setText(text);   // `changed` : le filtre se refait
}
std::string AiPreviewDialog::searchText() const { return search_ ? search_->text() : std::string{}; }
void AiPreviewDialog::setIssueFilter(bool errors, bool warnings) {
    auto* b = bodyOf(*this);
    if (!b) return;
    b->onlyErrors = errors && !check_.errors.empty();
    b->onlyWarnings = warnings && !check_.warnings.empty();
    b->applyFilter();
}
bool AiPreviewDialog::errorFilter() const {
    const auto* b = bodyOf(*this);
    return b && b->onlyErrors;
}
bool AiPreviewDialog::warningFilter() const {
    const auto* b = bodyOf(*this);
    return b && b->onlyWarnings;
}
std::vector<std::string> AiPreviewDialog::visibleRows() const {
    std::vector<std::string> out;
    if (const auto* b = bodyOf(*this))
        for (std::size_t i = 0; i < b->rows.size(); ++i)
            if (!b->hidden(i)) out.push_back(b->rows[i].text);
    return out;
}
std::size_t AiPreviewDialog::matchCount() const {
    const auto* b = bodyOf(*this);
    return b && b->filtering ? b->matches : 0;
}
bool AiPreviewDialog::selectRow(const std::string& part) {
    auto* b = bodyOf(*this);
    if (!b || part.empty()) return false;
    // la ligne qui dit exactement `part` d'abord (une variable, un objet), sinon la premiere qui le contient
    for (const bool exact : {true, false})
        for (std::size_t i = 0; i < b->rows.size(); ++i) {
            const Row& r = b->rows[i];
            if (r.kind == Row::Kind::Heading || (b->filtering && b->hidden(i))) continue;
            if (exact ? r.text != part : r.text.find(part) == std::string::npos && r.right.find(part) == std::string::npos) continue;
            b->show(static_cast<int>(i));   // son groupe se deplie, la liste se deroule jusqu'a elle
            b->select(static_cast<int>(i), true);
            return true;
        }
    return false;
}
std::string AiPreviewDialog::selectedRow() const {
    const auto* b = bodyOf(*this);
    return b && b->selected >= 0 && static_cast<std::size_t>(b->selected) < b->rows.size() ? b->rows[static_cast<std::size_t>(b->selected)].text
                                                                                          : std::string{};
}
std::vector<hmi::Id> AiPreviewDialog::highlighted() const {
    const auto* b = bodyOf(*this);
    return b ? b->highlight : std::vector<hmi::Id>{};
}
bool AiPreviewDialog::clickObject(hmi::Id id) {
    auto* b = bodyOf(*this);
    if (!b) return false;
    for (const auto& o : preview_.view.objects)
        if (o.id == id) {
            b->selectObject(id, o.name);
            return true;
        }
    return false;
}
gfx::Rect AiPreviewDialog::chipRect(bool errors) const {
    const auto* b = bodyOf(*this);
    return b ? b->chipRect(errors) : gfx::Rect{};
}

AiPreviewDialog::AiPreviewDialog(AiPanel& panel, int message)
    : menu::WidgetMenu("dialog.ai.apercu"), panel_(panel), message_(message) {}

menu::MenuTraits AiPreviewDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status AiPreviewDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<AiPreviewBody>(*this);
    auto* b = body.get();
    variable_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".variable")));
    variable_->setVisibility(ui::Visibility::Collapsed);
    links_ += variable_->selectionChanged->connect([this](int i) { if (!syncing_) (void)chooseVariable(i); });
    // 1.22.4 : LA RECHERCHE DE LA LISTE (le champ de l'appli) - elle s'ouvre vide : une autre proposition, une autre liste.
    search_ = &static_cast<ui::SearchField&>(b->addChild(std::make_unique<ui::SearchField>(
        base + ".recherche", "Rechercher : objet, variable, type, erreur\xE2\x80\xA6",
        "Filtrer les lignes de toutes les cat\xC3\xA9gories : chaque mot (tous les mots), sans casse ni accents ; \"une phrase\" ; "
        "-mot : l'exclure. Ctrl+F : ici. \xC3\x89" "chap : tout remontrer.")));
    links_ += search_->changed->connect([this] {
        if (auto* root = dynamic_cast<AiPreviewBody*>(widgetRoot())) root->applyFilter();
    });
    search_->recall();
    search_->setText({});
    edit_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Modifier la demande", base + ".modifier")));
    edit_->setTooltip("Remettre la demande dans la saisie pour la reformuler (cette proposition reste \xC3\xA0 d\xC3\xA9" "cider, sur sa carte).");
    links_ += edit_->clicked->connect([this] { editRequest(); });
    // 1.16.1 : << Refuser... >> (une question d'abord) - plus << Annuler >>, qu'on prenait pour << fermer >>.
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Refuser\xE2\x80\xA6", base + ".refuser")));
    cancel_->setStyle(ui::Button::Style::Flat);
    cancel_->setTooltip("Refuser la proposition (une question d'abord) : le projet ne change pas ; sa carte pourra revenir sur ce refus. "
                        "\xC3\x89" "chap ferme cette fen\xC3\xAAtre SANS d\xC3\xA9" "cider.");
    links_ += cancel_->clicked->connect([this] { cancel(); });
    apply_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Appliquer", base + ".appliquer")));
    apply_->setStyle(ui::Button::Style::Primary);
    apply_->setTooltip("Modifier le projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
    links_ += apply_->clicked->connect([this] { (void)apply(); });
    // 1.22.1 : le menu du clic droit des erreurs (le menu de l'appli), devant le reste.
    fixMenu_ = &static_cast<ui::PopupMenu&>(b->addChild(std::make_unique<ui::PopupMenu>(base + ".corriger")));
    links_ += fixMenu_->itemChosen->connect([this](int id) { runFix(id); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

void AiPreviewDialog::refresh() {
    auto& a = panel_.assistant();
    const Proposal* p = a.proposal(message_);
    if (!p) return;
    // 1.20.1 : le resultat de la verification de la carte (fait sur un fil de fond pour une grosse proposition) - pas une
    // nouvelle validation sur le fil de l'interface. En cours : la fenetre l'attend (Update), l'avancement affiche.
    plan::Plan readyPlan;
    waiting_ = !panel_.chat().resultReady(message_, &check_, &preview_, &readyPlan);
    planReady_ = !waiting_;
    if (waiting_) {
        check_ = Check{};
        preview_ = Preview{};
        preview_.why = "V\xC3\xA9rification de la proposition sur un fil de fond\xE2\x80\xA6 "
                       + std::to_string(static_cast<int>(panel_.chat().checkProgress(message_) * 100.f + 0.5f)) + " %";
    }
    auto* body = dynamic_cast<AiPreviewBody*>(widgetRoot());
    // La liste de gauche.
    std::vector<Row> rows;
    const auto heading = [&](std::string t) { rows.push_back({Row::Kind::Heading, std::move(t), {}, 0}); };
    // 1.22.1 : LES CORRECTIONS FAITES (le clic droit sur une erreur) - ce qui a ete corrige, une ligne chacune ; le clic droit
    // sur l'une d'elles la reprend (Revenir). Puis les erreurs et les avertissements, les doublons regroupes.
    errorLines_ = fixes::errorLines(check_);
    warningLines_ = fixes::warningLines(check_);
    corrections_ = fixes::corrections(*p);
    if (!corrections_.empty()) {
        heading("CORRECTIONS (" + std::to_string(corrections_.size()) + ") \xE2\x80\x94 la proposition seule ; clic droit : revenir");
        rows.push_back({Row::Kind::Note, "Corrig\xC3\xA9 : " + fixes::summary(*p) + ".", {}, 0});
        for (std::size_t k = 0; k < corrections_.size(); ++k) {
            const auto& c = corrections_[k];
            Row r{Row::Kind::Item, c.text, c.state, c.what};
            r.excluded = c.what == 2;
            r.list = 3;
            r.line = k;
            // 1.22.4 : ce qu'elle touche - les objets d'une operation retiree, ceux qui lisent la variable choisie
            if (c.what == 2) {
                r.ref = Row::Ref::Ops;
                r.ops = c.undo.ops;
            } else if (const VariableChoice* ch = choiceFor(*p, c.undo.name)) {
                r.ref = Row::Ref::Uses;
                r.names = {ch->create && trimCopy(ch->use).empty() ? creatableName(c.undo.name) : trimCopy(ch->use)};
            }
            rows.push_back(std::move(r));
        }
    }
    if (!check_.errors.empty()) {
        // 1.16.1 (captures) : avec << Appliquer malgre les erreurs >>, le titre le dit (il disait encore << rien ne s'applique >>)
        const bool anywayTitle = !check_.stale && !check_.viewMissing && a.settings().applyWithErrors;
        heading("ERREURS (" + std::to_string(check_.errors.size()) + ") \xE2\x80\x94 "
                + (anywayTitle ? std::string(check_.errors.size() > 1 ? "laiss\xC3\xA9" "es de c\xC3\xB4t\xC3\xA9 par \xC2\xAB Appliquer malgr\xC3\xA9 \xC2\xBB"
                                                                      : "laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9 par \xC2\xAB Appliquer malgr\xC3\xA9 \xC2\xBB")
                               : std::string("rien ne s'applique tant qu'il y en a")));
        for (std::size_t k = 0; k < errorLines_.size(); ++k) {
            Row r{Row::Kind::Error, errorLines_[k].text, {}, 0};
            r.list = 1;
            r.line = k;
            r.ref = Row::Ref::Message;   // 1.22.4 : ses objets
            r.names = {errorLines_[k].message};
            r.ops = errorLines_[k].ops;
            r.issue = 2;
            rows.push_back(std::move(r));
        }
    }
    if (!check_.warnings.empty()) {
        heading("AVERTISSEMENTS (" + std::to_string(check_.warnings.size()) + ")");
        for (std::size_t k = 0; k < warningLines_.size(); ++k) {
            Row r{Row::Kind::Warning, warningLines_[k].text, {}, 0};
            r.list = 2;
            r.line = k;
            r.ref = Row::Ref::Message;   // 1.22.4 : ses objets
            r.names = {warningLines_[k].message};
            r.ops = warningLines_[k].ops;
            r.issue = 1;
            rows.push_back(std::move(r));
        }
    }
    // 1.21.0 : LES SUPPRESSIONS - chacune : sa strategie, le resume par genre, ce qui part, ce qui en depend, les
    // consequences ; un clic la retire du plan (ou l'y remet). Les suppressions retirees restent listees (a remettre).
    std::size_t removalOps = 0;
    for (std::size_t i = 0; i < p->ops.size(); ++i)
        if (p->ops[i].action == Operation::Action::Remove) ++removalOps;
    if (removalOps > 0 && !waiting_) {
        heading("SUPPRESSIONS (" + std::to_string(removalOps) + ") \xE2\x80\x94 clic sur une suppression : la retirer du plan ou l'y remettre");
        const auto cap = [&](const std::vector<Check::Made>& list, const char* title, std::size_t max) {
            if (list.empty()) return;
            rows.push_back({Row::Kind::Note, title, {}, 0});
            for (std::size_t k = 0; k < list.size() && k < max; ++k)
                rows.push_back({Row::Kind::Change, list[k].what + " " + list[k].name + (list[k].detail.empty() ? std::string{} : " \xE2\x80\x94 " + list[k].detail), {}, 0});
            if (list.size() > max) rows.push_back({Row::Kind::Change, "\xE2\x80\xA6 et " + std::to_string(list.size() - max) + " autres", {}, 0});
        };
        for (std::size_t i = 0; i < p->ops.size(); ++i) {
            if (p->ops[i].action != Operation::Action::Remove) continue;
            const bool excluded = std::find(p->excluded.begin(), p->excluded.end(), i) != p->excluded.end();
            const Check::Removal* r = nullptr;
            for (const auto& x : check_.removals)
                if (x.op == i) r = &x;
            std::string title = r ? r->title : "Supprimer " + [&] {
                std::string names;
                for (const auto& n : p->ops[i].objects) names += (names.empty() ? "" : ", ") + n;
                return names;
            }();
            Row head{Row::Kind::Item, std::move(title), excluded ? std::string("retir\xC3\xA9" "e") : r ? "strat\xC3\xA9gie " + r->strategy : std::string{}, 2};
            head.op = static_cast<int>(i);
            head.excluded = excluded;
            head.ref = Row::Ref::Ops;   // 1.22.4 : ce qui part
            head.ops = {i};
            if (i < check_.outcomes.size()) head.issue = !check_.outcomes[i].errors.empty() ? 2 : !check_.outcomes[i].warnings.empty() ? 1 : 0;
            rows.push_back(std::move(head));
            if (excluded) {
                rows.push_back({Row::Kind::Change, "Retir\xC3\xA9" "e du plan : rien ne part (clic : la remettre).", {}, 0});
                continue;
            }
            if (!r) continue;
            for (const auto& line : r->summary) rows.push_back({Row::Kind::Note, "\xE2\x80\xA2 " + line, {}, 0});
            cap(r->removed, "Part :", 12);
            cap(r->dependencies, "D\xC3\xA9pendances :", 12);
            if (!r->consequences.empty()) {
                rows.push_back({Row::Kind::Note, "Cons\xC3\xA9quences :", {}, 0});
                for (std::size_t k = 0; k < r->consequences.size() && k < 12; ++k) rows.push_back({Row::Kind::Change, r->consequences[k], {}, 0});
                if (r->consequences.size() > 12) rows.push_back({Row::Kind::Change, "\xE2\x80\xA6 et " + std::to_string(r->consequences.size() - 12) + " autres", {}, 0});
            }
            if (!r->keep.empty()) {
                std::string kept;
                for (const auto& k : r->keep) kept += (kept.empty() ? "" : ", ") + k;
                rows.push_back({Row::Kind::Note, "Exclus (gard\xC3\xA9s) : " + kept, {}, 0});
            }
        }
    }
    // 1.19.0 : L'APERCU GLOBAL PAR CATEGORIE - le meme plan que la carte de la conversation (aucune divergence) : une
    // synthese (l'architecture), puis chaque categorie, et pour chaque etape son nom, son action (creation, modification,
    // reutilisation), ce dont elle depend, ses avertissements et ses erreurs.
    std::vector<std::string> planGroups;   // les categories : repliees a la premiere ouverture (la synthese reste ouverte)
    {
        const auto doc = a.host().document ? a.host().document() : nullptr;
        const plan::Plan pl = planReady_ ? readyPlan : waiting_ ? plan::Plan{} : p->state == "proposee" && doc ? plan::build(doc->project, *p, check_) : a.planOf(message_);
        if (pl.worthShowing()) {
            std::map<plan::Category, std::vector<const plan::Step*>> byCat;
            for (const auto& st : pl.steps) byCat[st.category].push_back(&st);
            heading("ARCHITECTURE (" + std::to_string(pl.steps.size()) + " \xC3\xA9tapes, dans l'ordre d'ex\xC3\xA9" "cution)");
            std::size_t create = 0, modify = 0, reuse = 0, excluded = 0;
            for (const auto& st : pl.steps) {
                if (st.excluded) ++excluded;
                else if (st.verb == plan::Verb::Reuse) ++reuse;
                else if (st.verb == plan::Verb::Create) ++create;
                else ++modify;
            }
            rows.push_back({Row::Kind::Note, std::to_string(create) + " cr\xC3\xA9" "ation(s), " + std::to_string(modify) + " modification(s), "
                                                 + std::to_string(reuse) + " r\xC3\xA9utilisation(s)"
                                                 + (excluded ? ", " + std::to_string(excluded) + " retir\xC3\xA9" "e(s)" : std::string{}),
                            {}, 0});
            for (const auto& [cat, steps] : byCat)
                rows.push_back({Row::Kind::Change, std::string(plan::categoryLabel(cat)) + " : " + std::to_string(steps.size()) + " \xC3\xA9tape(s)", {}, 0});
            for (const auto& e : pl.errors) {
                Row r{Row::Kind::Error, e, {}, 0};
                r.ref = Row::Ref::Message;   // 1.22.4
                r.names = {e};
                r.issue = 2;
                rows.push_back(std::move(r));
            }
            for (const auto& [cat, steps] : byCat) {
                if (cat == plan::Category::Removals && removalOps > 0 && !waiting_) continue;   // 1.21.0 : la categorie Suppressions, plus haut
                std::string head = plan::categoryLabel(cat).data();
                for (auto& ch : head) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                heading(head + " (" + std::to_string(steps.size()) + ")");
                planGroups.push_back(head);
                for (const auto* st : steps) {
                    const int what = st->verb == plan::Verb::Create ? 0 : st->verb == plan::Verb::Remove ? 2 : 1;
                    Row item{Row::Kind::Item, st->id + "  " + st->title,
                             st->excluded ? std::string("retir\xC3\xA9" "e") : std::string(plan::verbLabel(st->verb)), what};
                    // 1.22.4 : l'etape designe les objets qu'elle cree ou modifie (sinon ceux qui lisent ses variables, ses
                    // types) ; sa couleur : ses erreurs, ses avertissements.
                    item.ref = Row::Ref::Ops;
                    item.ops = {st->op};
                    for (const auto& c : st->creates) {
                        if (c.kind == "variable") item.names.push_back(c.name);
                        if (c.kind == "type") item.types.push_back(c.name);
                    }
                    item.issue = !st->errors.empty() ? 2 : !st->warnings.empty() ? 1 : 0;
                    rows.push_back(std::move(item));
                    std::string state = "\xC3\x89tat : " + std::string(plan::stateLabel(st->state));
                    if (!st->note.empty()) state += " (" + st->note + ")";
                    rows.push_back({Row::Kind::Change, state, {}, 0});
                    if (!st->deps.empty()) {
                        std::string d = "D\xC3\xA9pend de ";
                        for (std::size_t k = 0; k < st->deps.size(); ++k) d += (k ? ", " : "") + st->deps[k];
                        rows.push_back({Row::Kind::Change, d, {}, 0});
                    }
                    std::string uses;
                    for (const auto& u : st->uses)
                        if (u.exists) uses += (uses.empty() ? "" : ", ") + u.name;
                    if (!uses.empty()) rows.push_back({Row::Kind::Change, "R\xC3\xA9utilise : " + uses, {}, 0});
                    for (const auto& w : st->warnings) {
                        Row r{Row::Kind::Warning, w, {}, 0};
                        r.ref = Row::Ref::Message;   // 1.22.4
                        r.names = {w};
                        r.ops = {st->op};
                        r.issue = 1;
                        rows.push_back(std::move(r));
                    }
                    for (const auto& e : st->errors) {
                        Row r{Row::Kind::Error, e, {}, 0};
                        r.ref = Row::Ref::Message;
                        r.names = {e};
                        r.ops = {st->op};
                        r.issue = 2;
                        rows.push_back(std::move(r));
                    }
                }
            }
        }
    }
    // 1.16.0 : CE QUI SERA CREE en plus des objets - la vue neuve, les variables, les types IHM (jamais en silence).
    if (!check_.newView.empty()) {
        heading("VUE \xC3\x80 CR\xC3\x89" "ER (elle s'ouvre apr\xC3\xA8s Appliquer)");
        std::string size;
        if (preview_.view.width > 0) size = std::to_string(preview_.view.width) + " \xC3\x97 " + std::to_string(preview_.view.height);
        rows.push_back({Row::Kind::Item, check_.newView, size, 0});
        if (!preview_.view.description.empty()) rows.push_back({Row::Kind::Change, preview_.view.description, {}, 0});
    }
    if (!check_.newTypes.empty()) {
        heading("TYPES IHM \xC3\x80 CR\xC3\x89" "ER (" + std::to_string(check_.newTypes.size()) + ")");
        for (const auto& [name, members] : check_.newTypes) {
            Row r{Row::Kind::Item, name, std::to_string(members.size()) + (members.size() > 1 ? " membres" : " membre"), 0};
            r.ref = Row::Ref::Uses;   // 1.22.4 : les objets qui lisent une variable de ce type
            r.types = {name};
            rows.push_back(std::move(r));
            std::size_t shown = 0;
            for (const auto& m : members) {
                if (shown++ >= 8) {
                    rows.push_back({Row::Kind::Change, "\xE2\x80\xA6 et " + std::to_string(members.size() - 8) + (members.size() - 8 > 1 ? " autres" : " autre"), {}, 0});
                    break;
                }
                rows.push_back({Row::Kind::Change, m.name + " : " + m.type + (m.description.empty() ? std::string{} : " \xE2\x80\x94 " + m.description), {}, 0});
            }
        }
    }
    if (!check_.newVariables.empty()) {
        heading("VARIABLES IHM \xC3\x80 CR\xC3\x89" "ER (" + std::to_string(check_.newVariables.size()) + ")");
        for (const auto& v : check_.newVariables) {
            Row r{Row::Kind::Item, v.name, v.type, 0};
            r.ref = Row::Ref::Uses;   // 1.22.4 : les objets qui la lisent
            r.names = {v.name};
            rows.push_back(std::move(r));
            // 1.22.1 : son dossier (donne, repris de l'existant ou deduit : les avertissements disent lequel)
            std::string detail = "Dossier : " + (v.folder.empty() ? std::string("(racine)") : v.folder);
            if (!v.initial.empty()) detail += " \xE2\x80\x94 valeur initiale " + v.initial;
            if (!v.description.empty()) detail += (detail.empty() ? "" : " \xE2\x80\x94 ") + v.description;
            if (!detail.empty()) rows.push_back({Row::Kind::Change, detail, {}, 0});
        }
    }
    // 1.18.0 : CE QUE LES OUTILS CREENT OU TOUCHENT EN PLUS (un symbole, un equipement, une adresse, un script, une alarme,
    // une ressource, un dossier...) : une ligne chacun, son detail dessous ; les detections PROPOSEES (lancees par toi).
    if (!check_.made.empty()) {
        heading("AUSSI CR\xC3\x89\xC3\x89 OU MODIFI\xC3\x89 (" + std::to_string(check_.made.size()) + ")");
        for (const auto& m : check_.made) {
            Row r{Row::Kind::Item, m.name, m.what, 0};
            r.ref = Row::Ref::Uses;   // 1.22.4 : les objets qui le citent
            r.names = {m.name};
            rows.push_back(std::move(r));
            if (!m.detail.empty()) rows.push_back({Row::Kind::Change, m.detail, {}, 0});
        }
    }
    if (!check_.detections.empty()) {
        heading("D\xC3\x89TECTION PROPOS\xC3\x89" "E (lanc\xC3\xA9" "e par toi, apr\xC3\xA8s Appliquer)");
        for (const auto& [eq, range] : check_.detections)
            rows.push_back({Row::Kind::Note, eq + (range.empty() ? std::string{} : " : ce qui r\xC3\xA9pond sur " + range), {}, 0});
    }
    heading("VARIABLES ASSOCI\xC3\x89" "ES");
    if (check_.bindings.empty()) rows.push_back({Row::Kind::Note, "Aucune (l'Assistant n'invente jamais une variable).", {}, 0});
    for (const auto& bnd : check_.bindings) {
        Row r{Row::Kind::Note, bnd, {}, 0};
        r.ref = Row::Ref::Object;   // 1.22.4 : << Pompe_1 -> PUMP_01_RUN >> : l'objet
        r.names = {trimCopy(std::string_view(bnd).substr(0, bnd.find(" \xE2\x86\x92 ")))};
        rows.push_back(std::move(r));
    }
    if (!check_.missingVariable.empty()) {
        Row r{Row::Kind::Note, "Introuvable : " + check_.missingVariable + " \xE2\x80\x94 choisis au-dessus.", {}, 0};
        for (const auto& f : check_.faults)
            if (f.kind == "variable" && f.name == check_.missingVariable && r.names.empty()) {
                r.ref = Row::Ref::Message;   // 1.22.4 : les objets qui la lisent
                r.names = {f.message};
                r.issue = 2;
            }
        rows.push_back(std::move(r));
    }
    heading("OBJETS CONCERN\xC3\x89S (" + std::to_string(check_.items.size()) + ")");
    for (const auto& it : check_.items) {
        const int what = it.what == Item::What::Create ? 0 : it.what == Item::What::Modify ? 1 : 2;
        Row r{Row::Kind::Item, it.name, std::string(hmi::kindLabel(it.kind)), what};
        if (it.view.empty() || it.view == preview_.view.name) {   // 1.22.4 : l'objet (dans la vue montree)
            r.ref = Row::Ref::Object;
            r.names = {it.name};
        }
        rows.push_back(std::move(r));
        std::size_t shown = 0;
        for (const auto& ch : it.changes) {
            if (it.what == Item::What::Create && (ch.key == "x" || ch.key == "y" || ch.key == "w" || ch.key == "h")) continue;
            if (shown++ >= 6) {
                rows.push_back({Row::Kind::Change, "\xE2\x80\xA6", {}, 0});
                break;
            }
            const std::string text = it.what == Item::What::Create ? ch.key + " = " + ch.after
                                                                   : ch.key + " : " + (ch.before.empty() ? std::string("(vide)") : ch.before) + " \xE2\x86\x92 " + ch.after;
            rows.push_back({Row::Kind::Change, text, {}, 0});
        }
    }
    // 1.22.4 : OU EST CE QUE DESIGNE CHAQUE LIGNE, ET OU SONT LES ERREURS - une fois par proposition (jamais a chaque image) :
    // les objets de chaque erreur et de chaque avertissement (leur niveau, leurs messages pour l'infobulle), puis les objets
    // de chaque ligne ; le detail d'une ligne designe ce qu'elle designe ; un titre prend le niveau le plus haut de son groupe.
    std::vector<std::vector<hmi::Id>> targets;
    {
        XPG_MESURE("ia.apercu.reperes");
        const auto doc = a.host().document ? a.host().document() : nullptr;
        const Locator loc(*p, check_, preview_, doc ? &doc->project : nullptr);
        objectIndex_.clear();
        issues_.clear();
        issueTexts_.clear();
        for (const auto& [id, i] : loc.index()) objectIndex_.emplace(id, i);
        std::unordered_map<std::string, int> nameIssue;   // un nom cite par un message (en majuscules) -> son niveau
        const auto note = [&](const fixes::Line& l, int level) {
            std::vector<hmi::Id> ids;
            loc.ofMessage(l.message, l.ops, level == 2, ids);
            const std::string said = (level == 2 ? "Erreur : " : "Avertissement : ") + l.text;
            for (const auto id : ids) {
                int& lv = issues_[id];
                lv = std::max(lv, level);
                issueTexts_[id].push_back(said);
            }
            for (const auto& w : wordsOf(l.message)) {
                int& lv = nameIssue[upperAscii(w)];
                lv = std::max(lv, level);
            }
        };
        for (const auto& l : errorLines_) note(l, 2);
        for (const auto& l : warningLines_) note(l, 1);
        const auto objectIssue = [&](const std::vector<hmi::Id>& ids) {
            int lv = 0;
            for (const auto id : ids)
                if (const auto it = issues_.find(id); it != issues_.end()) lv = std::max(lv, it->second);
            return lv;
        };
        const auto namedIssue = [&](const std::string& n) {
            const auto it = nameIssue.find(upperAscii(n));
            return it == nameIssue.end() ? 0 : it->second;
        };
        int lastItem = -1;
        for (auto& r : rows) {
            if (r.kind == Row::Kind::Heading) {
                lastItem = -1;
                continue;
            }
            std::vector<hmi::Id> ids;
            switch (r.ref) {
                case Row::Ref::Object:
                    if (!r.names.empty()) addUnique(ids, loc.named(r.names.front()));
                    r.issue = std::max(r.issue, objectIssue(ids));
                    break;
                case Row::Ref::Uses:
                    for (const auto& n : r.names) {
                        loc.usingName(n, ids);
                        r.issue = std::max(r.issue, namedIssue(n));
                    }
                    for (const auto& t : r.types) {
                        loc.usingType(t, ids);
                        r.issue = std::max(r.issue, namedIssue(t));
                    }
                    break;
                case Row::Ref::Ops: {
                    bool objects = false;
                    for (const auto op : r.ops) {
                        loc.ofOp(op, ids);
                        objects = objects || (op < p->ops.size() && !projectLevel(p->ops[op]));
                    }
                    if (ids.empty()) {
                        for (const auto& n : r.names) loc.usingName(n, ids);
                        for (const auto& t : r.types) loc.usingType(t, ids);
                    }
                    if (objects) r.issue = std::max(r.issue, objectIssue(ids));
                    break;
                }
                case Row::Ref::Message:
                    if (!r.names.empty()) loc.ofMessage(r.names.front(), r.ops, r.issue == 2, ids);
                    break;
                case Row::Ref::None:
                    break;
            }
            if (!ids.empty()) {
                r.target = static_cast<int>(targets.size());
                targets.push_back(std::move(ids));
            } else if (r.kind != Row::Kind::Item) {
                r.target = lastItem;   // le detail d'une ligne (son etat, ses changements) : ce qu'elle designe
            }
            if (r.kind == Row::Kind::Item) lastItem = r.target;
        }
        Row* head = nullptr;
        for (auto& r : rows) {
            if (r.kind == Row::Kind::Heading) head = &r;
            else if (head && r.issue > head->issue) head->issue = r.issue;
        }
    }
    if (body) {
        // 1.22.4 : la ligne choisie reste choisie (la meme, refaite) ; un objet clique reste encadre s'il est encore la.
        std::string chosen;
        Row::Kind chosenKind = Row::Kind::Note;
        if (body->selected >= 0 && static_cast<std::size_t>(body->selected) < body->rows.size()) {
            chosen = body->rows[static_cast<std::size_t>(body->selected)].text;
            chosenKind = body->rows[static_cast<std::size_t>(body->selected)].kind;
        }
        const bool objectOnly = body->selected < 0;
        std::vector<hmi::Id> kept;
        for (const auto id : body->highlight)
            if (objectIndex_.count(id)) kept.push_back(id);
        body->rows = std::move(rows);
        body->targets = std::move(targets);
        body->selected = -1;
        body->highlight.clear();
        if (!chosen.empty()) {
            for (std::size_t i = 0; i < body->rows.size(); ++i)
                if (body->rows[i].kind == chosenKind && body->rows[i].text == chosen) {
                    body->selected = static_cast<int>(i);
                    if (body->rows[i].target >= 0) body->highlight = body->targets[static_cast<std::size_t>(body->rows[i].target)];
                    break;
                }
        } else if (objectOnly) {
            body->highlight = std::move(kept);
        }
        body->applyFilter();
    }
    if (body && !planFolded_ && !planGroups.empty()) {
        for (const auto& g : planGroups) body->folded.insert(g);
        planFolded_ = true;
    }
    // Le choix de la variable (introuvable) : les plus proches d'abord, puis les autres, puis Creer.
    // 1.22.1 : il vise UNE variable introuvable - celle qu'on a demandee (Choisir une variable... au clic droit), sinon la
    // premiere encore introuvable ; une variable deja choisie reste montree (son choix se lit) tant qu'aucune autre n'attend.
    std::vector<const Check::Fault*> missingFaults;
    for (const auto& f : check_.faults)
        if (f.kind == "variable"
            && std::none_of(missingFaults.begin(), missingFaults.end(), [&](const Check::Fault* x) { return x->name == f.name; }))
            missingFaults.push_back(&f);
    const auto stillMissing = [&](const std::string& n) {
        return std::any_of(missingFaults.begin(), missingFaults.end(), [&](const Check::Fault* x) { return x->name == n; });
    };
    if (!waiting_) {
        const bool chosenHere = !missingName_.empty() && choiceFor(*p, missingName_) != nullptr;
        if (pinned_ && !missingName_.empty() && (stillMissing(missingName_) || chosenHere)) {
            // demandee par l'utilisateur : elle reste visee
        } else if (!missingName_.empty() && stillMissing(missingName_)) {
            pinned_ = false;
        } else if (!missingFaults.empty()) {
            missingName_ = missingFaults.front()->name;
            pinned_ = false;
        } else if (!chosenHere) {
            missingName_.clear();
            pinned_ = false;
            // le choix unique d'avant (un fichier d'avant 1.22.1) : la variable de l'association, comme avant
            if (!p->variable.use.empty() || p->variable.create)
                for (const auto& op : p->ops)
                    if (op.action == Operation::Action::Bind) missingName_ = op.variable;
        }
        otherMissing_ = 0;
        for (const auto* f : missingFaults)
            if (f->name != missingName_) ++otherMissing_;
    }
    const bool needChoice = !missingName_.empty();
    if (variable_) {
        syncing_ = true;
        if (needChoice) {
            const std::string missing = missingName_;
            const Check::Fault* fault = nullptr;
            for (const auto* f : missingFaults)
                if (f->name == missing) fault = f;
            createType_ = fault && !fault->type.empty() ? fault->type : std::string("BOOL");
            if (const VariableChoice* ch = choiceFor(*p, missing); ch && ch->create && !ch->type.empty()) createType_ = ch->type;
            variableNames_.clear();
            std::vector<ui::DropDown::Item> items;
            std::set<std::string> seen;
            items.push_back({"Choisir une variable\xE2\x80\xA6", "?"});   // l'invite : rien n'est choisi a la place de l'utilisateur
            variableNames_.push_back("?");
            const auto doc = a.host().document ? a.host().document() : nullptr;
            if (doc) {
                // 1.22.1 : les plus proches que la validation a deja trouvees (rien a recalculer ici), sinon comme avant.
                for (const auto& n : fault && !fault->closest.empty() ? fault->closest : nearVariables(doc->project, missing)) {
                    items.push_back({n + "  (proche)", n});
                    variableNames_.push_back(n);
                    seen.insert(n);
                }
                std::vector<std::string> all;
                for (const auto& v : doc->project.programs.variables)
                    if (!seen.count(v.name)) all.push_back(v.name);
                std::sort(all.begin(), all.end());
                for (const auto& n : all) {
                    items.push_back({n, n});
                    variableNames_.push_back(n);
                }
            }
            // 1.22.1 : un membre (Vanne.Cmd) se cree sous un nom d'identifiant (Vanne_Cmd) ; le type conseille.
            items.push_back({"Cr\xC3\xA9" "er \xC2\xAB " + creatableName(missing) + " \xC2\xBB (" + createType_ + ") dans les variables de l'IHM", "+"});
            variableNames_.push_back({});
            variable_->setItems(std::move(items));
            const VariableChoice* ch = choiceFor(*p, missing);
            if (!ch && (!p->variable.use.empty() || p->variable.create)) ch = &p->variable;
            int sel = 0;
            if (ch && ch->create) sel = static_cast<int>(variableNames_.size()) - 1;
            for (std::size_t i = 1; i + 1 < variableNames_.size(); ++i)
                if (ch && !ch->create && !ch->use.empty() && variableNames_[i] == ch->use) sel = static_cast<int>(i);
            variable_->setSelectedIndex(sel);
            variable_->setVisibility(ui::Visibility::Visible);
        } else {
            variable_->setVisibility(ui::Visibility::Collapsed);
        }
        syncing_ = false;
    }
    // 1.16.1 : << Appliquer malgre les erreurs >> (l'option des parametres) - le bouton le dit, la confirmation les liste.
    const bool anyway = !check_.ok() && !check_.stale && !check_.viewMissing && a.settings().applyWithErrors;
    if (apply_) {
        const std::size_t n = check_.errors.size();
        apply_->setText(anyway ? "Appliquer malgr\xC3\xA9 " + std::to_string(n) + (n > 1 ? " erreurs\xE2\x80\xA6" : " erreur\xE2\x80\xA6")
                               : std::string("Appliquer"));
        apply_->setTooltip(anyway ? "Ce qui est valide s'applique ; ce qui est en erreur est laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 et not\xC3\xA9 dans le journal "
                                    "(une confirmation les liste d'abord). Un seul Ctrl+Z reprend tout."
                                  : "Modifier le projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
        apply_->setEnabled(p->state == "proposee" && (check_.ok() || anyway) && !waiting_);   // 1.20.1 : apres la verification de fond
    }
    if (cancel_) cancel_->setEnabled(p->state == "proposee");
    key_ = proposalKey(*p);
    if (widgetRoot()) widgetRoot()->invalidateLayout();
}

std::vector<std::string> AiPreviewDialog::variableItems() const {
    std::vector<std::string> out;
    if (variable_)
        for (const auto& it : variable_->items()) out.push_back(it.label);
    return out;
}

bool AiPreviewDialog::chooseVariable(int index) {
    if (index <= 0 || index >= static_cast<int>(variableNames_.size())) return false;   // 0 : l'invite
    if (missingName_.empty()) return false;
    VariableChoice choice;
    if (variableNames_[static_cast<std::size_t>(index)].empty()) {
        choice.create = true;
        choice.type = createType_;
    } else {
        choice.use = variableNames_[static_cast<std::size_t>(index)];
    }
    // 1.22.1 : le choix vaut pour CETTE variable introuvable (toutes les operations qui la citent) - les autres gardent leur
    // erreur jusqu'a leur propre choix (avant : un seul choix pour toutes, Proposal::variable).
    pinned_ = false;
    const bool ok = panel_.assistant().chooseVariable(message_, missingName_, choice);
    panel_.sync();
    refresh();
    return ok;
}

bool AiPreviewDialog::excludeRemoval(std::size_t op, bool excluded) {
    const Proposal* p = panel_.assistant().proposal(message_);
    if (!p || p->state != "proposee" || op >= p->ops.size() || p->ops[op].action != Operation::Action::Remove) return false;
    const bool ok = panel_.assistant().setExcluded(message_, op, excluded);
    panel_.sync();
    refresh();
    return ok;
}

std::vector<std::string> AiPreviewDialog::removalRows() const {
    std::vector<std::string> out;
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    if (!b) return out;
    bool inside = false;
    for (const auto& r : b->rows) {
        if (r.kind == Row::Kind::Heading) {
            inside = groupKey(r.text) == "SUPPRESSIONS";
            continue;
        }
        if (inside) out.push_back(r.text + (r.right.empty() ? std::string{} : " | " + r.right));
    }
    return out;
}

// ---- 1.22.1 : le clic droit sur une erreur (AiFixes) -------------------------------------------------------------------
std::vector<std::string> AiPreviewDialog::errorRows() const {
    std::vector<std::string> out;
    for (const auto& l : errorLines_) out.push_back(l.text);
    return out;
}

std::vector<std::string> AiPreviewDialog::warningRows() const {
    std::vector<std::string> out;
    for (const auto& l : warningLines_) out.push_back(l.text);
    return out;
}

std::vector<std::string> AiPreviewDialog::correctionRows() const {
    std::vector<std::string> out;
    for (const auto& c : corrections_) out.push_back(c.text + " | " + c.state);
    return out;
}

bool AiPreviewDialog::openFixMenu(int list, std::size_t line, gfx::Point at) {
    const Proposal* p = panel_.assistant().proposal(message_);
    auto* root = widgetRoot();
    if (!p || !fixMenu_ || !root || done_ || waiting_) return false;
    fixes::Menu m;
    if (list == 1 && line < errorLines_.size()) {
        const auto doc = panel_.assistant().host().document ? panel_.assistant().host().document() : nullptr;
        m = fixes::menuFor(*p, check_, errorLines_[line], doc ? &doc->project : nullptr);
    } else if (list == 2 && line < warningLines_.size()) {
        m = fixes::menuFor(*p, check_, warningLines_[line], nullptr);
    } else if (list == 3 && line < corrections_.size()) {
        m = fixes::menuFor(*p, corrections_[line]);
    } else {
        return false;
    }
    fixes::fill(*fixMenu_, m, fixActions_);
    fixMenu_->setBounds(root->bounds());
    gfx::Size surface = ui::surfaceSize();
    if (surface.w <= 0.f || surface.h <= 0.f) surface = {root->bounds().right() + 400.f, root->bounds().bottom() + 400.f};
    fixMenu_->openAt(at, surface);
    return true;
}

namespace {
// Le point ou s'ouvre le menu d'une ligne demande par un essai : la ligne si elle est montree, sinon le milieu de la liste.
gfx::Point menuPoint(gfx::Rect row, gfx::Rect list) {
    if (row.w > 0.f) return {row.x + 40.f, row.y + row.h / 2.f};
    return {list.x + list.w / 2.f, list.y + list.h / 2.f};
}
} // namespace

bool AiPreviewDialog::openErrorMenu(std::size_t line) {
    const auto* b = dynamic_cast<const AiPreviewBody*>(widgetRoot());
    return b && openFixMenu(1, line, menuPoint(b->rowRect(1, line), b->listRect()));
}

bool AiPreviewDialog::openWarningMenu(std::size_t line) {
    const auto* b = dynamic_cast<const AiPreviewBody*>(widgetRoot());
    return b && openFixMenu(2, line, menuPoint(b->rowRect(2, line), b->listRect()));
}

bool AiPreviewDialog::openCorrectionMenu(std::size_t line) {
    const auto* b = dynamic_cast<const AiPreviewBody*>(widgetRoot());
    return b && openFixMenu(3, line, menuPoint(b->rowRect(3, line), b->listRect()));
}

bool AiPreviewDialog::menuOpen() const { return fixMenu_ && fixMenu_->isOpen(); }

std::vector<std::string> AiPreviewDialog::menuItems() const {
    return fixMenu_ && fixMenu_->isOpen() ? fixes::labels(*fixMenu_) : std::vector<std::string>{};
}

bool AiPreviewDialog::chooseMenuItem(const std::string& label) {
    if (!fixMenu_ || !fixMenu_->isOpen()) return false;
    const int id = fixes::idOf(*fixMenu_, label);
    if (id < 0) return false;
    fixMenu_->close();
    fixMenu_->itemChosen->emit(id);   // le meme chemin qu'un clic
    return true;
}

void AiPreviewDialog::focusVariable(const std::string& name, bool openList) {
    if (name.empty()) return;
    missingName_ = name;
    pinned_ = true;
    openChoice_ = openList;
    if (widgetRoot()) refresh();
}

void AiPreviewDialog::runFix(int id) {
    if (id < 0 || static_cast<std::size_t>(id) >= fixActions_.size() || done_) return;
    const fixes::Action act = fixActions_[static_cast<std::size_t>(id)];
    using K = fixes::Action::Kind;
    switch (act.kind) {
        case K::Copy:
            ui::setClipboardText(act.value);
            panel_.notice("Message copi\xC3\xA9.");
            return;
        case K::AskAi:
            // Le texte va dans la saisie du panneau (rien ne part seul) : la fenetre se ferme pour qu'on le relise.
            panel_.askFix(act.value);
            finish(false);
            return;
        case K::Pick:
            // Le choix du haut vise cette variable ; sa liste s'ouvre au prochain Update (le menu s'est referme).
            focusVariable(act.name, true);
            return;
        default: {
            std::string said;
            (void)fixes::apply(panel_.assistant(), message_, act, &said);
            panel_.notice(said);
            panel_.sync();
            refresh();
            return;
        }
    }
}

void AiPreviewDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    const Proposal* p = panel_.assistant().proposal(message_);
    // Appliquee (apres la confirmation d'une suppression), refusee, ou partie : la fenetre se ferme.
    if (!p || p->state != "proposee") {
        finish(p && p->state == "appliquee");
        return;
    }
    // 1.22.1 : << Choisir une variable... >> (le clic droit) - la liste du haut s'ouvre, sur la variable demandee.
    if (openChoice_ && variable_ && variable_->visibility() == ui::Visibility::Visible) {
        openChoice_ = false;
        if (auto* root = widgetRoot()) root->layout();
        const gfx::Rect at = variable_->bounds();
        if (at.w > 0.f && !variable_->isOpen())
            (void)variable_->dispatch(ui::MouseDown{{at.x + at.w / 2.f, at.y + at.h / 2.f}, ui::MouseButton::Left, 1, {}});
    }
    const std::string key = proposalKey(*p);
    if (key != key_) refresh();
    // 1.20.1 : la verification de fond finie : la fenetre se remplit ; en cours : l'avancement (quatre fois par seconde).
    else if (waiting_ && (!panel_.chat().checking(message_) || f.totalSeconds - lastWait_ >= 0.25 || f.totalSeconds < lastWait_)) {
        lastWait_ = f.totalSeconds;
        refresh();
    }
}

bool AiPreviewDialog::apply() {
    if (done_ || waiting_) return false;   // 1.20.1 : pas avant la fin de la verification de fond
    const bool ok = panel_.applyProposal(message_);   // une suppression multiple : la confirmation d'abord
    if (ok) finish(true);
    else refresh();
    return ok;
}

void AiPreviewDialog::cancel() {
    // 1.16.1 : la question d'abord ; refusee, la fenetre se ferme seule (Update voit l'etat changer).
    panel_.askRefuse(message_);
}

void AiPreviewDialog::editRequest() {
    panel_.editRequest(message_);
    finish(false);
}

void AiPreviewDialog::close() { finish(false); }

void AiPreviewDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

ui::EventResult AiPreviewDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev);
        k && k->key == ui::Key::Escape && !(variable_ && variable_->isOpen()) && !(fixMenu_ && fixMenu_->isOpen())) {
        // 1.22.4 : une recherche tapee, une puce choisie : Echap remontre tout (la fenetre reste) ; sinon elle se ferme.
        if (auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot()); b && b->filtering) {
            b->onlyErrors = b->onlyWarnings = false;
            if (search_ && !search_->text().empty()) search_->setText({});
            b->applyFilter();
            return ui::EventResult::Consumed;
        }
        close();
        return ui::EventResult::Consumed;
    }
    // 1.22.4 : Ctrl+F met le clavier dans la recherche (la liste masquee se montre).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F && k->mods.ctrl && !k->mods.alt && !k->mods.shift && search_) {
        setListHidden(false);
        if (auto* root = widgetRoot()) root->layout();
        search_->field().takeFocus();
        return ui::EventResult::Consumed;
    }
    // 1.16.1 : Ctrl+plus, Ctrl+moins, Ctrl+0 zooment l'apercu (pas l'appli derriere).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (const int step = ui::zoomKeyStep(*k); step != ui::kNotZoomKey) {
            setPreviewZoom(step == 0 ? 1.f : previewZoom() * (step > 0 ? 1.25f : 0.8f));
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
