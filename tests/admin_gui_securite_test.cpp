// =============================================================================
//  tests/admin_gui_securite_test.cpp - 1.33.0 : les pages << Parametres de
//  securite >> (la cle de l'editeur) et << Synchronisation GitHub >> (l'assistant)
//  de XPG-License-Manager-Admin, dessinees a blanc
// -----------------------------------------------------------------------------
//  La cle : creee par la page (une cle d'ESSAI, apres la confirmation), sauvegardee
//  (Argon2id sur le vrai fil de Background : les champs vides des le lancement,
//  la phrase jamais montree ; trop faible ou differente : refusee AVANT tout travail
//  lent), verifiee (bonne, mauvaise phrase), restauree dans un second poste,
//  exportee (TestPublisherKey.hpp). Un travail en cours : les boutons gris.
//
//  GitHub : un FAUX GitHub dans ce processus (le transport injecte,
//  app::ai::setHttpFactory - le meme que tests/github_sync_test.cpp) ; rien ne part
//  sur le reseau. L'assistant : un mauvais depot refuse ; le premier poste voit son
//  code de secours UNE fois (Continuer bloque tant que la case n'est pas cochee,
//  Copier sur un clic), puis seulement apres une confirmation ; le jeton
//  enregistre puis retire (jamais dessine) ; Verifier, Synchroniser, Recevoir
//  seulement par runSlow ; un second poste par le code de secours ; un depot
//  public refuse.
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/GitHubSync.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/Containers.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace admintest;
using core::json::Value;

namespace {

// ---------------------------------------------------------------------------
//  le faux GitHub (celui de github_sync_test.cpp, sans la course ni la branche protegee)
// ---------------------------------------------------------------------------
struct FakeGitHub {
    std::string repo{"editeur/licences-xpg"};
    std::string token{"github_pat_essai_jeton_00000000000000000000000000"};
    std::string branch{"main"};
    bool        isPrivate{true};
    std::map<std::string, std::string> blobs;
    std::map<std::string, std::map<std::string, std::string>> trees;
    struct Commit {
        std::string tree;
        std::vector<std::string> parents;
        std::string message;
    };
    std::map<std::string, Commit> commits;
    std::map<std::string, std::string> refs;
    std::vector<std::string> bodies;
    int counter{0};

    std::string newSha(const std::string& kind, const std::string& content) {
        return deploy::crypto::sha256Hex(kind + "\n" + std::to_string(++counter) + "\n" + content).substr(0, 40);
    }
    std::string blobSha(const std::string& content) {
        const std::string sha = deploy::crypto::sha256Hex("blob\n" + content).substr(0, 40);
        blobs[sha] = content;
        return sha;
    }
    std::string makeTree(const std::map<std::string, std::string>& files) {
        std::string canon;
        for (const auto& [p, s] : files) canon += p + "=" + s + "\n";
        const std::string sha = deploy::crypto::sha256Hex("tree\n" + canon).substr(0, 40);
        trees[sha] = files;
        return sha;
    }
    std::string makeCommit(const std::string& tree, const std::vector<std::string>& parents, const std::string& message) {
        const std::string sha = newSha("commit", tree + message);
        commits[sha] = Commit{tree, parents, message};
        return sha;
    }
    [[nodiscard]] bool anyBlobHas(const std::string& needle) const {
        for (const auto& [sha, content] : blobs)
            if (has(content, needle)) return true;
        return false;
    }

    struct Answer {
        int status;
        std::string body;
    };
    static Answer json(int status, const Value& v) { return {status, core::json::write(v, false)}; }
    static Answer msg(int status, const std::string& m) {
        Value v = Value::object();
        v.set("message", m);
        return json(status, v);
    }
    static std::string unbase64(const std::string& b64) {
        std::string content;
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        unsigned buf = 0;
        int bits = 0;
        for (char c : b64) {
            const int v = val(c);
            if (v < 0) continue;
            buf = (buf << 6) | static_cast<unsigned>(v);
            bits += 6;
            if (bits >= 8) {
                content += static_cast<char>((buf >> (bits - 8)) & 0xFF);
                bits -= 8;
            }
        }
        return content;
    }

