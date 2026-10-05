// tests/fileexplorer_test.cpp - lot API 8 : l'explorateur de fichiers de l'appli, sans ecran.
//
//   fileexplorer_test
//
//  La famille, le type, la pastille et la teinte d'une extension ; lire un
//  dossier (fabrique dans un dossier temporaire : des dossiers, des fichiers,
//  un fichier cache), le trier (les dossiers d'abord, Vue2 < Vue10, par date,
//  par taille, par type), le filtrer par les motifs d'une demande (et compter
//  ce que le filtre cache), y chercher (ni casse ni accents), taper les
//  premieres lettres ; les erreurs (dossier disparu, pas un dossier) ; le fil
//  d'Ariane (Windows, reseau, Linux) et le parent ; le nom a enregistrer
//  (l'extension ajoutee, les noms interdits, "existe deja") ; la memoire des
//  recents, des epingles et des vues (ecrite puis relue) ; les dates relatives
//  et les tailles ; la taille d'une image PNG ; 5 000 fichiers lus d'un coup.
#include "../src/ui/FileExplorerModel.hpp"
#include "../src/ui/FileKinds.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/FileExplorer.hpp"
#include "../src/app/FilePreview.hpp"   // Lot API 8, 2e partie : l'apercu

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>   // SetFileAttributesW : un fichier cache du systeme
#endif

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace fs = std::filesystem;
namespace f = ui::files;

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void touch(const fs::path& p, std::size_t bytes, int ageSeconds) {
    std::ofstream out(p, std::ios::binary);
    out << std::string(bytes, 'x');
    out.close();
    const auto t = fs::file_time_type::clock::now() - std::chrono::seconds(ageSeconds);
    std::error_code ec;
    fs::last_write_time(p, t, ec);
}

std::vector<std::string> names(const std::vector<f::Entry>& rows) {
    std::vector<std::string> out;
    for (const auto& e : rows) out.push_back(e.name);
    return out;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// ---- le dialogue, dans une vraie pile de menus (sans ecran) ----
class BaseScreen final : public menu::IMenu {
public:
    core::Status Initialize() override { return core::ok(); }
    void Update(const menu::FrameContext&) override {}
    void Render(gfx::IRenderer&, const menu::FrameContext&) override {}
    ui::EventResult HandleEvent(const ui::InputEvent&) override { return ui::EventResult::Ignored; }
    void OnEnter() override {}
    void OnExit() override {}
    [[nodiscard]] menu::MenuId id() const override { return "ecran"; }
    [[nodiscard]] menu::MenuTraits traits() const override { return {}; }
};

// Un renderer qui ne dessine rien : la peinture du dialogue passe (le cadre, la
// liste, les emplacements, les vignettes) et les textes sont gardes.
class FakeRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override { ++clips; }
    void popClip() override { --clips; }
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view s, gfx::FontId, gfx::Color) override { texts.emplace_back(s); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId) const override {
        return {8.f * static_cast<float>(s.size()), 16.f, 12.f, 4.f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId) const override { return 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1280.f, 800.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId, float maxWidth) const override {
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, maxWidth) / 8.f));
    }
    std::vector<std::string> texts;
    int                      clips{0};
};

struct ExplorerRun {
    ui::Theme               theme = ui::Theme::dark();
    FakeRenderer            renderer;
    ui::FileExplorerDialog* dialog{nullptr};
    int                     answers{0};
    std::string             answer;
    menu::MenuManager       mm{menu::MenuFactory{}};   // le dernier : detruit le premier (le dialogue repond encore)

    ExplorerRun(ui::FilePick pick, ui::FileExplorerContext ctx) {
        mm.PushMenu(std::make_unique<BaseScreen>());
        mm.applyPending();
        auto d = std::make_unique<ui::FileExplorerDialog>(std::move(pick), std::move(ctx), [this](std::string p) {
            ++answers;
            answer = std::move(p);
        });
        dialog = d.get();
        mm.ShowDialog(std::move(d), {});
        mm.applyPending();
        frame();
    }
    void frame() {
        const menu::FrameContext fc{0.016, 1.0, &theme, {1280.f, 800.f}, 1.f};
        mm.Update(fc);
        renderer.texts.clear();
        mm.Render(renderer, fc);
    }
    void send(const ui::InputEvent& e) {
        (void)mm.HandleEvent(e);
        mm.applyPending();
    }
    [[nodiscard]] bool open() const { return dynamic_cast<ui::FileExplorerDialog*>(mm.top()) != nullptr; }
    [[nodiscard]] bool drew(const std::string& needle) const {
        for (const auto& t : renderer.texts)
            if (t.find(needle) != std::string::npos) return true;
        return false;
    }
};

bool anyLine(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& l : lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

std::string crumbs(const std::vector<f::Crumb>& c) {
    std::string s;
    for (const auto& x : c) s += (s.empty() ? "" : " | ") + x.label + "=" + x.path;
    return s;
}

} // namespace

