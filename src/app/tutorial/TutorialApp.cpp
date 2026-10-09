#include "TutorialApp.hpp"

#include "TutorialOverlay.hpp"
#include "TutorialStageApp.hpp"
#include "UiDriver.hpp"
#include "../App.hpp"
#include "../Dossiers.hpp"   // 1.11.2 (decision 155) : le dossier de l'executable, pour le projet du bac
#include "../TopBar.hpp"
#include "../ViewModels.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiExprPageView.hpp"   // integration I111 : essai.convient
#include "TutorialExprTrial.hpp"         // tranche 15 : essai.convient et essai.texte
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiPublicVarsPane.hpp"   // 1.11.1 (decision 115) : volet.variable
#include "../hmi/HmiSimulation.hpp"
#include "../screens/Screens.hpp"
#include "../../help/CenterIndex.hpp"      // integration I111 : le lanceur du centre d'aide (T2)
#include "../../help/SandboxTrash.hpp"     // tranche 23 (decision 44) : l'ancien bac s'efface sans qu'on l'attende
#include "../../help/TutorialLaunch.hpp"
#include "../../help/TutorialPlayer.hpp"
#include "../../help/TutorialScreen.hpp"   // tranche 24 (R111-12) : centre.ouvert, onglet.courant
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiTutorialDeduce.hpp"   // integration I111 : les tutoriels deduits (T3)
#include "../../menu/IMenu.hpp"
#include "../../project/ProjectStore.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/TabArea.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cctype>
#include <memory>
#include <optional>
#include <utility>

namespace app::tutorials {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Une session : le tutoriel (une copie : le lecteur garde une reference), la
// scene reelle, le lecteur, le calque.
struct Session {
    help::Tutorial                  tutorial;
    std::unique_ptr<UiDriver>       driver;
    std::unique_ptr<TutorialStageApp> stage;
    std::unique_ptr<help::TutorialPlayer> player;
    std::unique_ptr<TutorialOverlay> overlay;
    std::string                     sandboxFolder;
    bool                            copied = false;
    std::uint64_t                   viewId = 0;      // la vue de @avant (vue <nom>)
    unsigned                        resets = 0;
};

App*                     g_app = nullptr;
std::unique_ptr<Session> g_session;
bool                     g_stopRequested = false;
// Integration I111 : Quitter rouvre le centre d'aide sur ce sujet (TutorialStart::returnTopic).
std::string              g_returnTopic;
// Tranche 17 (recette T2-18) : le projet ouvert avant le premier tutoriel, que Quitter rouvre
// (« Quitter jette le bac a sable », conception § 4). Vide : aucun projet, Quitter ferme le bac.
std::string              g_returnFolder;

// Les bacs a sable : <tmp>/xpg-bac-a-sable/<id>-<0|1>/<nom du projet de @bac>.
fs::path sandboxRoot() { return fs::temp_directory_path() / "xpg-bac-a-sable"; }

// Le dossier est-il dans un bac a sable (ou est-il le dossier des bacs) ?
bool inSandbox(const std::string& folder) { return help::sandbox::contains(folder, sandboxRoot()); }   // 1.11.1 : essai dans tutorial_test

// Le dossier `folder` est-il `dir` ou dedans ?
bool within(const std::string& folder, const fs::path& dir) {
    if (folder.empty()) return false;
    const auto d = dir.lexically_normal().string();
    const auto f = fs::path(folder).lexically_normal().string();
    return f.size() >= d.size() && f.compare(0, d.size(), d) == 0
           && (f.size() == d.size() || f[d.size()] == '/' || f[d.size()] == '\\');
}

// 1.11.2 (T1, decision 155 : « les tutoriels et essayer objets dans l'aide ne font rien, la barre de
// progression ne bouge meme pas, et je vois meme pas le tuto » ; le client, sous Windows, un projet neuf
// « New machine » ouvert, F1). Le projet de @bac (Armoire_Gaz) n'etait cherche que par XPG_BAC_SOURCES,
// PROJETS (les sessions, le verificateur, nos essais) et le dossier des projets. Chez le client, il n'y
// est pas : la copie echouait sans un mot (stderr), rien ne s'ouvrait, donc rien ne fermait le centre
// d'aide (adoptProject le fait, par SwitchMenu("analysis")), et chaque geste attendait sa cible absente
// avant de passer : le lecteur restait a 0:00 sur la page de l'aide. Reproduit sur la Release 1.11.1
// (tranche 38, session t38/repro-ligne : sans Armoire_Gaz trouvable, 33 ms d'horloge, puis 100, puis 300 ;
// avec, 4 066 puis 12 133). Introuvable : le lanceur le dit, et ne lance rien.
//
// Decision 164 : le projet MODELE est livre avec l'appli, dans bac/<nom> A COTE DE L'EXECUTABLE
// (l'installateur, le zip, le sans-installateur), et on ne le cherche que la : jamais dans le dossier
// courant (il change avec le raccourci, wine, une session), ni dans le dossier des projets (sans
// installation.ini, c'est <dossier courant>/projets), ni parmi les recents (un Armoire_Gaz du client,
// peut-etre modifie, ferait mentir les tutoriels). XPG_BAC_SOURCES et PROJETS (les essais : sessions,
// verificateur, recette) passent devant.
bool isProjectDir(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec) && !inSandbox(p.string());
}

// <dossier de l'exe>/bac/<nom> : le modele livre (vide si le dossier de l'exe est inconnu).
fs::path bundledSandboxSource(const std::string& name) {
    const std::string& exe = dossiers::actuel().programme;
    if (exe.empty() || name.empty()) return {};
    return dossiers::cheminDe(exe) / "bac" / dossiers::cheminDe(name);
}

std::optional<fs::path> findSandboxSource(const std::string& name) {
    if (name.empty()) return std::nullopt;
    for (const char* var : {"XPG_BAC_SOURCES", "PROJETS"})
        if (const char* d = std::getenv(var); d && *d && isProjectDir(fs::path(d) / name)) return fs::path(d) / name;
    if (const auto p = bundledSandboxSource(name); isProjectDir(p)) return p;
    return std::nullopt;
}