    Answer handle(const app::ai::HttpRequest& req) {
        std::string auth;
        for (const auto& [k, v] : req.headers)
            if (k == "Authorization") auth = v;
        bodies.push_back(req.body);
        if (auth != "Bearer " + token) return msg(401, "Bad credentials");
        const std::string base = "http://127.0.0.1:9/repos/";
        if (req.url.rfind(base, 0) != 0) return msg(404, "Not Found");
        std::string rest = req.url.substr(base.size());
        if (rest.rfind(repo, 0) != 0 || (rest.size() > repo.size() && rest[repo.size()] != '/')) return msg(404, "Not Found");
        std::string path = rest.substr(repo.size());
        if (const auto q = path.find('?'); q != std::string::npos) path.resize(q);
        const bool empty = refs.empty();
        Value body;
        if (!req.body.empty()) (void)core::json::parse(req.body, body, nullptr);
        if (req.method == "GET" && path.empty()) {
            Value v = Value::object();
            v.set("full_name", repo);
            v.set("private", isPrivate);
            v.set("default_branch", branch);
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/ref/heads/", 0) == 0) {
            if (empty) return msg(409, "Git Repository is empty.");
            const auto it = refs.find(path.substr(15));
            if (it == refs.end()) return msg(404, "Not Found");
            Value v = Value::object(), o = Value::object();
            o.set("sha", it->second);
            o.set("type", std::string("commit"));
            v.set("ref", "refs/heads/" + it->first);
            v.set("object", std::move(o));
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/commits/", 0) == 0) {
            const auto it = commits.find(path.substr(13));
            if (it == commits.end()) return msg(404, "Not Found");
            Value v = Value::object(), t = Value::object(), a = Value::object();
            v.set("sha", it->first);
            t.set("sha", it->second.tree);
            v.set("tree", std::move(t));
            v.set("message", it->second.message);
            a.set("date", std::string("2026-11-06T22:00:00Z"));
            v.set("author", std::move(a));
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/trees/", 0) == 0) {
            const auto it = trees.find(path.substr(11));
            if (it == trees.end()) return msg(404, "Not Found");
            Value v = Value::object(), items = Value::array();
            v.set("sha", it->first);
            for (const auto& [p, s] : it->second) {
                Value e = Value::object();
                e.set("path", p);
                e.set("mode", std::string("100644"));
                e.set("type", std::string("blob"));
                e.set("sha", s);
                items.push(std::move(e));
            }
            v.set("tree", std::move(items));
            v.set("truncated", false);
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/blobs/", 0) == 0) {
            const auto it = blobs.find(path.substr(11));
            if (it == blobs.end()) return msg(404, "Not Found");
            std::string b64 = app::ai::base64(it->second), wrapped;
            for (std::size_t i = 0; i < b64.size(); i += 60) wrapped += b64.substr(i, 60) + "\n";
            Value v = Value::object();
            v.set("sha", it->first);
            v.set("encoding", std::string("base64"));
            v.set("content", wrapped);
            return json(200, v);
        }
        if (empty && path.rfind("/git/", 0) == 0) return msg(409, "Git Repository is empty.");
        if (req.method == "POST" && path == "/git/blobs") {
            Value v = Value::object();
            v.set("sha", blobSha(unbase64(body.str("content"))));
            return json(201, v);
        }
        if (req.method == "POST" && path == "/git/trees") {
            std::map<std::string, std::string> files;
            const std::string baseTree = body.str("base_tree");
            if (!baseTree.empty()) {
                if (!trees.count(baseTree)) return msg(422, "base_tree inconnu");
                files = trees[baseTree];
            }
            if (const Value* items = body.find("tree"))
                for (const auto& it : items->items()) {
                    if (!blobs.count(it.str("sha"))) return msg(422, "blob inconnu");
                    files[it.str("path")] = it.str("sha");
                }
            Value v = Value::object();
            v.set("sha", makeTree(files));
            return json(201, v);
        }
        if (req.method == "POST" && path == "/git/commits") {
            std::vector<std::string> parents;
            if (const Value* ps = body.find("parents"))
                for (const auto& p : ps->items()) parents.push_back(p.asString());
            if (!trees.count(body.str("tree"))) return msg(422, "arbre inconnu");
            Value v = Value::object();
            v.set("sha", makeCommit(body.str("tree"), parents, body.str("message")));
            return json(201, v);
        }
        if (req.method == "PATCH" && path.rfind("/git/refs/heads/", 0) == 0) {
            const std::string b = path.substr(16);
            if (!refs.count(b)) return msg(422, "Reference does not exist");
            const std::string target = body.str("sha");
            if (!commits.count(target)) return msg(422, "commit inconnu");
            bool ff = false;
            std::vector<std::string> todo{target};
            while (!todo.empty() && !ff) {
                const std::string c = todo.back();
                todo.pop_back();
                if (c == refs[b]) ff = true;
                for (const auto& p : commits[c].parents) todo.push_back(p);
            }
            if (!ff && !body.flag("force")) return msg(422, "Update is not a fast forward");
            refs[b] = target;
            Value v = Value::object(), o = Value::object();
            o.set("sha", target);
            v.set("object", std::move(o));
            return json(200, v);
        }
        if (req.method == "PUT" && path.rfind("/contents/", 0) == 0) {
            if (!empty) return msg(422, "seulement pour un depot vide dans ce faux");
            const std::string br = body.str("branch", branch);
            std::map<std::string, std::string> files{{path.substr(10), blobSha(unbase64(body.str("content")))}};
            refs[br] = makeCommit(makeTree(files), {}, body.str("message"));
            Value v = Value::object(), c = Value::object();
            c.set("sha", refs[br]);
            v.set("commit", std::move(c));
            return json(201, v);
        }
        return msg(404, "Not Found");
    }
};

FakeGitHub* g_fake = nullptr;

class FakeClient final : public app::ai::HttpClient {
public:
    app::ai::HttpResponse perform(const app::ai::HttpRequest& req, const app::ai::HttpSink& sink) override {
        app::ai::HttpResponse r;
        const auto a = g_fake->handle(req);
        r.status = a.status;
        if (sink.onHeaders) sink.onHeaders(a.status, {});
        if (sink.onData) sink.onData(a.body);
        return r;
    }
    void cancel() override {}
    [[nodiscard]] bool cancelled() const noexcept override { return false; }
};

// ---------------------------------------------------------------------------
//  les aides
// ---------------------------------------------------------------------------
std::string g_clipboard;   // le presse-papiers des essais

void installServices() {
    installMetrics();
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return static_cast<float>(TextRenderer::chars(t)) * h * 0.5f;
    };
    s.lineHeight = [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; };
    s.clipboardGet = [] { return g_clipboard; };
    s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
    s.surfaceSize = [] { return gfx::Size{1280.f, 800.f}; };
    ui::installPlatformServices(std::move(s));
}

