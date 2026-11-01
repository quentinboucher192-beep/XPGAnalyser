// =============================================================================
//  tests/ai_notifications_test.cpp - 1.21.2 : LES NOTIFICATIONS DE L'ASSISTANT IA
// -----------------------------------------------------------------------------
//  << Un onglet en haut, notifications, pour les choses importantes >> - dans le
//  panneau de l'Assistant. Sans ecran ni reseau, sur un dossier IA/ jetable :
//    1. le journal (AiNotifications) : post, la cle qui remplace et compte, lu /
//       non lu, supprimer, effacer les lues, la borne (les plus anciennes lues
//       d'abord), sur entre fils, les liens du bouton, les jours et les heures ;
//    2. la garde sur le disque : relue, ecrite sur le fil du journal (au plus une
//       fois par seconde), un aller-retour, la fusion avec ce qui a ete poste avant ;
//    3. la liste (AiNotificationsView) : les jours, les lignes, Toutes / Non lues,
//       une ligne choisie devient lue, Tout marquer comme lu, Effacer les lues, le
//       bouton qui rend son lien, le clic droit, le clavier, le dessin ;
//    4. le panneau : les onglets Conversation | Notifications, la pastille (rouge
//       pour une erreur non lue), l'infobulle, un clic aller et retour ;
//    5. les sources de l'Assistant : une proposition prete hors de la vue, une
//       proposition appliquee, une erreur du service (x2), une piece jointe
//       illisible, un scan prepare par l'IA, un modele de fichier, l'analyse IA d'un
//       incident - et chaque bouton qui mene a la chose.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiNotifications.hpp"
#include "../src/app/ai/AiNotificationsView.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace nt = app::ai::notifs;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string readAll(const fs::path& p) {
    std::string s;
    (void)core::readFileAll(p, s);
    return s;
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

fs::path scratch(const std::string& name) {
    const fs::path p = fs::temp_directory_path() / ("xpg_ai_notifications_" + name);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

nt::Item item(const std::string& title, const std::string& tone = "info", const std::string& key = {}, const std::string& action = {}) {
    nt::Item n;
    n.category = "Essai";
    n.title = title;
    n.detail = "le d\xC3\xA9tail de " + title;
    n.tone = tone;
    n.key = key;
    if (!action.empty()) {
        n.button = "Ouvrir";
        n.action = action;
    }
    return n;
}

std::int64_t localTime(int year, int month, int day, int hour, int minute) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

// Un renderer qui retient les textes (les painters y passent).
struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex*, std::size_t) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
};

std::string g_clipboard;

