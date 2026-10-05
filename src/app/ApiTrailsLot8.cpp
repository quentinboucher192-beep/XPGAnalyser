// =============================================================================
//  app/ApiTrailsLot8.cpp - Lot API 8 : didacticiels et aide
// -----------------------------------------------------------------------------
//  LES ETAPES DES HUIT PARCOURS DU LOT 8 (le catalogue : TutorialsLot8.hpp ;
//  le moteur et l'onglet : ApiTrails.cpp, TutorialWorkspace.cpp). Les textes
//  reprennent ceux des visites de la maquette validee (maquette-lot8.html),
//  sur les vrais ecrans : le dossier Simulation de l'arbre, la barre du haut
//  (Simuler, Arreter, Un cycle, Affichage), les onglets du Centre de
//  simulation ("analysis.sim.<cle>"), les entrees de l'IHM.
//
//  Ce qui attend un geste (et le verifie) : la simulation en marche, en pause,
//  qui continue - Montre-moi le fait par les memes commandes que la barre. Le
//  reste est une visite : l'etape ouvre le bon ecran, la bulle le montre.
//  Un dialogue (Renommer, la galerie des themes, le depot de fichiers) ne
//  s'ouvre pas tout seul : l'etape montre ou il s'ouvre, et le dit.
//
//  F1 SUR UN NOUVEL ECRAN : un onglet du dossier Simulation ouvre sa page de
//  l'aide generale (lot8::helpAnchorFor) ; l'ecran d'aide y va.
// =============================================================================
#include "ApiTrails.hpp"
#include "App.hpp"
#include "TopBar.hpp"
#include "TutorialsLot8.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace app {

using NK = ProjectTreeModel::NodeKind;
using Step = HmiTutorial::Step;
using Rect = gfx::Rect;

struct MainAnalysisScreen::ApiTrails::Lot8::Kit {
    using Screen = MainAnalysisScreen;

    static bool shown(const ui::Widget* w) {
        if (!w) return false;
        for (const ui::Widget* at = w; at; at = at->parent())
            if (!at->visible()) return false;
        return true;
    }
    // Dans la fenetre de la bulle (pas une fenetre detachee).
    static bool onScreen(Screen& s, const ui::Widget* w) {
        if (!shown(w)) return false;
        const ui::Widget* top = w;
        while (top->parent()) top = top->parent();
        return top == s.widgetRoot();
    }
    static bool nodeRow(Screen& s, ui::NodeId node, Rect& r) { return s.explorer_ && node != ui::kInvalidNode && s.explorer_->rowRect(node, r); }
    // Un enfant d'un dossier de la racine (Simulation, IHM), par son genre ;
    // le dossier lui-meme sinon.
    static ui::NodeId childOf(Screen& s, ui::NodeId folder, NK kind) {
        if (!s.treeModel_ || folder == ui::kInvalidNode) return ui::kInvalidNode;
        for (std::size_t k = 0; k < s.treeModel_->childCount(folder); ++k)
            if (ProjectTreeModel::kindOf(s.treeModel_->childAt(folder, k)) == kind) return s.treeModel_->childAt(folder, k);
        return folder;
    }
    static ui::NodeId simNode(Screen& s, NK kind) {
        const auto folder = ProjectTreeModel::simFolderNode();
        return kind == NK::SimFolder ? folder : childOf(s, folder, kind);
    }
    static ui::NodeId hmiNode(Screen& s, NK kind) {
        if (!s.treeModel_ || !s.treeModel_->hasHmi()) return ui::kInvalidNode;
        return childOf(s, ProjectTreeModel::hmiFolderNode(), kind);
    }
    static ui::NodeId apiNode(NK kind) { return ProjectTreeModel::pack(kind, 0); }
    // Lot API 8 : l'arbre du projet - un outil sorti de l'arbre (Compiler, Generer) :
    // son bouton dans la rangee sous le titre du domaine.
    static std::function<bool(Rect&)> row(Screen& s, std::function<ui::NodeId()> node) {
        return [&s, node](Rect& r) { return s.treeToolRect(node(), r) || nodeRow(s, node(), r); };
    }
    static std::function<void()> reveal(Screen& s, std::function<ui::NodeId()> node, bool expand = false) {
        return [&s, node, expand] {
            const auto n = node();
            if (s.revealTreeTool(n)) return;
            (void)s.revealTreeNode(n);
            if (expand && s.explorer_ && n != ui::kInvalidNode) s.explorer_->expand(n);
        };
    }
    static std::function<bool(Rect&)> bar(Screen& s, std::string part) {
        return [&s, part](Rect& r) {
            if (!s.topBar_) return false;
            r = s.topBar_->partRect(part);
            return !r.empty();
        };
    }
    // Un onglet par l'identifiant de son cadre ("analysis.sim.ensemble"), s'il se voit.
    static std::function<bool(Rect&)> page(Screen& s, std::string id) {
        return [&s, id](Rect& r) {
            ui::Widget* root = s.widgetRoot();
            ui::Widget* w = root ? root->findById(id) : nullptr;
            if (!onScreen(s, w)) return false;
            r = w->bounds();
            return !r.empty();
        };
    }
    static std::function<void()> openSim(Screen& s, std::string key) {
        return [&s, key] { (void)s.openSimCenter(key); };
    }
    static SimulationHost::State state(Screen& s) {
        return s.app_.simulation().attached() ? s.app_.simulation().state() : SimulationHost::State::Stopped;
    }
    static bool running(Screen& s) { return state(s) == SimulationHost::State::Running; }
    static bool paused(Screen& s) { return state(s) == SimulationHost::State::Paused; }

