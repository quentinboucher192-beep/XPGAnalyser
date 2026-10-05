// Lot API 8 : le bandeau bas - le journal des messages (les 50 derniers, un durable
// qui en remplace un autre en moins de 1,5 s prend sa place, le meme redit ne change
// que son heure, Copier, Vider) et la rangee des pastilles (les moins utiles
// s'effacent quand la place manque, l'ordre reste). Sans ecran.
#include "../src/app/StatusStrip.hpp"

#include <cstdio>
#include <memory>
#include <string>

using app::MessageJournal;
using Sev = ui::StatusBar::Severity;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ECHEC ligne %d : %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    ui::PlatformServices ps;
    ps.measureWidth = [](std::string_view s, gfx::FontId) { return 7.f * static_cast<float>(s.size()); };
    ps.lineHeight = [](gfx::FontId) { return 16.f; };
    ui::installPlatformServices(ps);

    // 1. Les 50 derniers, les plus recents en tete.
    MessageJournal j;
    for (int i = 0; i < 60; ++i) j.push("message " + std::to_string(i), Sev::Info, true, "", 10.0 * i, "10:00:00");
    CHECK(j.entries().size() == 50);
    CHECK(j.entries().front().text == "message 59");
    CHECK(j.entries().back().text == "message 10");

    // 2. Un durable sans gravite qui en remplace un autre en moins de 1,5 s : il prend sa place.
    MessageJournal k;
    k.push("x = 10", Sev::None, false, "IHM \xC2\xB7 Vue_A", 100.0, "10:00:01");
    k.push("x = 11", Sev::None, false, "IHM \xC2\xB7 Vue_A", 100.2, "10:00:01");
    k.push("x = 12", Sev::None, false, "IHM \xC2\xB7 Vue_A", 100.4, "10:00:02");
    CHECK(k.entries().size() == 1);
    CHECK(k.entries().front().text == "x = 12");
    // ... pas apres 2 s.
    k.push("x = 13", Sev::None, false, "", 103.0, "10:00:05");
    CHECK(k.entries().size() == 2);
    // Un avertissement n'est jamais remplace par un durable.
    k.push("Echec de l'ecriture", Sev::Warning, true, "", 103.1, "10:00:05");
    k.push("x = 14", Sev::None, false, "", 103.2, "10:00:05");
    CHECK(k.entries().size() == 4);
    CHECK(k.entries()[1].severity == Sev::Warning);
    // Le meme, redit : son heure seulement.
    k.push("x = 14", Sev::None, false, "", 110.0, "10:00:12");
    CHECK(k.entries().size() == 4);
    CHECK(k.entries().front().time == "10:00:12");
    // Un vide n'entre pas.
    k.push("", Sev::Error, true, "", 111.0, "10:00:13");
    CHECK(k.entries().size() == 4);
    // Le texte (Copier) : une ligne par message, la gravite entre crochets.
    const auto text = k.toText();
    CHECK(text.find("[attention]  Echec de l'ecriture") != std::string::npos);
    std::size_t lines = 0;
    for (char c : text) lines += c == '\n';
    CHECK(lines == 4);
    const auto rev = k.revision();
    k.clear();
    CHECK(k.entries().empty());
    CHECK(k.revision() > rev);

    // 3. Les nombres, l'heure, les mots.
    CHECK(app::groupedCount(0) == "0");
    CHECK(app::groupedCount(999) == "999");
    CHECK(app::groupedCount(12480) == "12\xC2\xA0" "480");
    CHECK(app::groupedCount(1234567) == "1\xC2\xA0" "234\xC2\xA0" "567");
    CHECK(app::wallClock(1759156800000LL).size() == 8);
    CHECK(app::severityWord(Sev::Error) == "erreur");
    CHECK(app::severityWord(Sev::None).empty());

    // 4. La rangee : les moins utiles s'effacent quand la place manque, l'ordre reste.
    ui::Widget bar("bar");
    auto stripOwned = std::make_unique<app::StatusStrip>("strip");
    auto& strip = static_cast<app::StatusStrip&>(bar.addChild(std::move(stripOwned)));
    auto& a = strip.add(std::make_unique<app::StatusChip>("a"), 90);   // 14 + 7*10 = 84
    auto& b = strip.add(std::make_unique<app::StatusChip>("b"), 10);   // 84
    auto& c = strip.add(std::make_unique<app::StatusChip>("c"), 50);   // 84
    a.setText("aaaaaaaaaa"); b.setText("bbbbbbbbbb"); c.setText("cccccccccc");
    bar.setBounds({0.f, 0.f, 16.f + 260.f + 84.f * 3.f + 9.f * 2.f, 28.f});   // tout tient
    strip.setBounds({100.f, 0.f, strip.sizeHint().preferred.w, 28.f});
    bar.layout();
    CHECK(strip.visibleChips().size() == 3);
    CHECK(strip.sizeHint().preferred.w == 84.f * 3.f + 9.f * 2.f);
    bar.setBounds({0.f, 0.f, 16.f + 260.f + 84.f * 2.f + 9.f, 28.f});             // deux tiennent
    strip.invalidateLayout();
    bar.layout();
    auto shown = strip.visibleChips();
    CHECK(shown.size() == 2);
    CHECK(shown.size() == 2 && shown[0] == &a && shown[1] == &c);                  // b (10) part, l'ordre reste
    c.setShown(false);                                                              // le voeu du proprietaire
    bar.layout();
    shown = strip.visibleChips();
    CHECK(shown.size() == 2 && shown[0] == &a && shown[1] == &b);
    // Les morceaux : un point, une icone, une touche, le chevron elargissent la pastille.
    const float w0 = a.sizeHint().preferred.w;
    a.setDot(true, ui::Tone::Ok);
    CHECK(a.sizeHint().preferred.w == w0 + 8.f + 5.f);
    a.setKey("Ctrl+S");
    CHECK(a.sizeHint().preferred.w == w0 + 13.f + 5.f + 42.f + 8.f);
    // 5. Une pastille elastique (la selection) se raccourcit au lieu de disparaitre.
    a.setDot(false, ui::Tone::None);
    a.setKey({});
    c.setShown(true);
    c.setElastic(true);
    c.setText(std::string(40, 'c'));                                                 // voulue : 14 + 280 = 294 ; au moins 140
    bar.setBounds({0.f, 0.f, 16.f + 260.f + 84.f + 9.f + 200.f, 28.f});              // a (84), puis c (140 + 60), b ne tient plus
    strip.invalidateLayout();
    bar.layout();
    strip.setBounds({100.f, 0.f, strip.sizeHint().preferred.w, 28.f});
    bar.layout();
    shown = strip.visibleChips();
    CHECK(shown.size() == 2 && shown[0] == &a && shown[1] == &c);
    CHECK(c.bounds().w == 200.f);
    CHECK(strip.sizeHint().preferred.w == 84.f + 9.f + 200.f);
    // Le clic.
    int clicks = 0;
    b.setOnClick([&] { ++clicks; });
    b.click();
    CHECK(clicks == 1);

    std::printf(fails == 0 ? "statusstrip_test : tout est bon\n" : "statusstrip_test : %d echec(s)\n", fails);
    return fails == 0 ? 0 : 1;
}