// ======================================================================== 1. le journal ==
void testJournal() {
    std::printf("[1. le journal]\n");
    nt::resetForTests();
    const auto rev0 = nt::revision();
    const auto a = nt::post(item("A"));
    const auto b = nt::post(item("B", "warning"));
    const auto c = nt::post(item("C", "error"));
    auto l = nt::list();
    check(l.size() == 3 && l[0].title == "C" && l[2].title == "A", "la plus recente d'abord");
    check(nt::unread() == 3 && nt::unreadError(), "3 non lues, dont une erreur");
    check(nt::revision() != rev0, "la revision change");
    check(l[0].at > 0 && l[0].id == c && !l[0].read && l[0].count == 1, "l'heure, l'identifiant, non lue, une fois");
    // lu / non lu
    check(nt::markRead(c) && !nt::unreadError() && nt::unread() == 2, "l'erreur lue : la pastille n'est plus rouge");
    check(!nt::markRead(c), "deja lue : rien");
    check(nt::markRead(c, false) && nt::unreadError(), "de nouveau non lue");
    // la cle : remplace, remonte, compte, redevient non lue
    const auto k1 = nt::post(item("Cl\xC3\xA9 refus\xC3\xA9" "e", "error", "service:essai:401"));
    (void)nt::markRead(k1);
    (void)nt::post(item("D"));
    const auto k2 = nt::post(item("Cl\xC3\xA9 refus\xC3\xA9" "e (encore)", "error", "service:essai:401"));
    l = nt::list();
    check(k2 == k1 && l.size() == 5, "la meme cle : la meme notification (pas une de plus)");
    check(l[0].id == k1 && l[0].count == 2 && !l[0].read && l[0].title == "Cl\xC3\xA9 refus\xC3\xA9" "e (encore)",
          "elle remonte en tete, non lue, x2, son nouveau texte");
    (void)nt::post(item("x", "error", "service:essai:401"));
    check(nt::find(k1) && nt::find(k1)->count == 3, "x3");
    // supprimer, tout lire, effacer les lues
    check(nt::remove(a) && !nt::find(a) && !nt::remove(a), "supprimer");
    check(nt::markAllRead() == 4 && nt::unread() == 0, "tout marquer comme lu");
    (void)nt::post(item("E"));
    check(nt::clearRead() == 4 && nt::size() == 1 && nt::list()[0].title == "E", "effacer les lues : la non lue reste");
    (void)b;
    // la borne : les plus anciennes lues d'abord, puis les plus anciennes
    nt::resetForTests();
    for (int i = 0; i < 10; ++i) (void)nt::markRead(nt::post(item("lue " + std::to_string(i))));
    for (int i = 0; i < 295; ++i) (void)nt::post(item("non lue " + std::to_string(i)));
    l = nt::list();
    std::size_t read = 0;
    for (const auto& x : l) read += x.read ? 1 : 0;
    check(l.size() == nt::kMax && read == 5 && l.back().title == "lue 5", "305 : les 5 plus anciennes LUES partent (" + std::to_string(read) + ")");
    for (int i = 0; i < 10; ++i) (void)nt::post(item("encore " + std::to_string(i)));
    l = nt::list();
    read = 0;
    for (const auto& x : l) read += x.read ? 1 : 0;
    check(l.size() == nt::kMax && read == 0 && l.back().title == "non lue 5", "plus de lues : les plus anciennes partent (" + l.back().title + ")");
    // sur entre fils : 4 fils, 100 notifications chacun, 25 cles
    nt::resetForTests();
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([t] {
            for (int i = 0; i < 100; ++i) (void)nt::post(item("fil " + std::to_string(t), "info", "fil:" + std::to_string(i % 25)));
        });
    for (auto& t : threads) t.join();
    int total = 0;
    for (const auto& x : nt::list()) total += x.count;
    check(nt::size() == 25 && total == 400, "4 fils x 100 : 25 lignes, 400 apparitions (" + std::to_string(total) + ")");
    // les liens du bouton
    const auto t1 = nt::parseAction(nt::toCard("c-20261101-1", 7));
    check(t1.kind == "carte" && t1.id == "c-20261101-1" && t1.message == 7, "carte:conversation#7");
    const auto t2 = nt::parseAction(nt::toConversation("c-x"));
    check(t2.kind == "conv" && t2.id == "c-x" && t2.message < 0, "conv:conversation (sans message)");
    check(nt::parseAction(nt::toAttachment("p-1")).kind == "piece" && nt::parseAction(nt::toFileModel("p-1")).id == "p-1", "piece:, modele:");
    check(nt::parseAction(nt::toSouvenirs("conflits")).id == "conflits" && nt::parseAction(nt::toScanReport()).kind == "scan"
              && nt::parseAction(nt::toIncident("20261101-POMPE")).id == "20261101-POMPE" && nt::parseAction("parametres").kind == "parametres",
          "souvenirs:, scan, incident:, parametres");
    check(nt::parseAction("inconnu:x").kind.empty() && nt::parseAction("carte:c-x").kind.empty() && nt::parseAction("").kind.empty(),
          "un lien inconnu ou incomplet : rien");
    // les jours et les heures
    const auto now = localTime(2026, 11, 1, 12, 0);
    check(nt::dayLabel(localTime(2026, 11, 1, 0, 5), now) == "Aujourd'hui" && nt::dayLabel(localTime(2026, 10, 31, 23, 30), now) == "Hier",
          "Aujourd'hui, Hier");
    check(nt::dayLabel(localTime(2026, 10, 28, 9, 0), now) == "mercredi 28 octobre 2026", "la date : " + nt::dayLabel(localTime(2026, 10, 28, 9, 0), now));
    check(nt::timeLabel(localTime(2026, 11, 1, 14, 5)) == "14:05", "l'heure : 14:05");
    check(nt::oneLine("**Gras**\n`code` et  suite") == "Gras code et suite", "une ligne : " + nt::oneLine("**Gras**\n`code` et  suite"));
    check(nt::oneLine(std::string(200, 'a'), 20).size() == 23 && has(nt::oneLine(std::string(200, 'a'), 20), "\xE2\x80\xA6"), "coupee avec ...");
}