    static std::vector<Step> simulate(Screen& s);
    static std::vector<Step> debug(Screen& s);
    static std::vector<Step> pause(Screen& s);
    static std::vector<Step> drop(Screen& s);
    static std::vector<Step> themes(Screen& s);
    static std::vector<Step> rename(Screen& s);
    static std::vector<Step> expressions(Screen& s);
    static std::vector<Step> filters(Screen& s);
};


// ============================================================ Simuler et suivre
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::simulate(Screen& s) {
    std::vector<Step> st;
    const auto folder = [&s] { return simNode(s, NK::SimFolder); };
    st.push_back({"Un nouveau dossier : Simulation",
                  "Il est au m\xC3\xAAme niveau que API, IHM et Versions. Tout ce qui concerne la simulation est l\xC3\xA0 : avant, c'\xC3\xA9tait "
                  "\xC3\xA9parpill\xC3\xA9 entre API \xE2\x80\xBA Simulation et IHM \xE2\x80\xBA Simulation (ce sont maintenant Simulation \xE2\x80\xBA Automate et Simulation \xE2\x80\xBA IHM).\n"
                  "La pastille \xC3\xA0 droite dit en un mot o\xC3\xB9 on en est : en marche, pause, arr\xC3\xAAt, d\xC3\xA9" "faut.",
                  row(s, folder), reveal(s, folder, true)});
    st.push_back({"La phrase qui dit tout",
                  "Simulation \xE2\x80\xBA Vue d'ensemble (F9 l'ouvre) : le bandeau r\xC3\xA9pond \xC3\xA0 \xC2\xAB est-ce que \xC3\xA7" "a tourne ? \xC2\xBB en une phrase, avec "
                  "une couleur : vert tout va bien, bleu en pause, orange point d'arr\xC3\xAAt, rouge l'automate est arr\xC3\xAAt\xC3\xA9. Le num\xC3\xA9ro de "
                  "cycle avance sous tes yeux.",
                  page(s, "analysis.sim.ensemble"), openSim(s, "ensemble")});
    Step run{"Les commandes sont toujours l\xC3\xA0",
             "Dans la barre du haut, quel que soit l'onglet : Simuler (F5), Pause, Un cycle, Arr\xC3\xAAter (Maj+F5), et l'\xC3\xA9tat. Un clic "
             "sur l'\xC3\xA9tat ram\xC3\xA8ne \xC3\xA0 la Vue d'ensemble.\nClique sur Simuler (ou F5).",
             bar(s, "simuler"), {}};
    run.done = [&s](std::string& what) {
        if (!running(s) && !paused(s)) return false;
        what = "la simulation est en marche";
        return true;
    };
    run.showMe = [&s] { if (!running(s)) s.runSimulationTransport("sim.run"); };
    run.waiting = "J'attends la simulation en marche (Simuler, ou F5)\xE2\x80\xA6";
    st.push_back(std::move(run));
    Step pause{"Tout s'arr\xC3\xAAte ensemble",
               "Clique sur Pause : le bandeau passe au bleu et dit pourquoi. L'automate, l'IHM et les \xC3\xA9quipements attendent tous "
               "au m\xC3\xAAme cycle. Un cycle en fait passer un seul : le compteur fait +1.",
               bar(s, "simuler"), {}};
    pause.done = [&s](std::string& what) {
        if (!paused(s)) return false;
        what = "la simulation est en pause";
        return true;
    };
    pause.showMe = [&s] {
        if (!running(s) && !paused(s)) s.runSimulationTransport("sim.run");
        if (!paused(s)) s.runSimulationTransport("sim.pause");
    };
    pause.waiting = "J'attends la pause (Pause, dans la barre du haut)\xE2\x80\xA6";
    st.push_back(std::move(pause));
    st.push_back({"Ce qui m\xC3\xA9rite ton attention",
                  "La liste des probl\xC3\xA8mes, du plus grave au moins grave, chacun avec le bouton qui le r\xC3\xA8gle. Une ligne dispara\xC3\xAEt "
                  "quand c'est r\xC3\xA9gl\xC3\xA9. La cha\xC3\xAEne Automate \xE2\x86\x92 IHM \xE2\x86\x92 \xC3\xA9quipements montre les \xC3\xA9" "changes en direct : quand un maillon "
                  "casse, il devient rouge ici.",
                  page(s, "analysis.sim.ensemble"), openSim(s, "ensemble")});
    st.push_back({"For\xC3\xA7" "ages, courbes, journal",
                  "Simulation \xE2\x80\xBA For\xC3\xA7" "ages : tout ce qui est forc\xC3\xA9, de l'automate et des \xC3\xA9quipements, et Tout rel\xC3\xA2" "cher. Courbes : les "
                  "variables suivies. Journal : ce qui s'est pass\xC3\xA9, cycle par cycle - les pauses, les points d'arr\xC3\xAAt, les "
                  "modifications \xC3\xA0 chaud.",
                  [&s](Rect& r) {
                      Rect a{}, b{};
                      const bool ok = nodeRow(s, simNode(s, NK::SimForcing), a);
                      const bool ok2 = nodeRow(s, simNode(s, NK::SimJournal), b);
                      if (!ok && !ok2) return false;
                      r = !ok ? b : !ok2 ? a : Rect{a.x, a.y, std::max(a.x + a.w, b.x + b.w) - a.x, b.y + b.h - a.y};
                      return !r.empty();
                  },
                  reveal(s, [&s] { return simNode(s, NK::SimForcing); })});
    st.push_back({"La suite : d\xC3\xA9" "boguer",
                  "Relance avec Continuer (ou F5) ; Arr\xC3\xAAter (Maj+F5) remet tout \xC3\xA0 z\xC3\xA9ro. Le d\xC3\xA9" "bogage a son propre parcours : "
                  "points d'arr\xC3\xAAt, pas \xC3\xA0 pas, \xC2\xAB qui a \xC3\xA9" "crit cette variable ? \xC2\xBB (D\xC3\xA9" "boguer pas \xC3\xA0 pas).",
                  row(s, [&s] { return simNode(s, NK::SimDebug); }), reveal(s, [&s] { return simNode(s, NK::SimDebug); })});
    return st;
}