int main(int argc, char** argv) {   // Lot API 8, 2e partie : [MAST.XPG] [tests/fixtures/ihm]
    // ---- la famille et le type d'une extension -----------------------------------
    std::printf("familles et types\n");
    check(f::extensionOf("MAST.XPG") == "xpg" && f::extensionOf("a.tar.gz") == "gz" && f::extensionOf(".gitignore").empty()
              && f::extensionOf("LISEZMOI").empty() && f::extensionOf("D:\\x.y\\rapport") .empty(),
          "l'extension : en minuscules, la derniere, rien pour .gitignore ni un nom sans point");
    check(f::familyOf("MAST.XPG", false) == f::Family::ControlExpert && f::familyOf("CONFIG.xhw", false) == f::Family::ControlExpert
              && f::familyOf("a.STU", false) == f::Family::ControlExpert,
          ".XPG .XHW .STU : Control Expert");
    check(f::familyOf("a.xlsm", false) == f::Family::Workbook && f::familyOf("a.csv", false) == f::Family::Workbook
              && f::familyOf("a.xls", false) == f::Family::Workbook,
          "classeurs et CSV");
    check(f::familyOf("logo.PNG", false) == f::Family::Image && f::familyOf("d.pdf", false) == f::Family::Pdf
              && f::familyOf("j.log", false) == f::Family::Text && f::familyOf("z.zip", false) == f::Family::Archive
              && f::familyOf("t.xpgtheme", false) == f::Family::AppFile && f::familyOf("exports", true) == f::Family::Folder
              && f::familyOf("x.bin", false) == f::Family::Other,
          "images, PDF, texte, archives, fichiers de l'appli, dossiers, autres");
    check(f::tintOf(f::Family::ControlExpert) == f::Tint::Blue && f::tintOf(f::Family::Workbook) == f::Tint::Green
              && f::tintOf(f::Family::Image) == f::Tint::Violet && f::tintOf(f::Family::Pdf) == f::Tint::Red
              && f::tintOf(f::Family::Text) == f::Tint::Gray && f::tintOf(f::Family::Archive) == f::Tint::Brown
              && f::tintOf(f::Family::AppFile) == f::Tint::Orange && f::tintOf(f::Family::Folder) == f::Tint::Yellow,
          "les teintes de la conception (bleu, vert, violet, rouge, gris, brun, orange, jaune)");
    check(f::typeLabel("MAST.XPG", false) == "Export Control Expert" && f::typeLabel("CONFIG.XHW", false) == "Configuration mat\xC3\xA9rielle"
              && f::typeLabel("a.xlsm", false) == "Classeur Excel avec macros" && f::typeLabel("a.png", false) == "Image PNG"
              && f::typeLabel("a.pdf", false) == "Document PDF" && f::typeLabel("x", true) == "Dossier"
              && f::typeLabel("x.abc", false) == "Fichier ABC" && f::typeLabel("LISEZMOI", false) == "Fichier",
          "les libelles des types");
    check(f::badgeText("MAST.XPG", false) == "XPG" && f::badgeText("t.xpgtheme", false) == "XPGT" && f::badgeText("d", true).empty(),
          "la pastille : l'extension en capitales, 4 lettres au plus");
    check(f::countLabel(f::Family::Workbook, "xlsx", 3) == "3 classeurs" && f::countLabel(f::Family::Workbook, "csv", 8) == "8 CSV"
              && f::countLabel(f::Family::Folder, "", 1) == "1 dossier",
          "compter : 3 classeurs, 8 CSV, 1 dossier");
    {
        const auto dark = ui::Theme::byName("Dark");
        const auto light = ui::Theme::byName("Light");
        const auto blue = f::tintColor(dark, f::Tint::Blue);
        const auto red = f::tintColor(light, f::Tint::Red);
        check(blue.b > blue.r && red.r == light.color.error.r && red.g == light.color.error.g,
              "les teintes viennent du theme (bleu des entrees, rouge des erreurs)");
    }

    // ---- lire un dossier -----------------------------------------------------------
    std::printf("lire, trier, filtrer, chercher\n");
    const fs::path root = fs::temp_directory_path() / ("xpg_fileexplorer_" + std::to_string(std::time(nullptr)));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "exports", ec);
    fs::create_directories(root / "Armoire_Gaz", ec);
    fs::create_directories(root / "simulation", ec);
    touch(root / "MAST.XPG", 4000, 3600 * 30);
    touch(root / "CONFIG.XHW", 2000, 3600 * 20);
    touch(root / "Vue10.csv", 100, 50);
    touch(root / "Vue2.csv", 300, 500);
    touch(root / "rapport.csv", 200, 7200);
    touch(root / "affaire.xlsm", 9000, 60 * 60 * 24 * 40);
    touch(root / "\xC3\xA9tat.txt", 10, 10);
    touch(root / ".cache", 10, 10);
