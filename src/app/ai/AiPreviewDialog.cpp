// =============================================================================
//  app/ai/AiPreviewDialog.cpp - voir AiPreviewDialog.hpp
// =============================================================================
#include "AiPreviewDialog.hpp"

#include "AiDelete.hpp"   // 1.22.6 : une suppression du projet (elle peut changer des objets d'autres vues)
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
#include <atomic>
#include <cmath>
#include <functional>
#include <optional>
#include <thread>
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

// 1.22.5 : une ligne de la verification qui vient de l'avis du logiciel (build::merge : << Generer : ... >>, << Compiler : ... >>).
bool fromBuild(std::string_view message) {
    return message.rfind("G\xC3\xA9n\xC3\xA9rer : ", 0) == 0 || message.rfind("Compiler : ", 0) == 0;
}
// La ligne d'un constat dans la liste : la ligne versee dans la verification, et la correction du logiciel s'il en a une.
std::string buildRowText(const build::Finding& f) {
    std::string t = build::lineOf(f);
    if (!f.fixFrom.empty()) t += " (correction du logiciel : \xC2\xAB " + f.fixFrom + " \xC2\xBB \xE2\x86\x92 \xC2\xAB " + f.fixTo + " \xC2\xBB)";
    return t;
}
// Le constat comme une ligne de la verification (les cadres, le menu du clic droit). 1.22.7 : `rank` - son rang dans le verdict
// (le menu offre alors sa correction, et Corriger rapidement la fait).
fixes::Line buildLine(const build::Finding& f, const Check& ck, std::size_t ops, std::size_t rank = fixes::Line::kNone) {
    fixes::Line l;
    l.message = build::lineOf(f);
    l.text = buildRowText(f);
    l.error = f.error;
    if (f.op != Check::Fault::kNoOp && f.op < ops) l.ops = {f.op};
    for (std::size_t k = 0; k < ck.faults.size(); ++k)
        if (ck.faults[k].message == l.message) l.faults.push_back(k);
    l.finding = rank;
    if (rank != fixes::Line::kNone) {
        l.fixFrom = f.fixFrom;
        l.fixTo = f.fixTo;
    }
    return l;
}