// ============================================================ Deboguer pas a pas
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::debug(Screen& s) {
    std::vector<Step> st;
    const auto dbg = page(s, "analysis.sim.debogage");
    st.push_back({"Simulation \xE2\x80\xBA D\xC3\xA9" "bogage",
                  "L'onglet du d\xC3\xA9" "bogage : les points d'arr\xC3\xAAt, les espions, pourquoi la simulation s'est arr\xC3\xAAt\xC3\xA9" "e, et qui a \xC3\xA9" "crit "
                  "chaque espion. F9 ouvre la Vue d'ensemble ; ici, tout ce qui sert \xC3\xA0 chercher.",
                  dbg, [&s] {
                      (void)s.revealTreeNode(simNode(s, NK::SimDebug));
                      (void)s.openSimCenter("debogage");
                  }});
    // Le geste attendu : un point d'arret pose (le journal du debogage en dit
    // un de plus, "[point-arret] ...", depuis l'arrivee sur l'etape).
    auto mark = std::make_shared<std::pair<std::size_t, std::string>>();
    Step pose{"Pose un point d'arr\xC3\xAAt",
              "Ouvre une section (un double-clic dans l'arbre) et clique dans la marge, juste \xC3\xA0 gauche du num\xC3\xA9ro de ligne : un "
              "point rouge appara\xC3\xAEt (F9 sur la ligne du curseur fait pareil, Ctrl+F9 l'active ou le d\xC3\xA9sactive).\nSans "
              "condition, la simulation s'arr\xC3\xAAtera au prochain passage sur cette ligne, donc au prochain cycle.",
              row(s, [] { return apiNode(NK::UnitsFolder); }),
              [&s, mark] {
                  const auto events = s.debugEvents();
                  mark->first = events.size();
                  mark->second = events.empty() ? std::string() : events.back();
              }};
    pose.done = [&s, mark](std::string& what) {
        const auto events = s.debugEvents();
        if (events.empty() || (events.size() == mark->first && events.back() == mark->second)) return false;
        if (events.back().rfind("[point-arret]", 0) != 0) return false;
        what = "un point d'arr\xC3\xAAt est pos\xC3\xA9";
        return true;
    };
    pose.waiting = "J'attends un point d'arr\xC3\xAAt (un clic dans la marge du code d'une section)\xE2\x80\xA6";
    st.push_back(std::move(pose));
    st.push_back({"La simulation s'est arr\xC3\xAAt\xC3\xA9" "e dessus",
                  "Le bandeau dit o\xC3\xB9 (la section, la ligne) et quand (le cycle). La ligne est surlign\xC3\xA9" "e ; au survol, chaque "
                  "variable montre sa valeur. L'IHM et les \xC3\xA9quipements sont en pause eux aussi : le temps simul\xC3\xA9 est arr\xC3\xAAt\xC3\xA9.",
                  dbg, openSim(s, "debogage")});
    st.push_back({"Avance d'une section",
                  "Section suivante (F10) ex\xC3\xA9" "cute toute la section et s'arr\xC3\xAAte au d\xC3\xA9" "but de la suivante, dans l'ordre de MAST. En "
                  "bas, la trace du cycle montre o\xC3\xB9 tu en es : les sections pass\xC3\xA9" "es en bleu, celle o\xC3\xB9 tu es en orange, celles qui "
                  "restent en gris.",
                  dbg, {}});
    st.push_back({"Ne t'arr\xC3\xAAte que quand \xC3\xA7" "a compte",
                  "Un point d'arr\xC3\xAAt sans condition arr\xC3\xAAte \xC3\xA0 chaque cycle : c'est vite p\xC3\xA9nible. Dans la liste des points "
                  "d'arr\xC3\xAAt, un double-clic sur sa Condition la modifie : tape par exemple armoires[0].etat = 6 puis Entr\xC3\xA9" "e ; la "
                  "simulation ne s'arr\xC3\xAAte que quand elle est vraie.",
                  dbg, {}});
    st.push_back({"Pourquoi ici, et qui a \xC3\xA9" "crit quoi",
                  "Pourquoi la simulation s'est arr\xC3\xAAt\xC3\xA9" "e (la condition, sa valeur, le num\xC3\xA9ro de passage), puis qui a \xC3\xA9" "crit l'espion "
                  "choisi : la section, la ligne, le cycle, avec un bouton pour y aller. Une variable forc\xC3\xA9" "e : \xC2\xAB personne : elle "
                  "est forc\xC3\xA9" "e \xC2\xBB.",
                  dbg, {}});
    st.push_back({"Repars",
                  "Continuer (F5) : la simulation tourne jusqu'au prochain arr\xC3\xAAt ; Maj+F5 l'arr\xC3\xAAte. En pause, tu peux aussi "
                  "modifier le projet : le parcours \xC2\xAB Modifier pendant une pause \xC2\xBB le montre.",
                  bar(s, "simuler"), {}});
    return st;
}