fs::path sandboxSource(const std::string& name) {
    // Le projet de @bac (findSandboxSource) ; introuvable : le modele livre (la copie le dira).
    return findSandboxSource(name).value_or(bundledSandboxSource(name));
}

// Un tutoriel d'une page de l'aide (Raccourcis, Signaler : son @avant ouvre la page par « aide <cle> ») :
// il joue sur la page, sans le bac (il partait deja sans lui).
bool playsOnHelpPage(const help::Tutorial& t) {
    return std::any_of(t.before.begin(), t.before.end(), [](const help::Tutorial::Item& i) {
        return i.kind == help::Tutorial::Item::Kind::Gesture && i.gesture.kind == help::GestureKind::Prepare
               && !i.gesture.args.empty() && i.gesture.args.front() == "aide";
    });
}

// Decision 164 : ce que l'arret d'un tutoriel doit dire (la copie du bac ratee), montre par frame()
// APRES l'arret : rouvrir le projet du client (SwitchMenu) effacerait un dialogue pose avant.
std::string g_stopTitle;
std::string g_stopMessage;

hmi::View* sandboxView(Session& s) {
    auto doc = g_app ? g_app->hmi() : nullptr;
    if (!doc) return nullptr;
    if (s.viewId) return doc->project.view(s.viewId);
    return nullptr;
}

// La copie NEUVE du bac a sable, puis son ouverture. Rend false tant que ce n'est pas fait.
bool openSandbox(Session& s, const help::CompiledTutorial& t) {
    if (!g_app) return true;
    if (!s.copied) {
        const auto from = sandboxSource(t.sandbox);
        // Deux dossiers, tour a tour : rouvrir le dossier deja ouvert garderait le
        // document en memoire (vu sur la session 82 : @avant rejoue deux fois).
        // Tranche 17 (recette T2-18) : la copie porte le NOM du projet de @bac
        // (<id>-<0|1>/Armoire_Gaz). Le bandeau du haut montre le nom du dossier : il disait
        // « objet-van » ou « deduit-ob » (coupe), et non Armoire_Gaz, comme l'arbre et la
        // pastille « copie d'Armoire_Gaz ».
        std::string leaf = fs::path(t.sandbox).filename().string();
        if (leaf.empty() || leaf == "." || leaf == "..") leaf = "Projet";
        const auto slot = sandboxRoot() / (t.id + "-" + std::to_string(++s.resets % 2));
        const auto to = slot / leaf;
        std::error_code ec;
        // Tranche 10 : les bacs a sable des tutoriels deja quittes ne restent pas sur le
        // disque (deux copies du projet par tutoriel : le lot des 233 deduits en aurait
        // laisse pres de 4 Go). Restent les deux dossiers de ce tutoriel, le projet ouvert,
        // et ce qui a moins de 10 minutes (peut-etre le bac d'une autre instance de l'appli).
        {
            // 1.11.1 (tranche 31) : rien ne s'efface plus ici, sur le fil principal, et on n'attend
            // plus le fil de help::sandbox (avant : finishDiscards(), puis remove_all de chaque vieux
            // bac ; sous charge, 8,1 s de blocage dans std::thread::join, rapport de T2 du 03/10,
            // 11 h 57). Les vieux bacs vont dans sa file ; une corbeille deja dans la file ou en
            // cours d'effacement n'y est pas remise (discardLater).
            std::vector<fs::path> old;
            std::error_code ec2;
            const auto now = fs::file_time_type::clock::now();
            for (const auto& e : fs::directory_iterator(sandboxRoot(), ec2))
                if (e.path().filename().string().rfind(t.id + "-", 0) != 0) old.push_back(e.path());
            for (const auto& p : old) {
                std::error_code ec3;
                if (within(g_app->projectFolder(), p) || help::sandbox::pending(p)) continue;
                const auto when = fs::last_write_time(p, ec3);
                if (ec3 || now - when < std::chrono::minutes(10)) continue;
                help::sandbox::discardLater(p);
            }
        }
        // Tranche 23 (decision 44) : la copie d'avant-derniere (environ 600 fichiers) s'effacait ici,
        // sur place : 1,5 a 2,1 s des 3,5 a 3,9 s de la « Remise en place… ». Elle est renommee
        // (le dossier est libre tout de suite), puis effacee dans un fil a part (help::sandbox).
        (void)help::sandbox::discard(slot);
        fs::create_directories(slot, ec);
        if (auto ok = project::ProjectStore::duplicate(from.string(), to.string(), t.sandbox); !ok) {
            std::fprintf(stderr, "tutoriel : bac \xC3\xA0 sable impossible (%s) : %s\n", from.string().c_str(),
                         ok.error().message().c_str());
            // Decision 164 : la copie ratee se dit en clair, avec le remede, et le tutoriel s'arrete (avant :
            // il partait muet, a 0:00, chaque geste attendant sa cible absente). Pas un tutoriel d'une page
            // de l'aide : il joue sur la page, sans le bac.
            if (!playsOnHelpPage(s.tutorial)) {
                // Le bac ne s'est pas ouvert : l'ecran d'ou le tutoriel a ete lance (le centre d'aide) est
                // encore la ; l'arret n'en rouvre pas un second.
                if (!inSandbox(g_app->projectFolder())) g_returnTopic.clear();
                g_stopTitle = "Tutoriel arr\xC3\xAAt\xC3\xA9";
                // Tranche 40 (vu sur la capture de t39) : passe 500 octets, MessageDialog montre le message dans un
                // editeur sans retour a la ligne (la raison et le remede coupes a droite). Un seul chemin, celui de
                // la copie (le modele est a cote de l'application) ; la raison sans le chemin qu'elle repete entre
                // crochets. Le detail entier reste sur stderr (la ligne du dessus).
                std::string why = ok.error().message();
                if (const auto br = why.find(" ["); br != std::string::npos && why.back() == ']') why.erase(br);
                if (why.size() > 120) why = why.substr(0, 117) + "...";
                g_stopMessage = "La copie du projet mod\xC3\xA8le " + t.sandbox + " (le bac \xC3\xA0 sable du tutoriel) a \xC3\xA9"
                                "chou\xC3\xA9" "e : " + why + ".\n\nDossier de la copie : " + dossiers::utf8De(to)
                                + "\n\nV\xC3\xA9rifie la place sur le disque et les droits d'\xC3\xA9" "criture de ce dossier ; "
                                  "si le mod\xC3\xA8le est ab\xC3\xAEm\xC3\xA9, r\xC3\xA9installe l'application. Puis relance le tutoriel.";
                g_stopRequested = true;
            }
            return true;    // on n'attend pas pour rien : les cibles manqueront, le verificateur le dira
        }
        s.sandboxFolder = to.string();
        s.copied = true;
        if (s.driver) s.driver->clearVariant();
        s.viewId = 0;
        g_app->openProjectFolder(s.sandboxFolder);
        // Tranche 17 : le bac a sable n'entre pas dans les projets recents du client
        // (adoptProject l'y met : chaque tutoriel y poussait un /tmp/xpg-bac-a-sable/...,
        // vu dans settings.txt de la recette R1 ; douze au plus, ses vrais projets en sortaient).
        g_app->forgetPath(s.sandboxFolder);
        return false;
    }
    std::error_code ec;
    const bool open = g_app->hmi() != nullptr && !g_app->projectFolder().empty()
                      && fs::equivalent(g_app->projectFolder(), s.sandboxFolder, ec);
    if (open) {
        s.copied = false;   // la prochaine remise a neuf recopie
        g_app->forgetPath(s.sandboxFolder);
    }
    return open;
}

