// =============================================================================
//  tests/dossiers_test.cpp - 1.8.0 : les dossiers de l'application
// -----------------------------------------------------------------------------
//  app/Dossiers.hpp : le .ini (lire, ecrire sans perdre les commentaires), les
//  jetons ({Documents}, %VAR%), la resolution (installation.ini, puis
//  XPGAnalyser.ini, puis le defaut ; la version de developpement ne change
//  rien), le demarrage (le dossier des donnees cree, amorce, dossier de
//  travail), changer (tout verifier d'abord, copier sans ecraser, l'ancien
//  .ini garde, projets et captures tout de suite).
//  1.21.0 : app/ArbreTailles.hpp, le poids des fichiers de l'application (A
//  propos) - le total juste, le tri, l'annulation, les liens non suivis, une
//  racine dans une autre comptee une seule fois.
// =============================================================================
#include "../src/app/Dossiers.hpp"
#include "../src/app/ArbreTailles.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace app::dossiers;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Contexte contexte(const fs::path& root) {
    Contexte c;
    c.documents = (root / "Documents").string();
    c.appData = (root / "Roaming").string();
    c.localAppData = (root / "Local").string();
    c.programData = (root / "ProgramData").string();
    c.programme = (root / "Programme").string();
    c.variable = [](std::string_view nom) -> std::optional<std::string> {
        if (nom == "USERNAME") return std::string("Op\xC3\xA9rateur");
        return std::nullopt;
    };
    return c;
}