// ============================================================ Modifier pendant une pause
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::pause(Screen& s) {
    std::vector<Step> st;
    Step hold{"Mets en pause",
              "Lance la simulation (Simuler, ou F5), puis Pause : l'automate, l'IHM et les \xC3\xA9quipements attendent au m\xC3\xAAme cycle.",
              bar(s, "simuler"), {}};
    hold.done = [&s](std::string& what) {
        if (!paused(s)) return false;
        what = "la simulation est en pause";
        return true;
    };
    hold.showMe = [&s] {
        if (!running(s) && !paused(s)) s.runSimulationTransport("sim.run");
        if (!paused(s)) s.runSimulationTransport("sim.pause");
    };
    hold.waiting = "J'attends la simulation en pause (Simuler, puis Pause)\xE2\x80\xA6";
    st.push_back(std::move(hold));
    st.push_back({"Modifier sans repartir de z\xC3\xA9ro",
                  "En pause, tu peux modifier le projet : une valeur, une ligne de code, une variable. Au prochain Continuer, la "
                  "simulation reprend au m\xC3\xAAme cycle avec ta modification ; avant, elle repartait du cycle 0.",
                  row(s, [] { return apiNode(NK::VariablesFolder); }), {}});
    Step go{"Continuer",
            "Clique sur Continuer (ou F5).",
            bar(s, "simuler"), {}};
    go.done = [&s](std::string& what) {
        if (!running(s)) return false;
        what = "la simulation a repris";
        return true;
    };
    go.showMe = [&s] { if (!running(s)) s.runSimulationTransport("sim.run"); };
    go.waiting = "J'attends Continuer (F5)\xE2\x80\xA6";
    st.push_back(std::move(go));
    st.push_back({"C'est tout",
                  "Le compteur de cycle n'est pas revenu \xC3\xA0 0 : la modification a \xC3\xA9t\xC3\xA9 appliqu\xC3\xA9" "e \xC3\xA0 chaud. Le journal le note "
                  "(Simulation \xE2\x80\xBA Journal).",
                  row(s, [&s] { return simNode(s, NK::SimJournal); }), reveal(s, [&s] { return simNode(s, NK::SimJournal); })});
    return st;
}

