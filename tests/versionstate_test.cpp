// tests/versionstate_test.cpp - lot API 6 : l'etat du projet et les versions.
//
//   versionstate_test
//
//  Ce que dit le bloc de la barre du haut (standing) et ce que font Terminer
//  et Livrer (closing), sur un vrai magasin de versions dans un dossier
//  temporaire : NEW sans version ("V1 a venir") ; Terminer cree la V1 validee
//  (FINISH, "V1 validee") ; modifier commence la V2 (DEV, "V2 en cours, depuis
//  V1") ; une version intermediaire (brouillon) laisse en DEV et avance la
//  suivante ; Livrer sans rien changer promeut la validee en livree (LOCK,
//  "V1 livree") ; Livrer apres un changement cree la version suivante, livree.
//  Les noms proposes, les dates dites en francais.
//
//  Le parcours complet dans l'application (la premiere modification, Ctrl+Z
//  jusqu'au bout qui rend FINISH, deverrouiller) est rejoue par la session 56.
#include "../src/hmi/HmiVersionState.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace ver = hmi::ver;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

bool contains(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines)
        if (l.find(part) != std::string::npos) return true;
    return false;
}

} // namespace

int main() {
    const auto dir = fs::temp_directory_path() / "xpg_versionstate_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    write(dir / "project.xpgproj", "formatVersion = 1\nname = Essai\nversion = 1.0\nstate = NEW\n");
    write(dir / "sections" / "Marche.st", "Moteur := Marche AND NOT Defaut;\n");
    write(dir / "vars" / "globals.txt", "Marche ; BOOL\nDefaut ; BOOL\nMoteur ; BOOL\n");

    auto store = ver::open(dir.string());
    check(store && store->versions.empty(), "un projet neuf : pas de version");
    if (!store) return 1;

    // ---- NEW ---------------------------------------------------------------
    {
        const auto s = ver::standing(project::State::New, &*store, 0, 0);
        check(s.title == "V1 \xC3\xA0 venir" && s.tone == "new" && s.current == 1 && s.last == 0, "NEW : \"V1 a venir\"");
        check(s.subtitle == "aucune version encore", "NEW : aucune version encore");
        check(contains(s.heading, "La premi\xC3\xA8re modification commence la V1"), "NEW : la premiere modification commence la V1");
    }
    // ---- DEV, la V1 en cours --------------------------------------------------
    {
        const auto s = ver::standing(project::State::Dev, &*store, 0, 2);
        check(s.title == "V1 en cours" && s.tone == "dev" && s.current == 1, "DEV sans version : \"V1 en cours\"");
        check(contains(s.heading, "2 modifications non enregistr\xC3\xA9" "es"), "DEV : les modifications non enregistrees se comptent");
    }
    // ---- Terminer : la V1 validee, FINISH ------------------------------------
    {
        const auto c = ver::closing(project::State::Dev, project::State::Finish, *store, true);
        check(!c.promote && c.number == 1 && c.versionState == ver::State::Validated, "Terminer : la V1 a creer, validee");
        check(ver::defaultName(project::State::Finish, "2026-09-28 10:05") == "Termin\xC3\xA9" "e le 28/09", "le nom propose : \"Terminee le 28/09\"");
        auto v1 = ver::create(*store, ver::defaultName(project::State::Finish, "2026-09-28 10:05"), c.versionState, "", "Quentin", "2026-09-28 10:05");
        check(v1 && v1->number == 1 && v1->state == ver::State::Validated, "la V1 est creee, validee");
        const auto s = ver::standing(project::State::Finish, &*store, 0, 0);
        check(s.title == "V1 valid\xC3\xA9" "e" && s.tone == "finish" && s.current == 1 && s.last == 1, "FINISH : \"V1 validee\"");
        check(s.subtitle == "termin\xC3\xA9" "e le 28/09 \xC3\xA0 10:05", "FINISH : terminee le 28/09 a 10:05");
        check(contains(s.heading, "commencera la V2"), "FINISH : la premiere modification commencera la V2");
        check(!ver::changedSinceLast(*store), "le projet est exactement la V1");
    }
    // ---- modifier : la V2 commence (DEV) ------------------------------------
    write(dir / "sections" / "Marche.st", "Moteur := Marche AND NOT Defaut AND NOT Arret;\n");
    {
        check(ver::changedSinceLast(*store), "une section changee : le disque differe de la V1");
        const auto s = ver::standing(project::State::Dev, &*store, 1, 0);
        check(s.title == "V2 en cours" && s.current == 2 && s.last == 1, "DEV : \"V2 en cours\"");
        check(s.subtitle == "depuis V1 \xC2\xB7 valid\xC3\xA9" "e", "DEV : \"depuis V1 . validee\"");
        check(contains(s.heading, "1 \xC3\xA9l\xC3\xA9ment chang\xC3\xA9 depuis la V1"), "DEV : 1 element change depuis la V1");
        check(contains(s.heading, "partie de la V1"), "DEV : partie de la V1");
    }
    // ---- Livrer apres un changement : une nouvelle version, livree ------------
    {
        const auto c = ver::closing(project::State::Dev, project::State::Lock, *store, true);
        check(!c.promote && c.number == 2 && c.versionState == ver::State::Delivered, "Livrer depuis DEV : la V2 a creer, livree");
        const auto f = ver::closing(project::State::Finish, project::State::Lock, *store, true);
        check(!f.promote && f.number == 2, "Livrer depuis FINISH mais le disque a change : une nouvelle version");
    }
    // ---- une version intermediaire : le projet reste en DEV -----------------
    {
        auto v2 = ver::create(*store, "Essai sur site", ver::State::Draft, "", "Quentin", "2026-09-28 11:00");
        check(v2 && v2->number == 2 && v2->state == ver::State::Draft, "la V2 intermediaire (brouillon)");
        const auto s = ver::standing(project::State::Dev, &*store, 0, 0);
        check(s.title == "V3 en cours" && s.subtitle == "depuis V2 \xC2\xB7 brouillon", "apres un brouillon : \"V3 en cours, depuis V2 . brouillon\"");
        // Un FINISH d'avant le lot 6, dont la derniere version est un brouillon : dit tel quel.
        const auto f = ver::standing(project::State::Finish, &*store, 0, 0);
        check(f.title == "FINISH" && f.subtitle == "derni\xC3\xA8re version : V2" && contains(f.heading, "commencera la V3"),
              "FINISH sur un brouillon : \"FINISH, derniere version : V2\"");
    }
    // ---- Terminer la V3, puis Livrer sans rien changer : la V3 promue ---------
    write(dir / "vars" / "globals.txt", "Marche ; BOOL\nDefaut ; BOOL\nMoteur ; BOOL\nArret ; BOOL\n");
    {
        const auto c = ver::closing(project::State::Dev, project::State::Finish, *store, true);
        auto v3 = ver::create(*store, "Mise en service", c.versionState, "", "Quentin", "2026-09-28 12:00");
        check(v3 && v3->number == 3 && v3->state == ver::State::Validated, "Terminer : la V3 validee");
        const auto l = ver::closing(project::State::Finish, project::State::Lock, *store, ver::changedSinceLast(*store));
        check(l.promote && l.number == 3 && l.versionState == ver::State::Delivered, "Livrer sans rien changer : la V3 est promue, livree");
        check(ver::defaultName(project::State::Lock, "2026-09-28 12:30") == "Livr\xC3\xA9" "e le 28/09", "le nom propose : \"Livree le 28/09\"");
        const auto* last = store->last();
        check(last && static_cast<bool>(ver::update(*store, l.number, last->name, l.versionState, "au client")), "la promotion s'enregistre");
        check(store->versions.size() == 3 && store->last()->state == ver::State::Delivered, "trois versions, la derniere livree - pas de quatrieme");
        const auto s = ver::standing(project::State::Lock, &*store, 0, 0);
        check(s.title == "V3 livr\xC3\xA9" "e" && s.tone == "lock" && s.current == 3, "LOCK : \"V3 livree\"");
        check(s.subtitle == "verrouill\xC3\xA9" "e \xC2\xB7 lecture seule", "LOCK : verrouillee, lecture seule");
        check(contains(s.heading, "commence la V4"), "LOCK : deverrouiller commence la V4");
        const auto again = ver::open(dir.string());
        check(again && again->versions.size() == 3 && again->last()->state == ver::State::Delivered && again->last()->comment == "au client",
              "relu du disque : la V3 livree, son commentaire");
    }
    // ---- deverrouiller : DEV, la V4 en cours --------------------------------
    {
        const auto s = ver::standing(project::State::Dev, &*store, 0, 0);
        check(s.title == "V4 en cours" && s.subtitle == "depuis V3 \xC2\xB7 livr\xC3\xA9" "e", "DEV apres LOCK : \"V4 en cours, depuis V3 . livree\"");
    }
    // ---- FINISH et LOCK d'avant les versions --------------------------------
    {
        ver::Store none;
        const auto f = ver::standing(project::State::Finish, &none, 0, 0);
        check(f.title == "FINISH" && f.subtitle == "sans version", "FINISH sans version : dit tel quel");
        const auto l = ver::standing(project::State::Lock, nullptr, 0, 0);
        check(l.title == "LOCK" && l.tone == "lock", "LOCK sans magasin : dit tel quel");
        const auto c = ver::closing(project::State::Finish, project::State::Lock, none, false);
        check(!c.promote && c.number == 1 && c.versionState == ver::State::Delivered, "Livrer un FINISH sans version : la V1, livree");
    }
    check(ver::whenText("2026-09-26 18:40") == "26/09 \xC3\xA0 18:40" && ver::whenText("hier") == "hier", "les dates : \"26/09 a 18:40\"");

    fs::remove_all(dir, ec);
    std::printf(failures ? "ECHEC : %d echec(s)\n" : "versionstate_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