MainAnalysisScreen* analysisScreen() {
    return g_app ? dynamic_cast<MainAnalysisScreen*>(g_app->menus().top()) : nullptr;
}

// Ouvre l'editeur d'une vue : la ligne de l'arbre, "activee" (comme un double-clic).
bool openView(std::uint64_t viewId) {
    auto* screen = analysisScreen();
    if (!screen) return false;
    screen->refreshTreeState();
    auto* top = g_session && g_session->driver ? g_session->driver->top() : nullptr;
    auto* tree = top ? dynamic_cast<ui::TreeView*>(top->findById("analysis.explorer")) : nullptr;
    if (!tree || !tree->model()) return false;
    auto* model = dynamic_cast<ProjectTreeModel*>(tree->model().get());
    if (!model) return false;
    model->refresh();
    const auto node = model->hmiViewNode(viewId);
    if (!node) return false;
    tree->activated->emit(node);
    return true;
}

// Une modification directe du document (le bac a sable est neuf, rien a annuler) :
// l'editeur ouvert relit la vue.
void refreshEditor(Session& s) {
    if (auto* editor = s.driver ? s.driver->currentEditor() : nullptr) editor->refresh();
}

// Les commandes de @avant : vue <nom>, poser <genre> <x,y> [<nom>], regler <objet> <propriete> <valeur>,
// type, variable (tranche 5), action <identifiant> (tranche 14).
bool prepare(Session& s, const std::vector<std::string>& a) {
    auto doc = g_app ? g_app->hmi() : nullptr;
    if (!doc || a.empty()) return false;
    auto& project = doc->project;
    if (a[0] == "vue" && a.size() >= 2) {
        auto* v = project.viewByName(a[1]);
        if (!v) {
            project.views.push_back(hmi::makeView(project, a[1]));
            v = &project.views.back();
        }
        s.viewId = v->id;
        // Tranche 8 : la vue du tutoriel est aussi celle de DEMARRAGE du bac a sable. F8
        // (l'etape 7 de la vanne et du script) montrait la vue de demarrage d'Armoire_Gaz,
        // Vue_Supervision, sans V_201 (verificateur). La vue de depart des groupes
        // d'utilisateurs passe devant celle du projet (Runtime::homeView) : elle suit aussi.
        project.config.startView = v->id;
        for (auto& g : project.security.groups)
            if (g.startView != hmi::kNoId) g.startView = v->id;
        return openView(v->id);
    }
    // Tranche 5 : type T_VANNE "Pos : REAL; Defaut : BOOL; ..." puis variable V "ARRAY[0..3] OF T_VANNE".
    // La copie d'Armoire_Gaz n'a pas V : les expressions =V[0].Pos y etaient en rouge (verificateur, tranche 4).
    if (a[0] == "type" && a.size() >= 3) {
        hmi::HmiType t;
        t.name = a[1];
        for (std::size_t from = 0; from < a[2].size();) {
            auto semi = a[2].find(';', from);
            if (semi == std::string::npos) semi = a[2].size();
            const auto part = a[2].substr(from, semi - from);
            from = semi + 1;
            const auto colon = part.find(':');
            if (colon == std::string::npos) continue;
            auto trim = [](std::string x) {
                while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(x.begin());
                while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();
                return x;
            };
            hmi::TypeMember m;
            m.name = trim(part.substr(0, colon));
            m.type = trim(part.substr(colon + 1));
            if (!m.name.empty() && !m.type.empty()) t.members.push_back(std::move(m));
        }
        if (t.members.empty()) return false;
        auto& types = project.programs.types;
        // Les noms de types sont sans casse : Armoire_Gaz a deja T_Vanne (Ouverte, Fermee, Position),
        // cite par T_Four.Vannes et le script Surveillance_Fours (vu au verificateur, tranche 5 :
        // V[0]. proposait Position). Le type du projet est renomme <nom>_Projet dans le bac, et ce
        // qui le cite suit : le bac compile toujours, et T_VANNE est celui du tutoriel.
        for (auto& old : types) {
            if (lower(old.name) != lower(t.name)) continue;
            const std::string was = old.name, now = old.name + "_Projet";
            old.name = now;
            auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            auto retype = [&](std::string& ty) {
                std::size_t at = 0;
                while ((at = lower(ty).find(lower(was), at)) != std::string::npos) {
                    const auto end = at + was.size();
                    if ((at == 0 || !word(ty[at - 1])) && (end >= ty.size() || !word(ty[end]))) {
                        ty.replace(at, was.size(), now);
                        at += now.size();
                    } else {
                        at = end;
                    }
                }
            };
            for (auto& other : types)
                for (auto& m : other.members) retype(m.type);
            for (auto& v : project.programs.variables) retype(v.type);
            break;
        }
        t.id = project.allocate();
        types.push_back(std::move(t));
        return true;
    }
    if (a[0] == "variable" && a.size() >= 3) {
        auto& vars = project.programs.variables;
        for (auto& v : vars)
            if (v.name == a[1]) { v.type = a[2]; return true; }
        hmi::Variable v;
        v.id = project.allocate();
        v.name = a[1];
        v.type = a[2];
        v.initial = "";
        vars.push_back(std::move(v));
        refreshEditor(s);   // l'inspecteur reverifie ses expressions
        return true;
    }
    // Tranche 14 (decision du chef) : action <identifiant> joue une action de l'appli, comme son
    // menu ou son raccourci (App::actions()) : action help.expressions pousse la page des
    // expressions. Chaque remise a neuf rouvre le bac (adoptProject : SwitchMenu("analysis") vide
    // la pile des ecrans), donc la page poussee s'en va avec l'ancien bac et @avant la repousse,
    // apres le SwitchMenu (la meme file) : il n'y en a jamais deux. Elle va en dernier dans
    // @avant : vue, poser et regler visent l'editeur, sous la page. Inconnue ou refusee : false
    // (le verificateur la compte), et la raison sur stderr.
    if (a[0] == "action" && a.size() >= 2) {
        const auto r = g_app->actions().trigger(a[1], g_app->commands());
        if (!r) std::fprintf(stderr, "tutoriel : action %s : %s\n", a[1].c_str(), r.error().message().c_str());
        return static_cast<bool>(r);
    }
    // Tranche 19 (decision 12 du 03/10) : aide <cle> ouvre le centre d'aide sur ce sujet, par-dessus
    // le bac, comme Quitter y revient (App::setHelpTopic, puis PushMenu("help") : HelpCenterScreen::
    // onEnter lit la cle). Les tutoriels des pages speciales (Raccourcis, Signaler) commencent ainsi
    // sur leur page. Comme action : en dernier dans @avant, et chaque remise a neuf la rouvre.
    if (a[0] == "aide" && a.size() >= 2) {
        if (!help::findTopic(a[1])) {
            std::fprintf(stderr, "tutoriel : aide %s : sujet inconnu du centre\n", a[1].c_str());
            return false;
        }
        g_app->setHelpTopic(a[1]);
        // 1.11.2 (R1112-3) : dans une etape, le centre est souvent deja ouvert (l'« A toi » F1 d'avant) :
        // sa page change, sans en empiler un second (Echap fermerait l'un, l'autre resterait).
        if (const auto p = g_app->menus().path(); !p.empty() && p.back() == "help") g_app->menus().ReplaceMenu("help");
        else g_app->menus().PushMenu("help");
        return true;
    }
    auto* view = sandboxView(s);
    if (!view) return false;
    if (a[0] == "poser" && a.size() >= 3) {
        const auto comma = a[2].find(',');
        if (comma == std::string::npos) return false;
        const double x = std::atof(a[2].substr(0, comma).c_str()), y = std::atof(a[2].substr(comma + 1).c_str());
        for (auto k : hmi::kPlaceableKinds) {
            if (lower(std::string(hmi::kindLabel(k))) != lower(a[1])) continue;
            const std::string name = a.size() >= 4 ? a[3] : hmi::uniqueObjectName(*view, hmi::kindLabel(k));
            view->objects.push_back(hmi::makeObject(k, project.allocate(), name, x, y, view->activeLayer));
            refreshEditor(s);   // l'explorateur d'objets et l'inspecteur (vu sur la session 82)
            return true;
        }
        return false;
    }
    if (a[0] == "regler" && a.size() >= 4) {
        for (auto& o : view->objects) {
            if (o.name != a[1]) continue;
            const auto key = lower(a[2]);
            if (key == "largeur") o.setNumber("w", std::atof(a[3].c_str()));
            else if (key == "hauteur") o.setNumber("h", std::atof(a[3].c_str()));
            // Tranche 8 : "=..." pilote la propriete par une expression (rangee a part, sans
            // son "=", comme l'inspecteur) : regler V_201 opening "=V[0].Pos".
            else if (!a[3].empty() && a[3].front() == '=') o.setExpr(a[2], a[3].substr(1));
            else o.set(a[2], a[3]);
            refreshEditor(s);
            return true;
        }
        return false;
    }
    return false;
}