// ============================================================ Glisser un fichier
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::drop(Screen& s) {
    std::vector<Step> st;
    const auto resources = [&s] { return hmiNode(s, NK::HmiResources); };
    st.push_back({"Glisse un fichier sur la fen\xC3\xAAtre",
                  "Depuis ton bureau ou l'Explorateur, glisse un fichier n'importe o\xC3\xB9 sur l'appli : l'appli te dit tout ce qu'elle "
                  "peut en faire. Un .XPG ou un .XHW : l'importer (le r\xC3\xA9" "capitulatif d'abord) ou l'ouvrir comme un projet s\xC3\xA9par\xC3\xA9.",
                  {}, {}});
    st.push_back({"Tout ce qu'on peut en faire",
                  "Un classeur : une ligne par possibilit\xC3\xA9, avec son d\xC3\xA9tail - combien de variables, quelles colonnes sont "
                  "reconnues, ce qui sera mis \xC3\xA0 jour. Chaque import se d\xC3\xA9" "fait avec Ctrl+Z. Les lignes s\xC3\xBBres sont d\xC3\xA9j\xC3\xA0 coch\xC3\xA9" "es ; "
                  "les autres attendent ton choix.",
                  {}, {}});
    st.push_back({"Les lignes impossibles restent l\xC3\xA0",
                  "Gris\xC3\xA9" "es, avec leur raison (il manque un onglet du classeur ; un classeur n'est pas une ressource) : tu sais "
                  "pourquoi sans chercher. En bas \xC3\xA0 gauche, une phrase dit ce qui va se passer, dans l'ordre ; puis Faire.",
                  {}, {}});
    st.push_back({"Et un PDF, une image, un son ?",
                  "Pour un fichier qui n'est pas un classeur, il n'y a que deux choix, deux cases par fichier : Ressources, une "
                  "copie dans le projet, qui voyage avec lui ; Fichiers externes, un lien vers le fichier d'origine, sans copie. "
                  "Les deux se cochent.",
                  row(s, resources), reveal(s, resources)});
    st.push_back({"Les nouveaux sont marqu\xC3\xA9s",
                  "IHM \xE2\x80\xBA Ressources montre les fichiers d'avant et les nouveaux, marqu\xC3\xA9s nouveau ; le lien est dans IHM \xE2\x80\xBA Fichiers "
                  "externes. Maintenant, glisse un vrai fichier depuis ton bureau.",
                  row(s, resources), reveal(s, resources)});
    return st;
}

