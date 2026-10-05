// =============================================================================
//  app/screens/StartScreen.cpp - l'accueil (lot API 7)
// -----------------------------------------------------------------------------
//  LES DIALOGUES NE S'EMPILENT PLUS. MenuManager rappelle OnEnter sur l'ecran
//  du dessous chaque fois qu'un dialogue se ferme (popEntry), sans avoir appele
//  OnExit a son ouverture : seul un ECRAN pousse le fait. L'ancien onEnter
//  rebranchait tous les signaux a chaque passage (links_ += ...) : apres N
//  dialogues, chaque bouton partait N + 1 fois, et "Nouveau projet" ouvrait
//  N + 1 dialogues l'un sur l'autre. Maintenant :
//    - les signaux des widgets se branchent UNE FOIS, dans buildUi (uiLinks_) ;
//    - onEnter vide puis renouvelle les abonnements au bus (busLinks_) et relit
//      les projets : il peut etre appele deux fois de suite sans rien doubler ;
//    - un geste ne demande qu'un dialogue par image (claimDialog) : deux clics
//      arrives dans la meme image n'en ouvrent pas deux.
//
//  CE QUI SE LIT, pour chaque projet recent : le manifeste, l'icone
//  (config/icone.txt) et l'index des versions (versions/index.txt) - de petits
//  fichiers. Ce qui coute (compter les variables, les vues, les sections) ne se
//  lit que pour le projet choisi, une fois (loadDetails).
//
//  LES MODIFICATIONS EN MEMOIRE NE SE PERDENT PLUS EN SILENCE. Revenir a
//  l'accueil garde les modifications du projet ouvert en memoire ; ouvrir un
//  autre projet d'ici les remplacait sans un mot. confirmReplace le demande
//  d'abord. Et "ouvrir" le projet deja ouvert y revient (SwitchMenu) au lieu
//  de le relire du disque.
// =============================================================================
#include "../../help/Novelties.hpp"     // 1.10 (chantier P)
#include "Screens.hpp"

#include "../App.hpp"
#include "../Dossiers.hpp"
#include "../../core/Version.hpp"
#include "../StartPage.hpp"
#include "../hmi/HmiStationLaunch.hpp"     // lot 15 : le poste au demarrage du PC
#include "../../hmi/HmiVersionState.hpp"
#include "../../core/CodeStats.hpp"   // 1.11.7 : les lignes de code de l'application   // la ligne de version : "V48 en cours"
#include "../../project/ProjectIcon.hpp"
#include "../../project/ProjectStore.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace app {

using namespace ui;

namespace {

namespace fs = std::filesystem;

// La pastille sous le nom de l'application : le lot, et le jour ou ce programme
// a ete construit (__DATE__, "Sep 28 2026" -> "28/09/2026") - pas le jour ou
// on l'ouvre, qui ne dit pas quelle version tourne.
// 1.8.0 : la pastille sous le nom dit la version (core/Version.hpp), plus le lot.
const char* const kRelease = XPG_ANALYZER_VERSION;

// 1.11.7 : 453306 -> "453 306".
std::string groupedNumber(long long n) {
    std::string d = std::to_string(n < 0 ? -n : n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return (n < 0 ? "-" : "") + out;
}

// 1.11.7 : « Code : 453 306 lignes · .h 0 · .hpp 58 943 · .c 0 · .cpp 394 363 ».
std::string codeStatsText() {
    std::string out = "Code : " + groupedNumber(xpg::codestats::kTotalLines) + " lignes";
    for (const auto& e : xpg::codestats::kByExtension) out += " \xC2\xB7 " + std::string(e.extension) + " " + groupedNumber(e.lines);
    return out;
}

std::string buildDate() {
    const std::string_view d = __DATE__;
    const std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const auto m = d.size() >= 11 ? months.find(d.substr(0, 3)) : std::string_view::npos;
    if (m == std::string_view::npos) return {};
    const int day = std::atoi(std::string(d.substr(4, 2)).c_str());
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%.4s", day % 100, static_cast<int>(m / 3 + 1), d.substr(7, 4).data());
    return buf;
}

// ---------------------------------------------------------------- fichiers ----
// Un fichier entier ; faux s'il manque ou depasse `limit` octets.
bool readFile(const fs::path& path, std::string& out, std::uintmax_t limit = 64u * 1024u * 1024u) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > limit) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// Les lignes utiles d'un fichier du projet : ni vides, ni commentaires (#).
template <class Fn>
void eachLine(std::string_view text, Fn&& fn) {
    std::size_t from = 0;
    while (from < text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string_view::npos) nl = text.size();
        auto line = text.substr(from, nl - from);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos && line[first] != '#') fn(line);
        from = nl + 1;
    }
}

std::size_t countLines(const fs::path& path) {
    std::string text;
    if (!readFile(path, text)) return 0;
    std::size_t n = 0;
    eachLine(text, [&n](std::string_view) { ++n; });
    return n;
}

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

// Le meme dossier, ecrit autrement ("D:\A\B" et "d:/a/b", un lien) ?
bool samePath(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    std::error_code ec;
    const bool same = fs::equivalent(fs::path(a), fs::path(b), ec);
    return same && !ec;
}

// Le dernier morceau d'un chemin, meme termine par un separateur.
std::string leafName(const std::string& path) {
    fs::path p(path);
    if (p.filename().empty()) p = p.parent_path();
    const auto leaf = p.filename().string();
    return leaf.empty() ? path : leaf;
}

// Plusieurs chemins separes par ';' (un .XPG et son .XHW) ; sans les espaces ni
// les guillemets que l'Explorateur met autour.
std::vector<std::string> splitPaths(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto sep = text.find(';', from);
        std::string piece = trim(std::string_view(text).substr(from, (sep == std::string::npos ? text.size() : sep) - from));
        if (piece.size() >= 2 && piece.front() == '"' && piece.back() == '"') piece = trim(piece.substr(1, piece.size() - 2));
        if (!piece.empty()) parts.push_back(std::move(piece));
        if (sep == std::string::npos) break;
        from = sep + 1;
    }
    return parts;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

