// =============================================================================
//  license_admin/gui/AdminGuiMain.cpp - 1.33.0 : XPG-License-Manager-Admin.exe,
//  LA FENETRE de l'application de l'editeur (jamais livree a un client)
// -----------------------------------------------------------------------------
//  SDL3, le meme rendu et les memes widgets que les Studios (ReportWindow.cpp en
//  est le modele) : une fenetre 1400 x 900 (1100 x 700 au moins), la coquille
//  (Shell.hpp) sur le MenuManager, le theme clair. L'explorateur de fichiers du
//  systeme (SDL_ShowFileDialogWithProperties : la reponse peut venir d'un autre
//  fil, elle est rangee sous un verrou et rendue ici, sur celui de l'interface).
//  Les fichiers glisses sur la fenetre (un geste : DROP_BEGIN ... DROP_COMPLETE)
//  vont ensemble a la coquille (Shell::dropFiles).
//
//  OPTIONS
//    --dossier D         le dossier du poste Admin (defaut : celui de la cle,
//                        %LOCALAPPDATA%\XPG\LicenseAdmin, comme la console)
//    --page ID           ouvrir sur cette page (tableau, licences, securite...)
//    --version           << XPG License Manager Admin 1.0.0 >>, puis rien d'autre
//    FICHIER             1.40.0 : un fichier ouvert d'un double-clic (l'installateur associe ses types) -
//                        une demande (.xpgreq) ouvre l'assistant, une licence, un registre, une liste de
//                        revocation (.xpglic, .xpgreg, .xpgrev) sont ranges dans l'archive (comme glisses)
//    --taille LxH        la taille de la fenetre (les captures)
//  Les captures des livraisons (Wine) :
//    --assistant F.xpgreq   (repetable) ouvrir l'assistant de licence avec ces demandes
//    --cliquer ID           (repetable) cliquer ce widget du menu du dessus
//    --taper ID=TEXTE       (repetable) ecrire dans ce champ du menu du dessus
//    --choisir ID=N         (repetable) choisir la ligne N (1 : la premiere) de ce tableau ou de
//                           cette liste deroulante du menu du dessus
//    --attendre S           (repetable) 1.42.0 : une pause de S secondes avant l'action suivante
//    --glisser F            (repetable) glisser ce fichier sur la fenetre (plusieurs --glisser
//                           qui se suivent : un seul geste)
//                           (les actions dans l'ordre de la ligne de commande, une toutes les 0,3 s)
//    --capture F.png        dessiner, enregistrer l'image, quitter ;
//    --apres S              ... S secondes apres la derniere action (defaut 1)
// =============================================================================
#include "Page.hpp"
#include "Shell.hpp"

#include "../../core/SystemOpen.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../platform/Renderer.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/DatePicker.hpp"
#include "../../platform/FontAtlas.hpp"   // 1.41.0 : la police de l'interface
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/GlobeLists.hpp"   // 1.47.0 : --choisir une ville du globe

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

// stb_image_write (domaine public, third_party/) : la capture en PNG. Statique : aucun symbole partage avec une
// autre copie liee ailleurs.
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_DEPRECATION
#include "../../../third_party/stb_image_write.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace app {
// platform/SdlEventPump.cpp (comme ReportWindow.cpp et App.cpp).
std::optional<ui::InputEvent> translate(const SDL_Event& e);
}