// Tout ce qui a ete dessine pendant l'essai (les secrets n'y sont jamais).
std::vector<std::string> g_painted;

// Dessiner deux fois (une page qui voit un travail commencer ou finir se refait a l'image suivante) ; le texte
// a plat (les lignes coupees remises bout a bout).
std::string draw(ui::Widget& w, TextRenderer& tr, float width = 1100.f, float height = 760.f) {
    (void)paint(w, tr, width, height);
    std::string t = paint(w, tr, width, height);
    g_painted.push_back(t);
    for (auto& c : t)
        if (c == '\n') c = ' ';
    return t;
}

std::string drawMenu(menu::IMenu& m, TextRenderer& tr) {
    std::string t = paintMenu(m, tr);
    g_painted.push_back(t);
    for (auto& c : t)
        if (c == '\n') c = ' ';
    return t;
}

bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    if (!w) return false;
    for (ui::Widget* p = w; p; p = p->parent())
        if (!p->visible()) return false;
    return true;
}

bool enabledW(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->enabled();
}

std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(aucun rapport)"); }

bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }

// Aucun rapport, aucun message du bandeau, aucun dessin ne contient `secret`.
bool nowhere(const std::vector<const HostRig*>& rigs, const std::string& secret) {
    for (const auto* rig : rigs) {
        for (const auto& r : rig->reports)
            if (has(r.text(), secret) || has(r.output, secret)) return false;
        for (const auto& f : rig->flashes)
            if (has(f, secret)) return false;
    }
    for (const auto& t : g_painted)
        if (has(t, secret)) return false;
    return true;
}