std::string sizeText(std::uintmax_t bytes) {
    char buf[48];
    if (bytes >= 1024u * 1024u) std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024u) std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(bytes) / 1024.0);
    else std::snprintf(buf, sizeof buf, "%u octets", static_cast<unsigned>(bytes));
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// ------------------------------------------------------------------ dates ----
std::tm localTime(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string stampOf(const std::tm& t) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

std::string fileStamp(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return {};
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    return stampOf(localTime(std::chrono::system_clock::to_time_t(sys)));
}

// "2026-09-28 18:20:33" -> "aujourd'hui a 18:20", "hier a 17:55", "12/09 a 11:40",
// et "12/09/2025" pour une autre annee.
std::string friendlyWhen(const std::string& stamp) {
    if (stamp.size() < 16 || stamp[4] != '-' || stamp[7] != '-') return stamp;
    const std::time_t now = std::time(nullptr);
    const std::string today = stampOf(localTime(now)).substr(0, 10);
    const std::string yesterday = stampOf(localTime(now - 24 * 60 * 60)).substr(0, 10);
    const std::string day = stamp.substr(0, 10), hm = stamp.substr(11, 5);
    if (day == today) return "aujourd'hui \xC3\xA0 " + hm;
    if (day == yesterday) return "hier \xC3\xA0 " + hm;
    const std::string dm = stamp.substr(8, 2) + "/" + stamp.substr(5, 2);
    if (stamp.compare(0, 4, today, 0, 4) == 0) return dm + " \xC3\xA0 " + hm;
    return dm + "/" + stamp.substr(0, 4);
}

// "2026-09-28 18:12" -> "28/09 18:12".
std::string shortDate(const std::string& stamp) {
    if (stamp.size() < 16) return stamp;
    return stamp.substr(8, 2) + "/" + stamp.substr(5, 2) + " " + stamp.substr(11, 5);
}

// "BMXP342020" -> "BMX P34 2020" : la reference telle que l'ecrit le catalogue.
std::string prettyCpu(const std::string& ref) {
    const auto upper = [](char ch) { return ch >= 'A' && ch <= 'Z'; };
    const auto digit = [](char ch) { return ch >= '0' && ch <= '9'; };
    if (ref.size() < 8 || ref.find(' ') != std::string::npos) return ref;
    if (!upper(ref[0]) || !upper(ref[1]) || !upper(ref[2]) || !upper(ref[3]) || !digit(ref[4]) || !digit(ref[5])) return ref;
    return ref.substr(0, 3) + " " + ref.substr(3, 3) + " " + ref.substr(6);
}

// ------------------------------------------------------------- les icones ----
// Les pixels des icones de projet, d'un passage a l'autre sur l'accueil : la
// meme icone garde les memes pixels, donc la meme texture (StartPage.cpp).
struct CachedIcon {
    std::string                                      text;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
};

std::map<std::string, CachedIcon>& iconCache() {
    static std::map<std::string, CachedIcon> cache;
    return cache;
}

// L'icone d'un projet (config/icone.txt), agrandie quatre fois sans lisser :
// une icone de pixels reste nette a 44 et a 64 points.
std::shared_ptr<const std::vector<std::uint8_t>> projectIcon(const std::string& folder) {
    auto& cache = iconCache();
    std::string text;
    if (!readFile(fs::path(folder) / "config" / "icone.txt", text, 256u * 1024u)) {
        cache.erase(folder);
        return nullptr;
    }
    if (const auto it = cache.find(folder); it != cache.end() && it->second.text == text) return it->second.pixels;
    const auto icon = project::icon::fromText(text);
    if (!icon) {
        cache.erase(folder);
        return nullptr;
    }
    const auto small = project::icon::rgba(*icon);
    constexpr int kSide = project::icon::kSize;
    constexpr int kScale = start::kIconPixels / kSide;
    if (small.size() != static_cast<std::size_t>(kSide) * kSide * 4u) return nullptr;
    auto big = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(start::kIconPixels) * start::kIconPixels * 4u);
    for (int y = 0; y < start::kIconPixels; ++y)
        for (int x = 0; x < start::kIconPixels; ++x) {
            const auto from = static_cast<std::size_t>(((y / kScale) * kSide + x / kScale) * 4);
            const auto to = static_cast<std::size_t>((y * start::kIconPixels + x) * 4);
            for (std::size_t k = 0; k < 4; ++k) (*big)[to + k] = small[from + k];
        }
    std::shared_ptr<const std::vector<std::uint8_t>> pixels = std::move(big);
    cache[folder] = CachedIcon{std::move(text), pixels};
    return pixels;
}

// La couleur du point d'une version : validee verte, livree rouge (comme LOCK),
// brouillon bleu, automatique grise.
ui::Tone versionTone(hmi::ver::State s) {
    using S = hmi::ver::State;
    switch (s) {
        case S::Validated: return ui::Tone::Ok;
        case S::Delivered: return ui::Tone::Error;
        case S::Draft: return ui::Tone::Accent;
        case S::BeforeRestore: return ui::Tone::Warning;
        case S::Auto: break;
    }
    return ui::Tone::Muted;
}

// ------------------------------------------------------ renommer (ferme) ----
// Le nom d'un projet FERME, change dans son manifeste (project.xpgproj) : la
// ligne "name = ..." et la date de modification ; le reste du fichier est garde
// a l'octet pres, ses fins de ligne comprises. Ecrit a cote, puis renomme
// par-dessus : un disque plein ne laisse pas un manifeste coupe.
bool renameManifest(const std::string& folder, const std::string& name, std::string& why) {
    if (name.find_first_of("\r\n") != std::string::npos) {
        why = "un nom de projet tient sur une ligne";
        return false;
    }
    const fs::path file = fs::path(folder) / "project.xpgproj";
    std::string text;
    if (!readFile(file, text, 1024u * 1024u)) {
        why = "le manifeste (project.xpgproj) ne se lit pas";
        return false;
    }
    const bool crlf = text.find("\r\n") != std::string::npos;
    std::vector<std::string> lines;
    std::size_t from = 0;
    while (true) {
        const auto nl = text.find('\n', from);
        std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    const std::string now = stampOf(localTime(std::time(nullptr)));
    bool named = false;
    std::size_t after = 0;                 // ou poser "name" s'il manquait : apres formatVersion
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto& line = lines[i];
        const auto eq = line.find('=');
        if (line.empty() || line.front() == '#' || eq == std::string::npos) continue;
        const std::string key = trim(std::string_view(line).substr(0, eq));
        if (key == "formatVersion") after = i + 1;
        if (key == "name" && !named) {
            line = "name = " + name;
            named = true;
        } else if (key == "modified") {
            line = "modified = " + now;
        }
    }
    if (!named) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(std::min(after, lines.size())), "name = " + name);
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += crlf ? "\r\n" : "\n";
        out += lines[i];
    }
    const fs::path tmp = fs::path(folder) / "project.xpgproj.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(out.data(), static_cast<std::streamsize>(out.size()));
        f.close();
        if (!f) {
            why = "impossible d'\xC3\xA9" "crire dans le dossier du projet";
            std::error_code ignored;
            fs::remove(tmp, ignored);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        why = ec.message();
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

} // namespace

// ========================================================= StartupScreen ====
StartupScreen::StartupScreen(App& app) : menu::WidgetMenu("startup"), app_(app) {}

StartupScreen::~StartupScreen() = default;

