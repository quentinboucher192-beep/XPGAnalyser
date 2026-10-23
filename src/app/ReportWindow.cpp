// =============================================================================
//  app/ReportWindow.cpp - 1.12.28 : la fenetre de rapport a part
// =============================================================================
#include "ReportWindow.hpp"

#include "Brand.hpp"
#include "Capture.hpp"
#include "CrashDialogs.hpp"
#include "Settings.hpp"
#include "../core/CrashGuard.hpp"
#include "../core/Edition.hpp"
#include "../hmi/HmiMedia.hpp"
#include "../menu/MenuManager.hpp"
#include "../platform/Renderer.hpp"
#include "../ui/Theme.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/CrashReport.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#  define XPG_HAVE_SDL3 1
#else
#  define XPG_HAVE_SDL3 0
#endif

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#if XPG_HAVE_SDL3
namespace app {
// platform/SdlEventPump.cpp (comme App.cpp).
std::optional<ui::InputEvent> translate(const SDL_Event& e);
}
#endif

namespace app::reportwindow {

bool parse(int argc, char** argv, Args& out) {
    bool asked = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--fenetre-rapport" && more) { asked = true; out.kind = argv[++i]; }
        else if (a == "--rapport" && more) out.report = argv[++i];
        else if (a == "--pid" && more) out.pid = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--secondes" && more) out.seconds = std::atof(argv[++i]);
        else if (a == "--capture" && more) out.capture = argv[++i];
        else if (a == "--capture-apres" && more) out.captureAfter = std::atof(argv[++i]);
    }
    return asked;
}

#if XPG_HAVE_SDL3
namespace {

// L'icone de la fenetre : le logo, comme la fenetre principale (App.cpp).
void giveIcon(SDL_Window* window) {
    hmi::Rgba img;
    if (!brand::logoPixels(64, img)) return;
    if (SDL_Surface* s = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32, img.pixels.data(), img.width * 4)) {
        (void)SDL_SetWindowIcon(window, s);
        SDL_DestroySurface(s);
    }
}

#if defined(_WIN32)
std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
#endif

} // namespace
#endif

int run(const Args& args) {
#if XPG_HAVE_SDL3
    auto report = core::crash::readReport(args.report);
    if (!report) {
        std::fprintf(stderr, "fen\xC3\xAAtre de rapport : le rapport ne se lit pas (%s)\n", args.report.c_str());
        return 2;
    }
    // Vu : le lancement suivant ne le remontre pas.
    core::crash::markShown(report->path);
    const bool hang = args.kind == "blocage";

#if defined(_WIN32)
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "fen\xC3\xAAtre de rapport : %s\n", SDL_GetError());
        return 2;
    }
    // Le theme choisi dans l'appli (ses reglages, lus sans rien y ecrire).
    Settings settings;
    (void)settings.load(Settings::defaultPath());
    const ui::Theme theme = ui::Theme::byName(settings.getString("view.theme", "Light"));

    bool quit = false;
    ui::CrashReportSetup setup = crashdialogs::setupFor(*report, hang ? ui::crashreport::Mode::Hang : ui::crashreport::Mode::Crash);
    setup.stalledSeconds = args.seconds;
    setup.close = [&quit] { quit = true; };
    setup.wait = [&quit] { quit = true; };       // l'appli continue ; la fenetre s'en va
    ui::CrashReportView* view = nullptr;
#if defined(_WIN32)
    HANDLE process = args.pid ? OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(args.pid)) : nullptr;
    HANDLE recovered = nullptr;
    if (hang && args.pid) recovered = OpenEventW(SYNCHRONIZE, FALSE, wide(core::crash::recoveryEventName(args.pid)).c_str());
    setup.closeApp = [process] {
        if (process) (void)TerminateProcess(process, 4);   // la boucle de la fenetre verra la fin
    };
    if (!hang) {
        setup.restart = [&quit] {
            wchar_t exe[MAX_PATH * 2];
            if (GetModuleFileNameW(nullptr, exe, MAX_PATH * 2)) {
                std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
                STARTUPINFOW si{};
                si.cb = sizeof si;
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
            quit = true;
        };
    }
#endif

    // La fenetre : a la taille du contenu, toujours devant (l'appli bloquee est derriere).
    const std::string title = (hang ? core::productName() + " ne r\xC3\xA9pond plus" : core::productName() + " s'est arr\xC3\xAAt\xC3\xA9");
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 760, 520,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALWAYS_ON_TOP);
    if (!window) {
        std::fprintf(stderr, "fen\xC3\xAAtre de rapport : %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    (void)SDL_SetWindowMinimumSize(window, 560, 420);
    giveIcon(window);
    auto created = gfx::SdlRenderer::create(window);
    if (!created) {
        std::fprintf(stderr, "fen\xC3\xAAtre de rapport : %s\n", created.error().message().c_str());
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
    ui::installPlatformServices(std::move(services));

    menu::MenuManager menus{menu::MenuFactory{}};
    auto screen = std::make_unique<ui::CrashReportScreen>(std::move(setup));
    auto* raw = screen.get();
    menus.PushMenu(std::move(screen));
    menus.applyPending();
    view = raw->view();

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    double time = 0;
    bool captured = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                quit = true;      // la croix : comme Attendre (blocage), Fermer (plantage)
                continue;
            }
            if (auto ev = translate(e)) (void)menus.HandleEvent(*ev);
        }
        const auto now = Clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        time += dt;
#if defined(_WIN32)
        // L'appli bloquee : sa boucle repart, ou elle se ferme.
        if (view && hang && !view->recovered() && !view->gone()) {
            if (recovered && WaitForSingleObject(recovered, 0) == WAIT_OBJECT_0) view->setRecovered(args.seconds + time);
            else if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) view->setGone();
        }
#endif
        if (view) {
            const std::string shown = view->headline();
            if (shown != SDL_GetWindowTitle(window)) (void)SDL_SetWindowTitle(window, shown.c_str());
        }
        menus.applyPending();
        const menu::FrameContext fc{dt, time, &theme, renderer->surfaceSize(), renderer->dpiScale()};
        menus.Update(fc);
        renderer->beginFrame(theme.color.windowBg);
        menus.Render(*renderer, fc);
        if (!args.capture.empty() && !captured && time >= args.captureAfter) {
            captured = true;
            if (auto ok = captureToPng(*renderer, args.capture); ok) std::fprintf(stderr, "capture : %s\n", args.capture.c_str());
            else std::fprintf(stderr, "capture impossible : %s\n", ok.error().message().c_str());
            quit = true;
        }
        renderer->endFrame();
        if (menus.empty()) quit = true;
    }
#if defined(_WIN32)
    if (process) CloseHandle(process);
    if (recovered) CloseHandle(recovered);
#endif
    renderer.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
#else
    (void)args;
    std::fprintf(stderr, "fen\xC3\xAAtre de rapport : cet ex\xC3\xA9" "cutable est sans SDL\n");
    return 1;
#endif
}

} // namespace app::reportwindow
