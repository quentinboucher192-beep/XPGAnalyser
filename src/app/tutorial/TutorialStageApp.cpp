#include "TutorialStageApp.hpp"

#include <algorithm>
#include <cmath>

namespace app {
namespace {

using help::GestureKind;

gfx::Point lerp(gfx::Point a, gfx::Point b, double t) {
    const auto f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
}

// L'octet ou commence la lettre n (UTF-8) ; la taille du texte au-dela.
std::size_t letterOffset(const std::string& s, std::size_t n) {
    std::size_t letters = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) continue;
        if (letters == n) return i;
        ++letters;
    }
    return s.size();
}

std::size_t letterCount(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// Un geste qui ne fait que montrer : il se joue sans attendre d'image.
// La bulle : le `code` de l'auteur sans ses marques (vu sur la session 82) ; tranche 9 :
// le **gras** garde les siennes, le calque le dessine (TutorialOverlay, drawMarked).
std::string plainText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '`') continue;
        out += s[i];
    }
    return out;
}

bool showsOnly(GestureKind k) {
    return k == GestureKind::Spot || k == GestureKind::Say || k == GestureKind::Wait || k == GestureKind::Run;
}

// Tranche 11 : une cible qui se ferme d'elle-meme - la liste de l'aide a la saisie, un menu.
// Son encadre s'efface avec elle, pour de bon : sur script-rampe, celui de saisie:Pos restait
// sur le code, a la place de la liste fermee par Entree, jusqu'a la fin de l'etape.
// Tranche 23 (R111-15) : TOUT encadre suit sa cible a chaque image (help::TutorialSpot, frame()) ;
// une cible qui n'est pas fugace s'efface quand elle sort de la vue, et revient avec elle.
bool fleeting(std::string_view target) {
    return target == "aide-saisie" || target.rfind("saisie:", 0) == 0 || target.rfind("menu:", 0) == 0;
}

// Tranche 13 : une cible de taille nulle n'est pas encore placee. Une page qui vient de
// s'ouvrir, rejouee sans animation (seek, le verificateur), n'a pas eu son premier dessin :
// ses widgets sont en (0, 0, 0, 0). L'encadre (ou le clic) allait alors au coin de l'ecran
// (session 89, capture 12 : un carre orange en haut a gauche, sous le voile, a la fin de
// l'etape 7 de l'expression). On attend comme pour une cible absente.
// Les cibles point (ecran:X,Y, vue:X,Y) font 2 x 2 : elles passent.
bool placed(const std::optional<gfx::Rect>& r) { return r && r->w > 0.f && r->h > 0.f; }

} // namespace

void TutorialStageApp::reset(const help::CompiledTutorial& tutorial) {
    tutorial_ = &tutorial;
    queue_.clear();
    spot_.clear();
    say_.clear();
    key_.clear();
    clickFlash_ = 0;
    interactive_ = false;
    settle_ = 0;
    // Une copie NEUVE du bac a sable : l'appli la fait (peut-etre en plusieurs images).
    sandboxPending_ = openSandbox_ && !openSandbox_(tutorial);
}

void TutorialStageApp::advance(const help::Gesture& gesture, double from, double to, bool animated) {
    queue_.push_back({gesture, from, to, animated, 0, false, {}});
}

void TutorialStageApp::setInteractive(bool on) {
    interactive_ = on;
    // 1.11.2 (le LISEZ-MOI de la 1.11.0 : « dans un tutoriel d'objet, l'A toi ne te montre pas la tuile a
    // glisser : la bibliotheque est revenue en haut ») : la remise en place (seek, deja en file) rouvre une
    // copie neuve, la bibliotheque en haut. Apres elle, dans l'ordre de la file, un encadrer de la tuile :
    // locate la fait defiler jusqu'a l'ecran, l'encadre la suit ensuite (R111-15), sans voile en A toi.
    if (!on || !tutorial_ || shownStep_ >= tutorial_->steps.size()) return;
    const auto& aTry = tutorial_->steps[shownStep_].aTry;
    if (!aTry || aTry->show.empty()) return;
    Pending p;
    p.gesture.kind = help::GestureKind::Spot;
    p.gesture.target = aTry->show;
    p.gesture.line = tutorial_->steps[shownStep_].line;
    p.animated = true;   // pas une remise en place : la bulle reste la consigne, pas « Remise en place… »
    queue_.push_back(std::move(p));
}