// Les chemins des conditions (CONCEPTION-T1 section 1.6), sur le document du bac a sable.
std::optional<std::string> read(Session& s, std::string_view path) {
    auto doc = g_app ? g_app->hmi() : nullptr;
    if (path == "simulation.ihm") {
        auto* pane = g_app ? g_app->liveHmiPane() : nullptr;
        return std::string(pane && pane->runtime().running() ? "marche" : "arret");
    }
    // Tranche 24 (decision 53, R111-12 : les tutoriels de Demarrer). centre.ouvert : le centre d'aide
    // est ouvert (oui | non : un de ses ecrans dans la pile des menus) ; onglet.courant : le titre de
    // l'onglet courant de l'espace de travail ("" : aucun onglet en vue). Lus de l'ecran par
    // help::screen::read (TutorialScreen.hpp, essaye sans ecran par tutorial_test).
    if (path == "centre.ouvert" || path == "onglet.courant" || help::screen::isTreePath(path)) {
        help::screen::State st;
        if (g_app) st.menus = g_app->menus().path();
        if (auto* root = s.driver ? s.driver->top() : nullptr)
            if (auto* tabs = dynamic_cast<ui::TabArea*>(root->findById("analysis.centre")); tabs && tabs->tabCount())
                if (const auto* t = tabs->tab(tabs->currentIndex())) st.tab = t->title;
        // 1.11.2 (T1, decision 141 : les macros) : arbre.choisi, la ligne choisie de l'explorateur, d'apres ses lignes
        // visibles (leur profondeur et leur libelle) jusqu'a elle ; hors de vue ou aucune : vide.
        if (help::screen::isTreePath(path))
            if (auto* root = s.driver ? s.driver->top() : nullptr)
                if (auto* tree = dynamic_cast<ui::TreeView*>(root->findById("analysis.explorer")); tree && tree->model()) {
                    const auto chosen = tree->currentNode();
                    std::vector<help::screen::TreeRow> rows;
                    bool found = false;
                    for (const auto n : tree->visibleNodes()) {
                        rows.push_back({tree->visibleDepth(n), tree->model()->text(n)});
                        if (n == chosen) {
                            found = true;
                            break;
                        }
                    }
                    if (chosen != ui::kInvalidNode && found) st.tree = help::screen::treePath(rows, rows.size() - 1);
                }
        return help::screen::read(path, st);
    }
    // Tranche 5. saisie.ouverte : la liste de l'aide a la saisie est ouverte (oui | non).
    if (path == "saisie.ouverte") {
        bool open = false;
        if (auto* top = s.driver ? s.driver->top() : nullptr) {
            auto find = [&](auto&& self, ui::Widget& w) -> void {
                if (auto* f = dynamic_cast<ui::InputText*>(&w); f && f->suggestionsOpen()) open = true;
                if (auto* g = dynamic_cast<ui::PropertyGrid*>(&w); g && g->activeField() && g->activeField()->suggestionsOpen())
                    open = true;
                for (const auto& c : w.children()) self(self, *c);
            };
            find(find, *top);
        }
        return std::string(open ? "oui" : "non");
    }
    // Tranche 9. variable(V[0].Pos) : sa valeur pendant la simulation de l'IHM, lue comme les
    // expressions des vues la lisent (Runtime::environment) ; arretee ou inconnue : "".
    if (path.rfind("variable(", 0) == 0 && path.back() == ')') {
        auto* pane = g_app ? g_app->liveHmiPane() : nullptr;
        if (!pane || !pane->runtime().running()) return std::string();
        sim::Value v;
        if (!pane->runtime().environment().read(path.substr(9, path.size() - 10), v)) return std::string();
        return v.display();
    }
    // Tranche 9. expression(propriete:<nom>) : le texte de la case de l'inspecteur, comme la
    // grille le montre : son expression avec son "=", sinon sa valeur. Une case pilotee porte
    // "  f" (f crochet) au bout de son nom (HmiPanels.cpp). Pas de case de ce nom : "".
    if (path.rfind("expression(", 0) == 0 && path.back() == ')') {
        std::string name(path.substr(11, path.size() - 12));
        if (name.rfind("propriete:", 0) == 0) name = name.substr(10);
        else if (name.rfind("propri\xC3\xA9t\xC3\xA9:", 0) == 0) name = name.substr(12);
        const std::string driven = name + "  \xC6\x92";
        std::optional<std::string> found;
        using Category = ui::PropertyGrid::Category;
        auto inCategories = [&](auto&& self, const std::vector<Category>& cats) -> void {
            for (const auto& cat : cats) {
                for (const auto& prop : cat.properties)
                    if (!found && (prop.name == name || prop.name == driven))
                        found = prop.expression.empty() ? prop.value
                              : (prop.expression.front() == '=' ? prop.expression : "=" + prop.expression);
                self(self, cat.children);
            }
        };
        if (auto* top = s.driver ? s.driver->top() : nullptr) {
            auto find = [&](auto&& self, ui::Widget& w) -> void {
                if (found || !w.visible()) return;
                if (auto* g = dynamic_cast<ui::PropertyGrid*>(&w)) inCategories(inCategories, g->categories());
                for (const auto& c : w.children()) self(self, *c);
            };
            find(find, *top);
        }
        return found ? *found : std::string();
    }
    // Tranche 19. champ(<identifiant|N>) : le texte d'un champ visible de l'ecran du dessus, trouve
    // comme la cible champ: (cet identifiant, ou le N-ieme champ visible) ; introuvable : "". Les
    // A toi du centre d'aide : champ(help.report.quoi), la premiere question de Signaler.
    if (path.rfind("champ(", 0) == 0 && path.back() == ')') {
        const std::string name(path.substr(6, path.size() - 7));
        std::vector<ui::InputText*> fields;
        if (auto* top = s.driver ? s.driver->top() : nullptr) {
            auto find = [&](auto&& self, ui::Widget& w) -> void {
                if (!w.visible()) return;
                if (auto* f = dynamic_cast<ui::InputText*>(&w)) fields.push_back(f);
                for (const auto& c : w.children()) self(self, *c);
            };
            find(find, *top);
        }
        const bool number = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (number) {
            const auto n = static_cast<std::size_t>(std::atoi(name.c_str()));
            return n >= 1 && n <= fields.size() ? fields[n - 1]->text() : std::string();
        }
        for (auto* f : fields)
            if (f->id() == name) return f->text();
        return std::string();
    }
    // Tranche 21. compiler.resultats : le nombre de lignes des "Resultats de Compiler" (le tableau du
    // bas des scripts visible, HmiScriptsPane, le nombre que son titre dit) apres Compiler (F7, ou
    // l'outil Compiler) ; Compiler pas encore lance dans ce volet, ou pas de scripts ici : "". Les
    // fautes soulignees pendant la frappe sont deja dans ce tableau avant Compiler : l'A toi "Lance
    // Compiler" de script-rampe (script(X).erreurs >= 1) etait deja vrai avant F7 (lot final).
    if (path == "compiler.resultats") {
        std::optional<std::string> out;
        if (auto* top = s.driver ? s.driver->top() : nullptr) {
            auto find = [&](auto&& self, ui::Widget& w) -> void {
                if (out || !w.visible()) return;
                if (auto* pane = dynamic_cast<HmiScriptsPane*>(&w)) {
                    const auto& model = pane->diagnosticTable().model();
                    out = pane->compiledHere() ? std::to_string(model ? model->rowCount() : 0u) : std::string();
                    return;
                }
                for (const auto& c : w.children()) self(self, *c);
            };
            find(find, *top);
        }
        return out ? *out : std::string();
    }
    // 1.11.1 (decision 115) : volet.variable - le chemin de la variable choisie dans le volet des
    // variables publiques visible (IHM > Programmation generale > Variables systeme / d'instances :
    // "Vue_Tuto.Pompe_1.Alarmes.Defaut.Acked") ; aucune, ou pas de volet : "".
    if (path == "volet.variable") {
        std::optional<std::string> out;
        if (auto* top = s.driver ? s.driver->top() : nullptr) {
            auto find = [&](auto&& self, ui::Widget& w) -> void {
                if (out || !w.visible()) return;
                if (auto* pane = dynamic_cast<HmiPublicVarsPane*>(&w)) {
                    out = pane->selectedPath();
                    return;
                }
                for (const auto& c : w.children()) self(self, *c);
            };
            find(find, *top);
        }
        return out ? *out : std::string();
    }
    // biblio.variante : la variante choisie dans la bibliotheque. Les cartes de variantes ne
    // sont pas encore la : la tuile du genre en tient lieu (UiDriver, variante:) ; on rend la
    // variante de la derniere cible variante: trouvee ("" apres une remise a neuf).
    if (path == "biblio.variante") return s.driver ? s.driver->lastVariant() : std::string();
    // Integration I111 (help::kPathsToAdd de T3) : essai.convient ; tranche 15 : essai.texte (le
    // texte du champ d'essai). Lus dans la page des expressions visible (TutorialExprTrial.hpp) :
    // les A toi des tutoriels deduits des types d'expression.
    if (auto trial = readExprTrial(s.driver ? s.driver->top() : nullptr, path)) return trial;
    auto* view = sandboxView(s);
    if (!doc || !view) return std::nullopt;
    // selection : le nom de l'objet choisi dans la vue (plusieurs : separes par ", ").
    if (path == "selection") {
        std::string out;
        if (auto* editor = s.driver ? s.driver->currentEditor() : nullptr)
            for (const auto id : editor->canvas().selection())
                for (const auto& o : view->objects)
                    if (o.id == id) out += (out.empty() ? "" : ", ") + o.name;
        return out;
    }
    auto kindOf = [](std::string_view label) -> std::optional<hmi::Kind> {
        for (auto k : hmi::kPlaceableKinds)
            if (lower(std::string(hmi::kindLabel(k))) == lower(std::string(label))) return k;
        return std::nullopt;
    };
    const std::string p(path);
    if (p.rfind("objets[", 0) == 0 && p.back() == ']') {
        const auto k = kindOf(p.substr(7, p.size() - 8));
        if (!k) return std::nullopt;
        int n = 0;
        for (const auto& o : view->objects) n += o.kind == *k;
        return std::to_string(n);
    }
    // script(Nom).declencheur | .periode | .texte : un script general du bac a sable (le tutoriel
    // du script). Introuvable : "" (le @verifier echoue). .erreurs : pas encore (il faut compiler).
    if (p.rfind("script(", 0) == 0) {
        const auto close = p.find(')');
        if (close == std::string::npos) return std::nullopt;
        const auto name = p.substr(7, close - 7);
        const auto what = close + 2 <= p.size() ? p.substr(close + 2) : std::string();
        for (const auto& sc : doc->project.programs.scripts) {
            if (sc.name != name) continue;
            if (what == "declencheur") return std::string(hmi::eventLabel(sc.event));
            if (what == "periode") return std::to_string(sc.periodMs);
            if (what == "texte") return sc.body;
            // Tranche 5 : les fautes de Compiler (les memes que l'editeur souligne), comptees.
            if (what == "erreurs") {
                int n = 0;
                for (const auto& d : placedScriptDiagnostics(doc->project, sc.body, nullptr, nullptr, g_app->project().get(), nullptr, &sc.decls))
                    n += d.severity == hmi::ScriptDiagnostic::Severity::Error;
                return std::to_string(n);
            }
            return std::nullopt;
        }
        return std::string();
    }
    const hmi::Object* obj = nullptr;
    std::string member;
    if (p.rfind("objet[", 0) == 0) {
        const auto close = p.find(']');
        if (close == std::string::npos) return std::nullopt;
        const auto k = kindOf(p.substr(6, close - 6));
        if (!k) return std::nullopt;
        for (const auto& o : view->objects)
            if (o.kind == *k) obj = &o;
        member = close + 2 <= p.size() ? p.substr(close + 2) : std::string();
    } else if (p.rfind("objet(", 0) == 0) {
        const auto close = p.find(')');
        if (close == std::string::npos) return std::nullopt;
        const auto name = p.substr(6, close - 6);
        for (const auto& o : view->objects)
            if (o.name == name) obj = &o;
        member = close + 2 <= p.size() ? p.substr(close + 2) : std::string();
    } else {
        return std::nullopt;
    }
    if (!obj) return std::string();
    if (member == "nom") return obj->name;
    // Une case a expression : l'expression est rangee a part (Prop::expr) ; on la rend avec son
    // "=", comme l'inspecteur l'affiche (vu sur --verifier-tutoriel : objet[Vanne].value rendait
    // la valeur fixe, FALSE, alors que la case montrait =V[0].Pos > 0).
    if (auto e = obj->expr(member); !e.empty()) return e.front() == '=' ? e : "=" + e;
    return obj->text(member);
}