// ================================================================ 2. la garde sur le disque ==
void testDisk() {
    std::printf("[2. la garde sur le disque]\n");
    nt::resetForTests();
    const fs::path dir = scratch("disque");
    const std::string file = U(dir / "IA" / "notifications.json");
    nt::open(file);
    check(nt::opened() && nt::waitIdle(3000), "ouvert : relu (le fichier n'existe pas encore)");
    (void)nt::post(item("Proposition appliqu\xC3\xA9" "e", "ok", "proposition:p1", nt::toCard("c-1", 3)));
    const auto e = nt::post(item("Service", "error", "service:x:401"));
    (void)nt::post(item("Service", "error", "service:x:401"));
    (void)nt::markRead(e);
    check(nt::waitIdle(5000), "ecrit sur le fil du journal");
    const auto t0 = std::chrono::steady_clock::now();
    std::string text = readAll(dir / "IA" / "notifications.json");
    check(has(text, "\"notifications\"") && has(text, "\"cle\": \"service:x:401\"") && has(text, "\"fois\": 2") && has(text, "\"lue\": true"),
          "le fichier : les notifications, la cle, x2, lue");
    // au plus une fois par seconde : un changement juste apres une ecriture attend
    (void)nt::post(item("Juste apr\xC3\xA8s"));
    const std::string soon = readAll(dir / "IA" / "notifications.json");
    if (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500))
        check(!has(soon, "Juste apr"), "un changement dans la seconde : pas encore ecrit (au plus une fois par seconde)");
    check(nt::waitIdle(5000) && has(readAll(dir / "IA" / "notifications.json"), "Juste apr"), "... puis ecrit");
    std::vector<nt::Item> back;
    check(nt::fromJson(readAll(dir / "IA" / "notifications.json"), back) && back.size() == 3 && back.back().title == "Juste apr\xC3\xA8s",
          "relu : les plus anciennes d'abord");
    nt::close();
    check(!nt::opened(), "ferme");
    // un aller-retour : tout oublier, rouvrir - et ce qui a ete poste AVANT la relecture garde sa place (plus recent)
    nt::resetForTests();
    (void)nt::post(item("Service", "error", "service:x:401"));
    (void)nt::post(item("Avant la relecture"));
    nt::open(file);
    check(nt::waitIdle(5000), "rouvert, relu");
    const auto l = nt::list();
    check(l.size() == 4 && l[0].title == "Avant la relecture" && l.back().title == "Proposition appliqu\xC3\xA9" "e",
          "fusionnees : les relues avant (" + std::to_string(l.size()) + ")");
    const auto svc = std::find_if(l.begin(), l.end(), [](const nt::Item& x) { return x.key == "service:x:401"; });
    check(svc != l.end() && svc->count == 3 && std::count_if(l.begin(), l.end(), [](const nt::Item& x) { return x.key == "service:x:401"; }) == 1,
          "la meme cle relue : une ligne, ses apparitions ajoutees (x3)");
    const auto prop = std::find_if(l.begin(), l.end(), [](const nt::Item& x) { return x.key == "proposition:p1"; });
    check(prop != l.end() && prop->action == "carte:c-1#3" && prop->button == "Ouvrir" && prop->tone == "ok" && !prop->read, "le lien, le bouton, le ton relus");
    nt::close();
    // un fichier illisible : on repart vide, et c'est dit
    nt::resetForTests();
    {
        std::ofstream f(dir / "IA" / "notifications.json", std::ios::binary | std::ios::trunc);
        f << "{ pas du json";
    }
    nt::open(file);
    check(nt::waitIdle(5000) && nt::size() == 0 && has(nt::lastError(), "illisibles"), "illisible : vide, dit (" + nt::lastError() + ")");
    nt::close();
    nt::resetForTests();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// =========================================================================== 3. la liste ==