void TutorialStageApp::showStep(const help::CompiledTutorial& tutorial, std::size_t step) {
    shownStep_ = step;
    const std::string bubble = step < tutorial.steps.size() ? plainText(tutorial.steps[step].bubble) : std::string();
    // Apres les gestes deja en file (les etapes d'avant, rejouees par seek) : sinon
    // leur encadrer et leur dire passeraient APRES la bulle de cette etape (vu sur
    // la session 82 : l'encadre de l'etape 2 restait a l'etape 4).
    if (!queue_.empty()) {
        Pending mark;
        mark.stepMark = true;
        mark.bubble = bubble;
        queue_.push_back(std::move(mark));
        return;
    }
    spot_.clear();
    key_.clear();
    say_ = bubble;
}

std::optional<std::string> TutorialStageApp::read(std::string_view path) {
    return reader_ ? reader_(path) : std::nullopt;
}

std::optional<gfx::Rect> TutorialStageApp::find(const std::string& target, Pending& p) {
    std::string why;
    auto r = driver_.locate(target, &why);
    if (placed(r)) return r;
    if (r) why = "pas encore plac\xC3\xA9" "e (taille nulle)";
    if (++p.retries < kRetryFrames) return std::nullopt;
    missing_.push_back(target + " (ligne " + std::to_string(p.gesture.line) + ") : " + why);
    return std::nullopt;
}

void TutorialStageApp::frame() {
    if (clickFlash_ > 0) --clickFlash_;
    if (sandboxPending_) {
        sandboxPending_ = openSandbox_ && !openSandbox_(*tutorial_);
        if (sandboxPending_) return;
        // Tranche 25 (R111-12, vu au verificateur) : le bac vient de s'ouvrir, son ecran d'analyse
        // n'a encore ni mise a jour ni dessin. Le premier geste joue tout de suite s'y perdait :
        // apres une remise a neuf (tutoriel-aller, « A toi » d'une etape plus loin), « clic barre:? »
        // n'ouvrait pas le menu Aide (menu:Aide introuvable), et l'« A toi » « Ouvre le centre
        // d'aide » lisait centre.ouvert = non. On laisse passer deux images avant de jouer.
        settle_ = kSettleFrames;
    }
    if (settle_ > 0) {
        --settle_;
        return;
    }
    // Tranche 23 (R111-15, decision 43) : l'encadre SUIT SA CIBLE a chaque image, quelle qu'elle
    // soit (avant : seulement les fugaces). Il la retrouve elle-meme, sans rien defiler ni deplier
    // (UiDriver::follow) : il ne garde pas sa place. Sur api-ordre, le clic sur API > Ordre
    // d'execution ajoute "Recents" en tete de l'arbre, et l'encadre restait sur Blocs DFB (R145_08).
    spot_.follow([this](const std::string& target) { return driver_.follow(target); });
    // Ce qui ne fait que montrer passe d'un coup ; puis UN geste qui envoie de l'entree.
    while (!queue_.empty()) {
        Pending& head = queue_.front();
        if (head.stepMark) {
            spot_.clear();
            key_.clear();
            say_ = head.bubble;
            queue_.pop_front();
            continue;
        }
        const bool cheap = showsOnly(head.gesture.kind);
        if (!play(head)) {
            if (head.retries >= kRetryFrames) queue_.pop_front();   // introuvable : compte, on passe
            return;
        }
        queue_.pop_front();
        if (!cheap) return;
    }
}