void startSession(const help::Tutorial& t, const help::TutorialStart& o) {
    // Tranche 17 : le projet du client, que Quitter rouvrira. Un tutoriel lance depuis un autre
    // (son bac a sable est ouvert) garde le projet d'avant le premier.
    if (const std::string open = g_app ? g_app->projectFolder() : std::string(); !inSandbox(open))
        g_returnFolder = open;
    g_session.reset();
    auto s = std::make_unique<Session>();
    s->tutorial = t;
    s->driver = std::make_unique<UiDriver>(*g_app);
    s->stage = std::make_unique<TutorialStageApp>(*s->driver);
    Session* raw = s.get();
    s->stage->setSandboxOpener([raw](const help::CompiledTutorial& c) { return openSandbox(*raw, c); });
    s->stage->setPreparer([raw](const std::vector<std::string>& a) { return prepare(*raw, a); });
    s->stage->setReader([raw](std::string_view p) { return read(*raw, p); });
    s->player = std::make_unique<help::TutorialPlayer>(s->tutorial, *s->stage, o.variant);
    s->overlay = std::make_unique<TutorialOverlay>(*s->player, *s->stage);
    s->overlay->onQuit = [] { g_stopRequested = true; };
    g_returnTopic = o.returnTopic;
    g_session = std::move(s);
    g_session->player->start(o.step, o.paused);
    if (o.aTry) (void)g_session->player->startATry();
    g_session->player->setNotice(o.notice);   // 1.11.2 (R1112-4) : startTutorial l'a rempli, ou vide
}

