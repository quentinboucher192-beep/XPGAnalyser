// =============================================================================
//  tests/settings_test.cpp — workspace persistence
// -----------------------------------------------------------------------------
//  Two bugs are pinned here, both of which produced the same symptom: a window
//  where only the first panel was visible.
//
//  1. LOAD/SAVE RE-ENTRANCY. Restoring panel 0 fired its change handler, which
//     saved *every* panel from its current widget state - and panels 1..n had
//     not been restored yet, so they were written as false. The next iteration
//     read that back. Reading all values before touching a widget fixes it.
//
//  2. LOCALE. std::stof and std::ostringstream follow the global C locale, and
//     SDL calls setlocale() at start-up. On a French system a splitter ratio was
//     written as "0,2200" and, since the list separator is a comma, came back as
//     twice as many meaningless numbers. std::to_chars/from_chars are
//     locale-independent by specification.
// =============================================================================
#include "../src/app/Settings.hpp"

#include <cassert>
#include <clocale>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace app;

namespace {

std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

int main() {
    // --- 1. the ordering bug, reproduced then shown fixed -------------------
    {
        const char* keys[] = {"panel.a", "panel.b", "panel.c", "panel.d"};

        // The old shape: read a key, then immediately persist every key.
        {
            Settings s;
            s.load(tempPath("xpg-order-broken.txt"));
            std::vector<bool> shown(4, false);
            for (int i = 0; i < 4; ++i) {
                shown[i] = s.getBool(keys[i], true);
                for (int j = 0; j < 4; ++j) s.set(keys[j], shown[j]);   // the callback
            }
            assert(shown[0] && "the first panel always survived");
            assert(!shown[1] && !shown[2] && !shown[3]
                   && "reproduces the reported symptom: every later panel collapses");
        }

        // The fix: snapshot every value before applying any of them.
        {
            Settings s;
            s.load(tempPath("xpg-order-fixed.txt"));
            std::vector<bool> wanted;
            for (const auto* k : keys) wanted.push_back(s.getBool(k, true));
            std::vector<bool> shown = wanted;
            for (int j = 0; j < 4; ++j) s.set(keys[j], shown[j]);
            for (bool v : shown) assert(v && "every panel must restore");
        }
    }

    // --- 2. floats must round-trip whatever the locale ----------------------
    {
        // Ask for a comma-decimal locale; if the image does not have it the
        // test still runs, it just proves less.
        const char* got = std::setlocale(LC_ALL, "fr_FR.UTF-8");
        std::printf("locale in effect: %s\n", got ? got : "(C, fr_FR unavailable)");

        const std::string path = tempPath("xpg-locale.txt");
        const std::vector<float> ratios{0.22f, 0.48f, 0.30f};

        Settings out;
        out.load(path);
        out.setFloats("layout.upper", ratios);
        out.set("zoom", 1.25f);
        assert(out.save());

        Settings in;
        assert(in.load(path));
        const auto back = in.getFloats("layout.upper");
        std::printf("ratios read back: %zu values\n", back.size());
        assert(back.size() == ratios.size()
               && "a comma decimal separator would split each ratio into two");
        for (std::size_t i = 0; i < ratios.size(); ++i) {
            const float d = back[i] - ratios[i];
            assert(d < 0.001f && d > -0.001f);
        }
        const float zoom = in.getFloat("zoom", 0.f);
        assert(zoom > 1.24f && zoom < 1.26f);

        std::setlocale(LC_ALL, "C");
    }

    // --- 3. lists, and unknown keys surviving a round trip -------------------
    {
        const std::string path = tempPath("xpg-list.txt");
        {
            Settings s;
            s.load(path);
            s.setList("recent.projects", {"C:\\proj\\MAST.XPG", "D:\\a,b\\CONFIG.XHW"});
            s.set("future.setting", std::string("kept"));
            assert(s.save());
        }
        Settings s;
        assert(s.load(path));
        const auto recent = s.getList("recent.projects");
        assert(recent.size() == 2 && "a path containing a comma must not split");
        assert(recent[1] == "D:\\a,b\\CONFIG.XHW");
        assert(s.getString("future.setting") == "kept");
    }

    std::printf("\nsettings_test: all assertions passed\n");
    return 0;
}