namespace {

namespace gui = license_admin::gui;

// ---- l'explorateur de fichiers du systeme : SDL rappelle, peut-etre sur un autre fil ----
namespace filepick {
struct Request {
    std::function<void(std::string)> done;
    std::vector<std::string>          names, patterns;
    std::vector<SDL_DialogFileFilter> filters;   // lus par SDL jusqu'au rappel : vivent dans la demande
    std::string                       start, title;
};
std::mutex                                    g_mutex;
std::vector<std::pair<Request*, std::string>> g_done;

void SDLCALL onPicked(void* userdata, const char* const* files, int /*filter*/) {
    std::string chosen;
    if (files && files[0]) chosen = files[0];
    std::lock_guard<std::mutex> lock(g_mutex);
    g_done.emplace_back(static_cast<Request*>(userdata), std::move(chosen));
}

// Sur le fil de l'interface, a chaque image.
void deliver() {
    std::vector<std::pair<Request*, std::string>> ready;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ready.swap(g_done);
    }
    for (auto& [request, path] : ready) {
        std::unique_ptr<Request> owned(request);
        if (owned->done) owned->done(std::move(path));
    }
}

bool open(SDL_Window* window, const ui::FilePick& pick, std::function<void(std::string)> done) {
    auto request = std::make_unique<Request>();
    request->done = std::move(done);
    request->start = pick.start;
    request->title = pick.title;
    for (const auto& [name, pattern] : pick.filters) {
        request->names.push_back(name);
        request->patterns.push_back(pattern);
    }
    for (std::size_t i = 0; i < request->names.size(); ++i)
        request->filters.push_back(SDL_DialogFileFilter{request->names[i].c_str(), request->patterns[i].c_str()});
    auto* raw = request.release();
    const SDL_FileDialogType type = pick.folder ? SDL_FILEDIALOG_OPENFOLDER : pick.save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE;
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    if (!pick.folder && !raw->filters.empty()) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, raw->filters.data());
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(raw->filters.size()));
    }
    if (!raw->start.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, raw->start.c_str());
    if (!raw->title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, raw->title.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    SDL_ShowFileDialogWithProperties(type, &onPicked, raw, props);
    SDL_DestroyProperties(props);
    return true;
}
} // namespace filepick

// ---- la ligne de commande ----
struct Action {
    enum class Kind : std::uint8_t { Click, Type, Drop, Choose, Wait } kind{Kind::Click};   // 1.42.0 : Wait (--attendre S)
    std::string id, text;   // Drop : `id` est le fichier
};

struct Options {
    std::string              folder, page, capture;
    std::string              connexion;   // 1.47.0 : --connexion F (la connexion au service signee par l'editeur)
    double                   after{1.0};
    int                      width{1400}, height{900};
    bool                     version{false}, help{false};
    std::vector<std::string> requests;
    std::vector<std::string> open;   // 1.40.0 : les fichiers ouverts d'un double-clic (ranges dans l'archive)
    std::vector<Action>      actions;
};

// Les arguments en UTF-8 (sous Windows, argv est dans la page de code du systeme : un dossier accentue s'y perd).
std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> out;
#if defined(_WIN32)
    int n = 0;
    if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 0; i < n; ++i) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? static_cast<std::size_t>(len) : 0, '\0');
            if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
            if (!s.empty() && s.back() == '\0') s.pop_back();
            out.push_back(std::move(s));
        }
        LocalFree(w);
        return out;
    }
#endif
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