// Un travail lent qui attend qu'on le libere (les boutons gris pendant ce temps).
struct Blocker {
    std::atomic<bool> release{false};
    bool start(HostRig& rig) {
        rig.slowOnThread = true;
        return rig.host().runSlow("Essai : un travail long", [this] {
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return gui::OpResult::done("Travail d'essai fini.");
        }, nullptr);
    }
};

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    installServices();
    TextRenderer tr;
    menu::MenuManager menus{menu::MenuFactory{}};
    const std::string phrase = "cheval agrafe batterie correcte";
    std::string why;

    // =========================================================================================== la cle
    Fixture fa(source, "secu-a");
    HostRig rigA(fa);
    auto pa = gui::makeSecurityPage(rigA.host());
    rigA.refreshers.push_back([&] {
        fa.ws->reload();
        pa->refresh();
    });

    std::printf("-- securite : pas encore de cle\n");
    {
        const std::string t = draw(*pa, tr);
        check(pa->title() == "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9", "le titre : " + pa->title());
        check(has(t, "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9") && has(t, "La cl\xC3\xA9 qui signe toutes les licences"), "le titre et la phrase sous lui");
        check(has(t, "Pas encore de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste"), "sans cle : dit");
        check(has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9") && has(t, "Restaurer une sauvegarde"), "les deux choix : creer, restaurer");
        check(shown(*pa, "securite.creer") && shown(*pa, "securite.restaurer") && shown(*pa, "securite.nom") && shown(*pa, "securite.essai"),
              "les champs et les boutons des deux choix");
        check(!shown(*pa, "securite.sauver") && !shown(*pa, "securite.verifier") && !shown(*pa, "securite.exporter"),
              "sans cle : ni sauvegarde, ni verification, ni export");
        check(has(t, "Ce qui prot\xC3\xA8ge ton poste") && has(t, "ne quitte jamais ce PC") && has(t, "jamais livr\xC3\xA9" "e"), "ce qui protege le poste");
        check(find<ui::Checkbox>(*pa, "securite.essai") && !find<ui::Checkbox>(*pa, "securite.essai")->isChecked(), "cle d'essai : decochee par defaut");
        check(find<ui::InputText>(*pa, "securite.restaurer.phrase")->masked(), "la phrase de restauration : masquee");
        check(click(*pa, "securite.dossier") && !rigA.openedFolders.empty() && rigA.openedFolders.back() == fa.ws->folder(), "Ouvrir le dossier");
        check(has(t, "admin"), "le dossier est dit");
    }

    std::printf("-- securite : creer la cle (confirmation)\n");
    {
        check(click(*pa, "securite.creer") && rigA.dialogs.empty() && !lastOk(rigA) && has(lastText(rigA), "nom"),
              "sans nom : refuse, pas de dialogue (" + lastText(rigA) + ")");
        type(*pa, "securite.nom", "Cl\xC3\xA9 d'essai application");
        find<ui::Checkbox>(*pa, "securite.essai")->setState(ui::Checkbox::State::Checked);
        check(click(*pa, "securite.creer") && rigA.dialogs.size() == 1, "Creer la cle... : une confirmation");
        if (!rigA.dialogs.empty()) {
            rigA.dialogs.back()->attach(&menus);
            const std::string d = drawMenu(*rigA.dialogs.back(), tr);
            check(has(d, "UNE SEULE FOIS") && has(d, "compte Windows") && has(d, "sauvegarde") && has(d, "CL\xC3\x89 D'ESSAI"),
                  "la confirmation dit : une fois, protegee pour ce compte, la sauvegarde juste apres, cle d'essai");
            check(click(*rigA.dialogs.back()->widgetRoot(), "securite.creer.confirmation.non") && !fa.ws->key().present, "Annuler : pas de cle");
        }
        check(click(*pa, "securite.creer") && rigA.dialogs.size() == 2, "une seconde fois");
        if (rigA.dialogs.size() == 2) check(click(*rigA.dialogs.back()->widgetRoot(), "securite.creer.confirmation.oui"), "Creer la cle");
        check(fa.ws->key().present && fa.ws->key().test, "la cle d'essai est creee");
        check(lastOk(rigA) && has(lastText(rigA), "Cl\xC3\xA9 de l'\xC3\xA9" "diteur cr\xC3\xA9\xC3\xA9" "e"), "le rapport : " + lastText(rigA));
        const std::string t = draw(*pa, tr);
        check(has(t, fa.ws->key().id) && has(t, fa.ws->key().fingerprint), "l'identifiant et l'empreinte");
        check(has(t, "CL\xC3\x89 D'ESSAI") && has(t, "jamais pour des clients"), "CLE D'ESSAI dit en clair");
        check(has(t, "Protection") && has(t, "Fichier") && has(t, "Dossier"), "protection, fichier, dossier");
        check(!shown(*pa, "securite.creer") && shown(*pa, "securite.sauver") && shown(*pa, "securite.verifier") && shown(*pa, "securite.exporter"),
              "avec la cle : sauvegarder, verifier, exporter");
        check(has(t, "compil\xC3\xA9" "e dans API Studio et IHM Studio"), "l'export dit ce qui est compile dans les Studios");
    }

    std::printf("-- securite : la phrase refusee avant tout travail lent\n");
    {
        for (const char* id : {"securite.sauver.phrase", "securite.sauver.phrase2", "securite.verifier.phrase"})
            check(find<ui::InputText>(*pa, id)->masked(), std::string("champ masque : ") + id);
        const std::size_t asked0 = rigA.asked.size();
        type(*pa, "securite.sauver.phrase", "court");
        type(*pa, "securite.sauver.phrase2", "court");
        std::string t = draw(*pa, tr);
        check(has(t, "Trop faible"), "l'indication : trop faible");
        check(click(*pa, "securite.sauver") && !lastOk(rigA) && has(lastText(rigA), "12 caract"), "trop courte : refusee (" + lastText(rigA) + ")");
        type(*pa, "securite.sauver.phrase", "123456789012345");
        type(*pa, "securite.sauver.phrase2", "123456789012345");
        check(click(*pa, "securite.sauver") && !lastOk(rigA) && has(lastText(rigA), "trop simple"), "que des chiffres : refusee (" + lastText(rigA) + ")");
        type(*pa, "securite.sauver.phrase", phrase);
        type(*pa, "securite.sauver.phrase2", phrase + " !");
        t = draw(*pa, tr);
        check(has(t, "ne sont pas les m\xC3\xAAmes"), "l'indication : pas les memes");
        check(click(*pa, "securite.sauver") && !lastOk(rigA) && has(lastText(rigA), "pas les m\xC3\xAAmes"), "differentes : refusees (" + lastText(rigA) + ")");
        check(rigA.asked.size() == asked0 && !rigA.bg.running(), "rien demande, aucun travail lance");
    }

    std::printf("-- securite : sauvegarder (Argon2id sur un autre fil)\n");
    const fs::path backup = fa.root / "coffre" / "cle-editeur.xpgcle";
    {
        fs::create_directories(backup.parent_path());
        type(*pa, "securite.sauver.phrase", phrase);
        type(*pa, "securite.sauver.phrase2", phrase);
        std::string t = draw(*pa, tr);
        check(has(t, "Phrase acceptable, la m\xC3\xAAme deux fois"), "l'indication : acceptable");
        rigA.slowOnThread = true;
        rigA.picks.push_back(U(backup));
        check(click(*pa, "securite.sauver"), "Sauvegarder la cle...");
        check(!rigA.asked.empty() && rigA.asked.back().save && has(rigA.asked.back().start, "cle-editeur.xpgcle"),
              "enregistrer sous : cle-editeur.xpgcle");
        check(find<ui::InputText>(*pa, "securite.sauver.phrase")->text().empty() && find<ui::InputText>(*pa, "securite.sauver.phrase2")->text().empty(),
              "les champs sont vides des le lancement");
        check(has(rigA.bg.label(), "Sauvegarde"), "le travail lent : " + rigA.bg.label());
        t = draw(*pa, tr);
        if (rigA.bg.running()) {
            check(has(t, "Sauvegarde de la cl\xC3\xA9 en cours"), "la page dit : en cours");
            check(!enabledW(*pa, "securite.sauver") && !enabledW(*pa, "securite.verifier") && !enabledW(*pa, "securite.exporter"),
                  "pendant ce temps : boutons gris");
        }
        check(rigA.finishSlow(), "la sauvegarde finit");
        check(lastOk(rigA) && has(lastText(rigA), "Sauvegarde de la cl\xC3\xA9 \xC3\xA9" "crite"), "le rapport : " + lastText(rigA));
        check(fs::exists(backup) && has(readFile(backup), fa.ws->key().id), "le fichier .xpgcle, pour cette cle");
        t = draw(*pa, tr);
        check(!has(t, "en cours") && enabledW(*pa, "securite.sauver"), "fini : les boutons reviennent");
    }

    std::printf("-- securite : un travail en cours, tout attend\n");
    {
        Blocker blocker;
        check(blocker.start(rigA), "un travail long commence");
        std::string t = draw(*pa, tr);
        check(has(t, "Un travail est en cours"), "la page le dit");
        check(!enabledW(*pa, "securite.sauver") && !enabledW(*pa, "securite.verifier") && !enabledW(*pa, "securite.exporter"), "les boutons sont gris");
        check(!click(*pa, "securite.verifier"), "un clic n'y fait rien");
        blocker.release.store(true);
        check(rigA.finishSlow(), "le travail long finit");
        t = draw(*pa, tr);
        check(enabledW(*pa, "securite.verifier") && !has(t, "Un travail est en cours"), "puis tout revient");
    }

    std::printf("-- securite : verifier une sauvegarde\n");
    {
        const std::size_t asked0 = rigA.asked.size();
        check(click(*pa, "securite.verifier") && !lastOk(rigA) && has(lastText(rigA), "phrase") && rigA.asked.size() == asked0,
              "sans phrase : refuse avant de choisir le fichier");
        type(*pa, "securite.verifier.phrase", "une autre phrase bien longue");
        rigA.picks.push_back(U(backup));
        check(click(*pa, "securite.verifier") && find<ui::InputText>(*pa, "securite.verifier.phrase")->text().empty(), "lancee : le champ est vide");
        check(rigA.finishSlow() && !lastOk(rigA) && has(lastText(rigA), "Refus"), "une mauvaise phrase : refusee (" + lastText(rigA) + ")");
        type(*pa, "securite.verifier.phrase", phrase);
        rigA.picks.push_back(U(backup));
        check(click(*pa, "securite.verifier") && rigA.finishSlow() && lastOk(rigA) && has(lastText(rigA), "Bonne"), "la bonne : " + lastText(rigA));
    }

    std::printf("-- securite : exporter la cle publique\n");
    {
        rigA.slowOnThread = false;
        const fs::path header = fa.root / "TestPublisherKey.hpp";
        rigA.picks.push_back(U(header));
        check(click(*pa, "securite.exporter"), "Exporter la cle publique...");
        check(!rigA.asked.empty() && rigA.asked.back().save && rigA.asked.back().start == "TestPublisherKey.hpp",
              "enregistrer sous : TestPublisherKey.hpp (une cle d'essai)");
        check(lastOk(rigA) && fs::exists(header) && has(readFile(header), fa.ws->key().id), "l'en-tete contient la cle (" + lastText(rigA) + ")");
        check(find<ui::Checkbox>(*pa, "securite.export.garder") != nullptr, "la case : garder les cles du fichier");
    }

    std::printf("-- securite : restaurer sur un autre poste\n");
    {
        Fixture fb(source, "secu-b");
        HostRig rigB(fb);
        auto pb = gui::makeSecurityPage(rigB.host());
        rigB.refreshers.push_back([&] {
            fb.ws->reload();
            pb->refresh();
        });
        std::string t = draw(*pb, tr);
        check(has(t, "Pas encore de cl\xC3\xA9") && shown(*pb, "securite.restaurer"), "le second poste : pas de cle");
        check(click(*pb, "securite.restaurer") && !lastOk(rigB) && rigB.asked.empty(), "sans phrase : refuse avant de choisir");
        rigB.slowOnThread = true;
        type(*pb, "securite.restaurer.phrase", "une autre phrase bien longue");
        rigB.picks.push_back(U(backup));
        check(click(*pb, "securite.restaurer") && find<ui::InputText>(*pb, "securite.restaurer.phrase")->text().empty(), "lancee : le champ est vide");
        check(rigB.finishSlow() && !lastOk(rigB) && !fb.ws->key().present, "une mauvaise phrase : refusee (" + lastText(rigB) + ")");
        type(*pb, "securite.restaurer.phrase", phrase);
        rigB.picks.push_back(U(backup));
        check(click(*pb, "securite.restaurer") && rigB.finishSlow() && lastOk(rigB), "la bonne : " + lastText(rigB));
        check(fb.ws->key().present && fb.ws->key().id == fa.ws->key().id, "la meme cle sur le second poste");
        t = draw(*pb, tr);
        check(has(t, fa.ws->key().id) && shown(*pb, "securite.sauver"), "la page la montre");
        check(nowhere({&rigA, &rigB}, phrase) && nowhere({&rigA, &rigB}, "cheval agrafe"), "la phrase : jamais dans un rapport, un message, un dessin");
    }

    // =========================================================================================== GitHub
    FakeGitHub fake;
    g_fake = &fake;
    app::ai::setHttpFactory([] { return std::make_unique<FakeClient>(); });

    Fixture fc(source, "github-c");
    check(fc.ready(), "le poste d'essai : cle et registre");
    {
        const auto l = fc.licence("XPG-2026-00001", "Client d'essai Alpha", "PROFESSIONAL", {fc.request("pc-a", "Poste essai A")});
        check(l.ok, "une licence a publier (" + l.text() + ")");
    }
    HostRig rig(fc);
    auto gp = gui::makeGithubPage(rig.host());
    rig.refreshers.push_back([&] {
        fc.ws->reload();
        gp->refresh();
    });
    std::string code;   // le code de secours du premier poste

    std::printf("-- github : l'etat et l'etape 1\n");
    {
        const std::string t = draw(*gp, tr);
        check(gp->title() == "Synchronisation GitHub" && has(t, "Synchronisation GitHub"), "le titre");
        check(has(t, "pas encore r\xC3\xA9gl\xC3\xA9") && has(t, "absent") && has(t, "aucune synchro"), "l'etat : rien de regle");
        check(has(t, "cr\xC3\xA9" "e un d\xC3\xA9p\xC3\xB4t PRIV\xC3\x89 (par exemple licences-xpg)") && has(t, "New repository") && has(t, "Private"),
              "l'etape 1 : le depot prive, le chemin dans GitHub");
        check(has(t, "Pas encore essay\xC3\xA9 contre le vrai GitHub") && has(t, "d\xC3\xA9p\xC3\xB4t public est refus\xC3\xA9") && has(t, "Le jeton ne va jamais dans le d\xC3\xA9p\xC3\xB4t"),
              "les notes honnetes");
        check(find<ui::InputText>(*gp, "github.branche")->text() == "main", "la branche : main");
        check(shown(*gp, "github.depot") && shown(*gp, "github.premier") && shown(*gp, "github.secours") && !shown(*gp, "github.code"),
              "le depot, le choix du poste ; le code seulement pour un autre poste");
        type(*gp, "github.depot", "pas-un-depot");
        check(click(*gp, "github.configurer") && !lastOk(rig) && has(lastText(rig), "PROPRIETAIRE/NOM") && !fc.ws->github().configured,
              "un mauvais depot : refuse (" + lastText(rig) + ")");
    }

    std::printf("-- github : le premier poste et son code de secours\n");
    {
        type(*gp, "github.depot", "https://github.com/editeur/licences-xpg.git");
        check(click(*gp, "github.configurer") && lastOk(rig), "l'adresse du navigateur : acceptee (" + lastText(rig) + ")");
        check(fc.ws->github().configured && fc.ws->github().config.repo == "editeur/licences-xpg" && fc.ws->github().syncKey,
              "regle : editeur/licences-xpg, la cle de synchro creee");
        code = fc.ws->githubRecoveryCode(&why);
        check(code.rfind("XPGS1-", 0) == 0, "le code de secours existe");
        check(!has(lastText(rig), code), "le code n'est pas dans le rapport");
        std::string t = draw(*gp, tr);
        check(has(t, code), "le code est montre, en entier");
        check(has(t, "Note-le MAINTENANT hors de ce PC"), "et ce qu'il faut en faire");
        check(!enabledW(*gp, "github.code.continuer") && !click(*gp, "github.code.continuer"), "Continuer : bloque tant que la case n'est pas cochee");
        check(!shown(*gp, "github.jeton") && !shown(*gp, "github.configurer"), "l'assistant attend");
        g_clipboard.clear();
        check(click(*gp, "github.code.copier") && g_clipboard == code, "Copier : le code dans le presse-papiers (sur ce clic)");
        check(!rig.flashes.empty() && !has(rig.flashes.back(), code), "le message ne contient pas le code");
        find<ui::Checkbox>(*gp, "github.code.note")->setState(ui::Checkbox::State::Checked);
        check(enabledW(*gp, "github.code.continuer") && click(*gp, "github.code.continuer"), "coche : Continuer");
        t = draw(*gp, tr);
        check(!has(t, code), "le code n'est plus montre");
        check(has(t, "2. Le jeton") && has(t, "Settings \xE2\x80\xBA Developer settings \xE2\x80\xBA Personal access tokens \xE2\x80\xBA Fine-grained tokens")
                  && has(t, "Contents : Read and write") && has(t, "Only select repositories") && has(t, "Expiration : 1 an"),
              "l'etape 2 : le chemin exact du jeton");
        check(has(t, fc.ws->github().syncKeyId), "l'etat : l'identifiant de la cle de synchro");
    }

    std::printf("-- github : afficher le code de secours (apres confirmation)\n");
    {
        check(shown(*gp, "github.code.afficher"), "le bouton est la");
        check(click(*gp, "github.code.afficher") && !rig.dialogs.empty(), "une confirmation d'abord");
        if (!rig.dialogs.empty()) {
            rig.dialogs.back()->attach(&menus);
            const std::string d = drawMenu(*rig.dialogs.back(), tr);
            check(has(d, "personne ne regarde ton \xC3\xA9" "cran") && !has(d, code), "la confirmation : pas encore le code");
            check(click(*rig.dialogs.back()->widgetRoot(), "github.code.confirmation.non"), "Annuler");
        }
        std::string t = draw(*gp, tr);
        check(!has(t, code), "annule : pas de code");
        check(click(*gp, "github.code.afficher") && !rig.dialogs.empty() && click(*rig.dialogs.back()->widgetRoot(), "github.code.confirmation.oui"),
              "Afficher le code");
        t = draw(*gp, tr);
        check(has(t, code), "le code est montre");
        check(click(*gp, "github.code.masquer"), "Masquer le code");
        t = draw(*gp, tr);
        check(!has(t, code), "cache de nouveau");
    }

    std::printf("-- github : sans jeton, la synchro refuse\n");
    {
        check(click(*gp, "github.etape3"), "l'etape 3");
        const std::string t = draw(*gp, tr);
        check(has(t, "Il manque le jeton"), "l'etape 3 le dit");
        rig.slowOnThread = true;
        check(click(*gp, "github.verifier") && !lastOk(rig) && has(lastText(rig), "jeton") && !rig.bg.running(),
              "Verifier sans jeton : refuse, aucun travail (" + lastText(rig) + ")");
    }

    std::printf("-- github : le jeton\n");
    {
        check(click(*gp, "github.etape2"), "l'etape 2");
        check(find<ui::InputText>(*gp, "github.jeton")->masked(), "le champ du jeton : masque");
        check(click(*gp, "github.jeton.enregistrer") && !lastOk(rig), "vide : refuse");
        type(*gp, "github.jeton", fake.token);
        check(click(*gp, "github.jeton.enregistrer") && lastOk(rig) && has(lastText(rig), "Jeton GitHub enregistr"), "enregistre : " + lastText(rig));
        check(find<ui::InputText>(*gp, "github.jeton")->text().empty() && fc.ws->github().token, "le champ vide, le jeton sur le poste");
        const std::string t = draw(*gp, tr);
        check(has(t, "enregistr\xC3\xA9 sur ce poste") && !has(t, fake.token), "l'etat : enregistre (jamais sa valeur)");
        check(has(t, "3. V\xC3\xA9rifier, puis synchroniser"), "l'assistant passe a l'etape 3");
        check(fc.ws->githubSetApi("http://127.0.0.1:9").ok, "(l'essai : le faux GitHub)");
        gp->refresh();
    }

    std::printf("-- github : verifier, synchroniser, recevoir (runSlow)\n");
    {
        rig.slowOnThread = true;
        check(click(*gp, "github.verifier") && has(rig.bg.label(), "Synchro GitHub"), "Verifier : un travail lent (" + rig.bg.label() + ")");
        std::string t = draw(*gp, tr);
        if (rig.bg.running()) {
            check(has(t, "Synchro en cours"), "la page dit : Synchro en cours...");
            check(!enabledW(*gp, "github.synchroniser") && !enabledW(*gp, "github.verifier") && !enabledW(*gp, "github.jeton.enregistrer"),
                  "pendant ce temps : boutons gris");
        }
        check(rig.finishSlow(), "la verification finit");
        check(lastOk(rig) && has(lastText(rig), "V\xC3\xA9rifi\xC3\xA9, rien n'a chang\xC3\xA9") && has(lastText(rig), "\xC3\x80 envoyer"), "ce qui serait envoye : " + lastText(rig));
        check(fake.commits.empty(), "rien n'a ete envoye");
        t = draw(*gp, tr);
        check(has(t, "Dernier r\xC3\xA9sultat : v\xC3\xA9rification") && has(t, "\xC3\x80 envoyer"), "le dernier rapport dans la page");

        check(click(*gp, "github.synchroniser") && rig.finishSlow(), "Synchroniser");
        check(lastOk(rig) && has(lastText(rig), "Synchro faite") && has(lastText(rig), "Commit"), "publie : " + lastText(rig));
        check(!fake.commits.empty() && !fake.refs.empty(), "le faux GitHub a recu un commit");
        check(!fake.anyBlobHas(fake.token) && !fake.anyBlobHas("Client d'essai Alpha") && !fake.anyBlobHas("Poste essai A"),
              "dans le depot : ni le jeton, ni le client, ni le PC en clair");
        t = draw(*gp, tr);
        check(has(t, "Envoy\xC3\xA9 (commit") && has(t, "Dernier r\xC3\xA9sultat : synchronisation"), "la page : envoye, le commit");

        check(click(*gp, "github.recevoir") && rig.finishSlow() && lastOk(rig), "Recevoir seulement : " + lastText(rig));
        check(click(*gp, "github.verifier") && rig.finishSlow() && lastOk(rig) && has(lastText(rig), "m\xC3\xAAmes fichiers"), "puis a jour : " + lastText(rig));
    }

    std::printf("-- github : un travail en cours, tout attend\n");
    {
        Blocker blocker;
        check(blocker.start(rig), "un travail long commence");
        const std::string t = draw(*gp, tr);
        check(has(t, "Un travail est en cours"), "la page le dit");
        check(!enabledW(*gp, "github.verifier") && !enabledW(*gp, "github.synchroniser") && !enabledW(*gp, "github.recevoir"), "la synchro : grise");
        check(!click(*gp, "github.synchroniser"), "un clic n'y fait rien");
        blocker.release.store(true);
        check(rig.finishSlow(), "le travail long finit");
        (void)draw(*gp, tr);
        check(enabledW(*gp, "github.synchroniser"), "puis tout revient");
    }

    std::printf("-- github : terminer l'assistant, Modifier\n");
    {
        check(shown(*gp, "github.terminer") && click(*gp, "github.terminer"), "Terminer l'assistant");
        std::string t = draw(*gp, tr);
        check(has(t, "Synchroniser") && shown(*gp, "github.synchroniser") && shown(*gp, "github.modifier") && !shown(*gp, "github.depot"),
              "la carte Synchroniser ; Modifier...");
        check(click(*gp, "github.modifier") && shown(*gp, "github.depot") && find<ui::InputText>(*gp, "github.depot")->text() == "editeur/licences-xpg",
              "Modifier : l'assistant, le depot prerempli");
    }

    std::printf("-- github : un autre poste, par le code de secours\n");
    {
        Fixture fd(source, "github-d");
        {
            const fs::path bk = fc.root / "cle-c.xpgcle";   // un autre PC Admin : la meme cle de l'editeur, restauree
            const auto saved = fc.ws->backupKey(phrase, U(bk));
            const auto restored = fd.ws->restoreKey(U(bk), phrase);
            check(saved.ok && restored.ok, "(le second poste : la cle de l'editeur restauree) " + restored.text());
        }
        HostRig rigD(fd);
        auto dp = gui::makeGithubPage(rigD.host());
        rigD.refreshers.push_back([&] {
            fd.ws->reload();
            dp->refresh();
        });
        (void)draw(*dp, tr);
        find<ui::RadioButton>(*dp, "github.secours")->select();
        (void)draw(*dp, tr);
        check(shown(*dp, "github.code") && find<ui::InputText>(*dp, "github.code")->masked(), "le champ du code : montre, masque");
        check(click(*dp, "github.code.montrer") && !find<ui::InputText>(*dp, "github.code")->masked(), "Montrer : en clair");
        check(click(*dp, "github.code.montrer") && find<ui::InputText>(*dp, "github.code")->masked(), "Masquer : masque");
        type(*dp, "github.depot", "editeur/licences-xpg");
        check(click(*dp, "github.configurer") && !lastOk(rigD), "sans code : refuse");
        type(*dp, "github.code", code);
        check(click(*dp, "github.configurer") && lastOk(rigD), "le code de secours : " + lastText(rigD));
        check(fd.ws->github().syncKeyId == fc.ws->github().syncKeyId, "la meme cle de synchro que le premier poste");
        check(find<ui::InputText>(*dp, "github.code")->text().empty(), "le champ du code est vide");
        std::string t = draw(*dp, tr);
        check(!has(t, code) && !shown(*dp, "github.code.copier"), "pas de code montre sur ce poste");
        check(has(t, "2. Le jeton"), "ensuite : le jeton");
        type(*dp, "github.jeton", fake.token);
        check(click(*dp, "github.jeton.enregistrer") && lastOk(rigD), "le jeton du second poste");
        check(fd.ws->githubSetApi("http://127.0.0.1:9").ok, "(l'essai : le faux GitHub)");
        dp->refresh();
        rigD.slowOnThread = true;
        check(click(*dp, "github.recevoir") && rigD.finishSlow() && lastOk(rigD), "Recevoir seulement : " + lastText(rigD));
        fd.ws->reload();
        check(fd.ws->licence("XPG-2026-00001") != nullptr, "la licence du premier poste est recue");
        t = draw(*dp, tr);
        check(has(t, "Re\xC3\xA7u") && has(t, "Dernier r\xC3\xA9sultat : r\xC3\xA9" "ception"), "la page : recu");

        fake.isPrivate = false;
        check(click(*dp, "github.verifier") && rigD.finishSlow() && !lastOk(rigD) && has(lastText(rigD), "PUBLIC"),
              "un depot public : refuse (" + lastText(rigD) + ")");
        t = draw(*dp, tr);
        check(has(t, "PUBLIC"), "la page le dit");
        fake.isPrivate = true;
        check(nowhere({&rigD}, fake.token), "le jeton du second poste : jamais montre");
    }

    std::printf("-- github : retirer le jeton\n");
    {
        check(click(*gp, "github.etape2") && enabledW(*gp, "github.jeton.retirer"), "Retirer le jeton : possible");
        check(click(*gp, "github.jeton.retirer") && lastOk(rig) && has(lastText(rig), "r\xC3\xA9voquer"), "retire, et le rappel de le revoquer : " + lastText(rig));
        check(!fc.ws->github().token, "plus de jeton sur le poste");
        const std::string t = draw(*gp, tr);
        check(has(t, "absent") && has(t, "aucun jeton"), "l'etat : absent");
        check(!enabledW(*gp, "github.jeton.retirer"), "Retirer : gris sans jeton");
    }

    std::printf("-- les secrets\n");
    {
        check(nowhere({&rig}, fake.token), "le jeton : jamais dans un rapport, un message, un dessin");
        std::size_t paintedWithCode = 0;
        for (const auto& t : g_painted)
            if (has(t, code)) ++paintedWithCode;
        check(paintedWithCode == 2, "le code de secours : dessine seulement deux fois (le premier reglage, puis Afficher) : "
                                        + std::to_string(paintedWithCode));
        bool inReports = false;
        for (const auto* r : {&rig})
            for (const auto& x : r->reports) inReports = inReports || has(x.text(), code);
        for (const auto& f : rig.flashes) inReports = inReports || has(f, code);
        check(!inReports, "le code de secours : jamais dans un rapport ni un message");
    }

    return finish();
}