void testView() {
    std::printf("[3. la liste]\n");
    nt::resetForTests();
    const auto now = nt::nowSeconds();
    nt::Item old = item("Hier soir", "warning", {}, nt::toConversation("c-hier", 2));
    old.at = now - 86400;
    (void)nt::post(old);
    (void)nt::post(item("Une information"));
    (void)nt::post(item("R\xC3\xA9ponse pr\xC3\xAAte", "info", "reponse:c-1:m4", nt::toConversation("c-1", 3)));
    for (int i = 0; i < 3; ++i) (void)nt::post(item("Cl\xC3\xA9 API refus\xC3\xA9" "e par le service", "error", "service:openai:401", nt::toConversation("c-1", 5)));
    std::vector<std::string> followed, said;
    ai::AiNotificationsView::Callbacks cb;
    cb.follow = [&](const std::string& a) {
        followed.push_back(a);
        return true;
    };
    cb.status = [&](const std::string& s) { said.push_back(s); };
    ai::AiNotificationsView v("vue", cb);
    v.setBounds({0, 0, 420, 700});
    v.layout();
    check(v.rowCount() == 4 && v.unreadCount() == 4, "4 lignes, 4 non lues");
    const auto lines = v.lines();
    check(lines.size() == 6 && lines[0] == "== Aujourd'hui" && lines[4] == "== Hier", "rangees par jour : Aujourd'hui, Hier");
    const std::string first = v.rowText(0);
    check(has(first, "Cl\xC3\xA9 API refus\xC3\xA9" "e") && has(first, "\xC3\x97" "3") && has(first, "non lue") && has(first, "Essai"),
          "la plus recente : son titre, x3, non lue, sa categorie : " + first);
    // le dessin : les filtres, le titre, x3, le bouton
    {
        Recorder rec;
        v.render(ui::PaintContext{rec, ui::Theme::dark(), v.bounds(), 1.0, nullptr});
        check(rec.saw("Toutes") && rec.saw("Non lues") && rec.saw("Cl\xC3\xA9 API") && rec.saw("\xC3\x97" "3") && rec.saw("Ouvrir \xE2\x80\xBA")
                  && rec.saw("Aujourd'hui") && rec.saw("Hier"),
              "dessinee : les filtres, les jours, le titre, x3, le bouton");
        Recorder light;
        v.render(ui::PaintContext{light, ui::Theme::light(), v.bounds(), 1.0, nullptr});
        check(light.saw("Non lues"), "dessinee aussi en theme clair");
    }
    // une ligne choisie devient lue ; le filtre Non lues la garde jusqu'au prochain choix
    v.setFilter(ai::AiNotificationsView::Filter::Unread);
    check(v.rowCount() == 4, "Non lues : 4");
    const auto r1 = v.rowRect(1);
    check(r1.w > 0.f, "la 2e ligne a l'ecran");
    (void)v.dispatch(ui::MouseDown{{r1.x + 60.f, r1.y + 10.f}, ui::MouseButton::Left, 1, {}});
    check(v.selectedRow() == 1 && v.rowItem(1) && v.rowItem(1)->read && v.unreadCount() == 3, "un clic : choisie, lue");
    check(v.rowCount() == 4, "Non lues : elle reste a sa place (grisee) tant que le filtre ne change pas");
    (void)v.dispatch(ui::MouseDown{{v.filterRect(ai::AiNotificationsView::Filter::Unread).x + 5.f, v.filterRect(ai::AiNotificationsView::Filter::Unread).y + 5.f},
                                   ui::MouseButton::Left, 1, {}});
    check(v.rowCount() == 3, "Non lues choisi de nouveau : 3");
    (void)v.dispatch(ui::MouseDown{{v.filterRect(ai::AiNotificationsView::Filter::All).x + 5.f, v.filterRect(ai::AiNotificationsView::Filter::All).y + 5.f},
                                   ui::MouseButton::Left, 1, {}});
    check(v.filter() == ai::AiNotificationsView::Filter::All && v.rowCount() == 4, "Toutes : 4");
    // le bouton rend son lien (et la ligne devient lue)
    v.layout();
    const auto br = v.buttonRect(0);
    check(br.w > 0.f, "le bouton de la premiere ligne");
    (void)v.dispatch(ui::MouseDown{{br.x + 4.f, br.y + 4.f}, ui::MouseButton::Left, 1, {}});
    check(followed.size() == 1 && followed[0] == "conv:c-1#5" && v.rowItem(0)->read, "le bouton : conv:c-1#5, lue");
    check(!v.follow(2) && has(said.back(), "pas de lien"), "une notification sans lien : le dit");
    // le clavier
    v.takeFocus();
    (void)v.dispatch(ui::KeyDown{ui::Key::Home, {}, false});
    (void)v.dispatch(ui::KeyDown{ui::Key::Down, {}, false});
    check(v.selectedRow() == 1, "Debut, Bas : la 2e");
    (void)v.dispatch(ui::KeyDown{ui::Key::Return, {}, false});
    check(followed.size() == 2 && followed[1] == "conv:c-1#3", "Entree : son bouton");
    (void)v.dispatch(ui::KeyDown{ui::Key::Space, {}, false});
    check(v.rowItem(1) && !v.rowItem(1)->read, "Espace : de nouveau non lue");
    (void)v.dispatch(ui::KeyDown{ui::Key::C, {true, false, false, false}, false});
    check(has(g_clipboard, "R\xC3\xA9ponse pr\xC3\xAAte") && has(g_clipboard, "Essai"), "Ctrl+C : copiee (" + g_clipboard + ")");
    // le clic droit : Ouvrir, Marquer comme lue / non lue, Copier, Supprimer
    const auto r2 = v.rowRect(2);
    (void)v.dispatch(ui::MouseDown{{r2.x + 60.f, r2.y + 10.f}, ui::MouseButton::Right, 1, {}});
    bool open = false, unread = false, copy = false, del = false;
    for (const auto& it : v.menu()->items()) {
        open |= it.id == ai::AiNotificationsView::kMenuFollow && !it.enabled;   // "Une information" : pas de lien
        unread |= it.id == ai::AiNotificationsView::kMenuUnread && it.label == "Marquer comme non lue";
        copy |= it.id == ai::AiNotificationsView::kMenuCopy;
        del |= it.id == ai::AiNotificationsView::kMenuDelete && it.shortcut == "Suppr";
    }
    check(v.menu()->isOpen() && open && unread && copy && del, "le menu : Ouvrir (grise sans lien), Marquer comme non lue, Copier, Supprimer");
    v.menu()->itemChosen->emit(ai::AiNotificationsView::kMenuDelete);
    v.menu()->close();
    check(v.rowCount() == 3 && nt::size() == 3, "Supprimer");
    (void)v.dispatch(ui::KeyDown{ui::Key::Delete, {}, false});
    check(v.rowCount() == 2, "Suppr");
    // Tout marquer comme lu, Effacer les lues (grises quand il n'y a rien a faire)
    (void)nt::post(item("Nouvelle"));
    check(v.poll() && v.rowCount() == 3, "le journal change : la liste suit (poll)");
    check(v.markAllButton().enabled(), "Tout marquer comme lu : actif");
    v.markAllButton().clicked->emit();
    check(v.unreadCount() == 0 && !v.markAllButton().enabled() && v.clearReadButton().enabled(), "tout est lu : grise ; Effacer les lues : actif");
    v.clearReadButton().clicked->emit();
    check(v.rowCount() == 0 && nt::size() == 0 && !v.clearReadButton().enabled(), "Effacer les lues : vide, grise");
    {
        Recorder rec;
        v.render(ui::PaintContext{rec, ui::Theme::dark(), v.bounds(), 1.0, nullptr});
        check(rec.saw("Aucune notification."), "vide : le dit");
    }
}

