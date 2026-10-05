// =============================================================================
//  tests/xls_test.cpp — lire un .xlsx / .xlsm
// -----------------------------------------------------------------------------
//  Ce qui est teste n'est pas la decompression - miniz s'en charge et ce n'est
//  pas notre code. C'est tout ce qui, dans ce format, se trompe SILENCIEUSEMENT :
//
//    LES CHAINES PARTAGEES. Une cellule de texte ne contient pas son texte mais
//    un NUMERO dans une table commune. Lire une feuille sans cette table rend
//    des nombres a la place des designations - et ces nombres ont l'air de
//    donnees valides.
//
//    LES CELLULES EPARSES. Une ligne ne porte que ses cellules non vides, et
//    chacune dit ou elle est. Les empiler dans l'ordre d'arrivee decale tout ce
//    qui suit le premier trou, d'une colonne, sans rien casser d'apparent.
//
//    L'ORDRE DES FEUILLES. Il n'est pas celui des fichiers sheetN.xml. Supposer
//    que la premiere feuille est sheet1.xml marche sur un classeur neuf et faux
//    sur un classeur reorganise.
//
//    LES FORMULES. Une cellule calculee porte sa formule ET son dernier
//    resultat. Lire la formule au lieu du resultat donne "=B8*2" la ou on
//    attendait 42.
// =============================================================================
#include "../src/xls/Workbook.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

xls::ReadOptions ioOptions(std::string sheet) {
    xls::ReadOptions o;
    o.sheet = std::move(sheet);
    o.table.anchors = {"Carte", "Designation", "Adresse"};
    o.table.descriptionRows = 1;
    return o;
}

} // namespace