bool TutorialStageApp::play(Pending& p) {
    const auto& g = p.gesture;
    const bool ends = p.to >= 1.0 && p.from < 1.0;
    switch (g.kind) {
    case GestureKind::Spot: {
        if (p.from > 0) return true;
        const auto r = find(g.target, p);
        if (!r) return false;
        spot_.show(g.target, *r, fleeting(g.target));
        return true;
    }
    case GestureKind::Say: say_ = plainText(g.text); return true;
    case GestureKind::Wait:
    case GestureKind::Run: return true;
    case GestureKind::Prepare:
        // Tranche 14 : une action inconnue ou refusee (action <identifiant>) est nommee.
        if (!prepare_)
            missing_.push_back((g.args.empty() ? std::string("?") : g.args.front()) + " (ligne " + std::to_string(g.line)
                               + ") : pr\xC3\xA9paration pas encore branch\xC3\xA9" "e");
        else if (!prepare_(g.args))
            missing_.push_back((g.args.empty() ? std::string("?") : g.args.front())
                               + (g.args.size() > 1 && g.args.front() == "action" ? " " + g.args[1] : std::string())
                               + " (ligne " + std::to_string(g.line) + ") : pr\xC3\xA9paration impossible");
        return true;
    case GestureKind::Move:
    case GestureKind::Click: {
        const auto r = find(g.target, p);
        if (!r) return false;
        const gfx::Point at = centreOf(*r);
        if (p.animated && !ends) { cursor_ = lerp(cursor_, at, p.to); driver_.moveTo(cursor_); return true; }
        cursor_ = at;
        if (g.kind == GestureKind::Move || !ends) { driver_.moveTo(at); return true; }
        driver_.click(at, g.rightClick ? ui::MouseButton::Right : ui::MouseButton::Left, g.doubleClick ? 2 : 1);
        // Tranche 9 : le carre orange du clic seulement quand on le voit jouer. Rejoue sans
        // animation (seek : "Aller a", le verificateur), il restait sur les captures prises
        // deux images apres le dernier clic, pres du curseur ou dans la vue en marche.
        if (p.animated) clickFlash_ = 12;
        return true;
    }
    case GestureKind::Drag: {
        const auto a = find(g.target, p);
        if (!a) return false;
        const auto b = find(g.target2, p);
        if (!b) return false;
        const gfx::Point pa = centreOf(*a), pb = centreOf(*b);
        if (p.from <= 0) { driver_.press(pa); dragFrom_ = pa; }
        cursor_ = lerp(pa, pb, p.to);
        driver_.moveTo(cursor_);
        if (ends) driver_.release();
        return true;
    }
    case GestureKind::Type: {
        // Les lettres de from a to ; au debut, la case est cliquee et tout y est choisi.
        if (p.from <= 0 && !g.target.empty()) {
            const auto r = find(g.target, p);
            if (!r) return false;
            cursor_ = centreOf(*r);
            driver_.click(cursor_);
            driver_.key(ui::Key::A, ui::KeyMods{true, false, false, false});
        }
        const auto n = static_cast<double>(letterCount(g.text));
        const auto i0 = letterOffset(g.text, static_cast<std::size_t>(std::floor(p.from * n + 1e-9)));
        const auto i1 = letterOffset(g.text, static_cast<std::size_t>(std::floor(p.to * n + 1e-9)));
        if (i1 > i0) driver_.text(g.text.substr(i0, i1 - i0));
        return true;
    }
    case GestureKind::Key: {
        if (!ends) return true;
        ui::Key k{};
        ui::KeyMods m{};
        if (!UiDriver::parseKey(g.text, k, m)) {
            missing_.push_back("touche " + g.text + " (ligne " + std::to_string(g.line) + ") : inconnue");
            return true;
        }
        key_ = g.text;
        driver_.key(k, m);
        return true;
    }
    }
    return true;
}

} // namespace app