// ============================================================= 4 et 5. le panneau, les sources ==
struct FakeScript {
    std::deque<std::vector<ai::ProviderEvent>> replies;
};
class FakeProvider final : public ai::Provider {
public:
    explicit FakeProvider(FakeScript& s) : s_(s) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "openai"; }
    bool start(ai::ProviderRequest, std::string*) override {
        if (!s_.replies.empty()) {
            pending_ = std::move(s_.replies.front());
            s_.replies.pop_front();
        }
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        busy_ = false;
        return std::exchange(pending_, {});
    }
    void cancel() override { busy_ = false; }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    FakeScript&                    s_;
    std::vector<ai::ProviderEvent> pending_;
    bool                           busy_{false};
};

struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};
    std::uint64_t                scanSerial{0};
    std::string                  scanText;
    std::vector<std::string>     opened;   // ce que les boutons ont ouvert chez l'hote
    std::vector<std::string>     statuses;

    Fixture() {
        dir = scratch("panneau");
        ai::setRoot(U(dir / "IA"));
        auto& p = doc->project;
        p.views = {hmi::makeView(p, "Vue_Station")};
        view = p.views.front().id;
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        core.projectName = [] { return std::string("Station"); };
        core.projectPath = [this] { return U(dir / "Station"); };
        core.scanSerial = [this] { return scanSerial; };
        core.scanReportText = [this] { return scanText; };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        ui.status = [this](const std::string& s) { statuses.push_back(s); };
        ui.openSouvenirs = [this](const std::string& status) { opened.push_back("souvenirs:" + status); };
        ui.showScanReport = [this] {
            opened.push_back("scan");
            return true;
        };
        ui.openIncident = [this](const std::string& folder) {
            opened.push_back("incident:" + folder);
            return folder == "20261101-101500-POMPE_1-a1b2c3d4";
        };
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 440, 900});
        panel->ensureLoaded();
        ai::Settings st = panel->assistant().settings();
        st.provider = "demo";
        panel->assistant().setSettings(st);
        panel->layout();
    }
    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
    void step(double dt = 0.05) {
        t += dt;
        panel->tick(t);
        mm.applyPending();
        panel->layout();
    }
    void finish() {
        for (int guard = 0; (as().generating() || as().processing()) && guard < 4000; ++guard) step();
        step();
    }
    [[nodiscard]] int lastProposal() {
        const auto* c = as().current();
        if (!c) return -1;
        for (int i = static_cast<int>(c->messages.size()) - 1; i >= 0; --i)
            if (c->messages[static_cast<std::size_t>(i)].proposal) return i;
        return -1;
    }
    [[nodiscard]] std::optional<nt::Item> latest(const std::string& titlePart) const {
        for (const auto& n : nt::list())
            if (has(n.title, titlePart)) return n;
        return std::nullopt;
    }
    void click(const gfx::Rect& r) { (void)panel->dispatch(ui::MouseDown{{r.x + r.w / 2.f, r.y + r.h / 2.f}, ui::MouseButton::Left, 1, {}}); }
};

