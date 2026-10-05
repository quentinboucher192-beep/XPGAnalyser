// =============================================================================
//  app/FileExplorerHost.cpp - lot API 8 : l'explorateur de fichiers de l'appli
// =============================================================================
#include "FileExplorerHost.hpp"

#include "App.hpp"
#include "FilePreview.hpp"   // Lot API 8 : l'explorateur, 2e partie
#include "Settings.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/FileExplorerModel.hpp"
#include "../ui/widgets/FileExplorer.hpp"

namespace app::explorer {

    namespace {
        constexpr const char* kSystemKey = "explorateur.systeme";
        constexpr const char* kMemoryKey = "explorateur.memoire";
        bool                                  g_showNext = false;
        std::shared_ptr<ui::files::Memory>    g_memory;
    } // namespace

    bool systemPreferred(const Settings& s) { return s.getBool(kSystemKey, false); }

    void setSystemPreferred(Settings& s, bool on) { s.set(kSystemKey, on); }

    void showNextInScript() { g_showNext = true; }

    bool wantsAppExplorer(App& app) {
        if (g_showNext) {
            g_showNext = false;
            return true;
        }
        // Une session rejouee : comme avant (ses reponses sont deja rangees).
        if (app.scripted()) return false;
        return !systemPreferred(app.settings());
    }

    std::shared_ptr<ui::files::Memory> memory(App& app) {
        if (!g_memory) {
            g_memory = std::make_shared<ui::files::Memory>();
            g_memory->load(app.settings().getList(kMemoryKey));
        }
        return g_memory;
    }

    bool open(App& app, const ui::FilePick& pick, std::function<void(std::string)> done) {
        ui::FileExplorerContext ctx;
        ctx.projectDir = app.projectFolder();
        for (const auto& src : app.sourcePaths()) {
            if (ui::files::extensionOf(src) == "xpg") {
                ctx.sourceXpg = src;
                break;
            }
        }
        ctx.memory = memory(app);
        ctx.memoryChanged = [&app] {
            auto& s = app.settings();
            s.setList(kMemoryKey, memory(app)->save());
            (void)s.save();
        };
        // ---- Lot API 8 : l'explorateur, 2e partie (l'apercu, les vignettes, le lien du bas) ----
        preview::ProjectFacts facts;
        facts.sourceXpg = ctx.sourceXpg;
        if (const auto project = app.project()) facts.exportedAt = project->header.exportedAt;
        ctx.preview = [facts](const std::string& path) { return preview::describe(path, facts); };
        ctx.thumbnail = [](const std::string& path, int maxSide) { return preview::thumbnail(path, maxSide); };
        ctx.openExternally = [](const std::string& path) { return preview::openWithSystem(path); };
        ctx.useSystemExplorer = [&app] {
            setSystemPreferred(app.settings(), true);
            (void)app.settings().save();
        };
        // ---- fin Lot API 8 : l'explorateur, 2e partie ----
        app.menus().ShowDialog(std::make_unique<ui::FileExplorerDialog>(pick, std::move(ctx), std::move(done)), {});
        return true;
    }

    ui::FileExplorerDialog* current(App& app) { return dynamic_cast<ui::FileExplorerDialog*>(app.menus().top()); }

} // namespace app::explorer