core::Status StartupScreen::buildUi() {
    auto page = std::make_unique<start::StartPage>("startup.page");
    page_ = page.get();
    auto& rail = page_->rail();
    auto& grid = page_->grid();
    auto& detail = page_->detail();

    rail.setRelease(kRelease, buildDate());
    rail.setCodeStats(codeStatsText());              // 1.11.7 : les lignes de code (.h, .hpp, .c, .cpp)
    std::string program = leafName(station::currentExecutable());
    if (program.empty()) program = "xpg_analyzer";
    page_->setStatusRight("Entr\xC3\xA9" "e : ouvrir \xC2\xB7 F2 : renommer \xC2\xB7 Suppr : supprimer \xC2\xB7 Ctrl+F : chercher", "PLC Project Analyzer \xC2\xB7 " + program);
    page_->setDetailsLoader([this](start::ProjectEntry& e) { loadDetails(e); });
    page_->status().setMessage("Pr\xC3\xAAt");
    // 1.10 (chantier P) : l'accueil propose les nouveautes pas encore vues.
    {
        const auto& st = help::news::session();
        std::size_t unseen = 0;
        for (const auto* it : help::news::pending(st, help::news::sessionVersion()))
            if (!help::news::wasSeen(st, it->id)) ++unseen;
        if (unseen > 0)
            page_->status().setMessage("Nouveaut\xC3\xA9s de la " + help::news::shortVersion(help::news::sessionVersion()) + " : " + std::to_string(unseen)
                                       + " \xC3\xA0 d\xC3\xA9" "couvrir \xE2\x80\x94 menu Aide \xE2\x80\xBA Nouveaut\xC3\xA9s\xE2\x80\xA6");
    }

    // LES SIGNAUX DES WIDGETS, UNE FOIS POUR TOUTES : ici et pas dans onEnter,
    // que MenuManager rappelle a chaque dialogue ferme (voir en tete).
    uiLinks_ += rail.newCard().clicked->connect([this] { newProject(); });
    uiLinks_ += rail.openCard().clicked->connect([this] { openFileDialog(); });
    uiLinks_ += rail.tutorialLink().clicked->connect([this] { showTutorial(); });
    uiLinks_ += rail.helpLink().clicked->connect([this] { triggerAction("help.open"); });
    uiLinks_ += rail.settingsLink().clicked->connect([this] { showSettings(); });
    // 1.8.0 : les dossiers de l'application.
    uiLinks_ += rail.foldersLink().clicked->connect([this] {
        if (!claimDialog()) return;
        showFoldersDialog(app_);
    });
    uiLinks_ += rail.swatches().chosen->connect([this](const std::string& key) { chooseTheme(key); });
    uiLinks_ += rail.autostartButton().clicked->connect([this] { askAutostart(); });
    // Le bouton ... du champ du chemin : l'explorateur part du dossier des
    // projets ; le project.xpgproj choisi designe son dossier ; puis comme Entree.
    {
        auto spec = rail.browseButton().spec();
        spec.start = App::projectsRoot().string();
        rail.browseButton().setSpec(std::move(spec));
    }
    uiLinks_ += rail.browseButton().chosen->connect([this](const std::string& path) {
        std::string lowered = path;
        for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string_view project = ".xpgproj";
        if (lowered.size() > project.size() && lowered.compare(lowered.size() - project.size(), project.size(), project) == 0) {
            const auto slash = path.find_last_of("/\\");
            if (slash != std::string::npos) page_->rail().pathField().setText(path.substr(0, slash));
        }
        openTyped();
    });
    uiLinks_ += grid.activated->connect([this](int) { openSelected(); });
    uiLinks_ += grid.newRequested->connect([this] { newProject(); });
    uiLinks_ += detail.openButton().clicked->connect([this] { openSelected(); });
    uiLinks_ += detail.stationButton().clicked->connect([this] { openStation(); });   // lot 15
    uiLinks_ += detail.duplicateButton().clicked->connect([this] { duplicateSelected(); });
    uiLinks_ += detail.renameButton().clicked->connect([this] { renameSelected(); });
    uiLinks_ += detail.removeButton().clicked->connect([this] { supprimerProjet(); });

    setRoot(std::move(page));
    return core::ok();
}

void StartupScreen::onEnter() {
    // IDEMPOTENT : appele a chaque dialogue ferme, sans onExit entre deux. Le
    // bus d'abord vide, puis rebranche ; les projets relus (un dialogue a pu en
    // creer, en supprimer, en renommer).
    busLinks_.clear();
    busLinks_ += app_.events().subscribe<importer::ImportStarted>([this](const importer::ImportStarted& e) {
        page_->status().setMessage("Lecture de " + leafName(e.path) + "\xE2\x80\xA6", StatusBar::Severity::Info);
    });
    busLinks_ += app_.events().subscribe<importer::ImportProgress>([this](const importer::ImportProgress& p) {
        std::string text = "Import : " + p.stage;
        if (p.fraction > 0.f && p.fraction <= 1.f) text += " (" + std::to_string(std::lround(p.fraction * 100.f)) + " %)";
        page_->status().setMessage(text, StatusBar::Severity::Info);
    });
    busLinks_ += app_.events().subscribe<importer::ImportFailed>([this](const importer::ImportFailed& e) {
        page_->status().setMessage("Import impossible : " + e.message, StatusBar::Severity::Error);
    });
    // Un enregistrement (le projet ouvert renomme, "les enregistrer d'abord") :
    // la carte doit le montrer.
    busLinks_ += app_.events().subscribe<ProjectSaved>([this](const ProjectSaved&) { reload(); });
    page_->grid().resetClickChain();
    reload();
    // 1.8.0 : ce que le demarrage n'a pas pu faire avec les dossiers (un dossier
    // des donnees inaccessible, un repli) - dit une fois, ici.
    if (const auto messages = dossiers::prendreMessages(); !messages.empty()) {
        std::string text;
        for (const auto& m : messages) text += (text.empty() ? std::string{} : std::string(" ")) + m;
        page_->status().setMessage(text, StatusBar::Severity::Warning);
    }
}

void StartupScreen::onExit() {
    busLinks_.clear();
    pendingDrops_.clear();
}

void StartupScreen::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    ++frame_;
    // 1.8.0 : l'icone sur la barre des taches (cochee a l'installation). Windows ne
    // l'accorde qu'a une fenetre au premier plan : une seconde apres l'ouverture, et
    // quand aucun dialogue n'est ouvert (la cle administrateur passe avant).
    if (app_.taskbarPinPending() && frame_ > 60 && manager().top() == this) app_.proposeTaskbarPin();
    // Les fichiers glisses dans la fenetre : ceux d'un meme depot arrivent un a
    // un, dans la meme image ; on les ouvre ensemble (un .XPG et son .XHW).
    if (!pendingDrops_.empty()) {
        auto drops = std::move(pendingDrops_);
        pendingDrops_.clear();
        openDropped(std::move(drops));
    }
}

bool StartupScreen::claimDialog() {
    if (dialogFrame_ == frame_) return false;
    dialogFrame_ = frame_;
    return true;
}

// ------------------------------------------------------------ le clavier ----
ui::EventResult StartupScreen::HandleEvent(const ui::InputEvent& ev) {
    if (!page_) return menu::WidgetMenu::HandleEvent(ev);
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        // N'importe ou dans la fenetre : ouvert a la fin de l'image (Update).
        if (!drop->path.empty()) pendingDrops_.push_back(drop->path);
        return ui::EventResult::Consumed;
    }
    // Taper sans avoir clique dans un champ, c'est chercher (comme Ctrl+F puis
    // le texte) : la recherche prend le focus, et le texte avec. Un champ qui a
    // le focus garde ce qu'on y tape ; une espace seule (Espace sur un bouton
    // pris au clavier) ne lance rien.
    if (const auto* t = std::get_if<ui::TextInput>(&ev);
        t && t->utf8.find_first_not_of(' ') != std::string::npos && !page_->search().focused()
        && !page_->rail().pathField().focused() && !page_->sortBox().isOpen())
        page_->search().takeFocus();
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (!k) return menu::WidgetMenu::HandleEvent(ev);
    if (handleKey(*k, true)) return ui::EventResult::Consumed;
    if (menu::WidgetMenu::HandleEvent(ev) == ui::EventResult::Consumed) return ui::EventResult::Consumed;
    return handleKey(*k, false) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
}

