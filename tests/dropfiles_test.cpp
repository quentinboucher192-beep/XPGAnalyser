// tests/dropfiles_test.cpp - Lot API 8 : glisser n'importe quel fichier dans la fenetre.
//
//   dropfiles_test [dossier des fixtures ihm] [libs/] [classeur .xlsm]
//
//  Sans ecran : le plan (app/DropFilesPlan) - ce qu'on peut faire de chaque
//  fichier depose, lu dans son contenu.
//  1. Le genre d'un fichier par son extension (image, police, PDF, classeur,
//     CSV, texte, JSON, XML, base), un dossier, un fichier absent.
//  2. La case par defaut selon le genre : une image, un son, une video, une
//     police -> Ressources ; un texte, du JSON, du XML -> Fichiers externes ; un
//     PDF (lot API 8, 2e partie) : les deux cases possibles, Fichiers externes
//     cochee. Tout fichier va dans l'un, l'autre ou les deux.
//  3. Un classeur, detecte par son contenu : une liste d'E/S (Nom / Type /
//     Adresse...) -> importer les variables IHM ; "Texte (fr)" -> les
//     traductions ; toujours : Fichiers externes, Ressources (grisee), et pour
//     un .xlsx le classeur des macros. Sans IHM : les lignes de l'IHM grisees.
//  4. Le classeur d'automatisation (.xlsm, si on le trouve) : les macros de
//     libs/ qui le lisent, une ligne chacune, la meilleure cochee.
//  5. Les morceaux : les colonnes lues par une macro (Cell, HasColumn), le nom
//     d'une table tire d'un onglet, le texte de collage (tabulations, CSV ';').
//  6. (lot API 8, 2e partie) Faire avec un PDF, les deux cases cochees : ce que
//     fait runDropPlan (readResource, linkExternal) - une ressource Document
//     "PDF", un lien Document ; le projet IHM enregistre puis relu (le genre de
//     chacun sur le disque) ; les genres d'avant se relisent pareil.
#include "../src/app/DropFilesPlan.hpp"
#include "../src/hmi/HmiAssets.hpp"
#include "../src/hmi/HmiExternal.hpp"
#include "../src/hmi/HmiMedia.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace df = app::dropfiles;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    }
}

void touch(const fs::path& p, const std::string& text = "x") {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

const df::Action* findAction(const df::WorkbookPlan& w, df::ActionKind kind) {
    for (const auto& a : w.actions)
        if (a.kind == kind) return &a;
    return nullptr;
}

std::string firstExisting(const std::vector<std::string>& candidates) {
    std::error_code ec;
    for (const auto& c : candidates)
        if (!c.empty() && fs::exists(c, ec)) return c;
    return {};
}

} // namespace

