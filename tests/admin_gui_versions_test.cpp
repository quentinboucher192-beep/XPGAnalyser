// =============================================================================
//  tests/admin_gui_versions_test.cpp - 1.44.0 : LES VERSIONS, L'ENVOI DE L'ADMIN,
//  LA SAUVEGARDE DE SECOURS ET LES ADRESSES DE SECOURS a l'ecran
// -----------------------------------------------------------------------------
//  A blanc (TextRenderer, HostRig) sur un poste Admin d'essai (cle d'ESSAI,
//  registre d'exemple) :
//   - Versions : la page, << Publier une version... >> (deux petits .exe PE
//     fabriques ici, glisses sur la page : leur version lue, << elles
//     concordent >> ; deux versions differentes : dit) ;
//   - Parametres de securite : la sauvegarde de secours (le dossier, la phrase
//     deux fois, masquee, jamais montree ni gardee ; << Activer >> : reglee et
//     faite) ; restaurer une sauvegarde complete dans un poste neuf ;
//   - Service en ligne : une adresse de secours ajoutee, enregistree (le registre
//     signe de nouveau).
//  1.46.0 : << Annuler >> passait sur le coin des notes et << je ne peux pas ecrire
//  dans Notes >> - aucune place ou un bouton chevauche les notes (la taille du client,
//  1373 x 917, puis de 1100 x 700 a 1700 x 1050) ; les notes s'ecrivent au clavier
//  (un clic, du texte, Entree : la note suivante) et c'est ce texte qui est publie.
//  Puis contre le VRAI service local (sans Node ni wrangler : SAUTE) : publier la
//  version (la barre, le tableau << Versions publiees >>, les notes tapees), et << Ajouter un admin >>
//  : << Par un lien >> (l'installateur de l'Admin publie, un lien d'une fois copie).
//  Aucune donnee reelle : des fichiers et des noms d'essai.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"
#include "../third_party/miniz.h"   // 1.46.1 : des .zip d'essai (Versions)

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace admintest;

namespace {

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string draw(ui::Widget& w, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
std::string drawMenu(menu::IMenu& m) {
    TextRenderer r;
    return paintMenu(m, r, 1400.f, 900.f);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}
bool enabledBtn(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b && b->enabled() && b->visible();
}
bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }

// 1.46.0 : deux rectangles qui se chevauchent (un pixel au moins).
bool overlap(const gfx::Rect& a, const gfx::Rect& b) {
    return a.w > 0.f && a.h > 0.f && b.w > 0.f && b.h > 0.f && a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f &&
           b.y < a.bottom() - 0.5f;
}
std::string rectText(const gfx::Rect& r) {
    char t[96];
    std::snprintf(t, sizeof t, "%.0f,%.0f %.0fx%.0f", static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h));
    return t;
}
// La page Versions, le panneau ouvert, a la taille d'une fenetre de l'Admin (le menu de gauche : 232 ; la barre du haut :
// 64 ; le bandeau du bas : 40) : rien ne chevauche les notes, les boutons restent dans la colonne de droite, et << ce
// qu'ils verront >> (dessine) ne passe pas sous les boutons. Vide : tout va bien ; sinon, ce qui ne va pas.
std::string panelProblems(ui::Widget& page, float winW, float winH) {
    const float pw = winW - 232.f, ph = winH - 64.f - 40.f;
    TextRenderer r;
    (void)paint(page, r, pw, ph);
    auto* notes = find<ui::MultiLineText>(page, "versions.notes");
    auto* cancel = find<ui::Button>(page, "versions.annuler");
    auto* sign = find<ui::Button>(page, "versions.signer");
    auto* imp = page.findById("versions.importante");
    auto* pickS = find<ui::Button>(page, "versions.studios.choisir");
    auto* pickA = find<ui::Button>(page, "versions.admin.choisir");
    if (!notes || !cancel || !sign || !imp || !pickS || !pickA) return "un widget du panneau manque";
    if (!notes->visible() || !cancel->visible() || !sign->visible()) return "le panneau n'est pas ouvert";
    const gfx::Rect n = notes->bounds(), c = cancel->bounds(), g = sign->bounds(), i = imp->bounds();
    const std::string at = std::to_string(static_cast<int>(winW)) + "x" + std::to_string(static_cast<int>(winH)) + " : ";
    if (overlap(n, c)) return at + "Annuler (" + rectText(c) + ") sur les notes (" + rectText(n) + ")";
    if (overlap(n, g)) return at + "Signer et publier (" + rectText(g) + ") sur les notes (" + rectText(n) + ")";
    if (overlap(c, g)) return at + "Annuler sur Signer et publier";
    if (overlap(i, c) || overlap(i, g)) return at + "Importante sous un bouton";
    if (overlap(n, pickS->bounds()) || overlap(n, pickA->bounds())) return at + "un Choisir... sur les notes";
    if (c.x < n.right() + 20.f || g.x < n.right() + 20.f) return at + "un bouton a gauche de la colonne de droite (notes jusqu'a " + std::to_string(static_cast<int>(n.right())) + ")";
    if (c.right() > pw + 0.5f || g.right() > pw + 0.5f) return at + "un bouton hors de la page";
    if (n.h < 60.f || n.w < 200.f) return at + "les notes trop petites (" + rectText(n) + ")";
    const float buttonsTop = std::min(c.y, g.y);
    for (const auto& d : r.draws) {
        if (d.text == "CE QU'ILS VERRONT" && d.at.y + 20.f + 58.f > buttonsTop) return at + "<< ce qu'ils verront >> sous les boutons";
        // 1.47.0 : seulement dans le panneau (au-dessus de son bas, 18 px sous Signer et publier) - la barre des versions
        // publiees, plus bas, a aussi une puce << Importante >>.
        if (d.text == "Importante" && d.at.y < g.bottom() + 18.f && d.at.y + 34.f > buttonsTop) return at + "Importante sous les boutons";
    }
    // Les etapes (<< ce qui va se passer >>, deux lignes au plus chacune) au-dessus du trait d'Importante (14 px plus haut).
    const float rule = i.y - 14.f;
    for (const auto& d : r.draws)
        if (d.at.x > n.right() + 20.f && d.at.x < pw && d.at.y > i.y - 230.f && d.at.y < i.y - 2.5f && d.at.y + 13.f > rule)
            return at + "une etape (<< " + d.text + " >>) sur le trait d'Importante";
    return {};
}
// Ecrire dans les notes comme au clavier : un clic dedans (le clavier), puis du texte, Entree entre deux lignes.
void typeNotes(ui::Widget& page, const std::vector<std::string>& lines) {
    auto* notes = find<ui::MultiLineText>(page, "versions.notes");
    if (!notes) return;
    const gfx::Rect n = notes->bounds();
    (void)page.dispatch(ui::MouseDown{{n.x + n.w * 0.5f, n.y + 12.f}, ui::MouseButton::Left, 1, {}});
    (void)page.dispatch(ui::MouseUp{{n.x + n.w * 0.5f, n.y + 12.f}, ui::MouseButton::Left, {}});
    for (std::size_t k = 0; k < lines.size(); ++k) {
        if (k > 0) (void)page.dispatch(ui::KeyDown{ui::Key::Return, {}, false});
        (void)page.dispatch(ui::TextInput{lines[k]});
    }
}
std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(rien)"); }