fs::path findTutorialDir() {
    if (const char* d = std::getenv("XPG_TUTORIELS"); d && *d) return d;
    std::error_code ec;
    for (const auto& c : {fs::current_path(ec) / "tools" / "tutoriels", fs::current_path(ec) / "tutoriels"})
        if (fs::is_directory(c, ec)) return c;
    return {};
}

} // namespace

void install(App& app) {
    g_app = &app;
    {
        // Les tutoriels embarques (help/TutorialTexts.cpp, genere) ; un dossier (XPG_TUTORIELS, ou
        // tools/tutoriels du dossier courant) passe devant : l'auteur essaie sans reconstruire.
        std::vector<std::string> errors;
        const int n = help::registerEmbeddedTutorials(&errors);
        std::fprintf(stderr, "tutoriels : %d embarqu\xC3\xA9(s)\n", n);
        for (const auto& e : errors) std::fprintf(stderr, "tutoriels : %s\n", e.c_str());
    }
    if (const auto dir = findTutorialDir(); !dir.empty()) {
        std::vector<std::string> errors;
        const int n = help::registerTutorialDir(dir, &errors);
        std::fprintf(stderr, "tutoriels : %d inscrit(s) depuis %s\n", n, dir.string().c_str());
        for (const auto& e : errors) std::fprintf(stderr, "tutoriels : %s\n", e.c_str());
    }
    help::setTutorialLauncher([](const help::Tutorial& t, const help::TutorialStart& o) {
        if (!g_app) return false;
        // 1.11.2 (T1, decision 155) : sans le projet du bac (findSandboxSource), le tutoriel ne peut
        // rien montrer. Avant, il partait quand meme, muet, sous la page de l'aide (le lecteur a 0:00) :
        // on le dit, et on ne lance rien ; l'ecran d'ou il a ete demande (le centre d'aide) reste.
        // Sauf un tutoriel d'une page de l'aide (Raccourcis, Signaler : son @avant ouvre la page par
        // « aide <cle> ») : il joue sur la page, et partait deja sans bac.
        // Decision 164 : le modele manque (bac/<nom> a cote de l'exe) : on dit ou il devait etre, et le remede.
        if (!playsOnHelpPage(t) && !findSandboxSource(t.sandbox)) {
            const fs::path model = bundledSandboxSource(t.sandbox);
            const std::string where = model.empty() ? std::string("bac/") + t.sandbox : dossiers::utf8De(model);
            g_app->menus().ShowDialog(
                std::make_unique<MessageDialog>(
                    "Tutoriel pas lanc\xC3\xA9",
                    "Le tutoriel joue sur une copie du projet mod\xC3\xA8le " + t.sandbox + " (le bac \xC3\xA0 sable), "
                    "livr\xC3\xA9 avec l'application : ton projet n'est jamais touch\xC3\xA9. Ce mod\xC3\xA8le manque :\n"
                        + where + "\n\n"
                    "Pour le remettre : r\xC3\xA9installe l'application, ou recopie le dossier bac du zip \xC3\xA0 c\xC3\xB4t\xC3\xA9 "
                    "de l'application. Puis relance le tutoriel.",
                    MessageDialog::Icon::Warning),
                [](const menu::DialogResult&) {});
            std::fprintf(stderr, "tutoriel : %s : le projet du bac \xC3\xA0 sable (%s) est introuvable, rien n'est lanc\xC3\xA9\n",
                         t.id.c_str(), t.sandbox.c_str());
            return true;
        }
        // Tranche 17 : le bac a sable remplace le projet ouvert (adoptProject vide l'historique, la
        // reprise efface sa sauvegarde) : des modifications pas enregistrees du client seraient
        // perdues sans un mot. On demande d'abord ; Annuler ne lance rien. Pas dans un bac a sable
        // (un tutoriel lance depuis un autre) : ses modifications sont celles du tutoriel.
        if (g_app->project() && g_app->commands().isModified() && !inSandbox(g_app->projectFolder())) {
            auto dlg = std::make_unique<MessageDialog>(
                "Enregistrer ton projet ?",
                "Ton projet a des modifications qui ne sont pas enregistr\xC3\xA9" "es. Le tutoriel s'ouvre sur une "
                "copie d'essai (le bac \xC3\xA0 sable), \xC3\xA0 la place de ton projet : sans les enregistrer, tu les perdrais.\n\n"
                "Enregistrer et lancer : ton projet est \xC3\xA9" "crit sur le disque, puis le tutoriel commence. "
                "Quitter le tutoriel rouvre ton projet.",
                MessageDialog::Icon::Question, "Enregistrer et lancer");
            g_app->menus().ShowDialog(std::move(dlg), [tc = t, oc = o](const menu::DialogResult& r) {
                if (!r.accepted() || !g_app) return;
                if (auto ok = g_app->saveProject(); !ok) {
                    g_app->menus().ShowDialog(std::make_unique<MessageDialog>(
                                                  "Tutoriel pas lanc\xC3\xA9",
                                                  "Ton projet n'a pas pu \xC3\xAAtre enregistr\xC3\xA9 : " + ok.error().message()
                                                      + "\n\nLe tutoriel n'est pas lanc\xC3\xA9 : tes modifications sont toujours l\xC3\xA0.",
                                                  MessageDialog::Icon::Error),
                                              [](const menu::DialogResult&) {});
                    return;
                }
                startSession(tc, oc);
            });
            return true;
        }
        startSession(t, o);
        return true;
    });
    // Integration I111, lien 1 : le centre d'aide (T2 : Lancer, Essayer, Me montrer, une etape)
    // lance par le point d'entree de T1 (l'ecrit, sinon le deduit du sujet).
    help::center::setTutorialLauncher([](const help::center::TutorialRequest& r) {
        help::TutorialStart o{r.variant, r.step, r.aTry, r.paused, r.returnTopic, {}};   // notice : startTutorial
        return help::startTutorial(r.topic, r.variant, std::move(o)) == help::StartResult::Started;
    });
    // Integration I111, lien 3 : un sujet sans tutoriel ecrit a celui que T3 en deduit
    // (help::deduceTutorial), lu par le meme lecteur et garde en cache par tutorialForTopic.
    help::setTutorialDeducer([](const help::TopicInfo& topic) { return help::deduceTutorial(topic); });
    // Integration I111, lien 2 : la duree et les etapes de chaque sujet (la pastille, la carte, le
    // compteur "Tutoriels prets" du centre) : le tutoriel du sujet, l'ecrit sinon le deduit
    // (tutorialForTopic ; les sujets sont inscrits par le centre, help::setTopics), compile pour
    // sa variante par defaut. Rien, ou un tutoriel qui ne se compile pas proprement : le centre estime.
    help::center::setTutorialInfoProvider([](std::string_view key) -> std::optional<help::center::TutorialInfo> {
        const help::Tutorial* t = help::tutorialForTopic(key);
        if (t == nullptr) return std::nullopt;
        const help::CompiledTutorial c = t->compile();
        if (c.steps.empty() || !c.problems.empty()) return std::nullopt;
        help::center::TutorialInfo info;
        info.durationMs = c.totalMs;
        info.steps = static_cast<int>(c.steps.size());
        info.estimated = false;
        for (const auto& st : c.steps) info.stepStarts.emplace_back(st.title, st.startMs);
        return info;
    });
}