int main(int argc, char** argv) {
    // ---- l'arithmetique des references -------------------------------------
    //
    //  Base 26 SANS zero : A vaut 1, Z vaut 26, AA vaut 27. Traiter A comme zero
    //  donne AA = 0 et decale toute la feuille a partir de la vingt-septieme
    //  colonne - c'est-a-dire precisement sur les feuilles larges, les seules ou
    //  personne n'ira verifier a la main.
    assert(xls::columnOf("A1") == 0);
    assert(xls::columnOf("B1") == 1);
    assert(xls::columnOf("Z9") == 25);
    assert(xls::columnOf("AA1") == 26);
    assert(xls::columnOf("AB1") == 27);
    assert(xls::columnOf("AZ1") == 51);
    assert(xls::columnOf("BA1") == 52);
    assert(xls::columnOf("BC12") == 54);
    assert(xls::columnOf("") == 0);
    assert(xls::columnOf("12") == 0 && "une reference sans lettre ne casse rien");

    assert(xls::rowOf("A1") == 0);
    assert(xls::rowOf("A9") == 8);
    assert(xls::rowOf("BC12") == 11);
    assert(xls::rowOf("A") == 0 && "une reference sans chiffre non plus");

    // ---- LES CELLULES EPARSES, sur des donnees fabriquees ------------------
    //
    //  Le classeur de l'affaire n'a aucune cellule manquante, donc aucune
    //  lecture de ce fichier ne peut montrer qu'on empile au lieu de placer.
    //  Une passe de mutations l'a demontre : la faute passait inapercue.
    {
        const std::vector<std::string> chaines{"alpha", "beta", "gamma"};

        // Ligne 1 : A, puis C - B est absente. Ligne 2 : les trois.
        const std::string xml =
            "<worksheet><sheetData>"
            "<row r=\"1\">"
              "<c r=\"A1\" t=\"s\"><v>0</v></c>"
              "<c r=\"C1\" t=\"s\"><v>2</v></c>"
            "</row>"
            "<row r=\"2\">"
              "<c r=\"A2\"><v>10</v></c>"
              "<c r=\"B2\"><v>11</v></c>"
              "<c r=\"C2\"><v>12</v></c>"
            "</row>"
            "</sheetData></worksheet>";

        bool formules = false;
        const auto lignes = xls::parseSheet(xml, chaines, 100, &formules);
        assert(lignes.size() == 2);
        assert(!formules);

        assert(lignes[0].size() == 3 && "la ligne va jusqu'a la colonne C");
        assert(lignes[0][0] == "alpha" && "l'index 0 designe la premiere chaine");
        assert(lignes[0][1].empty() && "B est VIDE, pas decalee");
        assert(lignes[0][2] == "gamma"
               && "et gamma reste en C : l'empiler la mettrait en B, et toute la "
                  "colonne suivante serait fausse sur cette ligne seulement");

        assert(lignes[1][0] == "10" && lignes[1][1] == "11" && lignes[1][2] == "12");
    }

    // ---- les formules : le RESULTAT, jamais la formule ---------------------
    {
        const std::string xml =
            "<worksheet><sheetData><row r=\"1\">"
            "<c r=\"A1\"><f>IF(B1&gt;0,\"oui\",\"non\")</f><v>oui</v></c>"
            "</row></sheetData></worksheet>";
        bool formules = false;
        const auto lignes = xls::parseSheet(xml, {}, 100, &formules);
        assert(lignes.size() == 1);
        assert(lignes[0][0] == "oui"
               && "c'est le resultat qu'on veut - lire <f> donnerait la formule");
        assert(formules && "et la presence de formules se signale : un classeur "
                           "non recalcule a des resultats perimes");
    }

    // ---- un index de chaine hors bornes ------------------------------------
    {
        // Un classeur abime peut renvoyer a une chaine qui n'existe pas. Le
        // lecteur doit rendre une cellule vide, pas lire a cote de la table.
        const std::string xml =
            "<worksheet><sheetData><row r=\"1\">"
            "<c r=\"A1\" t=\"s\"><v>9999</v></c></row></sheetData></worksheet>";
        const auto lignes = xls::parseSheet(xml, {"une seule"}, 100, nullptr);
        assert(lignes.size() == 1);
        assert(lignes[0][0].empty());
    }

    // ---- maxRows arrete vraiment -------------------------------------------
    {
        std::string xml = "<worksheet><sheetData>";
        for (int i = 1; i <= 50; ++i)
            xml += "<row r=\"" + std::to_string(i) + "\"><c r=\"A"
                 + std::to_string(i) + "\"><v>1</v></c></row>";
        xml += "</sheetData></worksheet>";
        assert(xls::parseSheet(xml, {}, 10, nullptr).size() == 10);
    }

    // ---- LE CHOIX DE L'ONGLET, sur une liste fabriquee ---------------------
    //
    //  Le premier onglet du classeur de l'affaire est visible, donc "le premier"
    //  et "le premier visible" y donnent la meme reponse - et aucun test sur ce
    //  fichier ne peut les distinguer.
    {
        const std::vector<xls::SheetInfo> feuilles{
            {"_Travail", "xl/worksheets/sheet1.xml", true},
            {"Entrees TOR", "xl/worksheets/sheet2.xml", false},
            {"Sorties TOR", "xl/worksheets/sheet3.xml", false},
        };

        const auto* defaut = xls::chooseSheet(feuilles, "");
        assert(defaut && defaut->name == "Entrees TOR"
               && "sans nom demande : le premier VISIBLE, pas le premier");

        assert(xls::chooseSheet(feuilles, "Sorties TOR")->name == "Sorties TOR");
        assert(xls::chooseSheet(feuilles, "sorties tor")->name == "Sorties TOR"
               && "la casse ne compte pas : personne ne retape un nom d'onglet "
                  "a la majuscule pres");
        assert(xls::chooseSheet(feuilles, "_Travail")->name == "_Travail"
               && "une feuille masquee se lit quand on la demande NOMMEMENT - "
                  "c'est le defaut qui l'evite, pas une interdiction");
        assert(xls::chooseSheet(feuilles, "Absent") == nullptr);
        assert(xls::chooseSheet({}, "") == nullptr);

        // Toutes masquees : on rend quand meme la premiere. Un classeur dont
        // tout est masque existe, et ne rien rendre serait moins utile.
        const std::vector<xls::SheetInfo> toutesMasquees{
            {"A", "xl/worksheets/sheet1.xml", true},
            {"B", "xl/worksheets/sheet2.xml", true},
        };
        assert(xls::chooseSheet(toutesMasquees, "")->name == "A");
    }

    // ---- ce qui n'est pas un classeur --------------------------------------
    {
        const auto info = xls::inspect("/does/not/exist.xlsx");
        assert(info.sheets.empty());
        assert(!info.warnings.empty()
               && "un fichier absent doit se dire, pas rendre un classeur vide");

        // Un .xls ancien n'est PAS une archive : c'est un format binaire
        // different, et le message doit le dire plutot que "fichier illisible".
        const auto pasZip = xls::inspect("/etc/hostname");
        assert(pasZip.sheets.empty());
        bool dit = false;
        for (const auto& w : pasZip.warnings)
            if (w.find("ZIP") != std::string::npos) dit = true;
        assert(dit);

        const auto r = xls::read("/does/not/exist.xlsx");
        assert(!r.ok && r.table.rowCount() == 0);
    }

    // ---- un vrai classeur, quand on en a un --------------------------------
    if (argc < 2) {
        std::printf("xls_test: aucun classeur fourni, seule l'arithmetique est verifiee\n");
        return 0;
    }
    const std::string chemin = argv[1];

    const auto info = xls::inspect(chemin);
    if (info.sheets.empty()) {
        std::printf("xls_test: %s n'a pas pu etre lu\n", chemin.c_str());
        for (const auto& w : info.warnings) std::printf("   ! %s\n", w.c_str());
        return 1;
    }

    std::printf("%zu onglet(s), %zu chaine(s) partagee(s), macros : %s\n",
                info.sheets.size(), info.sharedStrings, info.hasMacros ? "oui" : "non");

    // Chaque onglet a un nom ET un fichier. Un onglet sans fichier est un rId
    // qu'on n'a pas su resoudre - et il rendrait un tableau vide sans raison.
    bool masqueVu = false;
    for (const auto& s : info.sheets) {
        assert(!s.name.empty());
        assert(!s.path.empty() && "chaque onglet doit etre relie a son fichier");
        assert(s.path.rfind("xl/", 0) == 0);
        if (s.hidden) masqueVu = true;
        std::printf("   %-22s %-26s%s\n", s.name.c_str(), s.path.c_str(),
                    s.hidden ? " (masque)" : "");
    }

    // ---- un onglet qui n'existe pas ----------------------------------------
    {
        const auto r = xls::read(chemin, ioOptions("Onglet Imaginaire"));
        assert(!r.ok);
        bool liste = false;
        for (const auto& w : r.warnings)
            if (w.find(info.sheets.front().name) != std::string::npos) liste = true;
        assert(liste && "un onglet introuvable doit dire LESQUELS existent : sinon il "
                        "faut rouvrir le classeur pour regarder");
    }

    // ---- la feuille par defaut n'est pas une feuille masquee ---------------
    {
        const auto r = xls::read(chemin, xls::ReadOptions{});
        if (masqueVu) {
            for (const auto& s : info.sheets)
                if (s.name == r.sheetUsed)
                    assert(!s.hidden && "sans onglet demande, on prend le premier "
                                        "VISIBLE : une feuille masquee l'a ete expres");
        }
    }

    // ---- une feuille d'E/S -------------------------------------------------
    {
        std::string cible;
        for (const auto& s : info.sheets)
            if (s.name.find("TOR") != std::string::npos
                || s.name.find("ANA") != std::string::npos) { cible = s.name; break; }
        if (cible.empty()) {
            std::printf("xls_test: pas d'onglet d'E/S ici, le reste est verifie\n");
            return 0;
        }

        const auto r = xls::read(chemin, ioOptions(cible));
        assert(r.ok);
        assert(r.sheetUsed == cible);
        assert(r.table.rowCount() > 0 && "un onglet d'E/S doit rendre des voies");
        assert(r.table.has("Carte") && r.table.has("Designation")
               && r.table.has("Adresse"));

        for (std::size_t i = 0; i < r.table.rowCount(); ++i) {
            const auto carte = r.table.cell(i, "Carte");
            const auto adresse = r.table.cell(i, "Adresse");

            // LES CHAINES PARTAGEES SONT RESOLUES. Si elles ne l'etaient pas, on
            // lirait "17" au lieu de "DI_R0S4" - un nombre qui a l'air d'une
            // donnee valide.
            assert(!carte.empty());
            assert(carte.find_first_not_of("0123456789") != std::string::npos
                   && "un nom de carte n'est pas un nombre : ce serait un index de "
                      "chaine partagee non resolu");

            // LA NOTE DE BAS DE PAGE N'EST PAS UNE VOIE. Une feuille porte
            // presque toujours une phrase sous ses donnees, dans la premiere
            // colonne - donc dans une colonne d'ancrage.
            assert(carte.size() < 40
                   && "une carte au nom de quarante caracteres est une phrase, pas "
                      "une carte");

            // L'ADRESSE EST UN RESULTAT DE FORMULE. La lire depuis <f> au lieu
            // de <v> donnerait "=IF(OR(..." a la place de "%I0.4.0".
            if (!adresse.empty()) {
                assert(adresse[0] == '%' && "une adresse API commence par %");
                assert(adresse.find('=') == std::string::npos
                       && "et n'est pas une formule : c'est le RESULTAT qu'on veut");
            }
        }

        // LES CELLULES EPARSES. Une ligne dont une cellule du milieu est vide ne
        // doit pas decaler celles d'apres : la colonne lue par son nom doit
        // rester la bonne sur toutes les lignes.
        const auto colonnes = r.table.columnCount();
        assert(colonnes >= 8);
        for (std::size_t i = 0; i < r.table.rowCount(); ++i) {
            const auto parNom = r.table.cell(i, "Adresse");
            const auto parIndex = r.table.cell(i, r.table.column("Adresse"));
            assert(parNom == parIndex);
        }

        // Les accents survivent : le XML est en UTF-8, et la table aussi.
        bool accent = false;
        for (std::size_t i = 0; i < r.table.rowCount(); ++i)
            if (r.table.cell(i, "Designation").find("\xC3") != std::string::npos)
                accent = true;

        std::printf("\n'%s' : %zu voie(s), %zu colonne(s)%s\n",
                    r.sheetUsed.c_str(), r.table.rowCount(), colonnes,
                    accent ? ", accents conserves" : "");
        for (std::size_t i = 0; i < r.table.rowCount() && i < 3; ++i)
            std::printf("   %-28s %-12s %s\n",
                        r.table.cell(i, "Designation").c_str(),
                        r.table.cell(i, "Adresse").c_str(),
                        r.table.cell(i, "Tableau").c_str());
    }

    std::printf("xls_test: ok\n");
    return 0;
}