// ============================================================ Creer ou modifier un theme
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::themes(Screen& s) {
    std::vector<Step> st;
    st.push_back({"Les th\xC3\xA8mes sont dans Affichage",
                  "Ouvre le menu Affichage, puis Th\xC3\xA8me\xE2\x80\xA6 : la galerie s'ouvre.",
                  bar(s, "affichage"), {}});
    st.push_back({"43 th\xC3\xA8mes, par famille",
                  "Sombres, Clairs, Color\xC3\xA9s, Contraste \xC3\xA9lev\xC3\xA9, Industriels. Chaque carte est l'appli en miniature dans "
                  "ses couleurs, avec le contraste du texte (7:1 au moins) et du secondaire (4,5:1). Un clic applique : toute "
                  "l'appli change, et le th\xC3\xA8me est retenu pour le prochain lancement.",
                  {}, {}});
    st.push_back({"Pars d'un th\xC3\xA8me",
                  "Choisis une carte, puis Nouveau : un nouveau th\xC3\xA8me \xC3\xA0 partir de celui-ci, ouvert dans l'\xC3\xA9" "diteur avec ses "
                  "couleurs ; le th\xC3\xA8me d'origine, lui, ne change pas. Donne-lui un nom (Cr\xC3\xA9" "er) : l'auteur et la famille iront "
                  "dans le fichier export\xC3\xA9.",
                  {}, {}});
    st.push_back({"Change une couleur",
                  "Clique une pastille et prends une couleur, ou tape un code (#E07A1F) : toute l'appli change pendant que tu "
                  "choisis. Les contrastes sont v\xC3\xA9rifi\xC3\xA9s : un avertissement dit quelle r\xC3\xA8gle ne passe plus, et de combien ; "
                  "Corriger les contrastes ne touche que la luminosit\xC3\xA9 des couleurs fautives : ta teinte reste.",
                  {}, {}});
    st.push_back({"Enregistre, exporte",
                  "Enregistrer le range dans \xC2\xAB \xC3\x80 toi \xC2\xBB, en haut de la galerie, et l'applique. Tes th\xC3\xA8mes se modifient, se "
                  "dupliquent, se renomment, s'exportent et se suppriment : Exporter\xE2\x80\xA6 \xC3\xA9" "crit un fichier .xpgtheme, du texte "
                  "lisible (nom, famille, auteur et les couleurs).",
                  {}, {}});
    st.push_back({"Importe celui d'un coll\xC3\xA8gue",
                  "Importer\xE2\x80\xA6 lit un .xpgtheme : choisis le fichier, ou l\xC3\xA2" "che-le sur la galerie. Un fichier ab\xC3\xAEm\xC3\xA9 est "
                  "refus\xC3\xA9, en disant la ligne et pourquoi. Il arrive dans \xC2\xAB \xC3\x80 toi \xC2\xBB : Modifier rouvre l'\xC3\xA9" "diteur, "
                  "Supprimer le retire.",
                  bar(s, "affichage"), {}});
    return st;
}

// ============================================================ Renommer partout
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::rename(Screen& s) {
    std::vector<Step> st;
    const auto vars = [] { return apiNode(NK::VariablesFolder); };
    st.push_back({"Renommer, en voyant tout ce qui change",
                  "F2, le clic droit ou le bouton Renommer ouvrent le dialogue Renommer : une variable, un type, un bloc, une "
                  "unit\xC3\xA9, une section, une table d'animation, une variable ou une vue de l'IHM.",
                  row(s, vars), reveal(s, vars)});
    st.push_back({"La case Nom",
                  "La case Nom se modifie toujours directement. Ce qui change en 1.8.0 : elle ne renomme plus sur place. Entr\xC3\xA9" "e, "
                  "F2 ou le double-clic ouvrent le dialogue Renommer, qui montre tout ce qui va changer avant de le faire.",
                  {}, {}});
    st.push_back({"Le verdict du nom",
                  "Le dialogue v\xC3\xA9rifie le nom pendant que tu tapes : \xC2\xAB libre \xC2\xBB en vert ; \xC2\xAB d\xC3\xA9j\xC3\xA0 pris \xC2\xBB ou \xC2\xAB mot r\xC3\xA9serv\xC3\xA9 \xC2\xBB en "
                  "rouge ; le m\xC3\xAAme nom qu'une globale de l'automate est possible, mais d\xC3\xA9" "conseill\xC3\xA9.",
                  {}, {}});
    st.push_back({"O\xC3\xB9 \xC3\xA7" "a change",
                  "API : le code de l'automate. IHM : vues, scripts, alarmes, recettes, historiques, rapports, traductions. "
                  "Tables : \xC3\xA9" "change et animation. Le chiffre dit combien d'endroits. L'arbre descend jusqu'\xC3\xA0 la propri\xC3\xA9t\xC3\xA9, avec "
                  "l'expression avant (ancien nom barr\xC3\xA9) et apr\xC3\xA8s (nouveau nom surlign\xC3\xA9).",
                  {}, {}});
    st.push_back({"Les expressions suivent",
                  "Dans un texte \xC3\xA0 trous, seul le trou change. En bas, le dialogue dit combien de changements dans combien "
                  "d'endroits. Confirmer (ou Entr\xC3\xA9" "e) fait tout en une commande : un seul Ctrl+Z d\xC3\xA9" "fait tout, des deux c\xC3\xB4t\xC3\xA9s ; "
                  "Annuler ne touche \xC3\xA0 rien.",
                  {}, {}});
    const auto types = [] { return apiNode(NK::TypesFolder); };
    st.push_back({"Pareil pour un champ de DDT",
                  "Dans API \xE2\x80\xBA Types d\xC3\xA9riv\xC3\xA9s, choisis un champ d'un type et appuie sur F2 (ou double-clic) : tout ce qui le lit "
                  "suit - les lignes de code, les tables d'animation, les variables et les vues de l'IHM. Un autre type qui a un "
                  "champ du m\xC3\xAAme nom n'est pas touch\xC3\xA9. Pareil pour une vue (IHM \xE2\x80\xBA Vues).",
                  row(s, types), reveal(s, types)});
    return st;
}