// `beforeWidgets` : ce qui passe AVANT les widgets (un champ garderait Entree
// et Echap pour lui) ; sinon, ce que personne n'a pris.
bool StartupScreen::handleKey(const ui::KeyDown& k, bool beforeWidgets) {
    auto& page = *page_;
    auto& grid = page.grid();
    auto& search = page.search();
    if (page.sortBox().isOpen()) return false;              // la liste du tri a ses touches
    if (beforeWidgets) {
        if (search.focused()) {
            switch (k.key) {
                case Key::Escape:
                    if (search.text().empty()) return false;   // le champ rend le focus
                    page.setQuery({});
                    return true;
                case Key::Return:
                    if (!k.repeat) openSelected();
                    return true;
                case Key::Up: grid.moveSelection(-grid.columns()); return true;
                case Key::Down: grid.moveSelection(grid.columns()); return true;
                case Key::PageUp: grid.pageSelection(-1); return true;
                case Key::PageDown: grid.pageSelection(1); return true;
                default: return false;
            }
        }
        if (page.rail().pathField().focused() && k.key == Key::Return) {
            if (!k.repeat) openTyped();
            return true;
        }
        return false;
    }
    const bool ctrlOnly = k.mods.ctrl && !k.mods.alt && !k.mods.shift && !k.mods.super;
    if (ctrlOnly && k.key == Key::F) {
        search.takeFocus();
        return true;
    }
    if (ctrlOnly && k.key == Key::N) {
        if (!k.repeat) newProject();
        return true;
    }
    if (ctrlOnly && k.key == Key::O) {
        if (!k.repeat) openFileDialog();
        return true;
    }
    if (!k.mods.none()) return false;
    switch (k.key) {
        case Key::F1:
            if (!k.repeat) triggerAction("help.open");
            return true;
        case Key::F2:
            if (!k.repeat) renameSelected();
            return true;
        case Key::Delete:
            if (!k.repeat) supprimerProjet();
            return true;
        case Key::Return:
            if (!k.repeat) openSelected();
            return true;
        case Key::Escape:
            if (page.query().empty()) return false;
            page.setQuery({});
            return true;
        case Key::Left: grid.moveSelection(-1); return true;
        case Key::Right: grid.moveSelection(1); return true;
        case Key::Up: grid.moveSelection(-grid.columns()); return true;
        case Key::Down: grid.moveSelection(grid.columns()); return true;
        case Key::PageUp: grid.pageSelection(-1); return true;
        case Key::PageDown: grid.pageSelection(1); return true;
        case Key::Home: grid.selectEdge(false); return true;
        case Key::End: grid.selectEdge(true); return true;
        default: return false;
    }
}

// ----------------------------------------------------------- les projets ----
void StartupScreen::reload(const std::string& keep) {
    const auto* sel = page_->selected();
    const std::string keepPath = !keep.empty() ? keep : sel ? sel->path : std::string{};
    std::string autostartName;
    auto entries = readRecent(autostartName);
    page_->setEntries(std::move(entries), keepPath);
    page_->rail().setAutostart(std::move(autostartName));
}

std::vector<start::ProjectEntry> StartupScreen::readRecent(std::string& autostartName) {
    std::vector<start::ProjectEntry> out;
    // Lot 15 : le projet que lance le PC a son demarrage.
    const std::string autostart = station::autostartProject();
    const auto& paths = app_.recentPaths();
    out.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        start::ProjectEntry e;
        e.path = paths[i];
        e.recentRank = i;
        std::error_code ec;
        e.isFolder = project::ProjectStore::isProjectFolder(e.path);
        if (e.isFolder) {
            // A project folder knows its own name and state; a bare export does
            // not, so it is labelled by its file name.
            if (auto m = project::ProjectStore::readManifest(e.path)) {
                // Le nom du DOSSIER, comme la barre du haut : celui de l'export
                // (souvent « Projet », le nom par defaut de Control Expert) ne
                // distingue pas deux affaires.
                e.name = leafName(e.path);
                if (e.name.empty()) e.name = m->name;
                e.cpu = prettyCpu(m->cpuReference);
                e.state = m->state;
                e.stamp = m->modified;
            }
            if (e.stamp.empty()) e.stamp = fileStamp(fs::path(e.path) / "project.xpgproj");
            e.badge = std::string(project::toString(e.state));
            e.icon = projectIcon(e.path);
            // La version que l'on modifie, comme la barre du haut : "V48 en
            // cours", "depuis V47 . auto" - et les quatre dernieres.
            const auto store = hmi::ver::open(e.path);
            const hmi::ver::Store* versions = store ? &*store : nullptr;
            const auto standing = hmi::ver::standing(e.state, versions, 0, 0);
            e.versionTitle = standing.title;
            e.versionSubtitle = standing.subtitle;
            if (versions)
                for (auto it = versions->versions.rbegin(); it != versions->versions.rend() && e.versions.size() < 4; ++it) {
                    start::VersionLine line;
                    line.number = "V" + std::to_string(it->number);
                    line.label = it->state == hmi::ver::State::Auto ? std::string("automatique")
                               : !it->name.empty()                  ? it->name
                                                                    : std::string(hmi::ver::stateLabel(it->state));
                    line.date = shortDate(it->date);
                    line.tone = versionTone(it->state);
                    e.versions.push_back(std::move(line));
                }
            e.autostart = !autostart.empty() && samePath(autostart, e.path);
        } else if (fs::exists(e.path, ec)) {
            e.name = leafName(e.path);
            e.badge = "export";
            std::string ext = fs::path(e.path).extension().string();
            for (auto& ch : ext) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            e.versionTitle = ext.empty() ? std::string("export Control Expert") : "export " + ext;
            e.versionSubtitle = "pas encore un projet";
            e.stamp = fileStamp(e.path);
        } else {
            e.missing = true;
            e.name = leafName(e.path);
            e.badge = "introuvable";
            e.versionSubtitle = "d\xC3\xA9plac\xC3\xA9, renomm\xC3\xA9, ou sur un lecteur absent";
        }
        if (e.name.empty()) e.name = leafName(e.path);
        e.when = friendlyWhen(e.stamp);
        e.open = isOpenProject(e);
        e.modified = e.open && app_.commands().isModified();
        out.push_back(std::move(e));
    }
    // Le nom du projet lance au demarrage : sa carte, sinon son manifeste.
    autostartName.clear();
    if (!autostart.empty()) {
        for (const auto& e : out)
            if (e.autostart) {
                autostartName = e.name;
                break;
            }
        if (autostartName.empty()) {
            autostartName = leafName(autostart);
        }
    }
    // Les icones des projets sortis de la liste s'en vont.
    auto& cache = iconCache();
    for (auto it = cache.begin(); it != cache.end();) {
        if (std::find(paths.begin(), paths.end(), it->first) == paths.end()) it = cache.erase(it);
        else ++it;
    }
    return out;
}