void usage(std::FILE* to) {
    std::fprintf(to,
                 "%s %s (l'outil de l'\xC3\xA9" "diteur : jamais livr\xC3\xA9 \xC3\xA0 un client)\n"
                 "  --dossier D           le dossier du poste Admin (d\xC3\xA9" "faut : celui de la cl\xC3\xA9, comme XPG-License-Admin)\n"
                 "  --page ID             ouvrir sur cette page (tableau, clients, licences, editions, fonctionnalites,\n"
                 "                        options, activations, demandes, flottantes, boutique, commandes, revocations, admins,\n"
                 "                        droits, journal, versions, service, historique, securite)\n"
                 "  --version             le nom et la version\n"
                 "  --connexion F.xpgsvc  la connexion au service (le certificat sign\xC3\xA9 par l'\xC3\xA9" "diteur) : l'adresse de ce poste\n"
                 "                        s'il n'en a pas (sinon : \xC3\xA0 c\xC3\xB4t\xC3\xA9 du programme, puis le dossier des licences du Studio)\n"
                 "  FICHIER               une demande (.xpgreq) : l'assistant ; .xpglic, .xpgreg, .xpgrev : rang\xC3\xA9s dans l'archive\n"
                 "  --taille LxH          la taille de la fen\xC3\xAAtre\n"
                 "  --assistant F.xpgreq  ouvrir l'assistant de licence avec cette demande (r\xC3\xA9p\xC3\xA9table)\n"
                 "  --cliquer ID          cliquer ce widget (r\xC3\xA9p\xC3\xA9table, dans l'ordre)\n"
                 "  --taper ID=TEXTE      \xC3\xA9" "crire dans ce champ (r\xC3\xA9p\xC3\xA9table, dans l'ordre)\n"
                 "  --choisir ID=N        choisir la ligne N (1 : la premi\xC3\xA8re) de ce tableau ou de cette liste (r\xC3\xA9p\xC3\xA9table)\n"
                 "  --glisser F           glisser ce fichier sur la fen\xC3\xAAtre (r\xC3\xA9p\xC3\xA9table ; ceux qui se suivent : un geste)\n"
                 "  --capture F.png       enregistrer l'image de la fen\xC3\xAAtre, puis quitter\n"
                 "  --apres S             la capture S secondes apr\xC3\xA8s la derni\xC3\xA8re action (d\xC3\xA9" "faut 1)\n",
                 gui::kAppName, gui::kAppVersion);
}

bool parse(const std::vector<std::string>& args, Options& o, std::string& why) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool more = i + 1 < args.size();
        const auto need = [&](const char* what) {
            why = a + " : il manque " + what;
            return false;
        };
        if (a == "--version") o.version = true;
        else if (a == "--aide" || a == "--help" || a == "-h" || a == "/?") o.help = true;
        else if (a == "--dossier") {
            if (!more) return need("le dossier");
            o.folder = args[++i];
        } else if (a == "--connexion") {   // 1.47.0 : connexion-service.xpgsvc (ServiceConnection.hpp)
            if (!more) return need("le fichier de la connexion (.xpgsvc)");
            o.connexion = args[++i];
        } else if (a == "--page") {
            if (!more) return need("la page");
            o.page = args[++i];
        } else if (a == "--capture") {
            if (!more) return need("le fichier PNG");
            o.capture = args[++i];
        } else if (a == "--apres") {
            if (!more) return need("les secondes");
            o.after = std::max(0.0, std::atof(args[++i].c_str()));
        } else if (a == "--taille") {
            if (!more) return need("LxH");
            const std::string t = args[++i];
            const auto x = t.find_first_of("xX");
            const int w = x == std::string::npos ? 0 : std::atoi(t.substr(0, x).c_str());
            const int h = x == std::string::npos ? 0 : std::atoi(t.substr(x + 1).c_str());
            if (w < 320 || h < 240) {
                why = "--taille : " + t + " (attendu : LxH, par exemple 1400x900)";
                return false;
            }
            o.width = w;
            o.height = h;
        } else if (a == "--assistant") {
            if (!more) return need("la demande (.xpgreq)");
            o.requests.push_back(args[++i]);
        } else if (a == "--cliquer") {
            if (!more) return need("l'identifiant");
            o.actions.push_back({Action::Kind::Click, args[++i], {}});
        } else if (a == "--taper") {
            if (!more) return need("ID=TEXTE");
            const std::string t = args[++i];
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0) {
                why = "--taper : " + t + " (attendu : ID=TEXTE)";
                return false;
            }
            o.actions.push_back({Action::Kind::Type, t.substr(0, eq), t.substr(eq + 1)});
        } else if (a == "--choisir") {
            if (!more) return need("ID=N");
            const std::string t = args[++i];
            const auto eq = t.find('=');
            const int  n = eq == std::string::npos ? 0 : std::atoi(t.substr(eq + 1).c_str());
            if (eq == std::string::npos || eq == 0 || n < 1) {
                why = "--choisir : " + t + " (attendu : ID=N, N \xE2\x89\xA5 1)";
                return false;
            }
            // 1.47.0 : ID=1,2,3 - plusieurs lignes d'un tableau (Demandes d'admin : << Accepter 3 demandes... >>)
            o.actions.push_back({Action::Kind::Choose, t.substr(0, eq), t.find(',', eq) == std::string::npos ? std::to_string(n) : t.substr(eq + 1)});
        } else if (a == "--attendre") {   // 1.42.0 : une pause entre deux actions (un releve a l'ouverture...)
            if (!more) return need("les secondes");
            const std::string t = args[++i];
            const double sec = std::atof(t.c_str());
            if (!(sec > 0.0) || sec > 600.0) {
                why = "--attendre : " + t + " (attendu : des secondes, de 0,1 \xC3\xA0 600)";
                return false;
            }
            o.actions.push_back({Action::Kind::Wait, {}, t});
        } else if (a == "--glisser") {
            if (!more) return need("le fichier");
            o.actions.push_back({Action::Kind::Drop, args[++i], {}});
        } else if (!a.empty() && a[0] != '-') {
            // 1.40.0 : un fichier ouvert d'un double-clic (l'installateur associe .xpgreq, .xpglic, .xpgreg, .xpgrev) :
            // une demande va a l'assistant, le reste est range comme s'il etait glisse sur la fenetre.
            std::string ext = a.size() >= 7 ? a.substr(a.size() - 7) : std::string();
            for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ext == ".xpgreq") o.requests.push_back(a);
            else if (ext == ".xpglic" || ext == ".xpgreg" || ext == ".xpgrev") o.open.push_back(a);
            else {
                why = "fichier non pris en charge : " + a + " (attendu : .xpgreq, .xpglic, .xpgreg ou .xpgrev)";
                return false;
            }
        } else {
            why = "option inconnue : " + a;
            return false;
        }
    }
    return true;
}

