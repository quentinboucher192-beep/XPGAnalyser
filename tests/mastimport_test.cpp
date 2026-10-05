// tests/mastimport_test.cpp - lot 7 : importer un .XPG dans le projet ouvert.
//
//   mastimport_test <MAST.XPG>
//
//  Le plan du projet d'essai contre lui-meme : tout est garde, rien ne part.
//  Contre une copie ou une variable est renommee : une supprimee, une
//  nouvelle, et le lien de l'IHM vers l'ancien nom est perdu (celui vers une
//  variable gardee reste). Un champ de DDT renomme : le type change, son
//  instance est gardee (avec la note), le chemin de l'IHM qui passait par ce
//  champ est perdu. La fusion garde les lignes IHM des tables d'animation et
//  la documentation d'une variable identique ; la commande importe, Ctrl+Z
//  rend le nombre de variables d'avant, Ctrl+Y le refait.
#include "../src/app/BackgroundTasks.hpp"     // 1.11 (R111) : l'avis de la cloche
#include "../src/app/ConditionsNotice.hpp"
#include "../src/app/Settings.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ActivationConditions.hpp"
#include "../src/project/MastImport.hpp"
#include "../src/project/ProjectStore.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace mast = project::mast;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::size_t globals(const domain::Project& p) {
    std::size_t n = 0;
    for (const auto& v : p.variables) n += v.scope == domain::VariableScope::Global ? 1u : 0u;
    return n;
}

// La premiere globale d'un type elementaire (BOOL, INT...) : celle qu'on renomme.
domain::Index firstElementary(const domain::Project& p) {
    for (domain::Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        if (v.scope != domain::VariableScope::Global) continue;
        const auto t = p.strings.text(v.type.name);
        if (t == "BOOL" || t == "EBOOL" || t == "INT" || t == "REAL" || t == "DINT" || t == "WORD") return i;
    }
    return domain::kNoIndex;
}

const mast::Item* find(const mast::Plan& plan, mast::Status s, const std::string& name) {
    for (const auto& it : plan.items)
        if (it.status == s && it.name == name) return &it;
    return nullptr;
}

std::size_t countGroup(const mast::Plan& plan, mast::Status s, mast::Group g) {
    std::size_t n = 0;
    for (const auto& it : plan.items) n += it.status == s && it.group == g ? 1u : 0u;
    return n;
}

} // namespace

// 1.11 (R111, decision 6) : un projet comme Armoire_Gaz (importe avant la 1.8.0) -
// des sections de tache, une unite dont les sections ont une condition.
domain::Project conditionsProject(bool taskConditions) {
    domain::Project p;
    // une section de tache a son Pou de genre Section, comme ProjectStore::open le fait
    const auto section = [&](const char* name, domain::Index owner, const char* cond) {
        if (owner == domain::kNoIndex) {
            domain::Pou own;
            own.name = p.strings.intern(name);
            own.kind = domain::PouKind::Section;
            p.pous.push_back(std::move(own));
            owner = static_cast<domain::Index>(p.pous.size() - 1);
        }
        domain::Section s;
        s.name = p.strings.intern(name);
        s.task = p.strings.intern("MAST");
        s.owner = owner;
        if (cond) {
            s.activationCondition = p.strings.intern(cond);
            s.logicCondition = p.strings.intern("standard");
        }
        p.sections.push_back(std::move(s));
    };
    section("Init", domain::kNoIndex, nullptr);
    section("Reset_all", domain::kNoIndex, taskConditions ? "reset_all_configs" : nullptr);
    section("Modifications", domain::kNoIndex, taskConditions ? "ConfigArmoireUtilisee.configuree" : nullptr);
    domain::Pou unit;
    unit.name = p.strings.intern("Gestion_armoires");
    unit.kind = domain::PouKind::ProgramUnit;
    p.pous.push_back(std::move(unit));
    const auto u = static_cast<domain::Index>(p.pous.size() - 1);
    section("Distribution", u, "configuree");
    section("SFC_DEBUG", u, nullptr);
    return p;
}