// ============================================================ Expressions
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::expressions(Screen& s) {
    std::vector<Step> st;
    const auto views = [&s] { return hmiNode(s, NK::HmiViews); };
    // Lot API 8 : l'arbre du projet - Compiler et Generer sont des boutons (sous
    // le titre IHM) : leur ancien noeud, que treeToolRect sait retrouver.
    const auto compile = [] { return ProjectTreeModel::toolNode(true, 4); };
    const auto generate = [] { return ProjectTreeModel::toolNode(true, 3); };
    st.push_back({"Le nouveau symbole fx",
                  "Ouvre une vue (IHM \xE2\x80\xBA Vues). Une propri\xC3\xA9t\xC3\xA9 li\xC3\xA9" "e \xC3\xA0 une expression se voit tout de suite : la pastille fx pleine "
                  "couleur, la case teint\xC3\xA9" "e avec une barre \xC3\xA0 gauche, l'expression en chasse fixe. Survole-la : l'infobulle donne "
                  "l'expression et sa valeur actuelle.",
                  row(s, views), reveal(s, views)});
    st.push_back({"Le badge fx dans la liste",
                  "Chaque objet dit combien de ses propri\xC3\xA9t\xC3\xA9s sont des expressions ; un objet sans expression n'a pas de badge. Le "
                  "badge devient rouge si une expression est impossible.",
                  row(s, views), {}});
    st.push_back({"Une expression impossible",
                  "Une variable qui n'existe plus : la pastille passe au rouge, l'expression est soulign\xC3\xA9" "e en rouge, et "
                  "l'infobulle dit pourquoi. L'objet affiche ###, il est cercl\xC3\xA9 de rouge, son badge aussi.",
                  {}, {}});
    st.push_back({"Compiler",
                  "Le bouton Compiler (sous le titre IHM de l'arbre) v\xC3\xA9rifie toutes les expressions. Chaque ligne de la liste des erreurs dit o\xC3\xB9 (vue \xE2\x80\xBA objet \xE2\x80\xBA "
                  "propri\xC3\xA9t\xC3\xA9), quoi, et propose le bon nom ; Aller \xC3\xA0 choisit l'objet et sa propri\xC3\xA9t\xC3\xA9.",
                  row(s, compile), reveal(s, compile)});
    st.push_back({"Corrige",
                  "Remplacer, sur une ligne : le bon nom remplace l'ancien dans cette propri\xC3\xA9t\xC3\xA9. Tout remplacer les fait toutes.",
                  row(s, compile), {}});
    st.push_back({"G\xC3\xA9n\xC3\xA9rer",
                  "G\xC3\xA9n\xC3\xA9rer refuse tant qu'il reste des erreurs, et dit lesquelles : rien de cass\xC3\xA9 ne part sur le pupitre. Plus "
                  "d'erreur : G\xC3\xA9n\xC3\xA9rer passe.",
                  row(s, generate), reveal(s, generate)});
    return st;
}