void testPanel() {
    std::printf("[4. le panneau : l'onglet et sa pastille]\n");
    nt::resetForTests();
    Fixture f;
    f.step();
    const auto conv = f.panel->conversationTabRect(), notes = f.panel->notificationsTabRect();
    check(conv.w > 0.f && notes.w > 0.f && notes.x >= conv.right() && notes.right() <= 440.f && conv.y >= 38.f,
          "deux onglets sous l'en-tete : Conversation, Notifications");
    check(f.panel->badgeCount() == 0 && !f.panel->badgeAlert(), "rien : pas de pastille");
    (void)nt::post(item("Une information"));
    f.step();
    check(f.panel->badgeCount() == 1 && !f.panel->badgeAlert(), "une non lue : la pastille (l'accent)");
    (void)nt::post(item("Cl\xC3\xA9 refus\xC3\xA9" "e", "error"));
    f.step();
    check(f.panel->badgeCount() == 2 && f.panel->badgeAlert(), "une erreur non lue : la pastille rouge");
    {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("Conversation") && rec.saw("Notifications") && std::find(rec.texts.begin(), rec.texts.end(), "2") != rec.texts.end(),
              "dessines : les onglets et la pastille 2");
        const auto r = f.panel->notificationsTabRect();
        const std::string tip = f.panel->liveTooltip({r.x + 20.f, r.y + 10.f});
        check(has(tip, "2 notifications non lues") && has(tip, "dont une erreur") && has(tip, "Cl\xC3\xA9 refus\xC3\xA9" "e"),
              "l'infobulle : le nombre et la plus recente (" + tip + ")");
    }
    // un clic : les notifications a la place de la conversation ; un autre : retour
    f.click(f.panel->notificationsTabRect());
    f.panel->layout();
    check(f.panel->notificationsShown() && f.panel->notifications().visible() && !f.panel->chat().visible() && !f.panel->prompt().visible(),
          "un clic : la liste, la conversation cachee");
    check(f.panel->notifications().rowCount() == 2 && f.panel->notifications().bounds().y >= f.panel->notificationsTabRect().bottom() - 0.5f,
          "la liste sous les onglets");
    {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("Cl\xC3\xA9 refus\xC3\xA9" "e") && !rec.saw("CONVERSATIONS ("), "dessinee : les notifications, pas la liste des conversations");
    }
    f.click(f.panel->notificationsTabRect());
    f.panel->layout();
    check(!f.panel->notificationsShown() && f.panel->chat().visible() && !f.panel->notifications().visible(), "un autre clic : la conversation revient");
    f.click(f.panel->notificationsTabRect());
    f.click(f.panel->conversationTabRect());
    check(!f.panel->notificationsShown(), "l'onglet Conversation y revient aussi");
}