void testMissingConditions() {
    std::printf("1b. Les conditions d'activation manquantes (projet importe avant la 1.8.0)\n");
    project::Manifest old;
    old.product = "Control Expert V15.3 - 230214C";
    old.created = "2026-09-27 03:33:45";
    const auto sans = conditionsProject(false);
    const auto r = project::missingTaskConditions(sans, old);
    check(r.suspected && r.sections.size() == 3 && r.sections[1] == "Reset_all",
          "importe le 27/09, aucune condition de tache : signale (" + std::to_string(r.sections.size()) + " sections)");
    check(r.unitConditions == 1, "la section d'unite avec sa condition est comptee");
    check(r.message.find("projet import\xC3\xA9 avant la 1.8.0") != std::string::npos
              && r.message.find("Fichier \xE2\x80\xBA Importer un .XPG (nouveau MAST)\xE2\x80\xA6") != std::string::npos,
          "le message dit pourquoi, et le remede");
    const auto avec = conditionsProject(true);
    check(!project::missingTaskConditions(avec, old).suspected, "les conditions retablies (comme le pupitre, indice B) : rien");
    project::Manifest recent = old;
    recent.created = "2026-09-30 10:12:00";
    check(!project::missingTaskConditions(sans, recent).suspected, "importe par la 1.8.0 ou apres : rien (le .XPG n'en avait pas)");
    project::Manifest made = old;
    made.product = "XpgAnalyzer 1.0";
    check(!project::missingTaskConditions(sans, made).suspected, "un projet cree ici, pas importe : rien");
    made.product.clear();
    check(!project::missingTaskConditions(sans, made).suspected, "sans product : rien");
    domain::Project none;
    check(!project::missingTaskConditions(none, old).suspected, "aucune section de tache : rien");
}

// 1.11 (R111, decisions 6 et 15) : ce que l'appli fait du signal - l'avis de la
// cloche (le bandeau haut le tient a jour chaque seconde), le meme message dans
// « Verifier l'ordre », « Ne plus le dire pour ce projet » (les reglages de
// l'utilisateur, pas le projet), et le reimport du .XPG qui rend les conditions.
void testConditionsNotice() {
    std::printf("1c. L'avis de la cloche, Verifier l'ordre, Ne plus le dire, le reimport\n");
    namespace cond = app::conditions;
    project::Manifest old;
    old.product = "Control Expert V15.3 - 230214C";
    old.created = "2026-09-27 03:33:45";
    auto doc = std::make_shared<domain::Project>(conditionsProject(false));
    app::Settings settings;                       // sans fichier : rien n'est ecrit
    const std::string folder = "/tmp/essai-conditions-111/Armoire_Gaz";
    const auto msg = cond::message(doc.get(), old, settings, folder);
    check(!msg.empty() && msg == project::missingTaskConditions(*doc, old).message, "le message pour ce projet : celui du signal");
    check(msg.find("Fichier \xE2\x80\xBA Importer un .XPG (nouveau MAST)\xE2\x80\xA6") != std::string::npos, "il dit le remede");
    check(cond::message(nullptr, old, settings, folder).empty(), "aucun projet ouvert : rien");
    check(cond::noticeText().empty(), "avant l'ouverture : aucun avis");
    check(cond::syncNotice(msg), "a l'ouverture, l'avis est pose (Verifier refait sa ligne d'aide)");   // le bandeau haut
    bool shown = false;
    for (const auto& n : app::bgtasks::notices())
        if (n.key == cond::kNoticeKey)
            shown = n.detail == msg && n.button == "Ne plus le dire pour ce projet" && n.action == cond::kSilenceAction
                 && n.tone == "warning" && n.title == "Conditions d'activation manquantes";
    check(shown, "l'avis de la cloche : le message, le bouton Ne plus le dire pour ce projet, en avertissement");
    check(cond::noticeText() == msg, "Verifier l'ordre dit le meme message");
    check(cond::shortText().find("Importer un .XPG (nouveau MAST)") != std::string::npos && cond::shortText().size() < msg.size(),
          "la ligne d'aide de Verifier : en court, avec le remede");
    const auto rev = app::bgtasks::revision();
    check(!cond::syncNotice(cond::message(doc.get(), old, settings, folder)), "le meme message : rien a refaire");
    check(app::bgtasks::revision() == rev, "la seconde d'apres, rien ne change : la cloche ne bouge pas");

    // Fichier > Importer un .XPG (nouveau MAST)... : la commande de l'import, avec
    // un .XPG qui porte les conditions de ses sections de tache.
    auto xpg = std::make_shared<const domain::Project>(conditionsProject(true));
    mast::ImportMastCommand cmd(doc, xpg, {}, "Armoire_Gaz.XPG");
    check(static_cast<bool>(cmd.execute()) && !project::missingTaskConditions(*doc, old).suspected,
          "le reimport rend les conditions : plus de signal");
    check(cond::syncNotice(cond::message(doc.get(), old, settings, folder)) && cond::noticeText().empty(),
          "l'avis est retire ; Verifier l'ordre n'en dit plus rien");
    check(static_cast<bool>(cmd.undo()) && project::missingTaskConditions(*doc, old).suspected, "Ctrl+Z : les conditions repartent");
    cond::syncNotice(cond::message(doc.get(), old, settings, folder));
    check(cond::noticeText() == msg, "Ctrl+Z : l'avis revient");

    // Le faux signal (un .XPG sans aucune condition) : Ne plus le dire pour ce projet.
    check(cond::silence(settings, folder + "/"), "Ne plus le dire pour ce projet");
    check(cond::noticeText().empty() && cond::shortText().empty(), "l'avis est retire tout de suite ; Verifier n'en dit plus rien");
    const auto tus = settings.getList(cond::kSilencedSetting);
    check(tus.size() == 1 && tus[0] == cond::folderKey(folder), "le dossier du projet est dans les reglages de l'utilisateur");
    check(cond::message(doc.get(), old, settings, folder).empty(), "ce projet : plus rien a dire");
    cond::syncNotice(cond::message(doc.get(), old, settings, folder));
    check(cond::noticeText().empty(), "la seconde d'apres : l'avis ne revient pas");
    (void)cond::silence(settings, folder);
    check(settings.getList(cond::kSilencedSetting).size() == 1, "deux fois : une seule ligne");
    check(!cond::message(doc.get(), old, settings, "/tmp/essai-conditions-111/Autre").empty(), "un autre projet reste signale");
    check(!cond::silence(settings, ""), "sans dossier (projet pas enregistre) : rien a retenir");

    // Les reglages de l'utilisateur sont ecrits tout de suite, et relus.
    const auto file = (std::filesystem::temp_directory_path() / "mastimport_test_conditions_111.txt").string();
    std::error_code ec;
    std::filesystem::remove(file, ec);
    {
        app::Settings s1;
        (void)s1.load(file);
        (void)cond::silence(s1, folder);
    }
    {
        app::Settings s2;
        check(s2.load(file) && cond::message(doc.get(), old, s2, folder).empty(), "relu des reglages : toujours tu");
    }
    std::filesystem::remove(file, ec);
}