#if defined(_WIN32)
    // Sous Windows, "cache" est un attribut (un nom en point y est un fichier comme un autre).
    SetFileAttributesW((root / ".cache").wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
    const std::string dir = u8(root);
    const auto listing = f::listFolder(dir);
    check(listing.error == f::ListError::None && listing.entries.size() == 10 && listing.systemHidden == 1,
          "10 elements lus, le fichier cache du systeme reste cache (" + std::to_string(listing.entries.size()) + ")");
    {
        auto rows = listing.entries;
        f::sortEntries(rows, f::SortKey::Name, true);
        check(joined(names(rows)) == "Armoire_Gaz, exports, simulation, affaire.xlsm, CONFIG.XHW, \xC3\xA9tat.txt, MAST.XPG, rapport.csv, Vue2.csv, Vue10.csv",
              "par nom : les dossiers d'abord, sans casse ni accents, Vue2 avant Vue10 : " + joined(names(rows)));
        f::sortEntries(rows, f::SortKey::Name, false);
        check(rows.front().folder && rows[3].name == "Vue10.csv", "par nom, a l'envers : les dossiers restent devant");
        f::sortEntries(rows, f::SortKey::Modified, false);
        check(rows[3].name == "\xC3\xA9tat.txt" && rows.back().name == "affaire.xlsm", "par date, le plus recent d'abord");
        f::sortEntries(rows, f::SortKey::Size, true);
        check(rows[3].name == "\xC3\xA9tat.txt" && rows.back().name == "affaire.xlsm", "par taille");
        f::sortEntries(rows, f::SortKey::Type, true);
        check(rows[3].name == "affaire.xlsm" && rows[4].name == "CONFIG.XHW", "par type (Classeur, Configuration, Document, Export, Fichier CSV)");
    }
    const auto groups = f::filterGroups({{"Classeurs", "xlsx;xlsm;xls"}, {"Fichiers CSV", "*.csv"}, {"Tous les fichiers", "*"}});
    check(groups.size() == 3 && groups[0].label() == "Classeurs (*.xlsx; *.xlsm; *.xls)" && groups[2].all()
              && groups[2].label() == "Tous les fichiers (*.*)",
          "les filtres de la demande, \"Tous les fichiers\" une fois a la fin : " + groups[0].label());
    check(f::filterGroups({}).size() == 1 && f::filterGroups({{"", "xpg; *.XHW"}})[0].label() == "Fichiers XPG, XHW (*.xpg; *.xhw)",
          "sans filtre : tous les fichiers ; un filtre sans nom le recoit");
    check(f::findFilter(groups, "classeur") == 0 && f::findFilter(groups, "CSV") == 1 && f::findFilter(groups, "tous") == 2
              && f::findFilter(groups, "images") == -1,
          "un filtre par le debut de son nom (explorateur-filtre)");
    {
        const auto shown = f::shownEntries(listing.entries, &groups[0], {}, f::SortKey::Name, true);
        check(shown.rows.size() == 4 && shown.hiddenByFilter == 6 && shown.rows[3].name == "affaire.xlsm",
              "filtre Classeurs : 3 dossiers et le classeur, 6 caches par le filtre");
        const auto all = f::shownEntries(listing.entries, &groups[0], {}, f::SortKey::Name, true, true);
        check(all.rows.size() == 10 && all.hiddenByFilter == 6, "Tout afficher : les 10, le compte reste");
        const auto search = f::shownEntries(listing.entries, &groups[1], "vue", f::SortKey::Name, true);
        check(joined(names(search.rows)) == "Vue2.csv, Vue10.csv", "chercher \"vue\" dans les CSV");
        const auto accents = f::shownEntries(listing.entries, nullptr, "ETAT", f::SortKey::Name, true);
        check(accents.rows.size() == 1 && accents.rows[0].name == "\xC3\xA9tat.txt", "chercher sans casse ni accents (ETAT -> \xC3\xA9tat.txt)");
        auto rows = listing.entries;
        f::sortEntries(rows, f::SortKey::Name, true);
        check(f::typeAhead(rows, "ra", 0) == 7 && f::typeAhead(rows, "v", 9) == 9 && f::typeAhead(rows, "v", 10) == 8
                  && f::typeAhead(rows, "zz", 0) == -1,
              "taper les premieres lettres (et reprendre au debut)");
    }
    {
        const auto gone = f::listFolder(u8(root / "disparu"));
        check(gone.error == f::ListError::NotFound && gone.message == "Ce dossier n'existe plus", "un dossier disparu : " + gone.message);
        const auto file = f::listFolder(u8(root / "MAST.XPG"));
        check(file.error == f::ListError::NotAFolder, "un fichier n'est pas un dossier");
        check(f::errorMessage(f::ListError::DriveGone, "E:\\Affaires") == "La cl\xC3\xA9 E: n'est plus l\xC3\xA0"
                  && f::errorMessage(f::ListError::AccessDenied, "C:\\x") == "Acc\xC3\xA8s refus\xC3\xA9 \xC3\xA0 ce dossier",
              "les erreurs dites dans la liste");
        const auto completions = f::completeFolder(dir + "/ex");
        check(completions.size() == 1 && completions[0] == dir + "/exports", "le chemin tape se complete (ex -> exports)");
    }

    // ---- le fil d'Ariane -------------------------------------------------------------
    std::printf("fil d'Ariane\n");
    check(crumbs(f::breadcrumb("D:\\Affaires\\Armoire_Gaz\\exports"))
              == "Ce PC= | D:=D:\\ | Affaires=D:\\Affaires | Armoire_Gaz=D:\\Affaires\\Armoire_Gaz | exports=D:\\Affaires\\Armoire_Gaz\\exports",
          "Windows : Ce PC > D: > Affaires > Armoire_Gaz > exports");
    check(crumbs(f::breadcrumb("d:/Affaires/")) == "Ce PC= | D:=D:/ | Affaires=D:/Affaires", "Windows, avec des / et un / final (la lettre en capitale)");
    check(crumbs(f::breadcrumb("\\\\srv-usine\\automatisme\\Gaz"))
              == "Ce PC= | \\\\srv-usine\\automatisme=\\\\srv-usine\\automatisme | Gaz=\\\\srv-usine\\automatisme\\Gaz",
          "un partage reseau : \\\\serveur\\partage d'un morceau");
    check(crumbs(f::breadcrumb("/home/q/Affaires")) == "Ce PC= | /=/ | home=/home | q=/home/q | Affaires=/home/q/Affaires", "Linux");
    check(f::parentOf("D:\\Affaires\\Gaz") == "D:\\Affaires" && f::parentOf("D:\\Affaires") == "D:\\" && f::parentOf("D:\\").empty()
              && f::parentOf("/home") == "/" && f::parentOf("/").empty(),
          "le parent (et Ce PC au-dessus d'une racine)");
    check(f::joinPath("D:\\Affaires", "a.csv") == "D:\\Affaires\\a.csv" && f::joinPath("D:\\", "a") == "D:\\a"
              && f::joinPath("/home/q", "a") == "/home/q/a",
          "joindre avec le separateur du dossier");
#if defined(_WIN32)
    const bool caseRule = f::samePath("/home/Q", "/home/q");    // Windows : jamais la casse
#else
    const bool caseRule = !f::samePath("/home/Q", "/home/q");   // ailleurs : la casse compte (hors D:\...)
#endif
    check(f::samePath("D:\\Affaires\\", "d:/affaires") && caseRule, "le meme chemin (sans casse pour Windows, avec pour Linux)");

    // ---- le nom a enregistrer ----------------------------------------------------------
    std::printf("le nom a enregistrer\n");
    check(f::nameError("rapport.csv").empty() && !f::nameError("rap:port").empty() && !f::nameError("a?b").empty()
              && !f::nameError("CON").empty() && !f::nameError("nul.txt").empty() && !f::nameError("com3.csv").empty()
              && f::nameError("console.csv").empty() && !f::nameError("rapport.").empty() && !f::nameError("   ").empty(),
          "les noms interdits (caracteres, noms reserves de Windows, point final, vide)");
    check(f::nameError("CON.txt") == "Nom r\xC3\xA9serv\xC3\xA9 de Windows : CON", "le message : " + f::nameError("CON.txt"));
    check(f::withExtension("rapport", &groups[1]) == "rapport.csv" && f::withExtension("rapport.csv", &groups[1]) == "rapport.csv"
              && f::withExtension("rapport", &groups[2], "txt") == "rapport.txt" && f::withExtension("rapport", nullptr) == "rapport",
          "l'extension du filtre ajoutee a un nom qui n'en a pas");
    {
        const auto now = f::nowSeconds();
        const auto fresh = f::checkSaveName(dir, "nouveau", &groups[1], now);
        check(fresh.error.empty() && !fresh.exists && fresh.name == "nouveau.csv" && fresh.path == f::joinPath(dir, "nouveau.csv"),
              "un nom libre : Enregistrer");
        const auto taken = f::checkSaveName(dir, "rapport", &groups[1], now);
        check(taken.exists && taken.error.empty() && taken.warning.rfind("rapport.csv existe d\xC3\xA9j\xC3\xA0 (modifi\xC3\xA9 ", 0) == 0
                  && taken.warning.find(") : il sera remplac\xC3\xA9") != std::string::npos,
              "un fichier qui existe : l'avertissement (" + taken.warning + ")");
        const auto folder = f::checkSaveName(dir, "exports", nullptr, now);
        check(!folder.error.empty() && !folder.exists, "un dossier du meme nom : refuse (" + folder.error + ")");
        const auto bad = f::checkSaveName(dir, "a|b", &groups[1], now);
        check(!bad.error.empty(), "un nom impossible : l'erreur, le bouton grise");
        const auto typedPath = f::checkSaveName(dir, u8(root / "exports" / "x"), &groups[1], now);
        check(typedPath.error.empty() && typedPath.path == f::joinPath(u8(root / "exports"), "x.csv"), "un chemin tape en entier : " + typedPath.path);
    }

    // ---- les libelles --------------------------------------------------------------------
    std::printf("dates et tailles\n");
    {
        std::tm base{};
        base.tm_year = 2026 - 1900;
        base.tm_mon = 8;
        base.tm_mday = 29;
        base.tm_hour = 14;
        base.tm_min = 30;
        base.tm_isdst = -1;
        const auto now = static_cast<std::int64_t>(std::mktime(&base));
        std::tm y = base;
        y.tm_mday = 28;
        y.tm_hour = 17;
        y.tm_min = 42;
        y.tm_isdst = -1;
        const auto yesterday = static_cast<std::int64_t>(std::mktime(&y));
        std::tm old = base;
        old.tm_mday = 12;
        old.tm_isdst = -1;
        const auto older = static_cast<std::int64_t>(std::mktime(&old));
        check(f::relativeTime(now - 20, now) == "\xC3\xA0 l'instant" && f::relativeTime(now - 180, now) == "il y a 3 min"
                  && f::relativeTime(now - 3 * 3600, now) == "aujourd'hui 11:30" && f::relativeTime(yesterday, now) == "hier 17:42"
                  && f::relativeTime(older, now) == "12/09/2026" && f::relativeTime(0, now).empty(),
              "il y a 3 min, aujourd'hui 11:30, hier 17:42, 12/09/2026");
    }
    check(f::sizeLabel(0) == "0 octet" && f::sizeLabel(532) == "532 octets" && f::sizeLabel(12 * 1024) == "12 Ko"
              && f::sizeLabel(1025) == "2 Ko" && f::sizeLabel(3565158) == "3,4 Mo" && f::sizeLabel(340ull * 1024 * 1024) == "340 Mo",
          "les tailles : " + f::sizeLabel(3565158));
    {
        // Un PNG de 1600 x 900 : la signature et IHDR suffisent.
        const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                     0, 0, 0x06, 0x40, 0, 0, 0x03, 0x84, 8, 6, 0, 0, 0};
        {
            std::ofstream out(root / "capture.png", std::ios::binary);
            out.write(reinterpret_cast<const char*>(png), sizeof png);
        }
        const auto size = f::imageSize(u8(root / "capture.png"));
        check(size && size->w == 1600 && size->h == 900
                  && f::imageTypeLabel("capture.png", size) == "Image PNG 1600 \xC3\x97 900",
              "l'en-tete d'une image : Image PNG 1600 x 900");
        check(!f::imageSize(u8(root / "MAST.XPG")), "pas une image : rien");
    }

    // ---- la memoire ---------------------------------------------------------------------
    std::printf("memoire\n");
    {
        f::Memory m;
        const auto kind = f::requestKind(f::PickMode::Save, groups);
        check(kind == "enregistrer:xlsx" && f::requestKind(f::PickMode::Folder, groups) == "dossier"
                  && f::requestKind(f::PickMode::Open, f::filterGroups({})) == "ouvrir:*",
              "le genre d'une demande : " + kind);
        for (int i = 0; i < 7; ++i) m.remember(kind, "D:\\Affaires\\A" + std::to_string(i) + "\\x.csv", false);
        m.remember(kind, "d:\\affaires\\a3\\y.csv", false);
        const auto rec = m.recents(kind);
        check(rec.size() == 5 && rec[0].folder == "D:\\affaires\\a3" && rec[0].file == "d:\\affaires\\a3\\y.csv" && rec[1].folder == "D:\\Affaires\\A6",
              "5 recents au plus, le dernier en tete, sans doublon");
        check(m.lastFolder(kind) == "D:\\affaires\\a3" && m.lastFolder("ouvrir:xpg").empty(), "le dernier dossier, par genre");
        check(m.isRecentFile("D:\\Affaires\\A3\\Y.csv") && !m.isRecentFile("D:\\Affaires\\A3\\z.csv"), "la marque \"recent\" d'un fichier");
        m.rememberFolder("dossier", "\\\\srv-usine\\automatisme");
        check(m.recents("dossier").size() == 1 && m.recents("dossier")[0].folder == "\\\\srv-usine\\automatisme",
              "un chemin reseau tape revient dans les recents");
        check(m.pin("D:\\Affaires") && !m.pin("d:/affaires/") && m.pinned("D:\\AFFAIRES") && m.pins().size() == 1, "epingler (une fois)");
        check(m.pin("/home/q/Affaires") && m.unpin("D:\\Affaires") && !m.unpin("D:\\Affaires") && m.pins().size() == 1, "desepingler");
        f::Memory::ViewState v;
        v.thumbnails = true;
        v.sort = f::SortKey::Modified;
        v.ascending = false;
        v.widths = {300.f, 140.f, 190.f, 90.f};
        m.setView(kind, v);
        f::Memory again;
        again.load(m.save());
        const auto v2 = again.view(kind);
        check(again.recents(kind).size() == 5 && again.recents(kind)[0].file == "d:\\affaires\\a3\\y.csv" && again.pins() == m.pins()
                  && v2.thumbnails && v2.sort == f::SortKey::Modified && !v2.ascending && v2.widths.size() == 4 && v2.widths[0] == 300.f
                  && again.lastFolder("dossier") == "\\\\srv-usine\\automatisme",
              "ecrite puis relue a l'identique");
        bool noPipe = true;
        for (const auto& line : m.save()) noPipe = noPipe && line.find('|') == std::string::npos;
        check(noPipe, "aucune ligne ne porte | (le separateur de Settings::setList)");
        check(!again.view("ouvrir:xpg").thumbnails && again.view("ouvrir:xpg").sort == f::SortKey::Name, "une vue jamais vue : Details, par nom");
    }

    // ---- les emplacements ---------------------------------------------------------------
    std::printf("emplacements\n");
    {
        const auto project = f::projectPlaces(dir, u8(root / "Armoire_Gaz" / "MAST.XPG"));
        std::vector<std::string> labels;
        for (const auto& p : project) labels.push_back(p.label);
        check(joined(labels) == u8(root.filename()) + ", exports, simulation, Armoire_Gaz",
              "CE PROJET : son dossier, exports, simulation (ressources n'existe pas), le dossier du .XPG : " + joined(labels));
        const auto pc = f::computerPlaces();
        bool drive = false;
        for (const auto& p : pc) drive = drive || p.family == f::Family::Drive;
        check(!pc.empty() && drive, "CE PC : des dossiers et au moins un lecteur (" + std::to_string(pc.size()) + ")");
    }

    // ---- 5 000 fichiers -------------------------------------------------------------------
    std::printf("vitesse\n");
    {
        const fs::path big = root / "gros";
        fs::create_directories(big, ec);
        for (int i = 0; i < 5000; ++i) std::ofstream(big / ("mesure_" + std::to_string(i) + (i % 3 ? ".csv" : ".xlsx")));
        const auto t0 = std::chrono::steady_clock::now();
        const auto l = f::listFolder(u8(big));
        const auto shown = f::shownEntries(l.entries, &groups[1], {}, f::SortKey::Name, true);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  (5 000 fichiers lus, filtres et tries en %lld ms)\n", static_cast<long long>(ms));
        check(l.entries.size() == 5000 && shown.rows.size() == 3333 && shown.hiddenByFilter == 1667 && shown.rows[1].name == "mesure_2.csv",
              "5 000 fichiers : 3 333 CSV montres, 1 667 caches par le filtre, dans l'ordre naturel");
        check(ms < 1000, "en moins d'une seconde (la cible : 100 ms sur un poste de travail)");
    }

    // ---- le dialogue ----------------------------------------------------------------------
    std::printf("le dialogue\n");
    fs::remove_all(root / "gros", ec);          // le dossier de 5 000 fichiers et l'image : pas dans la liste
    fs::remove(root / "capture.png", ec);
    {
        auto memory = std::make_shared<f::Memory>();
        int saved = 0;
        const auto context = [&] {
            ui::FileExplorerContext ctx;
            ctx.projectDir = dir;
            ctx.sourceXpg = u8(root / "Armoire_Gaz" / "MAST.XPG");
            ctx.memory = memory;
            ctx.memoryChanged = [&saved] { ++saved; };
            return ctx;
        };
        // Ouvrir : les classeurs.
        {
            ui::FilePick pick;
            pick.title = "Importer les traductions";
            pick.filters = {{"Classeurs", "xlsx;xlsm"}, {"Tous les fichiers", "*"}};
            pick.start = dir;
            ExplorerRun run(pick, context());
            auto lines = run.dialog->lines();
            check(run.open() && run.dialog->folder() == dir && run.dialog->title() == "Importer les traductions",
                  "ouvert sur le dossier de la demande, avec son titre");
            check(run.drew("Importer les traductions") && run.drew("CE PROJET") && run.drew("CE PC") && run.drew("affaire.xlsm")
                      && run.drew("Armoire_Gaz") && run.drew("Ouvrir") && run.renderer.clips == 0,
                  "dessine : le titre, les emplacements, la liste, le bouton (" + std::to_string(run.renderer.texts.size()) + " textes)");
            check(lines.size() == 5 && lines[0] == "[D] Armoire_Gaz" && lines[3] == "[F] affaire.xlsm | Classeur Excel avec macros"
                      && anyLine(lines, "6 autres fichiers cach\xC3\xA9s par le filtre"),
                  "la liste : les dossiers, le classeur, \"6 autres fichiers caches par le filtre\" (" + std::to_string(lines.size()) + " lignes)");
            check(run.dialog->statusText().rfind("4 \xC3\xA9l\xC3\xA9ments \xC2\xB7 filtre : Classeurs", 0) == 0,
                  "la ligne d'etat : " + run.dialog->statusText());
            std::string why;
            check(run.dialog->setFilter("tous", &why) && run.dialog->lines().size() == 10, "filtre Tous les fichiers : les 10");
            check(run.dialog->select("mast", &why) && run.dialog->primaryLabel() == "Ouvrir", "choisir MAST.XPG par le debut de son nom");
            check(!run.dialog->select("v", &why) && !why.empty(), "un debut qui ne suffit pas (Vue2, Vue10) : refuse");
            const bool accepted = run.dialog->accept(&why);
            run.mm.applyPending();
            check(accepted && run.answers == 1 && run.answer == f::joinPath(dir, "MAST.XPG") && !run.open(),
                  "Ouvrir : la reponse (" + run.answer + "), le dialogue ferme");
            check(memory->lastFolder("ouvrir:xlsx") == dir && memory->isRecentFile(f::joinPath(dir, "MAST.XPG")) && saved == 1,
                  "retenu : le dossier et le fichier, la memoire ecrite");
        }
        // Enregistrer : "existe deja", puis un nom impossible, puis un nom libre.
        {
            ui::FilePick pick;
            pick.save = true;
            pick.filters = {{"Fichiers CSV", "csv"}};
            pick.start = f::joinPath(dir, "rapport.csv");
            ExplorerRun run(pick, context());
            check(run.dialog->folder() == dir && run.dialog->primaryLabel() == "Remplacer"
                      && run.dialog->message().rfind("rapport.csv existe d\xC3\xA9j\xC3\xA0", 0) == 0,
                  "le nom propose existe : Remplacer, et l'avertissement (" + run.dialog->message() + ")");
            run.dialog->setFileName("a|b");
            std::string why;
            check(!run.dialog->accept(&why) && run.answers == 0 && run.open() && !run.dialog->message().empty(), "un nom impossible : refuse (" + why + ")");
            run.dialog->setFileName("bilan");
            check(run.dialog->primaryLabel() == "Enregistrer" && run.dialog->message().empty(), "un nom libre : Enregistrer");
            const bool accepted = run.dialog->accept(&why);
            check(accepted && run.answer == f::joinPath(dir, "bilan.csv"), "l'extension ajoutee : " + run.answer);
        }
        // Un dossier ; le clavier.
        {
            ui::FilePick pick;
            pick.folder = true;
            pick.start = dir;
            ExplorerRun run(pick, context());
            check(run.dialog->primaryLabel() == "Choisir \xC2\xAB " + u8(root.filename()) + " \xC2\xBB", "un dossier : " + run.dialog->primaryLabel());
            check(run.dialog->lines().size() == 10, "les fichiers restent visibles (grises)");
            run.dialog->setThumbnails(true);
            run.frame();
            check(run.dialog->thumbnails() && run.dialog->lines().size() == 10 && run.drew("XPG") && run.drew("MAST.XPG"),
                  "la vue Vignettes : les cartes, la pastille de l'extension");
            run.dialog->setThumbnails(false);
            check(run.dialog->navigate(u8(root / "exports") + "/") && run.dialog->folder() == u8(root / "exports"),
                  "ouvrir exports (le separateur final retire)");
            run.send(ui::KeyDown{ui::Key::Backspace, {}, false});
            check(run.dialog->folder() == dir, "Retour arriere : le dossier parent");
            check(run.dialog->back() && run.dialog->folder() == u8(root / "exports") && run.dialog->forward() && run.dialog->folder() == dir,
                  "Precedent, Suivant");
            std::string why;
            check(!run.dialog->navigate(u8(root / "disparu"), &why) && anyLine(run.dialog->lines(), "Ce dossier n'existe plus"),
                  "un dossier disparu : dit dans la liste");
            (void)run.dialog->navigate(dir);
            check(run.dialog->select("exports") && run.dialog->primaryLabel() == "Choisir \xC2\xAB exports \xC2\xBB", "Choisir \"exports\"");
            run.send(ui::KeyDown{ui::Key::Escape, {}, false});
            check(run.answers == 1 && run.answer.empty() && !run.open(), "Echap : annule, la demande recoit \"\"");
        }
        // Ferme sans reponse : l'appelant l'apprend comme une annulation.
        {
            ui::FilePick pick;
            pick.start = dir;
            ExplorerRun run(pick, context());
            run.mm.PopToRoot();
            run.mm.applyPending();
            check(run.answers == 1 && run.answer.empty(), "ferme sans reponse : done(\"\")");
        }
    }

    // ---- Lot API 8 : l'explorateur, 2e partie : l'apercu (app/FilePreview) ----------
    {
        namespace pv = app::preview;
        const fs::path adir = root / "apercu";
        fs::create_directories(adir / "sous", ec);
        // Un petit .XPG (l'en-tete, 2 sections, 1 DFB, 3 variables globales).
        const fs::path xpg = adir / "petit.XPG";
        {
            std::ofstream o(xpg, std::ios::binary);
            o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<PGMExchangeFile>\n"
                 "<fileHeader company=\"Schneider Automation\" product=\"Control Expert V15.3 - 230214C\" "
                 "dateTime=\"date_and_time#2026-9-3-14:18:52\" DTDVersion=\"41\"></fileHeader>\n"
                 "<contentHeader name=\"Armoire_Gaz\" version=\"0.0.12\" dateTime=\"date_and_time#2026-8-3-12:57:25\"></contentHeader>\n"
                 "<logicConf><resource resName=\"Micro Basic\" resIdent=\"BMX P34 2020 03.30\"><taskDesc task=\"MAST\">\n"
                 "<sectionDesc name=\"Init\" SectionOrder=\"1\"></sectionDesc><sectionDesc name=\"Marche\" SectionOrder=\"2\"></sectionDesc>\n"
                 "</taskDesc></resource></logicConf>\n"
                 "<FBSource nameOfFBType=\"Vanne\"><inputParameters><variables name=\"Ouvrir\" typeName=\"BOOL\"></variables>"
                 "</inputParameters></FBSource>\n"
                 "<dataBlock><variables name=\"A\" typeName=\"BOOL\"></variables><variables name=\"B\" typeName=\"INT\"></variables>"
                 "<variables name=\"V1\" typeName=\"Vanne\"></variables></dataBlock>\n</PGMExchangeFile>\n";
        }
        const auto fact = [](const ui::FilePreviewInfo& p, const std::string& key) {
            for (const auto& [k, v] : p.facts)
                if (k == key) return v;
            return std::string("<absent>");
        };
        pv::XpgSummary s;
        check(pv::readXpg(u8(xpg), s) && s.project == "Armoire_Gaz" && s.version == "0.0.12" && s.plc == "BMX P34 2020 03.30"
                  && s.counted && s.sections == 2 && s.dfbs == 1 && s.variables == 3,
              "apercu .XPG : l'en-tete et les nombres (2 sections, 1 DFB, 3 variables du dataBlock)");
        check(pv::dateLabel(s.exportedAt) == "03/09/2026 14:18" && pv::dateLabel("n'importe quoi").empty()
                  && pv::dateKey("date_and_time#2026-10-1-8:00:00") > pv::dateKey("date_and_time#2026-9-30-23:59:59"),
              "la date d'un export : 03/09/2026 14:18, et l'ordre des dates");
        pv::ProjectFacts facts;
        facts.sourceXpg = u8(xpg);
        facts.exportedAt = "date_and_time#2026-8-1-10:00:00";
        {
            const auto p = pv::describe(u8(xpg), facts);
            check(p.kind == "Export Control Expert" && fact(p, "Projet") == "Armoire_Gaz (v0.0.12)" && fact(p, "Automate") == "BMX P34 2020 03.30"
                      && fact(p, "Export\xC3\xA9 le") == "03/09/2026 14:18" && fact(p, "Sections") == "2" && fact(p, "Variables") == "3"
                      && fact(p, "DFB") == "1" && fact(p, "Taille") != "<absent>",
                  "apercu .XPG : projet, automate, date, nombres, taille");
            check(anyLine(p.notes, "d\xC3\xA9j\xC3\xA0 import\xC3\xA9 dans ce projet") && anyLine(p.notes, "plus r\xC3\xA9" "cent que le projet ouvert"),
                  "le .XPG d'origine : deja importe ; exporte apres le projet : plus recent");
        }
        facts.sourceXpg = u8(adir / "autre.XPG");
        facts.exportedAt = "date_and_time#2026-10-1-10:00:00";
        check(pv::describe(u8(xpg), facts).notes.empty(), "un autre .XPG, plus ancien : aucune note");
        // Un CSV de 10 lignes (une tabulation, un octet Latin-1 d'Excel) ; un texte binaire.
        {
            std::ofstream o(adir / "liste.csv", std::ios::binary);
            o << "\xEF\xBB\xBFNom;Type\r\nVanne_1\tBOOL\r\nD\xE9" "bit;REAL\r\n";
            for (int i = 4; i <= 10; ++i) o << "L" << i << ";INT\r\n";
        }
        {
            std::ofstream o(adir / "binaire.txt", std::ios::binary);
            o << std::string("abc\0def", 7);
        }
        {
            const auto p = pv::describe(u8(adir / "liste.csv"), facts);
            check(p.text.size() == 8 && p.text[0] == "Nom;Type" && p.text[1] == "Vanne_1    BOOL" && p.text[2] == "D\xC3\xA9" "bit;REAL"
                      && p.text[7] == "L8;INT" && p.openable,
                  "apercu CSV : 8 lignes, sans BOM ni CR, tabulation en espaces, Latin-1 en UTF-8");
            const auto b = pv::describe(u8(adir / "binaire.txt"), facts);
            check(b.text.empty() && anyLine(b.notes, "binaire"), "un texte binaire : pas d'apercu du texte");
        }
        // Un dossier : ses genres, les plus nombreux d'abord.
        {
            std::ofstream(adir / "sous" / "a.csv") << "x";
            std::ofstream(adir / "sous" / "b.csv") << "y";
            std::ofstream(adir / "sous" / "note.txt") << "z";
            fs::create_directories(adir / "sous" / "vide", ec);
            const auto p = pv::describe(u8(adir / "sous"), facts);
            check(p.folder && fact(p, "Contenu") == "4 \xC3\xA9l\xC3\xA9ments : 2 CSV, 1 dossier, 1 texte",
                  "apercu d'un dossier : 4 elements : 2 CSV, 1 dossier, 1 texte (" + fact(p, "Contenu") + ")");
            check(fact(pv::describe(u8(adir / "sous" / "vide"), facts), "Contenu") == "vide", "un dossier vide");
        }
        check(!pv::describe(u8(adir / "disparu.pdf"), facts).error.empty(), "un fichier disparu : une erreur, pas d'exception");
        {
            std::ofstream(adir / "notice.pdf") << "%PDF-1.4";
            const auto p = pv::describe(u8(adir / "notice.pdf"), facts);
            check(p.kind == "Document PDF" && p.openable && fact(p, "Taille") != "<absent>" && fact(p, "Modifi\xC3\xA9") != "<absent>",
                  "apercu PDF : taille, date, Ouvrir avec le programme du systeme");
        }
        // Les echantillons de l'IHM : une image, un classeur.
        std::string ihm;
        for (const std::string& c : {std::string(argc > 2 ? argv[2] : ""), std::string("tests/fixtures/ihm"), std::string("../tests/fixtures/ihm"),
                                    std::string("/home/claude/depot/tests/fixtures/ihm")})
            if (!c.empty() && fs::exists(fs::path(c) / "logo_site.png", ec)) {
                ihm = c;
                break;
            }
        if (ihm.empty()) {
            std::printf("  (tests/fixtures/ihm absent : apercu d'image et de classeur non verifies)\n");
        } else {
            const auto img = pv::thumbnail(u8(fs::path(ihm) / "photo_armoire.jpg"), 64);
            check(img && img->width > 0 && img->width <= 64 && img->height <= 64 && (img->width == 64 || img->height == 64)
                      && img->rgba.size() == static_cast<std::size_t>(img->width) * static_cast<std::size_t>(img->height) * 4u
                      && img->fileWidth > 64u,
                  "vignette d'un JPEG : ramenee a 64 px, ses dimensions d'origine gardees");
            const auto p = pv::describe(u8(fs::path(ihm) / "logo_site.png"), facts);
            check(p.image && fact(p, "Dimensions").find(" \xC3\x97 ") != std::string::npos && p.kind.rfind("Image PNG", 0) == 0,
                  "apercu d'une image : la vignette et ses dimensions (" + fact(p, "Dimensions") + ")");
            check(!pv::thumbnail(u8(fs::path(ihm) / "consignes.csv"), 64), "pas de vignette pour un CSV");
            const auto wb = pv::describe(u8(fs::path(ihm) / "parametres.xlsx"), facts);
            check(wb.error.empty() && !wb.sheets.empty() && !wb.cells.empty() && wb.cells.size() <= 4 && wb.cells.front().size() <= 4,
                  "apercu d'un classeur : les onglets et le debut du premier (4 x 4)" + (wb.error.empty() ? std::string{} : " - " + wb.error));
        }
        // Le vrai MAST.XPG (en argument) : 18 sections, 8 DFB.
        if (argc > 1 && fs::exists(argv[1], ec)) {
            pv::XpgSummary m;
            const bool read = pv::readXpg(argv[1], m);
            check(read && m.project == "Projet" && m.plc == "BMX P34 2020 03.30" && m.sections == 18 && m.dfbs == 8 && m.variables == 251,
                  "MAST.XPG : Projet, BMX P34 2020 03.30, 18 sections, 8 DFB, 251 variables (" + std::to_string(m.variables) + ")");
        }
        // Dans le dialogue : l'apercu de l'element choisi (explorateur-apercu).
        {
            ui::FilePick pick;
            pick.start = u8(adir);
            ui::FileExplorerContext ctx;
            ctx.memory = std::make_shared<f::Memory>();
            ctx.preview = [facts](const std::string& path) { return pv::describe(path, facts); };
            ExplorerRun run(pick, std::move(ctx));
            check(run.dialog->select("liste.csv") && anyLine(run.dialog->previewLines(), "> Nom;Type")
                      && anyLine(run.dialog->previewLines(), "liste.csv | "),
                  "le dialogue : l'apercu du CSV choisi");
            check(run.dialog->select("petit.XPG") && anyLine(run.dialog->previewLines(), "Automate : BMX P34 2020 03.30"),
                  "le dialogue : l'apercu du .XPG choisi");
            run.dialog->setPreviewShown(false);
            check(anyLine(run.dialog->previewLines(), "repli"), "l'apercu replie se dit");
            run.dialog->setPreviewShown(true);
            run.frame();
            // (le renderer factice mesure large : les libelles sont coupes, les valeurs commencent)
            check(run.drew("APER\xC3\x87U") && run.drew("BMX P34") && run.drew("petit.XPG"), "l'apercu est peint");
            // Le menu du clic droit (explorateur-menu) : un dossier, un fichier.
            check(run.dialog->select("sous") && run.dialog->rowMenuLabels().size() == 2
                      && run.dialog->rowMenuLabels().front() == "\xC3\x89pingler" && run.dialog->rowMenuLabels().back() == "Copier le chemin",
                  "clic droit sur un dossier : Epingler, Copier le chemin");
            check(run.dialog->rowMenuChoose("Epingler") && !run.dialog->rowMenuLabels().empty()
                      && run.dialog->rowMenuLabels().front() == "D\xC3\xA9s\xC3\xA9pingler",
                  "Epingler (sans accent, par script) : l'entree devient Desepingler");
            check(run.dialog->rowMenuChoose("desep") && run.dialog->rowMenuLabels().front() == "\xC3\x89pingler", "Desepingler");
            check(run.dialog->select("notice.pdf") && run.dialog->rowMenuLabels().front().rfind("Ouvrir avec le programme", 0) == 0,
                  "clic droit sur un fichier : Ouvrir avec le programme du systeme");
            std::string why;
            check(!run.dialog->rowMenuChoose("Renommer", &why) && why.find("Copier le chemin") != std::string::npos,
                  "une entree absente du menu : dite");
            check(!run.dialog->chooseSystemExplorer(&why) && !run.drew("Utiliser l'explorateur du syst\xC3\xA8me"),
                  "sans reglage fourni par l'appli : pas de lien");
            run.dialog->cancel();
        }
        // Le lien du bas : "Utiliser l'explorateur du systeme" (le reglage de l'appli).
        {
            ui::FilePick pick;
            pick.start = u8(adir);
            ui::FileExplorerContext ctx;
            ctx.memory = std::make_shared<f::Memory>();
            int chosen = 0;
            ctx.useSystemExplorer = [&chosen] { ++chosen; };
            ExplorerRun run(pick, std::move(ctx));
            check(run.drew("Utiliser l'explorateur du syst\xC3\xA8me"), "le lien du bas est peint");
            check(run.dialog->chooseSystemExplorer() && chosen == 1 && run.dialog->systemExplorerChosen(), "le lien : le reglage change");
            run.frame();
            check(run.drew("la prochaine fois"), "le lien dit que c'est note");
            // Chercher "et ses sous-dossiers" (explorateur-sous-dossiers).
            run.dialog->setSearch("csv");
            check(run.dialog->lines().size() == 1 && anyLine(run.dialog->lines(), "liste.csv"), "chercher csv : le dossier seul");
            run.dialog->setSearchSubfolders(true);
            const auto deep = run.dialog->lines();
            check(run.dialog->searchSubfolders() && deep.size() == 3 && anyLine(deep, "liste.csv")
                      && (anyLine(deep, "sous/a.csv") || anyLine(deep, "sous\\a.csv")) && (anyLine(deep, "sous/b.csv") || anyLine(deep, "sous\\b.csv")),
                  "et ses sous-dossiers : 3 CSV, les noms relatifs (" + joined(deep) + ")");
            check((run.dialog->select("sous/a.csv") || run.dialog->select("sous\\a.csv"))
                      && run.dialog->currentPreview().path.find("a.csv") != std::string::npos,
                  "choisir un CSV d'un sous-dossier par son nom relatif");
            run.dialog->setSearchSubfolders(false);
            check(run.dialog->lines().size() == 1, "la case decochee : le dossier seul");
            run.frame();
            run.dialog->cancel();
        }
    }
    // ---- fin Lot API 8 : l'explorateur, 2e partie ----

    fs::remove_all(root, ec);
    std::printf("%s (%d echec%s)\n", failures ? "ECHEC" : "OK", failures, failures > 1 ? "s" : "");
    return failures ? 1 : 0;
}