// Une action des captures, sur le menu du dessus (le dialogue ouvert, sinon la coquille).
void perform(menu::MenuManager& menus, const Action& a) {
    menu::IMenu* top = menus.top();
    ui::Widget*  root = top ? top->widgetRoot() : nullptr;
    ui::Widget*  w = root ? root->findById(a.id) : nullptr;
    const std::string where = top ? top->id() : std::string("(aucun menu)");
    if (!w) {
        std::fprintf(stderr, "attention : \xC2\xAB %s \xC2\xBB introuvable dans %s\n", a.id.c_str(), where.c_str());
        return;
    }
    if (a.kind == Action::Kind::Type) {
        if (auto* t = dynamic_cast<ui::InputText*>(w)) t->setText(a.text);
        else if (auto* m = dynamic_cast<ui::MultiLineText*>(w)) m->setText(a.text);
        else std::fprintf(stderr, "attention : \xC2\xAB %s \xC2\xBB n'est pas un champ (%s)\n", a.id.c_str(), where.c_str());
        return;
    }
    if (a.kind == Action::Kind::Choose) {
        const int row = std::atoi(a.text.c_str()) - 1;
        std::vector<ui::RowIndex> rows;   // 1.47.0 : << 1,2,3 >> - plusieurs lignes
        for (std::size_t p = 0; p < a.text.size();) {
            const std::size_t c = std::min(a.text.find(',', p), a.text.size());
            if (const int n = std::atoi(a.text.substr(p, c - p).c_str()); n >= 1) rows.push_back(static_cast<ui::RowIndex>(n - 1));
            p = c + 1;
        }
        if (auto* t = dynamic_cast<ui::TableView*>(w)) t->selectModelRows(rows, true);
        else if (auto* d = dynamic_cast<ui::DropDown*>(w)) d->setSelectedIndex(row);
        else if (auto* g = dynamic_cast<ui::GlobeCityList*>(w); g && row >= 0 && row < static_cast<int>(g->order().size()))
            g->placeChosen->emit(g->order()[static_cast<std::size_t>(row)]);   // 1.47.0 : une ville de la liste du globe (sa carte s'ouvre)
        else std::fprintf(stderr, "attention : \xC2\xAB %s \xC2\xBB n'est ni un tableau ni une liste (%s)\n", a.id.c_str(), where.c_str());
        return;
    }
    if (!w->enabled()) std::fprintf(stderr, "attention : \xC2\xAB %s \xC2\xBB est gris\xC3\xA9 (%s) : cliqu\xC3\xA9 quand m\xC3\xAAme\n", a.id.c_str(), where.c_str());
    if (auto* b = dynamic_cast<ui::Button*>(w)) b->clicked->emit();
    else if (auto* c = dynamic_cast<ui::Checkbox*>(w)) c->setState(c->isChecked() ? ui::Checkbox::State::Unchecked : ui::Checkbox::State::Checked);
    else if (auto* r = dynamic_cast<ui::RadioButton*>(w)) r->select();
    else if (auto* tb = dynamic_cast<ui::ToggleButton*>(w)) tb->setChecked(!tb->checked());
    else if (auto* dp = dynamic_cast<ui::DatePicker*>(w)) dp->isOpen() ? dp->close() : dp->open();   // 1.42.0 : le mini calendrier
    else std::fprintf(stderr, "attention : \xC2\xAB %s \xC2\xBB ne se clique pas (%s)\n", a.id.c_str(), where.c_str());
}