// Ce que le panneau de droite dit d'un projet, compte sans l'importer : les
// fichiers du dossier, lus une fois, au premier choix du projet.
void StartupScreen::loadDetails(start::ProjectEntry& e) {
    e.factsLoaded = true;
    e.facts.clear();
    const fs::path root(e.path);
    if (e.missing) {
        e.facts.emplace_back("Chemin", e.path);
        return;
    }
    if (!e.isFolder) {
        std::error_code ec;
        const auto bytes = fs::file_size(root, ec);
        std::string ext = root.extension().string();
        for (auto& ch : ext) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        e.facts.emplace_back("Fichier", "export " + ext + (ec ? std::string{} : " \xC2\xB7 " + sizeText(bytes)));
        e.facts.emplace_back("L'ouvrir", "le lit, puis propose d'en faire un projet");
        e.facts.emplace_back("Dossier", root.parent_path().string());
        return;
    }

    // L'automate : la reference, et sa famille (config/hardware.txt).
    std::string family;
    if (std::string hw; readFile(root / "config" / "hardware.txt", hw, 16u * 1024u * 1024u))
        eachLine(hw, [&family](std::string_view line) {
            const auto eq = line.find('=');
            if (family.empty() && eq != std::string_view::npos && trim(line.substr(0, eq)) == "family") family = trim(line.substr(eq + 1));
        });
    std::string cpu = e.cpu;
    if (!family.empty()) cpu += (cpu.empty() ? "" : " \xC2\xB7 ") + family;
    e.facts.emplace_back("Automate", cpu.empty() ? std::string("\xE2\x80\x94") : cpu);

    // Le programme : les index du dossier (une ligne par section, unite, bloc, type).
    const auto sections = countLines(root / "sections" / "index.txt");
    const auto units = countLines(root / "units" / "index.txt");
    const auto dfbs = countLines(root / "dfb" / "index.txt");
    const auto ddts = countLines(root / "ddt" / "index.txt");
    std::string program = plural(sections, "section", "sections");
    if (units) program += " \xC2\xB7 " + plural(units, "unit\xC3\xA9", "unit\xC3\xA9s");
    if (dfbs) program += " \xC2\xB7 " + std::to_string(dfbs) + " DFB";
    if (ddts) program += " \xC2\xB7 " + std::to_string(ddts) + " DDT";
    e.facts.emplace_back("Programme", program);

    // Les variables globales ; situees : celles qui ont une adresse (3e champ).
    std::size_t globals = 0, located = 0;
    if (std::string vars; readFile(root / "vars" / "globals.txt", vars))
        eachLine(vars, [&](std::string_view line) {
            ++globals;
            const auto a = line.find(';');
            const auto b = a == std::string_view::npos ? a : line.find(';', a + 1);
            const auto c = b == std::string_view::npos ? b : line.find(';', b + 1);
            if (b != std::string_view::npos && !trim(line.substr(b + 1, (c == std::string_view::npos ? line.size() : c) - b - 1)).empty()) ++located;
        });
    e.facts.emplace_back("Variables", globals == 0 ? std::string("aucune")
                                                   : plural(globals, "globale", "globales") + (located ? " \xC2\xB7 " + plural(located, "situ\xC3\xA9" "e", "situ\xC3\xA9" "es") : std::string{}));

    // L'IHM : l'index ihm/ihm.txt (une ligne par vue, variable IHM, alarme).
    if (std::string ihm; readFile(root / "ihm" / "ihm.txt", ihm)) {
        std::size_t views = 0, hmiVars = 0, alarms = 0;
        eachLine(ihm, [&](std::string_view line) {
            if (line.rfind("vue ", 0) == 0) ++views;
            else if (line.rfind("variable ", 0) == 0) ++hmiVars;
            else if (line.rfind("alarme ", 0) == 0) ++alarms;
        });
        std::string text = plural(views, "vue", "vues");
        if (hmiVars) text += " \xC2\xB7 " + plural(hmiVars, "variable", "variables");
        if (alarms) text += " \xC2\xB7 " + plural(alarms, "alarme", "alarmes");
        e.facts.emplace_back("IHM", text);
    } else {
        e.facts.emplace_back("IHM", "pas encore");
    }

    // Les versions : combien, et la derniere.
    if (const auto store = hmi::ver::open(e.path); store && store->last())
        e.facts.emplace_back("Versions", std::to_string(store->versions.size()) + " \xC2\xB7 la derni\xC3\xA8re le " + hmi::ver::whenText(store->last()->date));
    else
        e.facts.emplace_back("Versions", "aucune");
    e.facts.emplace_back("Dossier", e.path);
}

bool StartupScreen::isOpenProject(const start::ProjectEntry& e) const {
    if (!app_.project() || e.missing) return false;
    if (e.isFolder) return samePath(e.path, app_.projectFolder());
    if (!app_.projectFolder().empty()) return false;
    const auto& sources = app_.sourcePaths();
    return std::any_of(sources.begin(), sources.end(), [&e](const std::string& s) { return samePath(s, e.path); });
}

// Ouvrir un AUTRE projet remplace celui qui est en memoire. Revenir a l'accueil
// a garde ses modifications ; elles partaient ici sans un mot. On demande.
void StartupScreen::confirmReplace(std::function<void()> then) {
    const bool unsaved = app_.project() && !app_.projectFolder().empty() && app_.commands().isModified();
    if (!unsaved) {
        then();
        return;
    }
    const std::string name = app_.manifest().name.empty() ? leafName(app_.projectFolder()) : app_.manifest().name;
    const std::string keep = "Les enregistrer d'abord";
    const std::string drop = "Les abandonner";
    std::string why = name + " a des modifications qui ne sont pas encore enregistr\xC3\xA9" "es";
    if (const auto last = app_.commands().undoLabel(); !last.empty()) why += " (la derni\xC3\xA8re : " + last + ")";
    why += ". Ce que tu ouvres le remplace en m\xC3\xA9moire : choisis ce qu'elles deviennent.";
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Les modifications de " + name, keep, "", false, {keep, drop}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.startLeave", "Modifications non enregistr\xC3\xA9" "es", why, std::move(fields), "Continuer"),
        [this, keep, next = std::move(then)](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v.front() == keep) {
                if (auto st = app_.saveProject(); !st) {
                    app_.menus().ShowDialog(std::make_unique<MessageDialog>(
                                                "Enregistrement impossible",
                                                st.error().message() + "\n\nRien n'a \xC3\xA9t\xC3\xA9 ouvert : les modifications sont toujours en m\xC3\xA9moire.",
                                                MessageDialog::Icon::Error),
                                            [](const menu::DialogResult&) {});
                    return;
                }
            }
            next();
        });
}

void StartupScreen::triggerAction(const char* id) {
    if (auto st = app_.actions().trigger(id, app_.commands()); !st)
        page_->status().setTransientMessage(std::string("Pas disponible ici (") + id + ").", 6.0, StatusBar::Severity::Warning);
}

// ------------------------------------------------------------ les gestes ----
void StartupScreen::newProject() {
    if (!claimDialog()) return;
    confirmReplace([this] { triggerAction("project.new"); });
}

void StartupScreen::openFileDialog() {
    if (!claimDialog()) return;
    confirmReplace([this] { triggerAction("file.open"); });
}