void testSources() {
    std::printf("[5. les sources de l'Assistant, et ce que leurs boutons ouvrent]\n");
    nt::resetForTests();
    Fixture f;
    f.as().newConversation();
    f.panel->sync();
    const std::string convA = f.as().current()->id;
    // 5a. une proposition prete pendant que l'onglet Notifications est devant
    check(f.panel->sendText("Ajoute une cuve avec un indicateur de niveau"), "envoyer");
    f.panel->showNotifications(true);
    f.finish();
    const int m = f.lastProposal();
    auto ready = f.latest("Proposition pr\xC3\xAAte");
    check(m >= 0 && ready && ready->action == nt::toCard(convA, m) && ready->button == "Voir la carte" && !ready->read,
          "proposition prete hors de la vue : notee, son bouton mene a sa carte");
    check(f.panel->badgeCount() >= 1, "la pastille la compte");
    // le bouton : la conversation, la carte
    f.as().newConversation();
    f.panel->sync();
    check(f.as().current()->id != convA, "une autre conversation ouverte");
    check(f.panel->followNotification(ready->action) && !f.panel->notificationsShown() && f.as().current()->id == convA,
          "Voir la carte : l'onglet Conversation, la conversation de la proposition");
    // la meme reponse vue (la conversation a l'ecran) : rien de plus
    const auto before = nt::size();
    (void)f.panel->sendText("Ajoute une cuve avec un indicateur de niveau");
    f.finish();
    check(!f.latest("R\xC3\xA9ponse pr\xC3\xAAte") && nt::size() == before, "une reponse vue arriver : pas de notification");
    // 5b. une proposition appliquee
    const int m2 = f.lastProposal();
    check(f.panel->applyProposal(m2), "appliquer");
    f.step();
    const auto applied = f.latest("Proposition appliqu\xC3\xA9" "e");
    check(applied && applied->tone == "ok" && applied->action == nt::toCard(convA, m2) && applied->category == "Proposition",
          "proposition appliquee : notee (ok), son bouton mene a sa carte");
    // 5c. une erreur du service (une cle refusee), deux fois : une ligne, x2
    FakeScript s;
    f.as().setProviderFactory([&s](std::string_view) { return std::make_unique<FakeProvider>(s); });
    {
        ai::Settings st = f.as().settings();
        st.provider = "openai";
        st.preset = "openrouter";
        st.endpoint = "https://openrouter.ai/api/v1";
        st.model = "modele-essai";
        f.as().setSettings(st);
        f.as().setApiKey("cle-essai");
    }
    for (int k = 0; k < 2; ++k) {
        ai::ProviderEvent e;
        e.type = ai::ProviderEvent::Type::Error;
        e.httpStatus = 401;
        e.text = "Cl\xC3\xA9 API refus\xC3\xA9" "e (401) : v\xC3\xA9rifie-la dans les Param\xC3\xA8tres.";
        s.replies.push_back({e});
        check(f.panel->sendText("Bonjour"), "envoyer a un service (" + std::to_string(k + 1) + ")");
        for (int g = 0; g < 50 && f.as().generating(); ++g) f.step();
    }
    const auto svc = f.latest("Cl\xC3\xA9 API refus\xC3\xA9" "e par le service");
    const auto* cur = f.as().current();
    check(svc && svc->tone == "error" && svc->count == 2 && has(svc->detail, "v\xC3\xA9rifie-la") && cur
              && svc->action == nt::toConversation(cur->id, static_cast<int>(cur->messages.size()) - 1),
          "erreur du service : notee (erreur), x2, son texte, son bouton mene au message");
    check(f.panel->badgeAlert(), "une erreur non lue : la pastille rouge");
    {
        ai::Settings st = f.as().settings();
        st.provider = "demo";
        f.as().setSettings(st);
    }
    // 5d. une piece jointe illisible (un .xlsx qui n'est pas un classeur)
    const fs::path bad = f.dir / "casse.xlsx";
    {
        std::ofstream out(bad, std::ios::binary);
        out << "ce n'est pas un classeur";
    }
    check(f.panel->attachFile(U(bad)), "joindre casse.xlsx");
    std::optional<nt::Item> piece;
    for (int g = 0; g < 400 && !piece; ++g) {
        f.step();
        piece = f.latest("Pi\xC3\xA8" "ce jointe illisible");
        if (!piece) pause(5);
    }
    check(piece && piece->tone == "error" && has(piece->title, "casse.xlsx") && nt::parseAction(piece->action).kind == "piece",
          "piece jointe illisible : notee, son bouton mene a la piece");
    if (piece) {
        f.panel->showNotifications(true);
        check(f.panel->followNotification(piece->action) && !f.panel->notificationsShown() && has(f.statuses.back(), "contexte de la discussion"),
              "Voir la piece : le contexte de la discussion");
    }
    // 5e. un scan prepare par l'IA
    f.as().expectScan();
    f.scanText = "3 appareils trouv\xC3\xA9s : 2 d\xC3\xA9" "clar\xC3\xA9s, 1 absent du projet.\n\n192.168.1.10 - Automate";
    f.scanSerial = 7;
    f.step();
    const auto scan = f.latest("Scan r\xC3\xA9seau termin\xC3\xA9");
    check(scan && has(scan->title, "3 appareils trouv\xC3\xA9s") && scan->action == "scan", "scan termine : note, la premiere ligne du compte rendu");
    if (scan) check(f.panel->followNotification(scan->action) && f.opened.back() == "scan", "Voir le compte rendu : l'hote l'ouvre");
    f.scanSerial = 8;
    f.step();
    const auto all = nt::list();
    const auto scans = std::count_if(all.begin(), all.end(), [](const nt::Item& n) { return n.action == "scan"; });
    check(scans == 1, "un autre scan, pas prepare par l'IA : rien de plus");
    // 5f. un modele de fichier enregistre (la fenetre Modele de fichier), l'analyse IA d'un incident, des souvenirs
    nt::fileModelSaved("Liste E/S Station", "Liste d'entr\xC3\xA9" "es/sorties", "p-essai", "io.xlsx");
    const auto model = f.latest("Mod\xC3\xA8le \xC2\xAB Liste E/S Station \xC2\xBB");
    check(model && model->action == "modele:p-essai" && model->tone == "ok", "modele de fichier enregistre : note, son bouton mene a la fenetre Modele");
    if (model) check(!f.panel->followNotification(model->action) && has(f.statuses.back(), "n'est plus"), "une piece introuvable : le dit");
    nt::incidentAnalysed("POMPE_1_DEFAUT", "20261101-101500-POMPE_1-a1b2c3d4", true, "Anthropic (claude)", {});
    const auto inc = f.latest("Analyse IA termin\xC3\xA9" "e");
    check(inc && inc->action == "incident:20261101-101500-POMPE_1-a1b2c3d4" && has(inc->title, "POMPE_1_DEFAUT"), "analyse IA d'un incident : notee");
    if (inc) check(f.panel->followNotification(inc->action) && f.opened.back() == "incident:20261101-101500-POMPE_1-a1b2c3d4", "Ouvrir l'incident");
    check(!f.panel->followNotification(nt::toIncident("disparu")) && has(f.statuses.back(), "introuvable"), "un incident disparu : le dit");
    check(f.panel->followNotification(nt::toSouvenirs("conflits")) && f.opened.back() == "souvenirs:conflits", "Resoudre le conflit : les Souvenirs filtres");
    check(!f.panel->followNotification(nt::toConversation("c-disparue")) && has(f.statuses.back(), "n'existe plus"), "une conversation supprimee : le dit");
    // tout est sur le disque ? (le journal n'est pas ouvert ici : rien n'est ecrit)
    check(!fs::exists(f.dir / "IA" / "notifications.json"), "sans open() : rien sur le disque");
    std::error_code ec;
    fs::remove_all(f.dir, ec);
    nt::resetForTests();
}

} // namespace

int main() {
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
        s.clipboardGet = [] { return g_clipboard; };
        ui::installPlatformServices(std::move(s));
    }
    testJournal();
    testDisk();
    testView();
    testPanel();
    testSources();
    std::printf("ai_notifications_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