// ---- un petit .exe (PE32) avec une ressource VS_VERSIONINFO (le meme que tests/admins_fichiers_test.cpp) ----
void put16(std::string& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<char>(v & 0xFF);
    b[o + 1] = static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& b, std::size_t o, std::uint32_t v) {
    put16(b, o, v & 0xFFFF);
    put16(b, o + 2, v >> 16);
}
std::string u16(const std::string& ascii) {
    std::string out;
    for (char c : ascii) {
        out += c;
        out += '\0';
    }
    out += std::string(2, '\0');
    return out;
}
void pad4(std::string& b) {
    while (b.size() % 4) b += '\0';
}
std::string verNode(const std::string& key, const std::string& value, std::uint16_t valueLen, std::uint16_t type, const std::vector<std::string>& children) {
    std::string b(6, '\0');
    b += u16(key);
    pad4(b);
    b += value;
    for (const auto& ch : children) {
        pad4(b);
        b += ch;
    }
    put16(b, 0, static_cast<std::uint32_t>(b.size()));
    put16(b, 2, valueLen);
    put16(b, 4, type);
    return b;
}
std::string strNode(const std::string& key, const std::string& value) {
    return verNode(key, u16(value), static_cast<std::uint16_t>(value.size() + 1), 1, {});
}
std::string buildPe(const std::array<std::uint16_t, 4>& fixed, const std::string& product, const std::string& overlay) {
    std::string fixedInfo(52, '\0');
    put32(fixedInfo, 0, 0xFEEF04BDu);
    put32(fixedInfo, 4, 0x00010000u);
    put32(fixedInfo, 8, (static_cast<std::uint32_t>(fixed[0]) << 16) | fixed[1]);
    put32(fixedInfo, 12, (static_cast<std::uint32_t>(fixed[2]) << 16) | fixed[3]);
    put32(fixedInfo, 16, (static_cast<std::uint32_t>(fixed[0]) << 16) | fixed[1]);
    put32(fixedInfo, 20, (static_cast<std::uint32_t>(fixed[2]) << 16) | fixed[3]);
    put32(fixedInfo, 24, 0x3F);
    put32(fixedInfo, 32, 0x40004);
    put32(fixedInfo, 36, 1);
    std::vector<std::string> strings;
    strings.push_back(strNode("CompanyName", "Editeur d'essai"));
    strings.push_back(strNode("ProductName", "Installateur d'essai"));
    strings.push_back(strNode("ProductVersion", product));
    std::vector<std::string> children;
    children.push_back(verNode("StringFileInfo", "", 0, 1, {verNode("040904b0", "", 0, 1, strings)}));
    std::string translation(4, '\0');
    put16(translation, 0, 0x0409);
    put16(translation, 2, 0x04b0);
    children.push_back(verNode("VarFileInfo", "", 0, 1, {verNode("Translation", translation, 4, 0, {})}));
    const std::string vi = verNode("VS_VERSION_INFO", fixedInfo, 52, 0, children);
    std::string rsrc(0x58, '\0');
    put16(rsrc, 0x0E, 1);
    put32(rsrc, 0x10, 16);
    put32(rsrc, 0x14, 0x80000000u | 0x18);
    put16(rsrc, 0x18 + 0x0E, 1);
    put32(rsrc, 0x28, 1);
    put32(rsrc, 0x2C, 0x80000000u | 0x30);
    put16(rsrc, 0x30 + 0x0E, 1);
    put32(rsrc, 0x40, 0x409);
    put32(rsrc, 0x44, 0x48);
    put32(rsrc, 0x48, 0x1000 + 0x58);
    put32(rsrc, 0x4C, static_cast<std::uint32_t>(vi.size()));
    rsrc += vi;
    const std::size_t rawSize = (rsrc.size() + 0x1FF) & ~static_cast<std::size_t>(0x1FF);
    rsrc.resize(rawSize, '\0');
    std::string pe(0x200, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    put32(pe, 0x3C, 0x40);
    pe.replace(0x40, 4, std::string("PE\0\0", 4));
    put16(pe, 0x44, 0x014C);
    put16(pe, 0x46, 1);
    put16(pe, 0x54, 224);
    put16(pe, 0x56, 0x0102);
    const std::size_t opt = 0x58;
    put16(pe, opt, 0x10B);
    put32(pe, opt + 28, 0x400000);
    put32(pe, opt + 32, 0x1000);
    put32(pe, opt + 36, 0x200);
    put32(pe, opt + 56, 0x1000 + static_cast<std::uint32_t>((rawSize + 0xFFF) & ~static_cast<std::size_t>(0xFFF)));
    put32(pe, opt + 60, 0x200);
    put16(pe, opt + 68, 2);
    put32(pe, opt + 92, 16);
    put32(pe, opt + 96 + 16, 0x1000);
    put32(pe, opt + 96 + 20, 0x58 + static_cast<std::uint32_t>(vi.size()));
    const std::size_t sec = opt + 224;
    pe.replace(sec, 8, std::string(".rsrc\0\0\0", 8));
    put32(pe, sec + 8, static_cast<std::uint32_t>(0x58 + vi.size()));
    put32(pe, sec + 12, 0x1000);
    put32(pe, sec + 16, static_cast<std::uint32_t>(rawSize));
    put32(pe, sec + 20, 0x200);
    put32(pe, sec + 36, 0x40000040u);
    return pe + rsrc + overlay;
}
// 1.46.1 : un .zip d'essai - chaque entree (son nom dans le zip, ses octets) ; vrai : ecrit.
bool writeZip(const fs::path& zip, const std::vector<std::pair<std::string, std::string>>& entries) {
    std::error_code ec;
    fs::create_directories(zip.parent_path(), ec);
    fs::remove(zip, ec);
    mz_zip_archive z{};
    if (!mz_zip_writer_init_heap(&z, 0, 0)) return false;
    bool ok = true;
    for (const auto& [name, bytes] : entries)
        ok = ok && mz_zip_writer_add_mem(&z, name.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED);
    void* buf = nullptr;
    size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
    if (ok) writeFile(zip, std::string(static_cast<const char*>(buf), size));
    mz_zip_writer_end(&z);
    return ok;
}
// Change le repertoire central d'un zip : `at` octets apres la signature de la premiere entree (0x02014b50), `n` octets.
void patchCentral(const fs::path& zip, std::size_t at, std::uint32_t value, int n) {
    std::ifstream in(zip, std::ios::binary);
    std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::size_t k = b.find(std::string("PK\x01\x02", 4));
    if (k == std::string::npos) return;
    for (int i = 0; i < n; ++i) b[k + at + static_cast<std::size_t>(i)] = static_cast<char>((value >> (8 * i)) & 0xFF);
    writeFile(zip, b);
}

std::string randomBytes(std::size_t n, std::mt19937_64& rng) {
    std::string s(n, '\0');
    for (auto& c : s) c = static_cast<char>(rng() & 0xFF);
    return s;
}

const std::string kPhrase = "trois chevaux bleus traversent la riviere gelee";

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_versions_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();
    std::mt19937_64 rng(1443);

    Fixture fx(source, "versions-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    const fs::path studios = fx.root / "installateurs" / "API-IHM-Studio-Setup-1.44.1.exe";
    // 1.49.0 : trois installateurs - XPG Admin (XPG-Admin-Setup, sorte << admin >>), l'Admin+ (XPG-License-Admin-Setup-...-EDITEUR)
    const fs::path adminExe = fx.root / "installateurs" / "XPG-Admin-Setup-1.44.1.exe";
    const fs::path plusExe = fx.root / "installateurs" / "XPG-License-Admin-Setup-1.44.1-EDITEUR.exe";
    const fs::path adminOther = fx.root / "autres" / "XPG-Admin-Setup-1.44.2.exe";
    writeFile(studios, buildPe({1, 44, 1, 0}, "1.44.1", randomBytes(300000, rng)));
    writeFile(adminExe, buildPe({1, 44, 1, 0}, "1.44.1", randomBytes(120000, rng)));
    writeFile(plusExe, buildPe({1, 44, 1, 0}, "1.44.1", randomBytes(90000, rng)));
    writeFile(adminOther, buildPe({1, 44, 2, 0}, "1.44.2", randomBytes(1000, rng)));
    HostRig rig(fx);

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Versions : la page, publier (sans le service)\n");
    auto vp = gui::makeVersionsPage(rig.host());
    {
        std::string t = draw(*vp);
        check(has(t, "Versions") && has(t, "Versions publi\xC3\xA9" "es") && has(t, "seul l'admin+ publie") && has(t, "n'est pas r\xC3\xA9gl\xC3\xA9"),
              "la page : les versions publiees ; sans service, c'est dit");
        check(click(*vp, "versions.publier") && shown(*vp, "versions.studios.choisir") && shown(*vp, "versions.admin.choisir") && shown(*vp, "versions.adminplus.choisir"),
              "Publier une version... : le panneau (1.49.0 : trois emplacements)");
        t = draw(*vp);
        check(has(t, "LES STUDIOS") && has(t, "L'ADMIN+") && has(t, "L'ADMIN (XPG ADMIN)") && has(t, "Pour l'admin+ seul") && has(t, "Ce qui va se passer") &&
                  has(t, "Importante"),
              "les trois installateurs (Studios, Admin+, Admin), ce qui va se passer, Importante");
        check(vp->dropFiles({U(studios), U(adminOther)}), "deux installateurs glisses sur la page");
        t = draw(*vp);
        check(has(t, "version 1.44.1 lue dans le fichier") && has(t, "version 1.44.2 lue dans le fichier") && has(t, "pas de la m\xC3\xAA" "me version"),
              "deux versions differentes : dit");
        check(!enabledBtn(*vp, "versions.signer"), "Signer et publier : grise");
        rig.picks.push_back(U(adminExe));
        check(click(*vp, "versions.admin.choisir"), "Choisir... l'installateur de XPG Admin");
        t = draw(*vp);
        check(has(t, "elles concordent") && has(t, "La premi\xC3\xA8re version publi\xC3\xA9" "e") && has(t, "Signer et publier la 1.44.1"), "elles concordent : Signer et publier la 1.44.1");
        check(!enabledBtn(*vp, "versions.signer"), "... grise tant que le service n'est pas regle");
        auto* notes = find<ui::MultiLineText>(*vp, "versions.notes");
        check(notes != nullptr && !notes->readOnly(), "le champ des notes : on peut y ecrire (il naissait en lecture seule)");
        if (!notes) return finish();
        // 1.46.0 : << Annuler >> ne passe plus sur les notes - la taille du client, celles demandees, puis toute une plage.
        {
            std::string bad = panelProblems(*vp, 1373.f, 917.f);
            check(bad.empty(), "1373 x 917 (la fenetre du client) : rien sur les notes, les boutons dans leur colonne " + bad);
            bad = panelProblems(*vp, 1280.f, 800.f);
            check(bad.empty(), "1280 x 800 : idem, et << ce qu'ils verront >> pas sous les boutons " + bad);
            bad = panelProblems(*vp, 1600.f, 1000.f);
            check(bad.empty(), "1600 x 1000 : idem " + bad);
            std::string first;
            int sizes = 0;
            for (float w = 1100.f; w <= 1700.f; w += 25.f)
                for (float h = 700.f; h <= 1050.f; h += 50.f) {
                    ++sizes;
                    if (first.empty()) first = panelProblems(*vp, w, h);
                }
            check(first.empty(), "de 1100 x 700 a 1700 x 1050 (" + std::to_string(sizes) + " tailles) : aucun chevauchement " + first);
            (void)panelProblems(*vp, 1373.f, 917.f);
            const gfx::Rect c = find<ui::Button>(*vp, "versions.annuler")->bounds(), n = notes->bounds();
            const gfx::Rect g = find<ui::Button>(*vp, "versions.signer")->bounds();
            check(std::abs(c.y - g.y) < 0.5f, "1373 x 917 : Annuler et Signer et publier cote a cote (" + rectText(c) + " / " + rectText(g) + ")");
            (void)panelProblems(*vp, 1100.f, 700.f);
            const gfx::Rect c2 = find<ui::Button>(*vp, "versions.annuler")->bounds(), g2 = find<ui::Button>(*vp, "versions.signer")->bounds();
            check(c2.bottom() <= g2.y && std::abs(c2.x - g2.x) < 0.5f, "1100 x 700 (la plus petite) : une colonne etroite - Annuler au-dessus de Signer et publier");
            // Le coin des notes (la ou Annuler etait) : un clic y donne le clavier aux notes, il n'annule rien.
            (void)panelProblems(*vp, 1373.f, 917.f);
            const gfx::Rect nn = notes->bounds();
            (void)vp->dispatch(ui::MouseDown{{nn.right() - 6.f, nn.bottom() - 6.f}, ui::MouseButton::Left, 1, {}});
            (void)vp->dispatch(ui::MouseUp{{nn.right() - 6.f, nn.bottom() - 6.f}, ui::MouseButton::Left, {}});
            check(notes->focused() && shown(*vp, "versions.annuler") && shown(*vp, "versions.studios.choisir") && has(draw(*vp, 1141.f, 813.f), "elles concordent"),
                  "un clic dans le coin bas droit des notes : les notes ont le clavier, le panneau reste ouvert");
            (void)n;
        }
        // 1.46.0 : << je ne peux pas ecrire dans Notes >> - au clavier : du texte, Entree, une seconde ligne.
        notes->setText({});
        typeNotes(*vp, {"Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es.", "Admin : le journal se trie par criticit\xC3\xA9."});
        check(notes->text() == "Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es.\nAdmin : le journal se trie par criticit\xC3\xA9.",
              "tape au clavier (Entree : la note suivante) : << " + notes->text() + " >>");
        t = draw(*vp, 1141.f, 813.f);
        check(has(t, "Exports") && has(t, "journal"), "les deux notes se dessinent dans le champ");
    }

    // ------------------------------------------------------------------------------------------------
    // 1.46.1 : << si j'envoie un zip avec le .exe dedans, il extrait automatiquement le .exe >> - l'installateur attendu
    // seul, dans un dossier temporaire ; refuses : un chemin qui sort, chiffre, une taille absurde, plusieurs.
    std::printf("-- 1.46.1 : Versions accepte un .zip (l'installateur en est extrait)\n");
    {
        const std::string pe146 = buildPe({1, 46, 0, 0}, "1.46.0", randomBytes(200000, rng));
        const std::string adm146 = buildPe({1, 46, 0, 0}, "1.46.0", randomBytes(90000, rng));
        const fs::path zips = fx.root / "zips";
        const fs::path zs = zips / "API-IHM-Studio-Setup-1.46.0.zip", za = zips / "XPG-License-Admin-Setup-1.46.0-EDITEUR.zip", zb = zips / "les-deux.zip";
        const fs::path zx = zips / "XPG-Admin-Setup-1.46.0.zip", z3 = zips / "les-trois.zip";   // 1.49.0
        check(writeZip(zs, {{"LISEZ-MOI.txt", "un fichier a cote"}, {"API-IHM-Studio-Setup-1.46.0.exe", pe146}}) &&
                  writeZip(za, {{"editeur/XPG-License-Admin-Setup-1.46.0-EDITEUR.exe", adm146}}) && writeZip(zx, {{"XPG-Admin-Setup-1.46.0.exe", adm146}}) &&
                  writeZip(z3, {{"API-IHM-Studio-Setup-1.46.0.exe", pe146}, {"XPG-License-Admin-Setup-1.46.0-EDITEUR.exe", adm146}, {"XPG-Admin-Setup-1.46.0.exe", adm146}}) &&
                  writeZip(zb, {{"API-IHM-Studio-Setup-1.46.0.exe", pe146}, {"XPG-License-Admin-Setup-1.46.0-EDITEUR.exe", adm146}}),
              "des .zip d'essai");
        std::string out, why;
        check(gui::Workspace::installerFromZip(U(zs), false, out, &why) && fs::path(out).filename() == "API-IHM-Studio-Setup-1.46.0.exe" &&
                  gui::Workspace::readInstallerVersion(out) == "1.46.0" && fs::file_size(out) == pe146.size() &&
                  fs::path(out).parent_path().filename().string().rfind("xpg-versions-zip-", 0) == 0,
              "extrait : l'installateur des Studios seul, dans un dossier temporaire, sa version lue (" + why + ")");
        std::error_code ec;
        fs::remove_all(fs::path(out).parent_path(), ec);
        check(!gui::Workspace::installerFromZip(U(zs), true, out, &why) && has(why, "XPG-License-Admin-Setup"), "pas d'installateur de l'Admin dans ce zip : dit (" + why + ")");
        bool st = false, ad = false;
        check(gui::Workspace::installerKindsInZip(U(zb), st, ad, &why) && st && ad, "les deux sortes dans un zip");
        bool s3 = false, p3 = false, a3 = false;
        check(gui::Workspace::installerKindsInZip(U(z3), s3, p3, a3, &why) && s3 && p3 && a3, "1.49.0 : les trois sortes dans un zip (Studios, Admin+, XPG Admin)");
        check(gui::Workspace::installerKindsInZip(U(zb), s3, p3, a3, &why) && s3 && p3 && !a3, "1.49.0 : le zip d'avant : pas de XPG Admin");
        check(!gui::Workspace::installerFromZip(U(za), 3, out, &why) && has(why, "XPG-Admin-Setup"), "1.49.0 : XPG Admin absent du zip de l'Admin+ : dit (" + why + ")");
        const fs::path z2 = zips / "deux.zip", zt = zips / "sortie.zip", zc = zips / "chiffre.zip", zg = zips / "enorme.zip", zn = zips / "pas-un-zip.zip";
        (void)writeZip(z2, {{"API-IHM-Studio-Setup-1.46.0.exe", pe146}, {"vieux/API-IHM-Studio-Setup-1.45.0.exe", pe146}});
        check(!gui::Workspace::installerFromZip(U(z2), false, out, &why) && has(why, "plusieurs"), "deux installateurs des Studios : refuse (" + why + ")");
        (void)writeZip(zt, {{"../API-IHM-Studio-Setup-1.46.0.exe", pe146}});
        check(!gui::Workspace::installerFromZip(U(zt), false, out, &why) && has(why, "sort du dossier"), "un chemin qui sort du dossier (..) : refuse (" + why + ")");
        (void)writeZip(zc, {{"API-IHM-Studio-Setup-1.46.0.exe", pe146}});
        patchCentral(zc, 8, 1, 2);   // le drapeau << chiffre >> (bit 0)
        check(!gui::Workspace::installerFromZip(U(zc), false, out, &why) && has(why, "chiffr"), "une entree chiffree : refusee (" + why + ")");
        (void)writeZip(zg, {{"API-IHM-Studio-Setup-1.46.0.exe", pe146}});
        patchCentral(zg, 24, 0x80000000u, 4);   // la taille decompressee : 2 Gio
        check(!gui::Workspace::installerFromZip(U(zg), false, out, &why) && has(why, "taille absurde"), "une taille absurde : refusee (" + why + ")");
        writeFile(zn, randomBytes(4000, rng));
        check(!gui::Workspace::installerFromZip(U(zn), false, out, &why) && has(why, "pas un zip"), "pas un zip : refuse (" + why + ")");
        // la page : glisser un zip ; Choisir... un zip
        check(click(*vp, "versions.annuler") && click(*vp, "versions.publier"), "le panneau vide");
        check(vp->dropFiles({U(zs)}), "un .zip glisse sur la page");
        std::string t = draw(*vp);
        check(has(t, "API-IHM-Studio-Setup-1.46.0.exe") && has(t, "version 1.46.0 lue dans le fichier") && has(t, "extrait de API-IHM-Studio-Setup-1.46.0.zip"),
              "l'installateur des Studios extrait du .zip : << extrait de API-IHM-Studio-Setup-1.46.0.zip >>");
        rig.picks.push_back(U(zs));
        check(click(*vp, "versions.admin.choisir"), "Choisir... (l'Admin) : un .zip sans l'installateur de XPG Admin");
        t = draw(*vp);
        check(has(t, "ne porte pas XPG-Admin-Setup"), "l'Admin : ce zip ne le porte pas, dit dans sa zone");
        rig.picks.push_back(U(zx));
        check(click(*vp, "versions.admin.choisir"), "Choisir... (l'Admin) : son .zip");
        rig.picks.push_back(U(za));
        check(click(*vp, "versions.adminplus.choisir"), "1.49.0 : Choisir... (l'Admin+) : son .zip");
        t = draw(*vp);
        check(has(t, "extrait de XPG-Admin-Setup-1.46.0.zip") && has(t, "extrait de XPG-License-Admin-Setup-1.46.0") && has(t, "elles concordent") &&
                  has(t, "Signer et publier la 1.46.0"),
              "les trois extraits : elles concordent, Signer et publier la 1.46.0");
        std::size_t temps = 0;
        for (const auto& e : fs::directory_iterator(fs::temp_directory_path(), ec))
            if (e.path().filename().string().rfind("xpg-versions-zip-", 0) == 0) ++temps;
        check(click(*vp, "versions.annuler"), "Annuler");
        std::size_t after = 0;
        for (const auto& e : fs::directory_iterator(fs::temp_directory_path(), ec))
            if (e.path().filename().string().rfind("xpg-versions-zip-", 0) == 0) ++after;
        check(temps >= 3 && after + 3 <= temps, "Annuler : les dossiers temporaires des fichiers extraits s'en vont (" + std::to_string(temps) + " -> " + std::to_string(after) + ")");
        check(vp->dropFiles({U(z2)}), "un .zip refuse glisse : pris en charge (dit)");
        check(!rig.flashes.empty() && has(rig.flashes.back(), "plusieurs"), "le refus est dit (" + (rig.flashes.empty() ? std::string() : rig.flashes.back()) + ")");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Service en ligne : une adresse de secours\n");
    {
        auto sp = gui::makeServicePage(rig.host());
        std::string t = draw(*sp, 1100.f, 3000.f);
        check(has(t, "Les adresses de secours") && has(t, "Les Studios 1.43 et avant refusent un registre qui en contient"), "la carte et sa mise en garde");
        const std::uint64_t before = fx.ws->registry().number;
        check(type(*sp, "service.secours.nouvelle", "https://xpg-secours.essai.invalid") && click(*sp, "service.secours.ajouter"), "Ajouter... une adresse");
        t = draw(*sp, 1100.f, 3000.f);
        check(shown(*sp, "service.secours.1") && find<ui::InputText>(*sp, "service.secours.1")->text() == "https://xpg-secours.essai.invalid" &&
                  has(t, "Pas encore enregistr"),
              "dans la liste, pas encore enregistree");
        check(type(*sp, "service.secours.nouvelle", "http://pas-https.invalid") && click(*sp, "service.secours.ajouter") && !lastOk(rig), "http:// : refuse");
        check(enabledBtn(*sp, "service.secours.enregistrer") && click(*sp, "service.secours.enregistrer") && lastOk(rig), "Enregistrer : " + lastText(rig));
        fx.ws->reload();
        const auto f = fx.ws->fallbackServices();
        check(f.size() == 1 && f.front() == "https://xpg-secours.essai.invalid" && fx.ws->registry().number == before + 1, "le registre n. + 1 porte l'adresse de secours");
        sp->refresh();
        check(click(*sp, "service.secours.retirer.1") && click(*sp, "service.secours.enregistrer") && lastOk(rig), "Retirer, enregistrer : " + lastText(rig));
        fx.ws->reload();
        check(fx.ws->fallbackServices().empty(), "plus d'adresse de secours");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Parametres de securite : la sauvegarde de secours\n");
    std::string backupPath;
    {
        auto sp = gui::makeSecurityPage(rig.host());
        std::string t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "Sauvegarde de secours") && has(t, "pas encore r\xC3\xA9gl\xC3\xA9" "e") && has(t, "Restaurer une sauvegarde compl\xC3\xA8te"), "la carte : pas encore reglee");
        auto* p1 = find<ui::InputText>(*sp, "securite.sauvegarde.phrase");
        check(p1 && p1->masked() && find<ui::InputText>(*sp, "securite.sauvegarde.phrase2")->masked(), "la phrase : masquee, deux fois");
        rig.picks.push_back(U(fx.root / "sauvegardes"));
        check(click(*sp, "securite.sauvegarde.choisir") && find<ui::InputText>(*sp, "securite.sauvegarde.dossier")->text() == U(fx.root / "sauvegardes"), "Choisir le dossier...");
        type(*sp, "securite.sauvegarde.phrase", "court");
        t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "Trop faible"), "une phrase trop faible : dit");
        type(*sp, "securite.sauvegarde.phrase", kPhrase);
        type(*sp, "securite.sauvegarde.phrase2", kPhrase);
        t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "Phrase acceptable, la m\xC3\xAA" "me deux fois") && !has(t, kPhrase), "acceptable ; jamais dessinee");
        check(click(*sp, "securite.sauvegarde.activer") && lastOk(rig), "Activer la sauvegarde de chaque jour : " + lastText(rig));
        check(find<ui::InputText>(*sp, "securite.sauvegarde.phrase")->text().empty() && find<ui::InputText>(*sp, "securite.sauvegarde.phrase2")->text().empty(),
              "les champs de la phrase : vides");
        const auto s = fx.ws->backupSettings();
        check(s.configured && s.last > 0 && fs::exists(fs::path(s.lastPath)), "reglee, et la premiere sauvegarde faite");
        backupPath = s.lastPath;
        sp->refresh();
        t = draw(*sp, 1100.f, 3600.f);
        check(has(t, "r\xC3\xA9gl\xC3\xA9" "e : chaque jour, les 30 derni\xC3\xA8res gard\xC3\xA9" "es") && has(t, "Derni\xC3\xA8re"), "l'etat : reglee, la derniere");
        check(click(*sp, "securite.sauvegarde.maintenant") && lastOk(rig) && !rig.last()->output.empty(), "Sauvegarder maintenant : " + lastText(rig));
        check(!fx.ws->backupDueToday(), "celle du jour est faite");
        bool leaked = false;
        for (const auto& r : rig.reports) leaked = leaked || has(r.text(), kPhrase);
        for (const auto& f : rig.flashes) leaked = leaked || has(f, kPhrase);
        check(!leaked && !foundIn(fx.admin, kPhrase), "la phrase : dans aucun message, aucun fichier du poste");
    }
    std::printf("-- restaurer la sauvegarde complete dans un poste neuf\n");
    {
        Fixture neuf(source, "versions-neuf");
        HostRig nrig(neuf);
        auto sp = gui::makeSecurityPage(nrig.host());
        std::string t = draw(*sp, 1100.f, 3600.f);
        check(neuf.ws->role() == gui::Role::None && has(t, "Restaurer une sauvegarde compl\xC3\xA8te") && !has(t, "Sauvegarde de secours (si"),
              "un poste neuf : la restauration, pas la sauvegarde");
        type(*sp, "securite.restauration.phrase", kPhrase);
        nrig.picks.push_back(backupPath);
        const std::size_t d0 = nrig.dialogs.size();
        check(click(*sp, "securite.restauration") && nrig.dialogs.size() == d0 + 1, "le fichier choisi : ce qu'il contient, une confirmation");
        menu::MenuManager menus{menu::MenuFactory{}};   // la confirmation de cette page veut un MenuManager
        if (nrig.dialogs.size() == d0 + 1) {
            nrig.dialogs.back()->attach(&menus);
            const std::string d = drawMenu(*nrig.dialogs.back());
            check(has(d, "Sauvegarde compl\xC3\xA8te du") && has(d, fx.ws->key().id), "la confirmation : la date, la cle de l'editeur");
            check(click(*nrig.dialogs.back()->widgetRoot(), "securite.restauration.confirmation.oui") && lastOk(nrig), "restauree : " + lastText(nrig));
        }
        neuf.ws->reload();
        check(neuf.ws->key().present && neuf.ws->key().id == fx.ws->key().id && neuf.ws->role() == gui::Role::Plus, "la meme cle de l'editeur : l'admin+");
        check(find<ui::InputText>(*sp, "securite.restauration.phrase")->text().empty(), "le champ de la phrase : vide");
    }

    // ------------------------------------------------------------------------------------------------ le vrai service local
    std::printf("-- le service local : publier la version, l'Admin par un lien\n");
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    xpg_test::LocalService svc({fx.ws->signingKey() ? fx.ws->signingKey()->publicKeyHex() : std::string()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    check(svc.ready(), "le service demarre (" + svc.error() + ")");
    if (!svc.ready()) return finish();
    check(fx.ws->serviceConfigure(svc.url()).ok, "l'adresse du service");
    {
        const auto c = fx.ws->serviceCertify({});
        check(c.ok, "la cle du service certifiee (" + c.text() + ")");
        fx.ws->reload();
    }
    {
        vp->refresh();
        check(click(*vp, "versions.relever") && lastOk(rig), "Relever : " + lastText(rig));
        std::string t = draw(*vp);
        check(has(t, "Aucune version publi\xC3\xA9" "e"), "aucune version publiee encore");
        if (!find<ui::Button>(*vp, "versions.studios.choisir")->visible()) (void)click(*vp, "versions.publier");
        (void)vp->dropFiles({U(studios), U(adminExe), U(plusExe)});
        t = draw(*vp);
        check(enabledBtn(*vp, "versions.signer") && has(t, "elles concordent") && has(t, "XPG-License-Admin-Setup-1.44.1-EDITEUR.exe"),
              "Signer et publier la 1.44.1 : possible (les trois glisses ; l'EDITEUR dans l'Admin+)");
        // 1.46.0 : les notes tapees au clavier (une puce tapee aussi : elle s'enleve), c'est ce qui part
        if (auto* notes = find<ui::MultiLineText>(*vp, "versions.notes")) notes->setText({});
        typeNotes(*vp, {"\xE2\x80\xA2 Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es.", "", "Admin : le journal se trie par criticit\xC3\xA9."});
        check(click(*vp, "versions.signer") && lastOk(rig), "publiee : " + lastText(rig));
        {
            gui::VersionsView seen;
            const auto r = fx.ws->versions(seen);
            const std::vector<std::string> want = {"Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es.", "Admin : le journal se trie par criticit\xC3\xA9."};
            check(r.ok && !seen.versions.empty() && seen.versions.front().version == "1.44.1" && seen.versions.front().notes == want,
                  "les notes publiees : celles tapees, une par ligne (" + std::to_string(seen.versions.empty() ? 0 : seen.versions.front().notes.size()) + " notes)");
            std::string kinds;
            if (!seen.versions.empty())
                for (const auto& f : seen.versions.front().files) kinds += f.file.kind + "/" + f.file.audience + " ";
            check(kinds == "studios/tous admin/admins admin_plus/admin_plus ", "1.49.0 : les trois sortes publiees (" + kinds + ")");
        }
        t = draw(*vp);
        check(has(t, "1.44.1") && has(t, "actuelle") && has(t, "tout le monde") && has(t, "les admins") && has(t, "Admins :"), "le tableau : 1.44.1 actuelle, pour qui, les admins");
        check(!shown(*vp, "versions.signer"), "le panneau se referme");
    }
    {
        auto dlg = gui::makeAddAdminDialog(rig.host());
        (void)dlg->Initialize();
        ui::Widget& root = *dlg->widgetRoot();
        (void)drawMenu(*dlg);
        check(shown(root, "admins.ajouter.pc") && shown(root, "admins.ajouter.envoyer"), "dans les notifications de son Studio : le PC, Envoyer");
        auto* byLink = find<ui::RadioButton>(root, "admins.ajouter.envoi.lien");
        if (byLink) byLink->select();
        (void)drawMenu(*dlg);
        check(shown(root, "admins.ajouter.lien.creer") && !shown(root, "admins.ajouter.pc"), "par un lien : Creer le lien");
        check(click(root, "admins.ajouter.lien.creer") && lastOk(rig), "le lien (l'installateur de l'Admin publie) : " + lastText(rig));
        const std::string d = drawMenu(*dlg);
        check(has(d, "Lien (48 h, une seule fois)") && has(d, "/v1/fichiers/lien?jeton=") && has(d, "Le lien est pr\xC3\xAAt"), "le lien montre ; l'etape 2 le dit");
        g_copied.clear();
        check(click(root, "admins.ajouter.lien.copier") && has(g_copied, "/v1/fichiers/lien?jeton="), "Copier le lien : dans le presse-papiers");
    }
    return finish();
}