void StartupScreen::openSelected() {
    const auto* e = page_->selected();
    if (!e) {
        page_->status().setTransientMessage("Choisis d'abord un projet dans la liste.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    if (e->missing) {
        page_->status().setTransientMessage("Introuvable : " + e->path + " - d\xC3\xA9plac\xC3\xA9, renomm\xC3\xA9, ou sur un lecteur absent. Suppr le retire de la liste.",
                                            8.0, StatusBar::Severity::Warning);
        return;
    }
    if (isOpenProject(*e)) {
        // Deja en memoire : on y revient tel quel. Le relire du disque perdrait
        // ce qui n'est pas enregistre.
        app_.menus().SwitchMenu("analysis");
        return;
    }
    if (!claimDialog()) return;
    const std::string path = e->path;          // une copie : la liste peut changer
    confirmReplace([this, path] { openPath(path); });
}

void StartupScreen::openPath(const std::string& path) {
    // A project folder and a bare export are opened by different machinery; the
    // startup screen decides which, so no other code has to care.
    if (project::ProjectStore::isProjectFolder(path)) {
        if (app_.project() && samePath(path, app_.projectFolder())) {
            app_.menus().SwitchMenu("analysis");
            return;
        }
        page_->status().setMessage("Ouverture de " + leafName(path) + "\xE2\x80\xA6", StatusBar::Severity::Info);
        app_.openProjectFolder(path);
        return;
    }
    // Several files may be given at once, separated by ';' - the natural way to
    // hand over a .XPG and its .XHW together.
    const auto parts = splitPaths(path);
    if (parts.empty()) return;
    for (const auto& p : parts)
        if (p != path && project::ProjectStore::isProjectFolder(p)) {
            openPath(p);                        // un dossier de projet parmi eux : lui
            return;
        }
    page_->status().setMessage(parts.size() == 1 ? "Lecture de " + leafName(parts.front()) + "\xE2\x80\xA6"
                                                 : "Lecture de " + std::to_string(parts.size()) + " fichiers\xE2\x80\xA6",
                               StatusBar::Severity::Info);
    if (parts.size() == 1) app_.openPath(parts.front());
    else app_.importer().importAsync(parts);
}

// Le champ du chemin, Entree. Un chemin faux se dit ici, sans dialogue.
void StartupScreen::openTyped() {
    const std::string text = trim(page_->rail().pathField().text());
    const auto parts = splitPaths(text);
    if (parts.empty()) {
        page_->status().setTransientMessage("Colle d'abord le chemin d'un dossier de projet ou d'un export, puis Entr\xC3\xA9" "e.", 6.0,
                                            StatusBar::Severity::Warning);
        return;
    }
    for (const auto& p : parts) {
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            page_->status().setTransientMessage("Introuvable : " + p, 8.0, StatusBar::Severity::Warning);
            return;
        }
    }
    if (!claimDialog()) return;
    const bool current = parts.size() == 1 && app_.project() && samePath(parts.front(), app_.projectFolder());
    if (current) openPath(text);
    else confirmReplace([this, text] { openPath(text); });
}

void StartupScreen::openDropped(std::vector<std::string> paths) {
    paths.erase(std::remove_if(paths.begin(), paths.end(), [](const std::string& p) { return p.empty(); }), paths.end());
    if (paths.empty()) return;
    std::string joined;
    for (const auto& p : paths) joined += (joined.empty() ? "" : " ; ") + p;
    // Le champ montre ce qui a ete depose : on voit ce qui s'ouvre, et on peut
    // le reprendre si ce n'etait pas le bon.
    page_->rail().pathField().setText(joined);
    openTyped();
}

// Lot 15 : ouvrir le projet choisi directement en poste d'exploitation.
void StartupScreen::openStation() {
    const auto* sel = page_->selected();
    if (!sel || !sel->isFolder || sel->missing) {
        page_->status().setMessage("Poste d'exploitation : choisis d'abord un projet (un dossier) dans la liste.", StatusBar::Severity::Warning);
        return;
    }
    const start::ProjectEntry e = *sel;
    if (!claimDialog()) return;
    if (e.state != project::State::Finish) {
        app_.menus().ShowDialog(std::make_unique<MessageDialog>(
                                    "Poste d'exploitation",
                                    "Seul un projet FINISH (d\xC3\xA9" "clar\xC3\xA9 fini) s'ouvre en poste d'exploitation.\n\n" + e.name + " est "
                                        + std::string(project::toString(e.state)) + " : ouvre-le, puis Projet > \xC3\x89tat > FINISH."
                                        + (e.state == project::State::Lock ? " (LOCK n'est pas fini : il interdit seulement les modifications.)" : ""),
                                    MessageDialog::Icon::Warning),
                                [](const menu::DialogResult&) {});
        return;
    }
    // Le poste relit le projet sur le disque : ce qui est en memoire et pas
    // enregistre se demande d'abord (meme pour le projet ouvert).
    confirmReplace([this, path = e.path] { app_.openStation(path); });
}

// Lot 15 : le poste d'exploitation lance au demarrage du PC - un projet FINISH,
// un seul par PC (l'ouverture de session automatique de Windows se regle a part).
void StartupScreen::askAutostart() {
    if (!claimDialog()) return;
    struct Option { std::string label, name, path; };
    std::vector<Option> options{{"Aucun (le PC ne lance rien)", {}, {}}};
    std::string current = options.front().label;
    const std::string running = station::autostartProject();
    for (const auto& e : page_->entries()) {
        if (!e.isFolder || e.missing || e.state != project::State::Finish) continue;
        options.push_back({e.name + " (" + e.path + ")", e.name, e.path});
        if (!running.empty() && samePath(running, e.path)) current = options.back().label;
    }
    if (!running.empty() && current == options.front().label) {
        options.push_back({"(actuel) " + running, {}, running});
        current = options.back().label;
    }
    std::vector<std::string> choices;
    for (const auto& o : options) choices.push_back(o.label);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Au d\xC3\xA9marrage du PC", current, "", false, choices});
    // Un projet FINISH qui n'est pas dans la liste : son dossier, tape ou choisi
    // avec le bouton ... (l'explorateur). Rempli, il l'emporte sur la liste.
    fields.push_back({"Ou un autre projet", "", "le dossier d'un projet FINISH (\xE2\x80\xA6 : l'explorateur)", false, {}});
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.autostart", "Poste d'exploitation au d\xC3\xA9marrage du PC",
            "\xC3\x80 l'ouverture de la session, le PC lance le poste d'exploitation de ce projet : l'IHM seule, en plein \xC3\xA9" "cran. "
            "Un seul projet par PC ; seuls les projets FINISH sont accept\xC3\xA9s (ceux de la liste, ou un autre dossier). Windows : la cl\xC3\xA9 Run de l'utilisateur ; "
            "Linux : ~/.config/autostart. Pour un PC sans personne, r\xC3\xA8gle aussi l'ouverture de session automatique (voir l'aide). "
            "Apr\xC3\xA8s une coupure de courant, le poste propose de recharger son \xC3\xA9tat (30 s, puis d'office).",
            std::move(fields), "Appliquer"),
            1, ui::chooseFolder(App::projectsRoot().string(), "Le projet du poste d'exploitation")),
        [this, options](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            const std::string choice = v.empty() ? std::string{} : v.front();
            std::string other = v.size() > 1 ? trim(v[1]) : std::string{};
            if (other.size() >= 2 && other.front() == '"' && other.back() == '"') other = trim(other.substr(1, other.size() - 2));
            const auto it = std::find_if(options.begin(), options.end(), [&choice](const Option& o) { return o.label == choice; });
            std::string where, why, message;
            bool ok = true;
            if (!other.empty()) {
                // Un autre dossier : un projet, et FINISH (le poste ne s'ouvre que sur lui).
                const auto manifest = project::ProjectStore::readManifest(other);
                if (!manifest) {
                    ok = false;
                    message = "Pas un dossier de projet (pas de project.xpgproj) : " + other;
                } else if (manifest->state != project::State::Finish) {
                    ok = false;
                    message = manifest->name + " est " + std::string(project::toString(manifest->state))
                              + " : seul un projet FINISH s'ouvre en poste d'exploitation.";
                } else {
                    ok = station::setAutostart(manifest->name, station::currentExecutable(), other, true, &where, &why);
                    message = ok ? "Au d\xC3\xA9marrage du PC : le poste d'exploitation de " + manifest->name + " (" + where + ")." : "Impossible : " + why;
                }
            } else if (it == options.end() || it == options.begin()) {
                const std::string launched = station::autostartProject();
                ok = launched.empty() || station::setAutostart({}, station::currentExecutable(), launched, false, &where, &why);
                message = ok ? std::string("Le PC ne lance plus de poste d'exploitation \xC3\xA0 son d\xC3\xA9marrage.") : "Impossible : " + why;
            } else if (it->name.empty()) {
                message = "Rien ne change.";
            } else {
                ok = station::setAutostart(it->name, station::currentExecutable(), it->path, true, &where, &why);
                message = ok ? "Au d\xC3\xA9marrage du PC : le poste d'exploitation de " + it->name + " (" + where + ")." : "Impossible : " + why;
            }
            reload();
            page_->status().setMessage(message, ok ? StatusBar::Severity::Success : StatusBar::Severity::Warning);
        });
}