// 1.21.0 : l'arbre des tailles de la fenetre A propos (app/ArbreTailles.hpp).
void arbreDesTailles(const fs::path& base) {
    namespace t = app::tailles;
    const fs::path A = base / "A", B = base / "B";
    write(A / "a1.bin", std::string(100, 'x'));
    write(A / "a2.bin", std::string(300, 'x'));
    write(A / "sous" / "s1.bin", std::string(1000, 'x'));
    fs::create_directories(A / "vide");
    write(B / "b.bin", std::string(50, 'x'));
    write(B / "inclus" / "c.bin", std::string(7, 'x'));

    const auto noms = [](const t::Noeud& n) {
        std::string s;
        for (const auto& e : n.enfants) s += (s.empty() ? "" : ",") + e.nom;
        return s;
    };

    // ---- parcourir : le total, les fichiers, le tri ----------------------------
    t::Parcours p = t::parcourir(A);
    check(!p.annule && p.illisibles == 0 && p.racine.dossier, "parcourir : un dossier lu sans erreur");
    check(p.racine.octets == 1400 && p.racine.fichiers == 3, "parcourir : 1 400 octets dans 3 fichiers (sous-dossier compris)");
    t::trier(p.racine, t::Tri::Poids);
    check(noms(p.racine) == "sous,a2.bin,a1.bin,vide", "tri par poids decroissant (le dossier vide en dernier) : " + noms(p.racine));
    const t::Noeud* sous = p.racine.enfants.empty() ? nullptr : &p.racine.enfants.front();
    check(sous && sous->dossier && sous->octets == 1000 && sous->fichiers == 1, "un dossier porte le poids et le nombre de ses fichiers");
    t::trier(p.racine, t::Tri::Nom);
    check(noms(p.racine) == "sous,vide,a1.bin,a2.bin", "tri par nom : les dossiers d'abord : " + noms(p.racine));

    // ---- annulation -------------------------------------------------------------
    {
        std::atomic<bool> arret{true};
        t::Options o;
        o.arret = &arret;
        const t::Parcours q = t::parcourir(A, o);
        check(q.annule, "le drapeau leve : le parcours s'arrete");
        const t::Bilan b = t::mesurer({{"A", utf8De(A)}}, o);
        check(b.annule && b.racines.empty() && b.octets == 0, "mesurer s'arrete aussi (rien de compte)");
    }

    // ---- la limite des noeuds : tout est compte, pas tout est garde ---------------
    {
        t::Options o;
        o.maxNoeuds = 1;
        const t::Parcours q = t::parcourir(A, o);
        std::uint64_t caches = 0;
        const std::function<void(const t::Noeud&)> somme = [&](const t::Noeud& n) {
            caches += n.nonDetailles;
            for (const auto& e : n.enfants) somme(e);
        };
        somme(q.racine);
        check(q.racine.octets == 1400 && q.racine.fichiers == 3 && q.noeuds <= 1 && caches > 0,
              "au-dela de la limite : compte (1 400 octets, 3 fichiers) mais pas detaille");
    }

    // ---- les liens symboliques ne sont pas suivis -----------------------------------
    {
        std::error_code ec;
        fs::create_directory_symlink(B, A / "lien", ec);
        if (!ec) {
            const t::Parcours q = t::parcourir(A);
            check(q.racine.octets == 1400 && q.racine.fichiers == 3, "un lien vers un autre dossier n'est ni suivi ni compte");
            fs::remove(A / "lien", ec);
        } else {
            std::printf("     (pas de lien symbolique ici : %s)\n", ec.message().c_str());
        }
    }

    // ---- retenir : les racines qui existent, aucune dans une autre -----------------
    {
        const std::vector<t::Racine> c{{"Inclus", utf8De(B / "inclus")},
                                       {"A", utf8De(A)},
                                       {"B", utf8De(B)},
                                       {"A encore", utf8De(A) + "/"},
                                       {"Absent", utf8De(base / "absent")},
                                       {"Vide", ""}};
        const t::Retenues r = t::retenir(c);
        check(r.racines.size() == 2 && r.racines[0].libelle == "A" && r.racines[1].libelle == "B",
              "retenir : A et B ; l'absent et le vide oublies, l'inclus et le doublon retires");
        bool inclus = false, doublon = false;
        for (const auto& x : r.incluses) {
            if (x.racine.libelle == "Inclus") inclus = x.dans == 1 && x.sousChemin == std::vector<std::string>{"inclus"};
            if (x.racine.libelle == "A encore") doublon = x.dans == 0 && x.sousChemin.empty();
        }
        check(r.incluses.size() == 2 && inclus && doublon, "les incluses disent dans quelle racine (et le chemin depuis elle)");

        t::Progression prog;
        t::Options o;
        o.progression = &prog;
        std::vector<std::string> vues;
        const t::Bilan b = t::mesurer(c, o, t::Tri::Poids, [&](std::size_t, const t::Racine& r0) { vues.push_back(r0.libelle); });
        check(!b.annule && b.racines.size() == 2 && b.octets == 1400 + 57 && b.fichiers == 5,
              "mesurer : 1 457 octets dans 5 fichiers - B/inclus compte une seule fois");
        check(prog.fichiers.load() == 5 && prog.octets.load() == 1457 && vues == std::vector<std::string>{"A", "B"},
              "la progression avance, chaque racine annoncee");
        bool trouvee = false;
        for (const auto& x : b.incluses)
            if (x.racine.libelle == "Inclus") trouvee = x.trouvee && x.octets == 7 && x.fichiers == 1;
        check(trouvee, "l'incluse est retrouvee dans l'arbre de sa racine (7 octets)");
        const std::string s = t::resume(b, "Essai");
        check(s.find("Poids total : 1,4 Ko") != std::string::npos && s.find("dont Inclus : 7 o") != std::string::npos
                  && s.find("illisible") == std::string::npos,
              "le resume : le total a la francaise, ce que contient chaque racine");
        check(t::milliers(1234567) == "1\xE2\x80\xAF" "234\xE2\x80\xAF" "567" && t::nombreDeFichiers(1) == "1 fichier",
              "les milliers et le pluriel");
    }

    // ---- un dossier illisible : saute et compte (sauf pour un administrateur) ----------
    {
        std::error_code ec;
        const fs::path ferme = A / "ferme";
        write(ferme / "secret.bin", std::string(10, 'x'));
        fs::permissions(ferme, fs::perms::none, ec);
        std::error_code lu;
        fs::directory_iterator essai(ferme, lu);
        if (!ec && lu) {
            const t::Parcours q = t::parcourir(A);
            check(q.illisibles == 1 && q.racine.octets == 1400, "un dossier sans droit de lecture : saute, compte comme illisible");
        } else {
            std::printf("     (le dossier ferme reste lisible ici : essai des droits saute)\n");
        }
        fs::permissions(ferme, fs::perms::owner_all, ec);
    }
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "xpg-dossiers-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path cwdBefore = fs::current_path();

    // ---- 1. le .ini -----------------------------------------------------------
    {
        const std::string text = "\xEF\xBB\xBF; un commentaire\r\n[installation]\r\nversion = 1.8.0\r\n\r\n[Dossiers]\r\n"
                                 "# autre commentaire\r\ndonnees = \"C:\\Mes donn\xC3\xA9" "es\\XPG\"\r\nprojets =\r\n[Autre]\r\ncle=valeur\r\n";
        const Ini ini = Ini::depuisTexte(text);
        check(ini.valeur("Installation", "VERSION") == std::optional<std::string>("1.8.0"), "section et cle sans la casse");
        check(ini.valeur("dossiers", "donnees") == std::optional<std::string>("C:\\Mes donn\xC3\xA9" "es\\XPG"),
              "une valeur entre guillemets, accents gardes, BOM retire");
        check(ini.valeur("Dossiers", "projets") == std::optional<std::string>(""), "une cle vide : \"\"");
        check(!ini.valeur("Dossiers", "captures").has_value(), "une cle absente : nullopt");
        check(!ini.valeur("Dossiers", "cle").has_value(), "une cle d'une autre section n'est pas prise");
        check(ini.lignes().front() == "; un commentaire", "le BOM ne reste pas dans la premiere ligne");

        Ini w = ini;
        w.poser("Dossiers", "projets", "D:\\Projets");
        w.poser("Dossiers", "captures", "{Donnees}\\Captures");
        w.poser("Maintenance", "journaux", "{LocalAppData}\\XpgAnalyzer\\Journaux");
        const Ini r = Ini::depuisTexte(w.texte());
        check(r.valeur("Dossiers", "projets") == std::optional<std::string>("D:\\Projets"), "poser remplace la ligne de la cle");
        check(r.valeur("Dossiers", "captures") == std::optional<std::string>("{Donnees}\\Captures"), "poser ajoute une cle a sa section");
        check(r.valeur("Maintenance", "journaux").has_value(), "poser cree une section absente");
        check(r.valeur("Autre", "cle") == std::optional<std::string>("valeur"), "les autres sections restent");
        const std::string t = w.texte();
        check(t.find("# autre commentaire") != std::string::npos && t.find("; un commentaire") != std::string::npos,
              "les commentaires restent");
        const auto posCaptures = t.find("captures = ");
        const auto posAutre = t.find("[Autre]");
        check(posCaptures != std::string::npos && posCaptures < posAutre, "la cle ajoutee reste dans [Dossiers], avant [Autre]");

        const fs::path f = root / "rw" / "essai.ini";
        std::string why;
        check(ecrireIni(f, w, &why), "ecrireIni " + why);
        const std::string bytes = read(f);
        check(bytes.rfind("\xEF\xBB\xBF", 0) == 0, "ecrit en UTF-8 avec BOM");
        const auto back = lireIni(f);
        check(back && back->valeur("Dossiers", "projets") == std::optional<std::string>("D:\\Projets"), "relu tel quel");
        check(!lireIni(root / "absent.ini").has_value(), "un .ini absent : nullopt");
        const Ini modele = Ini::depuisTexte(modeleIniUtilisateur());
        check(modele.valeur("Dossiers", "donnees") == std::optional<std::string>("") &&
                  modele.valeur("Dossiers", "bibliotheque") == std::optional<std::string>(""),
              "le modele : les quatre cles, vides");
    }

    // ---- 2. les jetons ----------------------------------------------------------
    const Contexte c = contexte(root);
    {
        const std::string d = developper("{documents}\\XPGAnalyser", c);
        check(d == (root / "Documents").string() + (fs::path::preferred_separator == '\\' ? "\\" : "/") + "XPGAnalyser",
              "{Documents} sans la casse : " + d);
        check(developper("%USERNAME%-x", c) == "Op\xC3\xA9rateur-x", "une %VARIABLE%");
        check(developper("%INCONNUE%", c) == "%INCONNUE%", "une variable inconnue reste");
        check(developper("{Rien}", c) == "{Rien}", "un jeton inconnu reste");
        check(developper("100% {Programme}", c).find((root / "Programme").string()) != std::string::npos, "un % seul ne gene pas");
        const std::string a = absolu("Projets/../Projets/./X/", c, (root / "Donnees").string());
        check(a == (root / "Donnees" / "Projets" / "X").string(), "absolu : relatif au dossier donne, normalise, sans / final : " + a);
        check(absolu(root.string(), c, "/ailleurs") == root.string(), "absolu : un chemin absolu reste");
    }

    // ---- 3. la resolution --------------------------------------------------------
    {
        const Etat dev = resoudre(nullptr, nullptr, c, (root / "travail").string(), (root / "reglages").string());
        check(!dev.installe, "sans installation.ini : la version de developpement");
        check(dev[Cle::Donnees].chemin == (root / "travail").string() && dev[Cle::Projets].chemin == (root / "travail" / "projets").string() &&
                  dev[Cle::Bibliotheque].chemin == (root / "travail" / "libs").string() && dev[Cle::Captures].source == Source::Developpement,
              "developpement : tout suit le dossier de travail");

        const Ini inst = Ini::depuisTexte("[Installation]\nversion = 1.8.0\nportee = utilisateur\n[Dossiers]\ndonnees = {Documents}\\XPGAnalyser\n"
                                          "[Maintenance]\njournaux = {LocalAppData}\\XpgAnalyzer\\Journaux\n");
        const Etat e1 = resoudre(&inst, nullptr, c, {}, (root / "reglages").string());
        const std::string data = (root / "Documents" / "XPGAnalyser").string();
        check(e1.installe && e1[Cle::Donnees].chemin == data && e1[Cle::Donnees].source == Source::Installation,
              "installation.ini : le dossier des donnees choisi a l'installation : " + e1[Cle::Donnees].chemin);
        check(e1[Cle::Projets].chemin == (fs::path(data) / "projets").string() && e1[Cle::Projets].source == Source::ParDefaut,
              "projets par defaut : <donnees>/projets");
        check(e1[Cle::Bibliotheque].chemin == (fs::path(data) / "libs").string(), "bibliotheque par defaut : <donnees>/libs");
        check(e1.journaux == (root / "Local" / "XpgAnalyzer" / "Journaux").string() && e1.portee == "utilisateur" && e1.versionInstallee == "1.8.0",
              "[Maintenance] et [Installation] lus");

        const Ini user = Ini::depuisTexte("[Dossiers]\ndonnees = " + (root / "Ailleurs").string() + "\nbibliotheque = " + (root / "Partage" / "libs").string() +
                                          "\ncaptures = Captures\nprojets =\n");
        const Etat e2 = resoudre(&inst, &user, c, {}, {});
        check(e2[Cle::Donnees].chemin == (root / "Ailleurs").string() && e2[Cle::Donnees].source == Source::Utilisateur,
              "XPGAnalyser.ini l'emporte sur installation.ini");
        check(e2[Cle::Bibliotheque].chemin == (root / "Partage" / "libs").string() && e2[Cle::Bibliotheque].source == Source::Utilisateur,
              "une bibliotheque ailleurs (un partage)");
        check(e2[Cle::Captures].chemin == (root / "Ailleurs" / "Captures").string(), "un relatif part du dossier des donnees");
        check(e2[Cle::Projets].source == Source::ParDefaut && e2[Cle::Projets].chemin == (root / "Ailleurs" / "projets").string(),
              "une cle vide : le defaut, sous le NOUVEAU dossier des donnees");

        const Ini vide = Ini::depuisTexte("[Installation]\nversion = 1.8.0\n");
        const Etat e3 = resoudre(&vide, nullptr, c, {}, {});
        check(e3[Cle::Donnees].source == Source::ParDefaut && e3[Cle::Donnees].chemin == data, "installation.ini sans dossier : Documents/XPGAnalyser");

        // 1.14.0 (lot M2) : l'installation.ini que l'installateur ecrit maintenant (UTF-8 avec BOM, CRLF) - le profil,
        // les applications installees, les raccourcis du Bureau, la copie de secours, la section [Poste] - se lit
        // comme avant : les dossiers, la portee, la version, l'epinglage.
        const Ini m2 = Ini::depuisTexte(
            "\xEF\xBB\xBF; installation.ini - \xC3\xA9" "crit par l'installateur d'API IHM Studio 1.14.0. Ne pas modifier :\r\n"
            "[Installation]\r\nnom = API IHM Studio\r\ncle = XPGAnalyser\r\nversion = 1.14.0\r\nportee = tous\r\nexe = IHM-Studio.exe\r\n"
            "profil = poste\r\napplications = IHM-Studio.exe\r\nchaine = mingw\r\nraccourci_bureau = oui\r\n"
            "raccourcis_bureau = Poste d'exploitation\r\nepingler_barre_taches = oui\r\n\r\n"
            "[Dossiers]\r\ndonnees = {Documents}\\XPGAnalyser\r\n\r\n"
            "[Maintenance]\r\njournaux = {LocalAppData}\\XpgAnalyzer\\Journaux\r\ncopie_de_secours = non\r\n\r\n"
            "[Poste]\r\nprojet = D:\\Projets\\Armoire\r\ndemarrage = oui\r\nrelance = oui\r\n");
        const Etat e4 = resoudre(&m2, nullptr, c, {}, (root / "reglages").string());
        check(e4.installe && e4[Cle::Donnees].chemin == data && e4[Cle::Donnees].source == Source::Installation && e4.portee == "tous" &&
                  e4.versionInstallee == "1.14.0" && e4.epinglerBarre &&
                  e4.journaux == (root / "Local" / "XpgAnalyzer" / "Journaux").string(),
              "1.14.0 : installation.ini avec profil, applications, copie_de_secours et [Poste] : les dossiers, la portee, l'epinglage lus comme avant");
        check(m2.valeur("Installation", "profil") == std::optional<std::string>("poste") &&
                  m2.valeur("Poste", "projet") == std::optional<std::string>("D:\\Projets\\Armoire"),
              "1.14.0 : le profil et le projet du poste se lisent (la maintenance s'en sert)");
    }

    // ---- 4. copier sans ecraser ; contient ---------------------------------------
    {
        const fs::path a = root / "copie" / "a", b = root / "copie" / "b";
        write(a / "un.txt", "1");
        write(a / "sous" / "deux.txt", "22");
        write(b / "un.txt", "le mien");
        const Copie k = copierContenu(a, b);
        check(k.fichiers == 1 && k.gardes == 1 && k.erreurs == 0 && k.octets == 2, "copierContenu : 1 copie, 1 garde");
        check(read(b / "un.txt") == "le mien", "un fichier deja la n'est pas ecrase");
        check(read(b / "sous" / "deux.txt") == "22", "les sous-dossiers suivent");
        check(read(a / "un.txt") == "1", "la source reste");
        check(contient(root / "x", root / "x" / "y") && contient(root / "x", root / "x") && !contient(root / "x", root / "xy") &&
                  !contient(root / "x" / "y", root / "x"),
              "contient : dedans, le meme, pas un voisin au nom plus long, pas le parent");
        std::uintmax_t octets = 0;
        check(compterFichiers(a, &octets, 100) == 2 && octets == 3, "compterFichiers");
        check(tailleLisible(512) == "512 o" && tailleLisible(1536) == "1,5 Ko", "tailleLisible : " + tailleLisible(1536));
    }

    // ---- 5. le demarrage de la version installee ---------------------------------
    const fs::path exe = root / "Programme";
    const fs::path data = root / "Mes donn\xC3\xA9" "es" / "XPG";
    const fs::path reglages = root / "Roaming" / "XpgAnalyzer";
    {
        write(exe / "resources" / "schneider_library.txt", "blocs");
        write(exe / "resources" / "plc_catalog.txt", "catalogue");
        write(exe / "libs" / "index.txt", "index");
        write(exe / "libs" / "Macros" / "m.mac", "macro");
        // Rien d'installe : la version de developpement ne change rien.
        const Demarrage dev = preparer(exe, utf8De(reglages));
        check(!dev.installe && actif(Cle::Projets).empty() && fs::current_path() == cwdBefore,
              "sans installation.ini : rien ne change (dossier de travail, actif vide)");
        check(!changer({{Cle::Projets, (root / "x").string(), false}}, c).ecrit, "developpement : changer ne fait rien");

        write(exe / "installation.ini", "\xEF\xBB\xBF[Installation]\r\nversion = 1.8.0\r\n[Dossiers]\r\ndonnees = " + utf8De(data) + "\r\n");
        write(data / "libs" / "index.txt", "mon index");   // deja la : garde
        const Demarrage d = preparer(exe, utf8De(reglages));
        check(d.installe && d.messages.empty(), "installe : aucun message");
        check(fs::equivalent(fs::current_path(), data), "le dossier des donnees devient le dossier de travail");
        check(read(data / "resources" / "schneider_library.txt") == "blocs" && read(data / "resources" / "plc_catalog.txt") == "catalogue",
              "resources\\ amorce depuis le dossier du programme");
        check(read(data / "libs" / "index.txt") == "mon index" && !fs::exists(data / "libs" / "Macros" / "m.mac"),
              "une bibliotheque deja la (index.txt) n'est pas touchee");
        check(actif(Cle::Projets) == utf8De(data / "projets") && actif(Cle::Captures) == utf8De(data / "captures"),
              "actif : projets et captures sous le dossier des donnees");
        check(actuel().iniUtilisateur == utf8De(reglages / "XPGAnalyser.ini"), "le .ini de l'utilisateur, a cote des reglages");

        // ---- 6. changer ------------------------------------------------------------
        write(data / "projets" / "Armoire" / "project.xpgproj", "p");
        const fs::path autres = root / "Autres projets";
        Bilan b = changer({{Cle::Projets, utf8De(autres), true}}, c);
        check(b.ok && b.ecrit && !b.redemarrer, "changer les projets : ecrit, sans redemarrer");
        check(read(autres / "Armoire" / "project.xpgproj") == "p" && fs::exists(data / "projets" / "Armoire" / "project.xpgproj"),
              "copie faite, l'ancien dossier reste");
        check(actif(Cle::Projets) == utf8De(autres), "projets : tout de suite");
        const auto ini = lireIni(reglages / "XPGAnalyser.ini");
        check(ini && ini->valeur("Dossiers", "projets") == std::optional<std::string>(utf8De(autres)), "XPGAnalyser.ini ecrit");
        check(ini && ini->valeur("Dossiers", "donnees") == std::optional<std::string>(""), "les autres cles restent vides (le modele)");

        b = changer({{Cle::Bibliotheque, "{Donnees}\\Biblio", false}}, c);
        check(b.ok && b.redemarrer && actif(Cle::Bibliotheque) == utf8De(data / "libs"), "la bibliotheque : au prochain demarrage");
        check(fs::exists(reglages / "XPGAnalyser.ini.precedent"), "l'ancien XPGAnalyser.ini garde en .precedent");
        check(fs::is_directory(data / "Biblio"), "le nouveau dossier est cree");

        write(root / "un-fichier", "x");
        b = changer({{Cle::Captures, (root / "caps").string(), false}, {Cle::Projets, (root / "un-fichier").string(), false}}, c);
        check(!b.ok && !b.ecrit && actif(Cle::Captures) == utf8De(data / "captures"),
              "un dossier refuse (un fichier) : rien ne change, meme les autres");

        // Relire : XPGAnalyser.ini l'emporte maintenant.
        const Demarrage d2 = preparer(exe, utf8De(reglages));
        check(d2.installe && actif(Cle::Projets) == utf8De(autres) && actif(Cle::Bibliotheque) == utf8De(data / "Biblio"),
              "au demarrage suivant : les dossiers de XPGAnalyser.ini");
        check(read(data / "Biblio" / "index.txt") == "index", "la nouvelle bibliotheque, vide, est amorcee");
    }

    // ---- 7. 1.21.0 : le poids des fichiers de l'application (A propos) ------------
    arbreDesTailles(root / "tailles");

    fs::current_path(cwdBefore);
    fs::remove_all(root, ec);
    std::printf("%s (%d echec%s)\n", failures ? "DOSSIERS : ECHEC" : "DOSSIERS : OK", failures, failures > 1 ? "s" : "");
    return failures ? 1 : 0;
}