// L'image de la fenetre en PNG (le rendu relit ses pixels avant de les montrer).
bool savePng(gfx::IRenderer& renderer, const std::string& path, std::string& why) {
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!renderer.readPixels(rgba, w, h) || w <= 0 || h <= 0) {
        why = "ce rendu ne sait pas relire son image";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;   // opaque
    std::vector<std::uint8_t> png;
    const auto sink = [](void* context, void* data, int size) {
        auto* v = static_cast<std::vector<std::uint8_t>*>(context);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        v->insert(v->end(), bytes, bytes + size);
    };
    if (!stbi_write_png_to_func(sink, &png, w, h, 4, rgba.data(), w * 4) || png.empty()) {
        why = "PNG impossible";
        return false;
    }
    // Un chemin UTF-8 (Windows compris) : par std::filesystem, pas par fopen.
    std::FILE* f = nullptr;
#if defined(_WIN32)
    f = _wfopen(p.wstring().c_str(), L"wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) {
        why = "impossible d'\xC3\xA9" "crire " + path;
        return false;
    }
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    if (!ok) why = "\xC3\xA9" "criture incompl\xC3\xA8te : " + path;
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    core::hardenLibrarySearch();   // aucune DLL du dossier courant ni du PATH
    Options o;
    std::string why;
    if (!parse(utf8Args(argc, argv), o, why)) {
        std::fprintf(stderr, "%s\n\n", why.c_str());
        usage(stderr);
        return 2;
    }
    if (o.version) {
        std::printf("%s %s\n", gui::kAppName, gui::kAppVersion);
        return 0;
    }
    if (o.help) {
        usage(stdout);
        return 0;
    }
    if (!o.page.empty() && !gui::Shell::entry(o.page)) {
        std::fprintf(stderr, "--page : page inconnue \xC2\xAB %s \xC2\xBB\n\n", o.page.c_str());
        usage(stderr);
        return 2;
    }

#if defined(_WIN32)
    // L'icone de l'executable (xpg_license_admin.rc, ressource 1) : la barre de titre et celle des taches.
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s : %s\n", gui::kAppName, SDL_GetError());
        return 2;
    }
    // 1.41.0 : LE NOUVEAU DESIGN - le theme XPG, en violet (la couleur de l'Admin), et la police Inter fournie a cote
    // de l'exe (resources\fonts).
    ui::Theme::setProductAccent(ui::Theme::ProductAccent::Violet);
    // Le texte de l'Admin un cran plus petit que celui des Studios (la maquette : 13,5 a 14 px) - plus d'air.
    const ui::Theme theme = [] {
        ui::Theme t = ui::Theme::byName("XPG");
        t.font.ui = gfx::FontId{14};
        t.font.uiBold = gfx::semibold(gfx::FontId{14});
        t.font.mono = gfx::FontId{14};
        t.font.smallUi = gfx::FontId{12};
        t.metric.rowHeight = 28.f;
        t.metric.headerHeight = 34.f;
        return t;
    }();
    if (const char* base = SDL_GetBasePath(); base && *base) {
        std::error_code ec;
        const std::string b(base);
        const auto fonts = std::filesystem::path(std::u8string(b.begin(), b.end())) / "resources" / "fonts";
        if (std::filesystem::is_directory(fonts, ec)) {
            const auto u8 = fonts.u8string();
            gfx::setInterfaceFontFolder(std::string(u8.begin(), u8.end()));
        }
    }
    SDL_Window* window = SDL_CreateWindow(gui::kAppName, o.width, o.height, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "%s : %s\n", gui::kAppName, SDL_GetError());
        SDL_Quit();
        return 2;
    }
    (void)SDL_SetWindowMinimumSize(window, std::min(1100, o.width), std::min(700, o.height));
    auto created = gfx::SdlRenderer::create(window);
    if (!created) {
        std::fprintf(stderr, "%s : %s\n", gui::kAppName, created.error().message().c_str());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    auto renderer = std::move(*created);
    SDL_StartTextInput(window);

    ui::PlatformServices services;
    services.measureWidth = [r = renderer.get()](std::string_view text, gfx::FontId font) { return r->measure(text, font).width; };
    services.lineHeight = [r = renderer.get()](gfx::FontId font) { return r->lineHeight(font); };
    services.clipboardGet = [] {
        char* text = SDL_GetClipboardText();
        std::string out = text ? text : "";
        SDL_free(text);
        return out;
    };
    services.clipboardSet = [](std::string_view text) { SDL_SetClipboardText(std::string(text).c_str()); };
    services.surfaceSize = [r = renderer.get()] { return r->surfaceSize(); };
    services.pickFile = [window](const ui::FilePick& pick, std::function<void(std::string)> done) { return filepick::open(window, pick, std::move(done)); };
    ui::installPlatformServices(std::move(services));

    // 1.47.0 : LA CONNEXION AU SERVICE QUI VOYAGE AVEC L'INSTALLATEUR (docs/ADMINS.md, par. 11) - un poste sans adresse
    // (et sans la cle de l'editeur) la prend, signee par l'editeur : --connexion, a cote du programme, le Studio de ce PC.
    license_admin::connexion::setPlaces(license_admin::connexion::defaultPlaces(license_admin::connexion::programFolder(), o.connexion));
    {
        menu::MenuManager menus{menu::MenuFactory{}};
        auto owned = std::make_unique<gui::Shell>(o.folder);
        gui::Shell* shell = owned.get();
        if (!o.page.empty()) shell->navigate(o.page);
        menus.PushMenu(std::move(owned));
        menus.applyPending();
        if (!o.open.empty()) shell->dropFiles(o.open);   // 1.40.0 : les fichiers ouverts d'un double-clic
        if (!o.requests.empty()) {
            shell->openDialog(gui::makeLicenceWizard(shell->host(), o.requests));
            menus.applyPending();
        }

        std::vector<std::string> drops;   // un geste : DROP_BEGIN ... DROP_COMPLETE
        double lastDrop = -1.0;
        const auto deliverDrops = [&] {
            if (drops.empty()) return;
            std::vector<std::string> paths;
            paths.swap(drops);
            // A la page montree, meme sous un dialogue : elle les donne a l'assistant ouvert (des demandes), ou la
            // coquille les range dans l'archive.
            shell->dropFiles(paths);
        };

        using Clock = std::chrono::steady_clock;
        auto        last = Clock::now();
        double      time = 0.0;
        bool        quit = false, captured = false;
        std::size_t nextAction = 0;
        double      nextActionAt = 0.8;   // apres les premieres images (la mise en page, les polices)
        double      captureAt = o.actions.empty() ? o.after : -1.0;
        while (!quit) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                    case SDL_EVENT_QUIT:
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: quit = true; continue;
                    case SDL_EVENT_DROP_BEGIN: drops.clear(); continue;
                    case SDL_EVENT_DROP_FILE:
                        if (e.drop.data && *e.drop.data) drops.emplace_back(e.drop.data);
                        lastDrop = time;
                        continue;
                    case SDL_EVENT_DROP_COMPLETE: deliverDrops(); continue;
                    case SDL_EVENT_DROP_TEXT:
                    case SDL_EVENT_DROP_POSITION: continue;
                    default: break;
                }
                if (auto ev = app::translate(e)) (void)menus.HandleEvent(*ev);
            }
            // Un systeme qui ne dit pas la fin du geste : ce qui est arrive ensemble part ensemble.
            if (!drops.empty() && time - lastDrop > 0.3) deliverDrops();
            filepick::deliver();
            // Le travail lent suivi meme sous un dialogue (la coquille n'a pas Update alors).
            (void)shell->poll();

            const auto now = Clock::now();
            const double dt = std::chrono::duration<double>(now - last).count();
            last = now;
            time += dt;
            menus.applyPending();
            const menu::FrameContext fc{dt, time, &theme, renderer->surfaceSize(), renderer->dpiScale()};
            menus.Update(fc);
            // 1.47.0 : LA FENETRE REDUITE (ou cachee) - rien a montrer : pas de dessin (le globe de la page Statistiques ne
            // tourne pas pour rien) et pas de boucle a vide ; on attend un evenement, un dixieme de seconde au plus (le
            // regard, les travaux lents continuent par Update). Comme les fenetres detachees des Studios.
            if (o.capture.empty() && (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0) {
                (void)SDL_WaitEventTimeout(nullptr, 100);
                continue;
            }
            renderer->beginFrame(theme.color.windowBg);
            menus.Render(*renderer, fc);
            double pause = 0.0;   // 1.42.0 : --attendre
            if (nextAction < o.actions.size() && time >= nextActionAt) {
                if (o.actions[nextAction].kind == Action::Kind::Drop) {
                    // Les --glisser qui se suivent : un seul geste, comme dans l'Explorateur.
                    while (nextAction < o.actions.size() && o.actions[nextAction].kind == Action::Kind::Drop) drops.push_back(o.actions[nextAction++].id);
                    deliverDrops();
                } else if (o.actions[nextAction].kind == Action::Kind::Wait) {
                    pause = std::atof(o.actions[nextAction++].text.c_str());
                } else {
                    perform(menus, o.actions[nextAction++]);
                }
                nextActionAt = time + (pause > 0.0 ? pause : 0.3);
                if (nextAction == o.actions.size()) captureAt = nextActionAt - (pause > 0.0 ? 0.0 : 0.3) + o.after;
                pause = 0.0;
            }
            if (!o.capture.empty() && !captured && captureAt >= 0.0 && time >= captureAt) {
                captured = true;
                const menu::IMenu* top = menus.top();
                std::fprintf(stderr, "capture : page \xC2\xAB %s \xC2\xBB (%s)%s%s\n", shell->currentPageId().c_str(), shell->pageTitle().c_str(),
                             top && top != shell ? ", dialogue : " : "", top && top != shell ? (top->id() + " \xC2\xAB " + top->title() + " \xC2\xBB").c_str() : "");
                std::string err;
                if (savePng(*renderer, o.capture, err)) std::fprintf(stderr, "capture : %s\n", o.capture.c_str());
                else std::fprintf(stderr, "capture impossible : %s\n", err.c_str());
                quit = true;
            }
            renderer->endFrame();
            if (menus.empty()) quit = true;
        }
    }
    renderer.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