// Dupliquer le projet choisi, sans l'ouvrir : ProjectStore::duplicate, comme
// l'action du projet ouvert (project.duplicate), qui ne sait copier que lui.
void StartupScreen::duplicateSelected() {
    const auto* sel = page_->selected();
    if (!sel || !sel->isFolder || sel->missing) {
        page_->status().setTransientMessage("Dupliquer : choisis d'abord un projet (un dossier) dans la liste.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    const start::ProjectEntry e = *sel;
    if (!claimDialog()) return;
    if (isOpenProject(e)) {
        triggerAction("project.duplicate");    // le projet ouvert : son action
        return;
    }
    // LOCK : la copie revient en DEV, sans verrou. La faire d'ici, sans le mot
    // de passe, contournerait le verrou : on passe par l'ouverture, qui le demande.
    if (e.state == project::State::Lock) {
        app_.menus().ShowDialog(
            std::make_unique<MessageDialog>("Projet verrouill\xC3\xA9",
                                            e.name + " est LOCK : sa copie serait modifiable (DEV, sans verrou).\n\n"
                                                     "Ouvre-le d'abord avec son mot de passe, puis Projet > Dupliquer.",
                                            MessageDialog::Icon::Warning, "Ouvrir"),
            [this, path = e.path](const menu::DialogResult& r) {
                if (r.accepted()) confirmReplace([this, path] { openPath(path); });
            });
        return;
    }
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nouveau nom", e.name + " - variante", "", false, {}});
    fields.push_back({"Dossier de la copie", e.path + " - copie", "", false, {}});
    // Le bouton ... : OU creer la copie (l'explorateur ; son nom reste).
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.duplicate", "Dupliquer le projet",
                                     "Copie " + e.name + " dans un nouveau dossier. La copie est toujours modifiable : elle revient en DEV, "
                                     "sans verrou et sans le dossier src/ g\xC3\xA9n\xC3\xA9r\xC3\xA9. Ses versions la suivent.",
                                     std::move(fields), "Dupliquer"),
                               1, ui::newFolder(e.path, "Dossier de la copie")),
        [this, from = e.path](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2 || trim(v[1]).empty()) return;
            const std::string name = trim(v[0]), to = trim(v[1]);
            if (auto ok = project::ProjectStore::duplicate(from, to, name); !ok) {
                app_.menus().ShowDialog(std::make_unique<MessageDialog>("Duplication impossible", ok.error().message(), MessageDialog::Icon::Error),
                                        [](const menu::DialogResult&) {});
                return;
            }
            app_.rememberPath(to);
            reload(to);
            page_->status().setTransientMessage("Copi\xC3\xA9 dans " + to + " : la copie est en DEV.", 8.0, StatusBar::Severity::Success);
        });
}

// Renommer le projet choisi : son nom, pas son dossier. Le projet ouvert passe
// par son action (le manifeste en memoire, puis l'enregistrement) ; un projet
// ferme change dans son manifeste, sans etre ouvert.
void StartupScreen::renameSelected() {
    const auto* sel = page_->selected();
    if (!sel) {
        page_->status().setTransientMessage("Renommer : choisis d'abord un projet dans la liste.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    const start::ProjectEntry e = *sel;
    if (!e.isFolder || e.missing) {
        page_->status().setTransientMessage("Un export garde le nom de son fichier : ouvre-le pour en faire un projet, qui aura son nom.", 8.0,
                                            StatusBar::Severity::Info);
        return;
    }
    if (e.state == project::State::Lock) {
        page_->status().setTransientMessage("LOCK : personne ne modifie " + e.name + " ; ouvre-le, puis Projet > D\xC3\xA9verrouiller.", 8.0,
                                            StatusBar::Severity::Warning);
        return;
    }
    if (!claimDialog()) return;
    if (isOpenProject(e)) {
        triggerAction("project.rename");       // l'enregistrement (ProjectSaved) relit la liste
        return;
    }
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", e.name, "", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.rename", "Renommer le projet",
                                     "Change le nom du projet, pas celui de son dossier sur le disque (" + e.path + ").", std::move(fields),
                                     "Renommer"),
        [this, path = e.path, old = e.name](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            const std::string name = v.empty() ? std::string{} : trim(v.front());
            if (name.empty() || name == old) return;
            if (std::string why; !renameManifest(path, name, why)) {
                app_.menus().ShowDialog(std::make_unique<MessageDialog>("Renommage impossible", path + "\n\n" + why, MessageDialog::Icon::Error),
                                        [](const menu::DialogResult&) {});
                return;
            }
            reload(path);
            page_->status().setTransientMessage(old + " s'appelle maintenant " + name + ".", 8.0, StatusBar::Severity::Success);
        });
}