// ============================================================ Chercher, filtres retenus
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::Kit::filters(Screen& s) {
    std::vector<Step> st;
    const auto vars = [] { return apiNode(NK::VariablesFolder); };
    st.push_back({"Un champ de recherche dans chaque volet",
                  "Variables, Recettes, Utilisateurs, Styles, Sous-routines, T\xC3\xA2" "ches, Statistiques : chaque volet a son champ. "
                  "Ctrl+F y met le curseur. Tape un mot : la liste se filtre \xC3\xA0 chaque lettre.",
                  row(s, vars), [&s] { s.openApiTabFromAction("variables"); }});
    st.push_back({"\xC2\xAB 3 sur 18 \xC2\xBB",
                  "Le compteur dit combien de lignes restent sur combien. Les mots trouv\xC3\xA9s sont surlign\xC3\xA9s dans le tableau : tu "
                  "vois tout de suite pourquoi une ligne est l\xC3\xA0. Plusieurs mots : tous ; \"pompe 2\" la phrase exacte ; -secours "
                  "exclu.",
                  {}, {}});
    st.push_back({"Les filtres par colonne",
                  "L'entonnoir d'un titre ouvre son filtre (contient, =, entre\xE2\x80\xA6, et les valeurs de la colonne \xC3\xA0 cocher) ; les "
                  "filtres actifs sont des pastilles au-dessus du tableau.",
                  {}, {}});
    st.push_back({"Demain matin, tu rouvres l'appli",
                  "Avant la 1.8.0, fermer l'appli vidait tous les filtres. Maintenant, m\xC3\xAAme recherche, m\xC3\xAAmes filtres, et une "
                  "pastille qui le dit : filtre de la derni\xC3\xA8re fois. Clique sur Effacer quand tu n'en veux plus.",
                  {}, {}});
    st.push_back({"Aller \xC3\xA0\xE2\x80\xA6 cherche partout",
                  "Ce qu'un volet ne montre pas, Aller \xC3\xA0\xE2\x80\xA6 (Ctrl+K) le trouve : variables et leurs membres, types, sections, vues, "
                  "objets, alarmes, le code ligne \xC3\xA0 ligne. Et les exports demandent maintenant o\xC3\xB9 enregistrer : le dossier du "
                  "projet est propos\xC3\xA9, le bouton \xE2\x80\xA6 en choisit un autre.",
                  {}, {}});
    return st;
}

// ============================================================ les entrees ======
std::vector<Step> MainAnalysisScreen::ApiTrails::Lot8::steps(MainAnalysisScreen& s, const std::string& key, std::string& refusal) {
    if (key == "api-simuler") return Kit::simulate(s);
    if (key == "api-deboguer") return Kit::debug(s);
    if (key == "api-pause") return Kit::pause(s);
    if (key == "api-glisser") return Kit::drop(s);
    if (key == "api-theme") return Kit::themes(s);
    if (key == "api-renommer") return Kit::rename(s);
    if (key == "api-expressions") {
        if (!s.app_.hmi()) {
            refusal = "ce parcours a besoin d'une IHM (les expressions sont celles des objets de ses vues).";
            return {};
        }
        return Kit::expressions(s);
    }
    if (key == "api-filtres") return Kit::filters(s);
    return {};
}

void MainAnalysisScreen::ApiTrails::Lot8::openHelpAt(MainAnalysisScreen& s, const std::string& anchor) {
    lot8::setPendingHelpAnchor(anchor);
    s.app_.menus().PushMenu("help");
}

bool MainAnalysisScreen::ApiTrails::Lot8::helpNow(MainAnalysisScreen& s) {
    if (!s.centre_ || s.centre_->tabCount() == 0) return false;
    const ui::Widget* page = s.centre_->page(s.centre_->currentIndex());
    if (!page) return false;
    std::string key;
    const std::string& id = page->id();
    if (id.rfind("analysis.sim.", 0) == 0) key = id.substr(13);
    else if (page == s.apiTab("simulation")) key = "automate";
    if (key.empty()) return false;
    const std::string anchor = lot8::helpAnchorFor(key);
    if (anchor.empty()) return false;
    openHelpAt(s, anchor);
    static const std::pair<const char*, const char*> kLabels[] = {
        {"ensemble", "Vue d'ensemble"}, {"automate", "Automate"}, {"debogage", "D\xC3\xA9" "bogage"},
        {"forcages", "For\xC3\xA7" "ages"}, {"courbes", "Courbes"}, {"journal", "Journal"},
    };
    std::string label = key;
    for (const auto& [k, l] : kLabels)
        if (key == k) label = l;
    if (s.status_) s.status_->setTransientMessage("Aide : Simulation \xE2\x80\xBA " + label, 6.0);
    return true;
}

} // namespace app