int main(int argc, char** argv) {
    const std::string fixtures = firstExisting({argc > 1 ? argv[1] : "", "tests/fixtures/ihm", "../tests/fixtures/ihm",
                                                "/home/claude/depot/tests/fixtures/ihm"});
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / "dropfiles_test";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "dossier", ec);

    // ---- 1. le genre ------------------------------------------------------------------
    {
        const std::vector<std::pair<std::string, df::Genre>> cases = {
            {"a.png", df::Genre::Image},   {"a.JPG", df::Genre::Image},  {"a.wav", df::Genre::Sound},
            {"a.mp4", df::Genre::Video},   {"a.ttf", df::Genre::Font},   {"a.pdf", df::Genre::Pdf},
            {"a.xlsx", df::Genre::Workbook}, {"a.xlsm", df::Genre::Workbook}, {"a.csv", df::Genre::Table},
            {"a.tsv", df::Genre::Table},   {"a.txt", df::Genre::Text},   {"a.json", df::Genre::Json},
            {"a.xml", df::Genre::Xml},     {"a.db", df::Genre::Database},
        };
        for (const auto& [name, genre] : cases) {
            touch(tmp / name);
            check(df::genreOf((tmp / name).string()) == genre, "genre de " + name + " : " + df::genreLabel(df::genreOf((tmp / name).string())));
            check(!df::genreLabel(genre).empty(), "un libelle pour le genre de " + name);
        }
        check(df::genreOf((tmp / "dossier").string()) == df::Genre::Folder, "un dossier");
        check(df::genreOf((tmp / "absent.png").string()) == df::Genre::Missing, "un fichier absent");
        check(df::isWorkbook(df::Genre::Workbook) && df::isWorkbook(df::Genre::Table) && !df::isWorkbook(df::Genre::Pdf), "isWorkbook");
    }

    // ---- 2. la case par defaut selon le genre (une IHM vide) ---------------------------
    hmi::Project emptyHmi;
    df::Context ctx;
    ctx.hmi = &emptyHmi;
    ctx.projectFolder = tmp.string();
    if (!fixtures.empty()) {
        const auto row = [&](const std::string& name) { return df::analyseFile((fs::path(fixtures) / name).string(), ctx); };
        const auto png = row("logo_site.png");
        check(png.genre == df::Genre::Image && png.resourceOk && png.resource && !png.file, "une image : Ressources cochee");
        check(png.fileOk && !png.file, "une image : Fichiers externes possible (un lien), pas cochee");
        check(png.hasPreview, "une image : sa vignette");
        const auto ttf = row("industrie_bold.ttf");
        check(ttf.resourceOk && ttf.resource && !ttf.file, "une police : Ressources cochee");
        const auto mp3 = row("annonce.mp3");
        check(mp3.resourceOk && mp3.resource, "un son : Ressources cochee");
        const auto txt = row("journal.txt");
        check(txt.fileOk && txt.file && !txt.resource && txt.resourceOk, "un texte : Fichiers externes coche, Ressources possible");
        const auto json = row("recettes_gaz.json");
        check(json.fileOk && json.file && !json.resource, "du JSON : Fichiers externes coche");
        const auto xml = row("equipements.xml");
        check(xml.fileOk && xml.file && !xml.resource, "du XML : Fichiers externes coche");
        check(!png.present() && !txt.present(), "une IHM vide : rien de deja present");
        check(png.wanted() && txt.wanted(), "wanted : une case cochee et possible");
    } else {
        std::printf("saute : pas de fixtures ihm (passe le dossier en argument)\n");
    }
    {
        touch(tmp / "notice.pdf", "%PDF-1.4");
        const auto pdf = df::analyseFile((tmp / "notice.pdf").string(), ctx);
        check(pdf.genre == df::Genre::Pdf, "un PDF");
        // Lot API 8, 2e partie : deux cases cochables ; par defaut le lien (comme la maquette).
        check(pdf.resourceOk && pdf.fileOk, "un PDF : les deux cases cochables");
        check(pdf.file && !pdf.resource && pdf.wanted(), "un PDF : Fichiers externes cochee, Ressources non");
        // Un ZIP, un fichier sans extension : pareil.
        touch(tmp / "plans.zip", "PK");
        touch(tmp / "LISEZMOI", "texte");
        const auto zip = df::analyseFile((tmp / "plans.zip").string(), ctx);
        const auto bare = df::analyseFile((tmp / "LISEZMOI").string(), ctx);
        check(zip.resourceOk && zip.fileOk && zip.file, "un ZIP : les deux cases cochables, le lien coche");
        check(bare.resourceOk && bare.fileOk, "un fichier sans extension : les deux cases cochables");
        // Un paquet de vues garde son chemin a lui (IHM > Vues > Importer des vues).
        touch(tmp / "vues.xpgvues", "PK");
        const auto pack = df::analyseFile((tmp / "vues.xpgvues").string(), ctx);
        check(!pack.resourceOk && !pack.fileOk && !pack.resourceWhy.empty(), "un paquet de vues : ni l'un ni l'autre, avec sa raison");
    }
    {
        // Sans IHM : les cases de l'IHM sont grisees.
        df::Context none;
        touch(tmp / "image.png");
        const auto row = df::analyseFile((tmp / "image.png").string(), none);
        check(!row.resourceOk && !row.fileOk && !row.wanted(), "sans IHM : rien a ajouter");
    }

    // ---- 3. un classeur, par son contenu --------------------------------------------------
    if (!fixtures.empty()) {
        const auto io = df::analyseWorkbook((fs::path(fixtures) / "liste-io-armoire.tsv").string(), ctx);
        check(io.readable && io.genre == df::Genre::Table, "la liste d'E/S (.tsv) se lit : " + io.error);
        const auto* vars = findAction(io, df::ActionKind::HmiVariables);
        check(vars && vars->possible && vars->checked && vars->rows > 0, "une liste Nom / Type / Adresse : importer les variables IHM, cochee");
        if (vars) check(df::startsFolded(vars->label, "importer"), "son libelle : " + vars->label);
        const auto* res = findAction(io, df::ActionKind::Resource);
        check(res && !res->possible && !res->why.empty(), "un CSV n'est pas une ressource : grisee avec sa raison");
        const auto* ext = findAction(io, df::ActionKind::ExternalFile);
        check(ext && ext->possible, "un CSV : Fichiers externes possible");
        const auto* book = findAction(io, df::ActionKind::MacroWorkbook);
        check(!book || !book->possible, "un CSV n'est pas le classeur des macros");

        const auto tr = df::analyseWorkbook((fs::path(fixtures) / "traductions_armoire_gaz.xlsx").string(), ctx);
        check(tr.readable && tr.genre == df::Genre::Workbook && !tr.sheets.empty(), "le classeur des traductions se lit : " + tr.error);
        const auto* t = findAction(tr, df::ActionKind::Translations);
        check(t != nullptr, "la colonne Texte (fr) : importer les traductions");
        if (t) {
            const bool sameSource = emptyHmi.languages.source() == "fr";
            check(t->possible == sameSource, "les traductions : possibles si l'IHM part du francais (" + t->why + ")");
        }
        const auto* wb = findAction(tr, df::ActionKind::MacroWorkbook);
        check(wb && wb->possible, "un .xlsx : en faire le classeur des macros");
        check(!tr.summary().empty(), "le resume du classeur");

        // Sans IHM : les lignes de l'IHM sont grisees.
        df::Context none;
        const auto io2 = df::analyseWorkbook((fs::path(fixtures) / "liste-io-armoire.tsv").string(), none);
        const auto* vars2 = findAction(io2, df::ActionKind::HmiVariables);
        check(!vars2 || (!vars2->possible && !vars2->checked && !vars2->why.empty()), "sans IHM : importer les variables grisee");
    }

    // ---- 4. le classeur d'automatisation et les macros de libs/ -------------------------
    {
        const std::string libs = firstExisting({argc > 2 ? argv[2] : "", "libs", "../libs", "/home/claude/libs"});
        const std::string xlsm = firstExisting({argc > 3 ? argv[3] : "", "/home/claude/replaya2/echantillons/affaire.xlsm",
                                                "/home/claude/livraison-2/tests/fixtures/affaire.xlsm"});
        if (libs.empty() || xlsm.empty() || !fs::exists(fs::path(libs) / "Macros", ec)) {
            std::printf("saute : pas de libs/Macros ou de classeur .xlsm (passe-les en argument)\n");
        } else {
            df::Context c2;
            c2.libsRoot = libs;
            const auto w = df::analyseWorkbook(xlsm, c2);
            // (son vbaProject.bin, s'il en a un, est ignore : w.vba le dit)
            check(w.readable && w.sheets.size() > 1, "le .xlsm se lit : " + w.error);
            std::size_t macros = 0, checked = 0;
            for (const auto& a : w.actions)
                if (a.kind == df::ActionKind::Macro) {
                    ++macros;
                    if (a.checked) ++checked;
                    check(!a.macro.empty() && !a.field.empty() && df::startsFolded(a.label, "lancer " + a.macro),
                          "une ligne par macro : " + a.label);
                    check(a.possible || !a.why.empty(), "une macro impossible dit pourquoi : " + a.label);
                }
            check(macros > 0, "des macros de libs/ lisent ce classeur");
            check(checked <= 1, "une seule macro cochee d'office");
            const auto* wb = findAction(w, df::ActionKind::MacroWorkbook);
            check(wb && wb->possible, "le .xlsm peut devenir le classeur des macros");
            // Les macros viennent apres les imports et les ajouts (l'ordre de Faire).
            int last = 0;
            for (const auto& a : w.actions) {
                const int stage = static_cast<int>(df::stageOf(a.kind));
                check(stage >= last, "les lignes rangees dans l'ordre de Faire : " + a.label);
                last = stage;
            }
        }
    }

    // ---- 5. le depot entier, les morceaux ---------------------------------------------------
    {
        touch(tmp / "a.png");
        const auto plan = df::analyse({(tmp / "a.png").string(), (tmp / "dossier").string(), (tmp / "absent.txt").string()}, ctx);
        check(plan.files.size() == 1 && plan.workbooks.empty(), "le depot : un fichier, pas de classeur");
        check(plan.ignored.size() == 2, "le depot : le dossier et le fichier absent sont ignores");

        const auto cols = df::cellColumns("x := Cell(t, i, 'Nom'); IF HasColumn(t, 'Acces ST') THEN (* Cell(t,i,'Zut') *) END_IF;");
        check(cols.size() == 2 && cols[0] == "Nom" && cols[1] == "Acces ST", "les colonnes lues par une macro (pas celles des commentaires)");
        check(df::tableName("Seuils (bar)") == "Seuils_bar", "un nom de table tire d'un onglet : " + df::tableName("Seuils (bar)"));
        check(df::tableName("___") == "Table", "un onglet sans lettre : Table");
        check(df::startsFolded("Importer les traductions", "IMPORTER LES"), "un libelle par son debut, sans casse");

        df::SheetData data;
        data.columns = {"Nom", "Type"};
        data.rows = {{"Pompe", "BOOL"}, {"a\tb", "dit \"x\""}};
        const auto tab = df::tabText(data);
        check(tab.rfind("Nom\tType", 0) == 0 && tab.find("\"a\tb\"") != std::string::npos, "le texte de collage : titres, cases a tabulation entre guillemets");
        const auto csv = df::csvText(data);
        check(csv.rfind("Nom;Type", 0) == 0, "le CSV des recettes : ';'");
    }

    // ---- 6. Lot API 8, 2e partie : Faire avec un PDF, puis l'aller-retour du projet ----------
    {
        const fs::path pdfPath = tmp / "notice.pdf";
        touch(pdfPath, "%PDF-1.4\n% la notice de la machine\n");
        hmi::Project p;
        df::Context c;
        c.hmi = &p;
        c.projectFolder = (tmp / "projet").string();
        fs::create_directories(tmp / "projet", ec);
        auto row = df::analyseFile(pdfPath.string(), c);
        row.resource = true;                                   // les deux cases cochees
        row.file = true;
        check(row.wanted(), "un PDF, les deux cases : quelque chose a faire");
        // Ce que fait Faire (runDropPlan : addResource, addExternal).
        if (row.resource && row.resourceOk) {
            auto res = hmi::readResource(p, row.path);
            check(static_cast<bool>(res), "Faire : le PDF entre dans les Ressources");
            if (res) p.assets.resources.push_back(*res);
        }
        if (row.file && row.fileOk) {
            const auto kind = hmi::externalKindFromPath(row.path);
            check(kind && *kind == hmi::ExternalKind::Document, "Faire : le PDF se lie comme un document");
            if (kind) p.assets.files.push_back(hmi::linkExternal(p, {}, *kind, row.path, {}, c.projectFolder));
        }
        const auto* res = p.resourceByName("notice.pdf");
        check(res && res->kind() == hmi::MediaKind::Document && res->format == "PDF" && res->data
                  && res->bytes == fs::file_size(pdfPath, ec),
              "la ressource : un Document PDF, son contenu garde tel quel");
        check(res && hmi::mediaKindLabel(res->kind()) == "Document", "son genre se dit Document");
        check(hmi::unusedResources(p).empty(), "un document n'est pas une ressource inutilisee");
        const auto* link = p.externalByName("notice");
        check(link && link->kind == hmi::ExternalKind::Document && link->bytes == fs::file_size(pdfPath, ec),
              "le lien : un Document, sa taille relevee");
        const auto data = hmi::readExternal(hmi::ExternalKind::Document, pdfPath.string());
        check(data.ok() && data.headers.empty() && data.summary.find("PDF") != std::string::npos, "l'apercu d'un document : " + data.summary);
        const hmi::BlobPtr pdfData = res ? res->data : nullptr;          // (les listes vont grandir)
        const std::string linkPath = link ? link->path : std::string{};

        // Enregistrer, relire : le genre de chacun sur le disque.
        hmi::Resource png;                                     // un genre d'avant, a cote
        png.id = p.allocate();
        png.name = "logo.png";
        png.format = "PNG";
        png.data = std::make_shared<hmi::Bytes>(hmi::Bytes{1, 2, 3});
        png.bytes = 3;
        p.assets.resources.push_back(png);
        p.assets.files.push_back(hmi::linkExternal(p, "Consignes", hmi::ExternalKind::Csv, (tmp / "consignes.csv").string(), {},
                                                   c.projectFolder));
        const std::string folder = (tmp / "projet").string();
        check(static_cast<bool>(hmi::save(p, folder)), "le projet IHM s'enregistre");
        auto back = hmi::load(folder);
        check(static_cast<bool>(back), "le projet IHM se relit");
        if (back) {
            const auto* r2 = back->resourceByName("notice.pdf");
            check(r2 && r2->kind() == hmi::MediaKind::Document && r2->format == "PDF" && r2->data && pdfData
                      && *r2->data == *pdfData,
                  "relu : la ressource Document PDF, le meme contenu");
            const auto* l2 = back->externalByName("notice");
            check(l2 && l2->kind == hmi::ExternalKind::Document && !linkPath.empty() && l2->path == linkPath,
                  "relu : le lien Document, son chemin");
            const auto* old = back->resourceByName("logo.png");
            check(old && old->kind() == hmi::MediaKind::Image, "relu : une image reste une image");
            const auto* csvLink = back->externalByName("Consignes");
            check(csvLink && csvLink->kind == hmi::ExternalKind::Csv, "relu : un CSV reste un CSV");
        }
        // Les genres d'avant : leurs noms sur le disque et leurs rangs ne changent pas.
        check(hmi::externalKindKey(hmi::ExternalKind::Document) == "Document"
                  && hmi::externalKindFromKey("Document") == hmi::ExternalKind::Document,
              "le nom de Document sur le disque");
        int k = 0;
        for (const char* key : {"Excel", "CSV", "TXT", "JSON", "XML", "SQLite", "Base externe"}) {
            const auto kind = hmi::externalKindFromKey(key);
            check(kind && static_cast<int>(*kind) == k, std::string("genre d'avant relu pareil : ") + key);
            ++k;
        }
        check(static_cast<int>(hmi::MediaKind::Unknown) == 4 && static_cast<int>(hmi::MediaKind::Document) == 5,
              "MediaKind : Document en fin, rien renumerote");
        for (const char* f : {"PNG", "JPEG", "GIF", "WAV", "MP3", "MP4", "TTF"})
            check(hmi::kindOfFormat(f) != hmi::MediaKind::Document && hmi::kindOfFormat(f) != hmi::MediaKind::Unknown,
                  std::string("un format de media reste un media : ") + f);
        check(hmi::kindOfFormat("") == hmi::MediaKind::Unknown, "un format vide : inconnu");
        check(hmi::documentFormat("plan.dwg") == "DWG" && hmi::documentFormat("LISEZMOI") == "FICHIER"
                  && hmi::documentFormat("C:\\a.b\\notes") == "FICHIER",
              "le format d'un document : son extension");
        check(hmi::externalKindFromPath("a.tsv") == hmi::ExternalKind::Csv && hmi::externalKindFromPath("a.zip") == hmi::ExternalKind::Document
                  && !hmi::externalKindFromPath(""),
              "le genre d'un lien par l'extension");
    }

    fs::remove_all(tmp, ec);
    if (failures) {
        std::printf("dropfiles_test : %d echec(s)\n", failures);
        return 1;
    }
    std::printf("dropfiles_test : tout est bon\n");
    return 0;
}