// 1.22.7 : une ligne en quelques mots (<< 2 restent : ... >>) - sa variable introuvable, sinon le debut de son texte.
std::string shortOf(const fixes::Line& l, const Check& ck) {
    for (const auto k : l.faults)
        if (k < ck.faults.size() && ck.faults[k].kind == "variable") return "\xC2\xAB " + ck.faults[k].name + " \xC2\xBB";
    if (l.message.size() <= 44) return l.message;
    std::size_t cut = l.message.rfind(' ', 44);
    if (cut == std::string::npos || cut < 12) cut = 44;
    while (cut > 0 && (static_cast<unsigned char>(l.message[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'une lettre
    return l.message.substr(0, cut) + "\xE2\x80\xA6";
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
    // 1.22.6 : TOUTES LES VUES - la vue que la ligne nomme (un objet : la sienne ; une vue a creer : elle-meme ; vide : celle
    // de ce qu'elle designe) ; les vues ou sont ses objets (leurs rangs dans la barre des vues, calcules avec `target`) ; un
    // titre : son texte tel que refresh l'a fait (le compte << sur cette vue >> s'y ajoute selon la vue montree).
    std::string              view{};
    std::vector<std::size_t> views{};
    std::string              base{};
    // 1.22.7 : un titre qui offre << Tout corriger rapidement >> (1 ERREURS, 2 AVERTISSEMENTS, 4 GENERER ET COMPILER ; 0 : aucun) ;
    // une ligne de CORRECTIONS : la cle de sa correction (la mise en evidence).
    int                      group{0};
    std::string              key{};
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

// 1.22.6 : UN OBJET D'UNE VUE DE LA PROPOSITION - le rang de sa vue (dans la barre des vues) et son id.
struct Place {
    std::size_t view{0};
    hmi::Id     id{hmi::kNoId};
    bool operator==(const Place&) const = default;
};
void addUnique(std::vector<Place>& to, const Place& at) {
    if (at.id != hmi::kNoId && std::find(to.begin(), to.end(), at) == to.end()) to.push_back(at);
}

// OU EST CE QUE DESIGNE UNE LIGNE, dans l'apercu - construit une fois par proposition (refresh) : les objets des vues
// montrees (1.22.6 : TOUTES les vues de la proposition, dans l'ordre de la barre ; par nom, par vue et id), les noms que
// chacun lit (sa variable, ses formules, ses textes a trous, ses actions), le type de chaque variable (du projet, creee
// par la proposition, parametre d'une vue), les objets de chaque operation et de chaque message (Check::spots, qui disent
// leur vue). Un nom sans vue dite : la premiere vue qui l'a (la principale d'abord).
class Locator {
public:
    Locator(const Proposal& p, const Check& ck, const std::vector<const hmi::View*>& views, const hmi::Project* project)
        : p_(p), ck_(ck), views_(views) {
        const auto typed = [&](const std::string& name, const std::string& type) {
            known_.insert(rootKey(name));
            for (const auto& w : wordsOf(type)) ofType_[upperAscii(w)].push_back(rootKey(name));
        };
        for (std::size_t k = 0; k < views.size(); ++k) {
            const hmi::View* v = views[k];
            if (!v) continue;
            rank_.emplace(v->name, k);
            for (std::size_t i = 0; i < v->objects.size(); ++i) {
                const auto& o = v->objects[i];
                const Place at{k, o.id};
                index_.emplace(std::make_pair(k, o.id), i);
                byName_[o.name].push_back(at);
                // Ce que l'objet lit : chaque texte ou un nom se lit (hmi::rewriteNames, sur une copie : rien ne change).
                hmi::Object copy = o;
                std::unordered_set<std::string> roots;
                hmi::rewriteNames(copy, [&](std::string_view text, bool) {
                    rootsOf(text, roots);
                    return std::string(text);
                });
                for (const auto& r : roots) readers_[r].push_back(at);
            }
            for (const auto& prm : v->params) typed(prm.name, prm.type);
        }
        if (project)
            for (const auto& v : project->programs.variables) typed(v.name, v.type);
        for (const auto& v : ck.newVariables) typed(v.name, v.type);
        for (std::size_t k = 0; k < ck.spots.size(); ++k) {
            const auto& s = ck.spots[k];
            if (s.message.empty()) byOp_.emplace(s.op, k);
            else byMessage_.emplace(s.message, k);
        }
    }

    // Le rang de la vue de ce nom (aucun : elle n'est pas dessinee).
    [[nodiscard]] std::optional<std::size_t> rankOf(const std::string& view) const {
        const auto it = rank_.find(view);
        return it == rank_.end() ? std::nullopt : std::optional<std::size_t>(it->second);
    }
    [[nodiscard]] Place resolve(const Check::Spot& s) const {
        if (s.view.empty()) return named(s.object);
        const auto k = rankOf(s.view);
        if (!k) return {};
        if (const auto it = index_.find(std::make_pair(*k, s.id)); it != index_.end() && views_[*k]->objects[it->second].name == s.object)
            return {*k, s.id};
        return namedIn(*k, s.object);
    }
    [[nodiscard]] Place named(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() || it->second.empty() ? Place{} : it->second.front();
    }
    [[nodiscard]] Place namedIn(std::size_t view, const std::string& name) const {
        if (const auto it = byName_.find(name); it != byName_.end())
            for (const auto& at : it->second)
                if (at.view == view) return at;
        return {};
    }
    // Un objet nomme sur sa vue (`view` : son nom ; vide : la premiere vue qui l'a ; une vue pas dessinee : aucun - un
    // homonyme d'une autre vue n'est pas lui).
    [[nodiscard]] Place object(const std::string& name, const std::string& view) const {
        if (view.empty()) return named(name);
        const auto k = rankOf(view);
        return k ? namedIn(*k, name) : Place{};
    }
    // Les objets qui lisent ce nom (une variable : Armoire1, Armoire1.Porte, Armoires[0].actif).
    void usingName(const std::string& name, std::vector<Place>& out) const {
        if (const auto it = readers_.find(rootKey(name)); it != readers_.end())
            for (const auto& at : it->second) addUnique(out, at);
    }
    // Les objets qui lisent une variable de ce type (ou un tableau de ce type), ou le type lui-meme.
    void usingType(const std::string& type, std::vector<Place>& out) const {
        const std::string key = upperAscii(type);
        if (const auto it = ofType_.find(key); it != ofType_.end())
            for (const auto& v : it->second) usingName(v, out);
        usingName(type, out);
    }
    // Les objets qu'une operation cree, modifie ou supprime ; sinon (ignoree, retiree) ceux qu'elle nomme, sur sa vue.
    void ofOp(std::size_t op, std::vector<Place>& out) const {
        const std::size_t before = out.size();
        const auto [b, e] = byOp_.equal_range(op);
        for (auto it = b; it != e; ++it) addUnique(out, resolve(ck_.spots[it->second]));
        if (out.size() > before || op >= p_.ops.size() || projectLevel(p_.ops[op])) return;
        const std::string target = opTarget(p_, op);
        const std::string view = target.empty() ? p_.view : target;
        addUnique(out, object(p_.ops[op].name, view));
        for (const auto& n : p_.ops[op].objects) addUnique(out, object(n, view));
    }
    // Les objets d'un message : ceux que la validation a notes ; sinon ceux qu'il nomme ; une erreur : sinon ceux qui lisent
    // une variable qu'elle nomme (du projet, de la proposition, un parametre d'une vue - pas un avertissement : << sera creee
    // dans le dossier... >> n'est pas un defaut des objets qui la lisent) ; sinon ceux de ses operations.
    void ofMessage(const std::string& message, const std::vector<std::size_t>& ops, bool error, std::vector<Place>& out) const {
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
    const std::vector<const hmi::View*>&                      views_;
    std::map<std::pair<std::size_t, hmi::Id>, std::size_t>    index_;
    std::unordered_map<std::string, std::size_t>              rank_;
    std::unordered_map<std::string, std::vector<Place>>       byName_;
    std::unordered_map<std::string, std::vector<Place>>       readers_;
    std::unordered_map<std::string, std::vector<std::string>> ofType_;
    std::unordered_set<std::string>                           known_;   // les variables (en majuscules)
    std::multimap<std::size_t, std::size_t>                   byOp_;
    std::multimap<std::string, std::size_t>                   byMessage_;
};

// 1.22.6 : le role d'une vue, dit dans la barre (vide : une vue ordinaire).
std::string roleLabel(const std::string& role) {
    if (role == "popup") return "popup";
    if (role == "symbole") return "symbole";
    if (role == "modele") return "\xC3\xA9" "cran mod\xC3\xA8le";
    if (role == "entete") return "en-t\xC3\xAA" "te";
    if (role == "pied") return "pied de page";
    return {};
}
// << 3 >> + << erreur >> : << 3 erreurs >>.
std::string plural(std::size_t n, const char* word) { return std::to_string(n) + " " + word + (n > 1 ? "s" : ""); }

// 1.22.6 : une seule vue (la plupart des propositions) - les objets ne visent qu'une vue, aucune suppression du projet (elle
// peut changer des objets ailleurs) : l'apercu de la carte suffit, rien a recalculer.
bool oneView(const Proposal& p) {
    std::optional<std::string> only;
    for (std::size_t i = 0; i < p.ops.size(); ++i) {
        const Operation& op = p.ops[i];
        if (del::projectRemoval(op)) return false;
        std::string name;
        if (op.action == Operation::Action::View) name = trimCopy(op.name);
        else if (!projectLevel(op)) name = opTarget(p, i);   // vide : la vue ouverte
        else continue;
        if (!only) only = name;
        else if (*only != name) return false;
    }
    return true;
}

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
    std::vector<std::vector<Place>>   targets;            // 1.22.6 : sur toutes les vues (le rang de la vue, l'id)
    bool                              onlyErrors{false}, onlyWarnings{false};
    bool                              onlyView{false};    // 1.22.6 : la puce << Cette vue seulement >>
    bool                              filtering{false};
    std::vector<char>                 pass;
    std::size_t                       matches{0};
    int                               selected{-1};
    std::vector<hmi::Id>              highlight;
    double                            pulseStart{-1.0};
    int                               scrollToRow{-1};
    // 1.22.7 : les lignes de CORRECTIONS que la derniere correction vient d'ajouter - mises en evidence un instant (un fond qui
    // s'eteint en une seconde et demie a partir de leur premier dessin).
    std::vector<int>                  flashed;
    double                            flashStart{-1.0};

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

    // ---- 1.22.6 : TOUTES LES VUES ----------------------------------------------------------------------------------------
    [[nodiscard]] bool multiView() const { return d_.viewCount() > 1; }
    // La ligne designe des objets de la vue montree ; la vue qu'elle dit (son nom, << 2 vues >> ; vide : aucune) ; les noms
    // de ses vues (la recherche les lit).
    [[nodiscard]] bool onShown(const Row& r) const { return std::find(r.views.begin(), r.views.end(), d_.shown_) != r.views.end(); }
    [[nodiscard]] std::string viewTag(const Row& r) const {
        if (r.views.empty()) return {};
        if (r.views.size() == 1) return d_.viewName(r.views.front());
        return std::to_string(r.views.size()) + " vues";
    }
    [[nodiscard]] std::string viewNames(const Row& r) const {
        std::string out;
        for (const auto k : r.views) out += (out.empty() ? "" : ", ") + d_.viewName(k);
        return out;
    }
    // Les objets de la ligne sur la vue montree (ce que son cadre entoure).
    [[nodiscard]] std::vector<hmi::Id> shownTargets(const Row& r) const {
        std::vector<hmi::Id> out;
        if (r.target >= 0 && static_cast<std::size_t>(r.target) < targets.size())
            for (const auto& at : targets[static_cast<std::size_t>(r.target)])
                if (at.view == d_.shown_) addUnique(out, at.id);
        return out;
    }
    // Les titres : << OBJETS CONCERNES (12, 3 sur cette vue) >> - le compte des lignes du groupe qui designent des objets de la
    // vue montree : ses objets, ses etapes (un groupe qui en a), sinon ses messages (plusieurs vues seulement ; un titre sans
    // compte reste tel quel).
    void relabel() {
        const bool multi = multiView();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            Row& h = rows[i];
            if (h.kind != Row::Kind::Heading) continue;
            h.text = h.base;
            if (!multi) continue;
            std::size_t end = i + 1;
            bool items = false;
            for (; end < rows.size() && rows[end].kind != Row::Kind::Heading; ++end) items = items || rows[end].kind == Row::Kind::Item;
            std::size_t total = 0, here = 0;
            for (std::size_t j = i + 1; j < end; ++j) {
                const Row& r = rows[j];
                if (r.kind == Row::Kind::Change || r.views.empty() || (items && r.kind != Row::Kind::Item)) continue;
                ++total;
                if (onShown(r)) ++here;
            }
            const std::size_t open = h.base.find(" (");
            if (total == 0 || open == std::string::npos) continue;
            std::size_t e = open + 2;
            while (e < h.base.size() && std::isdigit(static_cast<unsigned char>(h.base[e]))) ++e;
            if (e == open + 2 || e >= h.base.size() || h.base[e] != ')') continue;
            h.text = h.base.substr(0, e) + ", " + std::to_string(here) + " sur cette vue" + h.base.substr(e);
        }
    }
    // La vue montree a change : la vue entiere (Ajuster), ce que la ligne choisie encadre sur elle, les titres, le filtre.
    void shownViewChanged() {
        zoom = 1.f;
        pan = {0.f, 0.f};
        highlight = selected >= 0 && static_cast<std::size_t>(selected) < rows.size() ? shownTargets(rows[static_cast<std::size_t>(selected)])
                                                                                      : std::vector<hmi::Id>{};
        pulseStart = -1.0;
        relabel();
        if (filtering) applyFilter();
        invalidateLayout();
        invalidate();
    }

    // 1.22.4 : LE FILTRE - la recherche (le champ de l'appli : les mots ET, sans casse ni accents) et les puces (les erreurs,
    // les avertissements, ou les deux). Une ligne repond : elle se montre, son titre aussi ; le detail d'une ligne qui repond
    // (son etat, ses changements) se montre avec elle. Rien de tape, aucune puce : la liste comme avant (les groupes replies).
    void applyFilter() {
        const ui::SearchQuery* q = d_.search_ ? &d_.search_->query() : nullptr;
        const bool text = q && !q->empty();
        filtering = text || onlyErrors || onlyWarnings || onlyView;
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
                const bool chip = ((!onlyErrors && !onlyWarnings) || (onlyErrors && r.issue == 2) || (onlyWarnings && r.issue == 1))
                                  && (!onlyView || onShown(r));   // 1.22.6 : les lignes de la vue montree
                const std::string views = multiView() ? viewNames(r) : std::string{};   // 1.22.6 : la recherche lit leur vue
                if (chip && (!text || q->matches({std::string_view(r.text), std::string_view(r.right), std::string_view(views)}))) {
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
    // (et zoome si besoin) ; depuis l'apercu, la liste la fait voir (son groupe se deplie). 1.22.6 : ce qu'elle designe est sur
    // une AUTRE vue (rien sur la vue montree) : l'apercu passe d'abord sur cette vue.
    void select(int k, bool fromList) {
        selected = k >= 0 && static_cast<std::size_t>(k) < rows.size() ? k : -1;
        highlight.clear();
        if (selected >= 0) {
            const Row& r = rows[static_cast<std::size_t>(selected)];
            if (fromList && !r.views.empty() && !onShown(r)) {
                std::size_t to = r.views.front();
                if (r.target >= 0 && static_cast<std::size_t>(r.target) < targets.size() && !targets[static_cast<std::size_t>(r.target)].empty())
                    to = targets[static_cast<std::size_t>(r.target)].front().view;
                (void)d_.showView(to);
            }
            highlight = shownTargets(r);
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
            if (std::find(t.begin(), t.end(), Place{d_.shown_, id}) == t.end()) continue;   // 1.22.6 : sur la vue montree
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
            if (it.name != o.name || (!it.view.empty() && it.view != pv.view.name)) continue;   // 1.22.6 : celui de cette vue
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
                if (std::find(t.begin(), t.end(), Place{d_.shown_, id}) != t.end()) return true;
            }
        return false;
    }
    // 1.22.4 : << Erreurs (3) >>, << Avertissements (12) >> - le compte de la liste (ses titres le disent aussi).
    [[nodiscard]] std::string chipLabel(bool errors) const {
        return errors ? "Erreurs (" + std::to_string(d_.check_.errors.size()) + ")" : "Avertissements (" + std::to_string(d_.check_.warnings.size()) + ")";
    }
    [[nodiscard]] gfx::Rect chipRect(bool errors) const noexcept { return errors ? chipErr_ : chipWarn_; }
    [[nodiscard]] gfx::Rect viewChipRect() const noexcept { return chipView_; }   // 1.22.6
    [[nodiscard]] gfx::Rect viewBarPart(int part) const noexcept { return part < 0 ? viewPrev_ : part > 0 ? viewNext_ : viewName_; }
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
    // ---- 1.22.7 : la bande << Corrige : ... >>, << Tout corriger rapidement >> -------------------------------------------
    [[nodiscard]] gfx::Rect bandUndoRect() const noexcept { return bandUndo_; }
    [[nodiscard]] gfx::Rect quickAllRect(int group) const {
        for (const auto& [r, g] : allLinks_)
            if (g == group) return r;
        return {};
    }
    [[nodiscard]] gfx::Rect groupHeadingRect(int group) const {   // le titre d'une categorie qui se corrige d'un coup
        for (const auto& [r, i] : hits_)
            if (i < rows.size() && rows[i].kind == Row::Kind::Heading && rows[i].group == group) return r;
        return {};
    }
    [[nodiscard]] int groupAt(gfx::Point at) const {   // ce titre sous ce point (0 : aucun)
        for (const auto& [r, i] : hits_)
            if (r.contains(at) && i < rows.size() && rows[i].kind == Row::Kind::Heading && rows[i].group > 0) return rows[i].group;
        return 0;
    }

    bool hasTooltip() const override { return true; }
    std::string liveTooltip(gfx::Point mouse) const override {
        if (zoomOut_.contains(mouse)) return "Zoom arri\xC3\xA8re (Ctrl+moins, ou la molette sur l'aper\xC3\xA7u)";
        if (zoomFit_.contains(mouse)) return "Ajuster : la vue enti\xC3\xA8re (Ctrl+0, ou un double-clic sur l'aper\xC3\xA7u)";
        if (zoomIn_.contains(mouse)) return "Zoom avant (Ctrl+plus, ou la molette sur l'aper\xC3\xA7u)";
        if (toggle_.contains(mouse))
            return listHidden ? "Montrer la liste (ce que fait la proposition)" : "Masquer la liste : l'aper\xC3\xA7u prend toute la largeur";
        // 1.22.6 : la barre des vues, la puce << Cette vue seulement >>
        if (viewPrev_.contains(mouse)) return "Vue pr\xC3\xA9" "c\xC3\xA9" "dente (PageUp, ou Ctrl+gauche)";
        if (viewNext_.contains(mouse)) return "Vue suivante (PageDown, ou Ctrl+droite)";
        if (viewName_.contains(mouse))
            return multiView() ? d_.viewBarText() + "\nClic : la liste des vues de la proposition (ce que chacune cr\xC3\xA9" "e, modifie, supprime ; ses erreurs)."
                               : d_.viewBarText() + "\nLa proposition ne touche que cette vue.";
        if (!listHidden && chipView_.contains(mouse))
            return "Ne montrer que les lignes de la vue montr\xC3\xA9" "e \xC3\xA0 droite (" + d_.viewName(d_.shown_) + "). \xC3\x89" "chap : tout remontrer.";
        // 1.22.4 : les puces
        if (!listHidden && (chipErr_.contains(mouse) || chipWarn_.contains(mouse))) {
            const bool err = chipErr_.contains(mouse);
            // 1.22.6 : ou ils sont (plusieurs vues) - << Sur : Popup_Vanne (2), Vue_Station (1) >>
            std::string where;
            if (multiView())
                for (std::size_t k = 0; k < d_.viewStats_.size() && k < d_.viewCount(); ++k)
                    if (const std::size_t n = err ? d_.viewStats_[k].errors : d_.viewStats_[k].warnings; n > 0)
                        where += (where.empty() ? "\nSur : " : ", ") + d_.viewName(k) + " (" + std::to_string(n) + ")";
            return std::string(err ? "Ne montrer que les lignes li\xC3\xA9" "es \xC3\xA0 une erreur" : "Ne montrer que les lignes li\xC3\xA9" "es \xC3\xA0 un avertissement")
                   + " (les deux puces ensemble : l'un ou l'autre). \xC3\x89" "chap : tout remontrer." + where;
        }
        // 1.22.7 : la bande << Corrige : ... >>, son Revenir ; le lien << Tout corriger rapidement >> des titres
        if (bandUndo_.contains(mouse))
            return "Revenir sur " + std::string(d_.bandUndo_.size() > 1 ? "ces corrections" : "cette correction")
                   + " : la proposition reprend sa forme d'avant (le projet ne change pas).";
        if (band_.contains(mouse)) return d_.bandText();
        for (const auto& [r, g] : allLinks_)
            if (r.contains(mouse))
                return "Tout corriger rapidement : " + std::to_string(d_.quickCount(g)) + (d_.quickCount(g) > 1 ? " corrections s\xC3\xBBres" : " correction s\xC3\xBBre")
                       + " de ce groupe, en une fois (la proposition seule ; Revenir les reprend). Les autres lignes disent pourquoi au clic droit.";
        if (list_.contains(mouse))
            for (const auto& [r, i] : hits_) {
                if (!r.contains(mouse) || i >= rows.size()) continue;
                const auto& row = rows[i];
                if (row.kind == Row::Kind::Heading)
                    return std::string(folded.count(groupKey(row.text)) ? "Clic : d\xC3\xA9plier ce groupe" : "Clic : replier ce groupe")
                           + (row.group > 0 ? "\nClic droit : Tout corriger rapidement." : "");
                if (row.op >= 0)
                    return row.excluded ? "Clic : remettre cette suppression dans le plan."
                                        : "Clic : retirer cette suppression du plan (ce qui en d\xC3\xA9pend est ignor\xC3\xA9, la v\xC3\xA9rification se refait).";
                // 1.22.4 : ce que le clic encadre dans l'apercu
                const std::size_t n = row.target >= 0 && static_cast<std::size_t>(row.target) < targets.size() ? targets[static_cast<std::size_t>(row.target)].size() : 0;
                std::string show = n == 0 ? std::string{}
                                   : n == 1 ? std::string("\nClic : l'encadrer dans l'aper\xC3\xA7u.")
                                            : "\nClic : encadrer ses " + std::to_string(n) + " objets dans l'aper\xC3\xA7u.";
                // 1.22.6 : sa vue (montree a droite, ou une autre : le clic y fait passer l'apercu)
                if (multiView() && !row.views.empty())
                    show += std::string(row.views.size() > 1 ? "\nVues : " : "\nVue : ") + viewNames(row)
                            + (onShown(row) ? std::string(" (montr\xC3\xA9" "e \xC3\xA0 droite).") : std::string(" \xE2\x80\x94 le clic y fait passer l'aper\xC3\xA7u."));
                if (row.list == 3) return row.text + " \xE2\x80\x94 " + row.right + show + "\nClic droit : revenir sur cette correction.";
                if (row.list > 0) return row.text + show + "\nClic droit : Corriger rapidement, et les autres corrections.";   // 1.22.1, 1.22.7
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
        searchBox_ = chips_ = chipErr_ = chipWarn_ = chipView_ = gfx::Rect{};
        if (!listHidden) {
            searchBox_ = {x, listTop, leftW, 28.f};
            chips_ = {x, listTop + 32.f, leftW, 22.f};
            chipErr_ = {chips_.x, chips_.y, ui::measureWidth(chipLabel(true), kSmall) + 30.f, chips_.h};
            chipWarn_ = {chipErr_.right() + 8.f, chips_.y, ui::measureWidth(chipLabel(false), kSmall) + 30.f, chips_.h};
            // 1.22.6 : << Cette vue seulement >> (plusieurs vues) ; << Cette vue >> si la place manque.
            if (multiView()) {
                chipViewText_ = "Cette vue seulement";
                if (chipWarn_.right() + 8.f + ui::measureWidth(chipViewText_, kSmall) + 28.f > chips_.right()) chipViewText_ = "Cette vue";
                chipView_ = {chipWarn_.right() + 8.f, chips_.y, ui::measureWidth(chipViewText_, kSmall) + 28.f, chips_.h};
            }
            listTop += 60.f;
        }
        // 1.22.7 : LA BANDE << Corrige : ... - Revenir >> (ou << Reverification... >>), en haut de la liste.
        band_ = bandUndo_ = gfx::Rect{};
        if (!listHidden && (!d_.bandText_.empty() || d_.rechecking_)) {
            band_ = {x, listTop, leftW, 26.f};
            listTop += 30.f;
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
        // 1.22.6 : LA BARRE DES VUES, au-dessus de l'apercu : la fleche precedente, le titre de la vue montree (un clic : la
        // liste des vues), la fleche suivante.
        viewBar_ = {px, previewTop, right - px, 26.f};
        viewPrev_ = {viewBar_.x, viewBar_.y, 28.f, viewBar_.h};
        viewNext_ = {viewBar_.right() - 28.f, viewBar_.y, 28.f, viewBar_.h};
        viewName_ = {viewPrev_.right() + 4.f, viewBar_.y, std::max(0.f, viewNext_.x - viewPrev_.right() - 8.f), viewBar_.h};
        const float shownTop = viewBar_.bottom() + 6.f;
        preview_ = {px, shownTop, right - px, std::max(80.f, bottom - shownTop - 24.f)};
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
        // 1.22.7 : a gauche, << Retour >> (discret : fermer sans rien faire), puis << Modifier la demande >>.
        float lx = x;
        for (ui::Button* b : {d_.back_, d_.edit_}) {
            if (!b) continue;
            const float bw = ui::measureWidth(b->text(), gfx::FontId{16}) + 34.f;
            b->setBounds({lx, panel_.bottom() - 44.f, bw, 30.f});
            lx += bw + 10.f;
        }
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
        std::string where = createsView(*p) ? "Vue neuve \xC2\xAB " + newViewName(*p) + " \xC2\xBB"
                            : usesOpenView(*p) ? "Vue \xC2\xAB " + p->view + " \xC2\xBB"
                                               : std::string("Le projet (variables, types IHM)");
        if (const std::size_t n = d_.viewCount(); n > 1)   // 1.22.6 : toutes les vues (la barre au-dessus de l'apercu)
            where += " et " + std::to_string(n - 1) + (n > 2 ? " autres vues" : " autre vue");
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
        paintBand(ctx);     // 1.22.7
        paintList(ctx);
        paintPreview(ctx);
        paintViewBar(ctx);  // 1.22.6
    }

    // 1.22.6 : LA BARRE DES VUES - << Vue 2/4 : Popup_Vanne (neuve, popup) - 1 erreur >> entre deux fleches (grisees au
    // bout, et pour une seule vue) ; devant, la pastille de ses erreurs (ou de ses avertissements) ; derriere, la fleche vers
    // le bas : un clic ouvre la liste des vues.
    void paintViewBar(const ui::PaintContext& ctx) {
        if (viewBar_.w <= 0.f) return;
        const auto& c = ctx.theme.color;
        const std::size_t n = d_.viewCount(), k = d_.shown_;
        const auto arrow = [&](const gfx::Rect& b, int dir, bool on) {
            ctx.r.fillRoundedRect(b, on ? c.rowAltBg : c.inputBg, 4.f);
            ctx.r.strokeRect(b, c.border, 1.f);
            chevron(ctx.r, {b.x + (b.w - 16.f) / 2.f, b.y + (b.h - 16.f) / 2.f, 16.f, 16.f}, dir, on ? c.text : c.textDisabled);
        };
        arrow(viewPrev_, 2, n > 1 && k > 0);
        arrow(viewNext_, 0, n > 1 && k + 1 < n);
        const bool list = n > 1;
        ctx.r.fillRoundedRect(viewName_, c.inputBg, 4.f);
        ctx.r.strokeRect(viewName_, c.border, 1.f);
        float tx = viewName_.x + 10.f;
        float tw = viewName_.w - 20.f - (list ? 20.f : 0.f);
        const int level = k < d_.viewStats_.size() ? (d_.viewStats_[k].errors > 0 ? 2 : d_.viewStats_[k].warnings > 0 ? 1 : 0) : 0;
        if (level > 0 && n > 0) {
            pastille(ctx.r, {tx + 6.f, viewName_.y + viewName_.h / 2.f}, 13.f, issueColor(ctx.theme, level), c.panelBg);
            tx += 18.f;
            tw -= 18.f;
        }
        const std::string text = n == 0 ? std::string("Aucune vue \xC3\xA0 dessiner")
                                        : d_.viewBarText() + (d_.viewsPending() ? std::string(" \xC2\xB7 les autres vues se calculent\xE2\x80\xA6") : std::string{});
        ctx.r.drawText({tx, viewName_.y + (viewName_.h - ctx.r.lineHeight(kText)) / 2.f}, elide(ctx.r, text, kText, tw), kText, n == 0 ? c.textMuted : c.text);
        if (list) chevron(ctx.r, {viewName_.right() - 24.f, viewName_.y + (viewName_.h - 16.f) / 2.f, 16.f, 16.f}, 1, c.textMuted);
    }

    // 1.22.6 : LA VUE D'UNE LIGNE, en petit a droite (plusieurs vues seulement ; ni un titre, ni un detail) - sur la vue montree :
    // une pastille d'accent et le nom, sur un fond d'accent ; sur une autre vue : le nom, grise. Une ligne qui EST la vue (une
    // vue a creer) : la pastille seule, sur la vue montree. Sa largeur (0 : rien).
    [[nodiscard]] bool isView(const Row& row) const { return row.views.size() == 1 && d_.viewName(row.views.front()) == row.text; }
    [[nodiscard]] float tagWidth(gfx::IRenderer& r, const Row& row) const {
        if (!multiView() || row.views.empty() || row.kind == Row::Kind::Heading || row.kind == Row::Kind::Change) return 0.f;
        if (isView(row)) return onShown(row) ? 14.f : 0.f;
        return std::min(130.f, r.measure(viewTag(row), kSmall).width + 20.f);
    }
    void paintTag(const ui::PaintContext& ctx, const Row& row, float right, float y, float h) const {
        const float w = tagWidth(ctx.r, row);
        if (w <= 0.f) return;
        const auto& c = ctx.theme.color;
        const bool here = onShown(row);
        if (isView(row)) {
            ctx.r.fillRoundedRect({right - w + 4.f, y + h / 2.f - 3.f, 6.f, 6.f}, c.accent, 3.f);
            return;
        }
        const gfx::Rect box{right - w, y + 3.f, w, h - 6.f};
        if (here) {
            ctx.r.fillRoundedRect(box, alpha(c.accent, 40), 3.f);
            ctx.r.fillRoundedRect({box.x + 5.f, box.y + box.h / 2.f - 3.f, 6.f, 6.f}, c.accent, 3.f);
        }
        ctx.r.drawText({box.x + 14.f, box.y + (box.h - ctx.r.lineHeight(kSmall)) / 2.f}, elide(ctx.r, viewTag(row), kSmall, w - 17.f), kSmall,
                       here ? c.text : c.textDisabled);
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
        // 1.22.6 : << Cette vue seulement >> - sa marque : la pastille d'accent des lignes de la vue montree.
        if (chipView_.w > 0.f) {
            if (onlyView) {
                ctx.r.fillRoundedRect(chipView_, alpha(c.accent, 70), 4.f);
                ctx.r.strokeRect(chipView_, c.accent, 1.5f);
            } else {
                ctx.r.fillRoundedRect(chipView_, c.inputBg, 4.f);
                ctx.r.strokeRect(chipView_, c.border, 1.f);
            }
            ctx.r.fillRoundedRect({chipView_.x + 8.f, chipView_.y + chipView_.h / 2.f - 4.f, 8.f, 8.f}, c.accent, 4.f);
            ctx.r.drawText({chipView_.x + 21.f, chipView_.y + (chipView_.h - ctx.r.lineHeight(kSmall)) / 2.f}, chipViewText_, kSmall, c.text);
        }
    }

    // 1.22.7 : LA BANDE - << Corrige : ... >> et son << Revenir >> (un lien), sur un fond vert pale ; une reverification de fond :
    // << Reverification... >> (sur un fond bleu pale, seule).
    void paintBand(const ui::PaintContext& ctx) {
        bandUndo_ = {};
        if (band_.w <= 0.f) return;
        const auto& c = ctx.theme.color;
        const bool alone = d_.bandText_.empty();
        const gfx::Color col = alone ? c.info : c.ok;
        ctx.r.fillRoundedRect(band_, alpha(col, 38), 4.f);
        ctx.r.fillRect({band_.x, band_.y + 3.f, 3.f, band_.h - 6.f}, col);
        const float ty = band_.y + (band_.h - ctx.r.lineHeight(kSmall)) / 2.f;
        float right = band_.right() - 8.f;
        if (!d_.bandUndo_.empty()) {
            const float w = ctx.r.measure("Revenir", kSmall).width;
            bandUndo_ = {right - w - 6.f, band_.y + 2.f, w + 12.f, band_.h - 4.f};
            ctx.r.drawText({right - w, ty}, "Revenir", kSmall, c.accent);
            ctx.r.line({right - w, ty + ctx.r.lineHeight(kSmall)}, {right, ty + ctx.r.lineHeight(kSmall)}, alpha(c.accent, 170), 1.f);
            const float dot = ctx.r.measure(" \xC2\xB7 ", kSmall).width;
            ctx.r.drawText({right - w - dot, ty}, " \xC2\xB7 ", kSmall, c.textMuted);
            right -= w + dot;
        }
        std::string text = d_.bandText_;
        if (d_.rechecking_)
            text += alone ? std::string("Rev\xC3\xA9rification de la proposition sur un fil de fond\xE2\x80\xA6") : std::string(" \xC2\xB7 Rev\xC3\xA9rification\xE2\x80\xA6");
        ctx.r.drawText({band_.x + 10.f, ty}, elide(ctx.r, text, kSmall, right - band_.x - 12.f), kSmall, c.text);
    }

    void paintList(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(list_, c.inputBg);
        ctx.r.strokeRect(list_, c.border, 1.f);
        allLinks_.clear();   // 1.22.7
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
            const float tag = tagWidth(ctx.r, rows[i]);   // 1.22.6 : sa vue, a droite de la premiere ligne
            wrapped[i] = multi ? wrap(rows[i].text, kSmall, w0 - 28.f - (tag > 0.f ? tag + 6.f : 0.f)) : std::vector<std::string>{rows[i].text};
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
        bool  flashing = false;   // 1.22.7
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
                // 1.22.7 : une correction qui vient d'etre faite - un fond vert qui s'eteint en une seconde et demie.
                if (!flashed.empty() && std::find(flashed.begin(), flashed.end(), static_cast<int>(i)) != flashed.end()) {
                    if (flashStart < 0.0) flashStart = ctx.time;
                    const double since = ctx.time - flashStart;
                    if (since >= 0.0 && since < 1.5) {
                        ctx.r.fillRect({list_.x + 1.f, y, list_.w - space - 2.f, rh}, alpha(c.ok, static_cast<int>(110.0 * (1.0 - since / 1.5))));
                        ctx.r.fillRect({list_.x + 1.f, y, 3.f, rh}, c.ok);
                        flashing = true;
                    }
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
                        // 1.22.7 : << Tout corriger rapidement >> - un lien a droite du titre, si le groupe a des corrections sures.
                        if (row.group > 0 && d_.quickCount(row.group) > 0) {
                            const char* link = "Tout corriger rapidement";
                            const float lw = ctx.r.measure(link, kSmall).width + 8.f;
                            if (tw - lw > 110.f) {
                                const float lx = list_.x + 22.f + tw - lw;
                                ctx.r.drawText({lx + 4.f, ty}, link, kSmall, c.accent);
                                ctx.r.line({lx + 4.f, ty + ctx.r.lineHeight(kSmall)}, {lx + lw - 4.f, ty + ctx.r.lineHeight(kSmall)}, alpha(c.accent, 170), 1.f);
                                allLinks_.emplace_back(gfx::Rect{lx, y + 1.f, lw, lh - 2.f}, row.group);
                                tw -= lw + 8.f;
                            }
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
                        // 1.22.6 : sa vue, a gauche de son genre
                        const float vw = tagWidth(ctx.r, row);
                        const float gap = rw > 0.f ? 6.f : 0.f;
                        paintTag(ctx, row, list_.x + w - rw - gap, y, lh);
                        const float used = rw + (vw > 0.f ? vw + gap : 0.f);
                        float tw = w - used - 34.f;
                        if (row.issue > 0) {
                            pastille(ctx.r, {list_.x + w - used - 12.f, y + lh / 2.f}, 13.f, issueColor(ctx.theme, row.issue), c.panelBg);
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
                        const float vw = tagWidth(ctx.r, row);   // 1.22.6 : sa vue, tout a droite
                        if (vw > 0.f) {
                            paintTag(ctx, row, list_.x + w + 4.f, y, lh);
                            tw -= vw + 6.f;
                        }
                        if (row.issue > 0) {
                            pastille(ctx.r, {list_.x + tw - 2.f, y + lh / 2.f}, 13.f, issueColor(ctx.theme, row.issue), c.panelBg);
                            tw -= 20.f;
                        }
                        ctx.r.drawText({list_.x + 12.f, ty}, elide(ctx.r, row.text, kSmall, tw), kSmall, issueCol);
                        break;
                    }
                    case Row::Kind::Warning:
                    case Row::Kind::Error: {
                        const bool err = row.kind == Row::Kind::Error;
                        ui::drawIcon(ctx.r, err ? ui::Icon::Error : ui::Icon::Warning, {list_.x + 8.f, y + 4.f, 14.f, 14.f}, err ? c.error : c.warning);
                        paintTag(ctx, row, list_.x + w + 4.f, y, lh);   // 1.22.6 : sa vue, a droite de la premiere ligne
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
        if (flashing) invalidate();
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
        // 1.22.7 : une grosse proposition corrigee se reverifie sur un fil de fond - l'apercu d'avant reste, et le dit.
        if (d_.rechecking_) {
            const std::string t = "Rev\xC3\xA9rification\xE2\x80\xA6";
            const gfx::Rect b{preview_.x + 6.f, preview_.y + 6.f, ctx.r.measure(t, kSmall).width + 16.f, 22.f};
            ctx.r.fillRoundedRect(b, alpha(c.panelBg, 235), 4.f);
            ctx.r.strokeRect(b, c.info, 1.f);
            ctx.r.drawText({b.x + 8.f, b.y + (b.h - ctx.r.lineHeight(kSmall)) / 2.f}, t, kSmall, c.text);
        }
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
            } else if (const int g = groupAt(d->pos); g > 0) {
                (void)d_.openGroupMenu(g, d->pos);   // 1.22.7 : le clic droit d'un titre - Tout corriger rapidement
            }
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            if (close_.contains(d->pos)) {
                d_.close();
                return ui::EventResult::Consumed;
            }
            // 1.22.7 : le << Revenir >> de la bande ; le lien << Tout corriger rapidement >> d'un titre.
            if (bandUndo_.contains(d->pos)) {
                (void)d_.undoBand();
                return ui::EventResult::Consumed;
            }
            for (const auto& [r, g] : allLinks_)
                if (r.contains(d->pos)) {
                    const int group = g;   // la liste se refait : le rang d'abord
                    (void)d_.quickFixAll(group);
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
            // 1.22.6 : la barre des vues - les fleches, le titre (la liste des vues) ; la puce << Cette vue seulement >>.
            if (viewPrev_.contains(d->pos) || viewNext_.contains(d->pos)) {
                (void)d_.stepView(viewPrev_.contains(d->pos) ? -1 : 1);
                return ui::EventResult::Consumed;
            }
            if (viewName_.contains(d->pos)) {
                (void)d_.openViewMenu();
                return ui::EventResult::Consumed;
            }
            if (chipView_.contains(d->pos)) {
                onlyView = !onlyView;
                applyFilter();
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
    gfx::Rect         chipView_{}, viewBar_{}, viewPrev_{}, viewNext_{}, viewName_{};   // 1.22.6 : la puce de la vue, la barre des vues
    gfx::Rect         band_{}, bandUndo_{};                                       // 1.22.7 : la bande << Corrige : ... >>, son Revenir
    std::vector<std::pair<gfx::Rect, int>> allLinks_;                             // 1.22.7 : les liens << Tout corriger rapidement >>
    std::string       chipViewText_;
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

// ---- 1.22.6 : toutes les vues de la proposition ----------------------------------------------------------------------------
// Le calcul des vues d'une grosse proposition, sur un fil de fond (une copie du projet, l'environnement fige) : le resultat
// passe dans la fenetre a sa mise a jour (pollViews).
struct AiPreviewDialog::ViewJob {
    std::vector<ViewPreview> views;
    std::atomic<bool>        finished{false};
    std::thread              worker;
};

namespace {
// Les vues qui se dessinent (une reponse << rien a dessiner >> : aucune - l'apercu de la carte dit pourquoi).
std::vector<ViewPreview> drawable(std::vector<ViewPreview> all) {
    std::erase_if(all, [](const ViewPreview& v) { return !v.preview.ok; });
    return all;
}
// << 3 objets crees - 1 modifie - 2 supprimes >> : ce qu'une vue change (la liste des vues).
std::string changesOf(const Touched& t) {
    std::string out;
    const auto part = [&](std::size_t n, const char* one, const char* many) {
        if (n > 0) out += (out.empty() ? "" : " \xC2\xB7 ") + std::to_string(n) + " " + (n > 1 ? many : one);
    };
    part(t.created.size(), "objet cr\xC3\xA9\xC3\xA9", "objets cr\xC3\xA9\xC3\xA9s");
    part(t.modified.size(), t.created.empty() ? "objet modifi\xC3\xA9" : "modifi\xC3\xA9", t.created.empty() ? "objets modifi\xC3\xA9s" : "modifi\xC3\xA9s");
    part(t.removed.size(), t.created.empty() && t.modified.empty() ? "objet supprim\xC3\xA9" : "supprim\xC3\xA9",
         t.created.empty() && t.modified.empty() ? "objets supprim\xC3\xA9s" : "supprim\xC3\xA9s");
    return out.empty() ? std::string("aucun objet chang\xC3\xA9") : out;
}
} // namespace

AiPreviewDialog::~AiPreviewDialog() {
    // Le calcul des vues finit (il ne touche que ses copies) : attendu ici, a la fermeture.
    if (viewJob_ && viewJob_->worker.joinable()) viewJob_->worker.join();
    for (auto& j : retiring_)
        if (j->worker.joinable()) j->worker.join();
}

void AiPreviewDialog::ensureViews(const Proposal& p) {
    auto& a = panel_.assistant();
    const auto doc = a.host().document ? a.host().document() : nullptr;
    const std::string key = proposalKey(p) + "|" + (doc ? std::to_string(doc->revision) : std::string("-"));
    if (key == viewsKey_) return;   // deja calculees (ou en calcul) pour cette proposition, sur ce projet
    viewsKey_ = key;
    if (viewJob_) retiring_.push_back(std::move(viewJob_));
    views_.clear();
    if (!doc) return;
    // Une seule vue (la plupart des propositions) : l'apercu de la carte, rien a recalculer.
    if (oneView(p)) {
        if (main_.ok) views_.push_back(ViewPreview{main_, doc->project.view(main_.view.id) == nullptr});
        return;
    }
    // Une grosse proposition : sur un fil de fond, comme la verification de la carte (la copie du projet se fait ici).
    Env frozen;
    if (checkInBackground(p) && a.frozenEnvironment(frozen)) {
        auto job = std::make_shared<ViewJob>();
        auto project = [&] {
            XPG_MESURE("ia.apercu.vues.copie");
            return std::make_shared<const hmi::Project>(doc->project);
        }();
        job->worker = std::thread([job, project, prop = p, env = std::move(frozen)] {
            XPG_TRAVAIL("ia.apercu-vues");   // l'onglet Performances, travaux de fond
            job->views = previews(*project, prop, env);
            job->finished.store(true, std::memory_order_release);
        });
        viewJob_ = std::move(job);
        return;
    }
    XPG_MESURE("ia.apercu.vues");
    views_ = drawable(previews(doc->project, p, a.host().env));
}

void AiPreviewDialog::pollViews() {
    std::erase_if(retiring_, [](const std::shared_ptr<ViewJob>& j) {
        if (!j->finished.load(std::memory_order_acquire)) return false;
        if (j->worker.joinable()) j->worker.join();
        return true;
    });
    if (!viewJob_ || !viewJob_->finished.load(std::memory_order_acquire)) return;
    if (viewJob_->worker.joinable()) viewJob_->worker.join();
    views_ = drawable(std::move(viewJob_->views));
    viewJob_.reset();
    refresh();   // les reperes de toutes les vues : la liste, la vue de chaque ligne, les erreurs de chaque vue
}

void AiPreviewDialog::useShownView() {
    if (shown_ >= std::max<std::size_t>(viewCount(), 1)) shown_ = 0;
    preview_ = shown_ < views_.size() ? views_[shown_].preview : main_;
    objectIndex_.clear();
    for (std::size_t i = 0; i < preview_.view.objects.size(); ++i) objectIndex_.emplace(preview_.view.objects[i].id, i);
    issues_.clear();
    issueTexts_.clear();
    for (const auto& [at, level] : viewIssues_)
        if (at.first == shown_) issues_[at.second] = level;
    for (const auto& [at, texts] : viewIssueTexts_)
        if (at.first == shown_) issueTexts_[at.second] = texts;
}

std::size_t AiPreviewDialog::viewCount() const noexcept { return !views_.empty() ? views_.size() : main_.ok ? 1 : 0; }

std::string AiPreviewDialog::viewName(std::size_t rank) const {
    if (rank < views_.size()) return views_[rank].preview.view.name;
    return rank == 0 && main_.ok ? main_.view.name : std::string{};
}

std::string AiPreviewDialog::viewTitle(std::size_t rank) const {
    const std::size_t n = viewCount();
    if (rank >= n) return {};
    const Preview& pv = rank < views_.size() ? views_[rank].preview : main_;
    bool created = rank < views_.size() && views_[rank].created;
    if (rank >= views_.size()) {   // l'apercu de la carte (les vues se calculent) : neuve si le projet ne l'a pas
        const auto doc = panel_.assistant().host().document ? panel_.assistant().host().document() : nullptr;
        created = doc && !doc->project.view(pv.view.id);
    }
    std::string what = created ? "neuve" : "modifi\xC3\xA9" "e";
    if (const std::string role = roleLabel(pv.view.role); !role.empty()) what += ", " + role;
    std::string t = "Vue " + std::to_string(rank + 1) + (viewsPending() ? std::string{} : "/" + std::to_string(n)) + " : " + pv.view.name + " (" + what + ")";
    if (rank < viewStats_.size()) {
        if (viewStats_[rank].errors > 0) t += " \xC2\xB7 " + plural(viewStats_[rank].errors, "erreur");
        if (viewStats_[rank].warnings > 0) t += " \xC2\xB7 " + plural(viewStats_[rank].warnings, "avertissement");
    }
    return t;
}

std::string AiPreviewDialog::viewBarText() const { return viewCount() == 0 ? std::string("Aucune vue \xC3\xA0 dessiner") : viewTitle(shown_); }

bool AiPreviewDialog::showView(std::size_t rank) {
    if (rank >= viewCount()) return false;
    shownName_ = viewName(rank);   // gardee d'un calcul a l'autre (une correction, l'avis du logiciel)
    if (rank == shown_) return true;
    shown_ = rank;
    useShownView();
    if (auto* b = bodyOf(*this)) b->shownViewChanged();
    return true;
}

bool AiPreviewDialog::stepView(int delta) {
    const long long to = static_cast<long long>(shown_) + delta;
    if (delta == 0 || to < 0 || to >= static_cast<long long>(viewCount())) return false;
    return showView(static_cast<std::size_t>(to));
}

bool AiPreviewDialog::openViewMenu() {
    auto* root = widgetRoot();
    const auto* b = bodyOf(*this);
    const std::size_t n = viewCount();
    if (!root || !b || !viewMenu_ || done_ || n < 2) return false;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = std::to_string(n) + " vues dans la proposition";
    items.push_back(std::move(head));
    for (std::size_t k = 0; k < n; ++k) {
        ui::PopupMenu::Item it;
        it.id = static_cast<int>(k);
        it.label = viewTitle(k);
        it.note = k < views_.size() ? changesOf(views_[k].preview.touched) : changesOf(main_.touched);
        it.radio = true;
        it.checked = k == shown_;
        items.push_back(std::move(it));
    }
    viewMenu_->setItems(std::move(items));
    viewMenu_->setBounds(root->bounds());
    gfx::Size surface = ui::surfaceSize();
    if (surface.w <= 0.f || surface.h <= 0.f) surface = {root->bounds().right() + 400.f, root->bounds().bottom() + 400.f};
    const gfx::Rect at = b->viewBarPart(0);
    viewMenu_->openAt({at.x, at.bottom() + 2.f}, surface);
    return true;
}

bool AiPreviewDialog::viewMenuOpen() const { return viewMenu_ && viewMenu_->isOpen(); }

std::vector<std::string> AiPreviewDialog::viewMenuItems() const {
    std::vector<std::string> out;
    if (!viewMenuOpen()) return out;
    for (const auto& it : viewMenu_->columnItems(0))
        if (!it.heading && !it.separator) out.push_back((it.checked ? "(o) " : "") + it.label + " | " + it.note);
    return out;
}

std::string AiPreviewDialog::rowView(const std::string& text) const {
    const auto* b = bodyOf(*this);
    if (!b || !b->multiView()) return {};   // une seule vue : rien ne se dit (rien ne se dessine)
    for (const bool exact : {true, false})
        for (const auto& r : b->rows)
            if (r.kind != Row::Kind::Heading && (exact ? r.text == text : r.text.find(text) != std::string::npos)) return b->viewTag(r);
    return {};
}

bool AiPreviewDialog::rowOnShownView(const std::string& text) const {
    const auto* b = bodyOf(*this);
    if (!b) return false;
    for (const bool exact : {true, false})
        for (const auto& r : b->rows)
            if (r.kind != Row::Kind::Heading && (exact ? r.text == text : r.text.find(text) != std::string::npos)) return b->onShown(r);
    return false;
}

void AiPreviewDialog::setViewFilter(bool on) {
    auto* b = bodyOf(*this);
    if (!b) return;
    b->onlyView = on && viewCount() > 1;
    b->applyFilter();
}

bool AiPreviewDialog::viewFilter() const {
    const auto* b = bodyOf(*this);
    return b && b->onlyView;
}

gfx::Rect AiPreviewDialog::viewBarRect(int part) const {
    const auto* b = bodyOf(*this);
    return b ? b->viewBarPart(part) : gfx::Rect{};
}

gfx::Rect AiPreviewDialog::viewChipRect() const {
    const auto* b = bodyOf(*this);
    return b ? b->viewChipRect() : gfx::Rect{};
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
    // 1.22.7 : RETOUR - fermer sans rien faire : ni refuser, ni appliquer, ni toucher la saisie (la proposition reste a decider).
    back_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Retour", base + ".retour")));
    back_->setStyle(ui::Button::Style::Flat);
    back_->setTooltip("Fermer la pr\xC3\xA9visualisation sans rien d\xC3\xA9" "cider (\xC3\x89" "chap)");
    links_ += back_->clicked->connect([this] { close(); });
    edit_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Modifier la demande", base + ".modifier")));
    edit_->setTooltip("Remettre la demande dans la saisie pour la reformuler (cette proposition reste \xC3\xA0 d\xC3\xA9" "cider, sur sa carte).");
    links_ += edit_->clicked->connect([this] { editRequest(); });
    // 1.16.1 : << Refuser... >> (une question d'abord) - plus << Annuler >>, qu'on prenait pour << fermer >>.
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Refuser\xE2\x80\xA6", base + ".refuser")));
    cancel_->setStyle(ui::Button::Style::Flat);
    cancel_->setTooltip("Refuser la proposition (une question d'abord) : le projet ne change pas ; sa carte pourra revenir sur ce refus. "
                        "Retour (ou \xC3\x89" "chap) ferme cette fen\xC3\xAAtre SANS d\xC3\xA9" "cider.");
    links_ += cancel_->clicked->connect([this] { cancel(); });
    apply_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Appliquer", base + ".appliquer")));
    apply_->setStyle(ui::Button::Style::Primary);
    apply_->setTooltip("Modifier le projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
    links_ += apply_->clicked->connect([this] { (void)apply(); });
    // 1.22.1 : le menu du clic droit des erreurs (le menu de l'appli), devant le reste.
    fixMenu_ = &static_cast<ui::PopupMenu&>(b->addChild(std::make_unique<ui::PopupMenu>(base + ".corriger")));
    links_ += fixMenu_->itemChosen->connect([this](int id) { runFix(id); });
    // 1.22.6 : la liste des vues (un clic sur le titre de la barre des vues).
    viewMenu_ = &static_cast<ui::PopupMenu&>(b->addChild(std::make_unique<ui::PopupMenu>(base + ".vues")));
    links_ += viewMenu_->itemChosen->connect([this](int id) {
        if (id >= 0) (void)showView(static_cast<std::size_t>(id));
    });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

void AiPreviewDialog::refresh() {
    XPG_MESURE("ia.apercu.rafraichir");   // 1.22.7 : la previsualisation refaite (une correction, l'avis du logiciel, une vue)
    auto& a = panel_.assistant();
    const Proposal* p = a.proposal(message_);
    if (!p) return;
    // 1.20.1 : le resultat de la verification de la carte (fait sur un fil de fond pour une grosse proposition) - pas une
    // nouvelle validation sur le fil de l'interface. En cours : la fenetre l'attend (Update), l'avancement affiche.
    plan::Plan readyPlan;
    waiting_ = !panel_.chat().resultReady(message_, &check_, &main_, &readyPlan);
    planReady_ = !waiting_;
    // 1.22.7 : UNE REVERIFICATION (une correction d'une grosse proposition, le projet qui change) - sur le fil de fond de la
    // carte, comme la premiere : la liste et l'apercu d'avant restent montres (le menu attend, Appliquer aussi), la bande et
    // l'apercu disent << Reverification... >> ; la fenetre garde la main. Le resultat arrive : la liste se refait (Update).
    rechecking_ = waiting_ && shownOnce_;
    if (rechecking_) {
        adoptStoredVerdict(*p, !key_.empty() && proposalKey(*p) != key_);
        if (apply_) apply_->setEnabled(false);
        if (cancel_) cancel_->setEnabled(p->state == "proposee");
        if (fixMenu_ && fixMenu_->isOpen()) fixMenu_->close();
        key_ = proposalKey(*p);
        if (auto* root = widgetRoot()) {
            root->invalidateLayout();
            root->invalidate();
        }
        return;
    }
    if (waiting_) {
        check_ = Check{};
        main_ = Preview{};
        main_.why = "V\xC3\xA9rification de la proposition sur un fil de fond\xE2\x80\xA6 "
                    + std::to_string(static_cast<int>(panel_.chat().checkProgress(message_) * 100.f + 0.5f)) + " %";
    }
    // 1.22.6 : TOUTES LES VUES - calculees une fois par proposition (ici, ou sur un fil de fond) ; la vue montree : celle qu'on
    // avait choisie (par son nom), sinon la principale. En verification : la carte d'abord.
    const std::size_t shownBefore = shown_;
    if (waiting_) {
        if (viewJob_) retiring_.push_back(std::move(viewJob_));
        views_.clear();
        viewsKey_.clear();
    } else {
        ensureViews(*p);
    }
    shown_ = 0;
    if (!shownName_.empty())
        for (std::size_t k = 0; k < views_.size(); ++k)
            if (views_[k].preview.view.name == shownName_) {
                shown_ = k;
                break;
            }
    // 1.22.5 : L'AVIS DU LOGICIEL - une proposition corrigee depuis (une autre cle) : le verdict d'avant ne vaut plus ; un verdict
    // neuf garde dans la proposition se lit. Il est verse dans la verification : ses erreurs et ses avertissements (les puces,
    // Appliquer), ses objets (les cadres) - sa categorie les montre, pas ERREURS ni AVERTISSEMENTS.
    adoptStoredVerdict(*p, !key_.empty() && proposalKey(*p) != key_);
    if (verdictKnown_ && verdict_.ran) build::merge(check_, verdict_);
    const std::size_t buildErrors = verdictKnown_ ? static_cast<std::size_t>(std::count_if(check_.errors.begin(), check_.errors.end(), fromBuild)) : 0;
    const std::size_t buildWarnings = verdictKnown_ ? static_cast<std::size_t>(std::count_if(check_.warnings.begin(), check_.warnings.end(), fromBuild)) : 0;
    auto* body = dynamic_cast<AiPreviewBody*>(widgetRoot());
    // La liste de gauche.
    std::vector<Row> rows;
    const auto heading = [&](std::string t, int group = 0) {
        rows.push_back({Row::Kind::Heading, std::move(t), {}, 0});
        rows.back().base = rows.back().text;   // 1.22.6 : le compte << sur cette vue >> s'y ajoute (relabel)
        rows.back().group = group;             // 1.22.7 : Tout corriger rapidement
    };
    // 1.22.1 : LES CORRECTIONS FAITES (le clic droit sur une erreur) - ce qui a ete corrige, une ligne chacune ; le clic droit
    // sur l'une d'elles la reprend (Revenir). Puis les erreurs et les avertissements, les doublons regroupes.
    errorLines_ = fixes::errorLines(check_);
    warningLines_ = fixes::warningLines(check_);
    if (verdictKnown_) {   // 1.22.5 : les constats du logiciel ont leur categorie
        std::erase_if(errorLines_, [](const fixes::Line& l) { return fromBuild(l.message); });
        std::erase_if(warningLines_, [](const fixes::Line& l) { return fromBuild(l.message); });
    }
    corrections_ = fixes::corrections(*p);
    if (!corrections_.empty()) {
        heading("CORRECTIONS (" + std::to_string(corrections_.size()) + ") \xE2\x80\x94 la proposition seule ; clic droit : revenir");
        rows.push_back({Row::Kind::Note, "Corrig\xC3\xA9 : " + fixes::summary(*p) + ".", {}, 0});
        for (std::size_t k = 0; k < corrections_.size(); ++k) {
            const auto& c = corrections_[k];
            // 1.22.7 : une correction rapide le dit ; une retouche se lit comme une modification (bleue)
            Row r{Row::Kind::Item, c.text + (quickKeys_.count(c.key) ? std::string(" (corrig\xC3\xA9 rapidement)") : std::string{}), c.state,
                  c.what == 3 ? 1 : c.what};
            r.excluded = c.what == 2;
            r.list = 3;
            r.line = k;
            r.key = c.key;
            // 1.22.4 : ce qu'elle touche - les objets d'une operation retiree, ceux qui lisent la variable choisie ; 1.22.7 : ceux
            // d'une operation retouchee
            if (c.what == 2) {
                r.ref = Row::Ref::Ops;
                r.ops = c.undo.ops;
            } else if (c.what == 3) {
                r.ref = Row::Ref::Ops;
                r.ops = {c.op};
            } else if (const VariableChoice* ch = choiceFor(*p, c.undo.name)) {
                r.ref = Row::Ref::Uses;
                r.names = {ch->create && trimCopy(ch->use).empty() ? creatableName(c.undo.name) : trimCopy(ch->use)};
            }
            rows.push_back(std::move(r));
        }
    }
    if (check_.errors.size() > buildErrors) {
        // 1.16.1 (captures) : avec << Appliquer malgre les erreurs >>, le titre le dit (il disait encore << rien ne s'applique >>)
        const bool anywayTitle = !check_.stale && !check_.viewMissing && a.settings().applyWithErrors;
        const std::size_t shownErrors = check_.errors.size() - buildErrors;
        heading("ERREURS (" + std::to_string(shownErrors) + ") \xE2\x80\x94 "
                + (anywayTitle ? std::string(shownErrors > 1 ? "laiss\xC3\xA9" "es de c\xC3\xB4t\xC3\xA9 par \xC2\xAB Appliquer malgr\xC3\xA9 \xC2\xBB"
                                                                      : "laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9 par \xC2\xAB Appliquer malgr\xC3\xA9 \xC2\xBB")
                               : std::string("rien ne s'applique tant qu'il y en a")),
                1);   // 1.22.7 : Tout corriger rapidement
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
    if (check_.warnings.size() > buildWarnings) {
        heading("AVERTISSEMENTS (" + std::to_string(check_.warnings.size() - buildWarnings) + ")", 2);
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
    // 1.22.5 : L'AVIS DU LOGICIEL - GENERER ET COMPILER sur une copie du projet ou la proposition est appliquee : chaque constat
    // NOUVEAU (l'etape, l'endroit, le message ; rouge ou ambre), le resume dans le titre ; << en cours >> avant le verdict.
    buildLines_.clear();
    {
        std::string state;
        // Attendu : Generer et Compiler tournent, ou la proposition est passee par le processus de la 1.22.5 (son verdict
        // arrive). Une proposition d'avant (sans processus, rien ne tourne) n'en aura pas : << pas lances >>, pas << en cours >>.
        const bool expected = verdictKnown_ || panel_.assistant().building(p->id) || p->extra.has("processus");
        if (!verdictKnown_ && expected) state = "en cours\xE2\x80\xA6";
        else if (!verdictKnown_ || !verdict_.ran) state = "pas lanc\xC3\xA9s";
        else state = build::countText(verdict_) + " \xC2\xB7 " + build::durationText(verdict_.ms);
        const std::size_t n = verdictKnown_ && verdict_.ran ? verdict_.findings.size() : 0;
        heading("G\xC3\x89N\xC3\x89RER ET COMPILER" + (n > 0 ? " (" + std::to_string(n) + ")" : std::string{}) + " \xE2\x80\x94 " + state, 4);
        if (!verdictKnown_ && !expected) {
            rows.push_back({Row::Kind::Note, "Pas d'avis du logiciel pour cette proposition : elle date d'avant la 1.22.5. Redemande-la \xC3\xA0 "
                                             "l'IA pour l'avoir (ou modifie-la : il se calcule alors).", {}, 0});
        } else if (!verdictKnown_) {
            rows.push_back({Row::Kind::Note, "L'avis du logiciel arrive : ta proposition est appliqu\xC3\xA9" "e \xC3\xA0 une copie du projet, "
                                             "puis G\xC3\xA9n\xC3\xA9rer et Compiler.", {}, 0});
        } else if (!verdict_.ran) {
            rows.push_back({Row::Kind::Note, "Pas lanc\xC3\xA9s" + (verdict_.why.empty() ? std::string(".") : " : " + verdict_.why), {}, 0});
        } else {
            rows.push_back({Row::Kind::Note, "L'avis du logiciel : ta proposition appliqu\xC3\xA9" "e \xC3\xA0 une copie du projet, g\xC3\xA9n\xC3\xA9r\xC3\xA9" "e et compil\xC3\xA9" "e.",
                            {}, 0});
            buildLines_ = verdict_.findings;
            for (std::size_t k = 0; k < buildLines_.size(); ++k) {
                const auto& f = buildLines_[k];
                Row r{f.error ? Row::Kind::Error : Row::Kind::Warning, buildRowText(f), {}, 0};
                r.list = 4;
                r.line = k;
                r.ref = Row::Ref::Message;   // ses objets : ceux que le verdict a notes (Check::spots, par son message)
                r.names = {build::lineOf(f)};
                if (f.op != Check::Fault::kNoOp && f.op < p->ops.size()) r.ops = {f.op};
                r.issue = f.error ? 2 : 1;
                rows.push_back(std::move(r));
            }
            if (buildLines_.empty()) rows.push_back({Row::Kind::Note, "Rien de nouveau : G\xC3\xA9n\xC3\xA9rer et Compiler acceptent la proposition.", {}, 0});
            if (verdict_.before > 0)
                rows.push_back({Row::Kind::Note, "D\xC3\xA9j\xC3\xA0 l\xC3\xA0 avant ta proposition : " + std::to_string(verdict_.before)
                                                     + (verdict_.before > 1 ? " constats (pas repris ici)." : " constat (pas repris ici)."),
                                {}, 0});
        }
    }
    // 1.22.7 : CORRIGER RAPIDEMENT - ce que ferait chaque ligne (les erreurs, les avertissements, les constats du logiciel), une
    // fois par verification : le menu, le lien des titres et Tout corriger rapidement le relisent (jamais a chaque image).
    {
        const auto doc = a.host().document ? a.host().document() : nullptr;
        const hmi::Project* project = doc ? &doc->project : nullptr;
        quickErrors_.clear();
        quickWarnings_.clear();
        quickBuild_.clear();
        for (const auto& l : errorLines_) quickErrors_.push_back(fixes::quick(*p, check_, l, project));
        for (const auto& l : warningLines_) quickWarnings_.push_back(fixes::quick(*p, check_, l, nullptr));
        for (std::size_t k = 0; k < buildLines_.size(); ++k) quickBuild_.push_back(fixes::quick(*p, check_, buildLine(buildLines_[k], check_, p->ops.size(), k), project));
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
    // 1.22.6 : CHAQUE VUE NEUVE (une vue, une popup, un symbole...) - sa taille, son role ; la principale s'ouvre apres
    // Appliquer. Un clic sur l'une : l'apercu y passe.
    {
        const auto sizeOf = [](const hmi::View& v) {
            if (v.width <= 0) return std::string{};
            std::string t = std::to_string(v.width) + " \xC3\x97 " + std::to_string(v.height);
            if (const std::string role = roleLabel(v.role); !role.empty()) t += " \xC2\xB7 " + role;
            return t;
        };
        std::vector<const hmi::View*> made;
        for (const auto& v : views_)
            if (v.created) made.push_back(&v.preview.view);
        if (made.empty() && !check_.newView.empty()) made.push_back(main_.ok && main_.view.name == check_.newView ? &main_.view : nullptr);
        if (made.size() > 1)
            heading("VUES \xC3\x80 CR\xC3\x89" "ER (" + std::to_string(made.size()) + ") \xE2\x80\x94 \xC2\xAB " + newViewName(*p) + " \xC2\xBB s'ouvre apr\xC3\xA8s Appliquer");
        else if (!made.empty())
            heading("VUE \xC3\x80 CR\xC3\x89" "ER (elle s'ouvre apr\xC3\xA8s Appliquer)");
        for (const hmi::View* v : made) {
            Row r{Row::Kind::Item, v ? v->name : check_.newView, v ? sizeOf(*v) : std::string{}, 0};
            r.view = r.text;
            rows.push_back(std::move(r));
            if (v && !v->description.empty()) rows.push_back({Row::Kind::Change, v->description, {}, 0});
        }
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
            r.view = m.name;          // 1.22.6 : une vue (une popup, un symbole) dessinee : elle-meme
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
        r.ref = Row::Ref::Object;   // 1.22.4 : l'objet ; 1.22.6 : sur sa vue (une autre que la montree : le clic y passe)
        r.names = {it.name};
        r.view = it.view;
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
    std::vector<std::vector<Place>> targets;
    {
        XPG_MESURE("ia.apercu.reperes");
        const auto doc = a.host().document ? a.host().document() : nullptr;
        // 1.22.6 : les objets de TOUTES les vues de la proposition (le rang de chacune dans la barre) ; en attendant leur
        // calcul (un fil de fond), ceux de la vue principale.
        std::vector<const hmi::View*> drawn;
        if (!views_.empty())
            for (const auto& v : views_) drawn.push_back(&v.preview.view);
        else if (main_.ok)
            drawn.push_back(&main_.view);
        const Locator loc(*p, check_, drawn, doc ? &doc->project : nullptr);
        viewIssues_.clear();
        viewIssueTexts_.clear();
        viewStats_.assign(std::max<std::size_t>(drawn.size(), 1), ViewStats{});
        std::unordered_map<std::string, int> nameIssue;   // un nom cite par un message (en majuscules) -> son niveau
        const auto note = [&](const fixes::Line& l, int level) {
            std::vector<Place> at;
            loc.ofMessage(l.message, l.ops, level == 2, at);
            const std::string said = (level == 2 ? "Erreur : " : "Avertissement : ") + l.text;
            std::set<std::size_t> onViews;
            for (const auto& pl : at) {
                int& lv = viewIssues_[{pl.view, pl.id}];
                lv = std::max(lv, level);
                viewIssueTexts_[{pl.view, pl.id}].push_back(said);
                onViews.insert(pl.view);
            }
            for (const auto k : onViews)   // 1.22.6 : la barre des vues dit ou sont les erreurs
                if (k < viewStats_.size()) ++(level == 2 ? viewStats_[k].errors : viewStats_[k].warnings);
            for (const auto& w : wordsOf(l.message)) {
                int& lv = nameIssue[upperAscii(w)];
                lv = std::max(lv, level);
            }
        };
        for (const auto& l : errorLines_) note(l, 2);
        for (const auto& l : warningLines_) note(l, 1);
        for (const auto& f : buildLines_) note(buildLine(f, check_, p->ops.size()), f.error ? 2 : 1);   // 1.22.5 : l'avis du logiciel
        const auto objectIssue = [&](const std::vector<Place>& at) {
            int lv = 0;
            for (const auto& pl : at)
                if (const auto it = viewIssues_.find({pl.view, pl.id}); it != viewIssues_.end()) lv = std::max(lv, it->second);
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
            std::vector<Place> at;
            switch (r.ref) {
                case Row::Ref::Object:
                    if (!r.names.empty()) addUnique(at, loc.object(r.names.front(), r.view));
                    r.issue = std::max(r.issue, objectIssue(at));
                    break;
                case Row::Ref::Uses:
                    for (const auto& n : r.names) {
                        loc.usingName(n, at);
                        r.issue = std::max(r.issue, namedIssue(n));
                    }
                    for (const auto& t : r.types) {
                        loc.usingType(t, at);
                        r.issue = std::max(r.issue, namedIssue(t));
                    }
                    break;
                case Row::Ref::Ops: {
                    bool objects = false;
                    for (const auto op : r.ops) {
                        loc.ofOp(op, at);
                        objects = objects || (op < p->ops.size() && !projectLevel(p->ops[op]));
                    }
                    if (at.empty()) {
                        for (const auto& n : r.names) loc.usingName(n, at);
                        for (const auto& t : r.types) loc.usingType(t, at);
                    }
                    if (objects) r.issue = std::max(r.issue, objectIssue(at));
                    break;
                }
                case Row::Ref::Message:
                    if (!r.names.empty()) loc.ofMessage(r.names.front(), r.ops, r.issue == 2, at);
                    break;
                case Row::Ref::None:
                    break;
            }
            if (!at.empty()) {
                r.target = static_cast<int>(targets.size());
                targets.push_back(std::move(at));
            } else if (r.kind != Row::Kind::Item) {
                r.target = lastItem;   // le detail d'une ligne (son etat, ses changements) : ce qu'elle designe
            }
            if (r.kind == Row::Kind::Item) lastItem = r.target;
            // 1.22.6 : SES VUES - celles de ce qu'elle designe (dans l'ordre de la barre) ; sinon celle qu'elle nomme.
            r.views.clear();
            if (r.target >= 0)
                for (const auto& pl : targets[static_cast<std::size_t>(r.target)])
                    if (std::find(r.views.begin(), r.views.end(), pl.view) == r.views.end()) r.views.push_back(pl.view);
            std::sort(r.views.begin(), r.views.end());
            if (r.views.empty() && !r.view.empty())
                if (const auto k = loc.rankOf(r.view)) r.views.push_back(*k);
        }
        Row* head = nullptr;
        for (auto& r : rows) {
            if (r.kind == Row::Kind::Heading) head = &r;
            else if (head && r.issue > head->issue) head->issue = r.issue;
        }
    }
    // 1.22.6 : l'apercu, les reperes et les cadres de la vue montree (une autre qu'avant : la vue entiere).
    useShownView();
    if (body && shown_ != shownBefore) {
        body->zoom = 1.f;
        body->pan = {0.f, 0.f};
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
        body->relabel();   // 1.22.6 : << (12, 3 sur cette vue) >>
        if (!chosen.empty()) {
            for (std::size_t i = 0; i < body->rows.size(); ++i)
                if (body->rows[i].kind == chosenKind && body->rows[i].text == chosen) {
                    body->selected = static_cast<int>(i);
                    body->highlight = body->shownTargets(body->rows[i]);
                    break;
                }
        } else if (objectOnly) {
            body->highlight = std::move(kept);
        }
        // 1.22.7 : CE QUI VIENT D'ETRE CORRIGE - ses lignes de CORRECTIONS (mises en evidence un instant, une fois la
        // verification faite : la liste deroulee jusqu'a elles, leurs objets encadres - le cadre qui pulse de la 1.22.4).
        body->flashed.clear();
        for (std::size_t i = 0; i < body->rows.size(); ++i)
            if (body->rows[i].list == 3 && std::find(flashKeys_.begin(), flashKeys_.end(), body->rows[i].key) != flashKeys_.end())
                body->flashed.push_back(static_cast<int>(i));
        body->applyFilter();
        if (flashPending_ && !waiting_) flash();
    }
    // 1.22.7 : la bande ne parle que de corrections qui sont encore la (reprises ailleurs : elle s'en va) ; << (corrige
    // rapidement) >> de meme.
    if (!waiting_) {
        const auto present = [&](const std::string& k) {
            return std::any_of(corrections_.begin(), corrections_.end(), [&](const fixes::Correction& c) { return c.key == k; });
        };
        if (!bandKeys_.empty() && std::none_of(bandKeys_.begin(), bandKeys_.end(), present)) {
            bandText_.clear();
            bandUndo_.clear();
            bandKeys_.clear();
        }
        std::erase_if(quickKeys_, [&](const std::string& k) { return !present(k); });
        shownOnce_ = true;
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
    if (viewCount() > 0) shownName_ = viewName(shown_);   // 1.22.7 : la vue montree reste la meme
    const auto before = correctionState();
    const bool ok = panel_.assistant().chooseVariable(message_, missingName_, choice);
    panel_.sync();
    corrected(before, false, {});   // 1.22.7 : la bande, la ligne de CORRECTIONS mise en evidence
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
    for (const auto& c : corrections_)
        out.push_back(c.text + (quickKeys_.count(c.key) ? std::string(" (corrig\xC3\xA9 rapidement)") : std::string{}) + " | " + c.state);   // 1.22.7
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
    } else if (list == 4 && line < buildLines_.size()) {
        // 1.22.5 : un constat du logiciel - le menu d'une erreur (Retirer l'operation, Copier, Demander a l'IA) ; la demande a
        // l'IA porte la correction du logiciel quand il en a une. 1.22.7 : son rang dans le verdict (buildLines_ en est la
        // copie) - le menu offre la correction du logiciel, et Corriger rapidement la fait.
        const auto& f = buildLines_[line];
        const auto doc = panel_.assistant().host().document ? panel_.assistant().host().document() : nullptr;
        m = fixes::menuFor(*p, check_, buildLine(f, check_, p->ops.size(), line), doc ? &doc->project : nullptr);
        m.title = f.step + (f.error ? " : erreur" : " : avertissement");
        if (!f.fixFrom.empty())
            for (auto& act : m.actions)
                if (act.kind == fixes::Action::Kind::AskAi)
                    act.value += "\nLe logiciel propose de remplacer \xC2\xAB " + f.fixFrom + " \xC2\xBB par \xC2\xAB " + f.fixTo + " \xC2\xBB.";
    } else {
        return false;
    }
    // 1.22.7 : << Corriger rapidement >> tel qu'il a ete calcule pour la ligne (le meme que le lien des titres).
    if ((list == 1 || list == 2 || list == 4) && line < quickOf(list).size())
        for (auto& act : m.actions)
            if (act.kind == fixes::Action::Kind::Quick) act = quickOf(list)[line];
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

// ---- 1.22.5 : l'avis du logiciel ---------------------------------------------------------------------------------------
bool AiPreviewDialog::openBuildMenu(std::size_t line) {
    const auto* b = dynamic_cast<const AiPreviewBody*>(widgetRoot());
    return b && openFixMenu(4, line, menuPoint(b->rowRect(4, line), b->listRect()));
}

std::vector<std::string> AiPreviewDialog::buildRows() const {
    std::vector<std::string> out;
    for (const auto& f : buildLines_) out.push_back(buildRowText(f));
    return out;
}

void AiPreviewDialog::setVerdict(const build::Verdict& verdict) {
    verdict_ = verdict;
    verdictKnown_ = true;
    // Il vaut pour la proposition telle qu'elle est : une correction faite juste avant ne l'efface pas (refresh y verrait une
    // autre cle).
    if (const Proposal* p = panel_.assistant().proposal(message_); p && !key_.empty()) key_ = proposalKey(*p);
    if (widgetRoot()) refresh();   // la liste et ses reperes ; ni validation, ni apercu
}

// Le verdict garde dans la proposition (Proposal::extra << logiciel >>) : un nouveau se lit ; `forget` (la proposition a change
// depuis) : celui qu'on avait ne vaut plus, celui qui est garde non plus (il est d'avant) - le suivant se lira.
void AiPreviewDialog::adoptStoredVerdict(const Proposal& p, bool forget) {
    const json::Value* stored = p.extra.isObject() ? p.extra.find("logiciel") : nullptr;
    if (forget) {
        verdict_ = build::Verdict{};
        verdictKnown_ = false;
        verdictSeen_ = stored ? *stored : json::Value{};
        return;
    }
    if (!stored || *stored == verdictSeen_) return;
    verdictSeen_ = *stored;
    verdict_ = build::fromJson(*stored);
    verdictKnown_ = true;
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
        case K::QuickAll:   // 1.22.7 : le clic droit d'un titre
            (void)quickFixAll(act.ops.empty() ? 0 : static_cast<int>(act.ops.front()));
            return;
        default: {
            // 1.22.7 : LA FENETRE RESTE OUVERTE - la proposition change, la verification et l'apercu se refont (la vue montree
            // reste la meme), ce qui a change se montre (la bande, la ligne de CORRECTIONS, les objets encadres).
            if (viewCount() > 0) shownName_ = viewName(shown_);
            const auto before = correctionState();
            std::string said;
            (void)fixes::apply(panel_.assistant(), message_, act, &said);
            panel_.notice(said);
            panel_.sync();
            corrected(before, act.kind == K::Quick, {});
            return;
        }
    }
}

// ---- 1.22.7 : corriger rapidement, sans fermer -------------------------------------------------------------------------
const std::vector<fixes::Action>& AiPreviewDialog::quickOf(int list) const {
    static const std::vector<fixes::Action> none;
    return list == 1 ? quickErrors_ : list == 2 ? quickWarnings_ : list == 4 ? quickBuild_ : none;
}

fixes::Action AiPreviewDialog::quickFor(int list, std::size_t line) const {
    const auto& qs = quickOf(list);
    return line < qs.size() ? qs[line] : fixes::Action{};
}

std::size_t AiPreviewDialog::quickCount(int list) const {
    const auto& qs = quickOf(list);
    return static_cast<std::size_t>(std::count_if(qs.begin(), qs.end(), [](const fixes::Action& q) { return q.enabled; }));
}

bool AiPreviewDialog::lineOf(int list, std::size_t line, fixes::Line& out) const {
    const Proposal* p = panel_.assistant().proposal(message_);
    if (list == 1 && line < errorLines_.size()) out = errorLines_[line];
    else if (list == 2 && line < warningLines_.size()) out = warningLines_[line];
    else if (list == 4 && line < buildLines_.size() && p) out = buildLine(buildLines_[line], check_, p->ops.size(), line);
    else return false;
    return true;
}

std::map<std::string, std::string> AiPreviewDialog::correctionState() const {
    std::map<std::string, std::string> out;
    if (const Proposal* p = panel_.assistant().proposal(message_))
        for (const auto& c : fixes::corrections(*p)) out[c.key] = c.text + "|" + c.state;
    return out;
}

void AiPreviewDialog::corrected(const std::map<std::string, std::string>& before, bool quick, const std::string& headline) {
    // Ce qui est nouveau (ou change) dans les corrections : la bande le dit, Revenir le reprend, ses lignes s'eclairent.
    std::vector<std::string>   fresh;
    std::vector<fixes::Action> undo;
    std::string                line;
    if (const Proposal* p = panel_.assistant().proposal(message_))
        for (const auto& c : fixes::corrections(*p)) {
            const auto it = before.find(c.key);
            if (it != before.end() && it->second == c.text + "|" + c.state) continue;
            fresh.push_back(c.key);
            undo.push_back(c.undo);
            line = c.what == 0 ? c.text + " (\xC3\xA0 cr\xC3\xA9" "er)" : c.what == 2 ? c.text + " retir\xC3\xA9" "e" : c.text;
            if (quick) quickKeys_.insert(c.key);
            else quickKeys_.erase(c.key);
        }
    if (!fresh.empty()) {
        bandText_ = !headline.empty() ? headline
                    : fresh.size() == 1 ? "Corrig\xC3\xA9 : " + line + (quick ? " (corrig\xC3\xA9 rapidement)" : "")
                                        : "Corrig\xC3\xA9 : " + std::to_string(fresh.size()) + " corrections";
        bandUndo_ = std::move(undo);
        bandKeys_ = fresh;
        flashKeys_ = std::move(fresh);
        flashPending_ = true;
    } else {
        flashKeys_.clear();
        flashPending_ = false;
    }
    refresh();
}

void AiPreviewDialog::flash() {
    flashPending_ = false;
    auto* b = bodyOf(*this);
    if (!b || b->flashed.empty()) return;
    b->flashStart = -1.0;
    b->selected = b->flashed.front();
    b->highlight.clear();
    for (const int k : b->flashed)
        if (k >= 0 && static_cast<std::size_t>(k) < b->rows.size())
            for (const auto id : b->shownTargets(b->rows[static_cast<std::size_t>(k)])) addUnique(b->highlight, id);
    b->pulseStart = -1.0;
    b->show(b->flashed.front());   // son groupe deplie, la liste deroulee jusqu'a elle
    b->invalidate();
}

bool AiPreviewDialog::quickFix(int list, std::size_t line) {
    if (done_ || waiting_) return false;
    const fixes::Action q = quickFor(list, line);   // une copie : la liste se refait
    if (q.kind != fixes::Action::Kind::Quick || !q.enabled) return false;
    if (viewCount() > 0) shownName_ = viewName(shown_);
    const auto before = correctionState();
    std::string said;
    const bool ok = fixes::apply(panel_.assistant(), message_, q, &said);
    panel_.notice(said);
    panel_.sync();
    corrected(before, true, {});
    return ok;
}

std::size_t AiPreviewDialog::quickFixAll(int list) {
    if (done_ || waiting_) return 0;
    const std::vector<fixes::Action> qs = quickOf(list);   // une copie : la liste se refait
    std::vector<std::string> left;
    std::size_t usable = 0;
    for (std::size_t k = 0; k < qs.size(); ++k) {
        if (qs[k].enabled) {
            ++usable;
            continue;
        }
        fixes::Line l;
        if (lineOf(list, k, l)) left.push_back(shortOf(l, check_));
    }
    if (usable == 0) {
        panel_.notice("Rien \xC3\xA0 corriger rapidement ici : " + std::string(qs.empty() ? "aucune ligne." : "aucune correction s\xC3\xBBre (clic droit sur une ligne : pourquoi)."));
        return 0;
    }
    if (viewCount() > 0) shownName_ = viewName(shown_);
    const auto before = correctionState();
    std::string said;
    const std::size_t fixed = fixes::applyAll(panel_.assistant(), message_, qs, &said);
    panel_.notice(said);
    panel_.sync();
    // << 6 erreurs corrigees, 2 restent : << Vanne.F0 >>, Code brut... >>
    const bool many = fixed > 1;
    std::string headline = std::to_string(fixed) + (list == 1   ? (many ? " erreurs corrig\xC3\xA9" "es" : " erreur corrig\xC3\xA9" "e")
                                                    : list == 2 ? (many ? " avertissements corrig\xC3\xA9s" : " avertissement corrig\xC3\xA9")
                                                                : (many ? " constats corrig\xC3\xA9s" : " constat corrig\xC3\xA9"));
    const std::size_t rest = qs.size() - std::min(qs.size(), fixed);
    if (rest > 0) {
        headline += ", " + std::to_string(rest) + (rest > 1 ? " restent" : " reste");
        if (!left.empty()) {
            headline += " : " + left.front();
            if (left.size() > 1) headline += ", " + left[1];
            if (left.size() > 2) headline += ", \xE2\x80\xA6";
        }
        headline += " (clic droit : pourquoi)";
    }
    corrected(before, true, fixed > 0 ? headline : std::string{});
    return fixed;
}

bool AiPreviewDialog::openGroupMenu(int list, gfx::Point at) {
    const Proposal* p = panel_.assistant().proposal(message_);
    auto* root = widgetRoot();
    if (!p || !fixMenu_ || !root || done_ || waiting_ || (list != 1 && list != 2 && list != 4)) return false;
    const std::size_t n = quickCount(list);
    fixes::Menu m;
    m.title = list == 1 ? "Erreurs" : list == 2 ? "Avertissements" : "G\xC3\xA9n\xC3\xA9rer et Compiler";
    fixes::Action all;
    all.kind = fixes::Action::Kind::QuickAll;
    all.label = "Tout corriger rapidement";
    all.ops = {static_cast<std::size_t>(list)};
    all.enabled = n > 0 && p->state == "proposee";
    all.note = n > 0 ? std::to_string(n) + (n > 1 ? " corrections s\xC3\xBBres, en une fois" : " correction s\xC3\xBBre") + " (la proposition seule ; Revenir les reprend)."
                     : std::string("Aucune correction s\xC3\xBBre ici : clic droit sur une ligne pour savoir pourquoi.");
    if (!all.enabled) all.why = all.note;
    m.actions.push_back(std::move(all));
    m.actions.push_back(fixes::Action{});   // un separateur
    fixes::Action copy;
    copy.kind = fixes::Action::Kind::Copy;
    copy.label = "Copier les lignes";
    const auto& qs = quickOf(list);
    for (std::size_t k = 0; k < qs.size(); ++k) {
        fixes::Line l;
        if (lineOf(list, k, l)) copy.value += (copy.value.empty() ? "" : "\n") + l.text;
    }
    m.actions.push_back(std::move(copy));
    fixes::fill(*fixMenu_, m, fixActions_);
    fixMenu_->setBounds(root->bounds());
    gfx::Size surface = ui::surfaceSize();
    if (surface.w <= 0.f || surface.h <= 0.f) surface = {root->bounds().right() + 400.f, root->bounds().bottom() + 400.f};
    fixMenu_->openAt(at, surface);
    return true;
}

bool AiPreviewDialog::openHeadingMenu(int list) {
    const auto* b = bodyOf(*this);
    if (!b) return false;
    const gfx::Rect r = b->groupHeadingRect(list);
    const gfx::Rect l = b->listRect();
    return openGroupMenu(list, r.w > 0.f ? gfx::Point{r.x + 40.f, r.y + r.h / 2.f} : gfx::Point{l.x + l.w / 2.f, l.y + l.h / 2.f});
}

std::string AiPreviewDialog::menuWhy(const std::string& label) const {
    if (!fixMenu_ || !fixMenu_->isOpen()) return {};
    std::string found;
    std::function<void(const std::vector<ui::PopupMenu::Item>&, const std::string&)> walk = [&](const std::vector<ui::PopupMenu::Item>& items,
                                                                                               const std::string& path) {
        for (const auto& it : items) {
            if (it.separator || it.heading) continue;
            const std::string full = path.empty() ? it.label : path + " > " + it.label;
            if (it.children.empty()) {
                if (full == label && !it.enabled && found.empty()) found = it.disabledReason.empty() ? std::string("(gris\xC3\xA9" "e)") : it.disabledReason;
            } else {
                walk(it.children, full);
            }
        }
    };
    walk(fixMenu_->items(), {});
    return found;
}

std::string AiPreviewDialog::bandText() const {
    std::string t = bandText_;
    if (!t.empty() && !bandUndo_.empty()) t += " \xC2\xB7 Revenir";
    if (rechecking_) t += t.empty() ? std::string("Rev\xC3\xA9rification de la proposition sur un fil de fond\xE2\x80\xA6") : std::string(" \xC2\xB7 Rev\xC3\xA9rification\xE2\x80\xA6");
    return t;
}

bool AiPreviewDialog::undoBand() {
    if (bandUndo_.empty() || done_) return false;
    if (viewCount() > 0) shownName_ = viewName(shown_);
    const auto before = correctionState();
    const std::vector<fixes::Action> undo = std::move(bandUndo_);
    bandText_.clear();
    bandUndo_.clear();
    bandKeys_.clear();
    bool any = false;
    std::string said;
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) any = fixes::apply(panel_.assistant(), message_, *it, &said) || any;   // la derniere d'abord
    panel_.notice(any && undo.size() > 1 ? std::string("Corrections reprises : la proposition reprend sa forme d'avant (le projet n'a pas chang\xC3\xA9).") : said);
    panel_.sync();
    corrected(before, false, {});
    return any;
}

std::vector<std::string> AiPreviewDialog::flashedRows() const {
    std::vector<std::string> out;
    if (const auto* b = bodyOf(*this))
        for (const int k : b->flashed)
            if (k >= 0 && static_cast<std::size_t>(k) < b->rows.size()) out.push_back(b->rows[static_cast<std::size_t>(k)].text);
    return out;
}

gfx::Rect AiPreviewDialog::bandUndoRect() const {
    const auto* b = bodyOf(*this);
    return b ? b->bandUndoRect() : gfx::Rect{};
}

gfx::Rect AiPreviewDialog::quickAllRect(int list) const {
    const auto* b = bodyOf(*this);
    return b ? b->quickAllRect(list) : gfx::Rect{};
}

void AiPreviewDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    const Proposal* p = panel_.assistant().proposal(message_);
    // Appliquee (apres la confirmation d'une suppression), refusee, ou partie : la fenetre se ferme.
    if (!p || p->state != "proposee") {
        finish(p && p->state == "appliquee");
        return;
    }
    pollViews();   // 1.22.6 : les vues calculees sur un fil de fond arrivent
    // 1.22.1 : << Choisir une variable... >> (le clic droit) - la liste du haut s'ouvre, sur la variable demandee.
    if (openChoice_ && variable_ && variable_->visibility() == ui::Visibility::Visible) {
        openChoice_ = false;
        if (auto* root = widgetRoot()) root->layout();
        const gfx::Rect at = variable_->bounds();
        if (at.w > 0.f && !variable_->isOpen())
            (void)variable_->dispatch(ui::MouseDown{{at.x + at.w / 2.f, at.y + at.h / 2.f}, ui::MouseButton::Left, 1, {}});
    }
    const std::string key = proposalKey(*p);
    const json::Value* stored = p->extra.isObject() ? p->extra.find("logiciel") : nullptr;   // 1.22.5 : un verdict neuf garde
    if (key != key_ || (stored && !(*stored == verdictSeen_))) refresh();
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
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && !(variable_ && variable_->isOpen())
                                                       && !(fixMenu_ && fixMenu_->isOpen()) && !viewMenuOpen()) {
        // 1.22.4 : une recherche tapee, une puce choisie : Echap remontre tout (la fenetre reste) ; sinon elle se ferme.
        if (auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot()); b && b->filtering) {
            b->onlyErrors = b->onlyWarnings = b->onlyView = false;
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
    // 1.22.6 : PageUp / PageDown, Ctrl+gauche / droite : la vue precedente, la suivante - pas dans une liste ouverte, ni dans
    // la recherche pour Ctrl+fleche (elle y saute un mot).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev);
        k && viewCount() > 1 && !(variable_ && variable_->isOpen()) && !(fixMenu_ && fixMenu_->isOpen()) && !viewMenuOpen()) {
        const bool page = (k->key == ui::Key::PageUp || k->key == ui::Key::PageDown) && !k->mods.ctrl && !k->mods.alt && !k->mods.shift;
        const bool arrow = (k->key == ui::Key::Left || k->key == ui::Key::Right) && k->mods.ctrl && !k->mods.alt && !k->mods.shift
                           && !(search_ && search_->field().focused());
        if (page || arrow) {
            (void)stepView(k->key == ui::Key::PageUp || k->key == ui::Key::Left ? -1 : 1);
            return ui::EventResult::Consumed;
        }
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