// La suppression, qui est la seule action de cet ecran a effacer des fichiers.
// Elle est donc la plus prudente de toutes.
void StartupScreen::supprimerProjet() {
    const auto* sel = page_->selected();
    if (!sel) {
        page_->status().setTransientMessage("Choisis d'abord un projet dans la liste.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    const start::ProjectEntry entree = *sel;

    // UN EXPORT N'EST PAS UN PROJET. Une ligne [export] designe un .XPG qui est
    // le fichier de quelqu'un d'autre, souvent sur un bureau ou une cle : le
    // retirer de la liste est une chose, le supprimer du disque en est une
    // autre, et ce bouton ne fait que la premiere. (Un chemin disparu non plus.)
    if (!entree.isFolder) {
        app_.forgetPath(entree.path);
        reload();
        page_->status().setMessage(entree.missing ? std::string("Retir\xC3\xA9 de la liste. Rien n'a \xC3\xA9t\xC3\xA9 touch\xC3\xA9 sur le disque.")
                                                  : std::string("Retir\xC3\xA9 de la liste. Le fichier lui-m\xC3\xAAme n'a pas \xC3\xA9t\xC3\xA9 touch\xC3\xA9 : "
                                                                "ce n'est pas un projet, c'est un export."),
                                   StatusBar::Severity::Success);
        return;
    }
    if (!claimDialog()) return;

    // Le projet ouvert ne se supprime pas sous ses propres pieds.
    if (isOpenProject(entree)) {
        app_.menus().ShowDialog(std::make_unique<MessageDialog>(
                                    "Projet ouvert",
                                    "Ce projet est ouvert en ce moment.\n\n"
                                    "Pour le supprimer, ouvre d'abord un autre projet, reviens ici, puis recommence. Supprimer un "
                                    "dossier pendant qu'on y travaille laisse l'application avec des chemins qui ne m\xC3\xA8nent plus nulle part.",
                                    MessageDialog::Icon::Warning),
                                [](const menu::DialogResult&) {});
        return;
    }

    // CE QUI VA DISPARAITRE, COMPTE AVANT DE LE DEMANDER. "Supprimer ce
    // projet ?" ne dit pas s'il y a trois fichiers ou six mois de travail.
    // Sans exception : un sous-dossier illisible arrete le compte, pas l'ecran.
    std::uintmax_t fichiers = 0, octets = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(entree.path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            ++fichiers;
            const auto size = it->file_size(fileEc);
            if (!fileEc) octets += size;
        }
        it.increment(ec);
    }

    std::string message = entree.name.empty() ? entree.path : entree.name;
    message += "\n" + entree.path + "\n\n";
    message += std::to_string(fichiers) + " fichier(s), " + sizeText(octets) + ".\n\n";
    // L'ETAT COMPTE. Un projet FINISH ou LOCK a ete valide par quelqu'un ; le
    // supprimer d'un clic parce qu'il etait selectionne serait trop facile.
    if (entree.state == project::State::Finish || entree.state == project::State::Lock)
        message += "CE PROJET EST MARQU\xC3\x89 " + std::string(entree.state == project::State::Lock ? "LOCK" : "FINISH") + " : il a \xC3\xA9t\xC3\xA9 valid\xC3\xA9. ";
    message += "Le dossier et tout ce qu'il contient - ses versions comprises - seront effac\xC3\xA9s du disque. "
               "Il n'y a pas de corbeille et pas de retour en arri\xC3\xA8re.";
    if (entree.autostart) message += "\n\nLe PC le lance \xC3\xA0 son d\xC3\xA9marrage : ce lancement sera retir\xC3\xA9.";

    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer d\xC3\xA9" "finitivement ?", message, MessageDialog::Icon::Warning, "Supprimer"),
        [this, entree](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            std::error_code ec2;
            const auto n = fs::remove_all(entree.path, ec2);
            if (ec2) {
                app_.menus().ShowDialog(std::make_unique<MessageDialog>(
                                            "Suppression impossible",
                                            entree.path + "\n\n" + ec2.message()
                                                + "\n\nRien n'a peut-\xC3\xAAtre \xC3\xA9t\xC3\xA9 effac\xC3\xA9, ou seulement une partie : v\xC3\xA9rifie le dossier avant de recommencer.",
                                            MessageDialog::Icon::Error),
                                        [](const menu::DialogResult&) {});
                reload();
                return;
            }
            app_.forgetPath(entree.path);
            std::string done = std::to_string(n) + " \xC3\xA9l\xC3\xA9ment(s) supprim\xC3\xA9(s) : " + entree.name + " n'existe plus.";
            if (entree.autostart) {
                // Un poste lance au demarrage sur un dossier disparu : le PC
                // afficherait une erreur a chaque ouverture de session.
                std::string where, why;
                done += station::setAutostart({}, station::currentExecutable(), entree.path, false, &where, &why)
                            ? " Le PC ne le lance plus \xC3\xA0 son d\xC3\xA9marrage."
                            : " Le lancement au d\xC3\xA9marrage du PC reste \xC3\xA0 retirer : " + why;
            }
            reload();
            page_->status().setMessage(done, StatusBar::Severity::Success);
        });
}

// ------------------------------------------------------ les liens et le theme ----
// "Decouvrir l'application" : la visite "Decouvrir l'API" (14 etapes, 5 min) de
// l'onglet API . Didacticiel. Elle se joue SUR UN PROJET OUVERT : celui qui l'est
// deja (on y retourne), sinon celui qui est choisi ici (il s'ouvre), sinon le
// premier qui s'ouvrira - l'ecran d'analyse prend la demande des qu'un projet
// est la (MainAnalysisScreen::takePendingApiTutorial).
void StartupScreen::showTutorial() {
    if (!claimDialog()) return;
    MainAnalysisScreen::requestApiTutorial("api-decouvrir");
    if (app_.project()) {
        page_->status().setMessage("Le didacticiel s'ouvre sur le projet ouvert\xE2\x80\xA6", StatusBar::Severity::Info);
        app_.menus().SwitchMenu("analysis");
        return;
    }
    const auto* e = page_->selected();
    if (e && e->isFolder && !e->missing) {
        page_->status().setMessage("Le didacticiel s'ouvre sur " + e->name + "\xE2\x80\xA6", StatusBar::Severity::Info);
        confirmReplace([this, path = e->path] { openPath(path); });
        return;
    }
    page_->status().setTransientMessage("Le didacticiel se joue sur un projet : cr\xC3\xA9" "e-en un (Ctrl+N) ou ouvre-en un, il d\xC3\xA9marre avec lui.",
                                        10.0, StatusBar::Severity::Info);
}

// LES REGLAGES. Les ecrans "settings" enregistres sont des ebauches (une ligne
// de texte, des cases sans effet) ; le reglage qui vit, c'est le theme : sa
// galerie, avec l'ecran en miniature dans chaque theme. Le jour ou un vrai
// ecran de reglages existera, ce lien y menera.
void StartupScreen::showSettings() {
    if (!claimDialog()) return;
    app_.showThemeGallery();
}

void StartupScreen::chooseTheme(const std::string& key) {
    if (key.empty() || key == app_.theme().name) return;
    app_.setTheme(key);
    // Ecrit tout de suite, comme "Garder" dans la galerie des themes : le
    // message dit vrai meme si le programme ne se ferme pas normalement.
    (void)app_.settings().save();
    page_->status().setTransientMessage("Th\xC3\xA8me " + ui::Theme::labelOf(key) + " : retenu pour les prochaines ouvertures.", 5.0,
                                        StatusBar::Severity::Info);
}

// ------------------------------------------------------- scripts et tests ----
bool StartupScreen::selectProject(const std::string& pathOrName) {
    if (!page_) return false;
    const auto& all = page_->entries();
    const start::ProjectEntry* found = nullptr;
    for (const auto& e : all)
        if (e.path == pathOrName) found = &e;
    if (!found)
        for (const auto& e : all)
            if (!found && samePath(e.path, pathOrName)) found = &e;
    if (!found) {
        const auto want = start::foldText(pathOrName);
        for (const auto& e : all)
            if (!found && start::foldText(e.name) == want) found = &e;
    }
    if (!found) return false;
    const std::string path = found->path;
    if (page_->select(path)) return true;
    // Cache par la recherche ou le filtre : on remontre tout.
    page_->setQuery({});
    page_->setFilter(start::Filter::All);
    return page_->select(path);
}

std::string StartupScreen::selectedPath() const {
    const auto* e = page_ ? page_->selected() : nullptr;
    return e ? e->path : std::string{};
}

std::vector<std::string> StartupScreen::visibleProjects() const {
    std::vector<std::string> names;
    if (!page_) return names;
    for (const auto* e : page_->visibleEntries()) names.push_back(e->name);
    return names;
}

void StartupScreen::setSearch(const std::string& text) {
    if (page_) page_->setQuery(text);
}

bool StartupScreen::chooseFilter(const std::string& labelPrefix) {
    const auto want = start::foldText(labelPrefix);
    if (!page_ || want.empty()) return false;
    for (std::size_t f = 0; f < start::kFilterCount; ++f) {
        const auto filter = static_cast<start::Filter>(f);
        if (start::foldText(start::filterLabel(filter)).rfind(want, 0) == 0) {
            page_->setFilter(filter);
            return true;
        }
    }
    return false;
}

bool StartupScreen::chooseSort(const std::string& labelPrefix) {
    const auto want = start::foldText(labelPrefix);
    if (!page_ || want.empty()) return false;
    const auto& items = page_->sortBox().items();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (start::foldText(items[i].label).rfind(want, 0) == 0) {
            page_->setSort(static_cast<start::Sort>(i));
            return true;
        }
    return false;
}

} // namespace app