int main(int argc, char** argv) {
    std::printf("1. Les etiquettes\n");
    check(std::string(mast::statusLabel(mast::Status::Removed)) == "Supprim\xC3\xA9", "l'onglet Supprime");
    check(std::string(mast::statusLabel(mast::Status::LinkLost)) == "Liens IHM perdus", "l'onglet Liens IHM perdus");
    check(std::string(mast::groupLabel(mast::Group::Instances)) == "Instances de DFB/DDT", "le groupe des instances");
    testMissingConditions();
    testConditionsNotice();

    if (argc < 2) {
        std::printf("       (sans MAST.XPG : rien d'autre a essayer)\n");
        std::printf(failures ? "ECHEC : %d echec(s)\n" : "mastimport_test : tout est bon\n", failures);
        return failures ? 1 : 0;
    }

    std::printf("2. Lire le .XPG sans toucher au bus de l'application\n");
    auto read = mast::read({argv[1]});
    check(static_cast<bool>(read) && *read != nullptr, "le projet d'essai se lit");
    if (!read || !*read) {
        std::printf("ECHEC : %d echec(s)\n", failures + 1);
        return 1;
    }
    const auto base = *read;
    check(mast::sniffFile(argv[1]) == 1, "sniffFile : un programme");
    check(!mast::read({std::string(argv[1]) + ".absent"}), "un fichier absent : refuse, avec sa raison");
    const auto g0 = globals(*base);
    check(g0 > 10, "des variables globales (" + std::to_string(g0) + ")");

    std::printf("3. Le projet contre lui-meme : tout est garde\n");
    {
        const auto plan = mast::makePlan(*base, *base, {}, {}, "MAST.XPG");
        check(plan.count(mast::Status::Removed) == 0, "rien de supprime");
        check(plan.count(mast::Status::Added) == 0, "rien de nouveau");
        check(plan.count(mast::Status::Changed) == 0, "rien de change");
        check(plan.count(mast::Status::LinkLost) == 0, "aucun lien perdu");
        check(plan.count(mast::Status::Kept) >= g0, "tout est garde (" + std::to_string(plan.count(mast::Status::Kept)) + " lignes)");
        check(plan.variablesBefore == g0 && plan.variablesAfter == g0, "les chiffres du bandeau");
        check(!plan.hardwareReplaced, "le materiel du projet reste (un .XPG n'a pas de racks)");
    }

    std::printf("4. Une variable renommee dans le .XPG\n");
    const auto target = firstElementary(*base);
    check(target != domain::kNoIndex, "une globale elementaire a renommer");
    if (target != domain::kNoIndex) {
        const std::string oldName(base->strings.text(base->variables[target].name));
        const std::string newName = oldName + "_renomme";
        auto renamed = std::make_shared<domain::Project>(*base);
        renamed->variables[target].name = renamed->strings.intern(newName);
        renamed->buildIndices();
        // Une autre globale, gardee : le lien de l'IHM vers elle reste.
        std::string keptName;
        for (const auto& v : base->variables)
            if (v.scope == domain::VariableScope::Global && base->strings.text(v.name) != oldName) {
                keptName = std::string(base->strings.text(v.name));
                break;
            }
        std::vector<mast::HmiRef> refs;
        refs.push_back({"Vue Vue_Essai", "Voyant", oldName, {}});
        refs.push_back({"Vue Vue_Essai", "Afficheur", keptName, {}});
        refs.push_back({"Scripts g\xC3\xA9n\xC3\xA9raux", "Script_Essai_Zz", "PasUneVariable", {}});     // pas un lien : ignore
        const auto plan = mast::makePlan(*base, *renamed, refs, {}, "MAST.XPG");
        const auto removed = countGroup(plan, mast::Status::Removed, mast::Group::Variables);
        const auto added = countGroup(plan, mast::Status::Added, mast::Group::Variables);
        check(removed == 1 && find(plan, mast::Status::Removed, oldName), "une supprimee : " + oldName);
        check(added == 1 && find(plan, mast::Status::Added, newName), "une nouvelle : " + newName);
        check(plan.count(mast::Status::LinkLost) == 1 && find(plan, mast::Status::LinkLost, "Voyant"), "le lien de Voyant est perdu");
        const auto* kept = find(plan, mast::Status::Kept, "Afficheur");
        check(kept != nullptr && kept->group == mast::Group::Hmi && kept->sub == "Vue Vue_Essai", "celui d'Afficheur reste (IHM > Vue Vue_Essai)");
        bool ignored = true;
        for (const auto& it : plan.items) ignored = ignored && it.name != "Script_Essai_Zz";
        check(ignored, "une reference qui n'est pas a l'automate n'est pas comptee");
        // Une variable IHM liee par l'adresse de la variable renommee (si elle en a une).
        const auto& address = base->variables[target].address.raw;
        if (!address.empty()) {
            const auto byAddress = mast::makePlan(*base, *renamed, {{"Variables IHM", "Copie_IHM", {}, address}}, {}, "MAST.XPG");
            const auto* changed = find(byAddress, mast::Status::Changed, "Copie_IHM");
            check(changed != nullptr, "une variable IHM liee a " + address + " lit maintenant " + newName);
        }
    }

    std::printf("5. Un champ de DDT renomme\n");
    {
        auto changed = std::make_shared<domain::Project>(*base);
        std::string typeName, fieldName, instance;
        for (const auto& d : changed->derivedTypes) {
            if (d.fields.empty()) continue;
            const std::string t(changed->strings.text(d.name));
            // Une instance globale de ce type (ou un tableau de ce type).
            for (const auto& v : changed->variables) {
                if (v.scope != domain::VariableScope::Global) continue;
                const std::string vt(changed->strings.text(v.type.name));
                if (vt == t || (vt.size() > t.size() && vt.compare(vt.size() - t.size(), t.size(), t) == 0 && vt.rfind("ARRAY", 0) == 0)) {
                    instance = std::string(changed->strings.text(v.name)) + (vt == t ? "" : "[0]");
                    break;
                }
            }
            if (instance.empty()) continue;
            typeName = t;
            fieldName = std::string(changed->strings.text(changed->variables[d.fields.front()].name));
            changed->variables[d.fields.front()].name = changed->strings.intern(fieldName + "_x");
            break;
        }
        check(!typeName.empty(), "un DDT et une instance (" + typeName + ", " + instance + ")");
        if (!typeName.empty()) {
            const std::string path = instance + "." + fieldName;
            std::string why;
            check(mast::pathExists(*base, path, &why), path + " existe dans le projet d'essai");
            why.clear();
            // L'appel d'abord : l'ordre d'evaluation des arguments n'est pas fixe.
            const bool gone = !mast::pathExists(*changed, path, &why);
            check(gone && !why.empty(), "et plus dans la copie : " + why);
            const auto plan = mast::makePlan(*base, *changed, {{"Alarmes", "Seuil", path, {}}}, {}, "MAST.XPG");
            const auto* type = find(plan, mast::Status::Changed, typeName);
            check(type != nullptr && type->group == mast::Group::Types, "le type " + typeName + " change" + (type ? " : " + type->detail : std::string{}));
            const auto root = instance.substr(0, instance.find('['));
            const auto* inst = find(plan, mast::Status::Kept, root);
            check(inst != nullptr && inst->group == mast::Group::Instances && inst->detail.find("change") != std::string::npos,
                  "l'instance " + root + " est gardee, avec la note" + (inst ? " (" + inst->detail + ")" : std::string{}));
            check(find(plan, mast::Status::LinkLost, "Seuil") != nullptr, "le chemin de l'alarme par ce champ est perdu");
        }
    }

    std::printf("6. La fusion et la commande\n");
    if (target != domain::kNoIndex) {
        // Le projet ouvert : le projet d'essai, avec une ligne IHM dans une table
        // et un commentaire sur une variable que le .XPG garde identique.
        auto doc = std::make_shared<domain::Project>(*base);
        if (doc->animationTables.empty()) {
            domain::AnimationTable t;
            t.name = doc->strings.intern("Essai");
            doc->animationTables.push_back(std::move(t));
        }
        doc->animationTables.front().entries.push_back({doc->strings.intern("Compteur_IHM"), true});
        domain::Index documented = domain::kNoIndex;
        for (domain::Index i = 0; i < doc->variables.size(); ++i)
            if (doc->variables[i].scope == domain::VariableScope::Global && i != target) { documented = i; break; }
        const std::string docName(doc->strings.text(doc->variables[documented].name));
        doc->variables[documented].comment = doc->strings.intern("Documente dans le projet");

        // Le .XPG : la variable renommee, et une globale de plus.
        auto xpg = std::make_shared<domain::Project>(*base);
        const std::string oldName(base->strings.text(base->variables[target].name));
        xpg->variables[target].name = xpg->strings.intern(oldName + "_renomme");
        domain::Variable extra;
        extra.name = xpg->strings.intern("Nouvelle_Variable_Essai");
        extra.type.name = xpg->strings.intern("INT");
        extra.type.klass = domain::TypeClass::Elementary;
        extra.scope = domain::VariableScope::Global;
        xpg->variables.push_back(extra);
        xpg->linkTypes();

        const auto before = doc->variables.size();
        const auto tables = doc->animationTables.size();
        mast::ImportMastCommand cmd(doc, xpg, mast::Options{true}, "MAST.XPG");
        check(static_cast<bool>(cmd.execute()), "l'import se fait");
        check(doc->variables.size() == xpg->variables.size(), "les variables du .XPG (" + std::to_string(doc->variables.size()) + ")");
        bool hmiLine = false;
        for (const auto& t : doc->animationTables)
            for (const auto& e : t.entries) hmiLine = hmiLine || (e.hmi && doc->strings.text(e.name) == "Compteur_IHM");
        check(hmiLine && doc->animationTables.size() >= tables, "la ligne IHM de la table d'animation reste");
        bool oldLine = false;
        for (const auto& t : doc->animationTables)
            for (const auto& e : t.entries) oldLine = oldLine || (!e.hmi && doc->strings.text(e.name) == oldName);
        check(!oldLine, "une ligne de la variable disparue ne reste pas");
        std::string comment;
        for (const auto& v : doc->variables)
            if (v.scope == domain::VariableScope::Global && doc->strings.text(v.name) == docName) comment = std::string(doc->strings.text(v.comment));
        check(comment == "Documente dans le projet", "la variable identique garde son commentaire du projet");
        std::string why;
        check(!mast::pathExists(*doc, oldName, &why) && mast::pathExists(*doc, "Nouvelle_Variable_Essai"), "le nom disparu n'est plus, le nouveau est la");
        check(cmd.label().find("MAST.XPG") != std::string::npos, "le libelle de l'historique : " + cmd.label());
        check(static_cast<bool>(cmd.undo()), "Ctrl+Z");
        check(doc->variables.size() == before, "Ctrl+Z rend le nombre de variables d'avant (" + std::to_string(before) + ")");
        check(mast::pathExists(*doc, oldName), "et l'ancien nom");
        check(static_cast<bool>(cmd.execute()) && doc->variables.size() == xpg->variables.size(), "Ctrl+Y refait l'import");

        // Decoche : tout vient du .XPG (le commentaire, les tables).
        auto doc2 = std::make_shared<domain::Project>(*base);
        doc2->variables[documented].comment = doc2->strings.intern("Documente dans le projet");
        const auto merged = mast::merge(*doc2, *xpg, mast::Options{false});
        std::string comment2 = "?";
        for (const auto& v : merged.variables)
            if (v.scope == domain::VariableScope::Global && merged.strings.text(v.name) == docName) comment2 = std::string(merged.strings.text(v.comment));
        check(comment2 != "Documente dans le projet", "decoche : le commentaire du .XPG");
        check(merged.animationTables.size() == xpg->animationTables.size(), "decoche : les tables du .XPG");
    }

    std::printf("7. L'ordre d'execution d'une tache\n");
    {
        auto swapped = std::make_shared<domain::Project>(*base);
        domain::Task* mast = nullptr;
        for (auto& t : swapped->tasks)
            if (t.sections.size() >= 2) { mast = &t; break; }
        check(mast != nullptr, "une tache a deux sections au moins");
        if (mast) {
            std::swap(mast->sections[0], mast->sections[1]);
            const std::string task(swapped->strings.text(mast->name));
            const auto plan = mast::makePlan(*base, *swapped, {}, {}, "MAST.XPG");
            const auto* it = find(plan, mast::Status::Changed, task);
            check(it != nullptr && it->detail.find("ordre") != std::string::npos,
                  "deux sections permutees : la tache " + task + " change (" + (it ? it->detail : std::string("?")) + ")");
        }
    }

    std::printf("8. Le .XPG et son .XHW, deposes ensemble\n");
    if (argc < 3) {
        std::printf("       (sans CONFIG.XHW : rien d'essaye ici)\n");
    } else {
        check(!mast::read({argv[2]}), "un .XHW seul n'est pas un MAST : refuse");
        check(mast::sniffFile(argv[2]) == 2, "sniffFile : une configuration");
        auto both = mast::read({argv[1], argv[2]});
        check(static_cast<bool>(both) && *both && !(*both)->hardware.racks.empty(), "les deux se lisent ensemble, racks compris");
        if (both && *both) {
            const auto plan = mast::makePlan(*base, **both, {}, {}, "MAST.XPG + CONFIG.XHW");
            check(plan.hardwareReplaced && countGroup(plan, mast::Status::Changed, mast::Group::Hardware) >= 1,
                  "le recapitulatif dit que le materiel est remplace");
            check(countGroup(plan, mast::Status::Removed, mast::Group::Variables) == 0, "le programme, lui, est le meme");
            const auto merged = mast::merge(*base, **both, {});
            bool notice = false;
            for (const auto& n : merged.partialDataNotices) notice = notice || n.find("Rack and module layout") != std::string::npos;
            check(!merged.hardware.racks.empty() && !notice, "la fusion prend les racks du .XHW (et oublie l'avis des racks manquants)");
            const auto kept = mast::merge(**both, *base, {});
            check(!kept.hardware.racks.empty(), "un .XPG seul garde les racks du projet");
        }
    }

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "mastimport_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