void shutdown() {
    g_session.reset();
    menu::WidgetMenu::muteTooltips(false);   // 1.11.2 (R1112-6)
    help::sandbox::finishDiscards();   // tranche 23 : l'effacement en cours d'un ancien bac
    help::setTutorialLauncher({});
    help::center::setTutorialLauncher({});
    help::center::setTutorialInfoProvider({});
    help::setTutorialDeducer({});
    g_returnFolder.clear();
    g_stopTitle.clear();
    g_stopMessage.clear();
    g_app = nullptr;
}

void stop() { g_stopRequested = true; }

void frame(double dtMs) {
    if (g_stopRequested) {
        g_stopRequested = false;
        g_session.reset();
        // Tranche 17 (recette T2-18) : Quitter jette le bac a sable (conception § 4). Le bac porte
        // maintenant le nom de son projet (Armoire_Gaz) : laisse ouvert, il passerait pour celui du
        // client, et ce qu'on y enregistre partirait avec le prochain bac. Le projet d'avant est
        // rouvert ; sans projet d'avant, le bac est ferme (l'accueil). Un autre projet ouvert
        // pendant le tutoriel (pas un bac) reste tel quel.
        if (g_app && inSandbox(g_app->projectFolder())) {
            std::error_code ec;
            const std::string back = std::exchange(g_returnFolder, {});
            if (!back.empty() && fs::is_directory(back, ec))
                g_app->openProjectFolder(back);
            else
                (void)g_app->actions().trigger("file.close", g_app->commands());
        } else {
            g_returnFolder.clear();
        }
        // Lance depuis le centre d'aide : Quitter y revient, sur le sujet (HelpCenterScreen::onEnter
        // lit la cle par App::takeHelpTopic, Index::forF1).
        if (std::string topic = std::exchange(g_returnTopic, {}); !topic.empty() && g_app) {
            g_app->setHelpTopic(topic);
            g_app->menus().PushMenu("help");
        }
        // Decision 164 : pourquoi le tutoriel s'est arrete (la copie du bac ratee), par-dessus l'ecran rendu.
        if (std::string message = std::exchange(g_stopMessage, {}); !message.empty() && g_app)
            g_app->menus().ShowDialog(std::make_unique<MessageDialog>(std::exchange(g_stopTitle, {}), std::move(message),
                                                                      MessageDialog::Icon::Error),
                                      [](const menu::DialogResult&) {});
    }
    if (g_session) g_session->overlay->frame(dtMs);
    // 1.11.2 (R1112-6 : « IHM · Clic : ouvrir… » s'ouvrait sous le pointeur du tutoriel et cachait Generer et
    // Compiler) : pendant la demonstration (lecture, pause, fin d'etape), les infobulles de l'appli se taisent ;
    // a l'« A toi », a la fin et sans tutoriel, elles reviennent (la souris est a l'eleve).
    const help::TutorialPlayer* p = g_session ? g_session->player.get() : nullptr;
    menu::WidgetMenu::muteTooltips(p && p->state() != help::PlayerState::ATry && p->state() != help::PlayerState::Finished);
}

void paint(gfx::IRenderer& r, const ui::Theme& theme, gfx::Size surface, double time) {
    if (!g_session) return;
    const gfx::Rect all{0.f, 0.f, surface.w, surface.h};
    g_session->overlay->setScreen(all);
    g_session->overlay->setBounds(all);
    // Aller a, dans le bandeau du haut : la pastille du bac a sable le recouvre (tranche 5).
    gfx::Rect goTo{};
    std::vector<gfx::Rect> parts;
    if (auto* top = g_session->driver ? g_session->driver->top() : nullptr) {
        auto find = [&](auto&& self, ui::Widget& w) -> void {
            if (goTo.w > 0.f) return;
            if (auto* bar = dynamic_cast<TopBar*>(&w); bar && bar->visible()) {
                goTo = bar->partRect("aller");
                // Tranche 10 : ses voisins, que les pastilles des variantes recouvrent en entier.
                parts.clear();
                for (const char* key : {"simuler", "arreter", "cycle", "etat", "ihm"})
                    if (const auto p = bar->partRect(key); p.w > 0.f) parts.push_back(p);
            }
            for (const auto& c : w.children()) self(self, *c);
        };
        find(find, *top);
    }
    g_session->overlay->setGoTo(goTo);
    g_session->overlay->setBarParts(std::move(parts));
    const ui::PaintContext ctx{r, theme, all, time, nullptr};
    g_session->overlay->paintTopMost(ctx);
}

bool event(const ui::InputEvent& e) {
    if (!g_session) return false;
    return g_session->overlay->dispatch(e) != ui::EventResult::Ignored;
}

help::TutorialPlayer* player() { return g_session ? g_session->player.get() : nullptr; }
TutorialStageApp*     stage() { return g_session ? g_session->stage.get() : nullptr; }
TutorialOverlay*      overlay() { return g_session ? g_session->overlay.get() : nullptr; }

// 1.11.1 (T1, R111-14 / R111-24) : voir TutorialApp.hpp.
bool isSandboxFolder(const std::string& folder) { return inSandbox(folder); }

} // namespace app::tutorials
